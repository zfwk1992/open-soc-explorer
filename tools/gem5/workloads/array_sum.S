# SPDX-License-Identifier: MIT
# Open SoC Explorer gem5 workload: sum of a 2048-word (8 KiB) array, to
# exercise the cache hierarchy. The array is visited in STRIDE passes; pass k
# reads words k, k + STRIDE, k + 2 * STRIDE, ... Every build performs the
# same 2048 loads and computes the same sum:
#   STRIDE=16 (array_sum.elf):     consecutive loads are 64 bytes apart
#                                  (one cache line each)
#   STRIDE=1  (array_sum_seq.elf): one sequential pass
# Exit status: the sum modulo 256 (0).
#ifndef STRIDE
#define STRIDE 16
#endif
    .data
    .balign 64
array:
    .rept 2048
    .word 1
    .endr

    .text
    .globl _start
_start:
    la   t0, array          # base
    li   t1, 2048           # words
    li   t2, 0              # index
    li   a0, 0              # sum
    li   t4, STRIDE         # stride in words
outer:
    li   t5, 0              # start offset
pass:
    mv   t2, t5
inner:
    slli t3, t2, 2
    add  t3, t0, t3
    lw   t6, 0(t3)
    add  a0, a0, t6
    add  t2, t2, t4
    blt  t2, t1, inner
    addi t5, t5, 1
    blt  t5, t4, pass
    andi a0, a0, 255
    li   a7, 93             # Linux exit(a0)
    ecall
