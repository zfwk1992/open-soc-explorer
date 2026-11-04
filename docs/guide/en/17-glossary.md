# 17 Glossary

> **What you will learn**: every technical term used in this guide, with a one-sentence explanation and its Chinese equivalent.
>
> **Prerequisites**: none. Browse by category or use your browser's/editor's search.
>
> Format: **term** — one-sentence explanation [Chinese term]. "see NN" means the term is used mainly in chapter NN.

---

## 17.1 CPU and ISA basics

| Term | Explanation | Chinese |
|---|---|---|
| **ISA** (Instruction Set Architecture) | The processor interface visible to software: instructions, registers, encodings and memory model. | 指令集架构 |
| **RISC-V** | An open reduced instruction set architecture; Ripes simulates RV32/RV64 with the I, M, C and other extensions. | RISC-V |
| **RV32I / RV64I** | The base integer instruction subset of RISC-V with 32- or 64-bit registers. | 32/64 位基础整数指令集 |
| **M extension** | The RISC-V extension adding integer multiply/divide instructions such as `mul` and `div`. | 乘除法扩展 |
| **C extension** | The RISC-V extension providing 16-bit compressed encodings to reduce code size. | 压缩指令扩展 |
| **register** | The fastest storage inside the CPU; RISC-V has 32 integer registers x0–x31. | 寄存器 |
| **x0 / zero** | A register that always reads 0 and ignores writes. | 零寄存器 |
| **ABI name** | The calling-convention alias of a register, such as `sp`, `ra`, `a0`, `t0`, `s0`. | ABI 名称 |
| **PC** (Program Counter) | The register holding the address of the next (or current) instruction. | 程序计数器 |
| **immediate** | A constant encoded directly in an instruction. | 立即数 |
| **opcode** | The instruction field that says which operation to perform. | 操作码 |
| **pseudo-instruction** | A convenience mnemonic the assembler expands into one or more real instructions, such as `li`, `la`, `mv`. | 伪指令 |
| **directive** | A statement starting with `.` that instructs the assembler rather than producing instructions, such as `.data`, `.word`, `.string`. | 汇编伪操作 |
| **label** | A name given to an address, used as a jump target or to take an address. | 标签 |
| **symbol** | A name-to-value mapping known to the assembler/linker, including labels and constants exported by peripherals. | 符号 |
| **.text / .data segment** | The memory regions holding instructions and initialised data respectively. | 代码段 / 数据段 |
| **load / store** | Instructions moving data between memory and registers (`lw`/`sw` etc.). | 加载 / 存储 |
| **branch / jump** | Instructions that change the PC; branches are conditional, jumps unconditional. | 分支 / 跳转 |
| **ecall** | The instruction requesting a service from the execution environment (Ripes' simulated system calls), such as printing or exiting. | 环境调用 |
| **calling convention** | The software rules for passing arguments and return values and for saving registers. | 调用约定 |
| **stack / stack pointer** | A last-in-first-out memory area for function-local data; `sp` points to its top. | 栈 / 栈指针 |
| **endianness / little-endian** | The byte order of multi-byte data in memory; little-endian puts the low byte at the low address (as RISC-V does). | 字节序 / 小端 |
| **sign extension** | Widening a signed number by filling the upper bits with its sign bit. | 符号扩展 |
| **alignment** | An address being a multiple of the data size (or of a given boundary). | 对齐 |
| **ELF** | The common Unix object/executable file format, which Ripes can load directly. | ELF 可执行文件格式 |
| **toolchain / cross-compiler** | The compiler, assembler and linker that produce programs for another ISA than the host's. | 工具链 / 交叉编译器 |
| **-nostdlib** | A GCC option that links neither the C library nor startup code, so the program starts at its first instruction. | 不链接标准库 |
| **CLI** (Command-Line Interface) | The mode that runs Ripes from arguments without a window; it cannot instantiate peripherals. | 命令行界面 |

## 17.2 Processors and pipelines

| Term | Explanation | Chinese |
|---|---|---|
| **single-cycle processor** | A processor model that completes every instruction in one clock cycle. | 单周期处理器 |
| **pipeline** | Splitting instruction execution into stages so that several instructions overlap. | 流水线 |
| **IF / ID / EX / MEM / WB** | The five stages of the classic pipeline: fetch, decode, execute, memory access, write-back. | 取指 / 译码 / 执行 / 访存 / 写回 |
| **clock cycle** | The basic time unit in which processor state is updated once. | 时钟周期 |
| **CPI / IPC** | Cycles per instruction / instructions per cycle, reciprocal measures of efficiency. | 每指令周期数 / 每周期指令数 |
| **hazard** | A situation that prevents the next instruction in the pipeline from executing correctly in the next cycle. | 冒险 |
| **data hazard** | A later instruction needs data not yet produced by an earlier one. | 数据冒险 |
| **control hazard** | The next instruction to fetch is unknown until a branch resolves. | 控制冒险 |
| **structural hazard** | Two instructions need the same hardware resource at the same time. | 结构冒险 |
| **forwarding / bypassing** | Sending a result from a later stage directly back to an earlier one instead of waiting for write-back. | 转发 / 旁路 |
| **stall / bubble** | Pausing the pipeline for a cycle, equivalent to inserting a no-op. | 停顿 / 气泡 |
| **flush** | Discarding instructions already fetched that must not execute (e.g. after a mispredicted branch). | 冲刷 |
| **load-use hazard** | Using a load result in the very next instruction, which costs a stall even with forwarding. | 加载-使用冒险 |
| **branch prediction** | Guessing a branch direction to keep fetching before it is resolved. | 分支预测 |
| **out-of-order execution** | Executing instructions as their data becomes ready rather than in program order (all Ripes processors are in-order). | 乱序执行 |
| **pipeline diagram** | A chart with instructions as rows and cycles as columns showing each instruction's stage. | 流水线图 |
| **reverse execution** | The Ripes feature that undoes one clock cycle (Reverse, F4); it does not apply to the GPU/NPU peripherals. | 反向执行 |
| **VCD** (Value Change Dump) | A standard file format recording signal changes over time, viewable in a waveform viewer. | 波形转储文件 |
| **multicore** | Several cores on one chip, each running its own instruction stream. | 多核 |
| **SMT** (Simultaneous Multithreading) | A core keeping several hardware thread contexts at once to improve utilisation. | 同时多线程 |

## 17.3 Caches and the memory hierarchy

| Term | Explanation | Chinese |
|---|---|---|
| **cache** | A small fast memory between CPU and main memory holding copies of recently used data. | 高速缓存 |
| **hit / miss** | The accessed data is / is not in the cache. | 命中 / 缺失 |
| **hit rate / miss rate** | The fraction of accesses that hit (miss). | 命中率 / 缺失率 |
| **cache line / block** | The smallest unit transferred and managed between cache and memory. | 缓存行 / 块 |
| **set / way** | The group an address maps to in a set-associative cache, and the number of places within it. | 组 / 路 |
| **direct-mapped** | A cache organisation where each address has exactly one possible location. | 直接映射 |
| **set-associative** | A cache organisation where an address may go into any way of one set. | 组相联 |
| **fully associative** | A cache organisation where any address may go anywhere. | 全相联 |
| **tag / index / offset** | The three parts of an address used for comparison, set selection and byte selection. | 标记 / 索引 / 块内偏移 |
| **replacement policy (LRU, random)** | The rule choosing which line to evict when a set is full. | 替换策略（最近最少使用、随机） |
| **write-back / write-through** | Writes update only the cache and are written back on eviction, or update cache and memory together. | 写回 / 写直达 |
| **write-allocate** | On a write miss, bring the line into the cache first, then write. | 写分配 |
| **dirty bit** | Marks a cache line as modified so it must be written back on eviction. | 脏位 |
| **coherence** | The property that all caches/devices see consistent data for the same address. | 一致性 |
| **flush / invalidate** | Write dirty lines back to memory; or discard cached copies so the next read comes from memory. | 写回 / 作废 |
| **fence** | An instruction guaranteeing that memory operations before it take effect before those after it. | 内存屏障 |
| **DRAM / SRAM** | Dense memory used for main memory / fast memory used for caches and on-chip buffers. | 动态 / 静态随机存储器 |
| **bandwidth / latency** | Data transferred per unit time / time from issuing an access to its completion. | 带宽 / 延迟 |
| **roofline model** | A model estimating attainable performance as min(peak compute, bandwidth × arithmetic intensity). | Roofline 模型 |
| **arithmetic intensity** | Operations performed per byte of data moved. | 算术强度 |
| **memory-bound / compute-bound** | Performance limited by memory bandwidth / by compute. | 访存受限 / 计算受限 |

## 17.4 MMIO and peripherals

| Term | Explanation | Chinese |
|---|---|---|
| **MMIO** (Memory-Mapped I/O) | Mapping peripheral registers into the address space so ordinary loads/stores access them. | 内存映射 I/O |
| **peripheral** | A device outside the CPU, such as the LED matrix, switches, GPU or NPU. | 外设 |
| **memory map** | The division of the address space into main memory and the ranges of each peripheral. | 内存映射表 |
| **base address** | The start address of a peripheral's register block (e.g. `GPU_0_BASE`). | 基址 |
| **offset** | The distance of a register from the base address (e.g. `GPU_0_CTRL_OFFSET`). | 偏移 |
| **register map** | The list of a peripheral's registers with offsets, widths and access rights. | 寄存器映射 |
| **exported symbol** | A name (address, bit mask, constant) a peripheral provides to the assembler/C compiler. | 导出符号 |
| **ripes_system.h** | The header C programs include; it contains the exported symbols of all instantiated peripherals. | ripes_system.h |
| **status register / control register** | A register reflecting device state / one used to command the device. | 状态寄存器 / 控制寄存器 |
| **sticky bit** | A status bit that stays set until software explicitly clears it. | 粘滞位 |
| **W1C** (write-1-to-clear) | Register semantics where writing 1 to a bit clears it and writing 0 has no effect. | 写 1 清零 |
| **polling** | The CPU repeatedly reading a status register in a loop to wait for an event. | 轮询 |
| **interrupt / IRQ** | A device actively notifying the CPU by interrupting its execution; Ripes processors have no external interrupt input. | 中断 / 中断请求 |
| **ISR** (Interrupt Service Routine) | The handler the CPU jumps to when an interrupt occurs. | 中断服务程序 |
| **interrupt enable / mask** | Bits deciding which events may raise an interrupt. | 中断使能 / 屏蔽 |
| **level-triggered / edge-triggered** | An interrupt active as long as the signal is asserted / raised once on a transition. | 电平触发 / 边沿触发 |
| **interrupt storm** | An ISR that fails to clear the source, so the interrupt fires again immediately. | 中断风暴 |
| **interrupt coalescing** | Raising one interrupt for several completion events to reduce overhead. | 中断合并 |
| **NMI** (Non-Maskable Interrupt) | An interrupt that the normal mask bits cannot disable. | 不可屏蔽中断 |
| **volatile** | A C keyword telling the compiler every access must really read/write memory (used for MMIO). | volatile |
| **offload** | Handing a computation to a dedicated accelerator. | 卸载 / 任务下放 |
| **accelerator** | Dedicated hardware designed for a specific computation (graphics, matrix multiply, …). | 加速器 |

## 17.5 SIMT GPU (see 09)

| Term | Explanation | Chinese |
|---|---|---|
| **GPU** (Graphics Processing Unit) | A processor that achieves throughput with massive numbers of parallel threads, originally for graphics. | 图形处理器 |
| **SIMD** (Single Instruction, Multiple Data) | One instruction processes all elements of a fixed-width vector at once. | 单指令多数据 |
| **SIMT** (Single Instruction, Multiple Threads) | The programmer writes scalar threads; the hardware packs them into warps driven by one instruction. | 单指令多线程 |
| **kernel** | A program executed in parallel by many threads on the GPU. | 核函数 |
| **thread / lane** | One scalar execution instance; "lane" is its position inside the warp. | 线程 / 通道 |
| **warp** | A group of threads executing the same instruction in lockstep (8 by default in this model, 32 on NVIDIA). | 线程束 |
| **block (thread block, CTA)** | Threads that cooperate through shared memory and barriers and reside on one SM. | 线程块 |
| **grid** | All thread blocks of one kernel launch. | 网格 |
| **SM** (Streaming Multiprocessor) | A GPU compute unit with warp slots, registers, shared memory and a scheduler. | 流式多处理器 |
| **threadIdx / blockIdx / blockDim / gridDim** | Built-in values a kernel uses to find which data element it handles (`S_TID` etc. in this model). | 线程号 / 块号 / 块大小 / 网格大小 |
| **warp scheduler** | The unit that picks one ready warp to issue an instruction each cycle. | warp 调度器 |
| **issue slot / idle slot** | An SM's chance to issue one instruction per cycle; idle when no warp is ready. | 发射槽 / 空闲槽 |
| **active mask** | A bit mask showing which lanes of a warp execute the current instruction. | 活跃掩码 |
| **predicate / guard** | A per-lane condition bit; the instruction executes only in lanes where it is true (`@P0`). | 谓词 / 守卫 |
| **predication / if-conversion** | Replacing a short branch by predicated instructions to avoid divergence. | 谓词执行 / if 转换 |
| **branch divergence** | Threads of one warp taking different branches, so the paths must run one after another. | 分支发散 |
| **reconvergence** | Divergent paths merging back into one at the join point. | 重汇聚 |
| **min-PC rule** | This model's reconvergence policy: always run the path with the smallest PC; merge paths with equal PC. | 最小 PC 规则 |
| **SIMT stack** | The hardware stack early GPUs used to track divergent paths and reconvergence points. | SIMT 栈 |
| **memory coalescing** | Merging the addresses of a warp into as few memory transactions as possible. | 合并访存 |
| **transaction / segment** | One bus transfer and the aligned address block it covers (32 bytes in this model). | 事务 / 段 |
| **shared memory** | Fast on-chip storage private to each thread block. | 共享内存 |
| **bank / bank conflict** | Shared memory is split into banks accessible in parallel; different addresses in the same bank must be serialised. | 存储体 / 存储体冲突 |
| **broadcast** | Several lanes reading the same word get it in one access; not a conflict. | 广播 |
| **barrier (BAR)** | A synchronisation point inside a block: continue only when all warps have arrived. | 屏障 |
| **latency hiding** | Scheduling other warps while one waits for memory so the wait wastes no issue slots. | 延迟隐藏 |
| **occupancy** | The ratio of resident warps to warp slots. | 占用率 |
| **SIMD efficiency** | The fraction of lanes doing useful work among all lanes of issued instructions. | SIMD 效率 |
| **launch / doorbell** | The host telling the device to start; in this model a write to `CTRL.START`. | 启动 / 门铃 |
| **FMA** (Fused Multiply-Add) | One instruction computing `a × b + c`. | 融合乘加 |

## 17.6 Tensor processor NPU (see 10)

| Term | Explanation | Chinese |
|---|---|---|
| **NPU** (Neural Processing Unit) | An accelerator for neural networks (mainly matrix multiply); here the Tensor Unit. | 神经网络处理器 |
| **TPU** (Tensor Processing Unit) | Google's neural-network accelerator; the first generation used an int8 weight-stationary systolic array. | 张量处理器 |
| **MAC** (Multiply-Accumulate) | One multiply plus one accumulate, the basic operation of matrix arithmetic. | 乘加 |
| **systolic array** | A regular grid of PEs in which data pulses between neighbours every cycle like a heartbeat. | 脉动阵列 |
| **PE** (Processing Element) | The smallest unit of a systolic array, performing one MAC per cycle. | 处理单元 |
| **weight-stationary** | A dataflow where weights stay in the PEs while activations and partial sums flow through. | 权重驻留 |
| **activation** | Input/output data of a neural-network layer; here the elements of A flowing through the array. | 激活值 |
| **partial sum (psum)** | An intermediate, not yet complete sum passed down the array row by row. | 部分和 |
| **skew / skewed injection** | Feeding row k one cycle later than row k−1 so partial sums meet at the right moment. | 斜向注入 |
| **tile / tiling** | Cutting a large matrix into array-sized blocks computed one by one. | 分块 |
| **int8 / int32** | 8-bit inputs save area and bandwidth; 32-bit accumulators prevent overflow. | 8 位 / 32 位整数 |
| **quantization / requantize** | Mapping floating-point values to small integers; scaling accumulated results back to small integers. | 量化 / 重新量化 |
| **ReLU** | `max(0, x)`, the most common activation function. | 线性整流函数 |
| **operator fusion** | Executing several operations (e.g. matmul + ReLU) as one to avoid a memory round trip. | 算子融合 |
| **descriptor** | A data structure in memory describing one task (opcode, addresses, sizes, flags). | 描述符 |
| **descriptor ring / ring buffer** | A circular array of descriptors; producer and consumer advance TAIL and HEAD. | 描述符环 / 环形缓冲区 |
| **HEAD / TAIL** | The next slot the device will process / the next slot the CPU will fill. | 头指针 / 尾指针 |
| **doorbell** | A register write that carries no data and only tells the device "there is new work". | 门铃 |
| **DMA** (Direct Memory Access) | A device reading and writing memory on the bus by itself, without the CPU. | 直接内存访问 |
| **double buffering** | Using two buffers so that data transfer overlaps with computation. | 双缓冲 |
| **state machine (FSM)** | Control logic described by a finite set of states and transitions. | 状态机 |
| **soft reset** | A software-triggered reset that aborts the task but keeps the configuration. | 软复位 |
| **utilization** | Useful MACs as a fraction of the array's theoretical peak. | 利用率 |
| **Amdahl's law** | When only part of a task is accelerated, the overall speed-up is limited by the rest. | 阿姆达尔定律 |

## 17.7 Neo Geo (see 12)

| Term | Explanation | Chinese |
|---|---|---|
| **Neo Geo (MVS / AES)** | SNK's 1990 arcade/home game system with a 68000 main CPU and a Z80 sound CPU. | Neo Geo（街机版 / 家用版） |
| **68000 (M68K)** | Motorola's 16/32-bit CISC processor, the Neo Geo's main CPU. | 摩托罗拉 68000 |
| **Z80** | The 8-bit processor handling sound in the Neo Geo. | Z80 |
| **LSPC** (Line SPrite Controller) | The video chip that composes sprites and the fix layer line by line, without a framebuffer. | 行精灵控制器 |
| **sprite** | An independently movable graphics block; Neo Geo screens are made almost entirely of sprites. | 精灵 / 子图 |
| **fix layer** | The topmost non-scrolling 8×8 character layer used for scores and text. | 固定层 |
| **framebuffer** | Memory holding a whole frame of pixels; the Neo Geo has none and renders line by line. | 帧缓冲 |
| **line buffer** | A buffer holding the pixels of a single scanline. | 行缓冲 |
| **VBlank / HBlank** | The vertical / horizontal retrace periods of the display, used to update video memory safely. | 垂直 / 水平消隐 |
| **VRAM** | Video memory; the Neo Geo 68000 can only reach it indirectly through LSPC registers. | 显存 |
| **sound latch** | The single 8-bit communication register (mailbox) between the 68000 and the Z80. | 声音锁存器 |
| **mailbox** | A small register or memory location used to pass messages between processors. | 信箱 |
| **NMI** (Non-Maskable Interrupt) | Raised on the Z80 when the 68000 writes the sound latch, signalling a new command. | 不可屏蔽中断 |
| **YM2610 (OPNB)** | The Neo Geo's FM/SSG/ADPCM sound synthesis chip. | YM2610 声音芯片 |
| **watchdog** | A timer that resets the system unless software "kicks" it regularly. | 看门狗 |
| **cooperative multitasking** | Multitasking in which tasks yield the CPU voluntarily; the example uses it to model two CPUs on one RISC-V core. | 协作式多任务 |

## 17.8 PS3 / Cell Broadband Engine (see 13)

| Term | Explanation | Chinese |
|---|---|---|
| **Cell BE** (Cell Broadband Engine) | The heterogeneous multicore processor by Sony/Toshiba/IBM: 1 PPE + 8 SPEs. | Cell 宽带引擎 |
| **heterogeneous multicore** | A chip integrating cores of different types. | 异构多核 |
| **PPE** (Power Processing Element) | Cell's general-purpose 64-bit PowerPC core that runs the OS and dispatches work to the SPEs. | PowerPC 处理单元 |
| **SPE** (Synergistic Processing Element) | Cell's vector coprocessor, made of an SPU, a Local Store and an MFC. | 协同处理单元 |
| **SPU** (Synergistic Processing Unit) | The 128-bit SIMD core inside an SPE that executes instructions. | 协同处理器执行单元 |
| **LS** (Local Store) | Each SPE's private 256 KB software-managed memory, the only memory the SPU accesses directly. | 本地存储 |
| **MFC** (Memory Flow Controller) | The SPE's DMA engine and communication interface, moving data between LS and main memory. | 内存流控制器 |
| **tag group** | A 5-bit number on MFC DMA commands used to wait for a group of transfers to complete. | 标签组 |
| **DMA list** | A scatter/gather DMA where one command describes several transfers. | DMA 列表 |
| **EIB** (Element Interconnect Bus) | The four on-chip rings connecting the PPE, SPEs, memory controller and I/O. | 单元互连总线 |
| **XDR DRAM** | Rambus high-bandwidth main memory; the PS3 has 256 MB (25.6 GB/s). | XDR 内存 |
| **MIC** (Memory Interface Controller) | The Cell controller attached to the XDR memory. | 内存接口控制器 |
| **RSX** (Reality Synthesizer) | The PS3's custom NVIDIA GPU, connected to Cell through FlexIO. | RSX 图形处理器 |
| **FlexIO** | Rambus's high-speed parallel interface linking Cell to RSX and the south bridge. | FlexIO 接口 |
| **BEI** (Broadband Engine Interface) | The Cell I/O interface unit implementing FlexIO. | 宽带引擎接口 |
| **GDDR3** | The 256 MB graphics memory local to the RSX. | GDDR3 显存 |
| **command buffer / push buffer** | A ring of commands written by the CPU and read by the GPU (PUT/GET pointers). | 命令缓冲区 |
| **SPURS** | Sony's runtime system scheduling tasks across several SPEs. | SPURS 运行时 |
| **software pipelining** | Reordering a loop (by programmer or compiler) so instructions of different iterations overlap. | 软件流水 |

## 17.9 Ripes and this project

| Term | Explanation | Chinese |
|---|---|---|
| **Ripes** | A visual RISC-V processor and memory simulator; open-soc-explorer is a fork of it. | Ripes |
| **open-soc-explorer** | The project extending Ripes with a GPU, an NPU, an architecture explorer and NeoGeo/PS3 teaching material. | open-soc-explorer |
| **Architecture Explorer** | The tab that shows whole-system block diagrams from JSON topologies and plays data flows step by step (see 11). | 架构浏览器 |
| **topology / flow / step** | The JSON content of the Architecture Explorer: blocks and buses, one data flow, one step of a flow. | 拓扑 / 数据流 / 步骤 |
| **SoC** (System on Chip) | A system integrating CPU, accelerators, memory controllers and peripherals on one chip. | 片上系统 |
| **QTest** | Qt's unit-test framework; this project uses it to run the GPU/NPU examples headless. | QTest |
| **offscreen** | `QT_QPA_PLATFORM=offscreen`: running a Qt program without showing a window (for tests and screenshots). | 离屏 |

## Summary

When you meet an unfamiliar word, look up its one-sentence explanation and Chinese term here, then go back to the chapter in parentheses for the full example.
