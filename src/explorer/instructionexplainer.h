// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QStringList>

#include <optional>
#include <vector>

#include "riscvdecoder.h"
#include "tracereplay.h"

namespace Ripes {
namespace Explorer {

/**
 * @brief The InstructionExplanation struct
 * Everything Open SoC Explorer can say about one dynamic instruction, built
 * exclusively from a recorded trace (simulator outputs) and the RISC-V
 * specification (decoding and semantics). Values that are derived rather than
 * observed are labelled as such when rendered.
 */
struct InstructionExplanation {
  qint64 instance = -1;
  quint64 pc = 0;
  QString assembly;
  RVDecoded decoded;
  std::optional<RVSemantics> semantics;
  TraceReplay::Outcome outcome = TraceReplay::Outcome::InFlight;
  qint64 firstCycle = -1;
  qint64 endCycle = -1;
  unsigned xlen = 32;

  struct Operand {
    QString role; // "rs1", "rs2"
    QString name; // "x1 (ra)"
    unsigned index = 0;
    /// Architectural value before the instruction retired.
    std::optional<quint64> value;
  };
  std::vector<Operand> operands;

  struct RegisterResult {
    QString name;
    quint64 value = 0;
    quint64 previous = 0;
    qint64 cycle = -1;
  };
  /// Register write observed in the trace and attributed to this instruction.
  std::optional<RegisterResult> result;
  /// Result predicted by the ISA semantics from the architectural operands.
  std::optional<quint64> expected;

  std::optional<bool> branchTaken;     ///< From the retirement order.
  std::optional<bool> branchCondition; ///< ISA evaluation of the condition.
  std::optional<quint64> nextRetiredPc;

  struct MemoryAccess {
    QString access;
    quint64 address = 0;
    unsigned bytes = 0;
    std::optional<quint64> value;
    qint64 cycle = -1;
  };
  std::vector<MemoryAccess> memory;

  struct Step {
    qint64 cycle = -1;
    QString where;
    QString what;
  };
  std::vector<Step> journey;

  /// Data dependencies on older instructions that were still in flight.
  QStringList hazards;
  /// Checks of the model output against the ISA semantics.
  QStringList findings;
  /// Fidelity notes about the model that produced the trace.
  QStringList notes;
  /// Source files implementing the behaviour, relative to the repository.
  QStringList sourceRefs;
};

/// Builds the explanation of instance @p id of @p replay.
InstructionExplanation explainInstance(const TraceReplay &replay, qint64 id);

/// Renders @p e as HTML for a QTextBrowser. @p dark selects colors with
/// sufficient contrast on dark backgrounds.
QString explanationToHtml(const InstructionExplanation &e, bool dark);

} // namespace Explorer
} // namespace Ripes
