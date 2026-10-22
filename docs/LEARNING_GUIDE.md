# Learning guide

Open SoC Explorer teaches computer architecture by letting you run real
simulations and inspect what happened, cycle by cycle. Everything you see in
the Explorer and everything a mission checks comes from the simulator (Ripes'
educational processor models) or from the RISC-V specification – never from a
scripted animation.

## 1. The workflow

1. **Editor** tab: write or load a RISC-V assembly program (it is assembled
   automatically).
2. **Processor** tab: choose a processor model (chip icon in the toolbar),
   then *Clock* (one cycle), *Run*, *Reverse* or *Reset*. The datapath drawing
   shows live signal values.
3. **Explorer** tab: every cycle is recorded into a trace. Use the timeline to
   step forwards and backwards (this replays the recording; the simulator is
   not reversed), click an instruction to get an explanation, open
   *Registers* to see the state at the selected cycle, *CPU components* to
   inspect datapath units with their live signals, and *Statistics* for CPI,
   stalls and flushes.
4. **Missions** tab: guided lessons with automatically verified steps and a
   challenge.

The banner at the top of the Explorer always states the engine, the model,
its fidelity class and the clock domain. Ripes' processors are educational
datapath models: their cycle counts describe these models, not a commercial
core.

## 2. Reading the Explorer

* **Pipeline at the selected cycle**: one box per stage. Blue = instruction,
  amber = held (stalled) instruction or stall bubble, red = flushed bubble or
  an instruction that will be squashed (wrong path).
* **Instruction timeline**: one row per *dynamic* instruction (the same
  instruction executed in a loop appears once per iteration), one column per
  cycle. `*` = held in that stage, `✓` = retired, `✕` = squashed.
* **Events**: everything recorded in that cycle, with its *observability*
  (how it was obtained – see [TRACE_PROTOCOL.md](TRACE_PROTOCOL.md) §5).
* **Instruction explanation**: what the instruction does, its encoding, the
  architectural operand values before it retired, the value it wrote, a check
  of that value against the ISA semantics, data dependencies on older
  instructions still in flight, its journey through the pipeline, and the
  source files that implement it.

## 3. Missions

| # | Mission | Processor | You learn |
|---|---|---|---|
| 1 | Execute Your First RISC-V Program | single-cycle (`RV32_SS`) | PC, register file, ADDI, ADD, write-back |
| 2 | Pipeline Hazards | 5-stage (`RV32_5S`), then `RV32_5S_NO_FW` | data dependencies, forwarding, stalls, CPI |
| 3 | Branch and Flush | 5-stage (`RV32_5S`) | branch condition, PC update, pipeline flush, loop unrolling |

Each mission has an objective, an explanation, an example program, guided
steps (each step shows its conditions and turns ✓ when the simulator state
satisfies them; completed steps stay completed), a challenge with a hint, and
completion feedback. Progress is stored in the application settings.

Numbers measured on the models for the examples (asserted by the tests):

* Mission 2: `RV32_5S` stalls 1 cycle (CPI 1.71); without forwarding
  (`RV32_5S_NO_FW`) the same program stalls 8 cycles (CPI 2.71); the
  reordered solution has 0 stalls.
* Mission 3: the first flush happens in cycle 7; the loop squashes 18
  instructions (9 taken branches × 2); a 2× unrolled loop squashes 8.

Challenges are scored only on a **complete** trace: recorded from reset, not
truncated, program finished. If a challenge says "run the program to
completion", reset and run again with recording enabled.

## 4. Writing a mission

Missions are JSON files in `src/explorer/missions/` listed in
`missions.qrc`. Fields:

| Field | Description |
|---|---|
| `id`, `number`, `title` | Identity; `number` orders the list |
| `learn` | List of concepts |
| `processor` | `ProcessorID` name the mission is designed for (e.g. `RV32_5S`) |
| `objective`, `explanation` (HTML), `example` (assembly), `completion` | Texts |
| `steps[]` | `title`, `instruction` (HTML), `checks[]` |
| `challenge` | `text` (HTML), `hint`, `checks[]`, `reference_solution` (not shown in the GUI; used by the tests to prove the mission is solvable) |

Criteria (`checks[]` entries have a `kind` and parameters; an optional
`label` overrides the generated description):

| kind | Parameters | Passes when |
|---|---|---|
| `processor_is` | `any_of` (default: mission processor) | the selected model is one of them |
| `program_loaded` | – | a program is assembled and loaded |
| `program_matches_example` | – | the editor program equals the example (ignoring comments/whitespace) |
| `source_differs_from_example` | – | the program was changed |
| `cycle_at_least` | `cycles` | current cycle ≥ value |
| `finished` | – | the processor finished |
| `trace_complete` | – | trace from reset, not truncated, finished |
| `register_equals` | `register` (`x5`, `t0`, ...), `value` (int or `0x..`) | register value matches |
| `register_written_by` | `register`, `mnemonic` | the last write to the register came from that instruction (complete trace) |
| `retired_at_least` | `count` | retired instructions ≥ count |
| `retired_mnemonic_at_least` | `mnemonic`, `count` | retired instances of that mnemonic ≥ count |
| `retired_branches_at_least` | `count` | retired conditional branches ≥ count |
| `stall_cycles_at_least` / `_at_most` | `count` | stall cycles (cycles with a held instruction or an inserted stall bubble); `_at_most` needs a complete trace |
| `squashed_at_least` / `_at_most` | `count` | squashed instructions; `_at_most` needs a complete trace |
| `cycles_at_most` | `cycles` | total cycles (complete trace) |
| `cpi_at_most` | `value` | cycles / retired (complete trace) |

When adding a mission, add a test to `test/tst_missions.cpp` that runs the
example and the reference solution on the simulator and checks that the
example does **not** already solve the challenge. Quote only numbers you have
measured.
