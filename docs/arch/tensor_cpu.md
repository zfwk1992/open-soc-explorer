# 张量处理器与 CPU 的交互（Tensor Unit / NPU）

本模块在 Ripes 中加入一个教学用的张量加速器外设 **Tensor Unit (NPU)**：一个 weight-stationary 的 int8 systolic array，
通过主存中的 **descriptor ring**、**doorbell** 寄存器、**DMA** 引擎和 **IRQ_STATUS** 寄存器与 RISC-V CPU 交互。
它的目的不是追求性能，而是把"CPU 如何把工作交给加速器、加速器如何搬数据、算完后如何通知 CPU"这条路径的
每一个周期都画出来。

源码位置：

| 文件 | 内容 |
|---|---|
| `src/accel/npu/tensorunit.h/.cpp` | 不依赖 Qt 的周期级模型（systolic array、descriptor ring、DMA、状态机、计数器） |
| `src/io/iotensorunit.h/.cpp` | Qt 外设 `IOTensorUnit : IOBase`：寄存器映射、与处理器时钟同步、DMA 重入保护、反向执行历史 |
| `src/io/iotensorunitwidget.h/.cpp` | 可视化 widget：标题栏、状态机图、脉动阵列热图、描述符环 / DMA / 中断 / 寄存器标签页 |
| `resources/i18n/ionpu_zh_CN.ts` | widget 与外设描述的中文翻译 |
| `examples/C/npu_matmul.c`、`examples/assembly/npu_matmul.s` | 示例程序：填描述符、敲门铃、轮询、CPU 验算、打印周期数 |
| `test/tst_npu.cpp` | QTest 单元测试 |

---

## 1. 为什么需要张量加速器

### 1.1 算术强度与 roofline

一个 kernel 的 **arithmetic intensity**（算术强度）定义为每从内存搬 1 byte 所做的运算次数：

```
AI = FLOPs (或 MACs) / bytes moved
```

矩阵乘 `C[M][N] = A[M][K] × B[K][N]` 需要 `M·N·K` 次乘加，但只需读 `M·K + K·N` 个输入、写 `M·N` 个输出。
以 int8 输入、int32 输出计，`M=N=K=n` 时：

```
MACs   = n^3
bytes  = 2·n^2 (输入, int8) + 4·n^2 (输出, int32) = 6·n^2
AI     = n / 6   MAC/byte
```

算术强度随 `n` 线性增长——矩阵越大，越"值得"用专用硬件算。**Roofline** 模型把可达性能表示为
`min(峰值算力, 带宽 × AI)`：当 AI 很低时性能被内存带宽封顶（memory-bound），AI 高到一定程度后才被算力封顶（compute-bound）。
通用 CPU 的问题在于：每做一次乘加都要经过取指、译码、寄存器读写，而且每个周期只能发射少量乘法；加速器用
成百上千个乘法器并行，同时把数据复用做到极致，才能把 AI 高的 kernel 推到 compute-bound 区域。

### 1.2 本模块的"小"roofline

本模块的 Tensor Unit 有 `N×N` 个乘法器（默认 4×4 = 16 MAC/cycle），DMA 每周期搬 `W` 个 32-bit 字（默认 1）。
因此：

```
峰值算力  = N^2 MAC/cycle            (默认 16)
带宽      = 4·W byte/cycle           (默认 4)
拐点 AI   = N^2 / (4·W) MAC/byte     (默认 4)
```

4×4×4 的矩阵乘 AI = 64 MAC / 96 byte ≈ 0.67，远低于拐点 4，所以在默认配置下它是 memory-bound 的——
第 5 节的实测周期会清楚地展示这一点："搬数据比算慢"。

---

## 2. Systolic array 数据流（TPU v1 式 weight-stationary）

### 2.1 结构

```
            n0        n1        n2        n3        <- 列 (输出 C 的列)
         ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐
 a[m][0]→│PE(0,0)│→│PE(0,1)│→│PE(0,2)│→│PE(0,3)│→   行 k0  (持有 B[0][*])
         └───┬───┘ └───┬───┘ └───┬───┘ └───┬───┘
             ↓psum     ↓         ↓         ↓
         ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐
 a[m][1]→│PE(1,0)│→│PE(1,1)│→│PE(1,2)│→│PE(1,3)│→   行 k1  (持有 B[1][*])
         └───┬───┘ └───┬───┘ └───┬───┘ └───┬───┘
             ↓         ↓         ↓         ↓
                     ...  (行 k2, k3)  ...
             ↓         ↓         ↓         ↓
          C[m][0]   C[m][1]   C[m][2]   C[m][3]      <- 结果从底部流出
```

每个 PE（processing element）保存三个值，widget 中每个格子都显示它们：

| 值 | 含义 |
|---|---|
| `w` | **stationary weight**：B 的一个元素 `B[k][n]`，在一个 K-tile 的计算期间固定不变 |
| `a` | 本周期流经该 PE 的 activation（A 的一个元素），下一周期传给右边的 PE |
| `acc` | 本周期产生的 partial sum：`acc = psum_in(上方 PE 上一周期的 acc) + a × w`，下一周期传给下方的 PE |

"weight-stationary" 的含义：权重先装进阵列并**停住**，activation 从左向右流，partial sum 从上向下流。
每个权重被 M 行 activation 复用 M 次而不需要重新从内存读取——这就是数据复用降低带宽需求的来源。

### 2.2 为什么要斜向（skewed）注入

`C[m][n] = Σ_k A[m][k]·B[k][n]`。列 n 的 K 个 PE 自上而下依次把 `A[m][k]·B[k][n]` 累加进同一条 psum 链。
psum 从 PE(k-1,n) 流到 PE(k,n) 要一个周期，所以 `A[m][k]` 必须比 `A[m][k-1]` **晚一个周期**到达同一列。
同时 `a` 从 PE(k,0) 流到 PE(k,n) 又要 n 个周期。于是约定：

```
A[m][k] 在周期 t = m + k 进入第 k 行的左端 PE(k,0)
→ 在周期 t = m + k + n 到达 PE(k,n)
→ 列 n 的底部 PE(N-1,n) 在周期 t = m + (N-1) + n 输出 C[m][n]
```

一个 M 行的 tile 从第一个 activation 进入到最后一个结果流出共 `M + 2N - 2` 个周期；之前还有 `N` 个周期把权重逐行移入。

### 2.3 2×2 示例逐周期演示

取 `N = 2`，`A = [[a00 a01],[a10 a11]]`，`B = [[b00 b01],[b10 b11]]`。先用 2 个周期装载权重（PE(k,n).w = b_kn），
然后：

```
周期 t=0   注入 a00 → PE(0,0)                    PE(1,0) 无输入
           PE(0,0).acc = a00·b00
           底部输出：无

周期 t=1   PE(0,0).a = a10 (新注入, m=1)         PE(0,1).a = a00 (从左传来)
           PE(1,0).a = a01 (新注入, m=0, k=1)
           PE(0,0).acc = a10·b00                 PE(0,1).acc = a00·b01
           PE(1,0).acc = [a00·b00] + a01·b10 = C00   ← 上方 PE(0,0) 上周期的 acc
           底部输出：列0 → C[0][0] ✔

周期 t=2   PE(0,1).a = a10                       PE(1,0).a = a11,  PE(1,1).a = a01
           PE(0,1).acc = a10·b01
           PE(1,0).acc = [a10·b00] + a11·b10 = C10
           PE(1,1).acc = [a00·b01] + a01·b11 = C01
           底部输出：列0 → C[1][0] ✔   列1 → C[0][1] ✔

周期 t=3   PE(1,1).a = a11
           PE(1,1).acc = [a10·b01] + a11·b11 = C11
           底部输出：列1 → C[1][1] ✔
```

4 个周期（`M + 2N - 2 = 2 + 4 - 2`）得到全部 4 个结果，8 次乘加由 4 个 PE 在 4 个周期内完成。
在 Ripes 里把阵列参数设成 2、单步执行示例程序，就能在 widget 上看到完全相同的数字流动。

### 2.4 Tiling

阵列只有 N×N，而描述符中的 M、N、K 可以更大。模型按下面的顺序把大矩阵切成 tile：

```
for mt in 0 .. ceil(M/N)-1:
  for nt in 0 .. ceil(N/N)-1:
    acc_tile = 0
    for kt in 0 .. ceil(K/N)-1:
      DMA_IN : A[mt-tile][kt-tile] (≤N×N int8), B[kt-tile][nt-tile] (≤N×N int8)
      COMPUTE: 装载权重 (N 周期) + systolic pass (tileM + 2N - 2 周期), 结果累加进 acc_tile
    DMA_OUT: acc_tile → C[mt-tile][nt-tile] (int32)
```

不足一个 tile 的边缘用 0 填充；填充部分的乘加不计入 `MACS` 计数器，因此 `MACS` 始终等于 `M·N·K`，
而 `util = MACS / (BUSY_CYCLES × N²)` 直接反映阵列的真实利用率。

---

## 3. CPU 与加速器的交互

### 3.1 MMIO 寄存器

加速器对 CPU 来说只是地址空间里的一段寄存器。所有"控制"都是普通的 `lw/sw`。本模块的寄存器
（基址由 Ripes 的 I/O tab 分配，符号 `TENSOR_UNIT_0_<REG>` 为绝对地址，`TENSOR_UNIT_0_<REG>_OFFSET` 为偏移）：

| 偏移 | 名称 | 读写 | 说明 |
|---|---|---|---|
| 0x00 | `QUEUE_BASE` | RW | descriptor ring 在主存中的字节地址 |
| 0x04 | `QUEUE_SIZE` | RW | ring 的槽数（每槽 32 字节）。为 0 时加速器不取描述符 |
| 0x08 | `HEAD` | R | 加速器下一次要消费的槽索引；每完成一个描述符加一（模 `QUEUE_SIZE`） |
| 0x0C | `TAIL` | RW | CPU 已填好的槽之后的第一个空槽索引。**写入即 doorbell** |
| 0x10 | `STATUS` | R | bit0 `BUSY`；bits[7:4] 状态机状态（0 IDLE,1 FETCH_DESC,2 DMA_IN,3 COMPUTE,4 DMA_OUT,5 COMPLETE）；bit8 `QUEUE_EMPTY`（head==tail）；bit9 `ERROR`（上一个描述符被拒绝或因 DMA 故障中止；取下一个描述符时自动清除）；bit10 `WEIGHT_LOAD`；bit11 `DMA_FAULT`（ERROR 的原因是 DMA 访问被拒绝，见 `FAULT_ADDR`） |
| 0x14 | `IRQ_EN` | RW | 中断使能，位定义同 `IRQ_STATUS` |
| 0x18 | `IRQ_STATUS` | RW (W1C) | bit0 `DESC_DONE`、bit1 `QUEUE_EMPTY`、bit2 `ERROR`。**写 1 清零**，写 0 无效果 |
| 0x1C | `CYCLES` | R | 复位以来的总周期数（无论忙闲都计数，可当作免费的 cycle counter） |
| 0x20 | `DMA_BYTES` | R | DMA 搬运的字节数（含描述符、输入、输出） |
| 0x24 | `MACS` | R | 有效 int8 乘加次数 |
| 0x28 | `ARRAY_N` | R | 阵列维度 N |
| 0x2C | `BUSY_CYCLES` | R | 状态不为 IDLE 的周期数 |
| 0x30 | `CTRL` | W | bit0 `SOFT_RESET`（中止当前任务，head=tail=0，清 IRQ_STATUS，保留 QUEUE_*、IRQ_EN 与计数器）；bit1 `CLEAR_COUNTERS` |
| 0x34 | `FAULT_ADDR` | R | 最近一次被拒绝的 DMA 访问的字节地址（例如 `QUEUE_BASE` 或描述符的 `dst` 指向 NPU 自己的寄存器区），见 7.2 节 |

外设还导出常量符号：`TENSOR_UNIT_0_N`（阵列维度）、`_DESC_SIZE`(32)、`_DESC_OPCODE/_SRCA/_SRCB/_DST/_M/_N/_K/_FLAGS`
（描述符字段偏移）、`_OP_NOP/_OP_MATMUL/_OP_RELU/_OP_ADD`、`_FLAG_RELU_FUSED/_FLAG_NO_IRQ`、
`_STATUS_BUSY/_STATUS_QUEUE_EMPTY/_STATUS_ERROR/_STATUS_DMA_FAULT`、`_IRQ_DESC_DONE/_IRQ_QUEUE_EMPTY/_IRQ_ERROR`、
`_CTRL_SOFT_RESET/_CTRL_CLEAR_COUNTERS`。C 程序 `#include "ripes_system.h"` 后即可使用。

外设参数（I/O tab 右侧）：`Array size (N x N)`（1..32，默认 4）、`DMA words/cycle`（1..64，默认 1）、`PE cell size`（绘图）。

### 3.2 Descriptor（任务描述符）

为什么不直接把 srcA/srcB/dst/M/N/K 做成寄存器？因为那样 CPU 每提交一个任务都得等上一个任务完成，
CPU 与加速器无法流水。把任务写成主存中的**描述符**，加速器自己去取，CPU 就可以一次排队多个任务后去做别的事。

每个描述符 32 字节，8 个 little-endian 的 uint32，位于 `QUEUE_BASE + idx × 32`：

```
偏移  字段     MATMUL                           RELU                 ADD
+0    opcode   1                                2                    3        (0 = NOP)
+4    srcA     A 的地址: M×K int8, row-major    src: M·N 个 int32    src0
+8    srcB     B 的地址: K×N int8, row-major    (忽略)               src1
+12   dst      C 的地址: M×N int32, row-major   dst (可等于 srcA)    dst
+16   M        A/C 行数                         } 元素个数 = M·N
+20   N        B/C 列数                         }
+24   K        A 列数 = B 行数                  (忽略)
+28   flags    bit0 RELU_FUSED: 写回前做 ReLU;  bit1 NO_IRQ: 完成时不置 DESC_DONE
```

`M`、`N`、`K` 必须在 `[1, 4096]`，opcode 必须合法；否则描述符被**拒绝**：`STATUS.ERROR` 与 `IRQ_STATUS.ERROR` 置位，
`HEAD` 照常前进（坏描述符不会卡死队列）。

### 3.3 Descriptor ring 与 doorbell

```
            QUEUE_BASE
            ┌──────┬──────┬──────┬──────┐      QUEUE_SIZE = 4
            │ slot0│ slot1│ slot2│ slot3│
            └──────┴──────┴──────┴──────┘
                      ▲             ▲
                    HEAD          TAIL
               (设备消费位置)   (CPU 生产位置)
```

- 队列为空：`HEAD == TAIL`。队列里有 `(TAIL - HEAD) mod SIZE` 个待处理任务。
- CPU 提交：把描述符写进 `slot[TAIL]`，然后写 `TAIL = (TAIL + 1) mod SIZE`。这个写操作就是 **doorbell**——
  它不传递数据，只是"敲一下门"告诉设备"有新工作"。设备空闲时下一个周期进入 `FETCH_DESC`。
- 满队列：经典的单槽留空规则——`(TAIL + 1) mod SIZE == HEAD` 时不能再提交，否则 CPU 分不清满与空。
- 写 `TAIL` 时的值自动按 `QUEUE_SIZE` 取模。

本模块只有一个 TAIL 寄存器且 CPU 单核，所以没有多生产者的并发问题；真实驱动里对 TAIL 的更新需要加锁或用原子操作。

### 3.4 状态机

```
                 doorbell (head != tail)
        ┌───────┐──────────────────►┌────────────┐
        │ IDLE  │                   │ FETCH_DESC │ 读 8 个字 (ceil(8/W) 周期)
        └───────┘◄──┐               └─────┬──────┘
                    │                     │ 校验 opcode / M / N / K
                    │             失败 ───┼─────────────────────────────┐
                    │                     ▼                             │
                    │               ┌──────────┐  A/B tile (int8)       │
                    │          ┌───►│  DMA_IN  │  或 int32 chunk        │
                    │          │    └────┬─────┘                        │
                    │          │         ▼                              │
                    │          │    ┌──────────┐  装权重 N 周期          │
                    │          │    │ COMPUTE  │  + systolic pass       │
                    │          │    └────┬─────┘  (RELU/ADD: ceil(n/N)) │
                    │          │  下一 K-tile │                         │
                    │          └──────────┤                             │
                    │                     ▼ 本 (m,n) tile 的 K 全部完成  │
                    │               ┌──────────┐                        │
                    │          ┌────│ DMA_OUT  │  写回 C tile (int32)    │
                    │          │    └────┬─────┘                        │
                    │ 下一 (m,n) tile → DMA_IN                           │
                    │                     ▼ 所有 tile 完成               │
                    │               ┌──────────┐                        │
                    └───────────────│ COMPLETE │◄───────────────────────┘
                                    └──────────┘ HEAD++, 置 IRQ_STATUS
```

`COMPLETE` 占一个周期：`HEAD = (HEAD + 1) mod SIZE`，置 `DESC_DONE`（除非 `NO_IRQ`）或 `ERROR`；
若此时 `HEAD == TAIL` 再置 `QUEUE_EMPTY`。回到 `IDLE` 后若队列仍非空，下一周期立刻再取下一个描述符。

### 3.5 DMA：搬数据的代价

加速器**不能**直接用 CPU 的 load/store——它通过 DMA 引擎自己访问总线（在 Ripes 里即 `IOBase::memRead/memWrite`）。
模型中 DMA 每周期最多搬 `W` 个 32-bit 字：int8 矩阵元素 4 个/字，int32 元素 1 个/字，描述符本身也要 8 个字。
widget 的 **DMA && cycles** 标签页用进度条显示当前传输（文字同时写明方向：主存→NPU 或 NPU→主存），并分别统计读入/写出字节数、DMA 任务数以及每个状态所占的周期数。

因为 `DMA_IN`、`COMPUTE`、`DMA_OUT` 是**串行**的（没有 double buffering），一个 4×4×4 矩阵乘中真正算的只有
14 个周期，而搬数据要 8 + 8 + 16 = 32 个周期。真实的加速器会用双缓冲让下一个 tile 的 DMA_IN 与当前 tile 的
COMPUTE 重叠，这正是 PS3 模块中 SPE 双缓冲 DMA 的思路。

### 3.6 完成通知：轮询 vs 中断

**轮询（polling）**：CPU 在循环里反复读 `HEAD`（或 `STATUS.BUSY`、`IRQ_STATUS.DESC_DONE`）。简单、延迟最低，
但 CPU 在等待期间什么都做不了，而且每次读都占用总线。

```c
#include "ripes_system.h"
#define REG(a) (*(volatile unsigned *)(a))

unsigned tail = REG(TENSOR_UNIT_0_TAIL);
fill_descriptor(ring[tail], ...);                  /* 普通 store */
tail = (tail + 1) % RING_SLOTS;
REG(TENSOR_UNIT_0_TAIL) = tail;                    /* doorbell */
while (REG(TENSOR_UNIT_0_HEAD) != tail) { }        /* 轮询 */
use(C);
```

```asm
        li   t1, TENSOR_UNIT_0_TAIL
        li   t2, 1
        sw   t2, 0(t1)            # doorbell: TAIL = 1
        li   t1, TENSOR_UNIT_0_HEAD
poll:   lw   t4, 0(t1)
        bne  t4, t2, poll         # 直到 HEAD == 1
```

**中断（interrupt）**：CPU 把 `IRQ_EN.DESC_DONE` 置 1 后去做别的事；加速器完成时 `IRQ_STATUS.DESC_DONE` 置位，
`IRQ_STATUS & IRQ_EN != 0` 使中断线有效，CPU 跳到 ISR。ISR 必须**写 1 清除**对应位（W1C），否则退出 ISR 后
中断线仍然有效，会立刻再次进入 ISR（"中断风暴"）。W1C 的好处是 ISR 只清自己处理过的位，不会误清同时到来的其它事件。

```c
void npu_isr(void) {
    unsigned st = REG(TENSOR_UNIT_0_IRQ_STATUS);
    if (st & TENSOR_UNIT_0_IRQ_DESC_DONE) {
        REG(TENSOR_UNIT_0_IRQ_STATUS) = TENSOR_UNIT_0_IRQ_DESC_DONE;  /* W1C: 只清这一位 */
        done_count++;
    }
    if (st & TENSOR_UNIT_0_IRQ_ERROR) {
        REG(TENSOR_UNIT_0_IRQ_STATUS) = TENSOR_UNIT_0_IRQ_ERROR;
        handle_error();
    }
}
```

Ripes 的 RISC-V 处理器模型没有外部中断控制器，因此本模块**不会真的打断 CPU**：中断线的状态显示在 widget 的
**Interrupts** 标签页上（“Interrupt line: ASSERTED”），程序可以用读 `IRQ_STATUS` 的方式"模拟"ISR 的行为（示例程序结尾演示了 W1C）。
批量提交时可以给前面的描述符置 `NO_IRQ`，只让最后一个产生 `DESC_DONE`，减少中断次数——这是驱动程序里常见的
"interrupt coalescing"。

### 3.7 内存一致性：cache flush / invalidate / fence

真实 SoC 里 CPU 带 cache，而 DMA 引擎直接访问 DRAM（或经过非一致的总线）。两个方向都会出问题：

1. **CPU → 设备**：CPU 写的描述符和矩阵 A、B 可能还停留在 write-back cache 里，DRAM 中是旧值。
   敲 doorbell 之前必须 **clean/flush**（写回）这些 cache line，并执行 **fence**，保证"写回完成"先于"doorbell 写"
   到达总线。RISC-V 上即 `fence w,o`（或 `fence rw,io`）之类的屏障；否则乱序/写缓冲可能让 doorbell 先于数据到达。
2. **设备 → CPU**：DMA 把 C 写进 DRAM，但 CPU cache 里可能还缓存着 C 所在的行（例如 CPU 之前读过或写过 C）。
   收到完成通知后、读 C 之前，必须 **invalidate** 这些行；而且在 DMA 进行期间 CPU 不能向这些行写入，否则 cache
   回写会覆盖 DMA 的结果（经典的 "DMA buffer 与别的变量共享 cache line" bug，所以 DMA buffer 要按 cache line 对齐）。
   硬件一致性（如 ARM 的 ACE、CCI，或 x86 的一致性 DMA）或 IOMMU 可以免去这些手工操作，代价是总线更复杂。

**在 Ripes 中不需要这些操作。** Ripes 的 cache simulator 只是统计命中/缺失的模型，叠在**单一的、一致的**内存之上；
CPU 的每次访存都直接读写这块内存，外设的 `memRead/memWrite` 也是。所以本模块的 DMA 永远看到最新的数据，
示例程序只在注释里标出"真实 SoC 在这里要 flush/invalidate"的位置。示例中的 `volatile` 只是阻止**编译器**
把描述符的 store 重排到 doorbell 之后；Ripes 的处理器模型是顺序执行的，没有硬件重排。

---

## 4. Offload 成本模型：什么时候值得交给加速器

把任务交给加速器的总代价不只是加速器的计算时间：

```
T_offload = T_submit           CPU 填描述符 + 敲门铃 (+ 真实 SoC 的 cache flush)
          + T_fetch            设备取描述符          = ceil(8 / W)
          + T_dma_in           输入搬入              ≈ (M·K + K·N) / (4·W)    (int8, 每 K-tile 重复)
          + T_compute          阵列计算              ≈ tiles × (N + tileM + 2N - 2)
          + T_dma_out          输出写回              = M·N·4 / (4·W) = M·N / W
          + T_notify           完成 → CPU 察觉        (轮询: 1 次 lw; 中断: ISR 延迟)
          (+ 真实 SoC 的 cache invalidate)

T_cpu     = M·N·K × (每次乘加的指令数 ≈ 2 次 load + mul + add + 地址/循环开销 ≈ 8~12 条指令)
```

offload 只有在 `T_offload < T_cpu` 时才划算；固定开销（submit/fetch/notify）决定了一个**最小值得 offload 的规模**，
而 DMA 项决定了算术强度低时是否 memory-bound。

### 4.1 用模块自己的计数器做一个实例

下表是模型实测（`BUSY_CYCLES` 按状态拆分，由 `test/tst_npu.cpp` 相同的模型得出）。`W` = DMA words/cycle。

| 配置 | FETCH | DMA_IN | COMPUTE | DMA_OUT | COMPLETE | BUSY 合计 | DMA bytes | MACs | 阵列利用率 |
|---|---|---|---|---|---|---|---|---|---|
| N=4, W=1, 4×4×4 | 8 | 8 | 14 | 16 | 1 | **47** | 128 | 64 | 64/(47×16) = 8.5% |
| N=4, W=4, 4×4×4 | 2 | 2 | 14 | 4 | 1 | **23** | 128 | 64 | 17.4% |
| N=4, W=1, 6×4×5 (C 示例) | 8 | 18 | 52 | 24 | 1 | **103** | 198 | 120 | 7.3% |
| N=4, W=1, 8×8×8 | 8 | 64 | 112 | 64 | 1 | **249** | 544 | 512 | 12.9% |
| N=8, W=1, 8×8×8 | 8 | 32 | 30 | 64 | 1 | **135** | 416 | 512 | 5.9% |
| N=8, W=4, 8×8×8 | 2 | 8 | 30 | 16 | 1 | **57** | 416 | 512 | 14.0% |
| N=4, W=1, 16×16×16 | 8 | 512 | 896 | 256 | 1 | **1673** | 3104 | 4096 | 15.3% |

读表：

- **4×4×4、W=1**：47 个忙周期里只有 14 个在算（其中 4 个还在装权重），其余 33 个全在搬数据和取描述符。
  把 DMA 带宽提到 4 字/周期后降到 23 周期，计算部分一个周期没少——memory-bound 的典型表现。
- **8×8×8 从 4×4 阵列换成 8×8**：COMPUTE 从 112 降到 30，但 DMA_OUT 仍是 64，总周期只从 249 降到 135；
  再把 W 提到 4 才到 57。算力翻 4 倍而总时间只快 1.8 倍，就是 Amdahl 定律在加速器上的体现。
- **8×8 阵列算 8×8×8 的利用率 (5.9%) 比 4×4 阵列 (12.9%) 还低**：阵列越大，填满它所需的 M、K 也越大，
  小矩阵根本喂不饱大阵列。这是为什么推理框架要把小算子"融合"成大算子。
- **16×16×16**：4×4 阵列要跑 64 个 K-tile 的 DMA_IN（每个 tile 的 A/B 子块都要重新搬，B 的复用只在 tile 内），
  DMA_IN 占 512 周期。更大的片上 buffer 或更聪明的 tile 顺序（固定 B tile、流过所有 M）可以减少重复搬运。

**与 CPU 对比**：示例 `examples/C/npu_matmul.c`（6×4×5）在 CPU 上的三重循环约需 120 次乘加 × ~10 条指令 ≈
1200 条指令；在 5 级流水线上大约 1300~1500 周期（含 load-use 停顿与分支代价），而 NPU 的忙周期是 103，
CPU 侧的 submit + 轮询开销只有几十个周期。即使 memory-bound，10 倍以上的收益仍然值得 offload；
但如果矩阵只有 2×2（8 次乘加，CPU 约 100 周期），NPU 的固定开销（取描述符 8 周期 + 装权重 + 流水延迟 + 写回）
就让 offload 几乎无利可图。示例程序会把 CPU 的周期数（用 `CYCLES` 寄存器做 timer 测得）与 NPU 的
`BUSY_CYCLES`、墙钟周期（含轮询）一起打印出来，可以换不同的 N 和 W 参数自己验证。

---

## 5. 量化（int8）简介

训练好的神经网络权重通常是 float32，但推理时把它们**量化**成 int8 可以让乘法器面积缩小约 16 倍、
内存带宽需求降到 1/4，这正是 TPU v1 选择 int8 的原因。最常用的是**对称线性量化**：

```
q = round(x / s),  x ≈ q × s,  s = max(|x|) / 127
```

两个 int8 相乘最大 127×127 ≈ 2^14，累加 K 项后需要 `14 + log2(K)` 位——所以累加器用 **int32**（本模块 `acc`
与输出 C 都是 int32），K 可达 2^17 都不会溢出。输出再按 `s_A × s_B` 缩放（requantize）回 int8 送给下一层。
本模块不做 requantize，只在 `flags.RELU_FUSED` 时把激活函数 ReLU 融合进写回，这也是真实加速器的常见做法
（把逐元素操作塞进 DMA_OUT 路径，免去一次读写往返）。

---

## 6. 教程：在 Ripes 中运行

1. **实例化外设**：I/O tab → 双击 "Tensor Unit (NPU)"。右侧可调 `Array size`（建议先用 2 或 4 便于观察）与
   `DMA words/cycle`。观察寄存器表与导出符号（`TENSOR_UNIT_0_*`）。
2. **加载示例**：`File → Load Example → Assembly → npu_matmul.s`（4×4×4）或 `C → npu_matmul.c`
   （6×4×5，需要配置 RISC-V 编译器并加 `-nostdlib`）。
3. **单步观察**（F5 = 单步 Clock；F6 = 自动时钟 Auto clock）：
   - 执行到 `sw t2, 0(t1)`（doorbell）之后，状态机图中高亮（并标 ▶）的状态从 `IDLE` 变成 `FETCH_DESC`，**Descriptor ring** 标签页中槽 0 标为 `HEAD - ▶ in flight`；
   - `DMA_IN` 阶段 **DMA && cycles** 标签页的进度条逐字节前进；
   - `COMPUTE` 的装载权重阶段，状态机下方的文字写明“已移入第几行”：B 的行从顶部进入，**最后一行 k=N-1 最先进入**，每周期向下移动一行，所以看起来是“倒序”推入的，这是正确的；
   - 脉动计算阶段显示“已完成 x / (tileM + 2N − 2) 个阵列周期”，`a` 从左向右、`acc` 从上向下流动；本周期做了乘加的 PE 按 |acc| 着色（热图），粗边框表示该 PE 持有有效 activation；
   - `DMA_OUT` 写回后回到 `IDLE`，HEAD 变成 1，**Interrupts** 标签页显示 `IRQ_STATUS` = DESC_DONE | QUEUE_EMPTY（0x3）。
4. **运行**（Run）：控制台打印 CPU 周期、NPU 墙钟周期、忙周期、DMA 字节数、MAC 数以及校验结果。
   把阵列改成 8×8 或把 DMA 带宽改成 4，重新运行，对照第 4 节的表格。
5. **实验建议**：
   - 用 `.equ`/宏改 M、N、K，找出 offload 不再划算的最小规模；
   - 把示例里的轮询换成"先做别的事再检查 `IRQ_STATUS`"，体会异步执行；
   - 写两个描述符再敲一次门铃（TAIL = 2），观察 ring 的 HEAD 连续前进与 `NO_IRQ` 的效果；
   - 故意写一个 opcode = 9 的描述符，观察 `STATUS.ERROR` 与 `IRQ_STATUS.ERROR`，确认队列不会卡死。

---

## 7. 实现说明

- **周期同步**：`IOTensorUnit` 以 `Qt::DirectConnection` 连接 `ProcessorHandler::processorClocked`，在处理器线程里
  每个 CPU 周期调用一次 `TensorUnit::step()`。这保证了 CPU 轮询时看到的 `HEAD`/`STATUS` 变化与周期数一致。
  注意：敲门铃后的第一个周期以及两个描述符之间，加速器各有一个周期处于 `IDLE`，所以轮询 `STATUS.BUSY` 的循环
  可能过早退出，应轮询 `HEAD`（或 `IRQ_STATUS`）。
- **线程安全**：`ioRead/ioWrite/step` 来自处理器线程，widget 在 GUI 线程；所有对模型的访问都在 `QMutex` 下进行，
  GUI 只读取一份 `Snapshot`。处理器线程每周期只置一个原子 dirty 标志，GUI 线程的 50 ms 定时器据此刷新
  （widget 不可见时不刷新），所以运行（Run）时不会每周期向 GUI 投递事件。外设参数（阵列大小、DMA 带宽、格子大小）
  在 GUI 线程的 `applyParameters()` 中读取一次并缓存为 `std::atomic<unsigned>`，处理器线程从不访问
  `std::map` 形式的 `IOParam` 表，也不做 `QVariant::toUInt()`。
- **复位**：处理器 reset 调用 `IOBase::reset()`，模型回到全零状态（含计数器），反向执行历史清空。
  `CTRL.SOFT_RESET` 只中止任务。

### 7.1 widget 布局

widget（`src/io/iotensorunitwidget.*`）自上而下：

1. **标题栏**：标题 + 一句话说明；右侧状态徽章（状态名 + BUSY / ERROR / 空闲，颜色与文字双重表达）和
   “?” 按钮（`Ripes::showHelp("10-npu")`）。下方一行计数：周期、忙周期、MAC、阵列利用率、反向执行历史深度与内存占用。
2. **提示条**：未敲过门铃时给出操作提示（空状态）；出错时写明原因（非法 opcode / 维度越界 / DMA 故障地址），
   并说明 `STATUS.ERROR` 会在取下一个描述符时自动清除、而 `IRQ_STATUS.ERROR` 是粘滞的（W1C）；反向执行时说明是否已回退。
3. **状态机图**：IDLE → FETCH_DESC → DMA_IN → COMPUTE → DMA_OUT → COMPLETE，当前状态填充高亮色并加 ▶ 与
   “current” 文字；虚线画出“下一个 K-tile”“下一个输出 tile”“HEAD++ 后取下一个描述符”“非法描述符或 DMA 故障”四条回路；
   下方文字写明当前子步骤（第几行权重、第几个阵列周期、tile (m,n,k)、DMA 进度）。鼠标悬停状态框显示该状态的说明。
4. **脉动阵列**（左，带滚动条）：每格显示 a / w / acc；本周期做了乘加的格子按 |acc| 着色（热图），只有权重的格子用浅色，
   tile 外的填充格用虚线框；带图例；悬停 PE 显示详细数值。
5. **标签页**（右）：
   - **Descriptor ring**：QUEUE_BASE / QUEUE_SIZE / HEAD / TAIL / 待处理数，以及每个槽的地址、HEAD/TAIL 标记、
     状态（执行中 / 待处理 / 空闲）与逐字段解码（opcode、srcA、srcB、dst、M、N、K、flags）。执行中的槽取自加速器
     自己取到的描述符；其它槽在模拟暂停时从内存重新读取（运行期间不读内存，避免与处理器线程竞争）；落在 MMIO 区的槽
     标红。`QUEUE_SIZE` 为 0 或 1、以及环已满（再写 TAIL 就会追上 HEAD、环看起来变空）时显示警告。
   - **DMA && cycles**：当前传输进度条与方向；读入/写出/合计字节、DMA 任务数、带宽、CYCLES、BUSY_CYCLES、
     各状态周期数、MACS、阵列利用率、已完成/出错描述符数（每行都有单位和含义）。
   - **Interrupts**：中断线是否有效（颜色 + 文字），每个位的 IRQ_EN / IRQ_STATUS 与含义；解释 W1C、粘滞位与
     `STATUS.ERROR` 的区别，并说明 Ripes 的 CPU 没有外部中断输入，CPU **永远不会真的被中断**，只能轮询。
   - **Registers**：名字、偏移、绝对地址、读写属性、当前值、解码后的含义。

所有颜色都由当前 `QPalette` 推导（深色主题下自动变亮），并在 `ThemeManager::themeChanged` 时重算；
格子文字颜色根据背景亮度选择黑或白。所有字符串都经过 `tr()`，中文在 `resources/i18n/ionpu_zh_CN.ts`。
布局使用 `QSplitter` + `QScrollArea`，1280×800 下状态与阵列都能完整显示，缩小窗口时出现滚动条而不是截断。

### 7.2 DMA 指向自身寄存器：死锁修复

模型的 DMA 通过 `IOBase::memRead/memWrite` 访问处理器的地址空间。如果 `QUEUE_BASE` 或描述符中的地址落在
NPU 自己的寄存器区，DMA 访问会经 `AddressSpaceMM` 回到 `IOTensorUnit::ioRead/ioWrite`——而此时处理器线程正在
`step()` 中持有非递归的 `m_mutex`，以前会**死锁**。

现在 `processorWasClocked()` 在调用 `step()` 前把当前线程 ID 记到原子变量 `m_stepThread`。`ioRead/ioWrite`
若发现调用者就是正在 step 的线程，即判定为“DMA 重入自身寄存器”：不加锁、不访问模型、读返回 0、写被丢弃，并通过
`TensorUnit::reportBusError(addr)` 报告**总线故障**。模型随即中止当前描述符：`STATUS.ERROR | STATUS.DMA_FAULT`、
`IRQ_STATUS.ERROR` 置位，`FAULT_ADDR` 记录出错地址，`HEAD` 照常前进（坏描述符不会卡死队列），寄存器内容不被
DMA 改写。其它线程（例如 GUI 线程的内存视图）读寄存器时照常加锁等待，不受影响。

### 7.3 反向执行（reverse）

选择的方案是**逐周期快照 + 有界队列**（PLAN2 §6(9) 的方案 a）：

- 每个处理器周期 step 之后，把模型完整状态存入 `std::deque`，长度上限为设置项 `RIPES_SETTING_REWINDSTACKSIZE`
  （与处理器自己的回退栈相同，默认 100）+ 1 帧。
- 状态分两部分：寄存器 / 计数器 / 状态机变量（约 296 字节，每帧都复制），以及片上缓冲区 + PE 阵列 + DMA 队列
  （只在加速器忙的周期重新复制，空闲周期与上一帧共享同一块不可变数据）。实测（`tst_saveRestore`）：
  N=4 时忙周期每帧 ≤ 1.35 KB，N=32 时 ≤ 32 KB，空闲周期每帧 296 字节；默认 100 帧时最多约 3 MB，足够小。
- DMA 写主存时先读出旧值记入该周期的撤销日志（VSRTL 的内存回退只记录 CPU 自己的写，不知道外设的写）。
- `ProcessorHandler::processorReversed` 时弹出最新一帧、恢复前一帧的状态，并按逆序把该周期 DMA 写过的内存恢复为旧值。
  因此回退后 `HEAD`、`STATUS`、`CYCLES`、计数器、PE 阵列以及 DMA 写出的结果矩阵都与当时完全一致
  （`tst_e2eReverse` 逐周期比对）。
- 若回退超出历史（外设在程序中途才创建，或历史上限小于处理器的回退深度），加速器状态保持不变，widget 提示条明确显示
  “NPU 状态没有被回退”，建议复位处理器。
- **64 位处理器**：寄存器与描述符字段都是 32 位；RV64 下地址必须落在低 4 GB。
- **测试**：`test/tst_npu.cpp` 覆盖 4×4 精确乘、8×6×5 tiled 乘（4×4 与 8×8 阵列）、RELU（含 in-place）、ADD、
  ring 回绕、IRQ W1C 与回调、非法描述符、DMA 带宽影响、soft reset、总线故障（模型级）、DMA 计数器与各状态周期
  （第 4.1 节表格第一行）、状态保存/恢复；以及在真实 RV32 5 级流水线上的端到端测试：`QUEUE_BASE` 指向自身寄存器
  与 `dst` 指向自身寄存器（不死锁、报 DMA 故障、寄存器不被改写）、反向执行逐周期比对、widget 离屏渲染
  （设置环境变量 `NPU_WIDGET_GRAB_DIR` 可输出截图）、`exercise_npu_batch.s`（忙周期 92，IRQ_STATUS = 3）。

## 8. 延伸阅读

- N. Jouppi et al., *In-Datacenter Performance Analysis of a Tensor Processing Unit*, ISCA 2017（weight-stationary 256×256 int8 systolic array、CISC 指令队列、统一 buffer）。
- H. T. Kung, *Why Systolic Architectures?*, IEEE Computer 1982。
- S. Williams et al., *Roofline: An Insightful Visual Performance Model for Multicore Architectures*, CACM 2009。
- Y.-H. Chen et al., *Eyeriss*, ISCA 2016（row-stationary 等其它数据流的比较）。
