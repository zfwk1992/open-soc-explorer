<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en_US">
<context>
    <name>Ripes::GpuLegend</name>
    <message>
        <location filename="../../src/io/iogpuwidget.cpp" line="+421"/>
        <source>Legend: what the lane squares and row colours mean</source>
        <translation>图例：lane 方块与行底色的含义</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Legend of the warp view</source>
        <translation>warp 视图的图例</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>active lane</source>
        <translation>活跃 lane</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>masked off: thread waits for the other path</source>
        <translation>被屏蔽：线程在等待另一条路径</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>no thread (block smaller than warp)</source>
        <translation>无线程（block 小于 warp）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>issued this cycle (row bar + lanes that executed)</source>
        <translation>本周期发射（行首竖条 + 执行了指令的 lane）</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>waiting at barrier</source>
        <translation>在屏障等待</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>exited</source>
        <translation>已退出</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>diverged warp (row)</source>
        <translation>发散的 warp（整行）</translation>
    </message>
</context>
<context>
    <name>Ripes::GpuWarpView</name>
    <message>
        <location line="-345"/>
        <source>Warp view: one row per warp slot. Hover a lane square to see its thread id, state and registers.</source>
        <translation>warp 视图：每个 warp 槽一行。鼠标悬停在 lane 方块上可查看其线程号、状态和寄存器。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>SM / warp / lane view of the running kernel</source>
        <translation>正在运行的 kernel 的 SM / warp / lane 视图</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+205"/>
        <source>waiting at BAR</source>
        <translation>在 BAR 等待</translation>
    </message>
    <message>
        <location line="-205"/>
        <location line="+207"/>
        <source>stall %1</source>
        <translation>停顿 %1</translation>
    </message>
    <message>
        <location line="-206"/>
        <location line="+208"/>
        <source>diverged: %1 paths</source>
        <translation>发散：%1 条路径</translation>
    </message>
    <message>
        <location line="-199"/>
        <location line="+117"/>
        <source>SM %1 · %2 resident block(s) · issued %3 · idle %4 · issue utilization %5%</source>
        <translation>SM %1 · 常驻 block %2 个 · 已发射 %3 · 空闲 %4 · 发射利用率 %5%</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>W%1 (free)</source>
        <translation>W%1（空闲）</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>(waiting)</source>
        <translation>（等待中）</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>issued this cycle</source>
        <translation>本周期发射</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>SM %1, warp slot %2: free (no warp resident)</source>
        <translation>SM %1，warp 槽 %2：空闲（没有常驻 warp）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>SM %1, warp slot %2 - block %3, warp %4 of the block</source>
        <translation>SM %1，warp 槽 %2 - block %3，该 block 的第 %4 个 warp</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Next pc: %1</source>
        <translation>下一条 pc：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Last issued: pc %1  %2 (exec mask 0x%3)</source>
        <translation>上一条发射：pc %1  %2（执行掩码 0x%3）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>pc %1 mask 0x%2%3</source>
        <translation>pc %1 掩码 0x%2%3</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> (at barrier)</source>
        <translation>（在屏障）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Control-flow paths: %1</source>
        <translation>控制流路径：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Lane %1, threadIdx.x = %2</source>
        <translation>Lane %1，threadIdx.x = %2</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>no thread (block smaller than the warp)</source>
        <translation>无线程（block 小于 warp）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>exited</source>
        <translation>已退出</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>waiting at a barrier</source>
        <translation>在屏障等待</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>active (follows the current path)</source>
        <translation>活跃（跟随当前路径）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>masked off (waits for another path to reconverge)</source>
        <translation>被屏蔽（等待另一条路径重汇聚）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>State: %1</source>
        <translation>状态：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Executed the last instruction: yes</source>
        <translation>执行了上一条指令：是</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Executed the last instruction: no</source>
        <translation>执行了上一条指令：否</translation>
    </message>
</context>
<context>
    <name>Ripes::IOGpu</name>
    <message>
        <location filename="../../src/io/iogpu.cpp" line="+17"/>
        <source>SMs</source>
        <translation>SM 数量</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Warps per SM</source>
        <translation>每个 SM 的 warp 数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Warp size (lanes)</source>
        <translation>warp 大小（lane 数）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Global memory latency (cycles)</source>
        <translation>全局内存延迟（周期）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>GPU cycles per CPU cycle</source>
        <translation>每个 CPU 周期的 GPU 周期数</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>A small SIMT GPU: a number of streaming multiprocessors (SMs), each holding several warps of lanes (threads) that execute one instruction in lockstep under an active mask.</source>
        <translation>一个小型 SIMT GPU：由若干流式多处理器（SM）组成，每个 SM 驻留多个 warp，warp 中的 lane（线程）在活跃掩码（active mask）控制下锁步执行同一条指令。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Place the kernel machine code (see docs/arch/gpu.md for the ISA and tools/gpu_asm.py for an assembler) in memory, write its address to KERNEL_PC, the number of blocks to GRID_X, the threads per block to BLOCK_X, up to four arguments to ARG0..ARG3, and write 1 (START) to CTRL. Poll STATUS until DONE (bit1) is set; write 1 to that bit to clear it. The GPU advances a configurable number of cycles for every processor cycle.</source>
        <translation>把 kernel 机器码放进内存（ISA 见 docs/arch/gpu.md，汇编器见 tools/gpu_asm.py），把它的地址写入 KERNEL_PC，block 数写入 GRID_X，每个 block 的线程数写入 BLOCK_X，最多四个参数写入 ARG0..ARG3，然后向 CTRL 写 1（START）。轮询 STATUS 直到 DONE（bit1）置位；向该位写 1 清除它。处理器每走一个周期，GPU 前进可配置的若干个周期。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>STATUS: bit0 BUSY, bit1 DONE, bit2 ERROR (e.g. undefined instruction, a block larger than one SM can hold, or a kernel access to the GPU&apos;s own registers), bit3 IRQ (DONE and CTRL.IRQ_EN), bit4 LAUNCH_IGNORED (START written while BUSY). The CYCLES and STATS_* registers expose GPU cycles, issued warp instructions, divergent branches, 32-byte global memory transactions and idle issue slots of the last kernel.</source>
        <translation>STATUS：bit0 BUSY（忙）、bit1 DONE（完成）、bit2 ERROR（错误，例如未定义指令、block 大于一个 SM 的容量，或 kernel 访问了 GPU 自己的寄存器）、bit3 IRQ（DONE 且 CTRL.IRQ_EN）、bit4 LAUNCH_IGNORED（BUSY 时写了 START）。CYCLES 与 STATS_* 寄存器给出上一个 kernel 的 GPU 周期数、发射的 warp 指令数、发散分支数、32 字节全局内存事务数和空闲发射槽数。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Widget: status strip, one row per warp slot of every SM (lane squares show the active mask), kernel disassembly with the current PCs, statistics and the register table. Press &quot;?&quot; for the tutorial.</source>
        <translation>部件：状态条、每个 SM 的每个 warp 槽一行（lane 方块显示活跃掩码）、带当前 PC 的 kernel 反汇编、统计和寄存器表。按 &quot;?&quot; 打开教程。</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>Control: bit0 START (write 1 to launch), bit1 IRQ_EN, bit2 ABORT. Every write sets IRQ_EN from bit1, so writing START alone clears IRQ_EN (write START|IRQ_EN to keep it)</source>
        <translation>控制：bit0 START（写 1 启动）、bit1 IRQ_EN、bit2 ABORT（终止）。每次写 CTRL 都会用 bit1 重写 IRQ_EN，所以只写 START 会清掉 IRQ_EN（要保留请写 START|IRQ_EN）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Status: bit0 BUSY, bit1 DONE, bit2 ERROR, bit3 IRQ, bit4 LAUNCH_IGNORED (bits 1/2/4: write 1 to clear)</source>
        <translation>状态：bit0 BUSY、bit1 DONE、bit2 ERROR、bit3 IRQ、bit4 LAUNCH_IGNORED（bit 1/2/4：写 1 清除）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Byte address of the first kernel instruction</source>
        <translation>kernel 第一条指令的字节地址</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Number of thread blocks in the grid</source>
        <translation>网格（grid）中的线程块（block）数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Threads per block (must fit in one SM)</source>
        <translation>每个 block 的线程数（必须能放进一个 SM）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Kernel argument %1 (read with S_ARG rd, %1)</source>
        <translation>kernel 参数 %1（用 S_ARG rd, %1 读取）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>GPU cycles of the last/current kernel</source>
        <translation>上一个/当前 kernel 的 GPU 周期数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Warp instructions issued</source>
        <translation>已发射的 warp 指令数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Divergent branches (a warp split into two paths)</source>
        <translation>发散分支数（一个 warp 被拆成两条路径）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>32-byte global memory transactions</source>
        <translation>32 字节全局内存事务数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Idle issue slots (SM-cycles with no ready warp)</source>
        <translation>空闲发射槽（没有就绪 warp 的 SM 周期数）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Configuration: [7:0] warp size, [15:8] warps per SM, [23:16] SMs</source>
        <translation>配置：[7:0] warp 大小、[15:8] 每 SM 的 warp 数、[23:16] SM 数</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>%1 SM x %2 warps x %3 lanes</source>
        <translation>%1 个 SM × %2 个 warp × %3 个 lane</translation>
    </message>
</context>
<context>
    <name>Ripes::IOGpuWidget</name>
    <message>
        <location filename="../../src/io/iogpuwidget.cpp" line="+178"/>
        <source>SIMT GPU</source>
        <translation>SIMT GPU</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A teaching model of a GPU: SMs run warps of lanes in lockstep (SIMT)</source>
        <translation>教学用 GPU 模型：SM 以锁步方式（SIMT）运行由 lane 组成的 warp</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Watch warps of threads execute a kernel in lockstep: active masks, branch divergence, memory coalescing and latency hiding.</source>
        <translation>观察线程组成的 warp 如何锁步执行 kernel：活跃掩码、分支发散、合并访存与延迟隐藏。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>What this view shows</source>
        <translation>这个视图展示什么</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Open the GPU tutorial (user guide chapter 09-gpu)</source>
        <translation>打开 GPU 教程（用户手册第 09-gpu 章）</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>GPU state from the STATUS register: IDLE (no kernel or DONE acknowledged), BUSY (kernel running), DONE (finished, not yet acknowledged), ABORTED (stopped by CTRL.ABORT), ERROR (kernel failed)</source>
        <translation>来自 STATUS 寄存器的 GPU 状态：IDLE（空闲：没有 kernel，或 DONE 已被确认）、BUSY（kernel 运行中）、DONE（已完成，尚未确认）、ABORTED（被 CTRL.ABORT 终止）、ERROR（kernel 出错）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>GPU cycles spent by the current or last kernel (the CYCLES register)</source>
        <translation>当前或上一个 kernel 所用的 GPU 周期数（CYCLES 寄存器）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Launch geometry of the current/last kernel: blocks in the grid x threads per block, and blocks not yet dispatched to an SM</source>
        <translation>当前/上一个 kernel 的启动规模：网格中的 block 数 × 每个 block 的线程数，以及尚未派发到 SM 的 block 数</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hardware configuration (change it in the Parameters table of the peripheral)</source>
        <translation>硬件配置（在外设的 Parameters 参数表中修改）</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Why the kernel stopped with an error</source>
        <translation>kernel 因错误停止的原因</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Recent event that changed the GPU state</source>
        <translation>最近改变 GPU 状态的事件</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>No kernel has been launched yet.

To start one, the host program writes the kernel address to KERNEL_PC, the number of blocks to GRID_X, the threads per block to BLOCK_X, the arguments to ARG0-ARG3, and then writes 1 to CTRL.START.
Try the example gpu_vecadd.s (File &gt; Load Example), then press Run or step with the clock button.</source>
        <translation>尚未启动 kernel。

要启动一个 kernel，主机程序依次把 kernel 地址写入 KERNEL_PC、block 数写入 GRID_X、每个 block 的线程数写入 BLOCK_X、参数写入 ARG0-ARG3，最后向 CTRL.START 写 1。
可以试试示例 gpu_vecadd.s（File &gt; Load Example），然后按 Run 运行，或用时钟按钮单步。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Shown until the first kernel launch</source>
        <translation>在第一次启动 kernel 之前显示</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>SMs, warp slots and lanes. Each square is one thread; hover it for details</source>
        <translation>SM、warp 槽和 lane。每个方块是一个线程；鼠标悬停可查看详情</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>The kernel code is read from KERNEL_PC when CTRL.START is written</source>
        <translation>写 CTRL.START 时从 KERNEL_PC 读取 kernel 代码</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>#</source>
        <translation>#</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Address</source>
        <translation>地址</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Machine code</source>
        <translation>机器码</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Instruction</source>
        <translation>指令</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Warps at this PC</source>
        <translation>位于此 PC 的 warp</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kernel disassembly. Highlighted rows are the next instruction of at least one warp; the last column lists those warps as SM/warp slot</source>
        <translation>kernel 反汇编。高亮行是至少一个 warp 的下一条指令；最后一列以 SM/warp 槽列出这些 warp</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Metric</source>
        <translation>指标</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+4"/>
        <source>Value</source>
        <translation>数值</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Unit / meaning</source>
        <translation>单位 / 含义</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Performance counters of the current/last kernel</source>
        <translation>当前/上一个 kernel 的性能计数器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Offset</source>
        <translation>偏移</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Meaning</source>
        <translation>含义</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Memory-mapped registers: name, byte offset from GPU_0_BASE, current value (decoded) and meaning</source>
        <translation>内存映射寄存器：名称、相对 GPU_0_BASE 的字节偏移、当前值（含解码）和含义</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Statistics</source>
        <translation>统计</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Registers</source>
        <translation>寄存器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Performance counters</source>
        <translation>性能计数器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Register table</source>
        <translation>寄存器表</translation>
    </message>
    <message>
        <location line="+72"/>
        <source>ERROR</source>
        <translation>ERROR 错误</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>BUSY</source>
        <translation>BUSY 忙</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>ABORTED</source>
        <translation>ABORTED 已终止</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>DONE</source>
        <translation>DONE 完成</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>IDLE</source>
        <translation>IDLE 空闲</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>GPU cycles: %1</source>
        <translation>GPU 周期：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Grid: %1 block(s) x %2 thread(s) · pending blocks: %3</source>
        <translation>网格：%1 个 block × %2 个线程 · 待派发 block：%3</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>No kernel launched</source>
        <translation>尚未启动 kernel</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 SM x %2 warps x %3 lanes · %4 GPU cycle(s) per CPU cycle</source>
        <translation>%1 个 SM × %2 个 warp × %3 个 lane · 每个 CPU 周期 %4 个 GPU 周期</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Error: %1</source>
        <translation>错误：%1</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Last error (acknowledged): %1</source>
        <translation>上一次错误（已确认）：%1</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>CTRL.START was written while the GPU was BUSY: that launch was ignored (STATUS.LAUNCH_IGNORED, %1 time(s) so far). The running kernel is not affected.</source>
        <translation>GPU 处于 BUSY 时写入了 CTRL.START：该次启动被忽略（STATUS.LAUNCH_IGNORED，迄今 %1 次）。正在运行的 kernel 不受影响。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Reverse execution: the GPU state was restored to the previous CPU cycle (%1 cycle(s) of GPU history left).</source>
        <translation>反向执行：GPU 状态已恢复到上一个 CPU 周期（剩余 %1 个周期的 GPU 历史）。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Reverse execution went past the recorded GPU history: the accelerator was reset to its pre-launch state. Memory the kernel already wrote is not undone; reset the processor and run again to relaunch the kernel.</source>
        <translation>反向执行超出了已记录的 GPU 历史：加速器已复位到启动前状态。kernel 已写入内存的数据不会撤销；请复位处理器并重新运行以再次启动 kernel。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>STATUS.LAUNCH_IGNORED is set: a launch was ignored because the GPU was BUSY.</source>
        <translation>STATUS.LAUNCH_IGNORED 已置位：因 GPU 处于 BUSY，一次启动被忽略。</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Kernel: none captured yet (read from KERNEL_PC on CTRL.START).</source>
        <translation>kernel：尚未捕获（在写 CTRL.START 时从 KERNEL_PC 读取）。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kernel at %1 - %2 instruction(s). Highlighted: next instruction of a warp.</source>
        <translation>kernel 位于 %1 - 共 %2 条指令。高亮：某个 warp 的下一条指令。</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>GPU cycles</source>
        <translation>GPU 周期</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>cycles since the kernel was launched</source>
        <translation>自 kernel 启动以来的周期数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Warp instructions issued</source>
        <translation>已发射的 warp 指令</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>warp-instructions (one per warp per issue)</source>
        <translation>warp 指令（每个 warp 每次发射计一条）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Thread instructions</source>
        <translation>线程指令</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>lane-instructions actually executed</source>
        <translation>实际执行的 lane 指令数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IPC</source>
        <translation>IPC</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>warp-instructions per GPU cycle (max = number of SMs)</source>
        <translation>每个 GPU 周期的 warp 指令数（最大值 = SM 数）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>SIMD efficiency</source>
        <translation>SIMD 效率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>thread instructions / (warp instructions x warp size)</source>
        <translation>线程指令数 / (warp 指令数 × warp 大小)</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>register %1</source>
        <translation>寄存器 %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>widget only</source>
        <translation>仅部件显示</translation>
    </message>
    <message>
        <location line="-43"/>
        <source>Issue-slot utilization</source>
        <translation>发射槽利用率</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>issued / (issued + idle slots)</source>
        <translation>发射数 / (发射数 + 空闲槽)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Idle issue slots</source>
        <translation>空闲发射槽</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>SM-cycles in which no warp was ready (latency not hidden)</source>
        <translation>没有就绪 warp 的 SM 周期数（延迟未被隐藏）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Divergent branches</source>
        <translation>发散分支</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>events: a BRA split a warp into two paths</source>
        <translation>事件：一条 BRA 把 warp 拆成两条路径</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Memory instructions</source>
        <translation>访存指令</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>LDG/STG warp-instructions</source>
        <translation>LDG/STG warp 指令</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Memory transactions</source>
        <translation>访存事务</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>32-byte segments moved</source>
        <translation>传输的 32 字节段数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Coalesced accesses</source>
        <translation>合并访问</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+3"/>
        <source>%1 instr / %2 txn</source>
        <translation>%1 条指令 / %2 个事务</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>used the minimum number of 32-byte segments</source>
        <translation>使用了最少数量的 32 字节段</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uncoalesced accesses</source>
        <translation>非合并访问</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>needed extra segments (strided or scattered addresses)</source>
        <translation>需要额外的段（跨步或分散的地址）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared-memory bank conflicts</source>
        <translation>共享内存 bank 冲突</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 instr / %2 cycles</source>
        <translation>%1 条指令 / %2 个周期</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>LDS/STS with conflicts / extra serialisation cycles</source>
        <translation>发生冲突的 LDS/STS 指令数 / 额外的串行化周期</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Barrier arrivals</source>
        <translation>屏障到达次数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>warps that reached BAR (counted once per warp)</source>
        <translation>到达 BAR 的 warp 数（每个 warp 只计一次）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blocks completed</source>
        <translation>已完成的 block</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 of %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>thread blocks finished / blocks in the grid</source>
        <translation>已完成的线程块 / 网格中的 block 总数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ignored launches</source>
        <translation>被忽略的启动</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CTRL.START writes while BUSY</source>
        <translation>BUSY 时写入 CTRL.START 的次数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reverse history</source>
        <translation>反向执行历史</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 cycle(s), %2 KiB</source>
        <translation>%1 个周期，%2 KiB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>saved GPU states for Reverse; %1 bytes per changed cycle</source>
        <translation>为 Reverse 保存的 GPU 状态；每个有变化的周期 %1 字节</translation>
    </message>
</context>
</TS>
