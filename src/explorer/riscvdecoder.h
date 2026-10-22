// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QString>
#include <QtGlobal>

#include <optional>

namespace Ripes {
namespace Explorer {

/// Base instruction formats of the RISC-V unprivileged specification.
enum class RVFormat { R, R4, I, S, B, U, J, Compressed, Unknown };
QString formatName(RVFormat f);

/// Register file referenced by an instruction operand.
enum class RegClass { None, GPR, FPR };

/**
 * @brief The RVDecoded struct
 * Field-level decoding of a single RISC-V instruction word, following the
 * RISC-V unprivileged ISA specification (RV32I/RV64I, M, F and D). This is
 * independent of the decoder inside the Ripes processor models: it is used to
 * explain instructions and to sanity-check trace attribution.
 */
struct RVDecoded {
  bool valid = false;
  bool compressed = false;
  quint32 word = 0;
  RVFormat format = RVFormat::Unknown;
  unsigned opcode = 0;
  unsigned rd = 0;
  unsigned rs1 = 0;
  unsigned rs2 = 0;
  unsigned rs3 = 0;
  unsigned funct3 = 0;
  unsigned funct7 = 0;
  bool hasImm = false;
  qint64 imm = 0;
  /// Lower case mnemonic, e.g. "addi". Empty if unknown/compressed.
  QString mnemonic;
  RegClass rdClass = RegClass::None;
  RegClass rs1Class = RegClass::None;
  RegClass rs2Class = RegClass::None;
  RegClass rs3Class = RegClass::None;
  bool isLoad = false;
  bool isStore = false;
  bool isBranch = false;
  bool isJump = false;
  bool isSystem = false;
  bool isMulDiv = false;
  /// Number of bytes accessed by a load or store.
  unsigned memBytes = 0;

  /// True if the instruction architecturally writes a destination register.
  /// Writes to x0 are discarded and therefore do not count.
  bool writesRd() const {
    return rdClass == RegClass::FPR || (rdClass == RegClass::GPR && rd != 0);
  }
};

/// Decodes a RISC-V instruction word. @p xlen is 32 or 64.
RVDecoded decodeRV(quint32 word, unsigned xlen);

/// Description of what an instruction does, for educational display.
struct RVSemantics {
  /// Register-transfer style expression, e.g. "rd ← rs1 + rs2".
  QString expression;
  /// One sentence explanation.
  QString description;
  /// Instruction class, e.g. "Integer arithmetic", "Load", "Branch".
  QString category;
};

/// Returns the semantics of @p mnemonic, or std::nullopt if unknown.
std::optional<RVSemantics> semanticsFor(const QString &mnemonic);

/// Computes the architectural destination value of simple integer
/// instructions (ALU, LUI/AUIPC, link value of JAL/JALR) from operand values.
/// Returns std::nullopt for instructions whose result depends on memory or
/// is otherwise not computable from the operands alone.
std::optional<quint64> evaluateIntegerResult(const RVDecoded &d, quint64 pc,
                                             quint64 rs1Value, quint64 rs2Value,
                                             unsigned xlen);

/// Evaluates the condition of a conditional branch. std::nullopt if @p d is
/// not a conditional branch.
std::optional<bool> evaluateBranch(const RVDecoded &d, quint64 rs1Value,
                                   quint64 rs2Value, unsigned xlen);

} // namespace Explorer
} // namespace Ripes
