# Open SoC Explorer trace protocol

**Schema version: 0.2.0** (`Ripes::Trace::kSchemaVersion`,
`src/explorer/tracetypes.h`)

The trace protocol is a simulator-neutral, versioned format for execution
events. Two adapters produce it today:

* the Ripes adapter (`src/explorer/tracerecorder.cpp`, live, all Ripes
  models), and
* the offline gem5 converter (`tools/gem5/gem5_to_osx.py`, gem5
  `TimingSimpleCPU`, `AtomicSimpleCPU` and `O3CPU`, see §9).

Future RTL (Verilator/Ibex) and accelerator adapters are meant to produce the
same format so that the Explorer GUI, the replay engine and the missions work
unchanged on top of them.

Changes in 0.2.0 (backwards compatible, 0.1.0 traces still load): header
`system` (simulated components, §2.1), `time_domains[].ticks_per_cycle` /
`tick_unit`, events with `time.tick`, fidelity class `cycle-level-simulator`,
and several instructions per stage (out-of-order models, §7).

The protocol was designed for this project. It is **not** a format that Ripes,
gem5 or any RTL simulator emits by itself.

## 1. File format

A trace is a [JSON Lines](https://jsonlines.org/) file: UTF-8, one JSON object
per line, in this order:

1. exactly one `header` record,
2. zero or more `event` records, ordered by `time.cycle` and then `sequence`,
3. one `footer` record (absent if the producer stopped abnormally).

Every record carries `record`, `schema_version` and `run_id`.

Produce traces with:

* GUI: *Explorer* tab → *Export trace (JSON Lines)…*
* CLI: `Ripes --mode cli -t asm --src prog.s --proc RV32_5S --trace-out trace.jsonl`
  (`--trace-max-cycles N` bounds the recording, default 1 000 000)
* gem5: run gem5 with the debug flags listed in `tools/gem5/README.md`, then
  `tools/gem5/gem5_to_osx.py`

Read them with `Ripes::Trace::readJsonl()` (C++), the *Explorer* tab
(*Open a trace file for offline replay…*), or `tools/trace/osx_trace.py`
(Python, standard library only).

## 2. Header record

```json
{
  "record": "header",
  "schema_version": "0.2.0",
  "run_id": "run-131d1e28",
  "created_utc": "2026-10-22T09:30:00Z",
  "source": {
    "engine": "ripes", "engine_version": "v2.3.0-7-g7635a87",
    "adapter": "osx-ripes-live", "adapter_version": "0.1.0",
    "model": "RV32_5S", "model_name": "5-stage processor",
    "isa": "RV32I", "extensions": [],
    "fidelity": "educational-model",
    "fidelity_note": "Cycle-level educational datapath model built with VSRTL. ...",
    "source_file": "src/processors/RISC-V/rv5s/rv5s.h"
  },
  "time_domains": [{"name": "cpu_clk", "unit": "cycle", "description": "..."}],
  "program": {"entry_point": "0x00000000", "text_start": "0x00000000",
              "text_size": 28, "image_sha256": "..."},
  "processor": {
    "xlen": 32,
    "stages": [{"lane": 0, "index": 0, "name": "IF", "component": "soc.cpu0.pipeline.if"}, "..."],
    "register_files": [{"name": "gpr", "names": ["x0", "..."], "aliases": ["zero", "..."]}]
  },
  "initial_state": {"cycle": 0, "pc": "0x00000000",
                    "registers": {"gpr": ["0x00000000", "..."]}},
  "system": [
    {"path": "soc.cpu0", "type": "RV32_5S", "role": "cpu", "...": "..."},
    {"path": "soc.mem", "type": "AddressSpaceMM", "role": "memory", "...": "..."}
  ]
}
```

| Field | Meaning |
|---|---|
| `source.engine` / `engine_version` | Simulation engine and its version (Ripes: `git describe` of the build). |
| `source.adapter` / `adapter_version` | Component that converted engine output into this protocol. |
| `source.model` | Engine-specific model identifier (Ripes: `ProcessorID` name). |
| `source.fidelity` | Coarse fidelity class, see §6. Always read `fidelity_note` too. |
| `source.source_file` | Where the modelled behaviour is implemented (repository-relative). |
| `time_domains` | Clock domains used by events. Both adapters use one: `cpu_clk`. Engines with their own time base add `ticks_per_cycle` and `tick_unit` (gem5: ticks of 1 `ps`). |
| `program.image_sha256` | Identifies the program. Ripes: SHA-256 (hex) over name, address and bytes of every loaded section. gem5 converter: SHA-256 of the ELF file. |
| `processor.stages` | Stages in the order used by the producer; `component` links events to stages. |
| `initial_state` | Architectural state at `cycle` (= first cycle of the trace). `registers` may be empty: gem5 traces do not contain initial register values, and consumers must then treat a register as *unknown* until it is written (`TraceReplay::registerKnownAt()`), never as zero. |
| `system` | Optional (0.2.0): the hardware components the engine simulated, §2.1. |

### 2.1 System description (`system`)

One entry per simulated component; only components that the engine actually
simulates are listed. The Explorer's *System* view draws exactly these
components and connections; typical SoC blocks that are absent are drawn as
dashed "not simulated" boxes and never animated.

Example (gem5 O3CPU run of `loop_sum`, L1 instruction cache):

```json
{"path": "board.cache_hierarchy.l1i-cache-0", "type": "Cache", "role": "cache",
 "description": "Cache simulated by gem5 (classic memory system).",
 "parameters": {"size": 16384, "assoc": 8, "tag_latency": 1, "...": "..."},
 "parameter_info": {"size": "capacity in bytes", "assoc": "associativity (ways)", "...": "..."},
 "statistics": {"demandHits::total": 21, "demandMisses::total": 11, "...": "..."},
 "statistic_info": {"demandMisses::total": "number of demand (read+write) misses (Count)", "...": "..."},
 "connections": ["board.cache_hierarchy.l2buses"]}
```

| Field | Meaning |
|---|---|
| `path` | Hierarchical name. Ripes: `soc.cpu0`, `soc.mem`, `soc.mmio.<peripheral>`. gem5: the SimObject path from `config.json`. |
| `type` | Engine type name (Ripes `ProcessorID`, gem5 SimObject type). |
| `role` | `cpu`, `cache`, `prefetcher`, `interconnect`, `memory-controller`, `memory`, `peripheral`, `dma`, `accelerator` or `other`. |
| `parameters` | Configuration values as reported by the engine (gem5: `config.json`; sizes in bytes, latencies in cycles of the component's clock). |
| `parameter_info` | Optional: meaning and unit of each parameter. |
| `statistics` | End-of-simulation values reported by the engine (gem5: `stats.txt`), not computed by Open SoC Explorer. Values that are not finite numbers are omitted. |
| `statistic_info` | Optional: the engine's own description of each statistic (gem5: the `stats.txt` comment, verbatim). |
| `connections` | Paths of components this one sends requests to (gem5: requestor port → owner of the peer port; a prefetcher → the cache it belongs to). |

## 3. Event record

The structure follows the proposal in the project brief, with the additional
correlation, observability and state fields:

```json
{
  "record": "event",
  "schema_version": "0.2.0",
  "run_id": "run-131d1e28",
  "sequence": 31,
  "source": {"engine": "ripes", "model": "RV32_5S", "fidelity": "educational-model"},
  "time": {"domain": "cpu_clk", "cycle": 5, "tick": null},
  "event": {
    "type": "register.write",
    "component": "soc.cpu0.regfile.gpr",
    "instruction_id": "inst-1",
    "pc": "0x00000000",
    "assembly": "auipc x5 0x10000",
    "payload": {"regfile": "gpr", "index": 5, "name": "x5", "alias": "t0",
                "value": "0x10000000", "previous": "0x00000000",
                "attribution": "retiring-instruction"}
  },
  "correlation": {"cause_sequence": 25},
  "observability": "state-diff",
  "state": {"architectural": true, "speculative": false},
  "source_ref": "src/processors/RISC-V/rv5s/rv5s.h#getRegister"
}
```

(This and the following examples are real records exported from the Mission 2
example program on `RV32_5S`; long fields are abbreviated with `...`.)

| Field | Meaning |
|---|---|
| `sequence` | Unique, strictly increasing per run. Used for ordering and correlation. |
| `time.domain` | Clock domain of `cycle`. |
| `time.cycle` | Cycle number in that domain. A Ripes cycle is one `clock()` of the model. |
| `time.tick` | Engine tick (gem5: ticks of 1 ps) if the engine has one; `null` for Ripes. For gem5 traces `cycle = tick / ticks_per_cycle` of the CPU clock domain. **Ticks and cycles of different engines are never compared.** |
| `event.type` | See §4. |
| `event.component` | Hierarchical component path (`soc.cpu0.pipeline.ex`, `soc.cpu0.regfile.gpr`, `soc.cpu0.lsu`, `soc.mem.data`, `soc.mmio`). |
| `event.instruction_id` | Dynamic instruction instance (`inst-N`, in fetch order) or `null`. |
| `event.pc`, `event.assembly` | Address and disassembly of that instance. Values are hex strings so 64-bit values survive JSON. |
| `event.payload` | Type-specific data (§4). |
| `correlation.cause_sequence` | `sequence` of the event that caused this one (e.g. retire → register write; branch → squash; request → response). |
| `observability` | How the information was obtained (§5). |
| `state.architectural` | `true` if the event describes ISA-visible state (retirement, register writes, committed memory writes). |
| `state.speculative` | `true` if the instruction was later squashed (wrong path), `false` if it retired or the event is not tied to an instruction, `null` if unresolved when the trace ended. Computed at export time from the instance's outcome. |
| `source_ref` | Source location defining the modelled behaviour (`file#symbol`). |

## 4. Event types

Emitted by the Ripes adapter:

| Type | When (Ripes adapter) | Payload |
|---|---|---|
| `state.snapshot` | First event of every trace | `registers` (per file, hex), `reason` (`processor-reset` / `recording-started`) |
| `instruction.fetch` | A new instruction instance appears in the first stage | `stage`, `word` |
| `pipeline.stage_enter` | An instance moves into a stage (also emitted for the first stage) | `stage` (`lane` for multi-lane models) |
| `instruction.decode` | An instance enters the model's decode stage (`ID`) | ISA-decoded fields: `mnemonic`, `format`, `opcode`, `rd`, `rs1`, `rs2`, `imm`, `word` |
| `pipeline.stall` | `kind: "held"`: an instance stayed in its stage. `kind: "bubble"`: the model marks an empty stage as *stalled*; `origin` is `inserted` or `propagated` | `stage`, `kind`, `origin` |
| `pipeline.flush` | `kind: "squash"`: an instance left the pipeline without retiring. `kind: "bubble"`: empty stage marked *flushed* | `stage`, `kind`, `origin`/`reason`, `cause_instance`, `cause` |
| `instruction.retire` | The model's retirement counter increased | `retire_index`, `stage`; or `attribution: inferred-from-register-write` (see §7) |
| `register.write` | A register value differs from the previous cycle | `regfile`, `index`, `name`, `alias`, `value`, `previous`, `attribution` |
| `memory.request` | The model presents a data memory access | `access` (`read`/`write`), `address`, `bytes`, `region` (`memory`/`mmio`), `timing` |
| `memory.response` | Read data (same cycle) or committed write value (next cycle) | as request + `value` (`null` for MMIO) |

Defined by the schema but **never emitted by the Ripes adapter** (the Ripes
models have no such subsystems): `cache.hit`, `cache.miss`, `bus.transfer`,
`dma.start`, `dma.complete`, `accelerator.dispatch`, `accelerator.complete`,
`interrupt.raise`. Other adapters may emit them.

Example (stall bubble inserted by the hazard detection unit for the load-use
hazard in Mission 2):

```json
{"record":"event","sequence":28,"time":{"domain":"cpu_clk","cycle":5,"tick":null},
 "event":{"type":"pipeline.stall","component":"soc.cpu0.pipeline.ex","instruction_id":null,
          "pc":null,"assembly":null,"payload":{"kind":"bubble","origin":"inserted","stage":"EX"}},
 "observability":"model-derived","state":{"architectural":false,"speculative":false},
 "source_ref":"src/processors/RISC-V/rv5s/rv5s.h#stageInfo", "...": "..."}
```

Example (load request and response, zero-latency memory):

```json
{"sequence":32,"event":{"type":"memory.request","component":"soc.cpu0.lsu","instruction_id":"inst-3",
  "assembly":"lw x6 0 x5","payload":{"access":"read","address":"0x10000000","bytes":4,
  "region":"memory","timing":"presented on the data memory port in this cycle"}},
  "observability":"direct", "...": "..."}
{"sequence":33,"event":{"type":"memory.response","component":"soc.mem.data","instruction_id":"inst-3",
  "payload":{"access":"read","address":"0x10000000","bytes":4,"region":"memory",
  "timing":"combinational read (zero-latency memory)","value":"0x00000007"}},
  "correlation":{"cause_sequence":32},"observability":"direct", "...": "..."}
```

## 5. Observability levels

| Level | Meaning | Ripes examples |
|---|---|---|
| `direct` | Read directly from a model output | data memory port (`dataMemAccess()`), register snapshot |
| `model-derived` | From the model's own interface | stage PCs/validity/state (`stageInfo()`), retirement counter |
| `state-diff` | Inferred by comparing architectural state between cycles | `register.write` |
| `inferred` | Produced by a heuristic | instruction identity across stages, squash causes, retirements the model did not count |
| `isa-decoded` | Decoded by Open SoC Explorer from the instruction encoding (RISC-V spec), not from the model's decoder | `instruction.decode` |

Consumers must not present `inferred` information as measured fact.

## 6. Fidelity classes

| `source.fidelity` | Meaning |
|---|---|
| `educational-model` | Textbook datapath model (Ripes VSRTL models). Cycle counts are properties of the model only. |
| `functional-iss` | Instruction set simulator: architectural results only, no timing model (Ripes ISS, gem5 `AtomicSimpleCPU`). |
| `cycle-level-simulator` | Cycle-level microarchitecture simulator with a configurable memory system (gem5 `TimingSimpleCPU`, `O3CPU`). Timing is a property of the configured model, not of a specific chip. |
| *(reserved)* `rtl-cycle-accurate` | For the future RTL adapter. |

See [SIMULATION_FIDELITY.md](SIMULATION_FIDELITY.md).

## 7. Semantics and guarantees of the Ripes adapter

* **Cycle numbering.** Events at cycle `c` describe the transition from
  `c − 1` to `c` and the model state at `c` (after the clock edge and
  combinational propagation). Cycle 0 is the state after reset.
* **Stage occupancy.** An instance occupies a stage from its `stage_enter`
  cycle until the cycle before its next `stage_enter`, its `instruction.retire`
  or its `pipeline.flush` (`kind: squash`). `TraceReplay::pipelineAt()`
  implements exactly this rule; `tst_trace` checks it against the live model on
  all ten models.
* **Instance identity** is inferred: an instruction in stage *s* at cycle *c*
  is the one that was in stage *s − 1* (moved) or *s* (held) at *c − 1* with
  the same PC, processed from the back of the pipeline to the front.
* **Retirement** follows the model's retirement counter, attributed to the
  oldest valid instructions (last stage first). If an instruction leaves the
  last stage without being counted while its decoded destination register
  changes in the same transition, it is reported as an `inferred` retirement
  with `model_retire_counter: "did not count this instruction"`; the footer
  caveats mention it. (This happens with Ripes' dual-issue model, see
  SIMULATION_FIDELITY.md.)
* **Register writes** are detected by comparing register files; writing the
  value a register already holds is invisible. Writes are attributed to the
  retiring instruction whose decoded `rd` matches (`attribution` tells how);
  otherwise `attribution: unexplained` (e.g. system-call side effects).
  Register initialisation applied by Ripes right after reset is folded into
  the initial state.
* **Memory values** are sampled with side-effect-free reads, except for MMIO
  regions, whose values are reported as `null`.
* **Reverse execution.** When the processor is reversed, events after the new
  cycle are discarded and recording continues; the result is identical to an
  uninterrupted run (tested).
* **Bounded recording.** Recording stops after `maxCycles` cycles
  (GUI default 5 000, CLI default 1 000 000); the footer then reports
  `truncated: true, truncation_reason: "max-cycles"`.
* **Determinism.** Two runs of the same program on the same model produce
  identical event streams apart from `run_id` and `created_utc` (tested).

## 8. Footer record

```json
{"record":"footer","schema_version":"0.2.0","run_id":"run-131d1e28",
 "completeness":{"complete":true,"starts_at_reset":true,"start_cycle":0,"end_cycle":12,
                 "truncated":false,"truncation_reason":null,"program_finished":true,
                 "caveats":["Instruction instance identity and squash causes are inferred ...",
                            "Register writes are detected by comparing register files ..."]},
 "summary":{"cycles":12,"events":72,"instructions_retired":7}}
```

`complete` is `starts_at_reset && !truncated && program_finished`. Missions
only score complete traces.

## 9. The gem5 converter

`tools/gem5/gem5_to_osx.py` (offline; see
[tools/gem5/README.md](../tools/gem5/README.md) and
[SIMULATION_FIDELITY.md §6](SIMULATION_FIDELITY.md)) emits:

| Type | gem5 source | Observability |
|---|---|---|
| `instruction.fetch`, `instruction.decode`, `pipeline.stage_enter` (stages `F`, `DEC`, `REN`, `DIS`, `ISS`, `CMP`), `instruction.retire` | O3CPU `O3PipeView` | `direct` |
| `pipeline.flush` `kind: squash` | O3CPU instruction that never committed | `inferred` (its time is not recorded) |
| `instruction.decode`, `pipeline.stage_enter` (stage `EXEC`) | Simple CPUs, `Exec` record tick | `direct` |
| `instruction.retire` | Simple CPUs, start of the next instruction | `inferred` |
| `register.write` | `Exec` `D=` (integer registers) | `direct`, placed at commit |
| `memory.request` | `Exec` `A=` | `direct` address, placed at commit |

`initial_state.registers` is empty, `time.tick` holds gem5 ticks and the
header `system` comes from `config.json` and `stats.txt`.

## 10. Guidance for other engines

* Use your engine's native time as `time.tick` with its unit documented in a
  `time_domains` entry; only fill `time.cycle` for a real clock of a named
  domain. Never convert gem5 ticks to Ripes cycles.
* Use `fidelity` and `fidelity_note` to state what the model is (e.g. gem5
  `O3CPU` with a given configuration; Ibex RTL at a pinned commit).
* Emit only what the engine observes; use `inferred` for anything derived.
* Prefer offline traces first; live streaming can reuse the same records.

## 11. Versioning

* Patch: clarifications, no format change.
* Minor (0.x → 0.y): backwards-compatible additions (new event types, new
  optional fields). Readers must ignore unknown fields.
* Major: breaking changes. `readJsonl()` rejects traces with a different major
  version.
