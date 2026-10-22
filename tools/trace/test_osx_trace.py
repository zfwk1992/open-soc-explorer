# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer. Run with:  python3 -m unittest discover tools/trace
import copy
import json
import os
import subprocess
import tempfile
import unittest

import osx_trace

HERE = os.path.dirname(os.path.abspath(__file__))
EXAMPLE = os.path.join(HERE, "examples", "mission2_rv32_5s.jsonl")


def write_trace(header, events, footer):
    fd, path = tempfile.mkstemp(suffix=".jsonl")
    with os.fdopen(fd, "w") as f:
        for record in [header] + events + ([footer] if footer else []):
            f.write(json.dumps(record) + "\n")
    return path


class ExampleTraceTest(unittest.TestCase):
    """The example was exported by the Ripes CLI (Mission 2 on RV32_5S)."""

    def setUp(self):
        self.header, self.events, self.footer = osx_trace.load(EXAMPLE)

    def test_valid(self):
        self.assertEqual(osx_trace.validate(self.header, self.events,
                                            self.footer), [])

    def test_summary(self):
        s = osx_trace.summary(self.header, self.events, self.footer)
        self.assertEqual(s["model"], "RV32_5S")
        self.assertEqual(s["fidelity"], "educational-model")
        self.assertTrue(s["complete"])
        self.assertEqual(s["cycles"], 12)
        self.assertEqual(s["instructions_retired"], 7)
        self.assertEqual(s["stall_cycles"], 1)  # one load-use stall
        self.assertEqual(s["squashed_instructions"], 0)
        self.assertEqual(s["memory_reads"], 2)

    def test_replay_shows_held_instruction(self):
        instances = osx_trace.replay(self.header, self.events)
        add = instances["inst-4"]
        self.assertTrue(add["assembly"].startswith("add x7"))
        self.assertEqual(add["held"], {5})
        self.assertEqual(add["outcome"], "retired")
        diagram = osx_trace.pipeline_diagram(self.header, self.events)
        self.assertIn("ID*", diagram)

    def test_detects_problems(self):
        events = copy.deepcopy(self.events)
        events[3]["sequence"] = 0
        events[4]["event"]["type"] = "pipeline.teleport"
        events[5]["correlation"]["cause_sequence"] = 10**6
        problems = osx_trace.validate(self.header, events, self.footer)
        self.assertTrue(any("sequence" in p for p in problems))
        self.assertTrue(any("unknown event type" in p for p in problems))
        self.assertTrue(any("cause_sequence" in p for p in problems))

    def test_rejects_unknown_major_version(self):
        header = dict(self.header, schema_version="1.0.0")
        path = write_trace(header, [], None)
        try:
            with self.assertRaises(osx_trace.TraceError):
                osx_trace.load(path)
        finally:
            os.remove(path)

    def test_missing_footer_is_reported(self):
        problems = osx_trace.validate(self.header, self.events, None)
        self.assertIn("missing footer record (trace incomplete)", problems)


@unittest.skipUnless(os.environ.get("OSX_RIPES_BIN"),
                     "set OSX_RIPES_BIN to the Ripes executable")
class CliExportTest(unittest.TestCase):
    """Exports fresh traces with the CLI and validates them."""

    def test_cli_traces_validate(self):
        program = "addi x1, x0, 10\naddi x2, x0, 20\nadd x3, x1, x2\n"
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "prog.s")
            with open(src, "w") as f:
                f.write(program)
            for proc in ("RV32_SS", "RV32_5S", "RV32_6S_DUAL", "RV32_ISS"):
                out = os.path.join(tmp, proc + ".jsonl")
                env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
                subprocess.run([os.environ["OSX_RIPES_BIN"], "--mode", "cli",
                                "-t", "asm", "--src", src, "--proc", proc,
                                "--trace-out", out], check=True, env=env,
                               stdout=subprocess.DEVNULL)
                header, events, footer = osx_trace.load(out)
                self.assertEqual(osx_trace.validate(header, events, footer),
                                 [], proc)
                s = osx_trace.summary(header, events, footer)
                self.assertEqual(s["instructions_retired"], 3, proc)


if __name__ == "__main__":
    unittest.main()
