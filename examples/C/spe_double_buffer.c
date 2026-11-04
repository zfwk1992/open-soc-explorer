/**
 * spe_double_buffer.c — 用纯软件模拟 Cell SPE 的「显式 DMA + 双缓冲」模式。
 * Software simulation of the Cell SPE "explicit DMA + double buffering" pattern.
 *
 * 背景 / Background
 *   在 PS3 的 Cell BE 中, 每个 SPE 只能直接访问自己的 256 KB Local Store (LS)。
 *   主存中的数据必须由程序员显式地通过 MFC 发起 DMA 搬进 LS, 算完再搬回去。
 *   DMA 是异步的: 发出命令后指令立即返回, 之后用 tag group 等待完成。双缓冲
 *   就是在计算第 i 块的同时预取第 i+1 块, 把 DMA 延迟藏在计算后面。
 *   On the Cell BE an SPU can only load/store its own 256 KB Local Store; main
 *   memory is reached exclusively through asynchronous MFC DMA commands that
 *   complete under a tag group. Double buffering prefetches chunk i+1 while
 *   chunk i is being computed.
 *
 *   本程序把这个结构搬到 Ripes 上: "主存" 是一个大数组, "LS" 是两个小缓冲区,
 *   "MFC" 是一个 16 项的软件命令队列, "DMA 引擎" 在 tag wait 时才真正执行
 *   拷贝 (用 volatile 逐字拷贝, 避免编译器把它变成 memcpy 调用)。
 *   Here "main memory" is a big array, "LS" is two small buffers, the "MFC" is
 *   a 16-entry software command queue and the "DMA engine" performs the copy
 *   when a tag is waited on (volatile word copy so GCC cannot turn it into a
 *   memcpy call; the program links with -nostdlib).
 *
 * 与真实 SPU 代码的对应 / Mapping to real SPU intrinsics (spu_mfcio.h)
 *   dma_get(ls, ea, size, tag)   <->  mfc_get(ls, ea, size, tag, 0, 0)
 *   dma_put(ls, ea, size, tag)   <->  mfc_put(ls, ea, size, tag, 0, 0)
 *   wait_tag(mask)               <->  mfc_write_tag_mask(mask);
 *                                     mfc_read_tag_status_all();
 *   mailbox_notify(v)            <->  spu_write_out_intr_mbox(v)  (SPU_WrOutIntrMbox)
 *
 * 编译 / Build (Ripes editor, input type C):
 *   compiler arguments:  -O1 -nostdlib
 *   需要 RV32IM; 不依赖任何 C 标准库。输出通过 ecall 打印。
 *   Requires RV32IM, no libc. Output goes through Ripes ecalls.
 */

/* ---- 启动代码 / startup: 必须是文件中第一段代码 (-nostdlib 时入口 = 第一条指令) ---- */
asm("li sp, 0x100000");  /* SP set to 1 MB */
asm("jal main");         /* call main */
asm("mv a1, a0");        /* save return value in a1 */
asm("li a7, 10");        /* prepare ecall exit */
asm("ecall");            /* stop the simulator */

/* ---- 参数 / parameters ---------------------------------------------------- */
#define STREAM_WORDS 256          /* "主存" 中的流长度 (32-bit 字)  / stream length  */
#define CHUNK_WORDS 16            /* 每块大小 (真实 SPE: ≤16 KB/命令) / chunk size   */
#define NUM_CHUNKS (STREAM_WORDS / CHUNK_WORDS)
#define MFC_QUEUE_DEPTH 16        /* 真实 SPU 命令队列深度 / real SPU command queue depth */
#define TAG_A 0                   /* 缓冲区 A 用的 tag group / tag group for buffer A */
#define TAG_B 1                   /* 缓冲区 B 用的 tag group / tag group for buffer B */

/* ---- "主存" (XDR) / "main memory" ----------------------------------------- */
static unsigned int xdr_in[STREAM_WORDS];   /* 输入流 / input stream           */
static unsigned int xdr_out[STREAM_WORDS];  /* 输出流 / output stream          */

/* ---- "Local Store": 两组输入/输出缓冲区 / two in/out buffer pairs ---------- */
/* 真实代码: __attribute__((aligned(128))) 且 LS 总共只有 256 KB。            */
static unsigned int ls_in[2][CHUNK_WORDS];
static unsigned int ls_out[2][CHUNK_WORDS];

/* ---- 模拟的 MFC 命令队列 / simulated MFC command queue --------------------- */
typedef struct {
    unsigned int* ls;       /* Local Store 地址 / LS address (LSA)              */
    unsigned int* ea;       /* 有效地址 (主存) / effective address (EA)         */
    unsigned int words;     /* 传输大小 / size in words                          */
    unsigned int tag;       /* tag group 0..31                                   */
    unsigned int is_put;    /* 0 = get (EA->LS), 1 = put (LS->EA)                */
    unsigned int pending;   /* 1 = 已入队未完成 / queued, not yet completed      */
} mfc_cmd_t;

static mfc_cmd_t mfc_queue[MFC_QUEUE_DEPTH];
static unsigned int stat_gets = 0, stat_puts = 0, stat_waits = 0, stat_stalls = 0;

/* ---- ecall 输出辅助 / ecall print helpers (no libc) ------------------------ */
static void print_str(const char* s) {
    register const char* a0 asm("a0") = s;
    register int a7 asm("a7") = 4;
    asm volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}
static void print_int(int v) {
    register int a0 asm("a0") = v;
    register int a7 asm("a7") = 1;
    asm volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}
static void print_char(char c) {
    register int a0 asm("a0") = c;
    register int a7 asm("a7") = 11;
    asm volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}

/* ---- "DMA 引擎": 真正执行一条命令 / the "DMA engine" executes one command -- */
static void dma_engine_execute(mfc_cmd_t* c) {
    /* volatile 逐字拷贝: (1) 模拟 DMA 引擎独立于 SPU 的 load/store 工作,
     * (2) 防止 GCC 在 -O1+ 把循环识别成 memcpy() (我们没有 libc)。
     * volatile word copy: models the DMA engine and keeps GCC from emitting a
     * memcpy() call that -nostdlib could not resolve.                          */
    volatile unsigned int* src = c->is_put ? c->ls : c->ea;
    volatile unsigned int* dst = c->is_put ? c->ea : c->ls;
    for (unsigned int i = 0; i < c->words; i++) {
        dst[i] = src[i];
    }
    c->pending = 0;
}

/* ---- 入队一条 DMA 命令 / enqueue a DMA command ----------------------------- */
/* 真实硬件: 队列满时 wrch MFC_Cmd 会阻塞 SPU 直到有空位。                       */
/* Real HW: wrch MFC_Cmd stalls the SPU when the 16-entry queue is full.        */
static void mfc_enqueue(unsigned int* ls, unsigned int* ea, unsigned int words,
                        unsigned int tag, unsigned int is_put) {
    for (;;) {
        for (int i = 0; i < MFC_QUEUE_DEPTH; i++) {
            if (!mfc_queue[i].pending) {
                mfc_queue[i].ls = ls;
                mfc_queue[i].ea = ea;
                mfc_queue[i].words = words;
                mfc_queue[i].tag = tag;
                mfc_queue[i].is_put = is_put;
                mfc_queue[i].pending = 1;
                return;
            }
        }
        /* 队列满: 模拟 SPU 停顿, 让引擎先把最老的命令做掉。
         * Queue full: SPU stalls; drain the oldest command.                    */
        stat_stalls++;
        dma_engine_execute(&mfc_queue[0]);
    }
}

/* mfc_get(ls, ea, size, tag, tid, rid): 主存 -> Local Store, 立即返回。
 * Asynchronous main memory -> LS transfer; returns immediately.                */
static void dma_get(unsigned int* ls, unsigned int* ea, unsigned int words, unsigned int tag) {
    stat_gets++;
    mfc_enqueue(ls, ea, words, tag, 0);
}

/* mfc_put(ls, ea, size, tag, tid, rid): Local Store -> 主存, 立即返回。
 * Asynchronous LS -> main memory transfer; returns immediately.                */
static void dma_put(unsigned int* ls, unsigned int* ea, unsigned int words, unsigned int tag) {
    stat_puts++;
    mfc_enqueue(ls, ea, words, tag, 1);
}

/* mfc_write_tag_mask(mask); mfc_read_tag_status_all();
 * 阻塞直到 mask 中所有 tag group 的命令全部完成。这里 "阻塞" = 让引擎把这些命令
 * 按入队顺序执行完。注意: 其它 tag 的命令保持 pending, 仍在 "后台" 进行。
 * Blocks until every command in the tag groups selected by mask has completed.
 * Commands of other tags stay pending ("in flight in the background").         */
static void wait_tag(unsigned int mask) {
    stat_waits++;
    for (int i = 0; i < MFC_QUEUE_DEPTH; i++) {
        if (mfc_queue[i].pending && ((1u << mfc_queue[i].tag) & mask)) {
            dma_engine_execute(&mfc_queue[i]);
        }
    }
}

/* spu_write_out_intr_mbox(v): 通过 outbound interrupt mailbox 通知 PPE。
 * 这里只是打印。 Notify the PPE; here we just print.                           */
static void mailbox_notify(unsigned int v) {
    print_str("[SPU->PPE outbound interrupt mailbox] ");
    print_int(v);
    print_char('\n');
}

/* ---- 计算核 / compute kernel: 作用在 Local Store 上, 从不触碰主存 ----------- */
/* 真实 SPU 上这里会是 4 路 SIMD (spu_madd 等)。                                */
static void compute_chunk(const unsigned int* in, unsigned int* out) {
    for (int i = 0; i < CHUNK_WORDS; i++) {
        out[i] = in[i] * 3u + 1u;
    }
}

/* ---- SPU 主循环: 双缓冲 / the SPU main loop with double buffering --------- */
static void spu_stream_double_buffered(void) {
    /* 预取第 0 块到缓冲区 A。 Prime the pipeline: fetch chunk 0 into buffer A.
     * real: mfc_get(ls_in[0], &xdr_in[0], CHUNK_BYTES, TAG_A, 0, 0);           */
    dma_get(ls_in[0], &xdr_in[0], CHUNK_WORDS, TAG_A);

    for (int i = 0; i < NUM_CHUNKS; i++) {
        int cur = i & 1;             /* 本块用的缓冲区 / buffer for chunk i      */
        int nxt = cur ^ 1;           /* 下一块用的缓冲区 / buffer for chunk i+1  */
        unsigned int tag_cur = cur ? TAG_B : TAG_A;
        unsigned int tag_nxt = cur ? TAG_A : TAG_B;

        /* (1) 先发出下一块的 get: 这是双缓冲的核心, 必须在计算之前发出。
         *     Issue the get for chunk i+1 BEFORE computing chunk i.
         *     real: mfc_get(ls_in[nxt], &xdr_in[(i+1)*CHUNK], CHUNK_BYTES, tag_nxt, 0, 0); */
        if (i + 1 < NUM_CHUNKS) {
            dma_get(ls_in[nxt], &xdr_in[(i + 1) * CHUNK_WORDS], CHUNK_WORDS, tag_nxt);
        }

        /* (2) 等本块的数据到位。同一个 tag 也覆盖了两块之前 (i-2) 从 ls_out[cur]
         *     发出的 put, 所以这里同时保证了 "输入已到" 和 "输出缓冲已空" (无 WAR)。
         *     Wait for chunk i's get. The same tag also covers the put issued
         *     two iterations ago from ls_out[cur], so this single wait proves
         *     both "input arrived" and "output buffer free" (no WAR hazard).
         *     real: mfc_write_tag_mask(1 << tag_cur); mfc_read_tag_status_all(); */
        wait_tag(1u << tag_cur);

        /* (3) 在 Local Store 上计算; 此时下一块的 DMA "正在后台进行"。
         *     Compute on LS while chunk i+1 is still in flight.                  */
        compute_chunk(ls_in[cur], ls_out[cur]);

        /* (4) 异步写回, 不等待; 用 tag_cur, 由 (2) 在两轮之后统一等待。
         *     Asynchronous write-back with tag_cur; waited for in step (2) two
         *     iterations later.
         *     real: mfc_put(ls_out[cur], &xdr_out[i*CHUNK], CHUNK_BYTES, tag_cur, 0, 0); */
        dma_put(ls_out[cur], &xdr_out[i * CHUNK_WORDS], CHUNK_WORDS, tag_cur);
    }

    /* (5) 等最后两块的 put 完成, 再通知 PPE。 Drain both tags, then notify PPE.
     * real: mfc_write_tag_mask((1<<TAG_A)|(1<<TAG_B)); mfc_read_tag_status_all(); */
    wait_tag((1u << TAG_A) | (1u << TAG_B));
    mailbox_notify(NUM_CHUNKS);
}

int main(void) {
    /* "PPE" 准备输入流。 The "PPE" prepares the input stream in main memory.  */
    for (int i = 0; i < STREAM_WORDS; i++) {
        xdr_in[i] = (unsigned int)i;
        xdr_out[i] = 0xDEADBEEFu;
    }

    print_str("SPE double-buffer demo: ");
    print_int(NUM_CHUNKS);
    print_str(" chunks x ");
    print_int(CHUNK_WORDS);
    print_str(" words\n");

    spu_stream_double_buffered();

    /* "PPE" 校验结果。 The "PPE" verifies the output against a direct computation. */
    int errors = 0;
    for (int i = 0; i < STREAM_WORDS; i++) {
        if (xdr_out[i] != (unsigned int)i * 3u + 1u) {
            errors++;
        }
    }

    print_str("gets=");
    print_int(stat_gets);
    print_str(" puts=");
    print_int(stat_puts);
    print_str(" tag_waits=");
    print_int(stat_waits);
    print_str(" queue_full_stalls=");
    print_int(stat_stalls);
    print_char('\n');
    print_str(errors ? "FAIL: mismatches=" : "OK: mismatches=");
    print_int(errors);
    print_char('\n');
    return errors;
}
