// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "modelprofile.h"

namespace Ripes {
namespace Explorer {

namespace {
const char *kVsrtlNote =
    "Cycle-level educational datapath model built with VSRTL. It models the "
    "textbook datapath of this processor with ideal single-cycle memories. It "
    "is not a model of any specific commercial or open-source RTL core; cycle "
    "counts are properties of this model only.";

const char *kIssNote =
    "Functional instruction set simulator: one instruction is executed per "
    "clock() call. Architectural results are meaningful; there is no "
    "datapath, pipeline or timing model.";
} // namespace

ModelProfile modelProfile(ProcessorID id) {
  ModelProfile p;
  p.id = id;
  p.idName = enumToString<ProcessorID>(id);
  p.name = ProcessorRegistry::getDescription(id).name;
  p.fidelity = "educational-model";
  p.fidelityNote = kVsrtlNote;

  switch (id) {
  case ProcessorID::RV32_ISS:
  case ProcessorID::RV64_ISS:
    p.fidelity = "functional-iss";
    p.fidelityNote = kIssNote;
    p.sourceFile = "src/processors/RISC-V/rviss/rviss.h";
    p.memAccessReportedAfterExecution = true;
    p.branchResolution = "Architectural: the next PC is computed when the "
                         "instruction executes.";
    break;
  case ProcessorID::RV32_SS:
  case ProcessorID::RV64_SS:
    p.sourceFile = "src/processors/RISC-V/rvss/rvss.h";
    p.branchResolution =
        "Single-cycle: the next PC is selected in the same cycle.";
    break;
  case ProcessorID::RV32_5MC_2M:
  case ProcessorID::RV64_5MC:
    p.sourceFile = "src/processors/RISC-V/rv5mc/rv5mc_2m.h";
    p.branchResolution =
        "Multi-cycle FSM: one instruction at a time, no speculation.";
    break;
  case ProcessorID::RV32_5MC_1M:
  case ProcessorID::RV64_5MC_1M:
    p.sourceFile = "src/processors/RISC-V/rv5mc/rv5mc_1m.h";
    p.branchResolution =
        "Multi-cycle FSM: one instruction at a time, no speculation.";
    break;
  case ProcessorID::RV32_5S_NO_FW_HZ:
  case ProcessorID::RV64_5S_NO_FW_HZ:
    p.sourceFile = "src/processors/RISC-V/rv5s_no_fw_hz/rv5s_no_fw_hz.h";
    p.pipelined = true;
    break;
  case ProcessorID::RV32_5S_NO_HZ:
  case ProcessorID::RV64_5S_NO_HZ:
    p.sourceFile = "src/processors/RISC-V/rv5s_no_hz/rv5s_no_hz.h";
    p.pipelined = true;
    p.hasForwarding = true;
    break;
  case ProcessorID::RV32_5S_NO_FW:
  case ProcessorID::RV64_5S_NO_FW:
    p.sourceFile = "src/processors/RISC-V/rv5s_no_fw/rv5s_no_fw.h";
    p.pipelined = true;
    p.hasHazardDetection = true;
    break;
  case ProcessorID::RV32_5S:
  case ProcessorID::RV64_5S:
    p.sourceFile = "src/processors/RISC-V/rv5s/rv5s.h";
    p.pipelined = true;
    p.hasForwarding = true;
    p.hasHazardDetection = true;
    break;
  case ProcessorID::RV32_6S_DUAL:
  case ProcessorID::RV64_6S_DUAL:
    p.sourceFile = "src/processors/RISC-V/rv6s_dual/rv6s_dual.h";
    p.pipelined = true;
    p.hasForwarding = true;
    p.hasHazardDetection = true;
    p.branchResolution = "See the model source; two in-order lanes.";
    break;
  case ProcessorID::NUM_PROCESSORS:
    break;
  }

  if (p.pipelined) {
    p.decodeStage = "ID";
    p.memoryStage = "MEM";
    if (p.branchResolution.isEmpty())
      p.branchResolution =
          "Branches and jumps are resolved in EX. Instructions are always "
          "fetched sequentially (static not-taken); a taken branch or jump "
          "flushes the younger instructions in IF and ID.";
  }
  return p;
}

} // namespace Explorer
} // namespace Ripes
