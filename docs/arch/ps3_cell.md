# PS3 与 Cell Broadband Engine 架构

> 本文是 open-soc-explorer「架构浏览器」模块 D 的配套文档。对应的拓扑/数据流描述在
> `resources/arch/ps3.json`，可在 Architecture tab 中逐步播放；对应的 Ripes 示例程序在
> `examples/C/spe_double_buffer.c`。GPU SIMT 与 NPU 的对照文档分别见 `gpu.md` 与
> `tensor_cpu.md`。
>
> 数值来源：IBM《Cell Broadband Engine Programming Handbook》、Kahle 等《Introduction to the
> Cell multiprocessor》(IBM JRD 2005)、Chen 等《Cell BE Architecture and its first
> implementation — a performance view》(IBM JRD 2007)、Hot Chips 17 IBM 演示稿，以及 Sony
> 官方开发资料的公开转述。少数 PS3 整机数据（南桥带宽等）只有二手来源，文中已标注。

![PS3 / Cell BE 系统框图（Architecture tab 渲染，`resources/arch/ps3.json`）](images/ps3.png)

---

## 1. 系统概览（2006）

PlayStation 3 于 2006 年 11 月发售。它是 Sony、Toshiba、IBM（STI 联盟）从 2001 年起合作
研发的 **Cell Broadband Engine (Cell BE)** 的旗舰应用，也是消费级产品中第一次把
「一个通用核心 + 一组显式管理本地存储的向量协处理器」这种 **异构多核** 结构推向大众。

| 项目 | 数值 |
|---|---|
| CPU | Cell BE，3.2 GHz，90 nm SOI（2006）→ 65 nm（2007 末）→ 45 nm（2009 Slim） |
| 通用核 | 1× PPE（PowerPC 64-bit，2-way SMT） |
| 协处理器 | 8× SPE（物理）；PS3 上启用 7 个（良率），其中 1 个被 Hypervisor/GameOS 保留，**6 个给游戏** |
| 片上互连 | EIB（Element Interconnect Bus），4 环 × 16 B |
| 主存 | 256 MB Rambus XDR DRAM，双通道，25.6 GB/s |
| GPU | RSX「Reality Synthesizer」，NVIDIA G70/NV47 派生，500 MHz |
| 显存 | 256 MB GDDR3，128-bit，650 MHz（1.3 Gbps/pin），20.8 GB/s |
| CPU↔GPU | Rambus FlexIO（IOIF0），Cell→RSX 20 GB/s，RSX→Cell 15 GB/s |
| 南桥 | FlexIO IOIF1 → 南桥（USB、BD 光驱、SATA HDD、GbE、Wi‑Fi、蓝牙、闪存读卡器） |
| 输出 | RSX → HDMI 1.3a / 模拟 AV（1080p） |

常见的「Cell ≈ 230 GFLOPS」口径是按 8 个 SPE 算的：PPE 的 VMX 约 25.6 GFLOPS，每个 SPE
25.6 GFLOPS（4 路 SIMD × FMA 2 flops × 3.2 GHz），8 × 25.6 + 25.6 = 230.4 GFLOPS。PS3 上只有
7 个 SPE 启用，对应 204.8 GFLOPS（游戏可用 6 个则是 179.2 GFLOPS）；再加 RSX 的 shader 算力
就是宣传中「接近 2 TFLOPS」的来源（其中 RSX 部分口径非常宽松）。

```
                         +-----------------------------------------------+
                         |                 Cell BE (3.2 GHz)             |
                         |  SPE0 SPE1 SPE2 SPE3 SPE4 SPE5 SPE6 [SPE7 x]   |
                         |   |    |    |    |    |    |    |              |
                         |  =====+====+====+====+====+====+====+======   |
                         |        EIB: 4 rings x 16 B, 1.6 GHz           |
                         |  =====+==========+=================+=======   |
                         |       |          |                 |          |
                         |      PPE        MIC           BEI/FlexIO      |
                         +-------|----------|-----------------|----------+
                                 |          |          IOIF0  |  IOIF1
                               (L2)    256 MB XDR        +----+----+
                                       25.6 GB/s         |         |
                                                        RSX     South Bridge
                                                     500 MHz      USB/BD/HDD/
                                                         |        Eth/WiFi/BT
                                                   256 MB GDDR3
                                                     20.8 GB/s
                                                         |
                                                     HDMI / AV
```

---

## 2. Cell BE 总体结构

Cell 的设计哲学可以浓缩为三句话：

1. **把控制与计算分离**：一个 PPE 管调度、OS 和串行代码；8 个 SPE 做数据并行计算。
2. **用软件管理的 Local Store 取代 cache**：SPE 不能直接访问主存，所有数据必须由程序员
   显式地用 DMA 搬进搬出 256 KB 的 Local Store（LS）。这消除了 cache 的面积、功耗和
   不确定性，但把「内存墙」的全部复杂度交给了程序员。
3. **用高带宽环形总线把一切连起来**：EIB 连接 12 个单元（PPE、8 SPE、MIC、2 个 BEI
   端口），用 DMA 流量而不是 cache 一致性流量来喂饱它。

```
   PPE               SPE (x8)
 +--------+        +-----------------------------+
 | PPU    |        |  SPU (SXU)                  |
 | L1I/L1D|        |   128 x 128-bit regs        |
 | 32K/32K|        |   even pipe | odd pipe      |
 +--------+        |        |         |          |
 | L2 512K|        |   +----v---------v------+   |
 +---+----+        |   |  Local Store 256KB  |   |
     |             |   +----------+----------+   |
     |             |              | 128 B/cycle   |
     |             |   +----------v----------+   |
     |             |   | MFC: DMA ctrl, MMU, |   |
     |             |   |  queues, mailboxes  |   |
     |             |   +----------+----------+   |
     |             +--------------|--------------+
     |                            |
 ====+============================+========= EIB =====
```

### 2.1 PPE（Power Processing Element）

| 项目 | 说明 |
|---|---|
| ISA | 64-bit PowerPC Architecture v2.02（与 PowerPC 970/G5 同一 ISA 族），含 **VMX/AltiVec** 128-bit SIMD |
| 微架构 | **顺序 (in-order)、双发射 (dual-issue)**，流水线约 23 级，设计目标是高频而非高 IPC |
| 多线程 | **2-way SMT**：两个硬件线程交替取指，用来掩盖顺序流水线的停顿 |
| L1 | 32 KB I-cache + 32 KB D-cache |
| L2 | 512 KB，8 路组相联，写回；PPE 是 Cell 中唯一带 cache 层次的核 |
| 职责 | 运行 Hypervisor (lv1)、GameOS (lv2)、游戏主线程、把任务派发给 SPE |

PPE 的单线程性能平平（顺序执行、长流水、无乱序），大约相当于同频 PowerPC 970 的一半
不到。这不是失误而是取舍：面积预算被优先给了 8 个 SPE。开发者的经验是「把所有能向量化
的东西都搬到 SPE，PPE 只做必须串行的部分」。

### 2.2 SPE（Synergistic Processing Element）

每个 SPE = **SPU**（Synergistic Processing Unit，执行单元）+ **LS**（Local Store）+
**MFC**（Memory Flow Controller，DMA 引擎 + MMU + 通信寄存器）。

#### SPU 执行单元

| 项目 | 说明 |
|---|---|
| 寄存器 | **128 个 128-bit 统一寄存器**（整数、浮点、地址、条件全放在同一个文件里）|
| 数据类型 | 16×8b / 8×16b / 4×32b / 2×64b 整数；4×fp32；2×fp64（首代 fp64 极慢，约 1.8 GFLOPS/SPE）|
| 发射 | 顺序双发射：**even pipeline**（定点、浮点、逻辑、移位、比较）+ **odd pipeline**（load/store、permute/shuffle、分支、channel 指令）|
| 峰值 | 4×fp32 FMA / cycle = 8 flop/cycle × 3.2 GHz = **25.6 GFLOPS**（单精度）|
| 取指 | 从 LS 取指；LS 单端口，**DMA > load/store > 取指** 的优先级，取指缺口可能导致停顿 |
| 分支 | **无硬件分支预测器**；默认预测「不跳」，错预测代价约 18–19 周期。编译器/程序员用 **branch hint** 指令（`hbr`、`hbra`、`hbrr`）提前至少约 11 周期告诉取指单元目标地址，才能零惩罚跳转 |
| 延迟 | LS load 6 周期，fp32 乘加 6 周期；完全由编译器调度，没有乱序 |

SPU 的所有访存指令只能访问 LS（18 位地址，256 KB 回绕）。**SPU 根本没有通向主存的
load/store 指令**；它看到的「世界」就是 256 KB。

#### Local Store（LS）

| 项目 | 说明 |
|---|---|
| 容量 | 256 KB（SRAM，ECC），代码 + 数据 + 栈 + DMA 缓冲全部共享 |
| 带宽 | SPU load/store 每周期 16 B；DMA 每周期 128 B |
| 端口 | 单端口；DMA 与 load/store 争用（DMA 每 128 B 占用 1 周期，影响很小）|
| 地址 | 既是 SPU 的本地地址空间（LSA），也在 PPE 的实地址空间中有映射（Problem-state MMIO），所以 PPE/其他 SPE 可以 DMA 到它 |
| 没有 | 没有 cache，没有 TLB（对 SPU 而言），没有保护；越界就是回绕 |

#### MFC（Memory Flow Controller）

MFC 是 SPE 与外界的**唯一**通道，SPU 通过 **channel 指令**（`rdch`/`wrch`/`rchcnt`）
访问它，PPE 通过 **MMIO 寄存器** 访问它。

| 资源 | 规格 |
|---|---|
| DMA 命令队列 | **SPU 命令队列 16 项**（SPU 发起）+ **Proxy 命令队列 8 项**（PPE/其他设备经 MMIO 发起）|
| 单次传输 | 1、2、4、8 B 或 **16 B 的倍数，最大 16 KB**；LSA 与 EA 的低 4 位必须相同（自然对齐）；**128 B 对齐**且大小为 128 B 倍数时性能最佳（正好一条 cache line / 一个 EIB 事务）|
| DMA list | 一条 `getl`/`putl` 命令带一个 **最多 2048 个元素** 的列表（每元素 ≤16 KB，列表放在 LS 内），用来做 scatter/gather，等价于最多 32 MB 的一次性搬运 |
| Tag group | 每条命令带 **5 位 tag（0–31）**；`MFC_WrTagMask` 写要等待的 tag 位掩码，`MFC_WrTagUpdate` 指定「任一/全部完成」，`MFC_RdTagStat` 读完成状态（阻塞）|
| 栅栏 | `getf/putf`（fence：本 tag 组内先前命令完成后才执行）、`getb/putb`（barrier：本 tag 组内先前**和后续**命令都被排序）|
| Mailbox | **SPU inbound mailbox 4 项**（PPE 写 → SPU 读，`SPU_RdInMbox`）；**SPU outbound mailbox 1 项**（SPU 写 → PPE 读，`SPU_WrOutMbox`）；**SPU outbound interrupt mailbox 1 项**（SPU 写 → 触发 PPE 中断，`SPU_WrOutIntrMbox`）。每项 32 bit |
| Signal notification | **2 个 32-bit 寄存器**（`SigNotify1/2`）；可配置为 **overwrite** 模式或 **OR** 模式（多个发送者各置一位）|
| MMU | MFC 内有 SLB/TLB，用 PPE 相同的页表把 EA 翻译成实地址，所以 DMA 用的是**虚拟地址**，并受保护 |
| 原子 | `getllar`/`putllc`/`putlluc`（load-and-reserve / store-conditional 的 128 B cache line 版本）用于 PPE/SPE 间的锁 |

**Problem state 与 isolation mode**：PPE 可以把 SPE 的 MMIO 寄存器分成 problem-state
（用户态可访问：mailbox、signal、proxy DMA、运行控制）和 privilege 1/2 state（只有
Hypervisor/OS 能动：MMU、中断路由、隔离控制）。**Isolation mode** 下 SPE 的 LS 对外部
（包括 PPE 和 Hypervisor）**完全不可见**，只有经过硬件认证的 secure loader 能载入代码；PS3
的安全子系统（解密、签名验证）就跑在隔离的 SPE 上，这也是 PS3 安全体系比对手更坚固的原因
之一（直到 2010 年 Hypervisor 被从外围攻破）。

### 2.3 EIB（Element Interconnect Bus）

```
   PPE   SPE1  SPE3  SPE5  SPE7  IOIF1
    |     |     |     |     |     |
  ==+=====+=====+=====+=====+=====+==>   ring 0 (clockwise)
  <=+=====+=====+=====+=====+=====+==    ring 1 (counter-clockwise)
  ==+=====+=====+=====+=====+=====+==>   ring 2 (clockwise)
  <=+=====+=====+=====+=====+=====+==    ring 3 (counter-clockwise)
    |     |     |     |     |     |
   MIC   SPE0  SPE2  SPE4  SPE6  IOIF0           (物理上是环, 此处展开画)
```

| 项目 | 说明 |
|---|---|
| 拓扑 | **4 条 16 B 宽的单向环**：2 条顺时针 + 2 条逆时针；外加一条**地址/命令总线**（树状 address concentrator，负责 snoop 与一致性）|
| 节点 | 12 个：PPE、8 SPE、MIC、BEI 的 2 个端口（IOIF0/IOIF1）；每节点每周期可发 16 B + 收 16 B |
| 并发 | 每环最多 **3 个并发传输**（只要路径不重叠）→ 全片 12 个传输 |
| 时钟 | 总线时钟是核心时钟的一半（1.6 GHz），每 2 个核心周期搬一拍 |
| 峰值 | 12 传输 × 16 B / 2 周期 = **96 B / 核心周期** → 307.2 GB/s 瞬时峰值；IBM 给出的**可持续峰值为 204.8 GB/s**（受仲裁与节点接口限制），实测典型 190+ GB/s |
| 仲裁 | 中央 data arbiter 选择最短方向、避免传输跨越环的一半以上（否则会阻塞更多节点）|
| 一致性 | 仅 PPE L2 与 MFC 的原子单元参与 cache 一致性；普通 DMA 流量不经过 PPE cache，但 snoop 会保证 DMA 读到 PPE 已写回的数据 |

### 2.4 MIC 与 XDR DRAM

MIC（Memory Interface Controller）接 **2 通道 Rambus XDR**，每通道 32-bit、3.2 Gbps/pin，
合计 64-bit × 3.2 GHz = **25.6 GB/s**。256 MB 是 PS3 的「主存」：PPE 代码/数据、SPE
DMA 源/目标、RSX 命令缓冲都在这里。读 vs 写的带宽分配不是对称的，XDR 的突发切换成本
比 DDR 低，所以 DMA 密集的流式负载能跑到接近峰值。

### 2.5 BEI / FlexIO

BEI（Broadband Engine Interface，文献中也叫 BIC）实现 Rambus **FlexIO** 并行差分接口，
物理上是 12 个 byte lane（7 出 + 5 入，6.4 Gbps/pin 时合计 76.8 GB/s）。Cell 把它配置成
两个逻辑接口：

| 接口 | 对端 | 带宽（PS3 配置）|
|---|---|---|
| IOIF0（coherent 可选）| RSX | Cell→RSX **20 GB/s**，RSX→Cell **15 GB/s**（实测约 15.8 / 11.9 GB/s @128 B 包）|
| IOIF1 | 南桥 | 约 2.5 GB/s 每方向（仅二手来源，未经 Sony 官方文档确认）|

---

## 3. RSX「Reality Synthesizer」

RSX 是 NVIDIA 为 Sony 定制的 **G70/NV47（GeForce 7800）派生** GPU。它不是 Cell 的
一部分，而是通过 FlexIO 连接的独立芯片。

| 项目 | 说明 |
|---|---|
| 核心 | 500 MHz（发布会口径 550 MHz，量产 500 MHz），90 nm → 65 nm → 40 nm → 28 nm |
| 渲染管线 | **24 个 pixel shader 管线**（6 quad × 4，每管线 2 个 fp32 vec4 ALU）、**8 个 vertex shader**（vec4 + 标量）、8 ROP、24 纹理单元 |
| Shader 模型 | Shader Model 3.0（Cg 语言），固定管线 + 可编程 vertex/pixel，**没有统一 shader、没有 geometry shader** |
| 显存 | 256 MB GDDR3，**128-bit** 总线（桌面 7800 GTX 是 256-bit），650 MHz（1.3 Gbps）→ **20.8 GB/s**；早期公开规格为 700 MHz → 22.4 GB/s |
| 接口 | FlexIO IOIF0 到 Cell；通过 Cell 访问 XDR |
| 输出 | HDMI 1.3a、模拟 AV（Multi AV），最高 1080p |

### 3.1 一个著名的不对称：谁能读谁的内存

PS3 内存系统最反直觉的事实（来自 Sony 2006 年开发者资料，被广泛转述）：

| 方向 | 带宽 |
|---|---|
| Cell 读/写 XDR | ~25 GB/s |
| RSX 读 GDDR3（本地）| 20.8 GB/s |
| RSX 读 XDR（经 FlexIO 15 GB/s 方向）| ~15 GB/s |
| RSX 写 XDR | ~20 GB/s |
| Cell **写** GDDR3 | ~4 GB/s |
| Cell **读** GDDR3 | **~16 MB/s**（兆字节！）|

原因：Cell 读 GDDR3 的路径是「PPE/SPE 发出 load → 经 IOIF0 到 RSX → RSX 内部的
内存仲裁器走非缓存、非流水的 host 读路径 → 一次只能处理一个未完成读请求」，每个读要
等完整的往返延迟（上千个周期），所以吞吐量只有 16 MB/s。写是 posted write，可以流水
排队，所以能到 4 GB/s。

工程后果：
- **永远不要让 Cell 读显存**。如果 CPU 需要 GPU 的结果（如 render-to-texture 后处理、
  遮挡查询），应由 RSX 把结果 **写到 XDR**（20 GB/s），再让 Cell 从 XDR 读。
- 顶点/纹理数据通常放在 XDR 让 RSX 拉取（省显存），但 RSX 读 XDR 的 15 GB/s 要与
  Cell 自己的访存流量共享，所以带宽预算要仔细划分。
- 很多 PS3 引擎用 SPE 做「GPU 的预处理器」（顶点变换、裁剪、后处理），正是因为
  SPE→XDR→RSX 的路径是通的，而反向却不是。

### 3.2 命令缓冲（FIFO）提交

RSX 沿用 NVIDIA 的「push buffer」模型（在 Sony 的 libgcm 中叫 **command buffer**）：

```
   PPE / SPE                          RSX
   ---------                          ---
   在 XDR 或 GDDR3 中的环形区域
   +------------------------------+
   |  cmd | cmd | cmd | ... |      |
   +------------------------------+
      ^GET (RSX 已消费到此)   ^PUT (CPU 写到此)
         (RSX 寄存器, CPU 读)      (CPU 写 RSX 的 PUT 寄存器)

   1. CPU 把 32-bit 命令字（方法号+参数）写进缓冲区（普通 store）。
   2. CPU 写 RSX 的 PUT 寄存器（MMIO，经 FlexIO）= 新的尾指针。
   3. RSX 的 FIFO 引擎看到 GET != PUT，通过 FlexIO DMA 读取 [GET, PUT) 的命令并执行。
   4. RSX 推进 GET；CPU 轮询 GET（或用 "reference" 命令 + 写回标签）判断已执行到哪里。
   5. 帧末的 flip 命令让 CRTC 在下一次 VSYNC 切换扫描输出的 front buffer。
```

`cellGcmSetFlip` / `cellGcmSetWaitFlip` / `cellGcmSetReferenceCommand` 等 API 就是这个
协议的薄封装。命令缓冲放在 XDR 还是 GDDR3 各有取舍：放 XDR 则 CPU 写得快、RSX 拉取走
15 GB/s 链路；放 GDDR3 则 CPU 写只有 4 GB/s 但 RSX 读是本地。实践中多数引擎把命令缓冲
放在 XDR，并让 SPE 直接生成命令（SPE 可以 DMA 写 XDR，也可以通过 MMIO 写 PUT）。

---

## 4. 软件模型

### 4.1 层次

```
   游戏代码
     |  libspe2 / libgcm / SPURS / PSGL ...
   GameOS (lv2)       <- 游戏看到的 "OS": 线程、SPU 线程组、内存
   Hypervisor (lv1)   <- 隔离、SPE 分配 (把 1 个 SPE 留给自己)、安全
   硬件
```

### 4.2 SPE 程序的生命周期

1. PPE 创建 **SPU thread group**，为每个 SPU 线程指定一个 SPU ELF 镜像。
2. Hypervisor/OS 把 ELF 的代码和数据 DMA 进目标 SPE 的 LS（LS 只有 256 KB，所以 SPU
   程序通常很小，或者用 **overlay** 按需换入代码段）。
3. PPE 写 SPE 的运行控制寄存器 `SPU_RunCntl` 启动 SPU。
4. SPU 从 LS 的入口开始执行；参数通过 mailbox / signal / 栈上 argp 传递。
5. SPU 退出（`stop` 指令）或 PPE 停止它；状态通过 `SPU_Status` 读出。

因为启动/停止一个 SPU 线程要走 Hypervisor 调用，开销是微秒级，所以实际游戏都采用
**常驻内核 + 作业队列** 的模式——这就是 SPURS。

### 4.3 SPURS（SPU Runtime System）

SPURS 是 Sony 提供的 SPE 任务调度器。每个游戏可用的 SPE 上常驻一个很小的 SPURS
kernel（约 16 KB LS），它从 XDR 中的 **workload 队列** 取任务：

| 概念 | 说明 |
|---|---|
| Workload | 一类工作的容器，带优先级；SPURS kernel 在多个 workload 间按优先级切换 |
| Job（Job Chain）| 最常用的模型：**无状态的小函数**，描述符声明输入 DMA 列表、输出 DMA 列表和代码。SPURS 自动做**三阶段流水**：取下一个 job 的输入 (DMA in) ‖ 执行当前 job ‖ 写回上一个 job 的输出 (DMA out)——即硬件化的双缓冲 |
| Task | 有状态、可阻塞的协程，上下文切换时整个 LS 被换出到 XDR（昂贵）|
| 同步 | PPE 与 SPE 之间用 XDR 上的原子计数器、event flag、以及 outbound interrupt mailbox |

Job 模型要求程序员把问题切成「输入 ≤ 几十 KB、输出 ≤ 几十 KB、无全局状态」的碎片。
这是 PS3 时代「data-oriented design」思潮的直接推动力。

### 4.4 Local Store 手工管理与对齐

- 256 KB 要装：代码 + 全局数据 + 栈 + 所有 DMA 缓冲。典型分配：代码 32–64 KB，栈 8–16 KB，
  其余是缓冲区。
- DMA 缓冲区声明为 `__attribute__((aligned(128)))`；主存侧的数据结构也要 128 B 对齐，
  否则一次 16 KB 的传输会被拆成多笔 EIB 事务。
- LS 没有保护：栈溢出会覆盖缓冲区，缓冲区越界会覆盖代码，而且不会有任何异常。
- 代码膨胀是真实问题：SPU 的 `shufb` 式向量代码很长，模板展开要克制。

### 4.5 双缓冲 DMA：逐周期示例

设每块 16 KB，DMA 一块约需 1200 核心周期（16 KB / ~14 B/cycle 的实际 per-SPE 带宽），
计算一块需 2000 周期。

**单缓冲（串行）**：

```
cycle:  0        1200           3200       4400          6400
        |---get0--|---compute0---|---get1---|---compute1---|
                                  SPU 空闲 1200 周期/块 → 利用率 62.5%
```

**双缓冲（重叠）**：

```
buf A:  |---get0--|---compute0---|-put0-|          |---compute2---|-put2-|
buf B:            |---get1--------|     |---compute1---|-put1-|
                                  ^      ^
                                  |      wait tag(B) 立即返回: get1 早已完成
                                  issue get2 → A (在 compute1 之前)
cycle:  0        1200           3200    3200+          5200
        SPU 除了第一块之外持续忙 → 利用率 → 100%，总时间 ≈ 1200 + N×2000
```

关键在于**发出下一块的 `get` 的时机**：必须在计算当前块**之前**发出，这样 DMA 延迟
被整个计算时间覆盖。代码骨架（真实 SPU 代码，`spu_mfcio.h`）：

```c
#define TAG_A 0
#define TAG_B 1
static vec_float4 bufA[CHUNK/4] __attribute__((aligned(128)));
static vec_float4 bufB[CHUNK/4] __attribute__((aligned(128)));

mfc_get(bufA, ea, CHUNK_BYTES, TAG_A, 0, 0);                 /* 预取第 0 块 */
for (i = 0; i < n; ++i) {
    void *cur = (i & 1) ? bufB : bufA;  int tag_cur = (i & 1) ? TAG_B : TAG_A;
    void *nxt = (i & 1) ? bufA : bufB;  int tag_nxt = (i & 1) ? TAG_A : TAG_B;
    if (i + 1 < n)
        mfc_get(nxt, ea + (i + 1) * CHUNK_BYTES, CHUNK_BYTES, tag_nxt, 0, 0);
    mfc_write_tag_mask(1 << tag_cur);                        /* 只等当前块 */
    mfc_read_tag_status_all();                               /* 阻塞直到 get(i) 和 put(i-2) 完成 */
    compute(cur);
    mfc_put(cur, ea_out + i * CHUNK_BYTES, CHUNK_BYTES, tag_cur, 0, 0);
}
mfc_write_tag_mask((1 << TAG_A) | (1 << TAG_B));
mfc_read_tag_status_all();                                   /* 等最后两块写回 */
```

注意 `put(i-2)` 也用同一个 tag 且目标就是 `cur`，所以同一次 tag wait 既保证输入到位，
也保证**上一次使用这块缓冲区的输出已经离开**——这是只用两块缓冲区而不出现 WAR 冲突的
原因。`examples/C/spe_double_buffer.c` 在 Ripes 上用纯软件复刻了这段结构。

三缓冲（SPURS job 的做法）进一步让「写回 i-1」「计算 i」「读取 i+1」三者完全并行，
代价是三倍 LS 占用。

### 4.6 软件流水与分支消除

SPU 没有乱序、没有分支预测，所以编译器/程序员要：
- **software pipelining / loop unrolling**：让 even/odd 两条流水线每周期都有指令，
  并用 6 周期的 FMA 延迟去填充独立迭代。
- **分支转换为 select**：`spu_sel(a, b, mask)` 替代 if/else；真正的循环用 `hbr` 提示。
- **SoA 布局**：4 个 float 的 x、y、z、w 各自打包成向量，比 AoS 的 `shufb` 重排少得多。

---

## 5. 与 GPU SIMT / NPU 的对照

| 维度 | Cell SPE | GPU SIMT（`gpu.md`）| 脉动阵列 NPU（`tensor_cpu.md`）|
|---|---|---|---|
| 执行模型 | 8 个**独立**的标量/向量核，各自 PC，各自程序 | 多个 SM，每个 SM 以 warp 为单位**锁步**执行同一指令 | 固定数据流，PE 阵列无指令 |
| 并行粒度 | 4 宽 SIMD × 8 核（任务并行 + 数据并行）| 32 lane × 数十 warp × 数十 SM（海量数据并行）| N×N MAC（矩阵并行）|
| 数据搬运 | **显式**：程序员发 DMA、管 tag、管双缓冲 | **硬件管理**：load 自动经 L1/L2 cache；coalescing 由硬件做 | **描述符驱动**：CPU 填 descriptor，DMA 引擎按 tile 搬 |
| 本地存储 | 256 KB LS，软件分配 | 寄存器 + shared memory（软件分配）+ L1（硬件）| 片上 weight/activation buffer |
| 延迟隐藏 | 双/三缓冲 + 软件流水（程序员负责）| 多 warp 切换（硬件负责，需要 occupancy）| 流水线固定，无需隐藏 |
| 分支 | 独立分支，无预测，需要 hint | 发散 → 掩码串行执行 | 无分支 |
| 控制路径 | mailbox / signal / 中断 | kernel launch 命令队列 | doorbell + 描述符环 + 中断 |
| 对程序员的要求 | 最高：手工内存管理 + 向量化 + 调度 | 中：写标量线程代码，注意合并访存与发散 | 低：只写描述符 |
| 适用 | 任意可切块的计算（物理、音频、顶点、解码）| 大规模同构并行 | 矩阵乘为主 |

**显式 vs 隐式数据搬运**是核心分野。Cell 把「什么时候、搬什么、搬到哪」的决定权完全
交给软件，因此理论上能做到最优的带宽利用（没有 cache 污染、没有预取失误）；GPU 则用
大量线程 + cache 让硬件在运行时猜，牺牲一点效率换来可编程性。有趣的是，现代 GPU 的
shared memory + `cp.async`/TMA（异步拷贝到 shared memory，带完成栅栏）几乎就是 MFC DMA
+ tag 的再发明——只是在 cache 之外**额外**提供，而不是唯一选项。

**锁步 vs 独立核**：GPU 的 warp 让 32 个线程共享一次取指，对分支发散敏感；SPE 是真正
独立的核，可以各跑不同程序（一个解码音频、一个做物理、三个做顶点处理），更像今天的
「异构小核 + DSP」。代价是 8 个独立取指/译码单元，面积效率低于 SIMT。

---

## 6. 为什么它难编程，以及后来者学到了什么

**难点**

1. **256 KB 的墙**：所有工作集都要切成 ≤ 几十 KB 的块，很多算法（树遍历、哈希表、
   任意指针追踪）没有自然的切法。
2. **两套 ISA、两套工具链**：PPE 代码和 SPU 代码分开编译、分开链接、分开调试；LS 里
   崩溃没有 core dump。
3. **一切都是手动的**：DMA、对齐、双缓冲、tag、向量化、分支提示、代码 overlay。
   一个做得好的 SPE 内核常常是性能最好的，但写它需要资深工程师数周。
4. **PPE 太弱**：没来得及搬到 SPE 的串行代码跑得比对手（Xbox 360 的 Xenon 同样是
   顺序 PowerPC，但有 3 核）更慢，首发时跨平台游戏普遍 PS3 版表现更差。
5. **内存不对称**：Cell 读显存 16 MB/s 这类陷阱没有写在显眼处。
6. **生态**：Cell 没有进入 PC，没有 CUDA 那样的通用编程模型；SPURS 很好但只在 PS3。

**后来者学到的**

| 教训 | 体现 |
|---|---|
| 协处理器也要有 cache | GPU 从 Fermi 起有真正的 L1/L2；Xbox One / PS4 用常规 cache 层次的 x86 + GCN |
| 统一内存地址空间 | PS4 的 CPU/GPU 共享 8 GB GDDR5，hUMA；Apple Silicon 的 UMA；CUDA Unified Memory |
| 显式异步拷贝作为**可选**优化 | CUDA `cp.async`、Hopper TMA、AMD LDS DMA——都是 MFC DMA + tag 的思路 |
| 把并行模型做成语言/运行时而不是库 | CUDA/OpenCL 的 kernel 模型；OpenCL 的 1.0 规范里明显有 Cell 的影子（local memory、显式 buffer 拷贝）|
| Job system / data-oriented design | 今天几乎所有游戏引擎的 job system（Naughty Dog、Unity DOTS、Unreal Tasks）都源自 SPURS 时代的经验 |
| 小而多的独立核 ≠ 高效 | 之后的向量加速器要么走 SIMT（GPU），要么走固定功能（NPU），没人再做 8 个独立取指的 SIMD 核 |

Cell 本身在 PS3 之后几乎没有续作（PowerXCell 8i 用于 Roadrunner 超算，之后项目终止），
但它定义的问题空间——异构核、显式本地存储、作业系统——每一项都在后来的设计中以
更友好的形式复活。

---

## 7. 常见误区

| 误区 | 事实 |
|---|---|
| 「PS3 有 8 个 SPE 可用」| 芯片有 8 个，PS3 上为提高良率只启用 7 个，再有 1 个被 Hypervisor/GameOS 独占（安全、解密、系统功能），游戏只能用 **6 个** |
| 「SPE 是协处理器，像 FPU 一样被 PPE 调用」| SPE 是**完整的独立核**，有自己的 PC、自己的程序、自己的中断；PPE 只是启动它并与之通信 |
| 「SPE 的 Local Store 是 cache」| LS 不是 cache：没有标签、没有缺失、没有自动填充。SPU load 只能从 LS 读；想要主存数据必须显式 DMA |
| 「EIB 是 204.8 GB/s 的总线，所以每个 SPE 有 204.8 GB/s」| 每个节点的接口是 16 B 入 + 16 B 出 / 总线周期，即单 SPE 约 25.6 GB/s 方向；204.8 GB/s 是全片并发总和（12 个传输同时跑）|
| 「XDR 25.6 GB/s + GDDR3 22.4 GB/s = 48 GB/s 总带宽」| 两者之间的 FlexIO 和不对称访问路径才是瓶颈；而且 RSX 的 GDDR3 实为 650 MHz → 20.8 GB/s |
| 「Cell 可以把显存当主存用」| Cell 读 GDDR3 只有约 16 MB/s，**不能**当主存用；写才有 4 GB/s |
| 「RSX 是 7800 GTX」| 核心架构同族，但是 128-bit 显存总线（桌面为 256-bit）、500 MHz、少数固定功能有差异 |
| 「SPE 的双精度性能也是 25.6 GFLOPS」| 首代 Cell 的 fp64 未流水化，每 SPE 只有约 1.8 GFLOPS；PowerXCell 8i 才提升到 12.8 GFLOPS |
| 「mailbox 是共享内存」| mailbox 是 MFC 内的 **4+1+1 项 32-bit 硬件 FIFO**，PPE 经 MMIO 访问，SPU 经 channel 指令访问；大数据必须走 DMA |
| 「DMA 是用物理地址的」| MFC 内有 MMU（SLB/TLB），DMA 使用 PPE 的**有效地址**，受页表保护 |
| 「一次 DMA 可以搬任意大小」| 单条命令最大 16 KB；更大的用 DMA list（≤2048 项）|
| 「DMA 完成后 SPU 会收到通知」| 默认不会。SPU 必须用 tag mask + `MFC_RdTagStat` 主动等待（或打开 MFC 事件中断）|

---

## 8. 在 open-soc-explorer 中使用

- **Architecture tab → "Sony PlayStation 3 / Cell BE"**：框图按「SPE 行 — EIB — PPE/MIC/BEI
  — XDR/RSX/GDDR3 — 南桥/HDMI」分层；点击任一块可看寄存器/规格表。
- **Flows**：
  1. `ppe_dispatch`：PPE 把 job 描述符写到 XDR → 写 inbound mailbox → SPU 读 mailbox →
     MFC DMA get → 计算 → DMA put → outbound interrupt mailbox → PPE 中断。
  2. `spe_double_buffer`：在计算块 N 的同时预取块 N+1，tag wait 的位置和原因。
  3. `rsx_cmdbuf`：PPE 在 XDR 中构建命令缓冲，写 PUT，RSX 经 FlexIO 拉取、渲染到 GDDR3、
     flip、扫描输出。
  4. `ls_to_ls`：SPE 之间通过 EIB 直接 LS→LS DMA（不经过主存）。
- **示例程序**：`examples/C/spe_double_buffer.c`（Ripes，RV32IM，`-nostdlib`）用
  纯软件模拟 MFC 命令队列与 tag，展示相同的循环骨架；可以在 Ripes 中单步跟踪
  `dma_get → wait_tag → compute → dma_put` 的顺序，并对照本文 4.5 节。

---

## 参考资料

- IBM, *Cell Broadband Engine Programming Handbook*, v1.1 (2007) — MFC/channel/mailbox 规格的权威来源。
- J. A. Kahle et al., "Introduction to the Cell multiprocessor", *IBM J. Res. & Dev.* 49(4/5), 2005.
- T. Chen et al., "Cell Broadband Engine Architecture and its first implementation — A performance view", *IBM J. Res. & Dev.* 51(5), 2007（EIB 204.8 / 307.2 GB/s 的出处）。
- IBM, "Cell Broadband Engine Architecture", Hot Chips 17 (2005).
- Sony, *libgcm / SPURS* 公开教程与 GDC 演讲（命令缓冲、job 模型）。
- Rambus 新闻稿（2005–2006）：XDR 与 FlexIO 在 PS3 中的配置。
- Wikipedia / psdevwiki：RSX 规格与内存访问带宽表（Cell 读 GDDR3 16 MB/s 等）。
