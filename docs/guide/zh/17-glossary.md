# 17 术语表

> **你将学到**：本手册中出现的全部技术术语的中英对照与一句话解释。
>
> **前置条件**：无。可以按分类浏览，也可以用浏览器/编辑器的搜索功能查找英文术语。
>
> 格式：**英文术语**（中文译名）— 一句话解释。括号里的"见 NN"表示主要出现在第 NN 章。

---

## 17.1 CPU 与指令集基础

| 术语 | 中文 | 解释 |
|---|---|---|
| **ISA** (Instruction Set Architecture) | 指令集架构 | 软件能看到的处理器接口：有哪些指令、寄存器、编码和内存模型。 |
| **RISC-V** | RISC-V | 开放的精简指令集架构；Ripes 模拟 RV32/RV64 的 I、M、C 等扩展。 |
| **RV32I / RV64I** | 32/64 位基础整数指令集 | RISC-V 的基础整数指令子集，32 或 64 位寄存器宽度。 |
| **M extension** | 乘除法扩展 | 增加 `mul`、`div` 等整数乘除指令的 RISC-V 扩展。 |
| **C extension** | 压缩指令扩展 | 提供 16 位短编码指令以减小代码体积的 RISC-V 扩展。 |
| **register** | 寄存器 | CPU 内部最快的存储单元；RISC-V 有 x0–x31 共 32 个整数寄存器。 |
| **x0 / zero** | 零寄存器 | 读出恒为 0、写入被忽略的寄存器。 |
| **ABI name** | ABI 名称 | 寄存器按调用约定起的别名，如 `sp`、`ra`、`a0`、`t0`、`s0`。 |
| **PC** (Program Counter) | 程序计数器 | 保存下一条（或当前）指令地址的寄存器。 |
| **immediate** | 立即数 | 直接编码在指令里的常数。 |
| **opcode** | 操作码 | 指令编码中表示"做什么操作"的字段。 |
| **pseudo-instruction** | 伪指令 | 汇编器展开成一条或多条真实指令的便捷写法，如 `li`、`la`、`mv`。 |
| **directive** | 汇编伪操作 | 以 `.` 开头、指导汇编器而非生成指令的语句，如 `.data`、`.word`、`.string`。 |
| **label** | 标签 | 给地址起的名字，供跳转或取地址使用。 |
| **symbol** | 符号 | 汇编器/链接器已知的名字与数值的对应，包括标签和外设导出的常量。 |
| **.text / .data segment** | 代码段 / 数据段 | 分别存放指令和初始化数据的内存区域。 |
| **load / store** | 加载 / 存储 | 在内存与寄存器之间搬运数据的指令（`lw`/`sw` 等）。 |
| **branch / jump** | 分支 / 跳转 | 改变 PC 的指令；分支有条件，跳转无条件。 |
| **ecall** | 环境调用 | 请求执行环境（Ripes 模拟的系统调用）提供服务的指令，如打印、退出。 |
| **calling convention** | 调用约定 | 规定参数、返回值、保存寄存器如何传递的软件约定。 |
| **stack / stack pointer** | 栈 / 栈指针 | 函数调用用来保存局部数据的后进先出内存区；`sp` 指向栈顶。 |
| **endianness / little-endian** | 字节序 / 小端 | 多字节数据在内存中的字节顺序；小端即低字节在低地址（RISC-V 采用）。 |
| **sign extension** | 符号扩展 | 把短的有符号数扩展到更宽位数时用符号位填充高位。 |
| **alignment** | 对齐 | 地址是数据大小（或指定边界）的整数倍。 |
| **ELF** | ELF 可执行文件格式 | Unix 系常用的目标文件/可执行文件格式，Ripes 可直接加载。 |
| **toolchain / cross-compiler** | 工具链 / 交叉编译器 | 在一种机器上生成另一种 ISA 程序的编译器、汇编器、链接器集合。 |
| **-nostdlib** | 不链接标准库 | GCC 选项：不链接 C 标准库与启动代码，程序入口为第一条指令。 |
| **CLI** (Command-Line Interface) | 命令行界面 | 不打开窗口、用参数运行 Ripes 的模式；不能实例化外设。 |

## 17.2 处理器与流水线

| 术语 | 中文 | 解释 |
|---|---|---|
| **single-cycle processor** | 单周期处理器 | 每条指令在一个时钟周期内完成全部步骤的处理器模型。 |
| **pipeline** | 流水线 | 把指令执行分成若干级、让多条指令重叠执行的技术。 |
| **IF / ID / EX / MEM / WB** | 取指 / 译码 / 执行 / 访存 / 写回 | 经典 5 级流水线的五个阶段。 |
| **clock cycle** | 时钟周期 | 处理器状态更新一次的基本时间单位。 |
| **CPI / IPC** | 每指令周期数 / 每周期指令数 | 衡量处理器效率的两个互为倒数的指标。 |
| **hazard** | 冒险 | 使流水线中下一条指令不能在下一周期正确执行的情况。 |
| **data hazard** | 数据冒险 | 后一条指令需要的数据还未由前一条指令产生。 |
| **control hazard** | 控制冒险 | 分支结果未知时不知道该取哪条指令。 |
| **structural hazard** | 结构冒险 | 两条指令同时需要同一个硬件资源。 |
| **forwarding / bypassing** | 转发 / 旁路 | 把结果从后级直接送回前级，避免等待写回。 |
| **stall / bubble** | 停顿 / 气泡 | 流水线暂停一个周期，相当于插入一条空操作。 |
| **flush** | 冲刷 | 丢弃流水线中已经取入但不该执行的指令（如分支预测错误后）。 |
| **load-use hazard** | 加载-使用冒险 | 紧跟在 load 之后使用其结果，即使有转发也要停顿一个周期。 |
| **branch prediction** | 分支预测 | 在分支结果确定前猜测方向以继续取指。 |
| **out-of-order execution** | 乱序执行 | 不按程序顺序、按数据就绪情况执行指令的技术（Ripes 处理器都是顺序执行）。 |
| **pipeline diagram** | 流水线图 | 以指令为行、周期为列显示每条指令所处阶段的图。 |
| **reverse execution** | 反向执行 | Ripes 中撤销一个时钟周期、回到上一状态的功能（Reverse，F4）；不作用于 GPU/NPU 外设。 |
| **VCD** (Value Change Dump) | 波形转储文件 | 记录信号随时间变化的标准文件格式，可用波形查看器打开。 |
| **multicore** | 多核 | 一颗芯片上有多个独立执行指令流的核心。 |
| **SMT** (Simultaneous Multithreading) | 同时多线程 | 一个核心同时维护多个硬件线程上下文以提高利用率。 |

## 17.3 缓存与存储层次

| 术语 | 中文 | 解释 |
|---|---|---|
| **cache** | 高速缓存 | 位于 CPU 与主存之间、保存最近使用数据副本的小而快的存储。 |
| **hit / miss** | 命中 / 缺失 | 访问的数据在 / 不在 cache 中。 |
| **hit rate / miss rate** | 命中率 / 缺失率 | 命中（缺失）次数占总访问次数的比例。 |
| **cache line / block** | 缓存行 / 块 | cache 与主存之间传输和管理的最小单位。 |
| **set / way** | 组 / 路 | 组相联 cache 中地址映射到的组，以及组内可放置的位置数。 |
| **direct-mapped** | 直接映射 | 每个地址只能放在一个固定位置的 cache 组织。 |
| **set-associative** | 组相联 | 每个地址可以放在某一组内任意一路的 cache 组织。 |
| **fully associative** | 全相联 | 任何地址可以放在任何位置的 cache 组织。 |
| **tag / index / offset** | 标记 / 索引 / 块内偏移 | 地址被拆成的三部分，分别用于比对、选组和选字节。 |
| **replacement policy (LRU, random)** | 替换策略（最近最少使用、随机） | 组满时决定淘汰哪一行的规则。 |
| **write-back / write-through** | 写回 / 写直达 | 写操作只更新 cache 并在淘汰时写回，或同时写 cache 和主存。 |
| **write-allocate** | 写分配 | 写缺失时先把行调入 cache 再写。 |
| **dirty bit** | 脏位 | 标记 cache 行已被修改、淘汰时需要写回。 |
| **coherence** | 一致性 | 多个缓存/设备看到同一地址的数据保持一致的性质。 |
| **flush / invalidate** | 写回 / 作废 | 把脏行写回主存；或丢弃 cache 中的副本，强制下次从主存读。 |
| **fence** | 内存屏障 | 保证屏障前后的内存操作按顺序生效的指令。 |
| **DRAM / SRAM** | 动态 / 静态随机存储器 | 主存常用的高密度存储 / cache 与片上缓冲常用的高速存储。 |
| **bandwidth / latency** | 带宽 / 延迟 | 单位时间可传输的数据量 / 一次访问从发出到完成的时间。 |
| **roofline model** | Roofline 模型 | 用 min(峰值算力, 带宽 × 算术强度) 估计可达性能的模型。 |
| **arithmetic intensity** | 算术强度 | 每搬运 1 字节数据所做的运算次数。 |
| **memory-bound / compute-bound** | 访存受限 / 计算受限 | 性能被内存带宽 / 被算力所限制。 |

## 17.4 MMIO 与外设

| 术语 | 中文 | 解释 |
|---|---|---|
| **MMIO** (Memory-Mapped I/O) | 内存映射 I/O | 把外设寄存器映射到地址空间，用普通 load/store 访问。 |
| **peripheral** | 外设 | CPU 之外的设备，如 LED 矩阵、开关、GPU、NPU。 |
| **memory map** | 内存映射表 | 地址空间中各段分别属于主存还是哪个外设的划分。 |
| **base address** | 基址 | 外设寄存器块的起始地址（如 `GPU_0_BASE`）。 |
| **offset** | 偏移 | 寄存器相对基址的距离（如 `GPU_0_CTRL_OFFSET`）。 |
| **register map** | 寄存器映射 | 外设所有寄存器的偏移、宽度、读写属性列表。 |
| **exported symbol** | 导出符号 | 外设提供给汇编器/C 编译器的名字（地址、位掩码、常量）。 |
| **ripes_system.h** | ripes_system.h | C 程序包含的头文件，内含所有已实例化外设的导出符号。 |
| **status register / control register** | 状态寄存器 / 控制寄存器 | 反映设备状态 / 用来命令设备的寄存器。 |
| **sticky bit** | 粘滞位 | 一旦置位就保持、直到被软件显式清除的状态位。 |
| **W1C** (write-1-to-clear) | 写 1 清零 | 向某位写 1 将其清零、写 0 无影响的寄存器语义。 |
| **polling** | 轮询 | CPU 在循环中反复读状态寄存器等待事件。 |
| **interrupt / IRQ** | 中断 / 中断请求 | 设备主动通知 CPU 打断当前执行的机制；Ripes 处理器没有外部中断输入。 |
| **ISR** (Interrupt Service Routine) | 中断服务程序 | 中断发生时 CPU 跳去执行的处理函数。 |
| **interrupt enable / mask** | 中断使能 / 屏蔽 | 决定哪些事件可以产生中断的位。 |
| **level-triggered / edge-triggered** | 电平触发 / 边沿触发 | 中断在信号保持有效期间持续有效 / 只在跳变时产生一次。 |
| **interrupt storm** | 中断风暴 | ISR 没有清除中断源，导致中断反复立即触发。 |
| **interrupt coalescing** | 中断合并 | 多个完成事件只产生一次中断，以减少开销。 |
| **NMI** (Non-Maskable Interrupt) | 不可屏蔽中断 | 不能被常规屏蔽位关闭的中断。 |
| **volatile** | volatile | C 关键字，告诉编译器每次访问都必须真正读写内存（用于 MMIO）。 |
| **offload** | 卸载 / 任务下放 | 把计算交给专用加速器执行。 |
| **accelerator** | 加速器 | 为特定计算（图形、矩阵乘等）设计的专用硬件。 |

## 17.5 SIMT GPU（见 09）

| 术语 | 中文 | 解释 |
|---|---|---|
| **GPU** (Graphics Processing Unit) | 图形处理器 | 以海量并行线程提高吞吐量的处理器，最初用于图形。 |
| **SIMD** (Single Instruction, Multiple Data) | 单指令多数据 | 一条指令同时处理一个固定宽度向量的多个元素。 |
| **SIMT** (Single Instruction, Multiple Threads) | 单指令多线程 | 程序员写标量线程，硬件把线程打包成 warp 用同一条指令驱动。 |
| **kernel** | 核函数 | 在 GPU 上由大量线程并行执行的一段程序。 |
| **thread / lane** | 线程 / 通道 | 一个标量执行实例；lane 指它在 warp 内的位置。 |
| **warp** | 线程束 | 锁步执行同一条指令的一组线程（本模型默认 8，NVIDIA 为 32）。 |
| **block (thread block, CTA)** | 线程块 | 可通过 shared memory 和 barrier 协作、驻留在同一 SM 上的一组线程。 |
| **grid** | 网格 | 一次 kernel 启动的全部线程块。 |
| **SM** (Streaming Multiprocessor) | 流式多处理器 | 拥有 warp 槽、寄存器、shared memory 和调度器的 GPU 计算单元。 |
| **threadIdx / blockIdx / blockDim / gridDim** | 线程号 / 块号 / 块大小 / 网格大小 | kernel 用来计算自己负责哪个数据元素的内建变量（本模型 `S_TID` 等）。 |
| **warp scheduler** | warp 调度器 | 每周期从就绪 warp 中选择一个发射指令的部件。 |
| **issue slot / idle slot** | 发射槽 / 空闲槽 | SM 每周期发射一条指令的机会；没有就绪 warp 时即为空闲槽。 |
| **active mask** | 活跃掩码 | 标明 warp 中哪些 lane 执行当前指令的位掩码。 |
| **predicate / guard** | 谓词 / 守卫 | 每个 lane 的条件位，指令只在谓词为真的 lane 上执行（`@P0`）。 |
| **predication / if-conversion** | 谓词执行 / if 转换 | 用带谓词的指令代替短分支，避免发散。 |
| **branch divergence** | 分支发散 | 同一 warp 的线程走向不同分支，只能分路径串行执行。 |
| **reconvergence** | 重汇聚 | 发散的路径在汇合点重新合并为一条。 |
| **min-PC rule** | 最小 PC 规则 | 本模型的重汇聚策略：总先执行 PC 最小的路径，PC 相同即合并。 |
| **SIMT stack** | SIMT 栈 | 早期 GPU 记录发散路径和重汇聚点的硬件栈。 |
| **memory coalescing** | 合并访存 | 把一个 warp 的多个地址合并成尽量少的内存事务。 |
| **transaction / segment** | 事务 / 段 | 一次总线传输及其覆盖的对齐地址块（本模型 32 字节）。 |
| **shared memory** | 共享内存 | 每个线程块私有的片上快速存储。 |
| **bank / bank conflict** | 存储体 / 存储体冲突 | shared memory 被分成可并行访问的 bank；多个不同地址落在同一 bank 时必须串行。 |
| **broadcast** | 广播 | 多个 lane 读同一个字时一次读出、同时分发，不算冲突。 |
| **barrier (BAR)** | 屏障 | 线程块内的同步点：所有 warp 到达后才继续。 |
| **latency hiding** | 延迟隐藏 | 一个 warp 等内存时调度其他 warp，让等待不浪费发射槽。 |
| **occupancy** | 占用率 | 驻留 warp 数与 warp 槽数之比。 |
| **SIMD efficiency** | SIMD 效率 | 实际工作的 lane 数占发射指令总 lane 数的比例。 |
| **launch / doorbell** | 启动 / 门铃 | 主机通知设备开始工作的动作；本模型写 `CTRL.START`。 |
| **FMA** (Fused Multiply-Add) | 融合乘加 | 一条指令完成 `a × b + c`。 |

## 17.6 张量处理器 NPU（见 10）

| 术语 | 中文 | 解释 |
|---|---|---|
| **NPU** (Neural Processing Unit) | 神经网络处理器 | 为神经网络（主要是矩阵乘）设计的加速器；本模块即 Tensor Unit。 |
| **TPU** (Tensor Processing Unit) | 张量处理器 | Google 的神经网络加速器，第一代使用 int8 weight-stationary 脉动阵列。 |
| **MAC** (Multiply-Accumulate) | 乘加 | 一次乘法加一次累加，矩阵运算的基本操作。 |
| **systolic array** | 脉动阵列 | 由规则排列的 PE 组成、数据像心跳一样逐拍在相邻 PE 间流动的计算阵列。 |
| **PE** (Processing Element) | 处理单元 | 脉动阵列中做一次乘加的最小单元。 |
| **weight-stationary** | 权重驻留 | 权重装入 PE 后保持不动，activation 和部分和流过阵列的数据流。 |
| **activation** | 激活值 | 神经网络层的输入/输出数据；本模块中指流过阵列的 A 矩阵元素。 |
| **partial sum (psum)** | 部分和 | 尚未累加完成的中间结果，在阵列中逐行向下传递。 |
| **skew / skewed injection** | 斜向注入 | 第 k 行数据比第 k−1 行晚一个周期送入，使部分和在正确时刻相遇。 |
| **tile / tiling** | 分块 | 把大矩阵切成阵列大小的小块逐块计算。 |
| **int8 / int32** | 8 位 / 32 位整数 | 输入用 int8 节省面积与带宽，累加用 int32 防止溢出。 |
| **quantization / requantize** | 量化 / 重新量化 | 把浮点数映射到低位整数；把累加结果缩放回低位整数。 |
| **ReLU** | 线性整流函数 | `max(0, x)`，最常用的激活函数。 |
| **operator fusion** | 算子融合 | 把多个操作（如 matmul + ReLU）合并成一次执行，省去中间内存往返。 |
| **descriptor** | 描述符 | 放在内存中、描述一个任务（操作码、地址、尺寸、标志）的数据结构。 |
| **descriptor ring / ring buffer** | 描述符环 / 环形缓冲区 | 首尾相接的描述符数组，生产者与消费者分别推进 TAIL 和 HEAD。 |
| **HEAD / TAIL** | 头指针 / 尾指针 | 设备下一个要处理的槽 / CPU 下一个要填的槽。 |
| **doorbell** | 门铃 | 不携带数据、只通知设备"有新工作"的寄存器写操作。 |
| **DMA** (Direct Memory Access) | 直接内存访问 | 设备不经 CPU、自己在总线上读写内存的机制。 |
| **double buffering** | 双缓冲 | 用两块缓冲区让数据搬运与计算重叠。 |
| **state machine (FSM)** | 状态机 | 由有限个状态和转移条件描述的控制逻辑。 |
| **soft reset** | 软复位 | 由软件写寄存器触发、只中止任务而保留配置的复位。 |
| **utilization** | 利用率 | 实际完成的乘加数占阵列理论峰值的比例。 |
| **Amdahl's law** | 阿姆达尔定律 | 加速只作用于一部分时，总体加速比受未加速部分限制。 |

## 17.7 Neo Geo（见 12）

| 术语 | 中文 | 解释 |
|---|---|---|
| **Neo Geo (MVS / AES)** | Neo Geo（街机版 / 家用版） | SNK 1990 年的街机/家用游戏系统，68000 主 CPU + Z80 音频 CPU。 |
| **68000 (M68K)** | 摩托罗拉 68000 | Neo Geo 的主 CPU，16/32 位 CISC 处理器。 |
| **Z80** | Z80 | Neo Geo 中负责音频的 8 位处理器。 |
| **LSPC** (Line SPrite Controller) | 行精灵控制器 | 逐扫描线合成 sprite 和 fix 层的视频芯片，没有帧缓冲。 |
| **sprite** | 精灵 / 子图 | 可独立移动的图形块；Neo Geo 画面几乎全由 sprite 组成。 |
| **fix layer** | 固定层 | 位于最上层、不滚动的 8×8 字符层，用于分数、文字。 |
| **framebuffer** | 帧缓冲 | 存放整帧像素的内存；Neo Geo 没有，而是逐行生成。 |
| **line buffer** | 行缓冲 | 只保存一条扫描线像素的缓冲区。 |
| **VBlank / HBlank** | 垂直 / 水平消隐 | 显示器回扫期间，常用于安全地更新显存。 |
| **VRAM** | 显存 | 视频芯片使用的存储；Neo Geo 的 68000 只能经 LSPC 寄存器间接访问。 |
| **sound latch** | 声音锁存器 | 68000 与 Z80 之间唯一的 8 位通信寄存器（信箱）。 |
| **mailbox** | 信箱 | 两个处理器之间用来传递消息的小寄存器或内存位置。 |
| **NMI** (Non-Maskable Interrupt) | 不可屏蔽中断 | 68000 写声音锁存器时触发 Z80 的 NMI，通知它有新命令。 |
| **YM2610 (OPNB)** | YM2610 声音芯片 | Neo Geo 的 FM/SSG/ADPCM 合成芯片。 |
| **watchdog** | 看门狗 | 软件必须定期"喂狗"、否则复位系统的定时器。 |
| **cooperative multitasking** | 协作式多任务 | 任务主动让出 CPU 的多任务方式；示例用它在一个 RISC-V 核上模拟两颗 CPU。 |

## 17.8 PS3 / Cell Broadband Engine（见 13）

| 术语 | 中文 | 解释 |
|---|---|---|
| **Cell BE** (Cell Broadband Engine) | Cell 宽带引擎 | Sony/Toshiba/IBM 设计的异构多核处理器：1 个 PPE + 8 个 SPE。 |
| **heterogeneous multicore** | 异构多核 | 一颗芯片上集成不同类型核心的结构。 |
| **PPE** (Power Processing Element) | PowerPC 处理单元 | Cell 中的通用 64 位 PowerPC 核，运行操作系统并给 SPE 派发任务。 |
| **SPE** (Synergistic Processing Element) | 协同处理单元 | Cell 中的向量协处理器，由 SPU + Local Store + MFC 组成。 |
| **SPU** (Synergistic Processing Unit) | 协同处理器执行单元 | SPE 中执行指令的 128 位 SIMD 核。 |
| **LS** (Local Store) | 本地存储 | 每个 SPE 私有的 256 KB 软件管理存储，SPU 只能直接访问它。 |
| **MFC** (Memory Flow Controller) | 内存流控制器 | SPE 的 DMA 引擎与通信接口，负责 LS 与主存之间的搬运。 |
| **tag group** | 标签组 | MFC DMA 命令的 5 位编号，用来等待一组传输完成。 |
| **DMA list** | DMA 列表 | 一条命令描述多段传输的分散/聚集 DMA。 |
| **EIB** (Element Interconnect Bus) | 单元互连总线 | 连接 PPE、SPE、内存控制器和 I/O 的 4 条环形片上总线。 |
| **XDR DRAM** | XDR 内存 | Rambus 的高带宽主存，PS3 有 256 MB（25.6 GB/s）。 |
| **MIC** (Memory Interface Controller) | 内存接口控制器 | Cell 中连接 XDR 内存的控制器。 |
| **RSX** (Reality Synthesizer) | RSX 图形处理器 | PS3 的 NVIDIA 定制 GPU，通过 FlexIO 连接 Cell。 |
| **FlexIO** | FlexIO 接口 | Rambus 的高速并行接口，连接 Cell 与 RSX 及南桥。 |
| **BEI** (Broadband Engine Interface) | 宽带引擎接口 | Cell 中实现 FlexIO 的 I/O 接口单元。 |
| **GDDR3** | GDDR3 显存 | RSX 本地使用的 256 MB 图形内存。 |
| **command buffer / push buffer** | 命令缓冲区 | CPU 写入、GPU 读取的环形命令队列（PUT/GET 指针）。 |
| **SPURS** | SPURS 运行时 | 在多个 SPE 上调度任务的 Sony 运行时系统。 |
| **software pipelining** | 软件流水 | 由程序员/编译器重排循环，让不同迭代的指令重叠执行。 |

## 17.9 Ripes 与本项目

| 术语 | 中文 | 解释 |
|---|---|---|
| **Ripes** | Ripes | 一个可视化的 RISC-V 处理器与内存模拟器；open-soc-explorer 是它的分支。 |
| **open-soc-explorer** | open-soc-explorer | 在 Ripes 基础上加入 GPU、NPU、架构浏览器与 NeoGeo/PS3 教学内容的项目。 |
| **Architecture Explorer** | 架构浏览器 | 用 JSON 拓扑描述整机框图并逐步播放数据流的标签页（见 11）。 |
| **topology / flow / step** | 拓扑 / 数据流 / 步骤 | 架构浏览器的 JSON 内容：方块与连线、一条数据流、数据流的一步。 |
| **SoC** (System on Chip) | 片上系统 | 把 CPU、加速器、内存控制器和外设集成在一颗芯片上的系统。 |
| **QTest** | QTest | Qt 的单元测试框架；本项目用它在无界面环境下运行 GPU/NPU 示例。 |
| **offscreen** | 离屏 | `QT_QPA_PLATFORM=offscreen`：不显示窗口地运行 Qt 程序（用于测试和截图）。 |

## 小结

遇到不认识的词，先在这里查中文和一句话解释，再回到括号里标注的章节看完整的例子。
