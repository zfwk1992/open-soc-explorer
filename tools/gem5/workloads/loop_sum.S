# SPDX-License-Identifier: MIT
# Open SoC Explorer gem5 workload: the Mission 3 loop (sum 10 + 9 + ... + 1)
# as a freestanding RISC-V Linux program for gem5 syscall emulation.
# The result (55) is returned as the exit status.
    .text
    .globl _start
_start:
    addi a0, x0, 0          # sum
    addi a1, x0, 10         # counter
loop:
    add  a0, a0, a1
    addi a1, a1, -1
    bne  a1, x0, loop
    addi a2, x0, 1          # code after the loop
    addi a3, x0, 2
    addi a7, x0, 93         # Linux exit(a0)
    ecall
