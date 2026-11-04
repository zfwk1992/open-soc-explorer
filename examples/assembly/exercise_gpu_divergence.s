# Exercise (docs/guide/zh|en/09-gpu.md, exercise 3): branch vs predication.
# Two kernels compute X[i] = |X[i]| for 8 threads (one warp). The first uses
# a branch (diverges when signs are mixed), the second uses a guarded
# instruction (never diverges). The host prints CYCLES, STATS_INSTRS and
# STATS_DIVERGE for both.
#
# 练习（09-gpu.md 练习 3）：分支 vs 谓词。两个 kernel 都计算 |X[i]|；
# 第一个用分支（符号混杂时发散），第二个用带守卫的指令（不发散）。
# 先在 I/O 标签页实例化 "SIMT GPU"（默认参数）。
#
# kernel_branch:                     kernel_pred:
#  0 S_TID   R1                       0 S_TID   R1
#  1 S_ARG   R6, 0                    1 S_ARG   R6, 0
#  2 SHL     R4, R1, 2                2 SHL     R4, R1, 2
#  3 ADD     R6, R6, R4               3 ADD     R6, R6, R4
#  4 LDG     R9, [R6]                 4 LDG     R9, [R6]
#  5 SETP.LT P0, R9, 0                5 SETP.LT P0, R9, 0
#  6 @!P0 BRA store                   6 @P0 SUB R9, R0, R9
#  7 SUB     R9, R0, R9               7 STG     [R6], R9
#  8 store: STG [R6], R9              8 EXIT
#  9 EXIT

.data
X1:     .word 5, -3, 7, -1, 0, -8, 2, -6
X2:     .word 5, -3, 7, -1, 0, -8, 2, -6
EXP:    .word 5, 3, 7, 1, 0, 8, 2, 6

kernel_branch:
    .word 0x01040000    #  0: S_TID R1
    .word 0x01180004    #  1: S_ARG R6, 0
    .word 0x07504002    #  2: SHLI R4, R1, 2
    .word 0x02199000    #  3: ADD R6, R6, R4
    .word 0x0a258000    #  4: LDG R9, [R6]
    .word 0x08424800    #  5: SETP.LT P0, R9, 0
    .word 0xc0c00008    #  6: @!P0 BRA 8
    .word 0x02642400    #  7: SUB R9, R0, R9
    .word 0x0a658000    #  8: STG [R6], R9
    .word 0x00400000    #  9: EXIT
kernel_pred:
    .word 0x01040000    #  0: S_TID R1
    .word 0x01180004    #  1: S_ARG R6, 0
    .word 0x07504002    #  2: SHLI R4, R1, 2
    .word 0x02199000    #  3: ADD R6, R6, R4
    .word 0x0a258000    #  4: LDG R9, [R6]
    .word 0x08424800    #  5: SETP.LT P0, R9, 0
    .word 0x82642400    #  6: @P0 SUB R9, R0, R9
    .word 0x0a658000    #  7: STG [R6], R9
    .word 0x00400000    #  8: EXIT

str_br:   .string "branch kernel:    "
str_pr:   .string "predicate kernel: "
str_cyc:  .string "cycles="
str_ins:  .string " instrs="
str_div:  .string " divergences="
str_ok:   .string "  result OK\n"
str_bad:  .string "  MISMATCH\n"

.text
main:
    li   s0, GPU_0_BASE
    la   a0, str_br
    li   a7, 4
    ecall
    la   a0, kernel_branch
    la   a1, X1
    jal  ra, launch_and_report

    la   a0, str_pr
    li   a7, 4
    ecall
    la   a0, kernel_pred
    la   a1, X2
    jal  ra, launch_and_report

    li   a7, 10
    ecall

# a0 = kernel address, a1 = data address (8 words). Uses s0 = GPU_0_BASE.
launch_and_report:
    mv   s1, a1
    sw   a0, GPU_0_KERNEL_PC_OFFSET(s0)
    li   t0, 1
    sw   t0, GPU_0_GRID_X_OFFSET(s0)
    li   t0, 8
    sw   t0, GPU_0_BLOCK_X_OFFSET(s0)
    sw   a1, GPU_0_ARG0_OFFSET(s0)
    li   t0, GPU_0_CTRL_START
    sw   t0, GPU_0_CTRL_OFFSET(s0)
    li   t1, GPU_0_STATUS_DONE
lr_wait:
    lw   t0, GPU_0_STATUS_OFFSET(s0)
    and  t2, t0, t1
    beqz t2, lr_wait
    sw   t1, GPU_0_STATUS_OFFSET(s0)   # W1C

    la   a0, str_cyc
    li   a7, 4
    ecall
    lw   a0, GPU_0_CYCLES_OFFSET(s0)
    li   a7, 1
    ecall
    la   a0, str_ins
    li   a7, 4
    ecall
    lw   a0, GPU_0_STATS_INSTRS_OFFSET(s0)
    li   a7, 1
    ecall
    la   a0, str_div
    li   a7, 4
    ecall
    lw   a0, GPU_0_STATS_DIVERGE_OFFSET(s0)
    li   a7, 1
    ecall

    la   t0, EXP
    li   t3, 8
lr_check:
    lw   t4, 0(s1)
    lw   t5, 0(t0)
    bne  t4, t5, lr_bad
    addi s1, s1, 4
    addi t0, t0, 4
    addi t3, t3, -1
    bnez t3, lr_check
    la   a0, str_ok
    li   a7, 4
    ecall
    ret
lr_bad:
    la   a0, str_bad
    li   a7, 4
    ecall
    ret
