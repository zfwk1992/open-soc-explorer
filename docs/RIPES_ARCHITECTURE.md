# Ripes architecture (as analysed for Open SoC Explorer)

This document describes the Ripes code base that Open SoC Explorer is built on.
It was written by reading the checked-out sources, not from tutorials. Revision
analysed: upstream `mortbopet/Ripes` `master` at commit `7827f5f`
(2026-10-22, release line v2.3.0), imported into this repository by merge commit
`2059036`. Paths are relative to the repository root.

If you change Ripes code, re-check the statements here; the
`tst_missions::tst_sourceReferencesExist` test only verifies that the files
cited by the GUI exist, not that this document is current.

## 1. Big picture

```
main.cpp ──► MainWindow (GUI mode)              CLIRunner (--mode cli)
                │                                    │
                ▼                                    ▼
          tabs (Editor, Processor, Cache, Memory, I/O, [Explorer, Missions])
                │                                    │
                └───────────────┬────────────────────┘
                                ▼
                       ProcessorHandler (singleton)
              ┌─────────────────┼─────────────────────┬──────────────┐
              ▼                 ▼                     ▼              ▼
     ProcessorRegistry   RipesProcessor (iface)  Assembler/Program  SyscallManager
     (model catalogue)          │                                    + IOManager
                                ▼
                 RipesVSRTLProcessor = RipesProcessor + vsrtl::core::Design
                                │
                 RVSS, RV5MC, RV5S(+variants), RV6S_DUAL, RVISS  (src/processors/RISC-V)
                                │
                       VSRTL components, ports, registers, memories
                       (external/VSRTL, git submodule)
```

* **Simulation core**: processor models are written with **VSRTL**, a C++
  library for describing and simulating synchronous digital circuits
  (components, ports, registers, memories) that also draws them. Ripes adds the
  RISC-V datapaths, the assembler, system calls, caches and the GUI.
* **One process-wide simulation**: `ProcessorHandler` (`src/processorhandler.h`)
  is a singleton that owns the current processor, program, assembler and
  breakpoints and broadcasts state changes as Qt signals. Almost everything
  else talks to the simulator through it.

## 2. Entry point and build

| Item | Location | Notes |
|---|---|---|
| Entry point | `main.cpp` | `parseCommandLine()` selects `guiMode()` or the CLI (`--mode cli`). `guiMode()` sets the Fusion style, `applyColorScheme()`, creates `Ripes::MainWindow`. |
| Top-level build | `CMakeLists.txt` | C++20, `find_package(Qt6 6.8 COMPONENTS Core Widgets Svg Charts REQUIRED)`, AUTOMOC/AUTOUIC/AUTORCC, resources (`resources/icons/icons.qrc`, `examples/examples.qrc`, `src/processors/layouts.qrc`, `resources/fonts/fonts.qrc`) compiled into the `Ripes` executable. `RIPES_BUILD_TESTS` enables `test/`. Optional `RIPES_BUILD_VERILATOR_PROCESSORS`. |
| Library structure | `src/CMakeLists.txt` | `create_ripes_lib()` globs `*.cpp/*.h/*.ui` of a directory into a static library. `ripes_lib` (all of `src/*.cpp`) links the sub-libraries `isa`, `cachesim`, `editor`, `syscall`, `assembler`, `io`, `utilities`, `processors`, `version`, `cli` (and, in Open SoC Explorer, `explorer`). |
| External code | `external/CMakeLists.txt` | `external/VSRTL` (git submodule), `external/fancytabbar` (vendored), libelfin and ELFIO via `FetchContent` (pinned commits). VSRTL itself fetches cereal, Signals and magic_enum. |
| Settings | `src/ripessettings.{h,cpp}` | `RipesSettings` wraps `QSettings`; every key needs a default in `s_defaultSettings`. `SettingObserver` emits `modified`. `RIPES_GLOBALSIGNAL_REQRESET` is a pseudo-setting used to request a processor reset. |

Note: the `.gitmodules` file still lists ELFIO and libelfin, but only
`external/VSRTL` is a submodule in the tree; the other two are fetched by CMake.

## 3. ISA and instruction decoding

| Concept | Location |
|---|---|
| ISA description interface | `src/isa/isainfo.h` – `ISAInfoBase` (register files via `regInfoMap()`, `bits()`, `instrBits()`, extensions, `instructions()`), `ProcessorISAInfo` (supported/default extensions) |
| RISC-V ISA | `src/isa/rv32isainfo.h`, `src/isa/rv64isainfo.h`, `src/isa/rvisainfo_common.{h,cpp}` (register names, `RVISA::GPR`/`FPR` ids), extension instruction sets `src/isa/rv_i_ext.cpp`, `rv_m_ext.cpp`, `rv_c_ext.cpp` |
| Instruction encoding framework | `src/isa/instruction.h` – compile-time `Instruction<...>` templates with `OpPart`, bit ranges, immediate reassembly; `InstructionBase::disassemble()` |
| Assembler / disassembler | `src/assembler/assembler.h` (`Assembler<ISA>`, `disassemble()`), `matcher.h` (`Matcher::matchInstruction` decodes a word to an `InstructionBase`), `directive.h`/`gnudirectives.*`, `expreval.*`, `program.{h,cpp}` (`Program`: sections, symbols, `entryPoint`, `sourceHash`) |
| Hardware decoder inside the datapaths | `src/processors/RISC-V/rv_decode.h` (`Decode` VSRTL component), `rv_control.h` (`Control`), `rv_immediate.h` (`Immediate`) |

`ProcessorHandler::disassembleInstr(addr)` disassembles the word at `addr`
using the current assembler; the GUI (and the Open SoC Explorer trace) use it
for instruction text such as `add x3 x1 x2`.

## 4. Processor models

All models are registered in `src/processorregistry.cpp`
(`ProcessorRegistry::ProcessorRegistry()`), identified by `ProcessorID`
(`src/processorregistry.h`) and constructed via `ProcInfo<T>::construct()`.

| ProcessorID | Class | Source | Structure |
|---|---|---|---|
| `RV32_SS`, `RV64_SS` | `RVSS` | `src/processors/RISC-V/rvss/rvss.h` | 1 stage |
| `RV32_5MC_2M`, `RV64_5MC` | `RV5MC2M` | `src/processors/RISC-V/rv5mc/rv5mc_2m.h` (+ `rv5mc_base.h`) | 5 FSM states, one active |
| `RV32_5MC_1M`, `RV64_5MC_1M` | `RV5MC1M` | `src/processors/RISC-V/rv5mc/rv5mc_1m.h` | 5 FSM states, single memory |
| `RV32_5S_NO_FW_HZ`, `RV64_…` | `RV5S_NO_FW_HZ` | `src/processors/RISC-V/rv5s_no_fw_hz/rv5s_no_fw_hz.h` | 5 stages, no forwarding, no hazard detection |
| `RV32_5S_NO_HZ`, `RV64_…` | `RV5S_NO_HZ` | `src/processors/RISC-V/rv5s_no_hz/rv5s_no_hz.h` | 5 stages, forwarding only |
| `RV32_5S_NO_FW`, `RV64_…` | `RV5S_NO_FW` | `src/processors/RISC-V/rv5s_no_fw/rv5s_no_fw.h` | 5 stages, hazard detection only |
| `RV32_5S`, `RV64_5S` | `RV5S` | `src/processors/RISC-V/rv5s/rv5s.h` | 5 stages, forwarding + hazard detection |
| `RV32_6S_DUAL`, `RV64_6S_DUAL` | `RV6S_DUAL` | `src/processors/RISC-V/rv6s_dual/rv6s_dual.h` | 2 lanes × 6 stages |
| `RV32_ISS`, `RV64_ISS` | `RVISS` | `src/processors/RISC-V/rviss/rviss.h` | functional ISS, 1 "stage" |

### 4.1 The `RipesProcessor` interface

`src/processors/interface/ripesprocessor.h` is the simulator-agnostic contract
between Ripes and a model. Key members:

* Structure: `structure()` (`ProcessorStructure`: lanes → stage count),
  `stageName(StageIndex)`, `getPcForStage()`, `stageInfo()` returning
  `StageInfo{pc, stage_valid, state ∈ {None, Stalled, Flushed, WayHazard, Unused}}`.
* State: `getRegister/setRegister(rfid, i)`, `getMemory()` (a
  `vsrtl::core::AddressSpaceMM`), `dataMemAccess()`/`instrMemAccess()`
  (`MemoryAccess{type, address, bytes}`), `getCycleCount()`,
  `getInstructionsRetired()`, `finished()`, `nextFetchedAddress()`.
* Control: `clock()` → protected `clockProcessor()`, `reverseProcessor()`
  (feature `isReversible`), `resetProcessor()`, `finalize()` (exit system call
  handling), `setPCInitialValue()`.
* Callbacks/signals: `isExecutableAddress`, `trapHandler`,
  Gallant signals `processorWasClocked/Reversed/Reset`.

### 4.2 VSRTL-based models

`src/processors/ripesvsrtlprocessor.h`: `RipesVSRTLProcessor` derives from
both `RipesProcessor` and `vsrtl::core::Design`, forwards VSRTL's
`designWasClocked/Reversed/Reset` to the Ripes signals and converts the
`opSig/addressSig` of a VSRTL memory into a `MemoryAccess` (`memToAccessInfo`).

A model is a constructor that instantiates components with `SUBCOMPONENT(...)`
and wires ports with `>>`. In `RV5S` (`src/processors/RISC-V/rv5s/rv5s.h`):

* IF: `pc_reg`, `pc_4`, `pc_inc`, `pc_src`, `instr_mem`, `uncompress`
* IF/ID: `ifid_reg` (`rv5s_no_fw_hz/rv5s_no_fw_hz_ifid.h`)
* ID: `decode`, `control`, `immediate`, `registerFile` (reads)
* ID/EX: `idex_reg` (`rv5s_idex.h`)
* EX: `alu_op1_src`, `alu_op2_src`, forwarding muxes `reg1_fw_src`/`reg2_fw_src`,
  `alu`, `branch`, `br_and`, `controlflow_or`, forwarding unit `funit`
  (`rv5s_forwardingunit.h`)
* EX/MEM: `exmem_reg` (`rv5s_exmem.h`); MEM: `data_mem`; MEM/WB: `memwb_reg`
  (`rv5s_memwb.h`); WB: `reg_wr_src` → `registerFile` write port
* Hazard detection: `hzunit` (`rv5s_hazardunit.h`) drives `pc_reg->enable`,
  `ifid_reg->enable` and the ID/EX clear/stall
* System calls: `ecallChecker` (`rv_ecallchecker.h`) on the ID/EX opcode

Verified behaviour of the 5-stage family (all four variants): branches and
jumps are evaluated in EX (`branch` reads ID/EX operands; `alu->res` feeds
`pc_src`); a taken control-flow change (`controlflow_or`) clears IF/ID and
ID/EX, i.e. the two younger instructions are flushed. Instructions are always
fetched sequentially.

`stageInfo()` derives validity from the pipeline registers' `valid_out`
signals, `isExecutableAddress()` and the exit system-call state;
`clockProcessor()` counts a retirement when the MEM/WB register holds a valid
instruction before the clock edge.

Datapath layouts (component positions for drawing) are JSON files next to the
models, listed in `src/processors/layouts.qrc` and referenced by
`ProcInfoBase::layouts`.

### 4.3 The ISA simulator

`RVISS` (`rviss.h`) executes one instruction per `clock()` in software
(`executeInstruction()`), keeps registers in an array and records the last data
access in `m_lastDataAccess`. It reports a single stage named `•`.

## 5. Simulation control and threading

`src/processorhandler.{h,cpp}`:

* `selectProcessor()` constructs a model through the registry, connects
  callbacks and signal wrappers, emits `processorChanged` and requests a reset.
* `loadProgram()` installs program sections as memory initialisations, sets the
  entry PC and triggers `RIPES_GLOBALSIGNAL_REQRESET` → `_reset()` →
  `resetProcessor()` → re-applies register initialisations (`sp`, `gp`) →
  emits `procStateChangedNonRun`.
* `clock()` runs one cycle on the Qt global thread pool (`ProcessorClocker`).
* `run()` runs `clockUnguarded()` in a `QtConcurrent` worker until a breakpoint,
  `finished()` or `stopRun()`. During a run VSRTL *port* signals are disabled
  (`setEnableSignals(false)`), but the per-cycle *clocked* signal stays enabled.
* Signals: `processorClocked` (every cycle, may come from the worker thread –
  connect with `Qt::DirectConnection`), `processorClockedNonRun`,
  `procStateChangedNonRun` (rate limited by `RIPES_SETTING_UIUPDATEPS`),
  `processorReset`, `processorReversed`, `runStarted`, `runFinished`,
  `programChanged`, `processorChanged`.
* Reverse execution is provided by VSRTL (register/memory rewind stacks,
  `RIPES_SETTING_REWINDSTACKSIZE`). Note: VSRTL emits `designWasReversed`
  inside `Design::reverse()`, i.e. before a model's own `reverse()` override
  has finished (relevant for anything reading `getInstructionsRetired()` in a
  reverse handler).

System calls: `src/syscall/ripes_syscall.h` (`SyscallManager`),
`riscv_syscall.h`, `systemio.{h,cpp}` (console/file I/O).
Memory-mapped I/O: `src/io/iomanager.{h,cpp}` registers peripherals
(`ioledmatrix`, `ioswitches`, `iodpad`, `ioscreen`, `ioclock`) as IO regions of
the processor's `AddressSpaceMM` (`regionType()` returns `IO` for them).

## 6. Caches

The datapath models use ideal memories. Cache behaviour is simulated *beside*
the processor: `src/cachesim/l1cacheshim.{h,cpp}` (`L1CacheShim`) connects to
`processorClocked` and replays `dataMemAccess()/instrMemAccess()` into a
`CacheSim` (`src/cachesim/cachesim.{h,cpp}`, configurable sets/ways/policies).
The Cache tab (`src/cachetab.*`, `src/cachesim/cachewidget.*`,
`cacheplotwidget.*`) visualises hits/misses; cache latency does **not** affect
the processor's cycle count.

## 7. GUI

| Tab | Class | Main pieces |
|---|---|---|
| Editor | `EditTab` (`src/edittab.*`) | `CodeEditor` (`src/editor/codeeditor.*`, RISC-V/C syntax highlighting), `ProgramViewer` (disassembly), assembles on edit (timer) and emits `programChanged` |
| Processor | `ProcessorTab` (`src/processortab.*`) | `vsrtl::VSRTLWidget` datapath drawing, simulator toolbar (reset/reverse/clock/run/auto-clock), `RegisterWidget`/`RegisterModel`, `InstructionModel` (stage of each instruction), statistics, `PipelineDiagramWidget` |
| Cache | `CacheTab` | cache configuration, plots |
| Memory | `MemoryTab` | `MemoryViewerWidget`/`MemoryModel` |
| I/O | `IOTab` | peripherals |

`MainWindow` (`src/mainwindow.*`) creates the tabs, a `FancyTabBar`
(`external/fancytabbar`) for navigation and one toolbar per tab (`RipesTab`,
`src/ripestab.h`). Dark/light mode: `applyColorScheme()`, `ThemeManager`
(`src/ripessettings.h`).

`PipelineDiagramModel` (`src/pipelinediagrammodel.*`) records
`stageInfo()` per cycle (bounded by `RIPES_SETTING_PIPEDIAGRAM_MAXCYCLES`) –
the closest existing feature to a trace.

## 8. Command-line mode

`src/cli/clioptions.{h,cpp}` define options (`--src`, `-t asm|c|bin|elf`,
`--proc`, `--isaexts`, `--timeout`, `--json`, telemetry flags such as
`--cycles --cpi --regs --pipeline`, cache presets); `src/cli/clirunner.*`
assembles/loads, runs via `ProcessorHandler::run()` and prints telemetry
(`src/cli/telemetry.h`). Open SoC Explorer adds `--trace-out` and
`--trace-max-cycles`.

## 9. Tests

`test/CMakeLists.txt` builds one QtTest executable per file
(`create_qtest`). Upstream tests: `tst_riscv` (riscv-tests programs in
`test/riscv-tests*` on all models), `tst_assembler`, `tst_expreval`,
`tst_cosimulate` (register-change traces of each model compared against the single-cycle `RVSS` reference), `tst_reverse`,
`tst_stall`. Programs are loaded through `ProgramLoader`
(`test/programloader.h`), which drives a real `EditTab`. CI runs every `tst_*`
binary (`.github/workflows/scripts/run_tests.sh`) under Xvfb.

## 10. Extension points used by Open SoC Explorer

| Need | Extension point used | Why |
|---|---|---|
| Per-cycle observation | `ProcessorHandler::processorClocked` (direct connection) | Same mechanism as `L1CacheShim` and `PipelineDiagramModel`; works during Run and in CLI mode |
| Model-independent state | `RipesProcessor` interface | Works for every registered model without modifying them |
| Datapath internals | `vsrtl::core::Design::getSubComponents()`, `SimComponent::getInputPorts()/getOutputPorts()`, `SimPort::uValue()/getInputPort()` | Read-only access to live signal values and wiring |
| Reset/reverse | `processorReset`, `processorReversed`, `procStateChangedNonRun` | Keep the trace consistent with the simulator |
| New GUI tabs | `MainWindow::TabIndex`, `RipesTab` | Same pattern as existing tabs |
| Processor switching | `ProcessorTab::selectProcessor()` (factored out of `processorSelection()`) | Missions select models exactly like the dialog |
| Headless export | `CLIModeOptions`, `CLIRunner` | Scriptable trace export |

### Reusable directly

Processor models, assembler/disassembler, editor, register/memory views, cache
simulator, simulation controls, reverse execution, CLI runner.

### Requires extension (and why)

* **Instruction identity**: Ripes only exposes the PC per stage; dynamic
  instruction instances are inferred by the trace recorder.
* **Register writes and operand values**: not exposed as events; derived by
  comparing register files between cycles.
* **Model-specific signals** (e.g. forwarding decisions) are only available by
  inspecting VSRTL ports, which differ per model.
* **SoC components** (bus, DMA, accelerators, multiple cores) do not exist in
  Ripes. They require new models or other engines (gem5, RTL) – see
  [ROADMAP.md](ROADMAP.md).

## 11. Learning map for new contributors

Read in this order:

1. `src/processors/interface/ripesprocessor.h` – what a "processor" is to Ripes.
2. `src/processorhandler.h` and `.cpp` (`_selectProcessor`, `_loadProgram`,
   `_reset`, `_run`) – lifecycle, signals and threading.
3. `src/processors/RISC-V/rv5s/rv5s.h` – a complete pipelined datapath; compare
   with `rvss/rvss.h` (single-cycle) to see what pipelining adds.
4. `src/processors/RISC-V/rv_alu.h`, `rv_decode.h`, `rv_control.h`,
   `rv5s/rv5s_hazardunit.h`, `rv5s/rv5s_forwardingunit.h` – small components.
5. `external/VSRTL/include/VSRTL/interface/vsrtl_interface.h` – `SimPort`,
   `SimComponent`, `SimDesign` (clock/reverse/reset, signals).
6. `src/pipelinediagrammodel.cpp` and `src/cachesim/l1cacheshim.cpp` – two
   existing per-cycle observers.
7. `src/processortab.cpp` and `src/mainwindow.cpp` – how the GUI is assembled.
8. `src/cli/clirunner.cpp` – the headless flow.
9. `test/tst_stall.cpp` and `test/programloader.h` – how to test with real
   programs.
10. Then the Open SoC Explorer layer: `src/explorer/tracerecorder.cpp`,
    `tracereplay.cpp`, `src/explorertab.cpp` (see [ARCHITECTURE.md](ARCHITECTURE.md)).
