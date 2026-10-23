// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "riscvdecoder.h"

#include <QCoreApplication>

#include <map>

namespace Ripes {
namespace Explorer {

namespace {

quint32 bits(quint32 word, unsigned hi, unsigned lo) {
  return (word >> lo) & ((quint32(1) << (hi - lo + 1)) - 1);
}

qint64 signExtend(quint64 value, unsigned width) {
  const quint64 signBit = quint64(1) << (width - 1);
  value &= (signBit << 1) - 1;
  return static_cast<qint64>((value ^ signBit) - signBit);
}

quint64 mask(unsigned xlen) {
  return xlen >= 64 ? ~quint64(0) : (quint64(1) << xlen) - 1;
}

qint64 asSigned(quint64 v, unsigned xlen) { return signExtend(v, xlen); }

void setRegs(RVDecoded &d, RegClass rd, RegClass rs1, RegClass rs2,
             RegClass rs3 = RegClass::None) {
  d.rdClass = rd;
  d.rs1Class = rs1;
  d.rs2Class = rs2;
  d.rs3Class = rs3;
}

const char *pick(unsigned idx, std::initializer_list<const char *> names) {
  if (idx >= names.size())
    return "";
  return *(names.begin() + idx);
}

void decodeOpFp(RVDecoded &d) {
  const unsigned f7 = d.funct7;
  const bool dbl = f7 & 1;
  const QString sfx = dbl ? ".d" : ".s";
  using R = RegClass;
  setRegs(d, R::FPR, R::FPR, R::FPR);
  switch (f7 & ~1u) {
  case 0x00:
    d.mnemonic = "fadd" + sfx;
    break;
  case 0x04:
    d.mnemonic = "fsub" + sfx;
    break;
  case 0x08:
    d.mnemonic = "fmul" + sfx;
    break;
  case 0x0C:
    d.mnemonic = "fdiv" + sfx;
    break;
  case 0x2C:
    d.mnemonic = "fsqrt" + sfx;
    d.rs2Class = R::None;
    break;
  case 0x10:
    d.mnemonic = QString(pick(d.funct3, {"fsgnj", "fsgnjn", "fsgnjx"})) + sfx;
    break;
  case 0x14:
    d.mnemonic = QString(pick(d.funct3, {"fmin", "fmax"})) + sfx;
    break;
  case 0x20:
    // fcvt.s.d (0x20) / fcvt.d.s (0x21)
    d.mnemonic = dbl ? "fcvt.d.s" : "fcvt.s.d";
    d.rs2Class = R::None;
    break;
  case 0x50:
    d.mnemonic = QString(pick(d.funct3, {"fle", "flt", "feq"})) + sfx;
    d.rdClass = R::GPR;
    break;
  case 0x60:
    d.mnemonic = QString(d.rs2 & 1 ? "fcvt.wu" : "fcvt.w") + sfx;
    if (d.rs2 >= 2)
      d.mnemonic = QString(d.rs2 & 1 ? "fcvt.lu" : "fcvt.l") + sfx;
    setRegs(d, R::GPR, R::FPR, R::None);
    break;
  case 0x68:
    d.mnemonic = "fcvt" + sfx + (d.rs2 & 1 ? ".wu" : ".w");
    if (d.rs2 >= 2)
      d.mnemonic = "fcvt" + sfx + (d.rs2 & 1 ? ".lu" : ".l");
    setRegs(d, R::FPR, R::GPR, R::None);
    break;
  case 0x70:
    d.mnemonic = d.funct3 == 1 ? "fclass" + sfx : (dbl ? "fmv.x.d" : "fmv.x.w");
    setRegs(d, R::GPR, R::FPR, R::None);
    break;
  case 0x78:
    d.mnemonic = dbl ? "fmv.d.x" : "fmv.w.x";
    setRegs(d, R::FPR, R::GPR, R::None);
    break;
  default:
    d.mnemonic = "op-fp";
    break;
  }
}

} // namespace

QString formatName(RVFormat f) {
  switch (f) {
  case RVFormat::R:
    return QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                       "R-type");
  case RVFormat::R4:
    return QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                       "R4-type");
  case RVFormat::I:
    return QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                       "I-type");
  case RVFormat::S:
    return QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                       "S-type");
  case RVFormat::B:
    return QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                       "B-type");
  case RVFormat::U:
    return QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                       "U-type");
  case RVFormat::J:
    return QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                       "J-type");
  case RVFormat::Compressed:
    return QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                       "Compressed (RVC)");
  case RVFormat::Unknown:
    return QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                       "Unknown");
  }
  return "Unknown";
}

RVDecoded decodeRV(quint32 word, unsigned xlen) {
  RVDecoded d;
  d.word = word;
  if ((word & 0b11) != 0b11) {
    // 16-bit compressed instruction. Field-level decoding of RVC is not
    // implemented; callers should rely on the disassembly.
    d.valid = true;
    d.compressed = true;
    d.format = RVFormat::Compressed;
    d.word = word & 0xFFFF;
    return d;
  }

  using R = RegClass;
  d.opcode = bits(word, 6, 0);
  d.rd = bits(word, 11, 7);
  d.funct3 = bits(word, 14, 12);
  d.rs1 = bits(word, 19, 15);
  d.rs2 = bits(word, 24, 20);
  d.rs3 = bits(word, 31, 27);
  d.funct7 = bits(word, 31, 25);

  const qint64 immI = signExtend(bits(word, 31, 20), 12);
  const qint64 immS =
      signExtend((bits(word, 31, 25) << 5) | bits(word, 11, 7), 12);
  const qint64 immB =
      signExtend((bits(word, 31, 31) << 12) | (bits(word, 7, 7) << 11) |
                     (bits(word, 30, 25) << 5) | (bits(word, 11, 8) << 1),
                 13);
  const qint64 immU = signExtend(word & 0xFFFFF000u, 32);
  const qint64 immJ =
      signExtend((bits(word, 31, 31) << 20) | (bits(word, 19, 12) << 12) |
                     (bits(word, 20, 20) << 11) | (bits(word, 30, 21) << 1),
                 21);

  d.valid = true;
  switch (d.opcode) {
  case 0x33: { // OP
    d.format = RVFormat::R;
    setRegs(d, R::GPR, R::GPR, R::GPR);
    if (d.funct7 == 0x00)
      d.mnemonic = pick(
          d.funct3, {"add", "sll", "slt", "sltu", "xor", "srl", "or", "and"});
    else if (d.funct7 == 0x20 && d.funct3 == 0)
      d.mnemonic = "sub";
    else if (d.funct7 == 0x20 && d.funct3 == 5)
      d.mnemonic = "sra";
    else if (d.funct7 == 0x01) {
      d.isMulDiv = true;
      d.mnemonic = pick(d.funct3, {"mul", "mulh", "mulhsu", "mulhu", "div",
                                   "divu", "rem", "remu"});
    } else
      d.valid = false;
    break;
  }
  case 0x3B: { // OP-32 (RV64)
    d.format = RVFormat::R;
    setRegs(d, R::GPR, R::GPR, R::GPR);
    if (d.funct7 == 0x00 && d.funct3 == 0)
      d.mnemonic = "addw";
    else if (d.funct7 == 0x00 && d.funct3 == 1)
      d.mnemonic = "sllw";
    else if (d.funct7 == 0x00 && d.funct3 == 5)
      d.mnemonic = "srlw";
    else if (d.funct7 == 0x20 && d.funct3 == 0)
      d.mnemonic = "subw";
    else if (d.funct7 == 0x20 && d.funct3 == 5)
      d.mnemonic = "sraw";
    else if (d.funct7 == 0x01) {
      d.isMulDiv = true;
      d.mnemonic = pick(d.funct3,
                        {"mulw", "", "", "", "divw", "divuw", "remw", "remuw"});
    }
    d.valid = d.valid && !d.mnemonic.isEmpty() && xlen == 64;
    break;
  }
  case 0x13: { // OP-IMM
    d.format = RVFormat::I;
    setRegs(d, R::GPR, R::GPR, R::None);
    d.hasImm = true;
    d.imm = immI;
    const unsigned shamtBits = xlen == 64 ? 6 : 5;
    if (d.funct3 == 1 || d.funct3 == 5) {
      d.imm = bits(word, 20 + shamtBits - 1, 20);
      const bool arith = bits(word, 30, 30);
      d.mnemonic = d.funct3 == 1 ? "slli" : (arith ? "srai" : "srli");
    } else {
      d.mnemonic = pick(
          d.funct3, {"addi", "", "slti", "sltiu", "xori", "", "ori", "andi"});
    }
    break;
  }
  case 0x1B: { // OP-IMM-32 (RV64)
    d.format = RVFormat::I;
    setRegs(d, R::GPR, R::GPR, R::None);
    d.hasImm = true;
    if (d.funct3 == 0) {
      d.mnemonic = "addiw";
      d.imm = immI;
    } else if (d.funct3 == 1 || d.funct3 == 5) {
      d.imm = bits(word, 24, 20);
      d.mnemonic =
          d.funct3 == 1 ? "slliw" : (bits(word, 30, 30) ? "sraiw" : "srliw");
    }
    d.valid = !d.mnemonic.isEmpty() && xlen == 64;
    break;
  }
  case 0x03: { // LOAD
    d.format = RVFormat::I;
    setRegs(d, R::GPR, R::GPR, R::None);
    d.hasImm = true;
    d.imm = immI;
    d.isLoad = true;
    d.mnemonic = pick(d.funct3, {"lb", "lh", "lw", "ld", "lbu", "lhu", "lwu"});
    static const unsigned sizes[] = {1, 2, 4, 8, 1, 2, 4, 0};
    d.memBytes = sizes[d.funct3];
    break;
  }
  case 0x23: { // STORE
    d.format = RVFormat::S;
    setRegs(d, R::None, R::GPR, R::GPR);
    d.hasImm = true;
    d.imm = immS;
    d.isStore = true;
    d.mnemonic = pick(d.funct3, {"sb", "sh", "sw", "sd"});
    d.memBytes = d.funct3 < 4 ? (1u << d.funct3) : 0;
    break;
  }
  case 0x63: { // BRANCH
    d.format = RVFormat::B;
    setRegs(d, R::None, R::GPR, R::GPR);
    d.hasImm = true;
    d.imm = immB;
    d.isBranch = true;
    d.mnemonic =
        pick(d.funct3, {"beq", "bne", "", "", "blt", "bge", "bltu", "bgeu"});
    break;
  }
  case 0x6F: // JAL
    d.format = RVFormat::J;
    setRegs(d, R::GPR, R::None, R::None);
    d.hasImm = true;
    d.imm = immJ;
    d.isJump = true;
    d.mnemonic = "jal";
    break;
  case 0x67: // JALR
    d.format = RVFormat::I;
    setRegs(d, R::GPR, R::GPR, R::None);
    d.hasImm = true;
    d.imm = immI;
    d.isJump = true;
    d.mnemonic = d.funct3 == 0 ? "jalr" : "";
    break;
  case 0x37: // LUI
    d.format = RVFormat::U;
    setRegs(d, R::GPR, R::None, R::None);
    d.hasImm = true;
    d.imm = immU;
    d.mnemonic = "lui";
    break;
  case 0x17: // AUIPC
    d.format = RVFormat::U;
    setRegs(d, R::GPR, R::None, R::None);
    d.hasImm = true;
    d.imm = immU;
    d.mnemonic = "auipc";
    break;
  case 0x73: { // SYSTEM
    d.format = RVFormat::I;
    d.isSystem = true;
    d.hasImm = true;
    d.imm = bits(word, 31, 20);
    if (d.funct3 == 0) {
      d.mnemonic = d.imm == 0 ? "ecall" : (d.imm == 1 ? "ebreak" : "");
    } else {
      d.mnemonic = pick(d.funct3, {"", "csrrw", "csrrs", "csrrc", "", "csrrwi",
                                   "csrrsi", "csrrci"});
      setRegs(d, R::GPR, d.funct3 < 4 ? R::GPR : R::None, R::None);
    }
    break;
  }
  case 0x0F: // MISC-MEM
    d.format = RVFormat::I;
    d.mnemonic = d.funct3 == 0 ? "fence" : (d.funct3 == 1 ? "fence.i" : "");
    break;
  case 0x07: // LOAD-FP
    d.format = RVFormat::I;
    setRegs(d, R::FPR, R::GPR, R::None);
    d.hasImm = true;
    d.imm = immI;
    d.isLoad = true;
    d.mnemonic = d.funct3 == 2 ? "flw" : (d.funct3 == 3 ? "fld" : "");
    d.memBytes = d.funct3 == 2 ? 4 : 8;
    break;
  case 0x27: // STORE-FP
    d.format = RVFormat::S;
    setRegs(d, R::None, R::GPR, R::FPR);
    d.hasImm = true;
    d.imm = immS;
    d.isStore = true;
    d.mnemonic = d.funct3 == 2 ? "fsw" : (d.funct3 == 3 ? "fsd" : "");
    d.memBytes = d.funct3 == 2 ? 4 : 8;
    break;
  case 0x43:
  case 0x47:
  case 0x4B:
  case 0x4F: {
    d.format = RVFormat::R4;
    setRegs(d, R::FPR, R::FPR, R::FPR, R::FPR);
    const QString base = d.opcode == 0x43   ? "fmadd"
                         : d.opcode == 0x47 ? "fmsub"
                         : d.opcode == 0x4B ? "fnmsub"
                                            : "fnmadd";
    d.mnemonic = base + ((d.funct7 & 0b11) == 1 ? ".d" : ".s");
    break;
  }
  case 0x53:
    d.format = RVFormat::R;
    decodeOpFp(d);
    break;
  default:
    d.valid = false;
    break;
  }

  if (d.mnemonic.isEmpty())
    d.valid = false;
  return d;
}

std::optional<RVSemantics> semanticsFor(const QString &mnemonic) {
  static const std::map<QString, RVSemantics> table = {
      // Integer register-register
      {"add",
       {"rd ← rs1 + rs2",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Adds two registers; overflow is ignored."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Integer arithmetic")}},
      {"sub",
       {"rd ← rs1 − rs2",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Subtracts rs2 from rs1; overflow is ignored."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Integer arithmetic")}},
      {"sll",
       {"rd ← rs1 << rs2[4:0]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Logical left shift by the low bits of rs2."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Shift")}},
      {"srl",
       {"rd ← rs1 >>u rs2[4:0]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Logical right shift (zero fill)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Shift")}},
      {"sra",
       {"rd ← rs1 >>s rs2[4:0]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Arithmetic right shift (sign fill)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Shift")}},
      {"slt",
       {"rd ← (rs1 <s rs2) ? 1 : 0",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Set if less than (signed compare)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Compare")}},
      {"sltu",
       {"rd ← (rs1 <u rs2) ? 1 : 0",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Set if less than (unsigned compare)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Compare")}},
      {"xor",
       {"rd ← rs1 ⊕ rs2",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Bitwise exclusive OR."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Logic")}},
      {"or",
       {"rd ← rs1 | rs2",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Bitwise OR."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Logic")}},
      {"and",
       {"rd ← rs1 & rs2",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Bitwise AND."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Logic")}},
      // Integer register-immediate
      {"addi",
       {"rd ← rs1 + sext(imm)",
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::RVSemantics",
            "Adds a sign-extended 12-bit immediate to rs1. 'addi rd, x0, imm' "
            "loads a small constant."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Integer arithmetic")}},
      {"slti",
       {"rd ← (rs1 <s sext(imm)) ? 1 : 0",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Set if less than immediate."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Compare")}},
      {"sltiu",
       {"rd ← (rs1 <u sext(imm)) ? 1 : 0",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Set if less than immediate (unsigned)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Compare")}},
      {"xori",
       {"rd ← rs1 ⊕ sext(imm)",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Bitwise XOR with immediate."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Logic")}},
      {"ori",
       {"rd ← rs1 | sext(imm)",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Bitwise OR with immediate."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Logic")}},
      {"andi",
       {"rd ← rs1 & sext(imm)",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Bitwise AND with immediate."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Logic")}},
      {"slli",
       {"rd ← rs1 << shamt",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Logical left shift by immediate."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Shift")}},
      {"srli",
       {"rd ← rs1 >>u shamt",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Logical right shift by immediate."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Shift")}},
      {"srai",
       {"rd ← rs1 >>s shamt",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Arithmetic right shift by immediate."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Shift")}},
      {"lui",
       {"rd ← imm[31:12] << 12",
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::RVSemantics",
            "Loads a 20-bit upper immediate; combine with addi to build 32-bit "
            "constants."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Constant")}},
      {"auipc",
       {"rd ← PC + (imm[31:12] << 12)",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Adds an upper immediate to the PC; used for "
                          "PC-relative addressing "
                          "(e.g. the 'la' pseudo-instruction)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Constant")}},
      // Loads/stores
      {"lb",
       {"rd ← sext(M[rs1 + imm][7:0])",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Load signed byte."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Load")}},
      {"lh",
       {"rd ← sext(M[rs1 + imm][15:0])",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Load signed half-word."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Load")}},
      {"lw",
       {"rd ← sext(M[rs1 + imm][31:0])",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Load word."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Load")}},
      {"ld",
       {"rd ← M[rs1 + imm][63:0]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Load double-word (RV64)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Load")}},
      {"lbu",
       {"rd ← zext(M[rs1 + imm][7:0])",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Load unsigned byte."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Load")}},
      {"lhu",
       {"rd ← zext(M[rs1 + imm][15:0])",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Load unsigned half-word."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Load")}},
      {"lwu",
       {"rd ← zext(M[rs1 + imm][31:0])",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Load unsigned word (RV64)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Load")}},
      {"sb",
       {"M[rs1 + imm] ← rs2[7:0]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Store byte."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Store")}},
      {"sh",
       {"M[rs1 + imm] ← rs2[15:0]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Store half-word."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Store")}},
      {"sw",
       {"M[rs1 + imm] ← rs2[31:0]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Store word."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Store")}},
      {"sd",
       {"M[rs1 + imm] ← rs2[63:0]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Store double-word (RV64)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Store")}},
      // Control flow
      {"beq",
       {"if (rs1 == rs2) PC ← PC + imm",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Branch if equal."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Branch")}},
      {"bne",
       {"if (rs1 ≠ rs2) PC ← PC + imm",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Branch if not equal."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Branch")}},
      {"blt",
       {"if (rs1 <s rs2) PC ← PC + imm",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Branch if less than (signed)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Branch")}},
      {"bge",
       {"if (rs1 ≥s rs2) PC ← PC + imm",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Branch if greater or equal (signed)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Branch")}},
      {"bltu",
       {"if (rs1 <u rs2) PC ← PC + imm",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Branch if less than (unsigned)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Branch")}},
      {"bgeu",
       {"if (rs1 ≥u rs2) PC ← PC + imm",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Branch if greater or equal (unsigned)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Branch")}},
      {"jal",
       {"rd ← PC + 4; PC ← PC + imm",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Jump and link: saves the return address in rd ('j' "
                          "uses rd = x0)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Jump")}},
      {"jalr",
       {"rd ← PC + 4; PC ← (rs1 + imm) & ~1",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Indirect jump and link ('ret' is jalr x0, 0(ra))."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Jump")}},
      // System
      {"ecall",
       {"trap to the execution environment",
        QT_TRANSLATE_NOOP(
            "Ripes::Explorer::RVSemantics",
            "Environment call. In Ripes, register a7 selects the system call "
            "(e.g. 10 = exit, 1 = print integer)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "System")}},
      {"ebreak",
       {"trap to the debugger",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Breakpoint exception."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "System")}},
      {"fence",
       {"order memory accesses",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Memory ordering fence."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Memory ordering")}},
      // M extension
      {"mul",
       {"rd ← (rs1 × rs2)[XLEN-1:0]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Multiply, low half of the product."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Multiply (M)")}},
      {"mulh",
       {"rd ← (rs1 ×s rs2)[2·XLEN-1:XLEN]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Multiply signed × signed, high half."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Multiply (M)")}},
      {"mulhsu",
       {"rd ← (rs1 ×su rs2)[2·XLEN-1:XLEN]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Multiply signed × unsigned, high half."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Multiply (M)")}},
      {"mulhu",
       {"rd ← (rs1 ×u rs2)[2·XLEN-1:XLEN]",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Multiply unsigned × unsigned, high half."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Multiply (M)")}},
      {"div",
       {"rd ← rs1 ÷s rs2",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Signed division."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Divide (M)")}},
      {"divu",
       {"rd ← rs1 ÷u rs2",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Unsigned division."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Divide (M)")}},
      {"rem",
       {"rd ← rs1 mod_s rs2",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Signed remainder."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Divide (M)")}},
      {"remu",
       {"rd ← rs1 mod_u rs2",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Unsigned remainder."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics", "Divide (M)")}},
      // RV64 word variants
      {"addw",
       {"rd ← sext((rs1 + rs2)[31:0])",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "32-bit add, sign-extended (RV64)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Integer arithmetic")}},
      {"subw",
       {"rd ← sext((rs1 − rs2)[31:0])",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "32-bit subtract, sign-extended (RV64)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Integer arithmetic")}},
      {"addiw",
       {"rd ← sext((rs1 + imm)[31:0])",
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "32-bit add immediate, sign-extended (RV64)."),
        QT_TRANSLATE_NOOP("Ripes::Explorer::RVSemantics",
                          "Integer arithmetic")}},
  };
  if (auto it = table.find(mnemonic); it != table.end()) {
    // Description and category in the user interface language.
    RVSemantics sem = it->second;
    sem.description = QCoreApplication::translate(
        "Ripes::Explorer::RVSemantics", sem.description.toUtf8());
    sem.category = QCoreApplication::translate("Ripes::Explorer::RVSemantics",
                                               sem.category.toUtf8());
    return sem;
  }
  return std::nullopt;
}

std::optional<quint64> evaluateIntegerResult(const RVDecoded &d, quint64 pc,
                                             quint64 rs1, quint64 rs2,
                                             unsigned xlen) {
  if (!d.valid || d.compressed)
    return std::nullopt;
  const quint64 m = mask(xlen);
  const unsigned shMask = xlen == 64 ? 0x3F : 0x1F;
  const quint64 imm = static_cast<quint64>(d.imm);
  const qint64 s1 = asSigned(rs1, xlen);
  const qint64 s2 = asSigned(rs2, xlen);
  const qint64 simm = d.imm;
  const QString &mn = d.mnemonic;
  quint64 r;
  if (mn == "add")
    r = rs1 + rs2;
  else if (mn == "sub")
    r = rs1 - rs2;
  else if (mn == "sll")
    r = rs1 << (rs2 & shMask);
  else if (mn == "srl")
    r = (rs1 & m) >> (rs2 & shMask);
  else if (mn == "sra")
    r = static_cast<quint64>(s1 >> (rs2 & shMask));
  else if (mn == "slt")
    r = s1 < s2 ? 1 : 0;
  else if (mn == "sltu")
    r = (rs1 & m) < (rs2 & m) ? 1 : 0;
  else if (mn == "xor")
    r = rs1 ^ rs2;
  else if (mn == "or")
    r = rs1 | rs2;
  else if (mn == "and")
    r = rs1 & rs2;
  else if (mn == "addi")
    r = rs1 + imm;
  else if (mn == "slti")
    r = s1 < simm ? 1 : 0;
  else if (mn == "sltiu")
    r = (rs1 & m) < (imm & m) ? 1 : 0;
  else if (mn == "xori")
    r = rs1 ^ imm;
  else if (mn == "ori")
    r = rs1 | imm;
  else if (mn == "andi")
    r = rs1 & imm;
  else if (mn == "slli")
    r = rs1 << (imm & shMask);
  else if (mn == "srli")
    r = (rs1 & m) >> (imm & shMask);
  else if (mn == "srai")
    r = static_cast<quint64>(s1 >> (imm & shMask));
  else if (mn == "lui")
    r = imm;
  else if (mn == "auipc")
    r = pc + imm;
  else if (mn == "jal" || mn == "jalr")
    r = pc + 4;
  else if (mn == "mul")
    r = rs1 * rs2;
  else if (mn == "addw")
    r = static_cast<quint64>(signExtend((rs1 + rs2) & 0xFFFFFFFF, 32));
  else if (mn == "subw")
    r = static_cast<quint64>(signExtend((rs1 - rs2) & 0xFFFFFFFF, 32));
  else if (mn == "addiw")
    r = static_cast<quint64>(signExtend((rs1 + imm) & 0xFFFFFFFF, 32));
  else
    return std::nullopt;
  return r & m;
}

std::optional<bool> evaluateBranch(const RVDecoded &d, quint64 rs1, quint64 rs2,
                                   unsigned xlen) {
  if (!d.valid || !d.isBranch)
    return std::nullopt;
  const quint64 m = mask(xlen);
  const qint64 s1 = asSigned(rs1, xlen);
  const qint64 s2 = asSigned(rs2, xlen);
  switch (d.funct3) {
  case 0:
    return (rs1 & m) == (rs2 & m);
  case 1:
    return (rs1 & m) != (rs2 & m);
  case 4:
    return s1 < s2;
  case 5:
    return s1 >= s2;
  case 6:
    return (rs1 & m) < (rs2 & m);
  case 7:
    return (rs1 & m) >= (rs2 & m);
  default:
    return std::nullopt;
  }
}

} // namespace Explorer
} // namespace Ripes
