# Open SoC Explorer architecture

Open SoC Explorer extends the Ripes C++/Qt application; it does not replace
it. This document maps the logical architecture to the code that exists today
and marks what is planned. For the Ripes code underneath, see
[RIPES_ARCHITECTURE.md](RIPES_ARCHITECTURE.md).

## 1. Layers

```
Open SoC Explorer
│
├── Interactive UI (Qt Widgets)
│   ├── Assembly editor ............ Ripes EditTab                     [Ripes]
│   ├── Processor view/pipeline .... Ripes ProcessorTab, VSRTL widget   [Ripes]
│   ├── Registers / memory / cache . Ripes tabs                         [Ripes]
│   ├── CPU Explorer ............... ExplorerTab (src/explorertab.*)    [new]
│   │     timeline, pipeline strip, events, registers at any cycle,
│   │     instruction explanations, CPU component inspector, statistics
│   ├── Learning missions .......... MissionsTab (src/missionstab.*)    [new]
│   ├── System view ................ SystemView (src/systemview.*)     [new]
│   │     components and connections the engine simulated, parameters,
│   │     engine statistics; absent SoC blocks drawn as "not simulated"
│   └── Accelerator explorer ....... —                                  [planned]
│
├── Simulation control ............. Ripes ProcessorHandler + toolbar   [Ripes]
│     run, pause, clock, reverse, reset, breakpoints
│
├── Trace and state layer (src/explorer, library explorer_lib)          [new]
│   ├── Trace schema ............... tracetypes.{h,cpp}
│   ├── Serialization .............. tracejson.{h,cpp}  (JSON Lines)
│   ├── Replay / snapshots ......... tracereplay.{h,cpp}
│   ├── Statistics ................. TraceReplay::stats()
│   ├── Explanations ............... instructionexplainer.{h,cpp}, riscvdecoder.{h,cpp}
│   ├── Component knowledge ........ componentinfo.{h,cpp}
│   └── Missions ................... mission.{h,cpp}, missions/*.json
│
├── Simulation adapters
│   ├── Ripes ...................... TraceRecorder + ModelProfile        [new]
│   ├── gem5 (offline) ............. tools/gem5/gem5_to_osx.py          [new]
│   ├── Verilator / Ibex ........... —                                  [planned]
│   └── AI accelerator ............. —                                  [planned]
│
└── Hardware models
    ├── RISC-V CPU ................. Ripes models (src/processors)     [Ripes]
    ├── Cache ...................... Ripes CacheSim (beside the CPU)    [Ripes]
    ├── Memory ..................... ideal VSRTL memories, MMIO         [Ripes]
    ├── Caches, crossbars, DRAM .... gem5 classic memory system         [gem5, external]
    └── DMA, accelerator ........................................... [planned]
```

Only directories that contain code exist; planned modules have no
placeholder directories.

## 2. Data flow

```
             clock()                          processorClocked (every cycle,
  ProcessorHandler ───► RipesProcessor ──────── direct connection, may be the
        │                    ▲                  run worker thread)
        │ reset/reverse      │ read-only                 │
        ▼                    │ (stageInfo, registers,    ▼
  processorReset,            │  dataMemAccess, memory)  TraceRecorder (Ripes adapter)
  processorReversed ─────────┼────────────────────────► │  mutex-protected Trace
  procStateChangedNonRun     │                          │
                             │                          ├─► trace() copy ─► TraceReplay ─► ExplorerTab
                             │                          │                              └► MissionsTab
                             │                          └─► exportJsonl() ─► .jsonl ─► Explorer (offline)
                             │                                                       └► tools/trace/osx_trace.py
                VSRTL ports ─┴──────────────────────────────────────────────► ExplorerTab "CPU components"
                                                                                 (live, not recorded)
```

* **Recording** happens in `TraceRecorder::onProcessorClocked()`, in lockstep
  with the processor, for every model. It never drives the simulation.
* **Replay** is independent of the simulator: `TraceReplay` rebuilds stage
  occupancy and architectural registers for any recorded cycle (register
  checkpoints every 64 cycles). Stepping backwards in the Explorer timeline
  does not touch the simulator.
* **Live vs. offline**: the Explorer shows the live trace (refreshed on
  `procStateChangedNonRun` and `runFinished`) or a trace loaded from a file.
* **Missions** combine live processor state (`captureMissionState()`) with
  statistics of the recorded trace; the trace must belong to the loaded
  program (`image_sha256`) and processor.

## 3. Threading

* `processorClocked` is emitted in the thread that clocks the processor (a
  thread-pool thread for single steps, a worker thread during Run). The
  recorder's handler uses a `std::mutex`; `trace()` returns a copy.
* GUI updates only happen on `procStateChangedNonRun`/`runFinished` (GUI
  thread, processor idle). The component inspector reads VSRTL ports only
  when the processor is not running.
* Recording is bounded (`maxCycles`, GUI setting `osx_trace_maxcycles`,
  default 5 000) so that long runs do not grow memory without bound; the
  trace is then marked as truncated.

## 4. Ripes adapter details

`TraceRecorder::recordTransitionLocked()` converts one clock transition into
events, in this order: retirement, instance correlation (back of the pipeline
to the front: moved / held / new), stage events and bubbles, squashes,
register writes, memory requests/responses. The rules and their limits are
specified in [TRACE_PROTOCOL.md §7](TRACE_PROTOCOL.md) and
[SIMULATION_FIDELITY.md](SIMULATION_FIDELITY.md).

`ModelProfile` (`modelprofile.cpp`) holds per-model facts the generic
interface does not expose: fidelity class and note, implementation file,
decode and memory stage names, whether the model reports memory accesses
after execution (ISS), forwarding/hazard-detection flags.

## 5. Other simulation engines

An adapter for another engine produces a `Trace::Trace` (or a JSON Lines file
following the protocol):

1. Fill `TraceHeader` honestly: `engine`, `engine_version`, `model`,
   `fidelity`, `fidelity_note`, time domain(s) with the engine's tick length,
   stages if the model has a pipeline view, and the simulated components
   (`system`).
2. Emit only events the engine observes; use `observability: inferred` for
   anything derived; use `time.tick` for engine ticks.
3. Offline import first, live streaming later.
4. Everything above the trace layer (replay, Explorer, System view,
   statistics) then works unchanged; missions can be written for the new
   engine.

### 5.1 gem5 (offline, implemented)

```
gem5.opt --debug-flags=Exec...,O3PipeView tools/gem5/se_trace.py prog.elf
   │  trace.out (Exec records, O3PipeView records), config.json, stats.txt
   ▼
tools/gem5/gem5_to_osx.py  ──►  trace.jsonl (schema 0.2.0, engine "gem5")
   ▼
Explorer: "Open a trace file for offline replay…"
```

* gem5 runs outside Open SoC Explorer (see `tools/gem5/README.md`); nothing
  of gem5 is linked or vendored.
* The converter is standard-library Python. Its mapping from gem5 records
  to events, and what is `direct` versus `inferred`, is documented at the
  top of `gem5_to_osx.py` and in [SIMULATION_FIDELITY.md](SIMULATION_FIDELITY.md).
* `TraceReplay` handles several instructions per stage (O3CPU) and
  register files without initial values (gem5 does not report them), and
  computes operands in commit order when several instructions retire in
  the same cycle.

### 5.2 Planned

Ibex RTL via Verilator (RVFI-based `trace_core_*.log` plus selected
signals) and accelerator models, see [ROADMAP.md](ROADMAP.md).

## 6. Code map

| File | Purpose |
|---|---|
| `src/explorer/tracetypes.{h,cpp}` | Schema types, enum names, hex/instance helpers |
| `src/explorer/tracejson.{h,cpp}` | JSON Lines writer/reader, speculative-status computation |
| `src/explorer/tracerecorder.{h,cpp}` | Ripes adapter; `programImageHash()` |
| `src/explorer/tracereplay.{h,cpp}` | Replay, occupancy, registers at any cycle, statistics |
| `src/explorer/modelprofile.{h,cpp}` | Per-model fidelity facts |
| `src/explorer/riscvdecoder.{h,cpp}` | RV32/64 I/M/F/D field decoder, semantics, evaluation |
| `src/explorer/instructionexplainer.{h,cpp}` | Explanations + HTML rendering |
| `src/explorer/componentinfo.{h,cpp}` | Datapath component descriptions |
| `src/explorer/mission.{h,cpp}`, `missions/*.json` | Mission format, loader, criteria evaluation |
| `src/explorertab.{h,cpp}` | Explorer tab GUI |
| `src/missionstab.{h,cpp}` | Missions tab GUI |
| `src/systemview.{h,cpp}` | System view: simulated components, connections, parameters, statistics |
| `src/cli/clioptions.*`, `src/cli/clirunner.*` | `--trace-out`, `--trace-max-cycles` |
| `tools/trace/osx_trace.py` | Python validate/summary/replay |
| `tools/gem5/se_trace.py` | gem5 configuration (stdlib board, CPU model and cache options) |
| `tools/gem5/gem5_to_osx.py` | gem5 output → trace protocol converter |
| `tools/gem5/workloads/` | RISC-V workloads for gem5 (Mission 3 loop, strided array sum) |
| `test/tst_trace.cpp`, `test/tst_missions.cpp`, `test/tst_explorergui.cpp` | Tests |

Upstream files touched (kept minimal to ease syncing with Ripes):
`src/CMakeLists.txt`, `src/mainwindow.{h,cpp}`, `src/processortab.{h,cpp}`
(`selectProcessor()` factored out), `src/ripessettings.{h,cpp}` (three
settings), `src/cli/*` (trace options), `test/CMakeLists.txt`.
