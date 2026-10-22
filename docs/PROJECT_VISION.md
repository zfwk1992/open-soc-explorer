# Project vision

**Open SoC Explorer — from instruction to Physical AI SoC.**

Open SoC Explorer is an interactive RISC-V, SoC and AI-accelerator simulation
and learning platform. It turns real simulation results into an explorable,
game-like experience, and it is meant to be useful both for learning
computer architecture and for small architecture experiments.

```
Application
    ↓
RISC-V instructions          ← today: Ripes assembler, editor, ISA decoder
    ↓
CPU pipeline                 ← today: Ripes datapath models + trace + Explorer
    ↓
Cache / memory               ← today: Ripes cache side-model; planned: gem5
    ↓
Interconnect / DMA           ← planned (gem5 / dedicated models)
    ↓
AI accelerator               ← planned (Nonlinear Function Accelerator Lab)
    ↓
Real-time workload           ← planned (Physical AI missions)
    ↓
System-level performance     ← planned
```

## Principles

1. **Real simulation first.** Every visualisation is driven by simulator state
   or a recorded trace. If something is not simulated, it is shown as
   planned/conceptual, never animated as if it were.
2. **Honest fidelity.** Each view names its engine, model and fidelity class.
   Educational pipelines are not presented as real cores; gem5 ticks, Ripes
   cycles and RTL cycles are never mixed.
3. **Explain, then let users verify.** Explanations show the data they are
   based on (operands, results, cycles) and check them against the ISA.
   Missions are scored by measured results.
4. **Build on Ripes, stay in sync.** Reuse the existing C++/Qt application,
   models and tools; keep upstream changes small so that Ripes improvements
   can be merged.
5. **Open and reproducible.** Versioned trace format, offline traces,
   scripts that work without the GUI, automated tests for every claim.

## Audience

* Students and engineers learning how instructions flow through a processor
  and why hazards, flushes and memory behaviour cost cycles.
* Embedded/automotive engineers moving towards RISC-V SoCs, accelerators and
  real-time AI (the original motivation: hardware/software optimisation of
  nonlinear functions such as sigmoid, tanh and exp on vector and custom
  hardware).
* Researchers who want a common trace format and viewer for several engines.

## What success looks like

* A learner can write a program, watch it execute, step back in time, ask
  "why did this instruction stall?" and get an answer backed by data.
* Missions progress from single instructions to pipelines, caches, SoC data
  movement, accelerators and real-time deadlines.
* The same Explorer can show a Ripes educational run, a gem5 run and an RTL
  run side by side, each clearly labelled.

See [ROADMAP.md](ROADMAP.md) for what exists today and what is planned.
