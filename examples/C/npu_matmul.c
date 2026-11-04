#include "ripes_system.h"

/* NPU matrix multiply
 * This program offloads an int8 matrix multiplication to the "Tensor Unit"
 * peripheral, waits for it by polling, checks the result against a CPU
 * implementation and prints cycle counts for both.
 *
 * To run this program, make sure that you have instantiated a "Tensor Unit
 * (NPU)" peripheral in the "I/O" tab, and compile with "-nostdlib" (see
 * docs/c_programming.md). We provide our own startup code below.
 *
 * Interaction model (see docs/arch/tensor_cpu.md):
 *   1. CPU writes a 32-byte descriptor into a ring buffer in memory.
 *   2. CPU writes TAIL (the doorbell) with the index of the next free slot.
 *   3. The unit fetches the descriptor, DMAs A and B in, computes on its
 *      systolic array, DMAs C out, and advances HEAD.
 *   4. CPU polls HEAD (or STATUS / IRQ_STATUS) until the work is done.
 */

asm("li sp, 0x100000"); // SP set to 1 MB
asm("jal main");        // call main
asm("li a7, 10");       // prepare ecall exit
asm("ecall");           // now your simulator should stop

#define M 6 // rows of A and C
#define K 5 // columns of A == rows of B
#define N 4 // columns of B and C

#define RING_SLOTS 4

/* 32-bit register access helper: the TENSOR_UNIT_0_* symbols are absolute
 * addresses exported by the peripheral (see the "I/O" tab). */
#define REG(addr) (*(volatile unsigned *)(addr))

/* Descriptor word indices (byte offset / 4). */
#define D_OPCODE 0
#define D_SRCA 1
#define D_SRCB 2
#define D_DST 3
#define D_M 4
#define D_N 5
#define D_K 6
#define D_FLAGS 7

static signed char A[M][K] = {
    {1, 2, 3, 4, 5},     {-1, 0, 2, 1, -3},  {3, -2, 1, 0, 7},
    {0, 1, -1, 2, -128}, {9, 8, 7, 6, 5},    {-5, 4, -3, 2, -1},
};
static signed char B[K][N] = {
    {2, 0, 1, -1}, {1, 1, 0, 2}, {0, -1, 1, 1}, {3, 2, -2, 0}, {127, -7, 5, 3},
};
static int C_cpu[M][N];
static int C_npu[M][N];

/* The descriptor ring lives in ordinary memory; the unit reads it by DMA. */
static volatile unsigned ring[RING_SLOTS][8] __attribute__((aligned(32)));

static void print_str(const char *s) {
    register const char *a0 asm("a0") = s;
    register unsigned a7 asm("a7") = 4;
    asm volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static void print_int(int v) {
    register int a0 asm("a0") = v;
    register unsigned a7 asm("a7") = 1;
    asm volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static void print_line(const char *label, int value) {
    print_str(label);
    print_int(value);
    print_str("\n");
}

static void cpu_matmul(void) {
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            int acc = 0;
            for (int k = 0; k < K; k++) {
                acc += A[m][k] * B[k][n];
            }
            C_cpu[m][n] = acc;
        }
    }
}

/* Fills the slot at `tail`, publishes it with the doorbell and returns the new
 * tail index. */
static unsigned submit(unsigned tail, unsigned opcode, const void *srcA,
                       const void *srcB, void *dst, unsigned m, unsigned n,
                       unsigned k, unsigned flags) {
    volatile unsigned *d = ring[tail];
    d[D_OPCODE] = opcode;
    d[D_SRCA] = (unsigned)srcA;
    d[D_SRCB] = (unsigned)srcB;
    d[D_DST] = (unsigned)dst;
    d[D_M] = m;
    d[D_N] = n;
    d[D_K] = k;
    d[D_FLAGS] = flags;

    /* On a real SoC the descriptor and the input matrices would have to be
     * flushed from the CPU caches to memory, followed by a fence, before the
     * doorbell is rung - otherwise the DMA engine may read stale data. Ripes'
     * cache simulator is a model layered over a single coherent memory, so
     * nothing is needed here; `volatile` already keeps the compiler from
     * reordering the stores past the doorbell write below. */
    tail = (tail + 1) % RING_SLOTS;
    REG(TENSOR_UNIT_0_TAIL) = tail; /* doorbell */
    return tail;
}

/* Spins until the unit has consumed every slot up to `tail`. Returns the
 * number of polls. */
static unsigned wait_head(unsigned tail) {
    unsigned polls = 0;
    while (REG(TENSOR_UNIT_0_HEAD) != tail) {
        polls++;
    }
    /* On a real SoC: invalidate the cache lines of the output matrix here,
     * so the CPU does not read stale copies. Not needed in Ripes. */
    return polls;
}

int main() {
    print_str("Tensor Unit matmul demo (int8 ");
    print_int(M);
    print_str("x");
    print_int(K);
    print_str(" * ");
    print_int(K);
    print_str("x");
    print_int(N);
    print_str(", systolic array N=");
    print_int(REG(TENSOR_UNIT_0_ARRAY_N));
    print_str(")\n");

    /* ---- 1. CPU reference, timed with the unit's free-running counter ---- */
    unsigned t0 = REG(TENSOR_UNIT_0_CYCLES);
    cpu_matmul();
    unsigned t1 = REG(TENSOR_UNIT_0_CYCLES);
    print_line("CPU matmul cycles:   ", t1 - t0);

    /* ---- 2. Configure the ring and offload ---- */
    REG(TENSOR_UNIT_0_QUEUE_BASE) = (unsigned)ring;
    REG(TENSOR_UNIT_0_QUEUE_SIZE) = RING_SLOTS;
    REG(TENSOR_UNIT_0_IRQ_EN) = 0; /* we poll */
    unsigned tail = REG(TENSOR_UNIT_0_TAIL);

    unsigned busy0 = REG(TENSOR_UNIT_0_BUSY_CYCLES);
    unsigned dma0 = REG(TENSOR_UNIT_0_DMA_BYTES);
    unsigned mac0 = REG(TENSOR_UNIT_0_MACS);
    unsigned t2 = REG(TENSOR_UNIT_0_CYCLES);

    tail = submit(tail, TENSOR_UNIT_0_OP_MATMUL, A, B, C_npu, M, N, K, 0);
    unsigned polls = wait_head(tail);

    unsigned t3 = REG(TENSOR_UNIT_0_CYCLES);
    print_line("NPU wall cycles:     ", t3 - t2);
    print_line("NPU busy cycles:     ", REG(TENSOR_UNIT_0_BUSY_CYCLES) - busy0);
    print_line("NPU DMA bytes:       ", REG(TENSOR_UNIT_0_DMA_BYTES) - dma0);
    print_line("NPU MACs:            ", REG(TENSOR_UNIT_0_MACS) - mac0);
    print_line("CPU polls:           ", polls);

    /* ---- 3. Verify ---- */
    int errors = 0;
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            if (C_cpu[m][n] != C_npu[m][n]) {
                errors++;
            }
        }
    }
    print_str(errors == 0 ? "MATMUL result: OK\n" : "MATMUL result: MISMATCH\n");

    /* ---- 4. A second descriptor: ReLU in place on the NPU result ---- */
    tail = submit(tail, TENSOR_UNIT_0_OP_RELU, C_npu, 0, C_npu, M, N, 0, 0);
    wait_head(tail);
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            int expected = C_cpu[m][n] < 0 ? 0 : C_cpu[m][n];
            if (C_npu[m][n] != expected) {
                errors++;
            }
        }
    }
    print_str(errors == 0 ? "RELU result:   OK\n" : "RELU result:   MISMATCH\n");

    print_str("IRQ_STATUS (sticky, write 1 to clear): ");
    print_int(REG(TENSOR_UNIT_0_IRQ_STATUS));
    REG(TENSOR_UNIT_0_IRQ_STATUS) = TENSOR_UNIT_0_IRQ_DESC_DONE |
                                    TENSOR_UNIT_0_IRQ_QUEUE_EMPTY;
    print_str(" -> ");
    print_int(REG(TENSOR_UNIT_0_IRQ_STATUS));
    print_str("\n");

    return errors;
}
