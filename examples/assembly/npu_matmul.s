# NPU matrix multiply
# This program offloads a 4x4 int8 matrix multiplication to the Tensor Unit
# peripheral, waits for it by polling HEAD, checks the result against a CPU
# implementation and prints the cycle counts of both.
#
# To run this program, make sure that you have instantiated a Tensor Unit
# (NPU) peripheral in the I/O tab. All TENSOR_UNIT_0_* symbols are exported
# by that peripheral (absolute register addresses, opcodes and bit masks).
#
# Interaction model (see docs/arch/tensor_cpu.md):
#   1. Write a 32-byte descriptor {opcode, srcA, srcB, dst, M, N, K, flags}
#      into slot HEAD..TAIL of the ring buffer in memory.
#   2. Write TAIL (the doorbell) with the index of the next free slot.
#   3. The unit fetches the descriptor, DMAs A and B in, computes on its
#      systolic array, DMAs C out and advances HEAD.
#   4. Poll HEAD until it equals the TAIL we published.

.data
# A: 4x4 int8, row-major
A:        .byte   1,  2,  3,  4
          .byte  -1,  0,  2,  1
          .byte   3, -2,  1,  0
          .byte   0,  1, -1,  2
# B: 4x4 int8, row-major
B:        .byte   2,  0,  1, -1
          .byte   1,  1,  0,  2
          .byte   0, -1,  1,  1
          .byte   3,  2, -2,  0
# C: 4x4 int32, row-major
C_cpu:    .zero 64
C_npu:    .zero 64
# Descriptor ring: 4 slots x 32 bytes, read by the unit's DMA engine.
ring:     .zero 128

str_hdr:  .string "Tensor Unit matmul demo (4x4x4 int8)\n"
str_cpu:  .string "CPU matmul cycles: "
str_wall: .string "\nNPU wall cycles:   "
str_busy: .string "\nNPU busy cycles:   "
str_dma:  .string "\nNPU DMA bytes:     "
str_mac:  .string "\nNPU MACs:          "
str_ok:   .string "\nResult: OK\n"
str_bad:  .string "\nResult: MISMATCH\n"

.text
main:
        la   a0, str_hdr
        li   a7, 4
        ecall

        # ---- 1. CPU reference: C_cpu = A x B, timed with CYCLES ----
        # CYCLES counts every processor cycle, so it doubles as a free-running
        # cycle counter for the CPU code as well.
        li   s0, TENSOR_UNIT_0_CYCLES
        lw   s1, 0(s0)                 # start
        la   a0, A
        la   a1, B
        la   a2, C_cpu
        jal  ra, cpu_matmul
        lw   s2, 0(s0)
        sub  s2, s2, s1                # s2 = CPU cycles

        # ---- 2. Configure the descriptor ring ----
        la   t0, ring
        li   t1, TENSOR_UNIT_0_QUEUE_BASE
        sw   t0, 0(t1)
        li   t1, TENSOR_UNIT_0_QUEUE_SIZE
        li   t2, 4
        sw   t2, 0(t1)
        li   t1, TENSOR_UNIT_0_IRQ_EN
        sw   zero, 0(t1)               # we poll, no interrupt

        # ---- 3. Fill descriptor slot 0 (TAIL is 0 after reset) ----
        la   t0, ring
        li   t1, TENSOR_UNIT_0_OP_MATMUL
        sw   t1, 0(t0)                 # opcode
        la   t1, A
        sw   t1, 4(t0)                 # srcA
        la   t1, B
        sw   t1, 8(t0)                 # srcB
        la   t1, C_npu
        sw   t1, 12(t0)                # dst
        li   t1, 4
        sw   t1, 16(t0)                # M
        sw   t1, 20(t0)                # N
        sw   t1, 24(t0)                # K
        sw   zero, 28(t0)              # flags
        # On a real SoC: flush the descriptor and A/B from the caches and
        # execute a fence here, before the doorbell. Ripes' memory is coherent
        # with the cache simulation, so nothing is needed.

        # ---- 4. Ring the doorbell: TAIL = 1 ----
        li   t3, TENSOR_UNIT_0_BUSY_CYCLES
        lw   s3, 0(t3)                 # busy cycles before
        lw   s1, 0(s0)                 # wall-clock start
        li   t1, TENSOR_UNIT_0_TAIL
        li   t2, 1
        sw   t2, 0(t1)

        # ---- 5. Poll HEAD until the unit has consumed slot 0 ----
        li   t1, TENSOR_UNIT_0_HEAD
poll:
        lw   t4, 0(t1)
        bne  t4, t2, poll

        lw   s4, 0(s0)
        sub  s4, s4, s1                # s4 = NPU wall cycles (incl. polling)
        lw   s5, 0(t3)
        sub  s5, s5, s3                # s5 = NPU busy cycles
        li   t1, TENSOR_UNIT_0_DMA_BYTES
        lw   s6, 0(t1)
        li   t1, TENSOR_UNIT_0_MACS
        lw   s7, 0(t1)
        # On a real SoC: invalidate the cache lines of C_npu here.

        # ---- 6. Print the statistics ----
        la   a0, str_cpu
        mv   a1, s2
        jal  ra, print_pair
        la   a0, str_wall
        mv   a1, s4
        jal  ra, print_pair
        la   a0, str_busy
        mv   a1, s5
        jal  ra, print_pair
        la   a0, str_dma
        mv   a1, s6
        jal  ra, print_pair
        la   a0, str_mac
        mv   a1, s7
        jal  ra, print_pair

        # ---- 7. Compare C_cpu and C_npu ----
        la   t0, C_cpu
        la   t1, C_npu
        li   t2, 16
cmp_loop:
        lw   t4, 0(t0)
        lw   t5, 0(t1)
        bne  t4, t5, mismatch
        addi t0, t0, 4
        addi t1, t1, 4
        addi t2, t2, -1
        bnez t2, cmp_loop
        la   a0, str_ok
        li   a7, 4
        ecall
        j    exit
mismatch:
        la   a0, str_bad
        li   a7, 4
        ecall
exit:
        li   a7, 10
        ecall

# --- cpu_matmul ---
# a0: A (4x4 int8), a1: B (4x4 int8), a2: C (4x4 int32)
cpu_matmul:
        li   t6, 4
        li   t0, 0                     # m
mm_row:
        li   t1, 0                     # n
mm_col:
        li   t2, 0                     # k
        li   t3, 0                     # acc
mm_k:
        slli t4, t0, 2                 # m*4
        add  t4, t4, t2                # m*4 + k
        add  t4, t4, a0
        lb   t4, 0(t4)                 # A[m][k]
        slli t5, t2, 2                 # k*4
        add  t5, t5, t1                # k*4 + n
        add  t5, t5, a1
        lb   t5, 0(t5)                 # B[k][n]
        mul  t4, t4, t5
        add  t3, t3, t4
        addi t2, t2, 1
        bne  t2, t6, mm_k
        slli t4, t0, 2                 # m*4
        add  t4, t4, t1                # m*4 + n
        slli t4, t4, 2                 # word offset -> byte offset
        add  t4, t4, a2
        sw   t3, 0(t4)                 # C[m][n] = acc
        addi t1, t1, 1
        bne  t1, t6, mm_col
        addi t0, t0, 1
        bne  t0, t6, mm_row
        ret

# --- print_pair ---
# a0: address of a label string, a1: integer to print after it
print_pair:
        mv   t0, a1
        li   a7, 4
        ecall
        mv   a0, t0
        li   a7, 1
        ecall
        ret
