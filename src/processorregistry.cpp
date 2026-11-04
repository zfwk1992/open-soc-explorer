#include "processorregistry.h"

#include <QCoreApplication>
#include <QPolygonF>

#include "processors/RISC-V/rv5mc/rv5mc_1m.h"
#include "processors/RISC-V/rv5mc/rv5mc_2m.h"
#include "processors/RISC-V/rv5s/rv5s.h"
#include "processors/RISC-V/rv5s_no_fw/rv5s_no_fw.h"
#include "processors/RISC-V/rv5s_no_fw_hz/rv5s_no_fw_hz.h"
#include "processors/RISC-V/rv5s_no_hz/rv5s_no_hz.h"
#include "processors/RISC-V/rv6s_dual/rv6s_dual.h"
#include "processors/RISC-V/rviss/rviss.h"
#include "processors/RISC-V/rvss/rvss.h"

namespace Ripes {

// TODO: This whole registration logic is ugly and should be reworked - either
// load a JSON file at runtime, or have a small codegen pass during compilation
// to generate this file based on an input JSON.

ProcessorRegistry::ProcessorRegistry() {
  // tr() here is Q_DECLARE_TR_FUNCTIONS(Ripes::ProcessorRegistry), see the
  // header: the translation context lives in one place.
  const QString no_hz_note =
      tr("<br><b>NOTE: given the lack of a hazard unit (and therefore the lack "
         "of dynamic hazard resolution in the pipeline) programs with hazards "
         "will execute incorrectly, unless resolved by inserting nop's to "
         "manually stall the pipeline.</b>");
  const QString rv5s_no_fw_hz_desc =
      tr("A 5-stage in-order processor with no forwarding or hazard "
         "detection/elimination.") +
      no_hz_note;
  const QString rv5s_no_hz_desc =
      tr("A 5-stage in-order processor with forwarding but no hazard "
         "detection/elimination.") +
      no_hz_note;
  const QString rv5s_desc =
      tr("A 5-stage in-order processor with hazard detection/elimination and "
         "forwarding.");
  const QString rv5s_no_fw_desc =
      tr("A 5-stage in-order processor with hazard detection/elimination but "
         "no forwarding unit.");
  const QString rv6s_desc =
      tr("A 6-stage dual-issue in-order processor. Each way may execute "
         "arithmetic instructions, whereas way 1 is reserved for controlflow "
         "and ecall instructions, and way 2 for memory accessing "
         "instructions.");
  const QString rv5mc_desc =
      tr("A 5 stage multicycle processor similar to the multicycle processor "
         "described in Harris&Harris. Instructions take a variable number of "
         "cycles to execute. By default, the current state is shown in the "
         "control unit. This version uses separate memories for data and "
         "instructions like the single cycle or segmented processors.");
  const QString rv5mc_desc_1m =
      tr("A 5 stage multicycle processor similar to the multicycle processor "
         "described in Harris&Harris. Instructions take a variable number of "
         "cycles to execute. By default, the current state is shown in the "
         "control unit. This version takes advantage of the fact that data "
         "and instructions are never accessed in the same cycle to use a "
         "single memory for data and instructions.");
  const QString rviss_desc =
      tr("An instruction-set simulator. This model executes one full "
         "instruction per clock cycle through a software interpreter. It is "
         "useful as a fast, reference model.");
  const QString single_cycle_desc = tr("A single cycle processor");
  const QString standardLayout = tr("Standard");
  const QString extendedLayout = tr("Extended");

  // Initialize processors
  std::vector<Layout> layouts;
  RegisterInitialization defRegVals;

  // RISC-V ISA simulator (software interpreter).
  layouts = {{standardLayout, "", {{{0, 0}, QPointF{0.5, 0}}}}};
  defRegVals = {{RVISA::GPR, {{2, 0x7ffffff0}, {3, 0x10000000}}}};
  addProcessor(ProcInfo<vsrtl::core::RVISS<uint32_t>>(
      ProcessorID::RV32_ISS, tr("ISA simulator"), rviss_desc, layouts, defRegVals));
  addProcessor(ProcInfo<vsrtl::core::RVISS<uint64_t>>(
      ProcessorID::RV64_ISS, tr("ISA simulator"), rviss_desc, layouts, defRegVals));

  // RISC-V single cycle
  layouts = {{standardLayout,
              ":/layouts/RISC-V/rvss/rv_ss_standard_layout.json",
              {{{0, 0}, QPointF{0.5, 0}}}},
             {extendedLayout,
              ":/layouts/RISC-V/rvss/rv_ss_extended_layout.json",
              {{{0, 0}, QPointF{0.5, 0}}}}};
  defRegVals = {{RVISA::GPR, {{2, 0x7ffffff0}, {3, 0x10000000}}}};
  addProcessor(ProcInfo<vsrtl::core::RVSS<uint32_t>>(
      ProcessorID::RV32_SS, tr("Single-cycle processor"),
      single_cycle_desc, layouts, defRegVals));
  addProcessor(ProcInfo<vsrtl::core::RVSS<uint64_t>>(
      ProcessorID::RV64_SS, tr("Single-cycle processor"),
      single_cycle_desc, layouts, defRegVals));

  // RISC-V multicycle
  layouts = {{extendedLayout,
              ":/layouts/RISC-V/rv5mc/rv5mc_extended_layout.json",
              {{{0, 0}, QPointF{0.08, 0}},
               {{0, 1}, QPointF{0.28, 0}},
               {{0, 2}, QPointF{0.54, 0}},
               {{0, 3}, QPointF{0.78, 0}},
               {{0, 4}, QPointF{0.9, 0}}}}};
  defRegVals = {{RVISA::GPR, {{2, 0x7ffffff0}, {3, 0x10000000}}}};
  addProcessor(ProcInfo<vsrtl::core::RV5MC2M<uint32_t>>(
      ProcessorID::RV32_5MC_2M, tr("Multi-cycle processor with separate memories"),
      rv5mc_desc, layouts, defRegVals));
  addProcessor(ProcInfo<vsrtl::core::RV5MC2M<uint64_t>>(
      ProcessorID::RV64_5MC, tr("Multi-cycle processor with separate memories"),
      rv5mc_desc, layouts, defRegVals));

  // RISC-V multicycle, one memory
  layouts = {{extendedLayout,
              ":/layouts/RISC-V/rv5mc/rv5mc_1m_extended_layout.json", // TODO
              {{{0, 0}, QPointF{0.08, 0}},
               {{0, 1}, QPointF{0.28, 0}},
               {{0, 2}, QPointF{0.54, 0}},
               {{0, 3}, QPointF{0.78, 0}},
               {{0, 4}, QPointF{0.9, 0}}}}};
  defRegVals = {{RVISA::GPR, {{2, 0x7ffffff0}, {3, 0x10000000}}}};
  addProcessor(ProcInfo<vsrtl::core::RV5MC1M<uint32_t>>(
      ProcessorID::RV32_5MC_1M, tr("Multi-cycle processor with one memory"),
      rv5mc_desc_1m, layouts, defRegVals));
  addProcessor(ProcInfo<vsrtl::core::RV5MC1M<uint64_t>>(
      ProcessorID::RV64_5MC_1M, tr("Multi-cycle processor with one memory"),
      rv5mc_desc_1m, layouts, defRegVals));

  // RISC-V 5-stage without forwarding or hazard detection
  layouts = {
      {standardLayout,
       ":/layouts/RISC-V/rv5s_no_fw_hz/rv5s_no_fw_hz_standard_layout.json",
       {{{0, 0}, QPointF{0.08, 0}},
        {{0, 1}, QPointF{0.3, 0}},
        {{0, 2}, QPointF{0.54, 0}},
        {{0, 3}, QPointF{0.73, 0}},
        {{0, 4}, QPointF{0.88, 0}}}},
      {extendedLayout,
       ":/layouts/RISC-V/rv5s_no_fw_hz/rv5s_no_fw_hz_extended_layout.json",
       {{{0, 0}, QPointF{0.08, 0.0}},
        {{0, 1}, QPointF{0.31, 0.0}},
        {{0, 2}, QPointF{0.56, 0.0}},
        {{0, 3}, QPointF{0.76, 0.0}},
        {{0, 4}, QPointF{0.9, 0.0}}}}};
  defRegVals = {{RVISA::GPR, {{2, 0x7ffffff0}, {3, 0x10000000}}}};
  addProcessor(ProcInfo<vsrtl::core::RV5S_NO_FW_HZ<uint32_t>>(
      ProcessorID::RV32_5S_NO_FW_HZ,
      tr("5-stage processor w/o forwarding or hazard detection"),
      rv5s_no_fw_hz_desc, layouts, defRegVals));
  addProcessor(ProcInfo<vsrtl::core::RV5S_NO_FW_HZ<uint64_t>>(
      ProcessorID::RV64_5S_NO_FW_HZ,
      tr("5-stage processor w/o forwarding or hazard detection"),
      rv5s_no_fw_hz_desc, layouts, defRegVals));

  // RISC-V 5-stage without hazard detection
  layouts = {{standardLayout,
              ":/layouts/RISC-V/rv5s_no_hz/rv5s_no_hz_standard_layout.json",
              {{{0, 0}, QPointF{0.08, 0}},
               {{0, 1}, QPointF{0.3, 0}},
               {{0, 2}, QPointF{0.53, 0}},
               {{0, 3}, QPointF{0.75, 0}},
               {{0, 4}, QPointF{0.88, 0}}}},
             {extendedLayout,
              ":/layouts/RISC-V/rv5s_no_hz/rv5s_no_hz_extended_layout.json",
              {{{0, 0}, QPointF{0.08, 0}},
               {{0, 1}, QPointF{0.28, 0}},
               {{0, 2}, QPointF{0.53, 0}},
               {{0, 3}, QPointF{0.78, 0}},
               {{0, 4}, QPointF{0.9, 0}}}}};
  defRegVals = {{RVISA::GPR, {{2, 0x7ffffff0}, {3, 0x10000000}}}};
  addProcessor(ProcInfo<vsrtl::core::RV5S_NO_HZ<uint32_t>>(
      ProcessorID::RV32_5S_NO_HZ, tr("5-stage processor w/o hazard detection"),
      rv5s_no_hz_desc, layouts, defRegVals));
  addProcessor(ProcInfo<vsrtl::core::RV5S_NO_HZ<uint64_t>>(
      ProcessorID::RV64_5S_NO_HZ, tr("5-stage processor w/o hazard detection"),
      rv5s_no_hz_desc, layouts, defRegVals));

  // RISC-V 5-stage without forwarding unit
  layouts = {{standardLayout,
              ":/layouts/RISC-V/rv5s_no_fw/rv5s_no_fw_standard_layout.json",
              {{{0, 0}, QPointF{0.08, 0}},
               {{0, 1}, QPointF{0.3, 0}},
               {{0, 2}, QPointF{0.53, 0}},
               {{0, 3}, QPointF{0.75, 0}},
               {{0, 4}, QPointF{0.88, 0}}}},
             {extendedLayout,
              ":/layouts/RISC-V/rv5s_no_fw/rv5s_no_fw_extended_layout.json",
              {{{0, 0}, QPointF{0.08, 0}},
               {{0, 1}, QPointF{0.28, 0}},
               {{0, 2}, QPointF{0.53, 0}},
               {{0, 3}, QPointF{0.78, 0}},
               {{0, 4}, QPointF{0.9, 0}}}}};
  defRegVals = {{RVISA::GPR, {{2, 0x7ffffff0}, {3, 0x10000000}}}};
  addProcessor(ProcInfo<vsrtl::core::RV5S_NO_FW<uint32_t>>(
      ProcessorID::RV32_5S_NO_FW, tr("5-Stage processor w/o forwarding unit"),
      rv5s_no_fw_desc, layouts, defRegVals));
  addProcessor(ProcInfo<vsrtl::core::RV5S_NO_FW<uint64_t>>(
      ProcessorID::RV64_5S_NO_FW, tr("5-Stage processor w/o forwarding unit"),
      rv5s_no_fw_desc, layouts, defRegVals));

  // RISC-V 5-stage
  layouts = {{standardLayout,
              ":/layouts/RISC-V/rv5s/rv5s_standard_layout.json",
              {{{0, 0}, QPointF{0.08, 0}},
               {{0, 1}, QPointF{0.29, 0}},
               {{0, 2}, QPointF{0.55, 0}},
               {{0, 3}, QPointF{0.75, 0}},
               {{0, 4}, QPointF{0.87, 0}}}},
             {extendedLayout,
              ":/layouts/RISC-V/rv5s/rv5s_extended_layout.json",
              {{{0, 0}, QPointF{0.08, 0}},
               {{0, 1}, QPointF{0.28, 0}},
               {{0, 2}, QPointF{0.54, 0}},
               {{0, 3}, QPointF{0.78, 0}},
               {{0, 4}, QPointF{0.9, 0}}}}};
  defRegVals = {{RVISA::GPR, {{2, 0x7ffffff0}, {3, 0x10000000}}}};
  addProcessor(ProcInfo<vsrtl::core::RV5S<uint32_t>>(
      ProcessorID::RV32_5S, tr("5-stage processor"), rv5s_desc, layouts,
      defRegVals));
  addProcessor(ProcInfo<vsrtl::core::RV5S<uint64_t>>(
      ProcessorID::RV64_5S, tr("5-stage processor"), rv5s_desc, layouts,
      defRegVals));

  // RISC-V 6-stage dual issue
  layouts = {{extendedLayout,
              ":/layouts/RISC-V/rv6s_dual/rv6s_dual_extended_layout.json",
              {{{{0, 0}, QPointF{0.06, 0}},
                {{1, 0}, QPointF{0.06, 1}},
                {{0, 1}, QPointF{0.22, 0}},
                {{1, 1}, QPointF{0.22, 1}},
                {{0, 2}, QPointF{0.40, 0}},
                {{1, 2}, QPointF{0.40, 1}},
                {{0, 3}, QPointF{0.59, 0}},
                {{1, 3}, QPointF{0.59, 1}},
                {{0, 4}, QPointF{0.80, 0}},
                {{1, 4}, QPointF{0.80, 1}},
                {{0, 5}, QPointF{0.90, 0}},
                {{1, 5}, QPointF{0.90, 1}}}}}};
  defRegVals = {{RVISA::GPR, {{2, 0x7ffffff0}, {3, 0x10000000}}}};
  addProcessor(ProcInfo<vsrtl::core::RV6S_DUAL<uint32_t>>(
      ProcessorID::RV32_6S_DUAL, tr("6-stage dual-issue processor"), rv6s_desc,
      layouts, defRegVals));
  addProcessor(ProcInfo<vsrtl::core::RV6S_DUAL<uint64_t>>(
      ProcessorID::RV64_6S_DUAL, tr("6-stage dual-issue processor"), rv6s_desc,
      layouts, defRegVals));
}
} // namespace Ripes
