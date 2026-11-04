#!/usr/bin/env python3
"""Tiny assembler/disassembler for the open-soc-explorer SIMT GPU kernel ISA.

It mirrors the C++ encoder in src/accel/gpu/simtgpu.cpp (assembleKernel) so
that kernels can be pre-assembled into `.word` directives for a RISC-V host
program (see examples/assembly/gpu_vecadd.s and docs/arch/gpu.md).

Usage:
    python tools/gpu_asm.py kernel.gpu            # print .word lines
    python tools/gpu_asm.py kernel.gpu -f hex     # one hex word per line
    python tools/gpu_asm.py kernel.gpu -f c       # C array initialiser
    python tools/gpu_asm.py -d 0x02908c00 ...     # disassemble words
    python tools/gpu_asm.py --selftest            # encoder self-check

Encoding (32-bit words, see simtgpu.h):

    bit  31  30  29..28  27..22  21..18  17..14  13..10  9..6   5..0
   R:   |GE |GN | GP    |opcode |  rd   |  rs1  |  rs2  | rs3  |funct |
   I:   |GE |GN | GP    |opcode |  rd   |  rs1  |         imm14        |
   P:   |GE |GN | GP    |opcode | pidx  |  rs1  | cond  |    imm10     |
   B:   |GE |GN | GP    |opcode |              target22               |
"""

import argparse
import re
import sys

OPCODES = {
    "NOP": 0, "EXIT": 1, "BAR": 2, "BRA": 3, "MOVS": 4,
    "ADD": 8, "SUB": 9, "MUL": 10, "FMA": 11, "AND": 12, "OR": 13,
    "XOR": 14, "SHL": 15, "SHR": 16, "SRA": 17,
    "ADDI": 24, "MULI": 25, "ANDI": 26, "ORI": 27, "XORI": 28,
    "SHLI": 29, "SHRI": 30, "SRAI": 31,
    "SETP": 32, "SETPI": 33,
    "LDG": 40, "STG": 41, "LDS": 42, "STS": 43,
}
OPNAMES = {v: k for k, v in OPCODES.items()}
OPNAMES[33] = "SETP"

CONDS = {"EQ": 0, "NE": 1, "LT": 2, "GE": 3, "LTU": 4, "GEU": 5, "LE": 6, "GT": 7}
CONDNAMES = {v: k for k, v in CONDS.items()}

SREGS = {"S_TID": 0, "S_BID": 1, "S_NTID": 2, "S_NBID": 3,
         "S_LANE": 8, "S_WARP": 9, "S_SMID": 10}
SREGNAMES = {v: k for k, v in SREGS.items()}

R_OPS = {"ADD", "SUB", "MUL", "AND", "OR", "XOR", "SHL", "SHR", "SRA"}
I_OPS = {"ADDI", "MULI", "ANDI", "ORI", "XORI", "SHLI", "SHRI", "SRAI"}
IMM_OF = {"ADD": "ADDI", "MUL": "MULI", "AND": "ANDI", "OR": "ORI",
          "XOR": "XORI", "SHL": "SHLI", "SHR": "SHRI", "SRA": "SRAI"}


class AsmError(Exception):
    pass


# ----------------------------------------------------------------------------
# Encoders
# ----------------------------------------------------------------------------

def guard(enable, negate, pidx):
    return (8 if enable else 0) | (4 if negate else 0) | (pidx & 3)


def enc_r(op, rd, rs1, rs2, rs3=0, funct=0, g=0):
    return ((g & 0xF) << 28) | ((op & 0x3F) << 22) | ((rd & 0xF) << 18) | \
           ((rs1 & 0xF) << 14) | ((rs2 & 0xF) << 10) | ((rs3 & 0xF) << 6) | \
           (funct & 0x3F)


def enc_i(op, rd, rs1, imm, g=0):
    return ((g & 0xF) << 28) | ((op & 0x3F) << 22) | ((rd & 0xF) << 18) | \
           ((rs1 & 0xF) << 14) | (imm & 0x3FFF)


def enc_p(pidx, rs1, cond, imm, g=0):
    return ((g & 0xF) << 28) | ((OPCODES["SETPI"] & 0x3F) << 22) | \
           ((pidx & 3) << 18) | ((rs1 & 0xF) << 14) | ((cond & 0xF) << 10) | \
           (imm & 0x3FF)


def enc_b(target, g=0):
    return ((g & 0xF) << 28) | ((OPCODES["BRA"] & 0x3F) << 22) | \
           (target & 0x3FFFFF)


def check_imm(v, bits):
    lo, hi = -(1 << (bits - 1)), (1 << (bits - 1)) - 1
    if v < lo or v > hi:
        raise AsmError("immediate %d does not fit in %d bits" % (v, bits))
    return v


# ----------------------------------------------------------------------------
# Operand parsing
# ----------------------------------------------------------------------------

def parse_int(tok):
    try:
        return int(tok.strip(), 0)
    except ValueError:
        return None


def is_reg(tok):
    m = re.fullmatch(r"[Rr](\d{1,2})", tok.strip())
    if not m or int(m.group(1)) > 15:
        return None
    return int(m.group(1))


def need_reg(tok):
    r = is_reg(tok)
    if r is None:
        raise AsmError("expected register R0..R15, got '%s'" % tok)
    return r


def need_pred(tok):
    m = re.fullmatch(r"[Pp]([0-3])", tok.strip())
    if not m:
        raise AsmError("expected predicate P0..P3, got '%s'" % tok)
    return int(m.group(1))


def need_int(tok):
    v = parse_int(tok)
    if v is None:
        raise AsmError("expected immediate, got '%s'" % tok)
    return v


def parse_mem(tok):
    t = tok.strip()
    if not (t.startswith("[") and t.endswith("]")):
        raise AsmError("expected memory operand [Rn+imm], got '%s'" % tok)
    inner = re.sub(r"\s+", "", t[1:-1])
    m = re.fullmatch(r"([Rr]\d{1,2})([+-].+)?", inner)
    if not m:
        raise AsmError("bad memory operand '%s'" % tok)
    rs1 = need_reg(m.group(1))
    off = need_int(m.group(2)) if m.group(2) else 0
    return rs1, check_imm(off, 14)


def split_ops(s):
    ops, cur, depth = [], "", 0
    for c in s:
        if c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
        if c == "," and depth == 0:
            ops.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip():
        ops.append(cur.strip())
    return ops


# ----------------------------------------------------------------------------
# Assembler
# ----------------------------------------------------------------------------

def strip_comment(line):
    for marker in ("#", "//", ";"):
        i = line.find(marker)
        if i >= 0:
            line = line[:i]
    return line


def expect(ops, n, mn):
    if len(ops) != n:
        raise AsmError("%s expects %d operand(s), got %d" % (mn, n, len(ops)))


def encode_line(mn, suffix, ops, g, labels):
    if mn in ("NOP", "EXIT", "BAR"):
        expect(ops, 0, mn)
        return enc_r(OPCODES[mn], 0, 0, 0, 0, 0, g)
    if mn == "BRA":
        expect(ops, 1, mn)
        t = parse_int(ops[0])
        if t is None:
            key = ops[0].upper()
            if key not in labels:
                raise AsmError("unknown label '%s'" % ops[0])
            t = labels[key]
        if t < 0 or t > 0x3FFFFF:
            raise AsmError("branch target out of range")
        return enc_b(t, g)
    if mn == "MOVS":
        expect(ops, 2, mn)
        sid = need_int(ops[1])
        if sid < 0 or sid > 10:
            raise AsmError("special register id out of range")
        return enc_i(OPCODES["MOVS"], need_reg(ops[0]), 0, sid, g)
    if mn in SREGS:
        expect(ops, 1, mn)
        return enc_i(OPCODES["MOVS"], need_reg(ops[0]), 0, SREGS[mn], g)
    if mn == "S_ARG":
        expect(ops, 2, mn)
        k = need_int(ops[1])
        if k < 0 or k > 3:
            raise AsmError("S_ARG index must be 0..3")
        return enc_i(OPCODES["MOVS"], need_reg(ops[0]), 0, 4 + k, g)
    if mn == "MOV":
        expect(ops, 2, mn)
        rd = need_reg(ops[0])
        rs = is_reg(ops[1])
        if rs is not None:
            return enc_r(OPCODES["ADD"], rd, rs, 0, 0, 0, g)
        return enc_i(OPCODES["ADDI"], rd, 0, check_imm(need_int(ops[1]), 14), g)
    if mn == "FMA":
        expect(ops, 4, mn)
        return enc_r(OPCODES["FMA"], need_reg(ops[0]), need_reg(ops[1]),
                     need_reg(ops[2]), need_reg(ops[3]), 0, g)
    if mn in I_OPS:
        expect(ops, 3, mn)
        return enc_i(OPCODES[mn], need_reg(ops[0]), need_reg(ops[1]),
                     check_imm(need_int(ops[2]), 14), g)
    if mn in R_OPS:
        expect(ops, 3, mn)
        rd, rs1 = need_reg(ops[0]), need_reg(ops[1])
        rs2 = is_reg(ops[2])
        if rs2 is not None:
            return enc_r(OPCODES[mn], rd, rs1, rs2, 0, 0, g)
        imm = need_int(ops[2])
        if mn == "SUB":
            return enc_i(OPCODES["ADDI"], rd, rs1, check_imm(-imm, 14), g)
        return enc_i(OPCODES[IMM_OF[mn]], rd, rs1, check_imm(imm, 14), g)
    if mn == "SETP":
        if suffix not in CONDS:
            raise AsmError("SETP needs a condition suffix (SETP.EQ/NE/LT/GE/"
                           "LTU/GEU/LE/GT)")
        expect(ops, 3, mn)
        pd, rs1 = need_pred(ops[0]), need_reg(ops[1])
        rs2 = is_reg(ops[2])
        if rs2 is not None:
            return enc_r(OPCODES["SETP"], pd, rs1, rs2, 0, CONDS[suffix], g)
        return enc_p(pd, rs1, CONDS[suffix],
                     check_imm(need_int(ops[2]), 10), g)
    if mn in ("LDG", "LDS"):
        expect(ops, 2, mn)
        rs1, imm = parse_mem(ops[1])
        return enc_i(OPCODES[mn], need_reg(ops[0]), rs1, imm, g)
    if mn in ("STG", "STS"):
        expect(ops, 2, mn)
        mem_tok, reg_tok = ops
        if not mem_tok.startswith("["):
            mem_tok, reg_tok = reg_tok, mem_tok
        rs1, imm = parse_mem(mem_tok)
        return enc_i(OPCODES[mn], need_reg(reg_tok), rs1, imm, g)
    raise AsmError("unknown mnemonic '%s'" % mn)


def assemble(source):
    """Returns a list of (word, line_no, source_text)."""
    lines, labels = [], {}
    for line_no, raw in enumerate(source.splitlines(), 1):
        text = strip_comment(raw).strip()
        while True:
            i = text.find(":")
            if i < 0:
                break
            before = text[:i]
            if not before or re.search(r"[\s,\[\]]", before):
                break
            key = before.strip().upper()
            if key in labels:
                raise AsmError("line %d: duplicate label '%s'" % (line_no, before))
            labels[key] = len(lines)
            text = text[i + 1:].strip()
        if not text:
            continue
        g = 0
        if text.startswith("@"):
            parts = text.split(None, 1)
            gtok = parts[0][1:].upper()
            text = parts[1].strip() if len(parts) > 1 else ""
            neg = gtok.startswith("!")
            if neg:
                gtok = gtok[1:]
            m = re.fullmatch(r"P([0-3])", gtok)
            if not m:
                raise AsmError("line %d: bad guard predicate '@%s'" % (line_no, gtok))
            g = guard(True, neg, int(m.group(1)))
        if not text:
            raise AsmError("line %d: guard without instruction" % line_no)
        parts = text.split(None, 1)
        mn = parts[0].upper()
        rest = parts[1] if len(parts) > 1 else ""
        suffix = ""
        if "." in mn:
            mn, suffix = mn.split(".", 1)
        lines.append((line_no, mn, suffix, split_ops(rest), g, raw.strip()))

    out = []
    for line_no, mn, suffix, ops, g, text in lines:
        try:
            out.append((encode_line(mn, suffix, ops, g, labels), line_no, text))
        except AsmError as e:
            raise AsmError("line %d: %s" % (line_no, e))
    return out


# ----------------------------------------------------------------------------
# Disassembler
# ----------------------------------------------------------------------------

def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def disassemble(word):
    op = (word >> 22) & 0x3F
    if op not in OPNAMES:
        return "??? (0x%08x)" % word
    name = OPNAMES[op]
    rd, rs1, rs2, rs3, funct = ((word >> 18) & 0xF, (word >> 14) & 0xF,
                                (word >> 10) & 0xF, (word >> 6) & 0xF,
                                word & 0x3F)
    pre = ""
    if word >> 31:
        pre = "@%sP%d " % ("!" if (word >> 30) & 1 else "", (word >> 28) & 3)
    r = lambda n: "R%d" % n  # noqa: E731

    def mem(reg, imm):
        if imm > 0:
            return "[%s+%d]" % (r(reg), imm)
        if imm < 0:
            return "[%s%d]" % (r(reg), imm)
        return "[%s]" % r(reg)

    if name in ("NOP", "EXIT", "BAR"):
        body = name
    elif name == "BRA":
        body = "BRA %d" % (word & 0x3FFFFF)
    elif name == "MOVS":
        sid = sext(word, 14)
        if 4 <= sid <= 7:
            body = "S_ARG %s, %d" % (r(rd), sid - 4)
        elif sid in SREGNAMES:
            body = "%s %s" % (SREGNAMES[sid], r(rd))
        else:
            body = "MOVS %s, %d" % (r(rd), sid)
    elif name == "FMA":
        body = "FMA %s, %s, %s, %s" % (r(rd), r(rs1), r(rs2), r(rs3))
    elif name in R_OPS:
        body = "%s %s, %s, %s" % (name, r(rd), r(rs1), r(rs2))
    elif name in I_OPS:
        body = "%s %s, %s, %d" % (name, r(rd), r(rs1), sext(word, 14))
    elif op == OPCODES["SETP"]:
        body = "SETP.%s P%d, %s, %s" % (CONDNAMES[funct & 7], rd & 3, r(rs1), r(rs2))
    elif op == OPCODES["SETPI"]:
        body = "SETP.%s P%d, %s, %d" % (CONDNAMES[rs2 & 7], rd & 3, r(rs1), sext(word, 10))
    elif name in ("LDG", "LDS"):
        body = "%s %s, %s" % (name, r(rd), mem(rs1, sext(word, 14)))
    else:  # STG / STS
        body = "%s %s, %s" % (name, mem(rs1, sext(word, 14)), r(rd))
    return pre + body


# ----------------------------------------------------------------------------
# CLI
# ----------------------------------------------------------------------------

VECADD = """
    S_TID   R1
    S_BID   R2
    S_NTID  R3
    MUL     R4, R2, R3
    ADD     R4, R4, R1
    S_ARG   R5, 3
    SETP.GE P0, R4, R5
    @P0 EXIT
    SHL     R4, R4, 2
    S_ARG   R6, 0
    S_ARG   R7, 1
    S_ARG   R8, 2
    ADD     R6, R6, R4
    ADD     R7, R7, R4
    ADD     R8, R8, R4
    LDG     R9, [R6]
    LDG     R10, [R7]
    ADD     R11, R9, R10
    STG     [R8], R11
    EXIT
"""

# Reference words produced by the C++ assembler (assembleKernel) for VECADD.
VECADD_WORDS = [
    0x01040000, 0x01080001, 0x010C0002, 0x02908C00, 0x02110400, 0x01140007,
    0x08011403, 0x80400000, 0x07510002, 0x01180004, 0x011C0005, 0x01200006,
    0x02199000, 0x021DD000, 0x02221000, 0x0A258000, 0x0A29C000, 0x022E6800,
    0x0A6E0000, 0x00400000,
]


def selftest():
    words = [w for w, _, _ in assemble(VECADD)]
    assert words == VECADD_WORDS, "encoder mismatch against C++ reference"
    for w in words:
        again = [x for x, _, _ in assemble(disassemble(w))]
        assert again == [w], "round trip failed for 0x%08x" % w
    assert disassemble(enc_i(OPCODES["LDG"], 5, 4, -4)) == "LDG R5, [R4-4]"
    assert disassemble(enc_p(1, 3, CONDS["LTU"], -1, guard(True, True, 2))) == \
        "@!P2 SETP.LTU P1, R3, -1"
    print("selftest OK (%d words)" % len(words))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("source", nargs="?", help="kernel source file (- for stdin)")
    ap.add_argument("-f", "--format", choices=("word", "hex", "c"), default="word",
                    help="output format (default: RISC-V .word directives)")
    ap.add_argument("-d", "--disassemble", nargs="+", metavar="WORD",
                    help="disassemble the given instruction words")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        selftest()
        return 0
    if args.disassemble:
        for i, tok in enumerate(args.disassemble):
            w = int(tok, 0) & 0xFFFFFFFF
            print("%3d  0x%08x  %s" % (i, w, disassemble(w)))
        return 0
    if not args.source:
        ap.print_help()
        return 1
    src = sys.stdin.read() if args.source == "-" else open(args.source).read()
    try:
        prog = assemble(src)
    except AsmError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    if args.format == "hex":
        for w, _, _ in prog:
            print("%08x" % w)
    elif args.format == "c":
        print("static const unsigned int kernel[] = {")
        for i, (w, _, text) in enumerate(prog):
            print("    0x%08x, /* %2d: %s */" % (w, i, disassemble(w)))
        print("};")
    else:
        for i, (w, _, text) in enumerate(prog):
            print("    .word 0x%08x    # %2d: %s" % (w, i, disassemble(w)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
