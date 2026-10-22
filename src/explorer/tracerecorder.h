// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QObject>

#include <map>
#include <mutex>
#include <vector>

#include "modelprofile.h"
#include "processors/interface/ripesprocessor.h"
#include "riscvdecoder.h"
#include "tracetypes.h"

namespace Ripes {
class Program;
namespace Explorer {

/// SHA-256 (hex) over the name, address and contents of every section of
/// @p program. Identifies the loaded program image in traces.
QString programImageHash(const Program &program);

/**
 * @brief The TraceRecorder class
 * Ripes simulation adapter for the Open SoC Explorer trace protocol.
 *
 * The recorder observes the currently selected Ripes processor through the
 * simulator-agnostic RipesProcessor interface (via ProcessorHandler signals)
 * and converts the per-cycle model state into normalized trace events:
 * instruction fetch/decode/retire, pipeline stage entries, stalls, flushes,
 * register writes and data memory accesses. Every event is labelled with how
 * it was obtained (see Trace::Observability).
 *
 * The recorder never drives the simulation; it only reads model state after
 * each clock. Recording is bounded by maxCycles(); once reached the trace is
 * marked as truncated.
 *
 * Thread-safety: the per-cycle handler runs in the thread that clocks the
 * processor (a worker thread while running). All state is protected by an
 * internal mutex; trace() returns a consistent copy.
 */
class TraceRecorder : public QObject {
  Q_OBJECT
public:
  /// If @p attach is true, the recorder connects to the ProcessorHandler
  /// singleton and starts recording from the current processor state.
  explicit TraceRecorder(QObject *parent = nullptr, bool attach = true);
  ~TraceRecorder() override;

  bool isEnabled() const;
  /// Enabling recording while the processor is not at reset starts a new,
  /// partial trace from the current state.
  void setEnabled(bool enabled);

  qint64 maxCycles() const;
  void setMaxCycles(qint64 cycles);

  /// Returns a consistent copy of the trace recorded so far.
  Trace::Trace trace() const;
  quint64 eventCount() const;
  /// Monotonic counter that changes whenever the recorded trace changes.
  quint64 revision() const;

  bool exportJsonl(const QString &path, QString *error = nullptr) const;

public slots:
  /// Starts a new trace from the current processor state.
  void restart();

signals:
  /// Emitted whenever a new trace (run) is started.
  void runStarted();

private:
  struct StageSlot {
    bool valid = false;
    quint64 pc = 0;
    StageInfo::State state = StageInfo::State::None;
    qint64 instance = -1;
  };
  struct CycleState {
    qint64 cycle = 0;
    long long retired = 0;
    bool finished = false;
    std::vector<StageSlot> stageSlots;
    std::map<QString, std::vector<quint64>> regs;
  };
  struct InstanceRec {
    quint64 pc = 0;
    QString assembly;
    RVDecoded decoded;
    quint64 lastSequence = 0;
  };
  struct PendingWrite {
    quint64 address = 0;
    unsigned bytes = 0;
    qint64 instance = -1;
    quint64 requestSequence = 0;
  };

  void attachToProcessorHandler();
  void onProcessorReset();
  void onProcessorClocked();
  void onProcessorReversed();
  void onProcessorStateChanged();

  void beginRunLocked();
  CycleState captureLocked() const;
  void recordTransitionLocked(CycleState &cur);
  void rebuildAfterTruncationLocked();

  qint64 newInstanceLocked(quint64 pc, qint64 cycle);
  quint64 emitLocked(Trace::TraceEvent ev);
  QString disassembleLocked(quint64 pc);
  QVariantMap decodedPayload(const RVDecoded &d) const;
  int stageIndex(unsigned lane, unsigned index) const;
  std::vector<unsigned> lanes() const;
  qint64 instanceInStage(const CycleState &s, const QString &stageName) const;
  void fillCompleteness(Trace::Trace &t) const;

  mutable std::mutex m_mutex;
  bool m_enabled = true;
  bool m_active = false;
  bool m_truncated = false;
  QString m_truncationReason;
  qint64 m_maxCycles = 5000;
  quint64 m_revision = 0;
  unsigned m_unattributedWrites = 0;
  unsigned m_inferredRetirements = 0;
  /// Model retirement counter value at the start of the trace.
  long long m_initialRetired = 0;
  bool m_sawMmio = false;

  ModelProfile m_profile;
  Trace::Trace m_trace;
  /// Register file identifiers, parallel to m_trace.header.registerFiles.
  std::vector<std::string> m_regFileIds;
  quint64 m_nextSequence = 0;
  qint64 m_nextInstance = 1;
  /// Index of the state.snapshot event describing the initial state.
  std::optional<size_t> m_snapshotEvent;

  CycleState m_last;
  /// Stage occupancy for each recorded cycle (index = cycle - startCycle);
  /// used to resume recording after the processor is reversed.
  std::vector<std::vector<StageSlot>> m_history;
  std::map<qint64, InstanceRec> m_instances;
  std::vector<PendingWrite> m_pendingWrites;
  std::map<quint64, QString> m_disasmCache;
};

} // namespace Explorer
} // namespace Ripes
