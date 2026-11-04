<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en_US">
<context>
    <name>Ripes::IOTensorUnit</name>
    <message>
        <location filename="../../src/io/iotensorunit.cpp" line="+25"/>
        <source>Array size (N x N)</source>
        <translation>阵列大小 (N x N)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>DMA words/cycle</source>
        <translation>DMA 字/周期</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>PE cell size (pixels)</source>
        <translation>PE 单元格大小（像素）</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>A weight-stationary int8 systolic-array tensor accelerator (NPU) driven by a descriptor ring in memory.</source>
        <translation>权重驻留（weight-stationary）的 int8 脉动阵列张量加速器（NPU），由内存中的描述符环（descriptor ring）驱动。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Write 32-byte descriptors {opcode, srcA, srcB, dst, M, N, K, flags} (little-endian words) to QUEUE_BASE + idx*32, then write the index of the next free slot to TAIL (the doorbell). The unit consumes entries from HEAD up to TAIL, advancing HEAD as each one completes.</source>
        <translation>把 32 字节的描述符 {opcode, srcA, srcB, dst, M, N, K, flags}（小端字）写到 QUEUE_BASE + idx*32，然后把下一个空闲槽的索引写入 TAIL（门铃 doorbell）。加速器从 HEAD 一直处理到 TAIL，每完成一个描述符 HEAD 加一。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Opcodes: 1 = MATMUL (C[M][N] int32 = A[M][K] int8 x B[K][N] int8), 2 = RELU (M*N int32, in place allowed), 3 = ADD (M*N int32), 0 = NOP. flags: bit0 fuse ReLU into MATMUL, bit1 suppress DESC_DONE.</source>
        <translation>操作码：1 = MATMUL（C[M][N] int32 = A[M][K] int8 x B[K][N] int8），2 = RELU（M*N 个 int32，允许原地），3 = ADD（M*N 个 int32），0 = NOP。flags：bit0 把 ReLU 融合进 MATMUL，bit1 不置 DESC_DONE。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>STATUS: bit0 busy, bits[7:4] state (0 idle, 1 fetch, 2 dma-in, 3 compute, 4 dma-out, 5 complete), bit8 queue empty, bit9 error, bit10 weight load, bit11 DMA fault. IRQ_STATUS/IRQ_EN: bit0 descriptor done, bit1 queue empty, bit2 error; IRQ_STATUS is write-1-to-clear. CTRL: bit0 soft reset, bit1 clear counters. FAULT_ADDR: address of the last refused DMA access (e.g. a ring or matrix placed inside the unit&apos;s own registers).</source>
        <translation>STATUS：bit0 忙，bits[7:4] 状态（0 空闲，1 取描述符，2 DMA 读入，3 计算，4 DMA 写回，5 完成），bit8 队列空，bit9 错误，bit10 装载权重，bit11 DMA 故障。IRQ_STATUS/IRQ_EN：bit0 描述符完成，bit1 队列空，bit2 错误；IRQ_STATUS 写 1 清零。CTRL：bit0 软复位，bit1 清计数器。FAULT_ADDR：最近一次被拒绝的 DMA 访问地址（例如把环或矩阵放进了加速器自己的寄存器区）。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>The unit advances one cycle per processor cycle; CYCLES therefore doubles as a free-running cycle counter for timing CPU code. Reverse execution rewinds the unit together with the processor.</source>
        <translation>加速器每个处理器周期前进一个周期，因此 CYCLES 也可以当作免费的周期计数器来给 CPU 代码计时。反向执行时加速器会与处理器一起回退。</translation>
    </message>
</context>
<context>
    <name>Ripes::IOTensorUnitView</name>
    <message>
        <location filename="../../src/io/iotensorunitwidget.cpp" line="+727"/>
        <source>Descriptor state machine. The current state is filled and marked with ▶; dashed arrows are the loops between tiles and descriptors. Hover a state for an explanation.</source>
        <translation>描述符状态机。当前状态被填充颜色并用 ▶ 标出；虚线箭头是 tile 之间和描述符之间的循环。鼠标悬停在状态上可查看说明。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The N x N systolic array. Each PE shows a (activation), w (stationary weight) and acc (partial sum). Hover a PE for details.</source>
        <translation>N x N 脉动阵列。每个 PE 显示 a（激活值）、w（驻留权重）和 acc（部分和）。鼠标悬停在 PE 上可查看详情。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Details: descriptor ring, DMA and cycle statistics, interrupt bits and the register table.</source>
        <translation>详细信息：描述符环、DMA 与周期统计、中断位和寄存器表。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Drag the handle to resize the array view and the detail tabs.</source>
        <translation>拖动分隔条可调整阵列视图和详情标签页的大小。</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Tensor Unit (NPU) — systolic-array accelerator</source>
        <translation>张量处理器 Tensor Unit (NPU) —— 脉动阵列加速器</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>A teaching model of a TPU-like accelerator attached to the CPU through memory-mapped registers.</source>
        <translation>一个类似 TPU 的教学用加速器模型，通过内存映射寄存器与 CPU 相连。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The CPU writes descriptors into a ring in memory and rings the TAIL doorbell; the unit fetches each one, moves data by DMA, multiplies on the N x N array and advances HEAD.</source>
        <translation>CPU 把描述符写进内存中的环并敲 TAIL 门铃；加速器逐个取出描述符，用 DMA 搬运数据，在 N x N 阵列上做乘法，然后推进 HEAD。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>What this peripheral does and what to look at.</source>
        <translation>这个外设做什么、应该看哪里。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Current state of the unit (also in STATUS[7:4]) and whether it is busy or reported an error. Note: after the doorbell and between two descriptors the unit spends one cycle in IDLE, so a loop polling STATUS.BUSY can exit too early - poll HEAD (or IRQ_STATUS) instead.</source>
        <translation>加速器当前状态（也在 STATUS[7:4] 中）以及是否忙或报告了错误。注意：敲门铃之后以及两个描述符之间，加速器会在 IDLE 停留一个周期，所以轮询 STATUS.BUSY 的循环可能过早退出——请改为轮询 HEAD（或 IRQ_STATUS）。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Open the user guide chapter on the NPU (descriptor ring, doorbell, DMA, matmul walkthrough).</source>
        <translation>打开用户手册中的 NPU 章节（描述符环、门铃、DMA、矩阵乘逐步讲解）。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Cycle counters (in CPU cycles) and how many cycles of NPU state can be undone with reverse execution.</source>
        <translation>周期计数（单位：CPU 周期），以及反向执行可以撤销多少个周期的 NPU 状态。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Hints, errors and reverse-execution notices.</source>
        <translation>提示、错误和反向执行的通知。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>QUEUE_BASE is the ring address, QUEUE_SIZE the number of 32-byte slots; the unit executes slots HEAD..TAIL-1.</source>
        <translation>QUEUE_BASE 是环的地址，QUEUE_SIZE 是 32 字节槽的个数；加速器执行 HEAD..TAIL-1 这些槽。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Slot</source>
        <translation>槽</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+98"/>
        <source>Address</source>
        <translation>地址</translation>
    </message>
    <message>
        <location line="-98"/>
        <source>Pointer / status</source>
        <translation>指针 / 状态</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Opcode</source>
        <translation>操作码</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>srcA</source>
        <translation>srcA</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>srcB</source>
        <translation>srcB</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>dst</source>
        <translation>dst</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>M</source>
        <translation>M</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>N</source>
        <translation>N</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>K</source>
        <translation>K</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Flags</source>
        <translation>标志</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Every slot of the descriptor ring, decoded field by field. The slot in flight is highlighted. Slot contents are re-read from memory whenever the simulation is paused.</source>
        <translation>描述符环的每个槽，逐字段解码。正在执行的槽被高亮。每当模拟暂停时，槽的内容会从内存重新读取。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Slot index in the ring (0 .. QUEUE_SIZE-1)</source>
        <translation>槽在环中的索引（0 .. QUEUE_SIZE-1）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Byte address of the slot: QUEUE_BASE + slot*32</source>
        <translation>槽的字节地址：QUEUE_BASE + slot*32</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>HEAD = next slot the unit executes, TAIL = next free slot for the CPU</source>
        <translation>HEAD = 加速器下一个要执行的槽，TAIL = CPU 下一个可用的空闲槽</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>+0 opcode: 0 NOP, 1 MATMUL, 2 RELU, 3 ADD</source>
        <translation>+0 opcode：0 NOP，1 MATMUL，2 RELU，3 ADD</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>+4 srcA: address of A (int8 MxK) or of the first int32 input</source>
        <translation>+4 srcA：A 的地址（int8 MxK）或第一个 int32 输入的地址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>+8 srcB: address of B (int8 KxN) or of the second int32 input</source>
        <translation>+8 srcB：B 的地址（int8 KxN）或第二个 int32 输入的地址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>+12 dst: address of the int32 result</source>
        <translation>+12 dst：int32 结果的地址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>+16 M: rows of A and C</source>
        <translation>+16 M：A 和 C 的行数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>+20 N: columns of B and C</source>
        <translation>+20 N：B 和 C 的列数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>+24 K: columns of A = rows of B (MATMUL only)</source>
        <translation>+24 K：A 的列数 = B 的行数（仅 MATMUL）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>+28 flags: bit0 RELU_FUSED, bit1 NO_IRQ</source>
        <translation>+28 flags：bit0 RELU_FUSED，bit1 NO_IRQ</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Descriptor ring</source>
        <translation>描述符环</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Direction and progress of the DMA transfer in flight.</source>
        <translation>正在进行的 DMA 传输的方向和进度。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Bytes of the current DMA transfer moved so far (bytes). The DMA moves &apos;DMA words/cycle&apos; 32-bit words per cycle.</source>
        <translation>当前 DMA 传输已搬运的字节数（字节）。DMA 每周期搬运 “DMA 字/周期” 个 32 位字。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Quantity</source>
        <translation>数量</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+52"/>
        <source>Value</source>
        <translation>值</translation>
    </message>
    <message>
        <location line="-52"/>
        <location line="+21"/>
        <location line="+31"/>
        <source>Meaning</source>
        <translation>含义</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>DMA traffic and cycle counters since the last reset or CTRL.CLEAR_COUNTERS. All cycles are CPU cycles.</source>
        <translation>自上次复位或 CTRL.CLEAR_COUNTERS 以来的 DMA 流量和周期计数。所有周期都是 CPU 周期。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>DMA &amp;&amp; cycles</source>
        <translation>DMA 与周期</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The interrupt line is asserted while IRQ_STATUS &amp; IRQ_EN is non-zero.</source>
        <translation>当 IRQ_STATUS &amp; IRQ_EN 非零时，中断线有效。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Bit</source>
        <translation>位</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+31"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="-31"/>
        <source>IRQ_EN</source>
        <translation>IRQ_EN</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IRQ_STATUS</source>
        <translation>IRQ_STATUS</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>IRQ_EN (offset 0x14) enables a source; IRQ_STATUS (offset 0x18) latches events until software clears them.</source>
        <translation>IRQ_EN（偏移 0x14）使能中断源；IRQ_STATUS（偏移 0x18）锁存事件，直到软件清除。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>IRQ_STATUS is write-1-to-clear (W1C): writing 1 to a bit clears it, writing 0 leaves it unchanged, so an interrupt handler clears exactly the events it handled. The bits are sticky: they stay set until software clears them.

STATUS.ERROR is different: it describes only the most recent descriptor and clears itself when the next descriptor is fetched. Use IRQ_STATUS.ERROR to find out whether any descriptor failed since you last looked.

Note: the Ripes RISC-V processors have no external interrupt input, so the CPU is never actually interrupted. The bits are observable only: poll IRQ_STATUS (or HEAD) to emulate an interrupt handler.</source>
        <translation>IRQ_STATUS 是写 1 清零（W1C）：向某位写 1 清除该位，写 0 不改变，因此中断处理程序只清除它处理过的事件。这些位是粘滞的：在软件清除之前一直保持置位。

STATUS.ERROR 不同：它只描述最近一个描述符，取下一个描述符时会自动清除。要知道自上次查看以来是否有描述符失败，请看 IRQ_STATUS.ERROR。

注意：Ripes 的 RISC-V 处理器没有外部中断输入，所以 CPU 永远不会真正被中断。这些位只能被观察：轮询 IRQ_STATUS（或 HEAD）来模拟中断处理程序。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>How the interrupt bits work and why the CPU must poll them in Ripes.</source>
        <translation>中断位如何工作，以及为什么在 Ripes 中 CPU 必须轮询它们。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Interrupts</source>
        <translation>中断</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Offset</source>
        <translation>偏移</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Access</source>
        <translation>访问</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Memory-mapped registers: name, byte offset from the peripheral base, absolute address, access (R/W/RW), current value and its decoded meaning.</source>
        <translation>内存映射寄存器：名称、相对外设基址的字节偏移、绝对地址、访问方式（R/W/RW）、当前值及其解码含义。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Registers</source>
        <translation>寄存器</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>invalid (%1)</source>
        <translation>非法（%1）</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>unknown bits</source>
        <translation>未知位</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>none</source>
        <translation>无</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>not busy</source>
        <translation>不忙</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>state=%1</source>
        <translation>状态=%1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Error: the last descriptor had an invalid opcode (%1). It was skipped; HEAD still advanced.</source>
        <translation>错误：上一个描述符的操作码非法（%1）。它被跳过；HEAD 照常前进。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Error: the last descriptor had M, N or K outside 1..%1 (M=%2, N=%3, K=%4). It was skipped; HEAD still advanced.</source>
        <translation>错误：上一个描述符的 M、N 或 K 不在 1..%1 范围内（M=%2，N=%3，K=%4）。它被跳过；HEAD 照常前进。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Error: DMA fault at address %1. The unit cannot access its own registers (or an unmapped region) by DMA - check QUEUE_BASE and the descriptor addresses. The descriptor was aborted; HEAD still advanced.</source>
        <translation>错误：地址 %1 处发生 DMA 故障。加速器不能通过 DMA 访问自己的寄存器（或未映射区域）——请检查 QUEUE_BASE 和描述符中的地址。该描述符被中止；HEAD 照常前进。</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>BUSY</source>
        <translation>忙</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+8"/>
        <source>ERROR</source>
        <translation>错误</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>done, idle</source>
        <translation>已完成，空闲</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>idle, no work yet</source>
        <translation>空闲，尚无任务</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Cycles: %1  |  Busy cycles: %2  |  MACs: %3  |  Array utilization: %4 %  |  Reverse history: %5 cycles (%6 KiB)</source>
        <translation>周期：%1  |  忙周期：%2  |  乘加：%3  |  阵列利用率：%4 %  |  反向执行历史：%5 个周期（%6 KiB）</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Reverse execution went further back than the NPU history (limit: the &apos;rewind stack size&apos; setting, or the unit was created/reset later). The NPU state was NOT rewound for this step; reset the processor to start over cleanly.</source>
        <translation>反向执行回退得比 NPU 的历史记录更远（上限为 “回退栈大小” 设置，或者外设是之后才创建/复位的）。这一步 NPU 状态没有被回退；请复位处理器以干净地重新开始。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>(STATUS.ERROR clears itself when the next descriptor is fetched; IRQ_STATUS.ERROR stays set until software writes 1 to it.)</source>
        <translation>（STATUS.ERROR 在取下一个描述符时自动清除；IRQ_STATUS.ERROR 会一直保持，直到软件向它写 1。）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Reversed one cycle: the NPU state and the memory written by its DMA in that cycle were rewound together with the CPU.</source>
        <translation>已反向执行一个周期：NPU 状态以及它在该周期通过 DMA 写入的内存都已随 CPU 一起回退。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>No work submitted yet. Set QUEUE_BASE and QUEUE_SIZE, write a 32-byte descriptor into slot TAIL, then write TAIL+1 to TAIL (the doorbell). Try the example npu_matmul.s.</source>
        <translation>尚未提交任务。设置 QUEUE_BASE 和 QUEUE_SIZE，把 32 字节描述符写入 TAIL 指向的槽，然后把 TAIL+1 写入 TAIL（门铃）。可以试试示例 npu_matmul.s。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ring configured (%1 slots at %2). Write a descriptor into slot %3 and then write %4 to TAIL to ring the doorbell.</source>
        <translation>环已配置（%1 个槽，位于 %2）。把描述符写入槽 %3，然后向 TAIL 写入 %4 来敲门铃。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>QUEUE_BASE = %1   QUEUE_SIZE = %2 slots   HEAD = %3 (next to execute)   TAIL = %4 (next free)   pending = %5</source>
        <translation>QUEUE_BASE = %1   QUEUE_SIZE = %2 个槽   HEAD = %3（下一个执行）   TAIL = %4（下一个空闲）   待处理 = %5</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>(showing the first %1 slots)</source>
        <translation>（只显示前 %1 个槽）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Warning: QUEUE_SIZE is 0 - descriptor fetch is disabled and the doorbell is ignored.</source>
        <translation>警告：QUEUE_SIZE 为 0——描述符获取被禁用，门铃会被忽略。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Warning: QUEUE_SIZE is 1 - one slot always stays empty (HEAD == TAIL means empty), so nothing can be submitted. Use at least 2 slots.</source>
        <translation>警告：QUEUE_SIZE 为 1——总有一个槽必须保持空（HEAD == TAIL 表示空），所以无法提交任何任务。请至少使用 2 个槽。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Ring full: %1 of %2 slots pending. Do not submit more until HEAD advances - writing TAIL onto HEAD would make the ring look empty and the queued work would be lost.</source>
        <translation>环已满：%2 个槽中有 %1 个待处理。在 HEAD 前进之前不要再提交——把 TAIL 写成等于 HEAD 会让环看起来是空的，排队的任务会丢失。</translation>
    </message>
    <message>
        <location line="+57"/>
        <source>▶ in flight</source>
        <translation>▶ 执行中</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>pending</source>
        <translation>待处理</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>free / done</source>
        <translation>空闲 / 已完成</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>HEAD: next slot the unit executes. TAIL: next free slot (the CPU writes here, then rings the doorbell).</source>
        <translation>HEAD：加速器下一个要执行的槽。TAIL：下一个空闲槽（CPU 写到这里，然后敲门铃）。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>MMIO region - not a valid ring address</source>
        <translation>MMIO 区域——不是有效的环地址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>(read when paused)</source>
        <translation>（暂停时读取）</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>DMA: fetching a 32-byte descriptor (host → NPU)</source>
        <translation>DMA：正在读取 32 字节描述符（主存 → NPU）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>DMA_IN: host memory → NPU buffers, %1 of %2 bytes</source>
        <translation>DMA_IN：主存 → NPU 缓冲区，%2 字节中已传 %1 字节</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>DMA_OUT: NPU results → host memory, %1 of %2 bytes</source>
        <translation>DMA_OUT：NPU 结果 → 主存，%2 字节中已传 %1 字节</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>DMA idle (last transfer: %1 bytes %2)</source>
        <translation>DMA 空闲（上一次传输：%1 字节，%2）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>out</source>
        <translation>写出</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>in</source>
        <translation>读入</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>%v / %m bytes</source>
        <translation>%v / %m 字节</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+19"/>
        <source>%1 cycles</source>
        <translation>%1 个周期</translation>
    </message>
    <message>
        <location line="-13"/>
        <source>DMA bytes in</source>
        <translation>DMA 读入字节</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+2"/>
        <location line="+2"/>
        <source>%1 bytes</source>
        <translation>%1 字节</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>host memory → NPU: descriptors, A/B tiles, vector inputs</source>
        <translation>主存 → NPU：描述符、A/B tile、向量输入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>DMA bytes out</source>
        <translation>DMA 写出字节</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NPU → host memory: int32 results</source>
        <translation>NPU → 主存：int32 结果</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>DMA_BYTES (total)</source>
        <translation>DMA_BYTES（合计）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>register 0x20: in + out</source>
        <translation>寄存器 0x20：读入 + 写出</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>DMA transfers</source>
        <translation>DMA 传输次数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>jobs started: descriptor fetches, tile loads, write-backs</source>
        <translation>启动的传输任务：取描述符、装载 tile、写回</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>DMA bandwidth</source>
        <translation>DMA 带宽</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 words/cycle = %2 bytes/cycle</source>
        <translation>%1 字/周期 = %2 字节/周期</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>parameter &apos;DMA words/cycle&apos;</source>
        <translation>参数 “DMA 字/周期”</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CYCLES</source>
        <translation>CYCLES</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>register 0x1C: every CPU cycle since reset</source>
        <translation>寄存器 0x1C：复位以来的每个 CPU 周期</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>BUSY_CYCLES</source>
        <translation>BUSY_CYCLES</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 cycles (%2 %)</source>
        <translation>%1 个周期（%2 %）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>register 0x2C: cycles not in IDLE</source>
        <translation>寄存器 0x2C：不处于 IDLE 的周期</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>  in FETCH_DESC</source>
        <translation>  处于 FETCH_DESC</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>reading descriptors</source>
        <translation>读取描述符</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>  in DMA_IN</source>
        <translation>  处于 DMA_IN</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>loading inputs</source>
        <translation>装载输入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>  in COMPUTE</source>
        <translation>  处于 COMPUTE</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>weight load + systolic passes (or SIMD for RELU/ADD)</source>
        <translation>装载权重 + 脉动计算（RELU/ADD 时为 SIMD）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>  in DMA_OUT</source>
        <translation>  处于 DMA_OUT</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>writing results</source>
        <translation>写回结果</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>  in COMPLETE</source>
        <translation>  处于 COMPLETE</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>HEAD++ and interrupt bits</source>
        <translation>HEAD++ 与中断位</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>MACS</source>
        <translation>MACS</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1 MACs</source>
        <translation>%1 次乘加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>register 0x24: useful int8 multiply-accumulates</source>
        <translation>寄存器 0x24：有效的 int8 乘加次数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Array utilization</source>
        <translation>阵列利用率</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1 %</source>
        <translation>%1 %</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>MACS / (BUSY_CYCLES x N²); low = memory-bound</source>
        <translation>MACS / (BUSY_CYCLES x N²)；低 = 受内存带宽限制（memory-bound）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Descriptors completed</source>
        <translation>已完成描述符</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 (%2 with error)</source>
        <translation>%1（其中 %2 个出错）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>including rejected / aborted ones</source>
        <translation>包括被拒绝 / 中止的描述符</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Interrupt line: ASSERTED (IRQ_STATUS &amp; IRQ_EN = %1)</source>
        <translation>中断线：有效（IRQ_STATUS &amp; IRQ_EN = %1）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Interrupt line: not asserted (IRQ_STATUS &amp; IRQ_EN = 0)</source>
        <translation>中断线：无效（IRQ_STATUS &amp; IRQ_EN = 0）</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>a descriptor completed (not set when flags.NO_IRQ)</source>
        <translation>一个描述符完成（flags.NO_IRQ 时不置位）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>the last pending descriptor completed (HEAD == TAIL)</source>
        <translation>最后一个待处理描述符完成（HEAD == TAIL）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>a descriptor was rejected or aborted by a DMA fault</source>
        <translation>一个描述符被拒绝，或因 DMA 故障被中止</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>1 enabled</source>
        <translation>1 已使能</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>0 disabled</source>
        <translation>0 未使能</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IRQ_EN bit %1: write 1 to enable this source</source>
        <translation>IRQ_EN 第 %1 位：写 1 使能该中断源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>1 pending</source>
        <translation>1 待处理</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>0 clear</source>
        <translation>0 已清除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IRQ_STATUS bit %1: set by the unit; write 1 to clear (W1C)</source>
        <translation>IRQ_STATUS 第 %1 位：由加速器置位；写 1 清零（W1C）</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>address of the descriptor ring in memory</source>
        <translation>描述符环在内存中的地址</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 slots x 32 bytes (0 = ring disabled)</source>
        <translation>%1 个槽 x 32 字节（0 = 禁用环）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>next slot the unit executes; advances after each descriptor</source>
        <translation>加速器下一个要执行的槽；每完成一个描述符前进一次</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>next free slot; writing it is the doorbell (%1 pending)</source>
        <translation>下一个空闲槽；写它就是敲门铃（%1 个待处理）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> (ERROR clears on the next fetch; BUSY is 0 for one cycle after the doorbell and between descriptors - poll HEAD)</source>
        <translation>（ERROR 在取下一个描述符时清除；敲门铃后及描述符之间 BUSY 会有一个周期为 0——请轮询 HEAD）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>enabled sources: %1</source>
        <translation>已使能的中断源：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>latched events: %1; sticky, write 1 to clear; observable only (the CPU is never interrupted)</source>
        <translation>已锁存的事件：%1；粘滞，写 1 清零；只能观察（CPU 永远不会被中断）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>cycles since reset (free-running CPU cycle counter)</source>
        <translation>复位以来的周期数（自由运行的 CPU 周期计数器）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>bytes moved by DMA (in + out)</source>
        <translation>DMA 搬运的字节数（读入 + 写出）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>useful int8 multiply-accumulates</source>
        <translation>有效的 int8 乘加次数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>array is %1 x %1 = %2 MACs/cycle peak</source>
        <translation>阵列为 %1 x %1 = 每周期峰值 %2 次乘加</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>cycles not in IDLE (%1 % of all cycles)</source>
        <translation>不处于 IDLE 的周期（占全部周期的 %1 %）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>write-only: bit0 SOFT_RESET, bit1 CLEAR_COUNTERS</source>
        <translation>只写：bit0 SOFT_RESET，bit1 CLEAR_COUNTERS</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>address of the refused DMA access</source>
        <translation>被拒绝的 DMA 访问地址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>no DMA fault since the last descriptor fetch</source>
        <translation>自上次取描述符以来没有 DMA 故障</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>unmapped</source>
        <translation>未映射</translation>
    </message>
</context>
<context>
    <name>Ripes::NpuArrayView</name>
    <message>
        <location line="-1041"/>
        <source>PE(k=%1, n=%2)
a = %3 (activation from the left%4)
w = %5 (stationary weight B[k][n]%6)
acc = %7 (psum_in + a*w, passed down)
%8</source>
        <translation>PE(k=%1, n=%2)
a = %3（从左侧流入的激活值%4）
w = %5（驻留权重 B[k][n]%6）
acc = %7（psum_in + a*w，向下传递）
%8</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>, none</source>
        <translation>，无</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>, padding</source>
        <translation>，填充</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>MAC performed this cycle</source>
        <translation>本周期执行了乘加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>no MAC this cycle</source>
        <translation>本周期没有乘加</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Systolic array %1 x %1 (active only in COMPUTE)</source>
        <translation>脉动阵列 %1 x %1（只在 COMPUTE 时工作）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Systolic array %1 x %1: loading weights - B rows are pushed in bottom row first and shift down</source>
        <translation>脉动阵列 %1 x %1：正在装载权重——B 的行从最底行开始依次推入并向下移动</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Systolic array %1 x %1: %2 of %3 PEs did a MAC this cycle</source>
        <translation>脉动阵列 %1 x %1：本周期 %3 个 PE 中有 %2 个执行了乘加</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>↓ weights B (stationary), partial sums flow down</source>
        <translation>↓ 权重 B（驻留），部分和向下流动</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>↓ results C leave the bottom row</source>
        <translation>↓ 结果 C 从最底行流出</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>activations A enter here →</source>
        <translation>激活值 A 从这里进入 →</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>No work submitted yet.
Fill a descriptor in memory and write TAIL (the doorbell) to start the unit.</source>
        <translation>尚未提交任务。
在内存中填好描述符并写 TAIL（门铃）来启动加速器。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Legend</source>
        <translation>图例</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>MAC this cycle; darker = larger |acc| (heat map)</source>
        <translation>本周期有乘加；颜色越深 = |acc| 越大（热图）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>weight loaded, no activation this cycle</source>
        <translation>已装载权重，本周期无激活值</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>empty / padding (outside the current tile)</source>
        <translation>空 / 填充（在当前 tile 之外）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Thick border = activation present. Cell text: a, w (top), acc (bottom). Hover a PE for details.</source>
        <translation>粗边框 = 有激活值。单元格文字：a、w（上），acc（下）。鼠标悬停在 PE 上可查看详情。</translation>
    </message>
</context>
<context>
    <name>Ripes::NpuStateMachineView</name>
    <message>
        <location line="-463"/>
        <source>IDLE: waiting for the doorbell (HEAD == TAIL).</source>
        <translation>IDLE：等待门铃（HEAD == TAIL）。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>FETCH_DESC: reading the 32-byte descriptor at QUEUE_BASE + HEAD*32 by DMA, then validating it.</source>
        <translation>FETCH_DESC：通过 DMA 读取 QUEUE_BASE + HEAD*32 处的 32 字节描述符，然后校验。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>DMA_IN: copying the input tile (A and B, or a vector chunk) from host memory into the on-chip buffers.</source>
        <translation>DMA_IN：把输入 tile（A 和 B，或一段向量）从主存复制到片上缓冲区。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>COMPUTE: shifting the weights in, then streaming the activations through the systolic array.</source>
        <translation>COMPUTE：先把权重移入，然后让激活值流过脉动阵列。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>DMA_OUT: writing the int32 result tile back to host memory.</source>
        <translation>DMA_OUT：把 int32 结果 tile 写回主存。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>COMPLETE: HEAD advances by one and the IRQ_STATUS bits are set (DESC_DONE / QUEUE_EMPTY / ERROR).</source>
        <translation>COMPLETE：HEAD 加一，并置位 IRQ_STATUS（DESC_DONE / QUEUE_EMPTY / ERROR）。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>, tile (m,n,k) = (%1,%2,%3) of (%4,%5,%6)</source>
        <translation>，tile (m,n,k) = (%1,%2,%3)，共 (%4,%5,%6)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>doorbell seen: fetching starts next cycle</source>
        <translation>已看到门铃：下一周期开始取描述符</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>waiting for work</source>
        <translation>等待任务</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>reading descriptor in slot %1</source>
        <translation>正在读取槽 %1 的描述符</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 of %2 bytes transferred</source>
        <translation>%2 字节中已传输 %1 字节</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>SIMD pass over elements %1..%2 of %3</source>
        <translation>SIMD 处理元素 %1..%2（共 %3 个）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>weight load: %1 of %2 rows shifted in (B rows enter at the top, last row k=%3 first, and move down one row per cycle)</source>
        <translation>装载权重：已移入 %2 行中的 %1 行（B 的行从顶部进入，最后一行 k=%3 最先进入，每周期向下移动一行）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>systolic pass: %1 of %2 array cycles done (tileM + 2N - 2)</source>
        <translation>脉动计算：已完成 %2 个阵列周期中的 %1 个（tileM + 2N - 2）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>finishing a rejected descriptor (ERROR)</source>
        <translation>正在结束一个被拒绝的描述符（ERROR）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>finishing descriptor: HEAD++</source>
        <translation>正在结束描述符：HEAD++</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>invalid descriptor or DMA fault</source>
        <translation>非法描述符或 DMA 故障</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>next K-tile</source>
        <translation>下一个 K-tile</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>next output tile</source>
        <translation>下一个输出 tile</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>HEAD++, set IRQ_STATUS; fetch the next descriptor if HEAD != TAIL</source>
        <translation>HEAD++，置位 IRQ_STATUS；若 HEAD != TAIL 则取下一个描述符</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>current</source>
        <translation>当前</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Current state: %1 (STATUS[7:4] = %2)</source>
        <translation>当前状态：%1（STATUS[7:4] = %2）</translation>
    </message>
</context>
</TS>
