# GPU 运行原理：SIMT 微架构模型

本文档配合 open-soc-explorer 中的 **SIMT GPU** 外设（`src/io/iogpu.*`）和与之对应的、不依赖 Qt 的仿真核（`src/accel/gpu/simtgpu.*`）使用。目标是让学生在 Ripes 里用一段 RISC-V 主机程序启动一个 kernel，并**逐周期**观察 GPU 内部发生了什么：warp 如何被调度、active mask 如何随分支变化、访存如何合并、多 warp 如何隐藏延迟。

> 本模型是一个**教学用的极简 GPU**。它刻意省略了真实 GPU 的大量细节（多级 cache、寄存器 bank、乱序访存、纹理单元、TLB……），但保留了理解 GPU 编程模型和性能特征所必需的核心机制。阅读时请把它当作"概念的最小可运行版本"，而不是某款真实芯片的精确复刻。

---

## 1. SIMT、SIMD 与多核：三种并行的区别

| | 多核 (multicore CPU) | SIMD (向量指令) | SIMT (GPU) |
|---|---|---|---|
| 并行粒度 | 每核一条独立指令流 | 一条指令、固定宽度的数据向量 | 一条指令、一组**线程**（warp）同时执行 |
| 控制流 | 每核独立 | 由程序员/编译器用 mask 处理 | 硬件自动维护 active mask，线程可以"各走各的分支" |
| 程序视角 | 线程 | 向量寄存器 | 标量线程（threadIdx 各不相同） |
| 典型宽度 | 4–64 核 | 128–512 bit | 32 lane × 数十 warp × 数十 SM |

- **SIMD** 的向量宽度暴露给程序员：你写的是"把 8 个 float 加起来"。
- **SIMT** 让程序员写**标量**代码（"把 *我的* 那个元素加起来"），硬件把 32 个这样的线程打包成一个 warp，用**同一条指令**驱动，每个线程（lane）拿自己的寄存器和自己的地址。
- 当 warp 内线程走不同分支时，SIMT 硬件用 **active mask** 关闭一部分 lane，分别执行两条路径，再重新汇合（见第 3 节）。这是 SIMT 与 SIMD 最本质的差别：**分支发散（divergence）对程序员透明，但代价真实存在**。

## 2. 执行层次：lane / warp / block / grid / SM

```
grid  ──┬── block 0 ──┬── warp 0 ── lane 0..7   (thread 0..7)
        │             └── warp 1 ── lane 0..7   (thread 8..15)
        ├── block 1 ──┬── warp 0 ...
        │             └── warp 1 ...
        └── block 2 ...
```

| 概念 | 含义 | 在本模型中 |
|---|---|---|
| **lane / thread** | 一个标量线程，拥有自己的寄存器 R0–R15 和谓词 P0–P3 | `S_TID` 读取 threadIdx.x |
| **warp** | 硬件调度的最小单位：`warpSize` 个线程锁步执行同一条指令 | 参数 *Warp size*，默认 8（真实 GPU 为 32） |
| **block (CTA)** | 一组可以通过 shared memory 和 barrier 协作的线程；必须**整个驻留在同一个 SM** | `BLOCK_X` 寄存器，`S_NTID` 读取 blockDim.x |
| **grid** | 一次 kernel launch 的全部 block | `GRID_X` 寄存器，`S_NBID` 读取 gridDim.x |
| **SM (Streaming Multiprocessor)** | 拥有若干 warp 槽、寄存器文件、shared memory 和调度器的计算单元 | 参数 *SMs* 与 *Warps per SM* |

**block 的分配规则**：一个 block 需要 `ceil(blockDim / warpSize)` 个 warp 槽；只有当某个 SM 有足够空闲槽时，调度器才把下一个 block 派发给它（每个 SM 每周期最多接收一个 block，SM 之间轮转）。block 大于一个 SM 的容量时 launch 失败（`STATUS.ERROR`）。这对应真实 GPU 中 "一个 block 的线程数上限（如 1024）"和"occupancy 受寄存器/shared memory 限制"这两件事。

## 3. 分支发散与重汇聚

### 3.1 什么是发散

warp 内 8 个 lane 共享一个 PC。执行 `@P0 BRA target` 时，如果有些 lane 的 P0 为真、有些为假，warp 就**发散（diverge）**了：硬件只能先执行其中一条路径（关闭其他 lane），再执行另一条路径，最后在两条路径的汇合点（**重汇聚点，reconvergence point**）把 mask 合并回来。期间每条指令只有部分 lane 在干活，**吞吐量按比例下降**。

### 3.2 本模型的规则：min-PC 重汇聚

每个 warp 维护一个 **路径集合** `{(pc, mask)}`：

1. 平时集合里只有一项，`mask` 就是 active mask。
2. 发散的分支把当前项拆成两项：`(target, taken_mask)` 和 `(pc+1, fallthrough_mask)`，统计 `STATS_DIVERGE += 1`。
3. **总是执行 pc 最小的那一项**（min-PC 规则）；
4. 任何时候若两项的 pc 相等，就把它们的 mask 按位或合并 —— 这就是重汇聚。
5. `EXIT` 把执行它的 lane 从当前项的 mask 中清除（并记入红色的 exited mask）；mask 为空的项被删除；集合为空 ⇒ warp 结束。

6. **`BAR` 按 warp 计数，而不是按路径计数**：执行 `BAR` 的路径被"停靠"（parked，pc 已指向 `BAR` 之后），不再发射；只有当 warp 的**所有存活 lane** 都已停靠在某个 `BAR`（或已 `EXIT`）时，这个 warp 才算"到达"一次 block barrier。block 内所有存活 warp 都到达后屏障释放，停靠的路径恢复执行，pc 相同的路径随即合并。这样一个发散的 warp 即使两条路径各执行了一次 `BAR`，也只计一次到达，屏障不会被提前释放（第一阶段的实现按路径计数，存在这个缺陷；见 `tst_divergentBarrier`）。

对结构化代码（if/else、循环）来说，"pc 较小的路径先跑"保证它总会先到达汇合点（汇合点的 pc 大于两条分支体），然后等待另一条路径追上来合并。循环的回跳目标 pc 更小，所以还在循环里的 lane 优先执行，已经退出循环的 lane 在循环出口处等待 —— 和真实硬件的行为一致。

真实 GPU 的做法：NVIDIA Volta 之前用 **SIMT 栈**，由编译器插入重汇聚点指令（PTX 层面的 `bra.uni`/`ssy`/`bssy`/`bsync`，即 immediate post-dominator）；Volta 之后的 **Independent Thread Scheduling** 为每个线程保存独立 PC，可以在发散路径之间交错执行，避免死锁。我们的 min-PC 规则是这两者的简化版：不需要编译器标注，也不需要显式 `JOIN` 指令。

### 3.3 谓词执行 vs 分支

短小的 if 不一定需要分支：本 ISA 的每条指令都能加 `@P0` / `@!P0` 守卫（guard），不满足谓词的 lane 直接跳过这条指令。`vecadd` 里的 `@P0 EXIT` 就是用谓词处理 `i >= N` 的尾部线程，**不产生发散事件**。编译器通常把很短的 if 变成谓词指令（if-conversion），把长的 if 留作分支。

## 4. 内存层次与合并访存

```
寄存器 (每 lane 私有, 0 延迟)
   │
shared memory (每 block 私有, 片上 SRAM, 分 bank)        ← LDS / STS
   │
L1 cache ── L2 cache ── HBM / GDDR 显存（全局内存）        ← LDG / STG
```

真实 GPU 的全局内存带宽很高（数百 GB/s 到数 TB/s），但**延迟也很高**（数百个周期），而且总线按**段（segment，通常 32 B 或 128 B）**传输。

### 4.1 合并访存（memory coalescing）

一个 warp 执行一条 `LDG` 时，8 个 lane 各自计算地址。硬件把这些地址归并成尽量少的 32 B 段事务：

- **连续访问** `A[tid]`（4 B × 8 lane = 32 B）：1 个事务 —— 完美合并。
- **跨步访问** `A[tid * 8]`（每个 lane 相隔 32 B）：8 个事务 —— 带宽利用率 1/8。
- **随机访问**：最坏情况每个 lane 一个事务。

本模型统计 `STATS_MEMTXN`（事务总数），并把每条 `LDG/STG` 分成两类：**合并（coalesced）**——事务数等于理论最小值 `ceil(活跃 lane 数 × 4 B / 32 B)`；**非合并（uncoalesced）**——需要更多的段。widget 的统计面板分别显示两类的指令数与事务数（`coalescedInstrs/Txns`、`uncoalescedInstrs/Txns`）。同时，并让一条 `LDG` 的发射 warp 停顿 `memLatency + (事务数 − 1)` 个周期，`STG` 停顿 `事务数 − 1` 个周期（存储不需要等待返回数据）。于是跨步访问不仅事务多，还让该 warp更久无法发射。

### 4.2 shared memory 与 bank conflict

shared memory 由 `sharedBanks`（默认 8）个 4 B 宽的 bank 组成，字地址 `w` 落在 bank `w % sharedBanks`。同一条 `LDS/STS` 中若多个**不同**字地址落到同一 bank，就必须串行，模型记为 `bankConflicts`（发生冲突的指令数）与 `bankConflictCycles`（额外串行周期），在 widget 的 Statistics 面板显示。多个 lane 读**同一个**字不冲突（广播）。

## 5. 延迟隐藏与 occupancy

CPU 用乱序执行和大 cache 来掩盖访存延迟；GPU 的策略是 **用大量 warp 互相掩盖**：当 warp A 在等 `LDG` 的数据时，调度器发射 warp B、C、D 的指令。只要就绪的 warp 足够多，发射槽就不会空闲。

本模型每个 SM 每周期发射**一条** warp 指令，调度器是 **loose round-robin**：从上次发射的下一个 warp 开始，挑第一个就绪（未停顿、未在 barrier 等待）的 warp。没有任何 warp 就绪时记一次 **idle issue slot**（`STATS_IDLE`）。

- **occupancy** = 驻留 warp 数 / warp 槽数。occupancy 越高，可供切换的 warp 越多，延迟隐藏越好。
- 试试把 *Global memory latency* 调大，并用 `BLOCK_X`/`GRID_X` 控制驻留 warp 数：观察 `STATS_IDLE` 与 `CYCLES` 的变化。`test/tst_gpu.cpp` 的 `tst_latencyHiding` 用同一个 kernel对比 1 个 warp 和 4 个 warp 的 IPC。

## 6. CPU 与 GPU 的提交路径

真实系统中：

1. 驱动把 kernel 代码和参数放进显存/统一内存；
2. CPU 向**命令队列（command queue / ring buffer）**写入 launch 命令（grid/block 尺寸、参数地址）；
3. 写**门铃（doorbell）**寄存器通知 GPU 前端；
4. GPU 的 block 调度器把 block 分发到各 SM；
5. 完成后 GPU 更新一个 **fence/信号量** 并（可选）发中断；CPU 通过轮询或等待中断得知完成；
6. 若 cache 不一致，还需要 flush/invalidate。

本模块把这条路径压缩为一组 MMIO 寄存器：`KERNEL_PC/GRID_X/BLOCK_X/ARG*` 相当于命令，`CTRL.START` 是门铃，`STATUS.DONE` 是完成信号，`CTRL.IRQ_EN`/`STATUS.IRQ` 模拟中断请求（Ripes 处理器模型目前没有外部中断输入，所以 IRQ 位只是"可观察"而不会打断 CPU）。**GPU 在 CPU 时钟的驱动下异步前进**（每个 CPU 周期推进 *GPU cycles per CPU cycle* 个 GPU 周期），所以 CPU 轮询 `STATUS` 的循环真的在和 GPU 并行。

## 7. 模型参数、调度与统计一览

| 参数（I/O tab 右侧） | 默认 | 说明 |
|---|---|---|
| SMs | 2 | SM 数量（1–8） |
| Warps per SM | 4 | 每个 SM 的 warp 槽数（1–16） |
| Warp size (lanes) | 8 | 每个 warp 的 lane 数（1–32） |
| Global memory latency (cycles) | 4 | `LDG` 后 warp 的停顿周期 |
| GPU cycles per CPU cycle | 1 | 每个 CPU 周期推进的 GPU 周期数（在 `applyParameters()` 时缓存到 `std::atomic<unsigned>`，每周期不再查参数表） |

每个 GPU 周期、每个 SM 的动作顺序：**派发 block → 选一个就绪 warp 发射 → 递减其它 warp 的停顿计数 → 回收结束的 warp**。统计量：`cycles`、`issued`（warp 指令数）、`idleSlots`、`divergences`、`memInstrs`/`memTransactions`、`coalescedInstrs/coalescedTxns`、`uncoalescedInstrs/uncoalescedTxns`、`bankConflicts`/`bankConflictCycles`、`barriers`（warp 到达屏障的次数，每个 warp 每次屏障只计一次）、`blocksLaunched/Completed`、`threadInstrs`（所有指令的执行 lane 数之和，`threadInstrs / (issued × warpSize)` 即 SIMD 效率）。

## 8. Kernel ISA

### 8.1 寄存器与执行模型

- 每 lane 16 个 32 位整数寄存器 **R0–R15**；**R0 恒为 0**（写入被忽略，和 RISC-V 的 x0 一样）。
- 每 lane 4 个谓词位 **P0–P3**，由 `SETP` 写入，由指令守卫 `@Pn`/`@!Pn` 读取。
- PC 以**指令索引**（相对于 `KERNEL_PC` 的字下标）计；分支目标是绝对索引。
- 所有运算为 32 位整数（无浮点，教学用）。

### 8.2 编码格式（32 位定长）

```
 bit  31  30  29..28  27..22  21..18  17..14  13..10  9..6   5..0
     +---+---+-------+-------+-------+-------+-------+------+------+
R:   |GE |GN | GP    |opcode |  rd   |  rs1  |  rs2  | rs3  |funct |
I:   |GE |GN | GP    |opcode |  rd   |  rs1  |         imm14        |
P:   |GE |GN | GP    |opcode | pidx  |  rs1  | cond  |    imm10     |
B:   |GE |GN | GP    |opcode |              target22               |
     +---+---+-------+-------+-------+-------+-------+------+------+
```

- **guard（bit 31–28）**：`GE`=守卫使能，`GN`=取反，`GP`=谓词编号。`@P1 ADD …` ⇒ GE=1,GN=0,GP=1（高 4 位 = `0x9`）；`@!P1` ⇒ `0xD`。无守卫时高 4 位为 0。
- `imm14`/`imm10` 符号扩展；`target22` 为绝对指令索引。
- `STG/STS` 的 `rd` 字段放**被存储的数据寄存器**。
- `SETP` 寄存器形式把条件码放在 `funct`，立即数形式（P 型）把条件码放在 bit 13–10、立即数只有 10 位（−512…511）。

### 8.3 操作码表

| opcode | 助记符 | 型 | 语义 |
|---|---|---|---|
| 0 | `NOP` | – | 空操作 |
| 1 | `EXIT` | – | 执行它的 lane 结束 |
| 2 | `BAR` | – | block 内 barrier：warp 等待，直到本 block 所有存活 warp 都到达 |
| 3 | `BRA target` | B | 通过守卫的 lane 跳到 `target`，其余 lane 顺序执行（可能发散） |
| 4 | `MOVS rd, sreg` | I | 读特殊寄存器：0 TID、1 BID、2 NTID、3 NBID、4–7 ARG0–3、8 LANEID、9 WARPID、10 SMID |
| 8 | `ADD rd, rs1, rs2` | R | rd = rs1 + rs2 |
| 9 | `SUB rd, rs1, rs2` | R | rd = rs1 − rs2 |
| 10 | `MUL rd, rs1, rs2` | R | 低 32 位乘积 |
| 11 | `FMA rd, rs1, rs2, rs3` | R | rd = rs1 × rs2 + rs3 |
| 12–14 | `AND` / `OR` / `XOR` | R | 位运算 |
| 15–17 | `SHL` / `SHR` / `SRA` | R | 左移 / 逻辑右移 / 算术右移（移位量取低 5 位） |
| 24–31 | `ADDI MULI ANDI ORI XORI SHLI SHRI SRAI rd, rs1, imm` | I | 立即数形式 |
| 32 | `SETP.cond pd, rs1, rs2` | R | pd = (rs1 cond rs2)，cond 在 funct |
| 33 | `SETP.cond pd, rs1, imm` | P | pd = (rs1 cond imm10) |
| 40 | `LDG rd, [rs1+imm]` | I | 全局内存读 32 位 |
| 41 | `STG [rs1+imm], rd` | I | 全局内存写 |
| 42 | `LDS rd, [rs1+imm]` | I | shared memory 读（地址相对 block 的 shared 起点，字节） |
| 43 | `STS [rs1+imm], rd` | I | shared memory 写 |

条件码 `cond`：0 `EQ`、1 `NE`、2 `LT`、3 `GE`、4 `LTU`、5 `GEU`、6 `LE`、7 `GT`（`LT/GE/LE/GT` 为有符号比较）。

### 8.4 汇编语法与伪指令

- 每行一条指令；`#`、`//`、`;` 开始注释；`label:` 定义标签（可与指令同行）。
- 寄存器 `R0..R15`，谓词 `P0..P3`，立即数支持十进制、负数、`0x` 十六进制。
- 内存操作数 `[R4]`、`[R4+8]`、`[R4-4]`。
- 第三操作数是立即数时自动选择 I 型：`ADD R1, R2, 5` ⇒ `ADDI`；`SUB R1, R2, 5` ⇒ `ADDI R1, R2, -5`。
- 伪指令：`MOV rd, rs`（`ADD rd, rs, R0`）、`MOV rd, imm`（`ADDI rd, R0, imm`）、`S_TID/S_BID/S_NTID/S_NBID/S_LANE/S_WARP/S_SMID rd`、`S_ARG rd, k`（均为 `MOVS`）。
- 守卫写在最前面：`@P0 BRA done`、`@!P1 EXIT`。

C++ 侧的操作码只有**一张表**（`simtgpu.cpp` 中的 `OpcodeTable`，按 6 位 opcode 索引，给出助记符与指令格式）；`decode()`、`opcodeForm()`、`opcodeName()` 都查这张表，不再有两份重复的 `switch`。

两套实现保证编码一致：C++ 的 `Ripes::gpu::assembleKernel()`（测试和 widget 反汇编用）与 `tools/gpu_asm.py`（给主机程序生成 `.word`）。`python tools/gpu_asm.py --selftest` 会用 vecadd 的参考机器码交叉校验。

### 8.5 编码示例（手工验证）

| 指令 | 字段 | 机器码 |
|---|---|---|
| `S_TID R1` | op=4, rd=1, imm=0 | `4<<22 \| 1<<18` = `0x01040000` |
| `MUL R4, R2, R3` | op=10, rd=4, rs1=2, rs2=3 | `0x02800000 \| 0x00100000 \| 0x8000 \| 0xC00` = `0x02908C00` |
| `SETP.GE P0, R4, R5` | op=32, rd=0, rs1=4, rs2=5, funct=3 | `0x08000000 \| 0x10000 \| 0x1400 \| 3` = `0x08011403` |
| `@P0 EXIT` | guard=`0x8`, op=1 | `0x80000000 \| 0x00400000` = `0x80400000` |
| `SETP.LT P0, R1, 4` | op=33, pidx=0, rs1=1, cond=2, imm=4 | `0x08400000 \| 0x4000 \| 0x800 \| 4` = `0x08404804` |
| `@P0 BRA 4` | guard=`0x8`, op=3, target=4 | `0x80C00004` |
| `MOV R2, -5` | op=24, rd=2, rs1=0, imm=−5 (`0x3FFB`) | `0x06083FFB` |

## 9. 外设寄存器表（`GPU_0_*`）

实例化外设后，汇编中可直接使用 `GPU_0_BASE`、`GPU_0_<REG>`（绝对地址）与 `GPU_0_<REG>_OFFSET`（相对偏移），C 程序 `#include "ripes_system.h"`。

| 偏移 | 名称 | 读写 | 说明 |
|---|---|---|---|
| 0x00 | `CTRL` | RW | bit0 `START`（写 1 启动，读为 0）、bit1 `IRQ_EN`、bit2 `ABORT`（写 1 终止 kernel）。**每次写 CTRL 都会用 bit1 重写 `IRQ_EN`**：只写 `START` 会把 `IRQ_EN` 清零，要保留中断使能请写 `START \| IRQ_EN` |
| 0x04 | `STATUS` | RW | bit0 `BUSY`、bit1 `DONE`（粘滞）、bit2 `ERROR`（粘滞）、bit3 `IRQ`（= DONE & IRQ_EN）、bit4 `LAUNCH_IGNORED`（粘滞：BUSY 时又写了 `CTRL.START`）；向 bit1/bit2/bit4 写 1 清除 |
| 0x08 | `KERNEL_PC` | RW | kernel 第一条指令的字节地址 |
| 0x0C | `GRID_X` | RW | block 数 |
| 0x10 | `BLOCK_X` | RW | 每 block 线程数 |
| 0x14–0x20 | `ARG0`–`ARG3` | RW | kernel 参数（`S_ARG rd, k` 读取） |
| 0x24 | `CYCLES` | R | 本次 kernel 的 GPU 周期数 |
| 0x28 | `STATS_INSTRS` | R | 发射的 warp 指令数 |
| 0x2C | `STATS_DIVERGE` | R | 发散事件数 |
| 0x30 | `STATS_MEMTXN` | R | 32 B 全局内存事务数 |
| 0x34 | `STATS_IDLE` | R | 空闲发射槽数 |
| 0x38 | `CONFIG` | R | `[7:0]` warp size、`[15:8]` warps/SM、`[23:16]` SM 数 |

额外导出的符号：`GPU_0_CTRL_START`(1)、`GPU_0_CTRL_IRQ_EN`(2)、`GPU_0_CTRL_ABORT`(4)、`GPU_0_STATUS_BUSY`(1)、`GPU_0_STATUS_DONE`(2)、`GPU_0_STATUS_ERROR`(4)、`GPU_0_STATUS_IRQ`(8)、`GPU_0_STATUS_LAUNCH_IGNORED`(16)、`GPU_0_NUM_SMS`、`GPU_0_WARPS_PER_SM`、`GPU_0_WARP_SIZE`。

**运行中再次写 `CTRL.START`**：GPU 忙（BUSY）时的门铃被**忽略**——正在运行的 kernel、它的参数和错误状态都不受影响，只置位粘滞的 `STATUS.LAUNCH_IGNORED`，widget 上显示"launch ignored"提示并累计次数（第一阶段会错误地把正在运行的 kernel 标成 ERROR）。

**kernel 访问 GPU 自己的寄存器区**：若 kernel 的 `LDG/STG`（或 `KERNEL_PC` 本身）指向 `GPU_0_BASE` 起的寄存器窗口，总线会回调到 `IOGpu::ioRead/ioWrite`。处理器线程此时正持有模型的互斥锁；第一阶段这里会死锁。现在 `IOGpu` 在加锁**之前**用 `m_busThread`（正在代表模型访问总线的线程 id）识别这种重入，拒绝该访问并通过 `SimtGpu::fault()` 让 kernel 以 `STATUS.ERROR` 结束，错误原因为 "kernel read/write ... GPU's own register window"（`tst_accel_e2e::testGpuSelfAccessNoDeadlock` 是回归测试）。

启动序列（见 `examples/assembly/gpu_vecadd.s`）：

```asm
li   s0, GPU_0_BASE
la   t0, kernel
sw   t0, GPU_0_KERNEL_PC_OFFSET(s0)
sw   grid,  GPU_0_GRID_X_OFFSET(s0)
sw   block, GPU_0_BLOCK_X_OFFSET(s0)
sw   &A, GPU_0_ARG0_OFFSET(s0)   # ... ARG1..ARG3
li   t0, GPU_0_CTRL_START
sw   t0, GPU_0_CTRL_OFFSET(s0)   # doorbell
wait:
lw   t0, GPU_0_STATUS_OFFSET(s0)
andi t0, t0, GPU_0_STATUS_DONE
beqz t0, wait
li   t0, GPU_0_STATUS_DONE
sw   t0, GPU_0_STATUS_OFFSET(s0) # W1C
```

## 10. 实例：vecadd 的逐周期执行

kernel（`examples/assembly/gpu_vecadd.s` 中的注释即此源码）：

```
 0  S_TID   R1            # threadIdx
 1  S_BID   R2            # blockIdx
 2  S_NTID  R3            # blockDim
 3  MUL     R4, R2, R3
 4  ADD     R4, R4, R1    # i
 5  S_ARG   R5, 3         # N
 6  SETP.GE P0, R4, R5    # P0 = i >= N
 7  @P0 EXIT              # 尾部线程退出（谓词，不发散）
 8  SHL     R4, R4, 2
 9  S_ARG   R6, 0         # A
10  S_ARG   R7, 1         # B
11  S_ARG   R8, 2         # C
12  ADD     R6, R6, R4
13  ADD     R7, R7, R4
14  ADD     R8, R8, R4
15  LDG     R9, [R6]
16  LDG     R10, [R7]
17  ADD     R11, R9, R10
18  STG     [R8], R11
19  EXIT
```

为了把表格放得下，用一个很小的配置：**1 SM × 2 warp 槽 × 4 lane，memLatency = 3，N = 6，BLOCK_X = 4，GRID_X = 2**。block 0 = 线程 0–3（W0），block 1 = 线程 4–7（W1，其中线程 6、7 的 i ≥ N）。下表由 `SimtGpu::step()` 实际跑出（每行是该周期**结束后**的状态；"pc" 是下一条要执行的指令，mask 的最低位是 lane 0）：

| cycle | issue | W0 pc / active | W0 执行的指令 (exec mask) | W1 pc / active | W1 执行的指令 (exec mask) |
|---|---|---|---|---|---|
| 1 | yes | 1 / 1111 | S_TID R1 (1111) | – | –（block 1 下个周期才派发） |
| 2 | yes | 1 / 1111 | – | 1 / 1111 | S_TID R1 (1111) |
| 3 | yes | 2 / 1111 | S_BID R2 (1111) | 1 / 1111 | – |
| 4 | yes | 2 / 1111 | – | 2 / 1111 | S_BID R2 (1111) |
| … | yes | 轮转发射：W0、W1 交替，每周期一条 | | | |
| 14 | yes | 7 / 1111 | – | 7 / 1111 | SETP.GE P0 (1111) |
| 15 | yes | 8 / 1111 | **@P0 EXIT (exec 0000)** | 7 / 1111 | – |
| 16 | yes | 8 / 1111 | – | 8 / **1100** | **@P0 EXIT (exec 0011)** |
| 17–30 | yes | … | 地址计算 | … / 1100 | 地址计算，只有 2 个 lane 工作 |
| 31 | yes | 16 / 1111 **stall=3** | LDG R9 (1111) | 15 / 1100 | – |
| 32 | yes | 16 / 1111 stall=2 | – | 16 / 1100 **stall=3** | LDG R9 (1100) |
| 33 | **idle** | 16 / 1111 stall=1 | – | 16 / 1100 stall=2 | – |
| 34 | **idle** | 16 / 1111 | – | 16 / 1100 stall=1 | – |
| 35 | yes | 17 / 1111 stall=3 | LDG R10 (1111) | 16 / 1100 | – |
| 36 | yes | 17 / 1111 stall=2 | – | 17 / 1100 stall=3 | LDG R10 (1100) |
| 37 | idle | 17 / 1111 stall=1 | – | 17 / 1100 stall=2 | – |
| 38 | idle | 17 / 1111 | – | 17 / 1100 stall=1 | – |
| 39 | yes | 18 / 1111 | ADD R11 (1111) | 17 / 1100 | – |
| 40 | yes | 18 / 1111 | – | 18 / 1100 | ADD R11 (1100) |
| 41 | yes | 19 / 1111 | STG (1111) | 18 / 1100 | – |
| 42 | yes | 19 / 1111 | – | 19 / 1100 | STG (1100) |
| 43 | yes | –（W0 结束） | EXIT | 19 / 1100 | – |
| 44 | yes | – | – | –（W1 结束） | EXIT |

最终：`cycles=44, issued=40, idle=4, divergences=0, memInstrs=6, memTransactions=6, threadInstrs=130`，`C[0..5] = 0,11,22,33,44,55`，`C[6]`、`C[7]` 未被写入。

从这张表能读出的要点：

1. **谓词退出不发散**：周期 15/16 两个 warp 都执行了 `@P0 EXIT`，W0 没有 lane 满足（exec 0000，但仍占用一个发射槽），W1 的 lane 2、3 退出，之后 W1 以 mask `1100` 继续，SIMD 效率减半。
2. **合并访存**：每条 `LDG` 4 个 lane 读连续 16 B，落在同一个 32 B 段，所以 6 条访存指令 = 6 个事务。
3. **延迟隐藏**：W0 在周期 31 发射 `LDG` 后停顿 3 周期，周期 32 调度器立刻发射 W1 —— 但只有 2 个 warp，不够掩盖 3 周期延迟，于是周期 33、34 出现 idle。若有 4 个 warp，这些空槽会被填满（试试把 N 调到 16、BLOCK_X = 4、GRID_X = 4 并把 *Warps per SM* 调到 4）。
4. **SIMD 效率** = threadInstrs / (issued × warpSize) = 130 / 160 ≈ 81%，损失来自 W1 的两个尾部 lane。

### 10.1 一个真正发散的例子

```
 0  S_TID R1
 1  ANDI R2, R1, 1
 2  SETP.NE P0, R2, 0
 3  @P0 BRA odd          # 奇数 lane 跳走
 4  MOV R3, 100          # 偶数路径
 5  BRA join
 6  odd:  MOV R3, 200    # 奇数路径
 7  join: S_ARG R4, 0
 8  SHL R5, R1, 2
 9  ADD R4, R4, R5
10  STG [R4], R3
11  EXIT
```

8 lane 的 warp 上，`SimtGpu` 的执行序列（pc : exec mask）为：

```
0:FF 1:FF 2:FF 3:AA | 4:55 5:55 | 6:AA | 7:FF 8:FF 9:FF 10:FF 11:FF
```

pc 3 的分支把路径拆成 `(4, 0x55)` 与 `(6, 0xAA)`；min-PC 先跑 pc 4、5（偶数 lane），`BRA join` 使它变成 `(7, 0x55)`，此时 `(6, 0xAA)` 的 pc 更小，于是切换到奇数路径；pc 6 执行完后它也到了 7，两项 pc 相等 ⇒ 合并成 `(7, 0xFF)`，之后全 warp 一起执行。这就是 `tst_divergenceReconvergence` 检查的序列。

## 11. 使用教程与 GPU 外设部件（widget）

1. **实例化外设**：**I/O** 标签页 → 双击 "SIMT GPU"。外设标签页的 *Parameters* 表可调 SM 数、warp 数、lane 数、访存延迟和 GPU/CPU 时钟比。
2. **加载示例**：`File → Load Example → Assembly → gpu_vecadd.s`，汇编。
3. **运行**：按 Run（或用 Clock 单步）。GPU 部件（`src/io/iogpuwidget.*`）从上到下是：
   - **标题栏**：标题 "SIMT GPU"、一句话说明、右侧 "?" 按钮（打开用户手册 `09-gpu` 章，`Ripes::showHelp("09-gpu")`）。
   - **状态条**：状态徽章 **IDLE / BUSY / DONE / ABORTED / ERROR**（颜色 + 文字双重表达；kernel 运行中一律显示 BUSY；被 `CTRL.ABORT` 终止后显示 ABORTED 而不是 DONE；主机用 W1C 确认 DONE 之后回到 IDLE），`GPU cycles: N`，网格信息 `Grid: 2 block(s) x 16 thread(s) · pending blocks`，右侧是硬件配置与 GPU/CPU 时钟比。出错时下面一行红字给出**错误原因**；忽略的启动、反向执行等事件以 "ⓘ" 提示显示。
   - **空状态提示**：还没有 launch 时，warp 区域显示"尚未启动 kernel，请写 KERNEL_PC/GRID_X/BLOCK_X/ARG0-3 再写 CTRL.START"的提示。
   - **SM / warp / lane 视图**（可滚动）：每个 SM 一个框，标题是常驻 block 数、已发射、空闲槽和发射利用率；每个 warp 槽一行：`W<槽> B<block>.<warp>`、**下一条 pc 与该指令的反汇编**、每个 lane 一个方块、以及文字状态（`issued this cycle`、`waiting at BAR`、`stall n`、`diverged: k paths`）。每个 SM 每周期最多发射一条 warp 指令，**本周期发射的那个 warp 的行**用浅色底 + 行首竖条高亮，只有这一行的 lane 带外框（标出真正执行了这条指令的 lane），因此一眼能看出"每个 SM 每周期只发射一个 warp"。**发散的 warp 整行用浅橙色底色高亮**。方块的含义同时用颜色和形状表达：绿色实心 = 活跃；灰底斜线 + 实线边框 = 被 mask 掉（线程存在，但在等待另一条路径重汇聚）；虚线空框（无填充）= 没有线程（block 小于 warp）；橙色带"暂停"竖条 = 停在 barrier；红色带 × = 已 EXIT。下方有**图例**逐一解释。鼠标悬停 lane 方块显示 SM/warp/lane、threadIdx.x、状态以及该 lane 的 R1–R15 和 P0–P3；悬停行首显示该 warp 的所有控制流路径 (pc, mask)。
   - **Kernel 反汇编**（左下）：`CTRL.START` 时从 `KERNEL_PC` 读取 kernel（遇到"不是规范编码的字"为止——即反汇编后再汇编得不到同一个字，例如 kernel 后面的数据；最多 256 条），列出序号、地址、机器码、指令；**至少有一个 warp 的下一条 pc 落在该行时整行高亮**，最后一列列出这些 warp（`S0W1`，停在屏障的加 `(BAR)`）。
   - **Statistics**（右下标签页）：GPU 周期、warp 指令、线程指令、IPC、SIMD 效率（= 线程指令数 / (warp 指令数 × warp 大小)）、发射利用率、空闲槽、发散次数、访存指令/事务、合并与非合并访问（指令数/事务数）、bank 冲突（指令数/额外周期）、屏障到达次数、完成的 block、被忽略的 launch、反向执行历史——每行都有单位/含义列，并标明来源：`[register CYCLES]` 等表示主机程序也能从该寄存器读到；**`[widget only]` 表示只在部件里显示、没有对应寄存器**（线程指令、IPC、SIMD 效率、访存指令数、合并/非合并、bank 冲突、屏障、block 数等）。
   - **Registers**（右下标签页）：寄存器表"名字 + 偏移 + 当前值（含位域解码，如 `0x00000002 (DONE)`）+ 含义"。
   - 布局用 `QSplitter` + 滚动区，窗口缩小时不会截断；颜色从 `QPalette` 派生并监听 `Ripes::ThemeManager::themeChanged`，深浅色主题都可读。所有控件都有 tooltip 与 statusTip；所有字符串用 `tr()`，中文翻译在 `resources/i18n/iogpu_zh_CN.ts`。
   - 刷新节流：部件按全局 UI 刷新率（设置项 `RIPES_SETTING_UIUPDATEPS`，默认 25 次/秒）轮询 `IOGpu::version()`，只有状态变化时才重建视图；处理器线程从不逐周期向 GUI 线程投递事件。
4. **单步观察**：用 **Clock（F5，单步时钟）** 逐周期前进，而不是 Run 或 **Auto clock（F6，自动连续时钟）**。每按一次 F5，GPU 前进 *GPU cycles per CPU cycle* 个周期，可以逐周期对照第 10 节的表格。
5. **写自己的 kernel**：把助记符源码存成 `foo.gpu`，运行 `python tools/gpu_asm.py foo.gpu` 得到 `.word` 行，粘贴到主机程序的 `.data` 段；用 `ARG0–3` 传指针和长度。`-d` 选项可反汇编机器码。
6. **做实验**：
   - 把 `MULI R2, R1, 4` 改成 `MULI R2, R1, 32`（跨步访问），比较 Statistics 中"合并/非合并访问"、`STATS_MEMTXN` 和 `CYCLES`；
   - 把访存延迟调到 20，比较 block 数不同（驻留 warp 不同）时的 `STATS_IDLE`；
   - 写一个带 if/else 的 kernel，看发散 warp 的高亮行和 `STATS_DIVERGE`，再改用谓词消除分支。

### 11.1 反向执行（Reverse）

Ripes 的 **Reverse** 会让处理器回退一个周期，外设也必须跟着回退。本模块选择方案 (a)：**每周期快照**，并以方案 (b) 作为兜底。

- **快照内容**：`SimtGpu::State`（所有 SM 的 warp 槽：寄存器、谓词、路径、停顿计数；常驻 block 及其 shared memory；统计量；launch 参数；忙/错误状态）加上 `IOGpu` 的主机可见寄存器（`KERNEL_PC`、`GRID_X`… 以及 DONE/ERROR/LAUNCH_IGNORED 粘滞位、kernel 反汇编缓存）。
- **快照大小（实测，`tst_gpu::tst_stateSnapshot` 与 `tst_accel_e2e::testGpuReverse` 打印）**：默认配置（2 SM × 4 warp × 8 lane，vecadd 运行中，2 个 block 常驻）**5 587 字节/帧**；最大配置（8 × 16 × 32，全部占满）**420 143 字节/帧**（`SimtGpu::stateBytes()` 实测）。默认回退深度 `RIPES_SETTING_REWINDSTACKSIZE` = 100，所以默认配置下历史最多约 0.6 MB，最大配置约 42 MB（仍低于 64 MB 预算）。因此选择方案 (a)。
- **有界队列**：`processorClocked` 之后压入一帧，最多保留 `REWINDSTACKSIZE + 1` 帧，并且总字节数不超过 `IOGpu::kHistoryBudgetBytes`（64 MB），超出时丢弃最旧的帧。**状态没有变化的周期**（GPU 空闲、也没有写寄存器）不复制状态，只共享上一帧的指针（8 字节）。
- **`ProcessorHandler::processorReversed`**：弹出一帧并恢复到上一帧，widget 显示"GPU state restored to the previous CPU cycle"。
- **兜底**：如果回退超出了记录的历史（例如字节预算把旧帧丢掉了），GPU 被复位到**启动前状态**（保留 launch 寄存器，清除 kernel、DONE/ERROR），并在 widget 上明示"Reverse execution went past the recorded GPU history: the accelerator was reset to its pre-launch state"。
- **限制**：kernel 已经用 `STG` 写入全局内存的数据**不会被撤销**（外设通过 `memWrite` 直接写地址空间，不在处理器的回退日志里）。重新执行同一个 kernel 会写入相同的值，所以对 vecadd 这类"输出只依赖输入"的 kernel 结果不变。

## 12. 常见误区

- **"warp 里的线程是真正独立的"** —— 不是。它们共享 PC，分支发散时串行执行各路径；`if (tid % 2)` 这类代码会让一半 lane 空转。真实 GPU 上 Independent Thread Scheduling 也只是让发散路径可以交错，并不会让它们同时执行。
- **"block 越大越好"** —— block 必须整个放进一个 SM；太大的 block 会 launch 失败（本模型）或限制 occupancy（真实 GPU）。另一方面，太小的 block（小于 warp size）会浪费 lane。
- **"BAR 能同步整个 grid"** —— `BAR` 只在 block 内生效；不同 block 之间没有执行顺序保证，跨 block 同步需要结束 kernel 或使用原子操作。
- **"在发散的分支里用 BAR 是安全的"** —— 不安全。`BAR` 要求 block 内所有存活 warp 都到达；若某条路径里只有部分 lane 执行 `BAR`，而另一条路径不执行，结果未定义（本模型按"warp 到达"计数：发散的两条路径都要停靠在 `BAR` 或退出，warp 才算到达一次，因此不会提前释放；但如果某条路径永远到不了 `BAR` 又不退出，block 就会一直等待）。
- **"访存延迟高所以 GPU 慢"** —— GPU 用并行度隐藏延迟：只要有足够多的就绪 warp，长延迟不会体现为空闲槽。看 `STATS_IDLE` 而不是看单条指令的延迟。
- **"每个 lane 读 4 B，带宽就只用了 4 B"** —— 总线按 32 B 段传输；不合并的访问会把带宽浪费在没人用的字节上。看 `STATS_MEMTXN / memInstrs` 的比值。
- **"`STATUS.DONE` 会自动清零"** —— 不会，它是粘滞位，必须写 1 清除（W1C）；否则下次 launch 前读到的 DONE 是上一次的。
- **"kernel 可以访问 CPU 的寄存器或栈"** —— 不行。GPU 只看得到内存；所有输入输出都要通过 `ARG*` 传地址，并在内存里交换。
- **"min-PC 规则就是真实硬件"** —— 它是教学简化。真实 GPU 用编译器标注的重汇聚点（SIMT stack）或每线程 PC（ITS）；对结构化代码三者行为一致，对非结构化跳转（例如交叉的 `goto`）结果可能不同。

## 13. 文件索引

| 文件 | 内容 |
|---|---|
| `src/accel/gpu/simtgpu.h/.cpp` | Qt-free 仿真核、ISA 编解码、汇编器/反汇编器 |
| `src/io/iogpu.h/.cpp` | MMIO 外设 `IOGpu`：寄存器、重入检测、反向执行历史、给 GUI 的 `View` |
| `src/io/iogpuwidget.h/.cpp` | 可视化部件 `IOGpuWidget`（状态条、warp 视图、图例、反汇编、统计、寄存器表） |
| `resources/i18n/iogpu_zh_CN.ts` | GPU 部件的中文翻译 |
| `tools/gpu_asm.py` | 独立的 Python 汇编器/反汇编器（与 C++ 编码一致） |
| `examples/assembly/gpu_vecadd.s` | RISC-V 主机程序示例 |
| `test/tst_gpu.cpp` | 编码、操作码表、汇编、vecadd、发散/重汇聚、循环、barrier（含发散 warp 的屏障）、运行中再次 launch、总线错误、合并统计、快照/恢复、延迟隐藏、错误处理的单元测试 |
| `test/tst_accel_e2e.cpp` | 端到端：示例程序、寄存器级 launch（kernel 由 `gpu::assembleKernel` 从源码生成）、示例机器码与源码一致性、自映射访问不死锁、BUSY 时 launch 被忽略、反向执行、widget 离屏渲染 |
