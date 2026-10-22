// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "componentinfo.h"

#include <QCoreApplication>

#include <map>

namespace Ripes {
namespace Explorer {

namespace {
const char *kRV = "src/processors/RISC-V/";
const char *kVSRTL = "external/VSRTL/include/VSRTL/core/";

const std::map<QString, ComponentInfo> &table() {
  static const std::map<QString, ComponentInfo> t = {
      {"pc_reg",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Program counter (PC)"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Holds the address of the instruction being fetched."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "A clocked register. At every clock edge it loads "
                          "the next PC chosen "
                          "by the PC multiplexer (PC + 4, or a branch/jump "
                          "target). The hazard "
                          "unit can disable it to stall fetching."),
        "IF",
        QString(kVSRTL) + "vsrtl_register.h",
        {"pc_src", "pc_4", "instr_mem"}}},
      {"pc_4",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo", "PC incrementer"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Computes the address of the sequentially next instruction."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "An adder that adds the instruction size (4, or 2 for compressed "
            "instructions) to the current PC."),
        "IF",
        QString(kVSRTL) + "vsrtl_adder.h",
        {"pc_reg", "pc_inc", "pc_src"}}},
      {"pc_inc",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Instruction size select"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Selects 4 or 2 as PC increment."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Driven by the uncompress unit: compressed (RVC) "
                          "instructions are 2 "
                          "bytes long."),
        "IF",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"pc_4", "uncompress"}}},
      {"pc_src",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Next-PC multiplexer"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Chooses where the next instruction is fetched from."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Selects PC + 4 normally, or the ALU result "
                          "(branch/jump target) when "
                          "a branch is taken or a jump executes."),
        "IF",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"pc_reg", "controlflow_or", "alu"}}},
      {"instr_mem",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Instruction memory"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Supplies the instruction word stored at the PC."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "An ideal (single-cycle, always hitting) read-only "
                          "memory port into "
                          "the program's address space."),
        "IF",
        QString(kVSRTL) + "vsrtl_memory.h",
        {"pc_reg", "uncompress"}}},
      {"uncompress",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo", "RVC expander"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Expands 16-bit compressed instructions to 32-bit form."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Passes regular 32-bit instructions through "
                          "unchanged and reports the "
                          "instruction size to the PC logic."),
        "IF",
        QString(kRV) + "rv_uncompress.h",
        {"instr_mem", "pc_inc"}}},
      {"ifid_reg",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "IF/ID pipeline register"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Carries the fetched instruction and its PC into the "
                          "decode stage."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Clocked register between IF and ID. It can be stalled (enable "
            "low) by "
            "the hazard unit or cleared (flushed) when a branch is taken."),
        "ID",
        QString(kRV) + "rv5s_no_fw_hz/rv5s_no_fw_hz_ifid.h",
        {"decode", "hzunit"}}},
      {"decode",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Instruction decoder"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Splits the instruction word into opcode and register indices."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Identifies the instruction (opcode) and extracts "
                          "rd, rs1 and rs2 for "
                          "the register file and control unit."),
        "ID",
        QString(kRV) + "rv_decode.h",
        {"control", "registerFile", "immediate"}}},
      {"control",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo", "Control unit"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Generates the control signals for the rest of the datapath."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Maps the decoded opcode to ALU operation, operand sources, memory "
            "operation, register write enable and branch/jump behaviour."),
        "ID",
        QString(kRV) + "rv_control.h",
        {"decode", "alu", "data_mem"}}},
      {"immediate",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Immediate generator"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Builds the sign-extended immediate operand."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Reassembles the immediate bits scattered over the "
                          "I/S/B/U/J formats "
                          "and sign-extends them to XLEN bits."),
        "ID",
        QString(kRV) + "rv_immediate.h",
        {"decode", "alu_op2_src"}}},
      {"registerFile",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo", "Register file"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Stores the 32 integer registers x0-x31."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Two combinational read ports (rs1, rs2) in ID and "
                          "one clocked write "
                          "port driven from WB. x0 always reads as zero."),
        "ID",
        QString(kRV) + "rv_registerfile.h",
        {"decode", "reg_wr_src", "memwb_reg"}}},
      {"idex_reg",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "ID/EX pipeline register"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Carries operands and control signals into the execute stage."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Inserting a bubble (clear) here is how the hazard unit stalls the "
            "pipeline and how taken branches flush the instruction in ID."),
        "EX",
        QString(kRV) + "rv5s/rv5s_idex.h",
        {"alu", "branch", "funit"}}},
      {"alu_op1_src",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "ALU operand 1 select"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Chooses the first ALU operand."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Register value (rs1) for most instructions, or the "
                          "PC (AUIPC, JAL, "
                          "branches compute targets with PC + imm)."),
        "EX",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"alu"}}},
      {"alu_op2_src",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "ALU operand 2 select"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Chooses the second ALU operand."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Register value (rs2) for register-register instructions, or the "
            "immediate."),
        "EX",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"alu", "immediate"}}},
      {"reg1_fw_src",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Forwarding multiplexer (rs1)"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Supplies the newest value of rs1 to EX."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Selects between the value read in ID and values forwarded from "
            "the "
            "EX/MEM or MEM/WB registers, as decided by the forwarding unit."),
        "EX",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"funit", "alu"}}},
      {"reg2_fw_src",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Forwarding multiplexer (rs2)"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Supplies the newest value of rs2 to EX."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Like the rs1 forwarding multiplexer; its output also provides the "
            "store data."),
        "EX",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"funit", "alu"}}},
      {"alu",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Arithmetic logic unit (ALU)"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Performs the arithmetic/logic operation of the instruction."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Computes add/sub/shift/compare/logic results, "
                          "load/store addresses "
                          "and branch/jump targets. Combinational: the result "
                          "is available in "
                          "the same cycle."),
        "EX",
        QString(kRV) + "rv_alu.h",
        {"alu_op1_src", "alu_op2_src", "exmem_reg"}}},
      {"branch",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Branch comparator"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Decides whether a conditional branch is taken."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Compares the two register operands according to the branch type "
            "(beq, bne, blt, ...). In the 5-stage models this happens in EX."),
        "EX",
        QString(kRV) + "rv_branch.h",
        {"br_and", "controlflow_or"}}},
      {"br_and",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Branch-taken gate"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "True if the instruction is a branch and its condition holds."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "AND of the control unit's 'is branch' signal and the comparator "
            "result."),
        "EX",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"branch", "controlflow_or"}}},
      {"controlflow_or",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Control-flow change"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "True when the next PC is not PC + 4."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "OR of 'branch taken' and 'jump'. It switches the "
                          "next-PC multiplexer "
                          "and flushes the younger instructions in IF and ID."),
        "EX",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"pc_src", "ifid_reg"}}},
      {"funit",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo", "Forwarding unit"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Detects when EX needs a value that is not yet in "
                          "the register file."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Compares the source registers in EX with the destination "
            "registers "
            "of the instructions in MEM and WB and steers the forwarding "
            "multiplexers."),
        "EX",
        QString(kRV) + "rv5s/rv5s_forwardingunit.h",
        {"reg1_fw_src", "reg2_fw_src"}}},
      {"hzunit",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Hazard detection unit"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Stalls the pipeline when forwarding cannot help."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Detects load-use hazards (a load in EX producing a "
                          "register needed "
                          "in ID): it freezes PC and IF/ID and inserts a "
                          "bubble into ID/EX for "
                          "one cycle."),
        "ID",
        QString(kRV) + "rv5s/rv5s_hazardunit.h",
        {"pc_reg", "ifid_reg", "idex_reg"}}},
      {"exmem_reg",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "EX/MEM pipeline register"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Carries the ALU result, store data and control into "
                          "the memory stage."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Its ALU result is also a forwarding source for the "
                          "next instruction."),
        "MEM",
        QString(kRV) + "rv5s/rv5s_exmem.h",
        {"data_mem", "funit"}}},
      {"data_mem",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo", "Data memory"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Performs loads and stores."),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "An ideal single-cycle memory port: reads return data in the same "
            "cycle, writes are committed at the clock edge. There is no cache "
            "in "
            "the datapath model itself (the Cache tab simulates caches "
            "separately)."),
        "MEM",
        QString(kRV) + "rv_memory.h",
        {"exmem_reg", "memwb_reg"}}},
      {"memwb_reg",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "MEM/WB pipeline register"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Carries results into the write-back stage."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Holds the ALU result, loaded data and PC + 4 until "
                          "the register file "
                          "write at the next clock edge."),
        "WB",
        QString(kRV) + "rv5s/rv5s_memwb.h",
        {"reg_wr_src", "registerFile"}}},
      {"reg_wr_src",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Write-back select"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Chooses the value written to rd."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "ALU result, loaded memory data, or PC + 4 (link "
                          "address of JAL/JALR)."),
        "WB",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"registerFile", "memwb_reg"}}},
      {"ecallChecker",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Environment call handler"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Detects ecall instructions and hands control to Ripes."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "When an ecall reaches it, Ripes executes the system "
                          "call (selected "
                          "by a7) and may stop the simulation on exit."),
        "EX",
        QString(kRV) + "rv_ecallchecker.h",
        {"hzunit"}}},
      {"efsc_or",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo", "Flush/exit gate"),
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::ComponentInfo",
            "Combines control-flow changes with system-call exits."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Its output clears the IF/ID register."),
        "",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"ifid_reg"}}},
      {"efschz_or",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo", "ID/EX clear gate"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Clears ID/EX on flushes and load-use stalls."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "OR of the flush/exit signal and the hazard unit's "
                          "bubble request."),
        "",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"idex_reg", "hzunit"}}},
      {"mem_stalled_or",
       {QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo", "Stall marker"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Tracks bubbles inserted by stalls."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::ComponentInfo",
                          "Used only to label stalled stages in the GUI and "
                          "pipeline diagram."),
        "",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"exmem_reg"}}},
  };
  return t;
}
} // namespace

std::optional<ComponentInfo> componentInfo(const QString &name) {
  const auto &t = table();
  if (auto it = t.find(name); it != t.end()) {
    // Texts in the user interface language.
    ComponentInfo info = it->second;
    for (QString *text : {&info.title, &info.purpose, &info.behaviour})
      *text = QCoreApplication::translate("Ripes::Explorer::ComponentInfo",
                                          text->toUtf8());
    return info;
  }
  return std::nullopt;
}

QStringList describedComponents() {
  QStringList names;
  for (const auto &it : table())
    names << it.first;
  return names;
}

} // namespace Explorer
} // namespace Ripes
