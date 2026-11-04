# Exercise (docs/guide/zh|en/09-gpu.md, exercise 1): integer SAXPY on the
# "SIMT GPU" peripheral.
#     Y[i] = a * X[i] + Y[i]        for i in [0, N)
#
# 练习（09-gpu.md 练习 1）：在 SIMT GPU 上做整数 SAXPY。
# Instantiate a "SIMT GPU" peripheral in the I/O tab first (default
# parameters), then assemble and run. / 先在 I/O 标签页实例化 "SIMT GPU"。
#
# Kernel source (assemble with: python tools/gpu_asm.py saxpy.gpu):
#      0  S_TID   R1
#      1  S_BID   R2
#      2  S_NTID  R3
#      3  MUL     R4, R2, R3
#      4  ADD     R4, R4, R1          # i
#      5  S_ARG   R5, 3               # N
#      6  SETP.GE P0, R4, R5
#      7  @P0 EXIT                    # tail threads leave
#      8  SHL     R4, R4, 2           # byte offset
#      9  S_ARG   R6, 0               # &X
#     10  S_ARG   R7, 1               # &Y
#     11  S_ARG   R8, 2               # a  (a value, not a pointer)
#     12  ADD     R6, R6, R4
#     13  ADD     R7, R7, R4
#     14  LDG     R9, [R6]            # X[i]
#     15  LDG     R10, [R7]           # Y[i]
#     16  FMA     R11, R8, R9, R10    # a*X[i] + Y[i]
#     17  STG     [R7], R11
#     18  EXIT

.data
N:      .word 10
X:      .word 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
Y:      .word 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000

kernel:
    .word 0x01040000    #  0: S_TID R1
    .word 0x01080001    #  1: S_BID R2
    .word 0x010c0002    #  2: S_NTID R3
    .word 0x02908c00    #  3: MUL R4, R2, R3
    .word 0x02110400    #  4: ADD R4, R4, R1
    .word 0x01140007    #  5: S_ARG R5, 3
    .word 0x08011403    #  6: SETP.GE P0, R4, R5
    .word 0x80400000    #  7: @P0 EXIT
    .word 0x07510002    #  8: SHLI R4, R4, 2
    .word 0x01180004    #  9: S_ARG R6, 0
    .word 0x011c0005    # 10: S_ARG R7, 1
    .word 0x01200006    # 11: S_ARG R8, 2
    .word 0x02199000    # 12: ADD R6, R6, R4
    .word 0x021dd000    # 13: ADD R7, R7, R4
    .word 0x0a258000    # 14: LDG R9, [R6]
    .word 0x0a29c000    # 15: LDG R10, [R7]
    .word 0x02ee2680    # 16: FMA R11, R8, R9, R10
    .word 0x0a6dc000    # 17: STG [R7], R11
    .word 0x00400000    # 18: EXIT

str_cyc:  .string "SAXPY done in "
str_cyc2: .string " GPU cycles, "
str_txn:  .string " memory transactions\n"
str_ok:   .string "Result OK: Y[i] = 3*X[i] + 1000\n"
str_bad:  .string "MISMATCH at i="
str_err:  .string "GPU reported an error\n"

.text
main:
    li   s0, GPU_0_BASE
    la   t0, kernel
    sw   t0, GPU_0_KERNEL_PC_OFFSET(s0)
    lw   s1, N                         # s1 = N
    li   s2, 8                         # threads per block
    sw   s2, GPU_0_BLOCK_X_OFFSET(s0)
    add  t0, s1, s2                    # grid = ceil(N / 8)
    addi t0, t0, -1
    divu t0, t0, s2
    sw   t0, GPU_0_GRID_X_OFFSET(s0)
    la   t0, X
    sw   t0, GPU_0_ARG0_OFFSET(s0)
    la   t0, Y
    sw   t0, GPU_0_ARG1_OFFSET(s0)
    li   t0, 3                         # a = 3 (passed by value)
    sw   t0, GPU_0_ARG2_OFFSET(s0)
    sw   s1, GPU_0_ARG3_OFFSET(s0)

    li   t0, GPU_0_CTRL_START          # doorbell
    sw   t0, GPU_0_CTRL_OFFSET(s0)
    li   t1, GPU_0_STATUS_DONE
wait:
    lw   t0, GPU_0_STATUS_OFFSET(s0)
    and  t2, t0, t1
    beqz t2, wait
    andi t2, t0, GPU_0_STATUS_ERROR
    bnez t2, gpu_error
    sw   t1, GPU_0_STATUS_OFFSET(s0)   # W1C: acknowledge DONE

    la   a0, str_cyc
    li   a7, 4
    ecall
    lw   a0, GPU_0_CYCLES_OFFSET(s0)
    li   a7, 1
    ecall
    la   a0, str_cyc2
    li   a7, 4
    ecall
    lw   a0, GPU_0_STATS_MEMTXN_OFFSET(s0)
    li   a7, 1
    ecall
    la   a0, str_txn
    li   a7, 4
    ecall

    la   t0, X
    la   t1, Y
    mv   t3, zero
check:
    bge  t3, s1, ok
    lw   t4, 0(t0)
    slli t5, t4, 1
    add  t4, t4, t5                    # 3 * X[i]
    addi t4, t4, 1000
    lw   t5, 0(t1)
    bne  t4, t5, bad
    addi t0, t0, 4
    addi t1, t1, 4
    addi t3, t3, 1
    j    check
ok:
    la   a0, str_ok
    li   a7, 4
    ecall
    j    exit
bad:
    la   a0, str_bad
    li   a7, 4
    ecall
    mv   a0, t3
    li   a7, 1
    ecall
    j    exit
gpu_error:
    la   a0, str_err
    li   a7, 4
    ecall
exit:
    li   a7, 10
    ecall
