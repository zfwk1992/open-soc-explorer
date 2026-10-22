#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""Validate, summarise and replay Open SoC Explorer traces (JSON Lines).

The trace format is specified in docs/TRACE_PROTOCOL.md. This tool uses only
the Python standard library so that traces can be checked and analysed
without building the C++ application.

Usage:
    osx_trace.py validate TRACE.jsonl
    osx_trace.py summary  TRACE.jsonl [--json]
    osx_trace.py pipeline TRACE.jsonl [--first N] [--last N]
"""

import argparse
import json
import re
import sys
from collections import Counter, OrderedDict

SCHEMA_MAJOR = "0"

EVENT_TYPES = {
    "state.snapshot", "instruction.fetch", "instruction.decode",
    "instruction.retire", "pipeline.stage_enter", "pipeline.stall",
    "pipeline.flush", "register.write", "memory.request", "memory.response",
    "cache.hit", "cache.miss", "bus.transfer", "dma.start", "dma.complete",
    "accelerator.dispatch", "accelerator.complete", "interrupt.raise",
}
OBSERVABILITY = {"direct", "model-derived", "state-diff", "inferred",
                 "isa-decoded"}
HEX = re.compile(r"^0x[0-9a-f]+$")
INSTANCE = re.compile(r"^inst-\d+$")


class TraceError(Exception):
    pass


def load(path):
    """Returns (header, events, footer) of a trace file."""
    header, footer, events = None, None, []
    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as e:
                raise TraceError(f"line {lineno}: invalid JSON: {e}") from e
            version = str(record.get("schema_version", ""))
            if version.split(".")[0] != SCHEMA_MAJOR:
                raise TraceError(
                    f"line {lineno}: unsupported schema version '{version}'")
            kind = record.get("record")
            if kind == "header":
                if header is not None:
                    raise TraceError(f"line {lineno}: second header")
                header = record
            elif kind == "event":
                if header is None:
                    raise TraceError(f"line {lineno}: event before header")
                if footer is not None:
                    raise TraceError(f"line {lineno}: event after footer")
                events.append(record)
            elif kind == "footer":
                footer = record
            else:
                raise TraceError(f"line {lineno}: unknown record '{kind}'")
    if header is None:
        raise TraceError("missing header record")
    return header, events, footer


def validate(header, events, footer):
    """Returns a list of problems (empty if the trace is valid)."""
    problems = []
    run_id = header.get("run_id")
    stages = {s["component"] for s in header["processor"]["stages"]}
    seen_sequences = set()
    last_sequence, last_cycle = -1, None
    for ev in events:
        seq = ev.get("sequence")
        where = f"event {seq}"
        if ev.get("run_id") != run_id:
            problems.append(f"{where}: run_id differs from header")
        if not isinstance(seq, int) or seq <= last_sequence:
            problems.append(f"{where}: sequence not strictly increasing")
        last_sequence = seq if isinstance(seq, int) else last_sequence
        seen_sequences.add(seq)

        time = ev.get("time", {})
        cycle = time.get("cycle")
        if "domain" not in time or not isinstance(cycle, int):
            problems.append(f"{where}: missing time.domain/time.cycle")
        elif last_cycle is not None and cycle < last_cycle:
            problems.append(f"{where}: cycle decreases")
        else:
            last_cycle = cycle

        body = ev.get("event", {})
        etype = body.get("type")
        if etype not in EVENT_TYPES:
            problems.append(f"{where}: unknown event type '{etype}'")
        if ev.get("observability") not in OBSERVABILITY:
            problems.append(f"{where}: unknown observability")
        inst = body.get("instruction_id")
        if inst is not None and not INSTANCE.match(str(inst)):
            problems.append(f"{where}: malformed instruction_id '{inst}'")
        pc = body.get("pc")
        if pc is not None and not HEX.match(str(pc)):
            problems.append(f"{where}: malformed pc '{pc}'")
        state = ev.get("state", {})
        if "architectural" not in state or "speculative" not in state:
            problems.append(f"{where}: missing state fields")
        cause = ev.get("correlation", {}).get("cause_sequence")
        if cause is not None and cause not in seen_sequences:
            problems.append(f"{where}: cause_sequence {cause} does not refer "
                            "to an earlier event")
        if etype in ("pipeline.stage_enter", "instruction.fetch") and \
                body.get("component") not in stages:
            problems.append(f"{where}: unknown stage component "
                            f"'{body.get('component')}'")
    if footer is None:
        problems.append("missing footer record (trace incomplete)")
    else:
        if footer.get("run_id") != run_id:
            problems.append("footer: run_id differs from header")
        if footer.get("summary", {}).get("events") != len(events):
            problems.append("footer: summary.events does not match")
    return problems


def replay(header, events):
    """Reconstructs instruction instances and their stage occupancy.

    Mirrors Ripes::Explorer::TraceReplay: an instance occupies a stage from its
    stage_enter cycle until the cycle before its next stage_enter, retirement
    or squash.
    """
    stage_names = {s["component"]: s["name"]
                   for s in header["processor"]["stages"]}
    instances = OrderedDict()
    for ev in events:
        body = ev["event"]
        inst = body.get("instruction_id")
        if inst is None:
            continue
        rec = instances.setdefault(inst, {
            "assembly": body.get("assembly") or "", "pc": body.get("pc"),
            "entries": [], "held": set(), "end": None, "outcome": "in-flight"})
        cycle = ev["time"]["cycle"]
        etype = body["type"]
        payload = body.get("payload", {})
        if etype == "pipeline.stage_enter":
            rec["entries"].append((cycle, stage_names.get(body["component"],
                                                          "?")))
        elif etype == "pipeline.stall" and payload.get("kind") == "held":
            rec["held"].add(cycle)
        elif etype == "instruction.retire":
            rec["outcome"], rec["end"] = "retired", cycle
        elif etype == "pipeline.flush" and payload.get("kind") == "squash":
            rec["outcome"], rec["end"] = "squashed", cycle
    return instances


def summary(header, events, footer):
    instances = replay(header, events)
    counts = Counter(ev["event"]["type"] for ev in events)
    stall_cycles = {
        ev["time"]["cycle"] for ev in events
        if ev["event"]["type"] == "pipeline.stall" and (
            ev["event"]["payload"].get("kind") == "held" or
            ev["event"]["payload"].get("origin") == "inserted")}
    squashed = sum(1 for i in instances.values() if i["outcome"] == "squashed")
    retired = sum(1 for i in instances.values() if i["outcome"] == "retired")
    first = header["initial_state"]["cycle"]
    last = max([ev["time"]["cycle"] for ev in events] + [first])
    if footer:
        last = max(last, footer["completeness"]["end_cycle"])
    cycles = last - first
    src = header["source"]
    return OrderedDict([
        ("engine", f"{src['engine']} {src.get('engine_version', '')}".strip()),
        ("model", src["model"]),
        ("fidelity", src["fidelity"]),
        ("complete", bool(footer and footer["completeness"]["complete"])),
        ("cycles", cycles),
        ("instructions_retired", retired),
        ("cpi", round(cycles / retired, 4) if retired else None),
        ("stall_cycles", len(stall_cycles)),
        ("squashed_instructions", squashed),
        ("memory_reads", sum(
            1 for ev in events if ev["event"]["type"] == "memory.request" and
            ev["event"]["payload"].get("access") == "read")),
        ("memory_writes", sum(
            1 for ev in events if ev["event"]["type"] == "memory.request" and
            ev["event"]["payload"].get("access") == "write")),
        ("events_by_type", OrderedDict(sorted(counts.items()))),
    ])


def pipeline_diagram(header, events, first=None, last=None):
    """Returns an ASCII pipeline diagram (rows: instances, columns: cycles)."""
    instances = replay(header, events)
    lo = header["initial_state"]["cycle"]
    hi = max([ev["time"]["cycle"] for ev in events] + [lo])
    lo = lo if first is None else max(lo, first)
    hi = hi if last is None else min(hi, last)
    width = max([len(f"{k}  {v['assembly']}") for k, v in instances.items()]
                + [10])
    lines = [" " * width + " " +
             "".join(f"{c:>5}" for c in range(lo, hi + 1))]
    for name, rec in instances.items():
        cells = []
        for c in range(lo, hi + 1):
            cell = ""
            for k, (start, stage) in enumerate(rec["entries"]):
                stop = (rec["entries"][k + 1][0] - 1
                        if k + 1 < len(rec["entries"])
                        else (rec["end"] - 1 if rec["end"] is not None
                              else hi))
                if start <= c <= stop:
                    cell = stage + ("*" if c in rec["held"] else "")
            if not cell and c == rec["end"]:
                cell = "ret" if rec["outcome"] == "retired" else "X"
            cells.append(f"{cell:>5}")
        lines.append(f"{name + '  ' + rec['assembly']:<{width}} " +
                     "".join(cells))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("validate", "summary", "pipeline"):
        p = sub.add_parser(name)
        p.add_argument("trace")
        if name == "summary":
            p.add_argument("--json", action="store_true")
        if name == "pipeline":
            p.add_argument("--first", type=int)
            p.add_argument("--last", type=int)
    args = parser.parse_args(argv)
    try:
        header, events, footer = load(args.trace)
    except (OSError, TraceError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    if args.command == "validate":
        problems = validate(header, events, footer)
        for p in problems:
            print(p)
        print(f"{len(events)} events, {len(problems)} problem(s)")
        return 1 if problems else 0
    if args.command == "summary":
        s = summary(header, events, footer)
        if args.json:
            print(json.dumps(s, indent=2))
        else:
            for k, v in s.items():
                if k != "events_by_type":
                    print(f"{k:24} {v}")
            for k, v in s["events_by_type"].items():
                print(f"  {k:22} {v}")
        return 0
    print(pipeline_diagram(header, events, args.first, args.last))
    return 0


if __name__ == "__main__":
    sys.exit(main())
