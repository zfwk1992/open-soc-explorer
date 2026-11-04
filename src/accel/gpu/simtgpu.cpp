#include "simtgpu.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>

namespace Ripes {
namespace gpu {

// ---------------------------------------------------------------------------
// ISA: names / forms
// ---------------------------------------------------------------------------

namespace {

/// The single opcode table: index = 6-bit opcode value. Entries without a
/// name are undefined opcodes. Built at compile time from one list so that
/// decode(), opcodeForm() and opcodeName() can never disagree.
struct OpcodeTable {
  std::array<OpcodeInfo, 64> entries{};
  constexpr OpcodeTable() {
    struct Row {
      Opcode op;
      const char *name;
      Form form;
    };
    constexpr Row rows[] = {
        {Opcode::NOP, "NOP", Form::NONE},   {Opcode::EXIT, "EXIT", Form::NONE},
        {Opcode::BAR, "BAR", Form::NONE},   {Opcode::BRA, "BRA", Form::B},
        {Opcode::MOVS, "MOVS", Form::I},    {Opcode::ADD, "ADD", Form::R},
        {Opcode::SUB, "SUB", Form::R},      {Opcode::MUL, "MUL", Form::R},
        {Opcode::FMA, "FMA", Form::R},      {Opcode::AND, "AND", Form::R},
        {Opcode::OR, "OR", Form::R},        {Opcode::XOR, "XOR", Form::R},
        {Opcode::SHL, "SHL", Form::R},      {Opcode::SHR, "SHR", Form::R},
        {Opcode::SRA, "SRA", Form::R},      {Opcode::ADDI, "ADDI", Form::I},
        {Opcode::MULI, "MULI", Form::I},    {Opcode::ANDI, "ANDI", Form::I},
        {Opcode::ORI, "ORI", Form::I},      {Opcode::XORI, "XORI", Form::I},
        {Opcode::SHLI, "SHLI", Form::I},    {Opcode::SHRI, "SHRI", Form::I},
        {Opcode::SRAI, "SRAI", Form::I},    {Opcode::SETP, "SETP", Form::R},
        {Opcode::SETPI, "SETP", Form::P},   {Opcode::LDG, "LDG", Form::I},
        {Opcode::STG, "STG", Form::I},      {Opcode::LDS, "LDS", Form::I},
        {Opcode::STS, "STS", Form::I},
    };
    for (const auto &r : rows) {
      auto &e = entries[static_cast<unsigned>(r.op) & 0x3Fu];
      e.name = r.name;
      e.form = r.form;
    }
  }
};

constexpr OpcodeTable kOpcodeTable{};
const OpcodeInfo kUndefinedOpcode{};

} // namespace

const OpcodeInfo &opcodeInfo(unsigned op) {
  return op < kOpcodeTable.entries.size() ? kOpcodeTable.entries[op]
                                          : kUndefinedOpcode;
}

Form opcodeForm(unsigned op) { return opcodeInfo(op).form; }

const char *opcodeName(Opcode op) {
  const char *n = opcodeInfo(static_cast<unsigned>(op)).name;
  return n ? n : "???";
}

const char *condName(Cond c) {
  switch (c) {
  case Cond::EQ:
    return "EQ";
  case Cond::NE:
    return "NE";
  case Cond::LT:
    return "LT";
  case Cond::GE:
    return "GE";
  case Cond::LTU:
    return "LTU";
  case Cond::GEU:
    return "GEU";
  case Cond::LE:
    return "LE";
  case Cond::GT:
    return "GT";
  }
  return "??";
}

const char *sregName(SReg s) {
  switch (s) {
  case SReg::TID:
    return "TID";
  case SReg::BID:
    return "BID";
  case SReg::NTID:
    return "NTID";
  case SReg::NBID:
    return "NBID";
  case SReg::ARG0:
    return "ARG0";
  case SReg::ARG1:
    return "ARG1";
  case SReg::ARG2:
    return "ARG2";
  case SReg::ARG3:
    return "ARG3";
  case SReg::LANEID:
    return "LANEID";
  case SReg::WARPID:
    return "WARPID";
  case SReg::SMID:
    return "SMID";
  }
  return "???";
}

// ---------------------------------------------------------------------------
// ISA: encode / decode
// ---------------------------------------------------------------------------

static int32_t signExtend(uint32_t value, unsigned bits) {
  const uint32_t mask = (1u << bits) - 1u;
  value &= mask;
  if (value & (1u << (bits - 1)))
    return static_cast<int32_t>(value | ~mask);
  return static_cast<int32_t>(value);
}

unsigned makeGuard(bool enable, bool negate, unsigned pidx) {
  return (enable ? 8u : 0u) | (negate ? 4u : 0u) | (pidx & 3u);
}

uint32_t encodeR(Opcode op, unsigned rd, unsigned rs1, unsigned rs2,
                 unsigned rs3, unsigned funct, unsigned guard) {
  return ((guard & 0xFu) << 28) | ((static_cast<uint32_t>(op) & 0x3Fu) << 22) |
         ((rd & 0xFu) << 18) | ((rs1 & 0xFu) << 14) | ((rs2 & 0xFu) << 10) |
         ((rs3 & 0xFu) << 6) | (funct & 0x3Fu);
}

uint32_t encodeI(Opcode op, unsigned rd, unsigned rs1, int32_t imm14,
                 unsigned guard) {
  return ((guard & 0xFu) << 28) | ((static_cast<uint32_t>(op) & 0x3Fu) << 22) |
         ((rd & 0xFu) << 18) | ((rs1 & 0xFu) << 14) |
         (static_cast<uint32_t>(imm14) & 0x3FFFu);
}

uint32_t encodeP(unsigned pidx, unsigned rs1, Cond cond, int32_t imm10,
                 unsigned guard) {
  return ((guard & 0xFu) << 28) |
         ((static_cast<uint32_t>(Opcode::SETPI) & 0x3Fu) << 22) |
         ((pidx & 0x3u) << 18) | ((rs1 & 0xFu) << 14) |
         ((static_cast<uint32_t>(cond) & 0xFu) << 10) |
         (static_cast<uint32_t>(imm10) & 0x3FFu);
}

uint32_t encodeB(uint32_t target22, unsigned guard) {
  return ((guard & 0xFu) << 28) |
         ((static_cast<uint32_t>(Opcode::BRA) & 0x3Fu) << 22) |
         (target22 & 0x3FFFFFu);
}

Instr decode(uint32_t word) {
  Instr in;
  in.raw = word;
  const unsigned op = (word >> 22) & 0x3Fu;
  in.valid = opcodeInfo(op).defined();
  in.op = static_cast<Opcode>(op);
  in.form = opcodeForm(op);
  in.guardEn = (word >> 31) & 1u;
  in.guardNeg = (word >> 30) & 1u;
  in.guardP = (word >> 28) & 3u;
  in.rd = (word >> 18) & 0xFu;
  in.rs1 = (word >> 14) & 0xFu;
  in.rs2 = (word >> 10) & 0xFu;
  in.rs3 = (word >> 6) & 0xFu;
  in.funct = word & 0x3Fu;
  switch (in.form) {
  case Form::I:
    in.imm = signExtend(word, 14);
    break;
  case Form::P:
    in.imm = signExtend(word, 10);
    break;
  case Form::B:
    in.target = word & 0x3FFFFFu;
    break;
  case Form::R:
  case Form::NONE:
    break;
  }
  return in;
}

// ---------------------------------------------------------------------------
// Disassembler
// ---------------------------------------------------------------------------

static std::string regName(unsigned r) { return "R" + std::to_string(r); }

static std::string memOperand(unsigned rs1, int32_t imm) {
  std::string s = "[" + regName(rs1);
  if (imm > 0)
    s += "+" + std::to_string(imm);
  else if (imm < 0)
    s += std::to_string(imm);
  s += "]";
  return s;
}

std::string disassemble(uint32_t word) {
  const Instr in = decode(word);
  std::ostringstream os;
  if (!in.valid) {
    os << "??? (0x" << std::hex << word << ")";
    return os.str();
  }
  if (in.guardEn)
    os << "@" << (in.guardNeg ? "!" : "") << "P" << unsigned(in.guardP) << " ";

  switch (in.op) {
  case Opcode::NOP:
  case Opcode::EXIT:
  case Opcode::BAR:
    os << opcodeName(in.op);
    break;
  case Opcode::BRA:
    os << "BRA " << in.target;
    break;
  case Opcode::MOVS: {
    const int id = in.imm;
    if (id >= static_cast<int>(SReg::ARG0) &&
        id <= static_cast<int>(SReg::ARG3)) {
      os << "S_ARG " << regName(in.rd) << ", "
         << (id - static_cast<int>(SReg::ARG0));
    } else if (id == static_cast<int>(SReg::TID)) {
      os << "S_TID " << regName(in.rd);
    } else if (id == static_cast<int>(SReg::BID)) {
      os << "S_BID " << regName(in.rd);
    } else if (id == static_cast<int>(SReg::NTID)) {
      os << "S_NTID " << regName(in.rd);
    } else if (id == static_cast<int>(SReg::NBID)) {
      os << "S_NBID " << regName(in.rd);
    } else if (id == static_cast<int>(SReg::LANEID)) {
      os << "S_LANE " << regName(in.rd);
    } else if (id == static_cast<int>(SReg::WARPID)) {
      os << "S_WARP " << regName(in.rd);
    } else if (id == static_cast<int>(SReg::SMID)) {
      os << "S_SMID " << regName(in.rd);
    } else {
      os << "MOVS " << regName(in.rd) << ", " << id;
    }
    break;
  }
  case Opcode::ADD:
  case Opcode::SUB:
  case Opcode::MUL:
  case Opcode::AND:
  case Opcode::OR:
  case Opcode::XOR:
  case Opcode::SHL:
  case Opcode::SHR:
  case Opcode::SRA:
    os << opcodeName(in.op) << " " << regName(in.rd) << ", " << regName(in.rs1)
       << ", " << regName(in.rs2);
    break;
  case Opcode::FMA:
    os << "FMA " << regName(in.rd) << ", " << regName(in.rs1) << ", "
       << regName(in.rs2) << ", " << regName(in.rs3);
    break;
  case Opcode::ADDI:
  case Opcode::MULI:
  case Opcode::ANDI:
  case Opcode::ORI:
  case Opcode::XORI:
  case Opcode::SHLI:
  case Opcode::SHRI:
  case Opcode::SRAI:
    os << opcodeName(in.op) << " " << regName(in.rd) << ", " << regName(in.rs1)
       << ", " << in.imm;
    break;
  case Opcode::SETP:
    os << "SETP." << condName(static_cast<Cond>(in.funct & 7u)) << " P"
       << unsigned(in.rd & 3u) << ", " << regName(in.rs1) << ", "
       << regName(in.rs2);
    break;
  case Opcode::SETPI:
    os << "SETP." << condName(static_cast<Cond>(in.rs2 & 7u)) << " P"
       << unsigned(in.rd & 3u) << ", " << regName(in.rs1) << ", " << in.imm;
    break;
  case Opcode::LDG:
  case Opcode::LDS:
    os << opcodeName(in.op) << " " << regName(in.rd) << ", "
       << memOperand(in.rs1, in.imm);
    break;
  case Opcode::STG:
  case Opcode::STS:
    os << opcodeName(in.op) << " " << memOperand(in.rs1, in.imm) << ", "
       << regName(in.rd);
    break;
  }
  return os.str();
}

// ---------------------------------------------------------------------------
// Assembler
// ---------------------------------------------------------------------------

namespace {

struct AsmError {
  std::string msg;
};

std::string trim(const std::string &s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
    ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
    --e;
  return s.substr(b, e - b);
}

std::string upper(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string stripComment(const std::string &line) {
  size_t cut = line.size();
  const size_t hash = line.find('#');
  if (hash != std::string::npos)
    cut = std::min(cut, hash);
  const size_t slashes = line.find("//");
  if (slashes != std::string::npos)
    cut = std::min(cut, slashes);
  const size_t semi = line.find(';');
  if (semi != std::string::npos)
    cut = std::min(cut, semi);
  return line.substr(0, cut);
}

/// Splits operands on commas (outside brackets).
std::vector<std::string> splitOperands(const std::string &s) {
  std::vector<std::string> out;
  std::string cur;
  int depth = 0;
  for (char c : s) {
    if (c == '[')
      depth++;
    else if (c == ']')
      depth--;
    if (c == ',' && depth == 0) {
      out.push_back(trim(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!trim(cur).empty())
    out.push_back(trim(cur));
  return out;
}

bool parseInt(const std::string &tok, int64_t &value) {
  const std::string t = trim(tok);
  if (t.empty())
    return false;
  char *end = nullptr;
  const long long v = std::strtoll(t.c_str(), &end, 0);
  if (end == t.c_str() || *end != '\0')
    return false;
  value = v;
  return true;
}

bool isReg(const std::string &tok, unsigned &reg) {
  const std::string t = upper(trim(tok));
  if (t.size() < 2 || t[0] != 'R')
    return false;
  for (size_t i = 1; i < t.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(t[i])))
      return false;
  const int v = std::atoi(t.c_str() + 1);
  if (v < 0 || v > 15)
    return false;
  reg = static_cast<unsigned>(v);
  return true;
}

bool isPred(const std::string &tok, unsigned &pidx) {
  const std::string t = upper(trim(tok));
  if (t.size() != 2 || t[0] != 'P' || !std::isdigit(static_cast<unsigned char>(t[1])))
    return false;
  const int v = t[1] - '0';
  if (v < 0 || v > 3)
    return false;
  pidx = static_cast<unsigned>(v);
  return true;
}

unsigned needReg(const std::string &tok) {
  unsigned r;
  if (!isReg(tok, r))
    throw AsmError{"expected register R0..R15, got '" + tok + "'"};
  return r;
}

unsigned needPred(const std::string &tok) {
  unsigned p;
  if (!isPred(tok, p))
    throw AsmError{"expected predicate P0..P3, got '" + tok + "'"};
  return p;
}

int64_t needInt(const std::string &tok) {
  int64_t v;
  if (!parseInt(tok, v))
    throw AsmError{"expected immediate, got '" + tok + "'"};
  return v;
}

int32_t checkImm(int64_t v, unsigned bits) {
  const int64_t lo = -(int64_t(1) << (bits - 1));
  const int64_t hi = (int64_t(1) << (bits - 1)) - 1;
  if (v < lo || v > hi)
    throw AsmError{"immediate " + std::to_string(v) + " does not fit in " +
                   std::to_string(bits) + " bits"};
  return static_cast<int32_t>(v);
}

/// Parses `[Rn]`, `[Rn+imm]`, `[Rn-imm]`.
void parseMem(const std::string &tok, unsigned &rs1, int32_t &imm) {
  std::string t = trim(tok);
  if (t.size() < 3 || t.front() != '[' || t.back() != ']')
    throw AsmError{"expected memory operand [Rn+imm], got '" + tok + "'"};
  t = t.substr(1, t.size() - 2);
  std::string inner;
  for (char c : t)
    if (!std::isspace(static_cast<unsigned char>(c)))
      inner.push_back(c);
  size_t split = inner.find_first_of("+-", 1);
  std::string regTok = inner;
  int64_t off = 0;
  if (split != std::string::npos) {
    regTok = inner.substr(0, split);
    const std::string offTok = inner.substr(split); // includes sign
    if (!parseInt(offTok, off))
      throw AsmError{"bad memory offset in '" + tok + "'"};
  }
  rs1 = needReg(regTok);
  imm = checkImm(off, 14);
}

bool parseCond(const std::string &s, Cond &cond) {
  const std::string u = upper(s);
  static const std::map<std::string, Cond> conds = {
      {"EQ", Cond::EQ}, {"NE", Cond::NE},   {"LT", Cond::LT},
      {"GE", Cond::GE}, {"LTU", Cond::LTU}, {"GEU", Cond::GEU},
      {"LE", Cond::LE}, {"GT", Cond::GT}};
  auto it = conds.find(u);
  if (it == conds.end())
    return false;
  cond = it->second;
  return true;
}

struct AsmLine {
  int lineNo = 0;
  unsigned guard = 0;
  std::string mnemonic; // upper case, without .cond suffix
  std::string suffix;   // text after the first '.', upper case
  std::vector<std::string> ops;
};

Opcode immOpcodeFor(const std::string &m, bool &found) {
  static const std::map<std::string, Opcode> table = {
      {"ADD", Opcode::ADDI}, {"MUL", Opcode::MULI}, {"AND", Opcode::ANDI},
      {"OR", Opcode::ORI},   {"XOR", Opcode::XORI}, {"SHL", Opcode::SHLI},
      {"SHR", Opcode::SHRI}, {"SRA", Opcode::SRAI}};
  auto it = table.find(m);
  found = it != table.end();
  return found ? it->second : Opcode::NOP;
}

Opcode regOpcodeFor(const std::string &m, bool &found) {
  static const std::map<std::string, Opcode> table = {
      {"ADD", Opcode::ADD}, {"SUB", Opcode::SUB}, {"MUL", Opcode::MUL},
      {"AND", Opcode::AND}, {"OR", Opcode::OR},   {"XOR", Opcode::XOR},
      {"SHL", Opcode::SHL}, {"SHR", Opcode::SHR}, {"SRA", Opcode::SRA}};
  auto it = table.find(m);
  found = it != table.end();
  return found ? it->second : Opcode::NOP;
}

Opcode explicitImmOpcode(const std::string &m, bool &found) {
  static const std::map<std::string, Opcode> table = {
      {"ADDI", Opcode::ADDI}, {"MULI", Opcode::MULI}, {"ANDI", Opcode::ANDI},
      {"ORI", Opcode::ORI},   {"XORI", Opcode::XORI}, {"SHLI", Opcode::SHLI},
      {"SHRI", Opcode::SHRI}, {"SRAI", Opcode::SRAI}};
  auto it = table.find(m);
  found = it != table.end();
  return found ? it->second : Opcode::NOP;
}

void expectOps(const AsmLine &l, size_t n) {
  if (l.ops.size() != n)
    throw AsmError{l.mnemonic + " expects " + std::to_string(n) +
                   " operand(s), got " + std::to_string(l.ops.size())};
}

uint32_t encodeLine(const AsmLine &l,
                    const std::map<std::string, uint32_t> &labels) {
  const std::string &m = l.mnemonic;
  const unsigned g = l.guard;

  if (m == "NOP") {
    expectOps(l, 0);
    return encodeR(Opcode::NOP, 0, 0, 0, 0, 0, g);
  }
  if (m == "EXIT") {
    expectOps(l, 0);
    return encodeR(Opcode::EXIT, 0, 0, 0, 0, 0, g);
  }
  if (m == "BAR") {
    expectOps(l, 0);
    return encodeR(Opcode::BAR, 0, 0, 0, 0, 0, g);
  }
  if (m == "BRA") {
    expectOps(l, 1);
    int64_t target;
    if (!parseInt(l.ops[0], target)) {
      auto it = labels.find(upper(l.ops[0]));
      if (it == labels.end())
        throw AsmError{"unknown label '" + l.ops[0] + "'"};
      target = it->second;
    }
    if (target < 0 || target > 0x3FFFFF)
      throw AsmError{"branch target out of range"};
    return encodeB(static_cast<uint32_t>(target), g);
  }
  if (m == "MOVS") {
    expectOps(l, 2);
    const unsigned rd = needReg(l.ops[0]);
    const int64_t id = needInt(l.ops[1]);
    if (id < 0 || id > static_cast<int64_t>(SReg::SMID))
      throw AsmError{"special register id out of range"};
    return encodeI(Opcode::MOVS, rd, 0, static_cast<int32_t>(id), g);
  }
  {
    static const std::map<std::string, SReg> sregs = {
        {"S_TID", SReg::TID},    {"S_BID", SReg::BID},
        {"S_NTID", SReg::NTID},  {"S_NBID", SReg::NBID},
        {"S_LANE", SReg::LANEID}, {"S_WARP", SReg::WARPID},
        {"S_SMID", SReg::SMID}};
    auto it = sregs.find(m);
    if (it != sregs.end()) {
      expectOps(l, 1);
      const unsigned rd = needReg(l.ops[0]);
      return encodeI(Opcode::MOVS, rd, 0, static_cast<int32_t>(it->second), g);
    }
  }
  if (m == "S_ARG") {
    expectOps(l, 2);
    const unsigned rd = needReg(l.ops[0]);
    const int64_t k = needInt(l.ops[1]);
    if (k < 0 || k > 3)
      throw AsmError{"S_ARG index must be 0..3"};
    return encodeI(Opcode::MOVS, rd, 0,
                   static_cast<int32_t>(SReg::ARG0) + static_cast<int32_t>(k),
                   g);
  }
  if (m == "MOV") {
    expectOps(l, 2);
    const unsigned rd = needReg(l.ops[0]);
    unsigned rs;
    if (isReg(l.ops[1], rs))
      return encodeR(Opcode::ADD, rd, rs, 0, 0, 0, g);
    return encodeI(Opcode::ADDI, rd, 0, checkImm(needInt(l.ops[1]), 14), g);
  }
  if (m == "FMA") {
    expectOps(l, 4);
    return encodeR(Opcode::FMA, needReg(l.ops[0]), needReg(l.ops[1]),
                   needReg(l.ops[2]), needReg(l.ops[3]), 0, g);
  }
  {
    bool found;
    const Opcode op = explicitImmOpcode(m, found);
    if (found) {
      expectOps(l, 3);
      return encodeI(op, needReg(l.ops[0]), needReg(l.ops[1]),
                     checkImm(needInt(l.ops[2]), 14), g);
    }
  }
  {
    bool found;
    const Opcode rop = regOpcodeFor(m, found);
    if (found) {
      expectOps(l, 3);
      const unsigned rd = needReg(l.ops[0]);
      const unsigned rs1 = needReg(l.ops[1]);
      unsigned rs2;
      if (isReg(l.ops[2], rs2))
        return encodeR(rop, rd, rs1, rs2, 0, 0, g);
      int64_t imm = needInt(l.ops[2]);
      if (m == "SUB") {
        return encodeI(Opcode::ADDI, rd, rs1, checkImm(-imm, 14), g);
      }
      bool ifound;
      const Opcode iop = immOpcodeFor(m, ifound);
      return encodeI(iop, rd, rs1, checkImm(imm, 14), g);
    }
  }
  if (m == "SETP") {
    Cond cond;
    if (!parseCond(l.suffix, cond))
      throw AsmError{"SETP needs a condition suffix (SETP.EQ/NE/LT/GE/LTU/"
                     "GEU/LE/GT)"};
    expectOps(l, 3);
    const unsigned pd = needPred(l.ops[0]);
    const unsigned rs1 = needReg(l.ops[1]);
    unsigned rs2;
    if (isReg(l.ops[2], rs2))
      return encodeR(Opcode::SETP, pd, rs1, rs2, 0,
                     static_cast<unsigned>(cond), g);
    return encodeP(pd, rs1, cond, checkImm(needInt(l.ops[2]), 10), g);
  }
  if (m == "LDG" || m == "LDS") {
    expectOps(l, 2);
    const unsigned rd = needReg(l.ops[0]);
    unsigned rs1;
    int32_t imm;
    parseMem(l.ops[1], rs1, imm);
    return encodeI(m == "LDG" ? Opcode::LDG : Opcode::LDS, rd, rs1, imm, g);
  }
  if (m == "STG" || m == "STS") {
    expectOps(l, 2);
    // Accept both "STG [addr], Rd" and "STG Rd, [addr]".
    std::string memTok = l.ops[0];
    std::string regTok = l.ops[1];
    if (!memTok.empty() && memTok.front() != '[')
      std::swap(memTok, regTok);
    const unsigned rd = needReg(regTok);
    unsigned rs1;
    int32_t imm;
    parseMem(memTok, rs1, imm);
    return encodeI(m == "STG" ? Opcode::STG : Opcode::STS, rd, rs1, imm, g);
  }
  throw AsmError{"unknown mnemonic '" + m + "'"};
}

} // namespace

std::vector<uint32_t> assembleKernel(const std::string &source,
                                     std::string *error) {
  std::vector<AsmLine> lines;
  std::map<std::string, uint32_t> labels;
  std::vector<uint32_t> words;

  auto report = [&](int lineNo, const std::string &msg) -> std::vector<uint32_t> {
    const std::string full = "line " + std::to_string(lineNo) + ": " + msg;
    if (error) {
      *error = full;
      return {};
    }
    throw std::runtime_error(full);
  };

  // Pass 1: tokenize, collect labels.
  std::istringstream in(source);
  std::string raw;
  int lineNo = 0;
  while (std::getline(in, raw)) {
    ++lineNo;
    std::string text = trim(stripComment(raw));
    // Labels (possibly several) at the start of the line.
    while (true) {
      const size_t colon = text.find(':');
      if (colon == std::string::npos)
        break;
      const std::string before = text.substr(0, colon);
      // Only treat as label if there's no whitespace in 'before' and it does
      // not look like an operand list.
      if (before.empty() ||
          before.find_first_of(" \t,[]") != std::string::npos)
        break;
      const std::string label = upper(trim(before));
      if (labels.count(label))
        return report(lineNo, "duplicate label '" + before + "'");
      labels[label] = static_cast<uint32_t>(lines.size());
      text = trim(text.substr(colon + 1));
    }
    if (text.empty())
      continue;

    AsmLine l;
    l.lineNo = lineNo;
    // Guard prefix.
    if (text[0] == '@') {
      const size_t sp = text.find_first_of(" \t");
      std::string gtok = upper(text.substr(1, sp == std::string::npos
                                                  ? std::string::npos
                                                  : sp - 1));
      text = sp == std::string::npos ? "" : trim(text.substr(sp));
      bool neg = false;
      if (!gtok.empty() && gtok[0] == '!') {
        neg = true;
        gtok = gtok.substr(1);
      }
      unsigned p;
      if (!isPred(gtok, p))
        return report(lineNo, "bad guard predicate '@" + gtok + "'");
      l.guard = makeGuard(true, neg, p);
    }
    if (text.empty())
      return report(lineNo, "guard without instruction");
    const size_t sp = text.find_first_of(" \t");
    std::string mn = upper(text.substr(0, sp));
    const std::string rest = sp == std::string::npos ? "" : trim(text.substr(sp));
    const size_t dot = mn.find('.');
    if (dot != std::string::npos) {
      l.suffix = mn.substr(dot + 1);
      mn = mn.substr(0, dot);
    }
    l.mnemonic = mn;
    l.ops = splitOperands(rest);
    lines.push_back(l);
  }

  // Pass 2: encode.
  words.reserve(lines.size());
  for (const auto &l : lines) {
    try {
      words.push_back(encodeLine(l, labels));
    } catch (const AsmError &e) {
      return report(l.lineNo, e.msg);
    }
  }
  return words;
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

SimtGpu::SimtGpu() { rebuildSMs(); }

SimtGpu::SimtGpu(const GpuConfig &config, ReadFn read, WriteFn write)
    : m_read(std::move(read)), m_write(std::move(write)) {
  setConfig(config);
}

void SimtGpu::setMemoryCallbacks(ReadFn read, WriteFn write) {
  m_read = std::move(read);
  m_write = std::move(write);
}

void SimtGpu::setConfig(const GpuConfig &config) {
  m_config = config;
  m_config.numSMs = std::max(1u, m_config.numSMs);
  m_config.warpsPerSM = std::max(1u, m_config.warpsPerSM);
  m_config.warpSize = std::min(32u, std::max(1u, m_config.warpSize));
  m_config.sharedBanks = std::max(1u, m_config.sharedBanks);
  m_config.sharedBytes = std::max(4u, m_config.sharedBytes);
  reset();
}

void SimtGpu::rebuildSMs() {
  m_sms.clear();
  m_sms.resize(m_config.numSMs);
  for (auto &sm : m_sms) {
    sm.warps.resize(m_config.warpsPerSM);
    for (auto &w : sm.warps)
      w = WarpState();
    sm.blocks.clear();
    sm.lastIssued = m_config.warpsPerSM - 1;
    sm.issuedThisCycle = false;
    sm.issued = 0;
    sm.idleSlots = 0;
  }
}

void SimtGpu::reset() {
  rebuildSMs();
  m_stats = GpuStats();
  m_params = LaunchParams();
  m_launched = false;
  m_busy = false;
  m_nextBlock = 0;
  m_error.clear();
  m_pendingFault.clear();
}

bool SimtGpu::launch(const LaunchParams &params) {
  if (m_busy) {
    // Ignored: the running kernel (and its error state) must not be touched.
    return false;
  }
  m_error.clear();
  m_pendingFault.clear();
  m_stats = GpuStats();
  m_params = params;
  m_nextBlock = 0;
  m_launched = true;
  rebuildSMs();

  if (!m_read || !m_write) {
    fail("no memory callbacks installed");
    return false;
  }
  if (params.gridX == 0 || params.blockX == 0) {
    fail("gridX and blockX must be >= 1");
    return false;
  }
  if (warpsPerBlock(params.blockX) > m_config.warpsPerSM) {
    fail("block of " + std::to_string(params.blockX) + " threads needs " +
         std::to_string(warpsPerBlock(params.blockX)) +
         " warps but an SM only has " + std::to_string(m_config.warpsPerSM));
    return false;
  }
  if (params.kernelPc % 4 != 0) {
    fail("kernel address must be word aligned");
    return false;
  }
  m_busy = true;
  return true;
}

void SimtGpu::fault(const std::string &msg) {
  if (m_pendingFault.empty())
    m_pendingFault = msg;
}

void SimtGpu::abort() {
  for (auto &sm : m_sms) {
    for (auto &w : sm.warps)
      w.valid = false;
    sm.blocks.clear();
  }
  m_busy = false;
}

void SimtGpu::fail(const std::string &msg) {
  m_error = msg;
  abort();
}

SimtGpu::State SimtGpu::saveState() const {
  State s;
  s.sms = m_sms;
  s.stats = m_stats;
  s.params = m_params;
  s.launched = m_launched;
  s.busy = m_busy;
  s.nextBlock = m_nextBlock;
  s.error = m_error;
  return s;
}

void SimtGpu::restoreState(const State &s) {
  m_sms = s.sms;
  m_stats = s.stats;
  m_params = s.params;
  m_launched = s.launched;
  m_busy = s.busy;
  m_nextBlock = s.nextBlock;
  m_error = s.error;
  m_pendingFault.clear();
}

size_t SimtGpu::stateBytes(const State &s) {
  size_t bytes = sizeof(State) + s.error.capacity();
  for (const auto &sm : s.sms) {
    bytes += sizeof(SMState);
    for (const auto &w : sm.warps) {
      bytes += sizeof(WarpState) + w.paths.capacity() * sizeof(WarpPath) +
               w.regs.capacity() * sizeof(uint32_t) +
               w.preds.capacity() * sizeof(uint8_t);
    }
    for (const auto &b : sm.blocks)
      bytes += sizeof(BlockState) + b.shared.capacity() * sizeof(uint32_t);
  }
  return bytes;
}

BlockState *SimtGpu::findBlock(SMState &sm, uint32_t blockId) {
  for (auto &b : sm.blocks)
    if (b.blockId == blockId)
      return &b;
  return nullptr;
}

void SimtGpu::tryDispatch(unsigned smIdx) {
  if (m_nextBlock >= m_params.gridX)
    return;
  auto &sm = m_sms[smIdx];
  const unsigned need = warpsPerBlock(m_params.blockX);
  unsigned free = 0;
  for (const auto &w : sm.warps)
    if (!w.valid)
      ++free;
  if (free < need)
    return;
  dispatchBlock(smIdx, m_nextBlock);
  ++m_nextBlock;
}

void SimtGpu::dispatchBlock(unsigned smIdx, uint32_t blockId) {
  auto &sm = m_sms[smIdx];
  const unsigned need = warpsPerBlock(m_params.blockX);

  BlockState block;
  block.blockId = blockId;
  block.liveWarps = need;
  block.warpsAtBarrier = 0;
  block.shared.assign(m_config.sharedBytes / 4, 0u);
  sm.blocks.push_back(std::move(block));

  unsigned placed = 0;
  for (auto &w : sm.warps) {
    if (placed == need)
      break;
    if (w.valid)
      continue;
    w = WarpState();
    w.valid = true;
    w.blockId = blockId;
    w.warpInBlock = placed;
    uint32_t vmask = 0;
    for (unsigned lane = 0; lane < m_config.warpSize; ++lane) {
      const uint32_t tid = placed * m_config.warpSize + lane;
      if (tid < m_params.blockX)
        vmask |= (1u << lane);
    }
    w.validMask = vmask;
    w.exitedMask = 0;
    w.paths.push_back(WarpPath{0, vmask, false});
    w.regs.assign(m_config.warpSize * 16, 0u);
    w.preds.assign(m_config.warpSize, 0u);
    ++placed;
  }
  m_stats.blocksLaunched++;
}

void SimtGpu::releaseBarrier(SMState &sm, uint32_t blockId) {
  for (auto &o : sm.warps) {
    if (!o.valid || o.blockId != blockId)
      continue;
    o.atBarrier = false;
    for (auto &p : o.paths)
      p.parked = false;
    normalizePaths(o); // parked and running paths at the same pc reconverge
  }
  if (BlockState *block = findBlock(sm, blockId))
    block->warpsAtBarrier = 0;
}

void SimtGpu::arriveAtBarrier(SMState &sm, WarpState &w) {
  // One arrival per warp, no matter how many divergent paths reached a BAR.
  w.atBarrier = true;
  m_stats.barriers++;
  BlockState *block = findBlock(sm, w.blockId);
  if (!block) {
    releaseBarrier(sm, w.blockId);
    return;
  }
  block->warpsAtBarrier++;
  if (block->warpsAtBarrier >= block->liveWarps)
    releaseBarrier(sm, w.blockId);
}

void SimtGpu::retireFinished(unsigned smIdx) {
  auto &sm = m_sms[smIdx];
  for (auto &w : sm.warps) {
    if (!w.valid || !w.finished())
      continue;
    w.valid = false;
    BlockState *block = findBlock(sm, w.blockId);
    if (!block)
      continue;
    if (block->liveWarps > 0)
      block->liveWarps--;
    if (block->liveWarps == 0) {
      const uint32_t id = w.blockId;
      sm.blocks.erase(std::remove_if(sm.blocks.begin(), sm.blocks.end(),
                                     [id](const BlockState &b) {
                                       return b.blockId == id;
                                     }),
                      sm.blocks.end());
      m_stats.blocksCompleted++;
    } else if (block->warpsAtBarrier > 0 &&
               block->warpsAtBarrier >= block->liveWarps) {
      // The remaining warps were all waiting for this (now exited) warp.
      releaseBarrier(sm, w.blockId);
    }
  }
}

void SimtGpu::step() {
  if (!m_busy)
    return;

  for (unsigned smIdx = 0; smIdx < m_sms.size(); ++smIdx) {
    auto &sm = m_sms[smIdx];
    sm.issuedThisCycle = false;
    tryDispatch(smIdx);

    const unsigned n = static_cast<unsigned>(sm.warps.size());
    int issuedIdx = -1;
    for (unsigned k = 1; k <= n; ++k) {
      const unsigned idx = (sm.lastIssued + k) % n;
      auto &w = sm.warps[idx];
      if (!w.valid || w.finished() || w.stallCycles > 0 || w.atBarrier ||
          w.currentPath() < 0)
        continue;
      execute(smIdx, idx);
      if (!m_busy)
        return; // execution error aborted the kernel
      sm.lastIssued = idx;
      sm.issuedThisCycle = true;
      sm.issued++;
      m_stats.issued++;
      issuedIdx = static_cast<int>(idx);
      break;
    }
    if (issuedIdx < 0) {
      sm.idleSlots++;
      m_stats.idleSlots++;
    }
    for (unsigned i = 0; i < n; ++i) {
      auto &w = sm.warps[i];
      if (w.valid && w.stallCycles > 0 && static_cast<int>(i) != issuedIdx)
        w.stallCycles--;
    }
    retireFinished(smIdx);
  }
  m_stats.cycles++;

  if (m_nextBlock >= m_params.gridX) {
    bool anyLive = false;
    for (const auto &sm : m_sms)
      for (const auto &w : sm.warps)
        if (w.valid)
          anyLive = true;
    if (!anyLive)
      m_busy = false;
  }
}

void SimtGpu::normalizePaths(WarpState &w) {
  auto &p = w.paths;
  p.erase(std::remove_if(p.begin(), p.end(),
                         [](const WarpPath &x) { return x.mask == 0; }),
          p.end());
  std::stable_sort(p.begin(), p.end(), [](const WarpPath &a, const WarpPath &b) {
    if (a.pc != b.pc)
      return a.pc < b.pc;
    return !a.parked && b.parked; // running before parked at the same pc
  });
  std::vector<WarpPath> merged;
  merged.reserve(p.size());
  for (const auto &x : p) {
    // Reconvergence: same pc and same barrier state. A parked path never
    // absorbs lanes that did not execute the BAR.
    if (!merged.empty() && merged.back().pc == x.pc &&
        merged.back().parked == x.parked)
      merged.back().mask |= x.mask;
    else
      merged.push_back(x);
  }
  p = std::move(merged);
}

unsigned SimtGpu::popcount(uint32_t v) {
  unsigned c = 0;
  while (v) {
    v &= v - 1;
    ++c;
  }
  return c;
}

uint32_t SimtGpu::sreg(const WarpState &w, unsigned smIdx, unsigned lane,
                       unsigned id) const {
  switch (static_cast<SReg>(id)) {
  case SReg::TID:
    return w.warpInBlock * m_config.warpSize + lane;
  case SReg::BID:
    return w.blockId;
  case SReg::NTID:
    return m_params.blockX;
  case SReg::NBID:
    return m_params.gridX;
  case SReg::ARG0:
    return m_params.args[0];
  case SReg::ARG1:
    return m_params.args[1];
  case SReg::ARG2:
    return m_params.args[2];
  case SReg::ARG3:
    return m_params.args[3];
  case SReg::LANEID:
    return lane;
  case SReg::WARPID:
    return w.warpInBlock;
  case SReg::SMID:
    return smIdx;
  }
  return 0;
}

bool SimtGpu::evalCond(Cond c, uint32_t a, uint32_t b) {
  const int32_t sa = static_cast<int32_t>(a);
  const int32_t sb = static_cast<int32_t>(b);
  switch (c) {
  case Cond::EQ:
    return a == b;
  case Cond::NE:
    return a != b;
  case Cond::LT:
    return sa < sb;
  case Cond::GE:
    return sa >= sb;
  case Cond::LTU:
    return a < b;
  case Cond::GEU:
    return a >= b;
  case Cond::LE:
    return sa <= sb;
  case Cond::GT:
    return sa > sb;
  }
  return false;
}

uint32_t SimtGpu::alu(Opcode op, uint32_t a, uint32_t b, uint32_t c) {
  switch (op) {
  case Opcode::ADD:
  case Opcode::ADDI:
    return a + b;
  case Opcode::SUB:
    return a - b;
  case Opcode::MUL:
  case Opcode::MULI:
    return a * b;
  case Opcode::FMA:
    return a * b + c;
  case Opcode::AND:
  case Opcode::ANDI:
    return a & b;
  case Opcode::OR:
  case Opcode::ORI:
    return a | b;
  case Opcode::XOR:
  case Opcode::XORI:
    return a ^ b;
  case Opcode::SHL:
  case Opcode::SHLI:
    return a << (b & 31u);
  case Opcode::SHR:
  case Opcode::SHRI:
    return a >> (b & 31u);
  case Opcode::SRA:
  case Opcode::SRAI:
    return static_cast<uint32_t>(static_cast<int32_t>(a) >>
                                 static_cast<int>(b & 31u));
  case Opcode::NOP:
  case Opcode::EXIT:
  case Opcode::BAR:
  case Opcode::BRA:
  case Opcode::MOVS:
  case Opcode::SETP:
  case Opcode::SETPI:
  case Opcode::LDG:
  case Opcode::STG:
  case Opcode::LDS:
  case Opcode::STS:
    break;
  }
  return 0;
}

void SimtGpu::execute(unsigned smIdx, unsigned warpIdx) {
  auto &sm = m_sms[smIdx];
  auto &w = sm.warps[warpIdx];
  const int ci = w.currentPath();
  if (ci < 0)
    return; // every path parked at a barrier (not schedulable)
  const size_t cur = static_cast<size_t>(ci);
  const uint32_t pc = w.paths[cur].pc;
  const uint32_t active = w.paths[cur].mask;
  m_pendingFault.clear();
  const uint32_t word = m_read(m_params.kernelPc + pc * 4u);

  w.hasLastInstr = true;
  w.lastInstr = word;
  w.lastPc = pc;

  auto checkFault = [&]() {
    if (m_pendingFault.empty())
      return false;
    std::ostringstream os;
    os << m_pendingFault << " (kernel pc " << pc << ", block " << w.blockId
       << ", warp " << w.warpInBlock << ")";
    m_pendingFault.clear();
    fail(os.str());
    return true;
  };
  if (checkFault()) {
    w.lastExecMask = 0;
    return;
  }

  const Instr in = decode(word);
  if (!in.valid) {
    std::ostringstream os;
    os << "undefined instruction 0x" << std::hex << word << " at kernel pc "
       << std::dec << pc << " (block " << w.blockId << ", warp "
       << w.warpInBlock << ")";
    w.lastExecMask = 0;
    fail(os.str());
    return;
  }

  // Lanes that pass the guard predicate.
  uint32_t exec = active;
  if (in.guardEn) {
    exec = 0;
    for (unsigned lane = 0; lane < m_config.warpSize; ++lane) {
      if (!((active >> lane) & 1u))
        continue;
      const bool p = (w.preds[lane] >> in.guardP) & 1u;
      if (p != in.guardNeg)
        exec |= (1u << lane);
    }
  }
  w.lastExecMask = exec;
  m_stats.threadInstrs += popcount(exec);

  bool advance = true;
  const unsigned ws = m_config.warpSize;

  switch (in.op) {
  case Opcode::NOP:
    break;

  case Opcode::EXIT:
    w.exitedMask |= exec;
    w.paths[cur].mask &= ~exec;
    break;

  case Opcode::BAR: {
    if (exec == 0)
      break;
    // The executing lanes park just past the BAR; lanes whose guard was false
    // continue as a separate (running) path. The warp arrives at the block
    // barrier once all of its live lanes are parked (checked below).
    const uint32_t rest = active & ~exec;
    w.paths[cur] = WarpPath{pc + 1, exec, true};
    if (rest != 0)
      w.paths.push_back(WarpPath{pc + 1, rest, false});
    advance = false;
    break;
  }

  case Opcode::BRA: {
    const uint32_t taken = exec;
    const uint32_t notTaken = active & ~exec;
    if (taken != 0 && notTaken != 0) {
      m_stats.divergences++;
      w.paths[cur] = WarpPath{pc + 1, notTaken, false};
      w.paths.push_back(WarpPath{in.target, taken, false});
      advance = false;
    } else if (taken != 0) {
      w.paths[cur].pc = in.target;
      advance = false;
    }
    break;
  }

  case Opcode::MOVS:
    for (unsigned lane = 0; lane < ws; ++lane)
      if ((exec >> lane) & 1u)
        writeReg(w, lane, in.rd,
                 sreg(w, smIdx, lane, static_cast<unsigned>(in.imm)));
    break;

  case Opcode::ADD:
  case Opcode::SUB:
  case Opcode::MUL:
  case Opcode::FMA:
  case Opcode::AND:
  case Opcode::OR:
  case Opcode::XOR:
  case Opcode::SHL:
  case Opcode::SHR:
  case Opcode::SRA:
    for (unsigned lane = 0; lane < ws; ++lane)
      if ((exec >> lane) & 1u)
        writeReg(w, lane, in.rd,
                 alu(in.op, readReg(w, lane, in.rs1), readReg(w, lane, in.rs2),
                     readReg(w, lane, in.rs3)));
    break;

  case Opcode::ADDI:
  case Opcode::MULI:
  case Opcode::ANDI:
  case Opcode::ORI:
  case Opcode::XORI:
  case Opcode::SHLI:
  case Opcode::SHRI:
  case Opcode::SRAI:
    for (unsigned lane = 0; lane < ws; ++lane)
      if ((exec >> lane) & 1u)
        writeReg(w, lane, in.rd,
                 alu(in.op, readReg(w, lane, in.rs1),
                     static_cast<uint32_t>(in.imm), 0));
    break;

  case Opcode::SETP:
  case Opcode::SETPI: {
    const unsigned pd = in.rd & 3u;
    const Cond cond = static_cast<Cond>(
        (in.op == Opcode::SETP ? in.funct : in.rs2) & 7u);
    for (unsigned lane = 0; lane < ws; ++lane) {
      if (!((exec >> lane) & 1u))
        continue;
      const uint32_t a = readReg(w, lane, in.rs1);
      const uint32_t b = in.op == Opcode::SETP ? readReg(w, lane, in.rs2)
                                               : static_cast<uint32_t>(in.imm);
      const bool r = evalCond(cond, a, b);
      w.preds[lane] = static_cast<uint8_t>((w.preds[lane] & ~(1u << pd)) |
                                           ((r ? 1u : 0u) << pd));
    }
    break;
  }

  case Opcode::LDG:
  case Opcode::STG: {
    // Coalescing: count distinct 32-byte segments touched by the lanes.
    std::vector<uint32_t> segments;
    for (unsigned lane = 0; lane < ws; ++lane) {
      if (!((exec >> lane) & 1u))
        continue;
      const uint32_t addr =
          readReg(w, lane, in.rs1) + static_cast<uint32_t>(in.imm);
      const uint32_t seg = addr >> 5;
      if (std::find(segments.begin(), segments.end(), seg) == segments.end())
        segments.push_back(seg);
      if (in.op == Opcode::LDG) {
        const uint32_t v = m_read(addr);
        if (checkFault())
          return;
        writeReg(w, lane, in.rd, v);
      } else {
        m_write(addr, readReg(w, lane, in.rd));
        if (checkFault())
          return;
      }
    }
    const unsigned txn = static_cast<unsigned>(segments.size());
    if (txn > 0) {
      m_stats.memInstrs++;
      m_stats.memTransactions += txn;
      // Minimum: the active lanes' 4-byte words packed into 32-byte segments.
      const unsigned minTxn = (popcount(exec) * 4u + 31u) / 32u;
      if (txn <= minTxn) {
        m_stats.coalescedInstrs++;
        m_stats.coalescedTxns += txn;
      } else {
        m_stats.uncoalescedInstrs++;
        m_stats.uncoalescedTxns += txn;
      }
      if (in.op == Opcode::LDG)
        w.stallCycles = m_config.memLatency + (txn - 1);
      else
        w.stallCycles = txn - 1;
    }
    break;
  }

  case Opcode::LDS:
  case Opcode::STS: {
    BlockState *block = findBlock(sm, w.blockId);
    // Bank conflicts: distinct words mapping to the same bank serialize.
    std::vector<uint32_t> words;
    for (unsigned lane = 0; lane < ws; ++lane) {
      if (!((exec >> lane) & 1u))
        continue;
      const uint32_t addr =
          readReg(w, lane, in.rs1) + static_cast<uint32_t>(in.imm);
      const uint32_t idx = addr >> 2;
      if (std::find(words.begin(), words.end(), idx) == words.end())
        words.push_back(idx);
      if (!block)
        continue;
      if (in.op == Opcode::LDS) {
        writeReg(w, lane, in.rd,
                 idx < block->shared.size() ? block->shared[idx] : 0u);
      } else if (idx < block->shared.size()) {
        block->shared[idx] = readReg(w, lane, in.rd);
      }
    }
    std::vector<unsigned> perBank(m_config.sharedBanks, 0u);
    unsigned worst = 0;
    for (const auto idx : words) {
      unsigned &cnt = perBank[idx % m_config.sharedBanks];
      cnt++;
      worst = std::max(worst, cnt);
    }
    if (worst > 1) {
      const unsigned extra = worst - 1;
      w.stallCycles = extra;
      m_stats.bankConflicts++;
      m_stats.bankConflictCycles += extra;
    }
    break;
  }
  }

  if (advance && cur < w.paths.size())
    w.paths[cur].pc = pc + 1;
  normalizePaths(w);

  // A warp arrives at the block barrier once every live lane is parked.
  if (!w.paths.empty() && !w.atBarrier && w.currentPath() < 0)
    arriveAtBarrier(sm, w);
}

} // namespace gpu
} // namespace Ripes
