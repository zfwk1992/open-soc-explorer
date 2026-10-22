# Roadmap

Status legend: **Done** (implemented and covered by automated tests),
**Partial**, **Planned** (not implemented).

## Milestone 0 — Import and build: **Done**

* Ripes imported with complete history (1 605 upstream commits) by merge
  commit `2059036`; the repository's own initial commit is preserved.
  `origin` = this repository, `upstream` = `mortbopet/Ripes`.
* Builds with Qt 6.9.3 (the version used by upstream CI); all 56 upstream test
  functions pass.
* Build instructions: [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md).

## Milestone 1 — Architecture analysis: **Done**

* [RIPES_ARCHITECTURE.md](RIPES_ARCHITECTURE.md): entry points, models,
  `RipesProcessor` interface, VSRTL, simulation control and threading, GUI,
  CLI, tests, extension points, learning map.

## Milestone 2 — Trace system: **Done**

* Versioned trace schema 0.1.0 ([TRACE_PROTOCOL.md](TRACE_PROTOCOL.md)).
* Ripes adapter for all models, live recording, JSON Lines export (GUI and
  `--trace-out`), offline import, deterministic replay.
* Tests: replay vs. live model on ten models, determinism, JSON round trip,
  reverse execution, truncation; Python validator with its own tests.

## Milestone 3 — CPU Explorer and missions: **Done (first version)**

* Explorer tab: timeline with forward/backward stepping and playback,
  pipeline occupancy, events, registers at any cycle, instruction
  explanations checked against the ISA, CPU component inspector with live
  signal values and source references, statistics, engine/fidelity banner.
* Missions 1–3 with live-verified steps and scored challenges.
* Next improvements:
  * clickable source references (open the file in an editor / on GitHub),
  * highlight the selected instruction in the VSRTL datapath drawing,
  * a richer animated "data movement" view driven by recorded port values
    (requires recording selected VSRTL signals per cycle),
  * missions about RV64, the M extension and the multi-cycle processor.

## Milestone 4 — SoC Explorer and gem5: **Partial (gem5 offline adapter and System view done)**

Done (tested):

* **Trace schema 0.2.0**: simulated components (`system`) with parameters,
  statistics and connections; engine ticks (`time.tick`,
  `ticks_per_cycle`).
* **System view** (Explorer → *System*): only components the engine
  reported as simulated, their reported connections, parameters with their
  meaning and statistics with the engine's description; typical SoC blocks
  that are not simulated are dashed and never animated. Works for Ripes
  (CPU, ideal memory, MMIO peripherals) and gem5 traces.
* **gem5 offline adapter** (`tools/gem5`, gem5 v26.0.0.0): configuration
  script, run script, converter for O3CPU, TimingSimpleCPU and
  AtomicSimpleCPU, workloads. Validated against real gem5 output (recorded
  in `tools/gem5/testdata`) and against Ripes (same committed instruction
  stream and result).
* **Out-of-order replay**: several instructions per stage, unknown initial
  registers, operands in commit order.
* **Measured cache-locality experiment** (strided vs. sequential, 16 vs.
  4 KiB L1D), see [tools/gem5/README.md](../tools/gem5/README.md).

Next:

1. **Mission 4 (cache optimization) on gem5 data**: precomputed, recorded
   gem5 runs of the cache experiment (no gem5 needed by the learner), scored
   on the measured statistics; optionally a "run it yourself" path.
2. **Comparison view** for two traces of the same program (e.g. strided vs.
   sequential): cycles, misses, prefetches side by side, from the engine's
   statistics.
3. **MinorCPU** support (MinorTrace parsing).
4. **Per-access cache and bus events** need more gem5 debug output (e.g.
   `Cache`, `XBar`, `DRAM` flags) and a converter for it; until then only
   end-of-run statistics are shown.
5. **Cache events from Ripes' cache simulator** (`CacheSim`) as `cache.hit` /
   `cache.miss` events, labelled as a side-car model that does not affect
   the processor timing.
6. Running gem5 from the GUI (currently: run script + open the trace).

## Milestone 5 — RTL integration (Ibex + Verilator): **Planned**

1. Pin a lowRISC Ibex revision as a submodule (Apache-2.0).
2. Build the official `simple_system` Verilator configuration (Verilator 5.020
   is available on Ubuntu 24.04; Ibex also documents FuseSoC and its own
   tool versions).
3. Run a minimal bare-metal program, collect Ibex's instruction trace (RVFI
   based `trace_core_*.log`) and selected signals (VCD/FST).
4. Normalize to the trace protocol (`engine: verilator-ibex`, real RTL
   cycles in a `clk_sys` domain).
5. Compare final architectural state with the Ripes ISS (`tst_cosimulate`
   style) and show RTL signals in the Explorer, labelled as RTL.
6. Never present Ripes' 5-stage timing as Ibex timing (Ibex has 2 or 3
   stages depending on configuration).

## Milestone 6 — AI accelerator / Nonlinear Function Accelerator Lab: **Planned**

Goal: compare `sigmoid`, `tanh`, `exp` implemented as

| Variant | Engine | What is measured vs. estimated |
|---|---|---|
| Scalar RV32IM software (fixed-point polynomial / piecewise linear) | Ripes (any model) | measured: instruction count, cycles of the educational model, memory traffic; error vs. double reference |
| Vector (RVV) | not supported by Ripes; needs another simulator (e.g. gem5 or Spike) | measured there, otherwise analytical estimate clearly labelled |
| Custom instruction | new Ripes ISA extension + ISS/datapath support | measured on the extended model |
| Dedicated accelerator | new accelerator model emitting `accelerator.*` and `dma.*` events | measured on that model; latency parameters documented as model assumptions |

Numerical accuracy (max/mean absolute error, ULP for fixed-point) is computed
offline against a reference implementation. Accelerator candidates to study
(each with its own license and adapter effort): Gemmini, VTA, NVDLA,
open-source vector units.

## Milestone 7 — Physical AI real-time workloads: **Planned**

* Periodic inference task + control task with a deadline, sharing memory.
* Requires an engine with contention modelling (gem5 or an accelerator/bus
  model); Ripes' ideal memories cannot show contention.
* Scoring from measured response times (deadline misses, jitter).

## Milestone 8 — HDL and FPGA lab (VHDL/Verilog): **Planned**

Goal: let learners go from a RISC-V program to the RTL that executes it, in
VHDL as well as Verilog, and from simulation to an FPGA bitstream, with the
same Explorer views.

Builds on Milestone 5 (RTL adapter, `rtl-cycle-accurate` time domain,
waveform import).

1. **VHDL simulation.** GHDL (and optionally NVC) as external tools; VCD/FST
   waveforms and testbench results imported into the trace protocol
   (signal-level events for selected signals, `engine: ghdl`).
2. **A VHDL RISC-V SoC to study.** NEORV32 (VHDL, BSD-3-Clause per its
   repository; simulates with GHDL) as the reference design: run the same
   programs as on Ripes, gem5 and Ibex, and compare architectural results
   (co-simulation like `tst_cosimulate`) and RTL cycle counts, each labelled
   with its own engine.
3. **Testbenches.** cocotb (Python) testbenches for VHDL and Verilog designs;
   results as mission checks.
4. **Synthesis and FPGA flow** with open tools: Yosys (+ ghdl-yosys-plugin
   for VHDL), nextpnr, for open boards (e.g. Lattice iCE40/ECP5). Resource
   usage and timing reports are shown as reported by the tools; no FPGA
   results are estimated.
5. **Explorer views:** waveform viewer synchronized with the instruction
   timeline; RTL hierarchy in the System view (from the elaborated design).
6. **Missions:** e.g. "write a VHDL timer peripheral for the NEORV32 bus,
   simulate it, see the bus transactions, then synthesize it and read the
   utilization report".

Open questions to resolve when starting: exact tool versions to pin, which
boards to support, packaging (container image with GHDL/Yosys/nextpnr).

## Milestone 9 — Classic and heterogeneous console architectures (research track): **Planned**

Goal: study the architectures of historical game consoles as case studies
of the SoC concepts taught here (multiple processors, fixed-function
hardware, DMA, local memories, interconnects), and give interested
contributors a structured place to research and build models.

Two case studies to start with:

| | SNK Neo Geo (MVS/AES, 1990) | Sony PlayStation 3 (2006) |
|---|---|---|
| Processors | Motorola 68000 (main), Zilog Z80 (sound) | Cell Broadband Engine: 1 PPE (PowerPC) + 8 SPEs, each with a 256 KiB local store and its own DMA engine (MFC) |
| Other hardware | YM2610 sound chip, sprite-based video chip | EIB ring interconnect, XDR memory, RSX GPU |
| Concepts | two CPUs communicating through latches and interrupts; fixed-function graphics | heterogeneous multicore, explicit DMA, double buffering, on-chip interconnect — the same ideas as Milestones 6–7 |

Planned approach, in order:

1. **Architecture notes** (`docs/CONSOLE_ARCHITECTURES.md`): block diagrams
   and data paths from public technical documentation, each fact with a
   source; what is known exactly and what is reverse-engineered.
2. **Educational models we can build and test ourselves:**
   * a *Cell-inspired* heterogeneous model in the accelerator lab: a
     control core plus several compute cores with local stores, DMA engines
     and an interconnect, emitting `dma.*`, `bus.transfer` and
     `accelerator.*` events. Labelled "Cell-inspired educational model", not
     PS3 hardware;
   * a dual-CPU model for main/sound processor communication
     (68000/Z80-style latches and interrupts).
3. **Traces from existing open-source emulators, used as external tools**
   (like gem5 in Milestone 4), with offline converters:
   * MAME (GPL-2.0-or-later as a whole; most source files BSD-3-Clause):
     its debugger can log executed instructions per CPU, e.g. for the Neo Geo
     68000 and Z80;
   * RPCS3 (GPL-2.0) for PS3 PPU/SPU execution, if its logging allows it.
   Emulators are usually functional, not cycle-accurate; their traces get the
   matching fidelity class and are never shown as hardware timing.
4. **FPGA re-implementations** (e.g. the Neo Geo core for MiSTer) as
   references for Milestone 8, after checking each project's license.
5. **Missions**, e.g. "double-buffered DMA on the Cell-inspired model",
   "main and sound CPU talk through a latch".

Rules for this track (in addition to the general rules):

* **No copyrighted firmware, BIOS images or game ROMs** in the repository,
  in test data or in CI. Use homebrew and test programs only: for the Neo
  Geo, ngdevkit (GPLv3+ per its distribution packages) provides a toolchain
  and an open-source BIOS replacement. Where an emulator requires original
  firmware (RPCS3 requires the PS3 system software), users supply it
  themselves and it is never redistributed.
* GPL-licensed emulators are integrated only as separate external tools
  (this repository is MIT); their code is not vendored or linked.
* Every view states which engine produced it: our educational model, an
  emulator, or RTL.

## Learning missions

| Mission | Status |
|---|---|
| 1 Execute your first RISC-V program | Done |
| 2 Pipeline hazards | Done |
| 3 Branch and flush | Done |
| 4 Cache optimization | Planned – the measured gem5 cache experiment (Milestone 4) is the basis; Ripes' cache simulator is an alternative that does not change processor timing |
| 5 SoC data transfer | Planned – needs Milestone 4 |
| 6 AI accelerator | Planned – needs Milestone 6 |
| 7 Physical AI real-time challenge | Planned – needs Milestone 7 |
| 8 Your first VHDL peripheral (simulate, then synthesize) | Planned – needs Milestone 8 |
| 9 Double-buffered DMA on a Cell-inspired multicore | Planned – needs Milestones 6 and 9 |
| 10 Two CPUs, one game: main/sound processor communication | Planned – needs Milestone 9 |

## Known issues / follow-ups

* Upstream: `RV6S_DUAL` retirement counter misses some lane-1 instructions
  (see SIMULATION_FIDELITY.md §5.2). Candidate for an upstream issue/PR.
* Upstream: `Program::sourceHash` stores raw SHA-1 bytes in a `QString`.
* The Explorer re-replays the whole trace on every refresh; fine for the
  default 5 000-cycle limit, incremental replay would be needed for very long
  traces.
