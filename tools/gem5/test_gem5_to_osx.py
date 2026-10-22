# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer. Run with:  python3 -m unittest discover tools/gem5
#
# The test data in testdata/ is real gem5 v26.0.0.0 output (see
# testdata/README.md). Set OSX_RIPES_BIN to a Ripes binary to also compare
# the gem5 runs with Ripes.
import copy
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "trace"))

import gem5_to_osx  # noqa: E402
import osx_trace  # noqa: E402

TESTDATA = os.path.join(HERE, "testdata")
needs_testdata = unittest.skipUnless(
    os.path.isdir(TESTDATA), "recorded gem5 output (testdata/) not present")
WORKLOADS = os.path.join(HERE, "workloads")
CREATED = "2026-01-01T00:00:00Z"


def convert(name, binary="loop_sum.elf"):
    """Converts testdata/<name> and returns (header, events, footer)."""
    directory = os.path.join(TESTDATA, name)
    records = gem5_to_osx.convert(
        os.path.join(directory, "trace.out.gz"), directory,
        os.path.join(TESTDATA, binary) if binary else None, "v26.0.0.0",
        CREATED)
    return records[0], records[1:-1], records[-1]


def of_type(events, etype):
    return [e for e in events if e["event"]["type"] == etype]


def retired_pcs(events):
    return [int(e["event"]["pc"], 16) for e in of_type(events,
                                                       "instruction.retire")]


def ripes_events_assembly(events):
    return [e["event"]["assembly"]
            for e in of_type(events, "instruction.retire")]


def final_registers(events):
    regs = {}
    for e in of_type(events, "register.write"):
        regs[e["event"]["payload"]["name"]] = int(
            e["event"]["payload"]["value"], 16)
    return regs


class ParserTest(unittest.TestCase):
    def test_exec_line_formats(self):
        # Formats produced by src/cpu/exetrace.cc (ExeTracerRecord::traceInst)
        rec = gem5_to_osx.parse_exec_line(
            "   1500: board.processor.cores.core: 0x100b4    : "
            "addi a1, zero, 10         : IntAlu :  D=0x000000000000000a"
            "  FetchSeq=2")
        self.assertEqual(rec["tick"], 1500)
        self.assertEqual(rec["pc"], 0x100b4)
        self.assertEqual(rec["disasm"], "addi a1, zero, 10")
        self.assertEqual(rec["opclass"], "IntAlu")
        self.assertEqual(rec["result"], 10)
        self.assertEqual(rec["fetch_seq"], 2)
        self.assertIsNone(rec["micro_pc"])

        rec = gem5_to_osx.parse_exec_line(
            "  7000: cpu: T0 : 0x100c0 @_start+16. 1 : sw a1, 0(a0) : "
            "MemWrite :  D=0x0000000000000007 A=0x7ffffff0")
        self.assertEqual(rec["pc"], 0x100c0)
        self.assertEqual(rec["micro_pc"], 1)
        self.assertEqual(rec["address"], 0x7ffffff0)

        self.assertIsNone(gem5_to_osx.parse_exec_line("O3PipeView:issue:5"))
        self.assertIsNone(gem5_to_osx.parse_exec_line("warn: something"))

    def test_destination_register(self):
        dst = gem5_to_osx.destination_register
        # Decoded from the word when it is known.
        self.assertEqual(dst("", 0x40208033, 32), 0)       # sub x0, x1, x2
        self.assertEqual(dst("", 0x002081b3, 32), 3)       # add x3, x1, x2
        self.assertIsNone(dst("", 0x00b52223, 32))         # sw
        self.assertIsNone(dst("", 0xfe059ae3, 32))         # bne
        self.assertIsNone(dst("", 0x00000073, 32))         # ecall
        self.assertEqual(dst("", 0xc0002573, 32), 10)      # csrr a0, cycle
        # Otherwise from the disassembly.
        self.assertEqual(dst("sub a0, a1, a2"), 10)
        self.assertEqual(dst("slli t0, t0, 2"), 5)
        self.assertIsNone(dst("sd ra, 8(sp)"))
        self.assertIsNone(dst("bne a1, zero, -12"))
        self.assertIsNone(dst("ecall"))
        self.assertIsNone(dst("fadd.d fa0, fa1, fa2"))


@needs_testdata
class O3TraceTest(unittest.TestCase):
    """loop_sum on O3CPU with private L1 + L2 caches."""

    @classmethod
    def setUpClass(cls):
        cls.header, cls.events, cls.footer = convert("loop_sum_o3_l1l2")

    def test_valid(self):
        self.assertEqual(osx_trace.validate(self.header, self.events,
                                            self.footer), [])

    def test_header(self):
        src = self.header["source"]
        self.assertEqual(src["engine"], "gem5")
        self.assertEqual(src["engine_version"], "v26.0.0.0")
        self.assertEqual(src["model"], "BaseO3CPU")  # gem5 v26 type name
        self.assertEqual(src["fidelity"], "cycle-level-simulator")
        domain = self.header["time_domains"][0]
        self.assertEqual(domain["ticks_per_cycle"], 1000)  # 1 GHz
        self.assertEqual(domain["tick_unit"], "ps")
        self.assertEqual([s["name"] for s in
                          self.header["processor"]["stages"]],
                         ["F", "DEC", "REN", "DIS", "ISS", "CMP"])
        self.assertEqual(self.header["initial_state"]["registers"], {})

    def test_architectural_result(self):
        # Same program as Mission 3. gem5 commits 35 instructions
        # (stats.txt: commitStats0.numInsts 35): the exit ecall ends the
        # simulation and is not counted. Ripes also retires the ecall (36).
        self.assertEqual(len(retired_pcs(self.events)), 35)
        regs = final_registers(self.events)
        self.assertEqual(regs["x10"], 55)
        self.assertEqual(regs["x11"], 0)
        self.assertEqual(regs["x17"], 93)

    def test_ticks_and_cycles(self):
        for e in self.events:
            self.assertEqual(e["time"]["cycle"], e["time"]["tick"] // 1000)

    def test_stage_order_per_instruction(self):
        stage_index = {s["component"]: s["index"]
                       for s in self.header["processor"]["stages"]}
        entries = {}
        for e in of_type(self.events, "pipeline.stage_enter"):
            entries.setdefault(e["event"]["instruction_id"], []).append(
                (e["time"]["tick"], stage_index[e["event"]["component"]]))
        for inst, seq in entries.items():
            self.assertEqual(seq, sorted(seq), inst)

    def test_commit_in_program_order(self):
        retires = of_type(self.events, "instruction.retire")
        ids = [int(e["event"]["instruction_id"][5:]) for e in retires]
        self.assertEqual(ids, sorted(ids))
        self.assertEqual([e["event"]["payload"]["retire_index"]
                          for e in retires], list(range(1, len(retires) + 1)))

    def test_superscalar_commit(self):
        # O3CPU commits several instructions in one cycle.
        cycles = [e["time"]["cycle"]
                  for e in of_type(self.events, "instruction.retire")]
        self.assertLess(len(set(cycles)), len(cycles))

    def test_wrong_path_instructions_are_squashed(self):
        squashes = [e for e in of_type(self.events, "pipeline.flush")
                    if e["event"]["payload"]["kind"] == "squash"]
        self.assertGreater(len(squashes), 0)
        for e in squashes:
            self.assertEqual(e["observability"], "inferred")
            self.assertTrue(e["state"]["speculative"])

    def test_system_description(self):
        system = {c["path"]: c for c in self.header["system"]}
        roles = sorted(c["role"] for c in system.values())
        self.assertIn("cpu", roles)
        self.assertEqual(roles.count("cache"), 3)  # L1I, L1D, L2
        # gem5's stdlib hierarchy gives every cache a stride prefetcher.
        self.assertEqual(roles.count("prefetcher"), 3)
        self.assertIn("interconnect", roles)
        self.assertIn("memory-controller", roles)
        for comp in system.values():
            for target in comp["connections"]:
                self.assertIn(target, system)
        cpu = next(c for c in system.values() if c["role"] == "cpu")
        self.assertEqual(cpu["type"], "BaseO3CPU")
        self.assertEqual(cpu["parameters"]["numROBEntries"], 192)
        self.assertEqual(cpu["statistics"]["commitStats0.numInsts"], 35)
        self.assertIn("committed", cpu["statistic_info"]["commitStats0.numInsts"])
        l1i = system["board.cache_hierarchy.l1i-cache-0"]
        self.assertEqual(l1i["parameters"]["size"], 16384)
        self.assertEqual(l1i["connections"], ["board.cache_hierarchy.l2buses"])
        self.assertEqual(
            system["board.cache_hierarchy.l1i-cache-0.prefetcher"]
            ["connections"], ["board.cache_hierarchy.l1i-cache-0"])
        # No statistic is "nan" (gem5 prints it for 0/0); JSON has no NaN.
        json.dumps(self.header, allow_nan=False)

    def test_statistics_come_from_stats_txt(self):
        stats = gem5_to_osx.parse_stats(
            os.path.join(TESTDATA, "loop_sum_o3_l1l2", "stats.txt.gz"))
        for comp in self.header["system"]:
            for name, value in comp["statistics"].items():
                self.assertEqual(stats[comp["path"] + "." + name], value)

    def test_deterministic(self):
        again = convert("loop_sum_o3_l1l2")
        self.assertEqual(json.dumps([self.header] + self.events +
                                    [self.footer]),
                         json.dumps([again[0]] + again[1] + [again[2]]))


@needs_testdata
class TimingTraceTest(unittest.TestCase):
    """loop_sum on TimingSimpleCPU with private L1 caches."""

    @classmethod
    def setUpClass(cls):
        cls.header, cls.events, cls.footer = convert("loop_sum_timing_l1")
        cls.o3 = convert("loop_sum_o3_l1l2")

    def test_valid(self):
        self.assertEqual(osx_trace.validate(self.header, self.events,
                                            self.footer), [])

    def test_same_instruction_stream_as_o3(self):
        self.assertEqual(retired_pcs(self.events), retired_pcs(self.o3[1]))
        self.assertEqual(final_registers(self.events),
                         final_registers(self.o3[1]))

    def test_one_instruction_at_a_time(self):
        self.assertEqual(len(self.header["processor"]["stages"]), 1)
        retires = of_type(self.events, "instruction.retire")
        for e in retires:
            self.assertEqual(e["observability"], "inferred")
        cycles = [e["time"]["cycle"] for e in retires]
        self.assertEqual(len(set(cycles)), len(cycles))

    def test_fidelity(self):
        self.assertEqual(self.header["source"]["model"], "BaseTimingSimpleCPU")
        self.assertEqual(self.header["source"]["fidelity"],
                         "cycle-level-simulator")


@needs_testdata
class ErrorTest(unittest.TestCase):
    def test_rejects_o3_without_pipeview(self):
        directory = os.path.join(TESTDATA, "loop_sum_o3_l1l2")
        with tempfile.TemporaryDirectory() as tmp:
            trace = os.path.join(tmp, "trace.out")
            with gem5_to_osx.open_text(
                    os.path.join(directory, "trace.out.gz")) as f, \
                    open(trace, "w") as out:
                out.writelines(line for line in f
                               if not line.startswith("O3PipeView"))
            with self.assertRaises(gem5_to_osx.ConversionError):
                gem5_to_osx.convert(trace, directory)

    def test_rejects_empty_trace(self):
        directory = os.path.join(TESTDATA, "loop_sum_o3_l1l2")
        with tempfile.TemporaryDirectory() as tmp:
            trace = os.path.join(tmp, "trace.out")
            open(trace, "w").close()
            with self.assertRaises(gem5_to_osx.ConversionError):
                gem5_to_osx.convert(trace, directory)


@unittest.skipUnless(os.environ.get("OSX_RIPES_BIN"),
                     "set OSX_RIPES_BIN to compare with Ripes")
class RipesComparisonTest(unittest.TestCase):
    """The same source on Ripes' ISS retires the same instruction stream."""

    def test_same_stream_and_result(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "ripes.jsonl")
            subprocess.run(
                [os.environ["OSX_RIPES_BIN"], "--mode", "cli", "-t", "asm",
                 "--src", os.path.join(WORKLOADS, "loop_sum.S"),
                 "--proc", "RV64_ISS", "--isaexts", "M", "--trace-out", out],
                check=True, capture_output=True,
                env=dict(os.environ, QT_QPA_PLATFORM="offscreen"))
            _, ripes_events, _ = osx_trace.load(out)
        _, gem5_events, _ = convert("loop_sum_o3_l1l2")
        ripes = retired_pcs(ripes_events)
        gem5 = retired_pcs(gem5_events)
        # Ripes also retires the final exit ecall, gem5 does not count it.
        self.assertEqual(len(ripes), len(gem5) + 1)
        self.assertEqual(ripes_events_assembly(ripes_events)[-1], "ecall")
        # Ripes places .text at 0, the ELF at its link address.
        self.assertEqual([pc - ripes[0] for pc in ripes[:-1]],
                         [pc - gem5[0] for pc in gem5])
        ripes_regs = final_registers(ripes_events)
        for name in ("x10", "x11", "x12", "x13", "x17"):
            self.assertEqual(ripes_regs[name], final_registers(gem5_events)[name])


if __name__ == "__main__":
    unittest.main()
