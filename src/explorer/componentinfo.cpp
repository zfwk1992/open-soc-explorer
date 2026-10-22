// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "componentinfo.h"

#include <map>

namespace Ripes {
namespace Explorer {

namespace {
const char *kRV = "src/processors/RISC-V/";
const char *kVSRTL = "external/VSRTL/include/VSRTL/core/";

const std::map<QString, ComponentInfo> &table() {
  static const std::map<QString, ComponentInfo> t = {
      {"pc_reg",
       {"Program counter (PC)",
        "Holds the address of the instruction being fetched.",
        "A clocked register. At every clock edge it loads the next PC chosen "
        "by the PC multiplexer (PC + 4, or a branch/jump target). The hazard "
        "unit can disable it to stall fetching.",
        "IF",
        QString(kVSRTL) + "vsrtl_register.h",
        {"pc_src", "pc_4", "instr_mem"}}},
      {"pc_4",
       {"PC incrementer",
        "Computes the address of the sequentially next instruction.",
        "An adder that adds the instruction size (4, or 2 for compressed "
        "instructions) to the current PC.",
        "IF",
        QString(kVSRTL) + "vsrtl_adder.h",
        {"pc_reg", "pc_inc", "pc_src"}}},
      {"pc_inc",
       {"Instruction size select",
        "Selects 4 or 2 as PC increment.",
        "Driven by the uncompress unit: compressed (RVC) instructions are 2 "
        "bytes long.",
        "IF",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"pc_4", "uncompress"}}},
      {"pc_src",
       {"Next-PC multiplexer",
        "Chooses where the next instruction is fetched from.",
        "Selects PC + 4 normally, or the ALU result (branch/jump target) when "
        "a branch is taken or a jump executes.",
        "IF",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"pc_reg", "controlflow_or", "alu"}}},
      {"instr_mem",
       {"Instruction memory",
        "Supplies the instruction word stored at the PC.",
        "An ideal (single-cycle, always hitting) read-only memory port into "
        "the program's address space.",
        "IF",
        QString(kVSRTL) + "vsrtl_memory.h",
        {"pc_reg", "uncompress"}}},
      {"uncompress",
       {"RVC expander",
        "Expands 16-bit compressed instructions to 32-bit form.",
        "Passes regular 32-bit instructions through unchanged and reports the "
        "instruction size to the PC logic.",
        "IF",
        QString(kRV) + "rv_uncompress.h",
        {"instr_mem", "pc_inc"}}},
      {"ifid_reg",
       {"IF/ID pipeline register",
        "Carries the fetched instruction and its PC into the decode stage.",
        "Clocked register between IF and ID. It can be stalled (enable low) by "
        "the hazard unit or cleared (flushed) when a branch is taken.",
        "ID",
        QString(kRV) + "rv5s_no_fw_hz/rv5s_no_fw_hz_ifid.h",
        {"decode", "hzunit"}}},
      {"decode",
       {"Instruction decoder",
        "Splits the instruction word into opcode and register indices.",
        "Identifies the instruction (opcode) and extracts rd, rs1 and rs2 for "
        "the register file and control unit.",
        "ID",
        QString(kRV) + "rv_decode.h",
        {"control", "registerFile", "immediate"}}},
      {"control",
       {"Control unit",
        "Generates the control signals for the rest of the datapath.",
        "Maps the decoded opcode to ALU operation, operand sources, memory "
        "operation, register write enable and branch/jump behaviour.",
        "ID",
        QString(kRV) + "rv_control.h",
        {"decode", "alu", "data_mem"}}},
      {"immediate",
       {"Immediate generator",
        "Builds the sign-extended immediate operand.",
        "Reassembles the immediate bits scattered over the I/S/B/U/J formats "
        "and sign-extends them to XLEN bits.",
        "ID",
        QString(kRV) + "rv_immediate.h",
        {"decode", "alu_op2_src"}}},
      {"registerFile",
       {"Register file",
        "Stores the 32 integer registers x0-x31.",
        "Two combinational read ports (rs1, rs2) in ID and one clocked write "
        "port driven from WB. x0 always reads as zero.",
        "ID",
        QString(kRV) + "rv_registerfile.h",
        {"decode", "reg_wr_src", "memwb_reg"}}},
      {"idex_reg",
       {"ID/EX pipeline register",
        "Carries operands and control signals into the execute stage.",
        "Inserting a bubble (clear) here is how the hazard unit stalls the "
        "pipeline and how taken branches flush the instruction in ID.",
        "EX",
        QString(kRV) + "rv5s/rv5s_idex.h",
        {"alu", "branch", "funit"}}},
      {"alu_op1_src",
       {"ALU operand 1 select",
        "Chooses the first ALU operand.",
        "Register value (rs1) for most instructions, or the PC (AUIPC, JAL, "
        "branches compute targets with PC + imm).",
        "EX",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"alu"}}},
      {"alu_op2_src",
       {"ALU operand 2 select",
        "Chooses the second ALU operand.",
        "Register value (rs2) for register-register instructions, or the "
        "immediate.",
        "EX",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"alu", "immediate"}}},
      {"reg1_fw_src",
       {"Forwarding multiplexer (rs1)",
        "Supplies the newest value of rs1 to EX.",
        "Selects between the value read in ID and values forwarded from the "
        "EX/MEM or MEM/WB registers, as decided by the forwarding unit.",
        "EX",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"funit", "alu"}}},
      {"reg2_fw_src",
       {"Forwarding multiplexer (rs2)",
        "Supplies the newest value of rs2 to EX.",
        "Like the rs1 forwarding multiplexer; its output also provides the "
        "store data.",
        "EX",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"funit", "alu"}}},
      {"alu",
       {"Arithmetic logic unit (ALU)",
        "Performs the arithmetic/logic operation of the instruction.",
        "Computes add/sub/shift/compare/logic results, load/store addresses "
        "and branch/jump targets. Combinational: the result is available in "
        "the same cycle.",
        "EX",
        QString(kRV) + "rv_alu.h",
        {"alu_op1_src", "alu_op2_src", "exmem_reg"}}},
      {"branch",
       {"Branch comparator",
        "Decides whether a conditional branch is taken.",
        "Compares the two register operands according to the branch type "
        "(beq, bne, blt, ...). In the 5-stage models this happens in EX.",
        "EX",
        QString(kRV) + "rv_branch.h",
        {"br_and", "controlflow_or"}}},
      {"br_and",
       {"Branch-taken gate",
        "True if the instruction is a branch and its condition holds.",
        "AND of the control unit's 'is branch' signal and the comparator "
        "result.",
        "EX",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"branch", "controlflow_or"}}},
      {"controlflow_or",
       {"Control-flow change",
        "True when the next PC is not PC + 4.",
        "OR of 'branch taken' and 'jump'. It switches the next-PC multiplexer "
        "and flushes the younger instructions in IF and ID.",
        "EX",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"pc_src", "ifid_reg"}}},
      {"funit",
       {"Forwarding unit",
        "Detects when EX needs a value that is not yet in the register file.",
        "Compares the source registers in EX with the destination registers "
        "of the instructions in MEM and WB and steers the forwarding "
        "multiplexers.",
        "EX",
        QString(kRV) + "rv5s/rv5s_forwardingunit.h",
        {"reg1_fw_src", "reg2_fw_src"}}},
      {"hzunit",
       {"Hazard detection unit",
        "Stalls the pipeline when forwarding cannot help.",
        "Detects load-use hazards (a load in EX producing a register needed "
        "in ID): it freezes PC and IF/ID and inserts a bubble into ID/EX for "
        "one cycle.",
        "ID",
        QString(kRV) + "rv5s/rv5s_hazardunit.h",
        {"pc_reg", "ifid_reg", "idex_reg"}}},
      {"exmem_reg",
       {"EX/MEM pipeline register",
        "Carries the ALU result, store data and control into the memory stage.",
        "Its ALU result is also a forwarding source for the next instruction.",
        "MEM",
        QString(kRV) + "rv5s/rv5s_exmem.h",
        {"data_mem", "funit"}}},
      {"data_mem",
       {"Data memory",
        "Performs loads and stores.",
        "An ideal single-cycle memory port: reads return data in the same "
        "cycle, writes are committed at the clock edge. There is no cache in "
        "the datapath model itself (the Cache tab simulates caches "
        "separately).",
        "MEM",
        QString(kRV) + "rv_memory.h",
        {"exmem_reg", "memwb_reg"}}},
      {"memwb_reg",
       {"MEM/WB pipeline register",
        "Carries results into the write-back stage.",
        "Holds the ALU result, loaded data and PC + 4 until the register file "
        "write at the next clock edge.",
        "WB",
        QString(kRV) + "rv5s/rv5s_memwb.h",
        {"reg_wr_src", "registerFile"}}},
      {"reg_wr_src",
       {"Write-back select",
        "Chooses the value written to rd.",
        "ALU result, loaded memory data, or PC + 4 (link address of JAL/JALR).",
        "WB",
        QString(kVSRTL) + "vsrtl_multiplexer.h",
        {"registerFile", "memwb_reg"}}},
      {"ecallChecker",
       {"Environment call handler",
        "Detects ecall instructions and hands control to Ripes.",
        "When an ecall reaches it, Ripes executes the system call (selected "
        "by a7) and may stop the simulation on exit.",
        "EX",
        QString(kRV) + "rv_ecallchecker.h",
        {"hzunit"}}},
      {"efsc_or",
       {"Flush/exit gate",
        "Combines control-flow changes with system-call exits.",
        "Its output clears the IF/ID register.",
        "",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"ifid_reg"}}},
      {"efschz_or",
       {"ID/EX clear gate",
        "Clears ID/EX on flushes and load-use stalls.",
        "OR of the flush/exit signal and the hazard unit's bubble request.",
        "",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"idex_reg", "hzunit"}}},
      {"mem_stalled_or",
       {"Stall marker",
        "Tracks bubbles inserted by stalls.",
        "Used only to label stalled stages in the GUI and pipeline diagram.",
        "",
        QString(kVSRTL) + "vsrtl_logicgate.h",
        {"exmem_reg"}}},
  };
  return t;
}
} // namespace

std::optional<ComponentInfo> componentInfo(const QString &name) {
  const auto &t = table();
  if (auto it = t.find(name); it != t.end())
    return it->second;
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
