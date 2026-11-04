# Exercise (docs/guide/zh|en/10-npu.md, exercises 2 and 3): batch two
# descriptors behind ONE doorbell and chain two "layers":
#     slot 0: MATMUL  C = A x B          flags = NO_IRQ (no DESC_DONE)
#     slot 1: RELU    C = max(0, C)      in place, flags = 0
# then poll HEAD until it reaches 2 and print the counters and IRQ_STATUS.
#
# 练习（10-npu.md 练习 2、3）：一次门铃提交两个描述符，串起两"层"：
# 槽 0 做 MATMUL（置 NO_IRQ），槽 1 对结果原地做 RELU；轮询 HEAD 到 2。
# 先在 I/O 标签页实例化 "Tensor Unit (NPU)"（默认参数 4x4，DMA 1 字/周期）。
#
# Expected / 预期: IRQ_STATUS = 3 (DESC_DONE | QUEUE_EMPTY, only from slot 1),
# MACs = 64, result equals EXP (relu of the 4x4 product).

.data
A:        .byte   1,  2,  3,  4
          .byte  -1,  0,  2,  1
          .byte   3, -2,  1,  0
          .byte   0,  1, -1,  2
B:        .byte   2,  0,  1, -1
          .byte   1,  1,  0,  2
          .byte   0, -1,  1,  1
          .byte   3,  2, -2,  0
# relu(A x B), computed by hand in 10-npu.md
EXP:      .word  16, 7, 0, 6
          .word   1, 0, 0, 3
          .word   4, 0, 4, 0
          .word   7, 6, 0, 1
C:        .zero 64
ring:     .zero 128                    # 4 slots x 32 bytes

str_busy: .string "NPU busy cycles: "
str_mac:  .string "\nNPU MACs:        "
str_irq:  .string "\nIRQ_STATUS:      "
str_ok:   .string "\nResult: OK\n"
str_bad:  .string "\nResult: MISMATCH\n"

.text
main:
        # ---- configure the ring (TAIL/HEAD are 0 after reset) ----
        la   t0, ring
        li   t1, TENSOR_UNIT_0_QUEUE_BASE
        sw   t0, 0(t1)
        li   t1, TENSOR_UNIT_0_QUEUE_SIZE
        li   t2, 4
        sw   t2, 0(t1)

        # ---- slot 0: MATMUL with NO_IRQ ----
        la   t0, ring
        li   t1, TENSOR_UNIT_0_OP_MATMUL
        sw   t1, TENSOR_UNIT_0_DESC_OPCODE(t0)
        la   t1, A
        sw   t1, TENSOR_UNIT_0_DESC_SRCA(t0)
        la   t1, B
        sw   t1, TENSOR_UNIT_0_DESC_SRCB(t0)
        la   t1, C
        sw   t1, TENSOR_UNIT_0_DESC_DST(t0)
        li   t1, 4
        sw   t1, TENSOR_UNIT_0_DESC_M(t0)
        sw   t1, TENSOR_UNIT_0_DESC_N(t0)
        sw   t1, TENSOR_UNIT_0_DESC_K(t0)
        li   t1, TENSOR_UNIT_0_FLAG_NO_IRQ
        sw   t1, TENSOR_UNIT_0_DESC_FLAGS(t0)

        # ---- slot 1: RELU in place on C (M*N = 16 int32) ----
        addi t0, t0, 32
        li   t1, TENSOR_UNIT_0_OP_RELU
        sw   t1, TENSOR_UNIT_0_DESC_OPCODE(t0)
        la   t1, C
        sw   t1, TENSOR_UNIT_0_DESC_SRCA(t0)
        sw   zero, TENSOR_UNIT_0_DESC_SRCB(t0)
        sw   t1, TENSOR_UNIT_0_DESC_DST(t0)
        li   t1, 4
        sw   t1, TENSOR_UNIT_0_DESC_M(t0)
        sw   t1, TENSOR_UNIT_0_DESC_N(t0)
        sw   zero, TENSOR_UNIT_0_DESC_K(t0)
        sw   zero, TENSOR_UNIT_0_DESC_FLAGS(t0)

        # ---- one doorbell for both: TAIL = 2 ----
        li   t3, TENSOR_UNIT_0_BUSY_CYCLES
        lw   s3, 0(t3)
        li   t1, TENSOR_UNIT_0_TAIL
        li   t2, 2
        sw   t2, 0(t1)

        li   t1, TENSOR_UNIT_0_HEAD
poll:
        lw   t4, 0(t1)
        bne  t4, t2, poll

        # ---- report ----
        la   a0, str_busy
        li   a7, 4
        ecall
        lw   a0, 0(t3)
        sub  a0, a0, s3
        li   a7, 1
        ecall
        la   a0, str_mac
        li   a7, 4
        ecall
        li   t1, TENSOR_UNIT_0_MACS
        lw   a0, 0(t1)
        li   a7, 1
        ecall
        la   a0, str_irq
        li   a7, 4
        ecall
        li   t1, TENSOR_UNIT_0_IRQ_STATUS
        lw   a0, 0(t1)
        li   a7, 1
        ecall
        li   t2, TENSOR_UNIT_0_IRQ_DESC_DONE
        ori  t2, t2, TENSOR_UNIT_0_IRQ_QUEUE_EMPTY
        sw   t2, 0(t1)                 # W1C acknowledge

        # ---- verify ----
        la   t0, C
        la   t1, EXP
        li   t2, 16
cmp:
        lw   t4, 0(t0)
        lw   t5, 0(t1)
        bne  t4, t5, bad
        addi t0, t0, 4
        addi t1, t1, 4
        addi t2, t2, -1
        bnez t2, cmp
        la   a0, str_ok
        li   a7, 4
        ecall
        j    exit
bad:
        la   a0, str_bad
        li   a7, 4
        ecall
exit:
        li   a7, 10
        ecall
