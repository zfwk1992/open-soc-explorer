# GPU vector add (SIMT GPU peripheral demo)
#
# This program launches a kernel on the "SIMT GPU" peripheral that computes
#     C[i] = A[i] + B[i]      for i in [0, N)
# and then verifies the result on the RISC-V host CPU.
#
# To run this program, instantiate a "SIMT GPU" peripheral in the "I/O" tab
# (default parameters: 2 SMs x 4 warps x 8 lanes). Switch to the I/O tab and
# press Run; the widget shows each warp's pc, current instruction and the
# per-lane active mask while the kernel executes. The CPU busy-waits on
# GPU_0_STATUS, so the GPU really works concurrently with the CPU.
#
# Launch protocol (see docs/arch/gpu.md):
#   1. put the kernel machine code in memory (kernel: below),
#   2. write GPU_0_KERNEL_PC, GPU_0_GRID_X, GPU_0_BLOCK_X, GPU_0_ARG0..3,
#   3. write GPU_0_CTRL_START to GPU_0_CTRL,
#   4. poll GPU_0_STATUS until GPU_0_STATUS_DONE is set,
#   5. write GPU_0_STATUS_DONE back to GPU_0_STATUS to clear it.
#
# N = 21 is deliberately not a multiple of the 16-thread block: the last block
# has threads whose index is >= N, and they are predicated off by the guarded
# EXIT ("@P0 EXIT"). Watch the red (exited) lanes in the widget.

.data
N:      .word 21
A:      .word  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
B:      .word  0,100,200,300,400,500,600,700,800,900,1000,1100,1200,1300,1400,1500,1600,1700,1800,1900,2000
C:      .zero 84                       # 21 words of output

# Kernel machine code, assembled with:  python tools/gpu_asm.py vecadd.gpu
# Kernel source (threadIdx -> R1, blockIdx -> R2, blockDim -> R3,
#                i = blockIdx*blockDim + threadIdx):
#
#      0  S_TID   R1              # R1 = threadIdx.x
#      1  S_BID   R2              # R2 = blockIdx.x
#      2  S_NTID  R3              # R3 = blockDim.x
#      3  MUL     R4, R2, R3      # R4 = blockIdx * blockDim
#      4  ADD     R4, R4, R1      # R4 = i
#      5  S_ARG   R5, 3           # R5 = N        (ARG3)
#      6  SETP.GE P0, R4, R5      # P0 = (i >= N)
#      7  @P0 EXIT                # tail threads leave (predicated, no branch)
#      8  SHL     R4, R4, 2       # R4 = i * 4   (byte offset)
#      9  S_ARG   R6, 0           # R6 = &A      (ARG0)
#     10  S_ARG   R7, 1           # R7 = &B      (ARG1)
#     11  S_ARG   R8, 2           # R8 = &C      (ARG2)
#     12  ADD     R6, R6, R4      # &A[i]
#     13  ADD     R7, R7, R4      # &B[i]
#     14  ADD     R8, R8, R4      # &C[i]
#     15  LDG     R9, [R6]        # A[i]   (coalesced: 8 lanes -> 1 x 32 B)
#     16  LDG     R10, [R7]       # B[i]
#     17  ADD     R11, R9, R10
#     18  STG     [R8], R11       # C[i] = A[i] + B[i]
#     19  EXIT
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
    .word 0x02221000    # 14: ADD R8, R8, R4
    .word 0x0a258000    # 15: LDG R9, [R6]
    .word 0x0a29c000    # 16: LDG R10, [R7]
    .word 0x022e6800    # 17: ADD R11, R9, R10
    .word 0x0a6e0000    # 18: STG [R8], R11
    .word 0x00400000    # 19: EXIT

str_launch: .string "Launching vecadd kernel: grid="
str_block:  .string " block="
str_wait:   .string "\nwaiting for GPU"
str_cycles: .string "\nGPU done in "
str_cyc2:   .string " GPU cycles, "
str_instr:  .string " warp instructions, "
str_txn:    .string " memory transactions\n"
str_ok:     .string "Result OK: C[i] = A[i] + B[i] for all i\n"
str_bad:    .string "MISMATCH at i="
str_nl:     .string "\n"
str_err:    .string "GPU reported an error (block too large for an SM?)\n"

.text
main:
    # ---- 1. fill in the launch registers ------------------------------------
    li   s0, GPU_0_BASE
    la   t0, kernel
    sw   t0, GPU_0_KERNEL_PC_OFFSET(s0)

    lw   s1, N                         # s1 = N
    li   s2, 16                        # s2 = threads per block
    sw   s2, GPU_0_BLOCK_X_OFFSET(s0)
    add  t0, s1, s2                    # grid = ceil(N / block)
    addi t0, t0, -1
    divu s3, t0, s2
    sw   s3, GPU_0_GRID_X_OFFSET(s0)

    la   t0, A
    sw   t0, GPU_0_ARG0_OFFSET(s0)
    la   t0, B
    sw   t0, GPU_0_ARG1_OFFSET(s0)
    la   t0, C
    sw   t0, GPU_0_ARG2_OFFSET(s0)
    sw   s1, GPU_0_ARG3_OFFSET(s0)

    la   a0, str_launch
    li   a7, 4
    ecall
    mv   a0, s3
    li   a7, 1
    ecall
    la   a0, str_block
    li   a7, 4
    ecall
    mv   a0, s2
    li   a7, 1
    ecall
    la   a0, str_wait
    li   a7, 4
    ecall

    # ---- 2. doorbell: start the kernel ---------------------------------------
    li   t0, GPU_0_CTRL_START
    sw   t0, GPU_0_CTRL_OFFSET(s0)

    # ---- 3. poll STATUS until DONE -------------------------------------------
    li   t1, GPU_0_STATUS_DONE
wait:
    lw   t0, GPU_0_STATUS_OFFSET(s0)
    and  t2, t0, t1
    beqz t2, wait

    andi t2, t0, GPU_0_STATUS_ERROR
    bnez t2, gpu_error

    # ---- 4. acknowledge DONE (write-1-to-clear) and print statistics --------
    sw   t1, GPU_0_STATUS_OFFSET(s0)

    la   a0, str_cycles
    li   a7, 4
    ecall
    lw   a0, GPU_0_CYCLES_OFFSET(s0)
    li   a7, 1
    ecall
    la   a0, str_cyc2
    li   a7, 4
    ecall
    lw   a0, GPU_0_STATS_INSTRS_OFFSET(s0)
    li   a7, 1
    ecall
    la   a0, str_instr
    li   a7, 4
    ecall
    lw   a0, GPU_0_STATS_MEMTXN_OFFSET(s0)
    li   a7, 1
    ecall
    la   a0, str_txn
    li   a7, 4
    ecall

    # ---- 5. verify on the CPU ------------------------------------------------
    la   t0, A
    la   t1, B
    la   t2, C
    mv   t3, zero                      # i
check:
    bge  t3, s1, all_ok
    lw   t4, 0(t0)
    lw   t5, 0(t1)
    add  t4, t4, t5
    lw   t5, 0(t2)
    bne  t4, t5, mismatch
    addi t0, t0, 4
    addi t1, t1, 4
    addi t2, t2, 4
    addi t3, t3, 1
    j    check

all_ok:
    la   a0, str_ok
    li   a7, 4
    ecall
    j    exit

mismatch:
    la   a0, str_bad
    li   a7, 4
    ecall
    mv   a0, t3
    li   a7, 1
    ecall
    la   a0, str_nl
    li   a7, 4
    ecall
    j    exit

gpu_error:
    la   a0, str_err
    li   a7, 4
    ecall

exit:
    li   a7, 10
    ecall
