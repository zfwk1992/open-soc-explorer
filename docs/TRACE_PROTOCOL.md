# Open SoC Explorer trace protocol

**Schema version: 0.1.0** (`Ripes::Trace::kSchemaVersion`,
`src/explorer/tracetypes.h`)

The trace protocol is a simulator-neutral, versioned format for execution
events. The Ripes adapter (`src/explorer/tracerecorder.cpp`) produces it today;
future gem5, RTL (Verilator/Ibex) and accelerator adapters are meant to produce
the same format so that the Explorer GUI, the replay engine and the missions
work unchanged on top of them.

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

Read them with `Ripes::Trace::readJsonl()` (C++), the *Explorer* tab
(*Open a trace file for offline replay…*), or `tools/trace/osx_trace.py`
(Python, standard library only).

## 2. Header record

```json
{
  "record": "header",
  "schema_version": "0.1.0",
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
                    "registers": {"gpr": ["0x00000000", "..."]}}
}
```

| Field | Meaning |
|---|---|
| `source.engine` / `engine_version` | Simulation engine and its version (Ripes: `git describe` of the build). |
| `source.adapter` / `adapter_version` | Component that converted engine output into this protocol. |
| `source.model` | Engine-specific model identifier (Ripes: `ProcessorID` name). |
| `source.fidelity` | Coarse fidelity class, see §6. Always read `fidelity_note` too. |
| `source.source_file` | Where the modelled behaviour is implemented (repository-relative). |
| `time_domains` | Clock domains used by events. Ripes uses one: `cpu_clk`. |
| `program.image_sha256` | SHA-256 (hex) over name, address and bytes of every loaded section; identifies the program image. |
| `processor.stages` | Stages in the order used by the producer; `component` links events to stages. |
| `initial_state` | Architectural state at `cycle` (= first cycle of the trace). |

## 3. Event record

The structure follows the proposal in the project brief, with the additional
correlation, observability and state fields:

```json
{
  "record": "event",
  "schema_version": "0.1.0",
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
| `time.tick` | Engine tick (e.g. gem5 ticks, ps) if the engine has one; `null` for Ripes. **Ticks and cycles are never interchangeable.** |
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
| `functional-iss` | Instruction set simulator: architectural results only, no timing. |
| *(reserved)* `architectural-simulator`, `rtl-cycle-accurate` | For future gem5 and RTL adapters. |

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
{"record":"footer","schema_version":"0.1.0","run_id":"run-131d1e28",
 "completeness":{"complete":true,"starts_at_reset":true,"start_cycle":0,"end_cycle":12,
                 "truncated":false,"truncation_reason":null,"program_finished":true,
                 "caveats":["Instruction instance identity and squash causes are inferred ...",
                            "Register writes are detected by comparing register files ..."]},
 "summary":{"cycles":12,"events":72,"instructions_retired":7}}
```

`complete` is `starts_at_reset && !truncated && program_finished`. Missions
only score complete traces.

## 9. Guidance for other engines

* Use your engine's native time as `time.tick` with its unit documented in a
  `time_domains` entry; only fill `time.cycle` for a real clock of a named
  domain. Never convert gem5 ticks to Ripes cycles.
* Use `fidelity` and `fidelity_note` to state what the model is (e.g. gem5
  `O3CPU` with a given configuration; Ibex RTL at a pinned commit).
* Emit only what the engine observes; use `inferred` for anything derived.
* Prefer offline traces first; live streaming can reuse the same records.

## 10. Versioning

* Patch: clarifications, no format change.
* Minor (0.x → 0.y): backwards-compatible additions (new event types, new
  optional fields). Readers must ignore unknown fields.
* Major: breaking changes. `readJsonl()` rejects traces with a different major
  version.
