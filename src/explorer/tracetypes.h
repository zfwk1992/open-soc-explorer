// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer. See docs/TRACE_PROTOCOL.md for the schema.
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <map>
#include <optional>
#include <vector>

namespace Ripes {
namespace Trace {

/// Version of the Open SoC Explorer trace schema emitted and accepted by this
/// build. Bump the minor version for backwards compatible additions and the
/// major version for breaking changes.
inline constexpr const char *kSchemaVersion = "0.2.0";

/// Version of the Open SoC Explorer extensions layered on top of Ripes.
inline constexpr const char *kExplorerVersion = "0.1.0";

/// Default clock domain of the Ripes adapter. A Ripes cycle is one clock() call
/// of the selected processor model; it is not related to wall-clock time, gem5
/// ticks or the cycles of any real RTL implementation.
inline constexpr const char *kRipesClockDomain = "cpu_clk";

enum class EventType {
  StateSnapshot,
  InstructionFetch,
  InstructionDecode,
  InstructionRetire,
  StageEnter,
  PipelineStall,
  PipelineFlush,
  RegisterWrite,
  MemoryRequest,
  MemoryResponse,
  // The following types are part of the schema so that other engines (gem5,
  // RTL, accelerator models) can emit them. The Ripes adapter never produces
  // them, since the Ripes models do not contain these subsystems.
  CacheHit,
  CacheMiss,
  BusTransfer,
  DmaStart,
  DmaComplete,
  AcceleratorDispatch,
  AcceleratorComplete,
  InterruptRaise,
};

/// Returns the schema name of an event type, e.g. "pipeline.stall".
QString eventTypeName(EventType type);
/// Parses a schema event type name. Returns std::nullopt for unknown names.
std::optional<EventType> eventTypeFromName(const QString &name);
/// All event types, in schema order.
const std::vector<EventType> &allEventTypes();

/// How the information in an event was obtained. See TRACE_PROTOCOL.md.
enum class Observability {
  /// Read directly from a model output (e.g. a memory port).
  Direct,
  /// Derived from the simulator model's own interface (stage PCs/states).
  ModelDerived,
  /// Inferred by comparing architectural state between two cycles.
  StateDiff,
  /// Correlation produced by a heuristic (e.g. instruction identity).
  Inferred,
  /// Decoded by Open SoC Explorer from the instruction encoding (ISA spec).
  IsaDecoded,
};

QString observabilityName(Observability o);
std::optional<Observability> observabilityFromName(const QString &name);

/// Formats an address/register value as a fixed width hexadecimal string.
QString toHex(quint64 value, unsigned bits = 64);
/// Parses hexadecimal ("0x..") or decimal strings. Returns std::nullopt on
/// failure.
std::optional<quint64> fromHex(const QString &str);

/// Formats a dynamic instruction instance id ("inst-42").
QString instanceName(qint64 instance);
/// Parses a dynamic instruction instance id. Returns -1 on failure.
qint64 instanceFromName(const QString &name);

struct TraceEvent {
  quint64 sequence = 0;
  qint64 cycle = 0;
  /// Engine tick (e.g. gem5 ticks), if the engine has one.
  std::optional<qint64> tick;
  EventType type = EventType::StateSnapshot;
  /// Hierarchical component path, e.g. "soc.cpu0.pipeline.ex".
  QString component;
  /// Dynamic instruction instance this event belongs to; -1 if none.
  qint64 instance = -1;
  std::optional<quint64> pc;
  QString assembly;
  QVariantMap payload;
  Observability observability = Observability::ModelDerived;
  /// True if the event describes ISA-visible (architectural) state.
  bool architectural = false;
  /// Sequence number of the event that caused this one (correlation).
  std::optional<quint64> causeSequence;
  /// Location in the simulator source code that defines the modelled
  /// behaviour, e.g. "src/processors/RISC-V/rv5s/rv5s.h#stageInfo".
  QString sourceRef;

  bool operator==(const TraceEvent &other) const = default;
};

struct StageDesc {
  unsigned lane = 0;
  unsigned index = 0;
  QString name;
  QString component;
  bool operator==(const StageDesc &other) const = default;
};

struct RegisterFileDesc {
  QString name;
  QStringList names;
  QStringList aliases;
  bool operator==(const RegisterFileDesc &other) const = default;
};

struct TraceSource {
  QString engine;
  QString engineVersion;
  QString adapter;
  QString adapterVersion;
  QString model;
  QString modelName;
  QString isa;
  QStringList extensions;
  /// Coarse fidelity class, e.g. "educational-model" or "functional-iss".
  QString fidelity;
  /// Human readable explanation of what the model does and does not capture.
  QString fidelityNote;
  QString sourceFile;
  bool operator==(const TraceSource &other) const = default;
};

/// A hardware component that the producing engine actually simulated
/// (schema >= 0.2.0). Components that are not simulated must not be listed.
struct SystemComponent {
  /// Hierarchical path, e.g. "soc.cpu0" or "board.cache_hierarchy.l1d-cache-0".
  QString path;
  /// Engine-specific type, e.g. "RV5S" or "Cache".
  QString type;
  /// One of "cpu", "cache", "interconnect", "memory-controller", "memory",
  /// "peripheral", "other".
  QString role;
  QString description;
  /// Configuration parameters as reported by the engine.
  QVariantMap parameters;
  /// Aggregate statistics as reported by the engine (end of simulation).
  QVariantMap statistics;
  /// Paths of components this one is connected to (downstream), as reported
  /// by the engine. Views must not draw connections that are not listed.
  QStringList connections;
  bool operator==(const SystemComponent &other) const = default;
};

struct TraceHeader {
  QString schemaVersion = kSchemaVersion;
  QString runId;
  QString createdUtc;
  TraceSource source;
  QString clockDomain = kRipesClockDomain;
  /// Human readable description of the clock domain.
  QString clockDomainDescription;
  /// If the engine has a tick-based time base (e.g. gem5), the number of
  /// ticks per cycle of clockDomain and the tick unit; 0 otherwise.
  qint64 ticksPerCycle = 0;
  QString tickUnit;
  unsigned xlen = 32;
  std::vector<StageDesc> stages;
  std::vector<RegisterFileDesc> registerFiles;
  quint64 entryPoint = 0;
  quint64 textStart = 0;
  quint64 textSize = 0;
  /// SHA-256 of the loaded program image (see programImageHash()).
  QString programHash;
  /// Architectural state at startCycle.
  qint64 startCycle = 0;
  quint64 initialPc = 0;
  std::map<QString, std::vector<quint64>> initialRegisters;
  /// Simulated hardware components (may be empty).
  std::vector<SystemComponent> system;
};

struct TraceCompleteness {
  /// True if recording started from processor reset (cycle 0).
  bool startsAtReset = true;
  qint64 startCycle = 0;
  /// Last cycle covered by the trace.
  qint64 endCycle = 0;
  /// True if recording stopped before the program finished, e.g. because the
  /// configured cycle limit was reached.
  bool truncated = false;
  QString truncationReason;
  /// True if the processor reported finished() at endCycle.
  bool programFinished = false;
  quint64 instructionsRetired = 0;
  /// Known limitations of this trace (e.g. unattributed register writes).
  QStringList caveats;
};

struct Trace {
  TraceHeader header;
  std::vector<TraceEvent> events;
  TraceCompleteness completeness;

  /// True if the trace covers the whole execution from reset until the
  /// processor finished, without truncation.
  bool isComplete() const {
    return completeness.startsAtReset && !completeness.truncated &&
           completeness.programFinished;
  }
};

} // namespace Trace
} // namespace Ripes
