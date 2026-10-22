// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QString>

#include "processorregistry.h"

namespace Ripes {
namespace Explorer {

/**
 * @brief The ModelProfile struct
 * Static description of a Ripes processor model as seen by the Open SoC
 * Explorer trace adapter: which fidelity class it belongs to, where the model
 * is implemented, and which of its stages perform decode and data memory
 * access. The information is derived from the model sources under
 * src/processors/RISC-V/ and is used to label traces honestly.
 */
struct ModelProfile {
  ProcessorID id = ProcessorID::RV32_5S;
  /// ProcessorID enumerator name, e.g. "RV32_5S".
  QString idName;
  QString name;
  /// Coarse fidelity class: "educational-model" (VSRTL datapath models) or
  /// "functional-iss" (instruction set simulator without a datapath).
  QString fidelity;
  QString fidelityNote;
  /// Path of the model implementation relative to the repository root.
  QString sourceFile;
  /// Stage name in which instructions are decoded ("" if not a separate stage).
  QString decodeStage;
  /// Stage name in which data memory is accessed ("" if not a separate stage).
  QString memoryStage;
  /// True if dataMemAccess() describes the access of the instruction that
  /// just executed (ISS), instead of the access currently presented on the
  /// datapath's memory port (VSRTL models).
  bool memAccessReportedAfterExecution = false;
  bool pipelined = false;
  bool hasForwarding = false;
  bool hasHazardDetection = false;
  /// Short description of how control flow changes are resolved.
  QString branchResolution;
};

/// Returns the profile for @p id.
ModelProfile modelProfile(ProcessorID id);

} // namespace Explorer
} // namespace Ripes
