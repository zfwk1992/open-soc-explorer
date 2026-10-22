#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""Convert gem5 output into an Open SoC Explorer trace (JSON Lines).

Inputs (from one gem5 run, see tools/gem5/README.md):
  * the debug output file written with --debug-file, containing Exec records
    (flags ExecEnable,ExecUser,ExecOpClass,ExecResult,ExecEffAddr,ExecFetchSeq)
    and, for O3CPU, O3PipeView records;
  * config.json and stats.txt from the gem5 output directory;
  * optionally the simulated ELF, to attach instruction words.

What is converted, and how it is labelled (docs/TRACE_PROTOCOL.md):
  * O3CPU: per dynamic instruction (gem5 sequence number) the fetch, decode,
    rename, dispatch, issue and complete ticks and the commit tick from
    O3PipeView -> instruction.fetch / pipeline.stage_enter /
    instruction.decode / instruction.retire (observability "direct"). Instructions with a commit
    tick of 0 never committed -> pipeline.flush kind=squash, placed one cycle
    after their last recorded stage (observability "inferred": O3PipeView
    does not record the squash time).
  * TimingSimpleCPU / AtomicSimpleCPU: gem5 reports the tick at which an
    instruction started executing. One stage "EXEC" is entered at that tick
    ("direct"); completion is inferred from the start of the next instruction
    ("inferred"; valid because these CPUs execute one instruction at a time).
  * Results (D=) and effective addresses (A=) from Exec records ->
    register.write (destination register taken from gem5's disassembly) and
    memory.request (address as reported at commit; the access time is not in
    the trace).
  * config.json + stats.txt -> header "system": simulated components with
    parameters, end-of-simulation statistics and port connections.

Time: time.tick holds gem5 ticks; time.cycle = tick // clock period of the
CPU's clock domain (both reported in the header). gem5 ticks are never
mixed with Ripes cycles.

Usage:
  gem5_to_osx.py --trace m5out/trace.out --m5out m5out \
                 [--binary prog.elf] [--gem5-version v26.0.0.0] -o out.jsonl
"""

import argparse
import datetime
import gzip
import hashlib
import json
import math
import os
import re
import struct
import sys
from collections import OrderedDict

SCHEMA_VERSION = "0.2.0"
ADAPTER_VERSION = "0.1.0"

GPR_NAMES = [f"x{i}" for i in range(32)]
GPR_ALIASES = ["zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1",
               "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3",
               "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4",
               "t5", "t6"]
REG_INDEX = {n: i for i, n in enumerate(GPR_NAMES)}
REG_INDEX.update({n: i for i, n in enumerate(GPR_ALIASES)})
REG_INDEX["fp"] = 8

# O3PipeView record name -> stage name shown in the Explorer.
O3_STAGES = [("fetch", "F"), ("decode", "DEC"), ("rename", "REN"),
             ("dispatch", "DIS"), ("issue", "ISS"), ("complete", "CMP")]

# Mnemonics (gem5 RISC-V disassembly) without a destination register.
NO_RD_MNEMONICS = {
    "beq", "bne", "blt", "bge", "bltu", "bgeu", "c.beqz", "c.bnez",
    "j", "jr", "ret", "c.j", "c.jr", "ecall", "ebreak", "c.ebreak",
    "fence", "fence.i", "fence.tso", "sfence.vma", "wfi", "mret", "sret",
    "nop", "c.nop", "unimp"}
# Opcodes (bits 6:0) of 32-bit instructions without a destination register.
NO_RD_OPCODES = {0x23, 0x27, 0x63, 0x0F}  # STORE, STORE-FP, BRANCH, MISC-MEM
OPCODE_SYSTEM = 0x73
OPCODES_FP_RD = {0x07, 0x43, 0x47, 0x4B, 0x4F, 0x53}  # may write an FPR
STORE_MNEMONICS = {"sb", "sh", "sw", "sd", "fsw", "fsd", "c.sw", "c.sd",
                   "c.swsp", "c.sdsp", "c.fsd", "c.fsdsp"}
ACCESS_BYTES = {"lb": 1, "lbu": 1, "sb": 1, "lh": 2, "lhu": 2, "sh": 2,
                "lw": 4, "lwu": 4, "sw": 4, "flw": 4, "fsw": 4, "c.lw": 4,
                "c.sw": 4, "c.lwsp": 4, "c.swsp": 4, "ld": 8, "sd": 8,
                "fld": 8, "fsd": 8, "c.ld": 8, "c.sd": 8, "c.ldsp": 8,
                "c.sdsp": 8, "c.fld": 8, "c.fsd": 8, "c.fldsp": 8,
                "c.fsdsp": 8}


class ConversionError(Exception):
    pass


def open_text(path):
    """Opens a text input; files ending in .gz are decompressed."""
    if path.endswith(".gz"):
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return open(path, encoding="utf-8", errors="replace")


def find_input(directory, name):
    """Path of <name> or <name>.gz in a gem5 output directory."""
    for candidate in (name, name + ".gz"):
        path = os.path.join(directory, candidate)
        if os.path.exists(path):
            return path
    raise ConversionError(f"{directory}: no {name} (or {name}.gz)")


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

EXEC_RE = re.compile(r"^\s*(\d+):\s+(\S+):\s+(.*)$")
# "%#x" PC, optional " @symbol[+off]", then ".%2d" micro-PC or three spaces.
PC_RE = re.compile(r"^(0x[0-9a-fA-F]+)(?:\s+@\S+)?(?:\.\s*(\d+))?\s*$")


def parse_exec_line(line):
    """Parses one gem5 Exec record (src/cpu/exetrace.cc, traceInst).

    Returns None for other lines.
    """
    m = EXEC_RE.match(line)
    if not m:
        return None
    tick, obj, rest = int(m.group(1)), m.group(2), m.group(3)
    parts = [p.strip() for p in rest.split(" : ")]
    if parts and re.match(r"^T\d+$", parts[0]):
        parts = parts[1:]
    if len(parts) < 2:
        return None
    pc = PC_RE.match(parts[0])
    if not pc:
        return None
    rec = {"tick": tick, "object": obj, "pc": int(pc.group(1), 16),
           "micro_pc": None if pc.group(2) is None else int(pc.group(2)),
           "disasm": parts[1], "opclass": None, "result": None,
           "address": None, "fetch_seq": None}
    tail = " ".join(parts[2:])
    if len(parts) >= 3 and re.match(r"^[A-Za-z]\w*$", parts[2]):
        rec["opclass"] = parts[2]
    for key, value in re.findall(r"(?:^|\s)(D|A|FetchSeq)=(\S+)", tail):
        if key == "D":
            try:
                rec["result"] = int(value, 16)
            except ValueError:  # vector/blob results
                rec["result"] = None
        elif key == "A":
            rec["address"] = int(value, 16)
        elif key == "FetchSeq":
            rec["fetch_seq"] = int(value)
    return rec


def parse_trace_file(path):
    """Returns (exec_records, o3_records) from a gem5 debug output file."""
    execs, o3 = [], []
    current = None
    with open_text(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("O3PipeView:"):
                fields = line.split(":")
                stage = fields[1]
                if stage == "fetch":
                    current = {"fetch": int(fields[2]),
                               "pc": int(fields[3], 16),
                               "upc": int(fields[4]),
                               "seq": int(fields[5]),
                               "disasm": ":".join(fields[6:]).strip()}
                    o3.append(current)
                elif current is not None and stage == "retire":
                    current["retire"] = int(fields[2])
                    if len(fields) >= 5 and fields[3] == "store":
                        current["store"] = int(fields[4])
                elif current is not None:
                    current[stage] = int(fields[2])
                continue
            rec = parse_exec_line(line)
            if rec is not None:
                execs.append(rec)
    return execs, o3


def parse_stats(path, with_descriptions=False):
    """Returns {stat name: value} of the first statistics dump.

    With with_descriptions, returns ({name: value}, {name: description}),
    the description being gem5's comment for the statistic. Values that are
    not finite numbers (gem5 prints "nan" for 0/0) are left out.
    """
    stats, descriptions = OrderedDict(), OrderedDict()
    with open_text(path) as f:
        started = False
        for line in f:
            if line.startswith("---------- Begin Simulation Statistics"):
                if started:
                    break
                started = True
                continue
            if line.startswith("---------- End Simulation Statistics"):
                break
            parts = line.split()
            if len(parts) < 2 or line.startswith(" "):
                continue
            try:
                value = float(parts[1])
            except ValueError:
                continue
            if not math.isfinite(value):
                continue
            stats[parts[0]] = int(value) if value.is_integer() else value
            if "#" in line:
                descriptions[parts[0]] = line.split("#", 1)[1].strip()
    return (stats, descriptions) if with_descriptions else stats


def read_elf_text(path):
    """Returns [(vaddr, bytes)] of the executable sections of an ELF64 LE."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ConversionError(f"{path}: not a little-endian ELF64 file")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x3A)
    sections = []
    for i in range(shnum):
        base = shoff + i * shentsize
        (_, sh_type, sh_flags, sh_addr, sh_offset, sh_size) = \
            struct.unpack_from("<IIQQQQ", data, base)
        if sh_type == 1 and sh_flags & 0x4:  # PROGBITS, SHF_EXECINSTR
            sections.append((sh_addr, data[sh_offset:sh_offset + sh_size]))
    return sections


def read_elf_entry(path):
    with open(path, "rb") as f:
        header = f.read(0x20)
    if header[:4] != b"\x7fELF" or header[4] != 2:
        raise ConversionError(f"{path}: not an ELF64 file")
    return struct.unpack_from("<Q", header, 0x18)[0]


def instruction_word(sections, pc):
    for addr, blob in sections:
        if addr <= pc < addr + len(blob):
            off = pc - addr
            low = struct.unpack_from("<H", blob, off)[0]
            if low & 0b11 != 0b11:
                return low, 16
            if off + 4 <= len(blob):
                return struct.unpack_from("<I", blob, off)[0], 32
    return None, None


# ---------------------------------------------------------------------------
# config.json helpers
# ---------------------------------------------------------------------------

def walk_objects(node, out):
    """Collects every SimObject dict (has 'type' and 'path') of config.json."""
    if isinstance(node, dict):
        if "type" in node and "path" in node:
            out.append(node)
        for value in node.values():
            walk_objects(value, out)
    elif isinstance(node, list):
        for value in node:
            walk_objects(value, out)
    return out


def find_cpu(objects):
    cpus = [o for o in objects if str(o.get("type", "")).endswith("CPU")
            and "CheckerCPU" not in o["type"]]
    if not cpus:
        raise ConversionError("config.json: no CPU object found")
    return cpus[0]


def clock_period(objects, cpu):
    """Clock period (ticks) of the CPU's clock domain."""
    by_path = {o["path"]: o for o in objects}
    domain = cpu.get("clk_domain")
    if isinstance(domain, str):
        dom = by_path.get(domain)
        if dom and dom.get("clock"):
            return int(dom["clock"][0]), domain
    raise ConversionError("config.json: cannot determine the CPU clock period")


ROLE_BY_TYPE = [
    (re.compile(r"CPU$"), "cpu"),
    (re.compile(r"Prefetcher$"), "prefetcher"),
    (re.compile(r"Cache$"), "cache"),
    (re.compile(r"XBar$|Bus$"), "interconnect"),
    (re.compile(r"MemCtrl$"), "memory-controller"),
    (re.compile(r"(DDR|LPDDR|HBM|DRAM|SimpleMemory|NVM)"), "memory"),
]

PARAMETER_KEYS = {
    "cpu": ["fetchWidth", "decodeWidth", "renameWidth", "dispatchWidth",
            "issueWidth", "commitWidth", "numROBEntries", "numIQEntries",
            "LQEntries", "SQEntries", "numPhysIntRegs"],
    "cache": ["size", "assoc", "tag_latency", "data_latency",
              "response_latency", "mshrs", "tgts_per_mshr", "writeback_clean"],
    "interconnect": ["width", "frontend_latency", "forward_latency",
                     "response_latency"],
    "memory-controller": ["write_buffer_size", "read_buffer_size",
                          "mem_sched_policy"],
    "memory": ["device_size", "ranks_per_channel", "banks_per_rank", "tCK",
               "tCL", "tRCD", "tRP", "range"],
    "prefetcher": ["degree", "distance", "latency", "table_entries",
                   "queue_size", "on_miss", "on_read", "on_write"],
}

# End-of-simulation statistics reported per role, as names relative to the
# component (gem5 stats.txt "<path>.<name>").
# Meaning and unit of the reported parameters (gem5 config.json stores
# Latency/Clock parameters in ticks, Cycles parameters in cycles of the
# object's clock domain, MemorySize parameters in bytes).
PARAMETER_INFO = {
    "fetchWidth": "instructions fetched per cycle",
    "decodeWidth": "instructions decoded per cycle",
    "renameWidth": "instructions renamed per cycle",
    "dispatchWidth": "instructions dispatched per cycle",
    "issueWidth": "instructions issued per cycle",
    "commitWidth": "instructions committed per cycle",
    "numROBEntries": "reorder buffer entries",
    "numIQEntries": "instruction queue entries",
    "LQEntries": "load queue entries",
    "SQEntries": "store queue entries",
    "numPhysIntRegs": "physical integer registers",
    "size": "capacity in bytes",
    "assoc": "associativity (ways)",
    "tag_latency": "tag lookup latency in cycles",
    "data_latency": "data access latency in cycles",
    "response_latency": "latency in cycles",
    "mshrs": "miss status holding registers (outstanding misses)",
    "tgts_per_mshr": "requests that can wait on one MSHR",
    "writeback_clean": "clean lines are written back on eviction",
    "width": "bytes transferred per cycle",
    "frontend_latency": "request front-end latency in cycles",
    "forward_latency": "forwarding latency in cycles",
    "write_buffer_size": "write queue entries",
    "read_buffer_size": "read queue entries",
    "mem_sched_policy": "scheduling policy (frfcfs: first-ready, "
                        "first-come-first-served)",
    "device_size": "size of one DRAM device in bytes",
    "ranks_per_channel": "ranks per channel",
    "banks_per_rank": "banks per rank",
    "tCK": "clock period in ps (gem5 ticks)",
    "tCL": "CAS latency in ps",
    "tRCD": "RAS-to-CAS delay in ps",
    "tRP": "row precharge time in ps",
    "range": "address range (bytes)",
    "degree": "prefetches issued per detected stride",
    "distance": "how many strides ahead the first prefetch is",
    "latency": "latency in cycles",
    "table_entries": "entries of the stride detection table",
    "queue_size": "prefetch queue entries",
    "on_miss": "trains only on cache misses",
    "on_read": "trains on reads",
    "on_write": "trains on writes",
}

STAT_PATTERNS = {
    "cpu": re.compile(r"numCycles|ipc|cpi|committedInsts|numInsts|numOps|"
                      r"commitStats0\.(numInsts|numOps|ipc|cpi)|"
                      r"commit\.(branchMispredicts|commitSquashedInsts)|"
                      r"branchPred\.(lookups|condIncorrect)\w*"),
    "cache": re.compile(r"(demandHits|demandMisses|demandAccesses|"
                        r"demandMissRate|demandAvgMissLatency|"
                        r"overallHits|overallMisses|overallMissRate)::total|"
                        r"writebacks::total"),
    "interconnect": re.compile(r"(pktCount|pktSize)::total|transDist::\w+"),
    "memory-controller": re.compile(r"(readReqs|writeReqs|readBursts|"
                                    r"writeBursts|avgRdBWSys|avgWrBWSys|"
                                    r"avgQLat|avgMemAccLat)"),
    "memory": re.compile(r"(bytesRead|bytesWritten)::total|avgRdBW|avgWrBW|"
                         r"readRowHits|writeRowHits|readRowHitRate|"
                         r"writeRowHitRate"),
    "prefetcher": re.compile(r"pfIssued|pfUseful|pfLate|accuracy|coverage"),
}

ROLE_DESCRIPTIONS = {
    "cpu": "CPU core model simulated by gem5.",
    "cache": "Cache simulated by gem5 (classic memory system).",
    "interconnect": "Crossbar/bus simulated by gem5; carries the packets "
                    "between the connected components.",
    "memory-controller": "Memory controller simulated by gem5 (request "
                         "queues, scheduling, DRAM timing).",
    "memory": "Memory device/interface simulated by gem5.",
    "prefetcher": "Hardware prefetcher simulated by gem5; it issues "
                  "prefetch requests into the cache it belongs to.",
}


def role_of(obj):
    for regex, role in ROLE_BY_TYPE:
        if regex.search(str(obj.get("type", ""))):
            return role
    return None


def owner_component(port_path, paths):
    """Closest ancestor of a port path that is a SimObject path."""
    owner = port_path.rsplit(".", 1)[0]
    while owner and owner not in paths:
        owner = owner.rsplit(".", 1)[0] if "." in owner else ""
    return owner


def describe_system(objects, stats, descriptions=None):
    """Simulated components with parameters, statistics and connections.

    Only objects with a hardware role (CPU, cache, crossbar, memory
    controller, memory) are reported. Connections follow the requestor ports
    of each component to the component that owns the peer port; ports of
    objects without a role (e.g. a CPU's MMU walkers) are attributed to the
    nearest reported ancestor.
    """
    descriptions = descriptions or {}
    by_path = {o["path"]: o for o in objects}
    roles = {path: role_of(obj) for path, obj in by_path.items()}
    reported = {path for path, role in roles.items() if role is not None}

    def reported_ancestor(path):
        while path and path not in reported:
            path = path.rsplit(".", 1)[0] if "." in path else ""
        return path

    connections = {path: set() for path in reported}
    for obj in objects:
        source = reported_ancestor(obj["path"])
        if not source:
            continue
        for value in obj.values():
            if not (isinstance(value, dict) and
                    value.get("role") == "GEM5 REQUESTOR"):
                continue
            peers = value.get("peer")
            for peer in peers if isinstance(peers, list) else [peers]:
                if not peer:
                    continue
                target = reported_ancestor(owner_component(str(peer),
                                                           by_path))
                if target and target != source:
                    connections[source].add(target)
        # A prefetcher is a child of its cache and issues requests into it.
        if roles.get(obj["path"]) == "prefetcher":
            parent = reported_ancestor(obj["path"].rsplit(".", 1)[0])
            if parent:
                connections[obj["path"]].add(parent)

    components = []
    for path in sorted(reported):
        obj, role = by_path[path], roles[path]
        params = OrderedDict()
        for key in PARAMETER_KEYS.get(role, []):
            if key in obj:
                value = obj[key]
                params[key] = (", ".join(str(v) for v in value)
                               if isinstance(value, list) else value)
        prefix = path + "."
        pattern = STAT_PATTERNS.get(role)
        component_stats = OrderedDict(
            (name[len(prefix):], value) for name, value in stats.items()
            if pattern and name.startswith(prefix) and
            pattern.fullmatch(name[len(prefix):]))
        components.append(OrderedDict([
            ("path", path), ("type", obj["type"]), ("role", role),
            ("description", ROLE_DESCRIPTIONS.get(role, "")),
            ("parameters", params),
            ("parameter_info", OrderedDict(
                (k, PARAMETER_INFO[k]) for k in params
                if k in PARAMETER_INFO)),
            ("statistics", component_stats),
            ("statistic_info", OrderedDict(
                (k, descriptions[prefix + k]) for k in component_stats
                if prefix + k in descriptions)),
            ("connections", sorted(connections[path]))]))
    return components


# ---------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------

def file_sha256(path):
    """SHA-256 of a file's (decompressed, for .gz) contents."""
    digest = hashlib.sha256()
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def hexs(value, bits=64):
    return "0x" + format(value & ((1 << bits) - 1), "0%dx" % (bits // 4))


def destination_register(disasm, word=None, bits=None):
    """Index of the integer destination register, or None.

    Decoded from the instruction word when it is known (32-bit encodings),
    otherwise from gem5's RISC-V disassembly ("<mnemonic> <rd>, ...").
    """
    if word is not None and bits == 32:
        opcode = word & 0x7F
        if opcode in NO_RD_OPCODES or opcode in OPCODES_FP_RD:
            return None
        if opcode == OPCODE_SYSTEM and (word >> 12) & 0x7 == 0:
            return None  # ecall, ebreak, mret, wfi, ...
        return (word >> 7) & 0x1F
    text = disasm.strip()
    mnemonic = text.split()[0] if text else ""
    if not mnemonic or mnemonic in NO_RD_MNEMONICS or \
            mnemonic in STORE_MNEMONICS or mnemonic.startswith("f"):
        return None
    first = text[len(mnemonic):].split(",")[0].strip()
    return REG_INDEX.get(first)


def is_store(disasm):
    return disasm.split()[0] in STORE_MNEMONICS if disasm.split() else False


class Builder:
    def __init__(self, period, run_id):
        self.period = period
        self.run_id = run_id
        self.events = []
        self.source_short = None

    def event(self, tick, etype, component, inst=None, pc=None, asm=None,
              payload=None, observability="direct", architectural=False,
              cause=None, source_ref=""):
        self.events.append({
            "tick": tick, "type": etype, "component": component,
            "instruction_id": None if inst is None else f"inst-{inst}",
            "pc": None if pc is None else hexs(pc),
            "assembly": asm, "payload": payload or {},
            "observability": observability, "architectural": architectural,
            "cause": cause, "source_ref": source_ref})
        return len(self.events) - 1


def convert(trace_path, m5out, binary=None, gem5_version="unknown",
            created_utc=None):
    execs, o3 = parse_trace_file(trace_path)
    if not execs and not o3:
        raise ConversionError(f"{trace_path}: no Exec or O3PipeView records")
    with open_text(find_input(m5out, "config.json")) as f:
        config = json.load(f)
    objects = walk_objects(config, [])
    stats, stat_descriptions = parse_stats(find_input(m5out, "stats.txt"),
                                           with_descriptions=True)
    cpu = find_cpu(objects)
    cpu_type = cpu["type"]
    period, domain_path = clock_period(objects, cpu)
    sections = read_elf_text(binary) if binary else []
    cpu_path = cpu["path"]

    # gem5 v26 type names: BaseO3CPU, BaseTimingSimpleCPU,
    # BaseAtomicSimpleCPU, BaseMinorCPU.
    is_o3 = "O3" in cpu_type
    is_atomic = "Atomic" in cpu_type
    if "Minor" in cpu_type:
        raise ConversionError(
            "MinorCPU is not supported yet: its Exec records carry the decode "
            "tick and no per-stage times (MinorTrace is not parsed).")
    if is_o3 and not o3:
        raise ConversionError("O3CPU run without O3PipeView records; enable "
                              "the O3PipeView debug flag")

    b = Builder(period, None)
    ref_cpu = {"o3": "src/cpu/o3/dyn_inst.cc#O3PipeView",
               "exec": "src/cpu/exetrace.cc#traceInst",
               "simple": "src/cpu/simple/base.cc#preExecute"}
    sim_ticks = int(stats.get("simTicks", 0))
    exec_by_seq = {r["fetch_seq"]: r for r in execs
                   if r["fetch_seq"] is not None}

    def word_payload(pc):
        word, bits = instruction_word(sections, pc)
        return {"word": hexs(word, bits)} if word is not None else {}

    def results(rec, inst, tick, retire_event):
        """register.write / memory.request derived from an Exec record."""
        if rec is None:
            return
        word, bits = instruction_word(sections, rec["pc"])
        idx = destination_register(rec["disasm"], word, bits)
        if idx and rec["result"] is not None:
            b.event(tick, "register.write", "soc.cpu0.regfile.gpr", inst,
                    rec["pc"], rec["disasm"],
                    {"regfile": "gpr", "index": idx, "name": GPR_NAMES[idx],
                     "alias": GPR_ALIASES[idx], "value": hexs(rec["result"]),
                     "attribution": "gem5-exec-result",
                     "register_from": "instruction word" if bits == 32
                     else "gem5 disassembly"},
                    observability="direct", architectural=True,
                    cause=retire_event, source_ref=ref_cpu["exec"])
        if rec["address"] is not None:
            payload = {"access": "write" if is_store(rec["disasm"])
                       else "read",
                       "address": hexs(rec["address"]),
                       "timing": "effective address reported by the gem5 "
                                 "Exec record; the access time is not in the "
                                 "trace (the event is placed at retirement)"}
            mnemonic = rec["disasm"].split()[0] if rec["disasm"] else ""
            if mnemonic in ACCESS_BYTES:
                payload["bytes"] = ACCESS_BYTES[mnemonic]
            b.event(tick, "memory.request", "soc.cpu0.lsu", inst, rec["pc"],
                    rec["disasm"], payload, observability="direct",
                    cause=retire_event, source_ref=ref_cpu["exec"])

    stages = []
    if is_o3:
        stages = [{"lane": 0, "index": i, "name": short,
                   "component": f"soc.cpu0.o3.{name}"}
                  for i, (name, short) in enumerate(O3_STAGES)]
        for r in sorted(o3, key=lambda r: r["seq"]):
            seq, pc, asm = r["seq"], r["pc"], r["disasm"]
            b.event(r["fetch"], "instruction.fetch", "soc.cpu0.o3.fetch", seq,
                    pc, asm, dict(stage="F", gem5_seq=seq, **word_payload(pc)),
                    source_ref=ref_cpu["o3"])
            last_tick = r["fetch"]
            for name, short in O3_STAGES:
                tick = r.get(name, 0)
                if tick:
                    b.event(tick, "pipeline.stage_enter",
                            f"soc.cpu0.o3.{name}", seq, pc, asm,
                            {"stage": short}, source_ref=ref_cpu["o3"])
                    last_tick = max(last_tick, tick)
                    if name == "decode":
                        b.event(tick, "instruction.decode",
                                "soc.cpu0.o3.decode", seq, pc, asm,
                                dict(stage=short, **word_payload(pc)),
                                source_ref=ref_cpu["o3"])
            retire = r.get("retire", 0)
            if retire:
                payload = {"stage": "commit"}
                if r.get("store"):
                    payload["store_tick"] = r["store"]
                ri = b.event(retire, "instruction.retire",
                             "soc.cpu0.o3.commit", seq, pc, asm, payload,
                             architectural=True, source_ref=ref_cpu["o3"])
                results(exec_by_seq.get(seq), seq, retire, ri)
            else:
                b.event(last_tick + period, "pipeline.flush",
                        "soc.cpu0.o3.fetch", seq, pc, asm,
                        {"kind": "squash",
                         "reason": "never committed (O3PipeView commit tick "
                                   "0); squash time not recorded, placed one "
                                   "cycle after the last recorded stage"},
                        observability="inferred", source_ref=ref_cpu["o3"])
    else:
        stages = [{"lane": 0, "index": 0, "name": "EXEC",
                   "component": "soc.cpu0.exec"}]
        for i, rec in enumerate(execs):
            inst = rec["fetch_seq"] if rec["fetch_seq"] is not None else i + 1
            # BaseSimpleCPU::preExecute decodes the instruction and creates
            # the trace record in the same tick.
            b.event(rec["tick"], "instruction.decode", "soc.cpu0.exec", inst,
                    rec["pc"], rec["disasm"],
                    dict(stage="EXEC", **word_payload(rec["pc"])),
                    source_ref=ref_cpu["simple"])
            b.event(rec["tick"], "pipeline.stage_enter", "soc.cpu0.exec", inst,
                    rec["pc"], rec["disasm"],
                    {"stage": "EXEC",
                     "note": "decoded and started executing (gem5 Exec "
                             "record tick)"},
                    source_ref=ref_cpu["simple"])
            if i + 1 < len(execs):
                end = execs[i + 1]["tick"]
                note = ("completed at or before this tick: the next "
                        "instruction started here (one instruction at a time; "
                        "the interval includes the next instruction's fetch)")
            else:
                end = max(sim_ticks, rec["tick"] + period)
                note = ("last instruction; completed at or before the end of "
                        "the simulation")
            ri = b.event(end, "instruction.retire", "soc.cpu0.exec", inst,
                         rec["pc"], rec["disasm"],
                         {"stage": "EXEC", "note": note},
                         observability="inferred", architectural=True,
                         source_ref=ref_cpu["simple"])
            results(rec, inst, end, ri)

    # Order by tick, keep producer order for equal ticks; assign sequences.
    order = sorted(range(len(b.events)),
                   key=lambda i: (b.events[i]["tick"], i))
    new_seq = {old: new for new, old in enumerate(order)}
    # gem5 starts simulating at tick 0; the time before the first event
    # (e.g. the first instruction fetch missing in the caches) belongs to
    # the run.
    first_cycle = 0
    last_tick = max(e["tick"] for e in b.events)
    sim_ticks = max(sim_ticks, last_tick)

    # Retirement index in commit order (gem5 commits in program order).
    retire_count = 0
    for old in order:
        e = b.events[old]
        if e["type"] == "instruction.retire":
            retire_count += 1
            e["payload"]["retire_index"] = retire_count

    run_id = "run-gem5-" + file_sha256(trace_path)[:16]
    source_short = {"engine": "gem5", "model": cpu_type,
                    "fidelity": "functional-iss" if is_atomic
                    else "cycle-level-simulator"}
    fidelity_note = (
        f"gem5 {cpu_type} in syscall-emulation mode with the configuration "
        "recorded in config.json (see the System view). gem5 is a "
        "cycle-level architectural simulator; this CPU model is not "
        "validated against a specific silicon implementation."
        if not is_atomic else
        f"gem5 {cpu_type}: atomic memory accesses, no pipeline or "
        "memory timing.")

    header = OrderedDict([
        ("record", "header"), ("schema_version", SCHEMA_VERSION),
        ("run_id", run_id),
        ("created_utc", created_utc or datetime.datetime.now(
            datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")),
        ("source", OrderedDict([
            ("engine", "gem5"), ("engine_version", gem5_version),
            ("adapter", "osx-gem5-offline"),
            ("adapter_version", ADAPTER_VERSION),
            ("model", cpu_type), ("model_name", f"gem5 {cpu_type}"),
            ("isa", "RV64"), ("extensions", []),
            ("fidelity", "cycle-level-simulator"
             if not is_atomic else "functional-iss"),
            ("fidelity_note", fidelity_note),
            ("source_file", "gem5: src/cpu/o3/" if is_o3 else
             "gem5: src/cpu/simple/")])),
        ("time_domains", [OrderedDict([
            ("name", "cpu_clk"), ("unit", "cycle"),
            ("description", f"gem5 clock domain {domain_path} of {cpu_path}: "
                            f"one cycle = {period} ticks (1 tick = 1 ps)"),
            ("ticks_per_cycle", period), ("tick_unit", "ps")])]),
        ("program", OrderedDict([
            ("entry_point", hexs(read_elf_entry(binary) if binary else
                                 min((r["pc"] for r in execs), default=0))),
            ("text_start", hexs(min((a for a, _ in sections), default=0))),
            ("text_size", sum(len(s) for _, s in sections)),
            ("image_sha256", file_sha256(binary) if binary else "")])),
        ("processor", OrderedDict([
            ("xlen", 64), ("stages", stages),
            ("register_files", [OrderedDict([("name", "gpr"),
                                             ("names", GPR_NAMES),
                                             ("aliases", GPR_ALIASES)])])])),
        ("initial_state", OrderedDict([
            ("cycle", first_cycle),
            ("pc", hexs(execs[0]["pc"] if execs else
                        min(r["pc"] for r in o3))),
            ("registers", {})])),
        ("system", describe_system(objects, stats, stat_descriptions)),
    ])

    events_out = []
    for old in order:
        e = b.events[old]
        events_out.append(OrderedDict([
            ("record", "event"), ("schema_version", SCHEMA_VERSION),
            ("run_id", run_id), ("sequence", new_seq[old]),
            ("source", source_short),
            ("time", OrderedDict([("domain", "cpu_clk"),
                                  ("cycle", e["tick"] // period),
                                  ("tick", e["tick"])])),
            ("event", OrderedDict([
                ("type", e["type"]), ("component", e["component"]),
                ("instruction_id", e["instruction_id"]), ("pc", e["pc"]),
                ("assembly", e["assembly"]), ("payload", e["payload"])])),
            ("correlation", {"cause_sequence": None if e["cause"] is None
                             else new_seq[e["cause"]]}),
            ("observability", e["observability"]),
            ("state", {"architectural": e["architectural"],
                       "speculative": None}),
            ("source_ref", "gem5: " + e["source_ref"])]))

    # speculative flag: squashed -> true, retired -> false
    outcome = {}
    for ev in events_out:
        body = ev["event"]
        if body["type"] == "instruction.retire":
            outcome[body["instruction_id"]] = False
        elif body["type"] == "pipeline.flush" and \
                body["payload"].get("kind") == "squash":
            outcome[body["instruction_id"]] = True
    for ev in events_out:
        inst = ev["event"]["instruction_id"]
        ev["state"]["speculative"] = outcome.get(inst) if inst else False

    retired = sum(1 for e in events_out
                  if e["event"]["type"] == "instruction.retire")
    caveats = [
        "Converted offline from gem5 debug output; see "
        "tools/gem5/gem5_to_osx.py for the mapping.",
        "Initial register values are not part of gem5 Exec traces; register "
        "views show values only once they are written.",
        "Destination registers of register.write events are decoded from the "
        "instruction word (ELF given) or taken from gem5's disassembly; "
        "only integer registers are reported.",
    ]
    if not is_o3:
        caveats.append("Instruction completion times are inferred from the "
                       "start of the next instruction (upper bound).")
    if any(r.get("retire", 1) == 0 for r in o3):
        caveats.append("Squash times of instructions that never committed "
                       "are not recorded by O3PipeView and are inferred.")
    footer = OrderedDict([
        ("record", "footer"), ("schema_version", SCHEMA_VERSION),
        ("run_id", run_id),
        ("completeness", OrderedDict([
            ("complete", True), ("starts_at_reset", True),
            ("start_cycle", first_cycle),
            ("end_cycle", sim_ticks // period),
            ("truncated", False), ("truncation_reason", None),
            ("program_finished", True), ("caveats", caveats)])),
        ("summary", OrderedDict([
            ("cycles", sim_ticks // period - first_cycle),
            ("events", len(events_out)),
            ("instructions_retired", retired)]))])
    return [header] + events_out + [footer]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--trace", required=True,
                        help="gem5 --debug-file output")
    parser.add_argument("--m5out", required=True,
                        help="gem5 output directory (config.json, stats.txt)")
    parser.add_argument("--binary", help="simulated ELF (instruction words)")
    parser.add_argument("--gem5-version", default="unknown")
    parser.add_argument("--created-utc",
                        help="timestamp for the header (default: now)")
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args(argv)
    try:
        records = convert(args.trace, args.m5out, args.binary,
                          args.gem5_version, args.created_utc)
    except (OSError, ConversionError, json.JSONDecodeError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    with open(args.output, "w", encoding="utf-8") as f:
        for rec in records:
            f.write(json.dumps(rec) + "\n")
    print(f"wrote {len(records) - 2} events to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
