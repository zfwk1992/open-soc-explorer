// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <vector>

#include "tracetypes.h"

namespace Ripes {
namespace Explorer {

/**
 * @brief The TraceReplay class
 * Deterministic, simulator-independent replay of a recorded trace. Given only
 * the events of a trace (live or loaded from a JSON Lines file), it
 * reconstructs for any recorded cycle:
 *  - which dynamic instruction occupies which pipeline stage,
 *  - the architectural register state (initial state + register writes),
 *  - per-instruction histories and aggregate statistics.
 *
 * Stepping backwards in time is supported by reconstruction; the simulator
 * itself does not need to support reverse execution.
 */
class TraceReplay {
public:
  enum class Outcome { InFlight, Retired, Squashed };

  struct Instance {
    qint64 id = -1;
    quint64 pc = 0;
    QString assembly;
    qint64 firstCycle = -1;
    /// Cycle at which the instance retired or was squashed; -1 if in flight.
    qint64 endCycle = -1;
    Outcome outcome = Outcome::InFlight;
    /// (cycle, stage index) of each stage entry, in order.
    std::vector<std::pair<qint64, int>> stageEntries;
    /// Cycles during which the instance was held in its stage.
    std::vector<qint64> heldCycles;
    /// Indices into trace().events belonging to this instance.
    std::vector<size_t> events;
    /// Index of the retire event, if any.
    std::optional<size_t> retireEvent;
  };

  enum class SlotKind { Empty, Instruction, StallBubble, FlushBubble };
  struct Slot {
    int stage = -1;
    SlotKind kind = SlotKind::Empty;
    /// Oldest instruction in the stage (models such as gem5's O3CPU can hold
    /// several instructions per stage).
    qint64 instance = -1;
    bool held = false;
    /// Number of instructions in the stage.
    int occupants = 0;
  };

  struct Stats {
    qint64 cycles = 0;
    quint64 retired = 0;
    /// Number of cycles in which at least one instruction was held or a stall
    /// bubble was inserted.
    quint64 stallCycles = 0;
    /// Number of stall bubbles inserted by the model (not counting their
    /// propagation down the pipeline).
    quint64 stallBubbles = 0;
    quint64 squashed = 0;
    quint64 flushBubbles = 0;
    quint64 registerWrites = 0;
    quint64 unattributedRegisterWrites = 0;
    quint64 memoryReads = 0;
    quint64 memoryWrites = 0;
    quint64 retiredBranches = 0;
    quint64 retiredTakenBranches = 0;
    double cpi = 0.0;
    std::map<QString, quint64> retiredMnemonics;
  };

  using RegisterState = std::map<QString, std::vector<quint64>>;

  explicit TraceReplay(Trace::Trace trace);

  const Trace::Trace &trace() const { return m_trace; }
  qint64 firstCycle() const { return m_firstCycle; }
  qint64 lastCycle() const { return m_lastCycle; }

  /// Pipeline occupancy at @p cycle, one slot per stage in header order.
  std::vector<Slot> pipelineAt(qint64 cycle) const;
  /// Architectural register state visible at @p cycle.
  RegisterState registersAt(qint64 cycle) const;
  /// True if the value of register @p index of @p regfile at @p cycle is
  /// known: the trace provides its initial value or it was written at or
  /// before @p cycle. Engines such as gem5 do not report initial registers.
  bool registerKnownAt(const QString &regfile, int index, qint64 cycle) const;
  /// Indices of events recorded at @p cycle.
  std::vector<size_t> eventsAt(qint64 cycle) const;

  const std::map<qint64, Instance> &instances() const { return m_instances; }
  const Instance *instance(qint64 id) const;
  /// Stage index occupied by @p id at @p cycle, or -1.
  int stageOf(qint64 id, qint64 cycle) const;
  /// True if @p id was held (stalled) in its stage at @p cycle.
  bool isHeld(qint64 id, qint64 cycle) const;
  /// Instances occupying stage @p stage at @p cycle, oldest first.
  std::vector<qint64> occupantsOf(int stage, qint64 cycle) const;
  /// Instances retired in program order.
  const std::vector<qint64> &retirementOrder() const { return m_retireOrder; }

  /// Register state immediately before @p id retired, i.e. the architectural
  /// operand values of an in-order processor: all writes of earlier cycles
  /// plus the writes of instances that retired earlier in the same cycle
  /// (superscalar and out-of-order models commit several instructions per
  /// cycle). std::nullopt if the instance did not retire.
  std::optional<RegisterState> registersBeforeRetire(qint64 id) const;
  /// True if register @p index of @p regfile is known in
  /// registersBeforeRetire(@p id).
  bool registerKnownBeforeRetire(qint64 id, const QString &regfile,
                                 int index) const;

  /// Mnemonic (first token of the disassembly) of an instance.
  QString mnemonicOf(qint64 id) const;

  Stats stats() const;

private:
  void build();

  Trace::Trace m_trace;
  qint64 m_firstCycle = 0;
  qint64 m_lastCycle = 0;
  std::map<qint64, Instance> m_instances;
  std::vector<qint64> m_retireOrder;
  /// Position of each retired instance in m_retireOrder.
  std::map<qint64, size_t> m_retirePosition;
  std::map<QString, int> m_stageByComponent;
  /// occupancy[cycle - first][stage]
  std::vector<std::vector<Slot>> m_occupancy;
  std::vector<std::pair<size_t, size_t>> m_eventRange; // per cycle [begin,end)
  /// Register checkpoints every kCheckpointInterval cycles.
  std::vector<RegisterState> m_checkpoints;
  struct RegWrite {
    qint64 cycle;
    QString regfile;
    int index;
    quint64 value;
    qint64 instance;
  };
  /// True if @p rw is visible to @p inst immediately before it retires.
  bool visibleBeforeRetire(const RegWrite &rw, const Instance &inst) const;
  std::vector<RegWrite> m_regWrites;
  static constexpr qint64 kCheckpointInterval = 64;
};

} // namespace Explorer
} // namespace Ripes
