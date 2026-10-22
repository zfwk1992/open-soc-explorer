# Simulation fidelity

Open SoC Explorer only visualises what a simulation engine actually produced,
and labels every view with the engine, model and fidelity class. This document
explains what each currently supported model represents and what the trace
layer adds on top of it.

## 1. Principles

1. **No fabricated behaviour.** Animations, timelines and mission scores are
   driven by recorded simulator state. If a subsystem is not simulated, it is
   not shown as if it were.
2. **Educational models are not real hardware.** Ripes' datapaths are textbook
   designs. Their cycle counts describe those designs, not any commercial or
   open-source core (Ibex, Rocket, etc.).
3. **Derived information is labelled.** Every trace event carries an
   `observability` level (`direct`, `model-derived`, `state-diff`, `inferred`,
   `isa-decoded`), see [TRACE_PROTOCOL.md](TRACE_PROTOCOL.md).
4. **Engines are not mixed.** Ripes cycles, gem5 ticks and RTL cycles are
   separate time domains and are never converted into each other.

## 2. Engines

| Engine | Status | Fidelity class |
|---|---|---|
| Ripes (VSRTL datapath models) | **Supported** (live and offline traces) | `educational-model` |
| Ripes ISA simulator | **Supported** | `functional-iss` |
| gem5 | Planned (offline trace import) | to be defined per CPU model |
| Verilator + Ibex RTL | Planned | `rtl-cycle-accurate` for the pinned RTL revision only |
| AI accelerator models | Planned | to be defined |

## 3. Ripes models

| Model | Pipelining | Hazard handling | Branches | Memory |
|---|---|---|---|---|
| `RV32/64_SS` single-cycle | none (1 instruction per cycle) | not needed | next PC in same cycle | ideal, combinational |
| `RV32/64_5MC_*` multi-cycle | none (FSM, one instruction at a time) | not needed | resolved by FSM | ideal; `_1M` shares one memory |
| `RV32/64_5S_NO_FW_HZ` | 5 stages | **none** – programs with close dependencies compute wrong results | EX, static not-taken, 2-instruction flush | ideal |
| `RV32/64_5S_NO_HZ` | 5 stages | forwarding only – load-use dependencies compute wrong results | as above | ideal |
| `RV32/64_5S_NO_FW` | 5 stages | stall until write-back | as above | ideal |
| `RV32/64_5S` | 5 stages | forwarding + load-use stall | as above | ideal |
| `RV32/64_6S_DUAL` | 2 lanes × 6 stages | forwarding + hazard units | see `rv6s_dual.h` | ideal |
| `RV32/64_ISS` | none | n/a | architectural | ideal |

Common limitations of all Ripes models:

* Instruction and data memories are ideal (no latency, no contention, no
  bus). The **Cache tab** simulates caches *beside* the processor using its
  access stream; cache misses do **not** change the processor's cycle count.
* No interrupts, no privilege modes, no multiple cores, no DMA, no
  interconnect. The trace schema reserves event types for these subsystems,
  but the Ripes adapter never emits them.
* System calls (`ecall`) are executed by the Ripes environment, not by a
  simulated operating system.

## 4. What the Ripes trace adapter adds

| Information | How it is obtained | Observability |
|---|---|---|
| Stage occupancy, stage stall/flush labels | `RipesProcessor::stageInfo()` each cycle | `model-derived` |
| Retirement | model's retirement counter | `model-derived` (or `inferred`, §5.2) |
| Dynamic instruction identity | matching stage PCs across consecutive cycles | `inferred` |
| Squash cause | nearest older control-flow instruction in the pipeline | `inferred` |
| Register writes | register files compared between cycles | `state-diff` |
| Memory accesses | `dataMemAccess()` + side-effect-free memory reads | `direct` |
| Instruction fields | Open SoC Explorer RISC-V decoder | `isa-decoded` |
| Operand values in explanations | architectural registers before retirement, reconstructed from the trace | derived (labelled in the GUI) |
| Component signal values (CPU components view) | live VSRTL port values | direct, not recorded |

Consistency is tested: `tst_trace::tst_replayMatchesSimulator` compares, for
every cycle and on all ten processor models, the register state and stage
occupancy reconstructed from an exported trace with the live model.

## 5. Findings from the current models

These findings come from running the models with the trace recorder. They are
covered by automated tests so that a future upstream change is noticed.

### 5.1 Hazard-unsafe models compute different results

On the loop program used in `tst_trace` (29 dynamic instructions on
hazard-safe models), `RV32_5S_NO_FW_HZ` and `RV32_5S_NO_HZ` read stale
operands, change the loop trip count and finish with different register
values (35 instructions executed). This is the intended educational behaviour
of these models. The instruction explainer flags such cases by comparing the
value the model wrote with the ISA semantics applied to the architectural
operands ("✗ … used a stale operand").

### 5.2 Dual-issue retirement counter

For the same 29-instruction program, upstream Ripes' `RV32_6S_DUAL` reports
**28** retired instructions (`getInstructionsRetired()`), also in the
unmodified upstream CLI (`--iret`), while `RV32_5S` and `RV32_ISS` report 29.
The final instruction (`addi a3, a2, 1`, in lane 1) writes its result but is
not counted: `RV6S_DUAL::instructionsRetired()` checks the second lane with
`isExecutableAddress(memwb_reg->pc4_out)`, which does not hold in this case.

The trace recorder reports such an instruction as an `instruction.retire`
event with `observability: inferred`,
`attribution: inferred-from-register-write` and
`model_retire_counter: "did not count this instruction"`, and adds a caveat to
the footer. Statistics computed from the trace therefore count 29 retired
instructions, while the Ripes statistics panel shows the model's 28. This
looks like an upstream issue; it has not been reported upstream yet.

### 5.3 Memory timing differs between model kinds

For VSRTL datapath models, `dataMemAccess()` describes the access presented
on the data memory port in the current cycle (writes commit at the next clock
edge). The ISA simulator reports the access of the instruction that just
executed. The adapter handles both (`memAccessReportedAfterExecution` in
`src/explorer/modelprofile.cpp`) and states the timing in each event's
`timing` field.

## 6. Mission scoring

Missions verify conditions against the live processor state and against the
statistics of a **complete** trace (from reset, not truncated, program
finished). Measured numbers quoted in the missions (e.g. one load-use stall
cycle on `RV32_5S`, 18 squashed instructions for the Mission 3 loop) were
obtained from these models and are asserted by `tst_missions`.
