# open-soc-explorer 扩展计划（2026-10-28）

本项目基于 Ripes（RISC-V 可视化模拟器，Qt6 + VSRTL）。本次扩展加入四个教学模块：

| 模块 | 内容 | 形式 |
|---|---|---|
| A. GPU 运行原理 | SIMT GPU 微架构模型（warp、lane、分支发散、合并访存、延迟隐藏） | 文档 + 纯 C++ 仿真核 + MMIO 外设 `IOGpu` + 示例程序 |
| B. 张量处理器与 CPU 交互 | 脉动阵列 NPU + 描述符环/门铃/DMA/中断 的主从交互模型 | 文档 + 纯 C++ 仿真核 + MMIO 外设 `IOTensorUnit` + 示例程序 |
| C. NeoGeo 架构 | 68000 + Z80 + LSPC + YM2610 的双 CPU 街机架构 | 文档 + 架构拓扑 JSON（供架构浏览器使用） |
| D. PS3 架构 | Cell BE（PPE + 8×SPE + EIB + MFC）+ RSX + XDR | 文档 + 架构拓扑 JSON |
| E. 架构浏览器（Architecture tab） | 通用的 JSON 驱动框图/数据流演示器，承载 C/D（以及 A/B 的系统级视图） | Qt 新 Tab |
| F. 工具链与集成 | 用户态安装 CMake/Ninja/Qt6/MinGW，拉取子模块，编译并跑测试 | 构建环境 |

## 总体约定

- **语言**：文档以中文为主，技术术语保留英文（warp、systolic array、doorbell…）。代码注释、标识符用英文，遵循仓库 `.clang-format`。
- **目录**：
  - `docs/arch/<module>.md`：各模块文档；`docs/README.md` 加链接。
  - `src/accel/gpu/`、`src/accel/npu/`：**不依赖 Qt** 的 C++17 仿真核（header + cpp），可单独单元测试。
  - `src/io/iogpu.*`、`src/io/iotensorunit.*`：Qt 外设（继承 `IOBase`），包装仿真核并提供可视化 widget。
  - `src/archexplorer/`：架构浏览器 Tab（`ArchExplorerTab : RipesTab`）。
  - `resources/arch/*.json`：架构拓扑描述；通过 `resources/arch/arch.qrc` 打包。
  - `examples/assembly/`、`examples/C/`：示例程序，登记到 `examples/examples.qrc`。
  - `test/tst_<module>.cpp`：QTest 单元测试，登记到 `test/CMakeLists.txt`（`create_qtest`）。
- **外设框架要点**（见 `src/io/iobase.h`、`src/io/iomanager.cpp`）：
  - `ioRead/ioWrite(offset, …)`：CPU 访问外设寄存器。
  - `memRead/memWrite` 两个 std::function 成员：外设主动访问总线（DMA）。
  - `ProcessorHandler::get()` 的 `processorClocked()` 信号：每个 CPU 周期触发一次，可用来让加速器**异步**地每周期前进一步（而不是在门铃写入时同步算完），这样 CPU 的轮询/中断等待才有意义。
  - `processorReset()` 信号 / `IOBase::reset()`：复位钩子。
  - 新外设需在 `src/io/ioregistry.h` 登记三处（enum、Titles、Factories）。**为避免并行冲突，各 agent 不要直接改 `ioregistry.h`、`mainwindow.cpp/.h`、`docs/README.md`、`examples/examples.qrc`、`test/CMakeLists.txt`、`src/CMakeLists.txt`、顶层 `CMakeLists.txt`，而是在最终报告中给出需要插入的精确片段，由集成步骤统一合并。**
- **构建约束**：本机当前没有 CMake/Qt，模块 F 负责搭建。写代码的 agent 需要自行严格审查语法/头文件/签名（对照已有外设如 `src/io/ioledmatrix.*`、`src/io/ioscreen.*`），集成阶段统一编译修错。

## 架构拓扑 JSON schema（模块 C/D/E 共用）

```jsonc
{
  "id": "neogeo",                   // 唯一 id
  "title": "SNK Neo Geo (MVS/AES)",
  "subtitle": "1990, 双 CPU 街机/家用机架构",
  "description": "一段总体介绍（纯文本，可含换行）",
  "blocks": [
    {
      "id": "m68k", "label": "Motorola 68000", "sublabel": "12 MHz, 16/32-bit",
      "kind": "cpu",                // cpu | gpu | accel | memory | cache | io | bridge | audio | video | bus | misc
      "x": 40, "y": 40, "w": 180, "h": 70,          // 画布坐标（逻辑像素）
      "details": "点击块后在侧栏显示的详细说明（纯文本，多段用空行）",
      "props": { "Clock": "12 MHz", "Bus": "16-bit data / 24-bit addr" }   // 键值属性表
    }
  ],
  "buses": [
    {
      "id": "b_main", "from": "m68k", "to": "workram",
      "label": "16-bit data bus", "kind": "bus",     // bus | dma | irq | mailbox | link
      "bidirectional": true,
      "props": { "Width": "16 bit" }
    }
  ],
  "flows": [
    {
      "id": "sound_cmd", "title": "68000 → Z80 发送音效命令",
      "description": "演示主 CPU 如何通过 sound latch 通知音频 CPU",
      "steps": [
        { "highlight": ["m68k"], "buses": ["b_latch"], "text": "68000 向 REG_SOUND (0x320000) 写入命令字节 …" },
        { "highlight": ["z80"],  "buses": [],          "text": "Z80 收到 NMI，从 port 0 读取命令 …" }
      ]
    }
  ]
}
```

- 画布建议 1200×700 逻辑像素内；块按层排布（例如 CPU 在上，内存在下，I/O 在右）。
- `kind` 决定配色；浏览器为每个 kind 分配固定颜色，并在图例中显示。
- `flows[].steps[].highlight` 列出高亮的 block id；`buses` 列出高亮的 bus id。
- 文件必须是严格 JSON（无注释），UTF-8。

## 模块详细说明

### A. GPU 运行原理（owner: agent "gpu"）
目标：让学生在 Ripes 里用 RISC-V 主机程序启动一个 kernel，观察 SIMT 执行。
1. `src/accel/gpu/simtgpu.h/.cpp`（无 Qt）：
   - 参数：SM 数量、每 SM warp 槽数、warp 宽度（默认 2 SM × 4 warp × 8 lane，便于可视化；可配置到 32 lane）。
   - 微型 kernel ISA（32-bit 定长，自定义编码，文档中给出编码表和汇编助记符）：`LDG/STG`（global）、`LDS/STS`（shared）、`ADD/SUB/MUL/FMA/AND/OR/SHL/SHR`（整型，寄存器/立即数）、`SETP`（比较置谓词）、`BRA/@P BRA`（分支）、`S_TID/S_BID/S_NTID`（读 threadIdx/blockIdx/blockDim 到寄存器）、`BAR`（block 内 barrier）、`EXIT`。
   - 每 warp 一个 PC + active mask；分支发散用重汇聚栈（immediate post-dominator 简化为"分支目标较小 PC 先执行"或显式 `JOIN` 指令，二选一，文档说明）。
   - warp 调度：loose round-robin；可统计 issue slot 利用率、发散次数、合并访存事务数（每 warp 一次 LDG 按 32B 段计算事务数）。
   - `step()` 每次推进一个 SM 周期；`launch(grid, block, pc, args)`；`done()`。
   - 访存通过回调 `std::function<uint32_t(uint32_t addr)>` / 写回调，便于接到 Ripes 总线。
   - 提供一个微型汇编器 `assembleKernel(const std::string&) -> std::vector<uint32_t>`（方便测试和示例）。
2. `src/io/iogpu.h/.cpp`：`IOGpu : IOBase`。寄存器：`CTRL`(bit0 start, bit1 irq_en)、`STATUS`(bit0 busy, bit1 done)、`KERNEL_PC`、`GRID_X`、`BLOCK_X`、`ARG0..ARG3`、`CYCLES`、`STATS_*`。参数（IOParam）：SM 数、warp 数、lane 数。widget 用 QPainter 画每个 SM 的 warp 槽：active mask 每 lane 一个小方块（绿=活跃，灰=屏蔽，红=退出），显示当前 PC 和指令助记符，以及利用率统计。连接 `processorClocked` 每 CPU 周期推进 1 步（可参数化比例）。
3. `docs/arch/gpu.md`：SIMT vs SIMD vs 多核；warp/lane/block/grid；发散与重汇聚；内存层次（寄存器/shared/L1/L2/HBM）、合并访存；延迟隐藏与 occupancy；GPU 与 CPU 的提交路径（命令队列、kernel launch、同步）；本模块的 ISA 编码表；使用教程（含截图占位）。
4. 示例：`examples/assembly/gpu_vecadd.s`（RISC-V 主程序：把预汇编的 kernel 机器码 `.word` 放进内存，填好 A/B/C 数组地址，写 MMIO 启动，轮询 STATUS，校验结果，经 ecall 打印）。同时给出 kernel 的助记符源码放在注释中。
5. `test/tst_gpu.cpp`：仿真核测试（vecadd、分支发散、barrier）。

### B. 张量处理器与 CPU 交互（owner: agent "npu"）
1. `src/accel/npu/tensorunit.h/.cpp`（无 Qt）：
   - weight-stationary 脉动阵列 N×N（默认 4×4，可配 8×8），int8 输入 × int8 权重 → int32 累加；按周期推进：权重装载、输入斜向注入、部分和向下流动、结果流出。能够记录每个 PE 当前持有的 (a, w, acc) 用于可视化。
   - 任务描述符（放在主存中，由 CPU 填写）：`{opcode(MATMUL/RELU/ADD), srcA, srcB, dst, M, N, K, flags}`，环形队列 `QUEUE_BASE + idx*32`。
   - DMA 引擎：从主存按 tile 搬入 A/B 到片上 buffer，计算完成后写回 C；每周期搬 1 个字（可配带宽），体现"搬数据比算慢"。
   - 完成后置 `IRQ_STATUS` 并（可选）调用中断回调。
2. `src/io/iotensorunit.h/.cpp`：`IOTensorUnit : IOBase`。寄存器：`QUEUE_BASE`、`QUEUE_SIZE`、`HEAD`(R, 设备消费位置)、`TAIL`(RW, CPU 写即门铃)、`STATUS`、`IRQ_EN`、`IRQ_STATUS`(W1C)、`CYCLES`、`DMA_BYTES`、`MACS`。widget：脉动阵列网格（每 PE 显示 a/w/acc），队列 head/tail 指针，DMA 进度条，周期/利用率统计。
3. `docs/arch/tensor_cpu.md`：为什么需要张量加速器（算术强度、roofline）；脉动阵列数据流（TPU v1 式）；CPU↔加速器交互：MMIO 寄存器、描述符环、门铃、DMA、完成通知（轮询 vs 中断）、内存一致性（cache flush/invalidate、fence）；offload 成本模型（何时值得 offload）；量化（int8）简介；本模块寄存器表与描述符格式；教程。
4. 示例：`examples/C/npu_matmul.c` 与 `examples/assembly/npu_matmul.s`（填描述符、敲门铃、轮询完成、CPU 对照验算、打印周期数对比）。
5. `test/tst_npu.cpp`。

### C. NeoGeo（owner: agent "neogeo"）
1. `docs/arch/neogeo.md`：系统概览（MVS/AES，1990）；68000 主 CPU（12 MHz，内存映射：P-ROM、Work RAM 64KB、BIOS、palette RAM、LSPC 寄存器、REG_SOUND 等）；Z80 音频 CPU（4 MHz，M1 ROM，64KB 地址空间，bank 切换）；YM2610（FM 4ch + SSG 3ch + ADPCM-A 6ch + ADPCM-B 1ch）；LSPC 视频（381 sprites × 16×16 tiles 最多 32 tile 高、sprite chaining、shrinking、fix layer、每行 96 sprite 限制、VRAM 布局、palette 2 bank × 256 palettes × 16 色、timing/VBL/HBL 中断、watchdog）；卡带 ROM 类型（P/S/M/V/C）与总线；68000↔Z80 通信（sound latch、NMI、应答）；帧渲染流程；与现代 SoC 的对照（无帧缓冲、无 DMA、tile-based）。
2. `resources/arch/neogeo.json`：拓扑 + 至少 3 个 flow：音效命令、一帧渲染（VBL→写 VRAM→LSPC 扫描线合成）、卡带 ROM 读取路径。
3. `examples/assembly/neogeo_mailbox.s`（可选）：在 Ripes RISC-V 上用两段代码+内存变量模拟 "sound latch + 应答" 协议的教学示例（用 LED Matrix 显示状态）。

### D. PS3 / Cell（owner: agent "ps3"）
1. `docs/arch/ps3_cell.md`：PS3 系统概览（2006）；Cell BE：PPE（PowerPC 64, 3.2 GHz, 顺序双发射, 2-way SMT, 32K L1 ×2, 512KB L2, VMX）；SPE（SXU 128×128-bit 寄存器、256KB Local Store、双流水线 even/odd、无 cache、分支提示；MFC：DMA 队列 16 项、每次 ≤16KB、list DMA、mailbox 4+1、signal notification；SPE 问题状态与隔离模式）；EIB（4 环 × 16B，仲裁，96 B/cycle 峰值）；MIC → XDR 256MB 25.6 GB/s；BIC/FlexIO → RSX；RSX（G70 派生，256MB GDDR3，vertex/pixel shader，可读 XDR）；软件模型（SPURS 作业系统、双缓冲 DMA、Local Store 手工管理、对齐要求）；与 GPU SIMT/NPU 的对照（显式 vs 隐式数据搬运）；为什么难编程。
2. `resources/arch/ps3.json`：拓扑 + flows：PPE 通过 mailbox 派发任务给 SPE、SPE 双缓冲 DMA、PPE→RSX 命令缓冲提交。
3. `examples/C/spe_double_buffer.c`（可选）：在 Ripes 上演示"显式 DMA + 双缓冲"思想（用纯软件 memcpy 模拟 DMA）。

### E. 架构浏览器（owner: agent "archexplorer"）
1. `src/archexplorer/`：`archmodel.h/.cpp`（用 QJsonDocument 解析上述 schema）、`archscene.h/.cpp`（QGraphicsScene/Items：块、总线连线带箭头、kind 配色）、`archexplorertab.h/.cpp/.ui`（左侧：架构下拉 + flow 列表 + 步骤文本；中间：QGraphicsView 可缩放拖拽；右侧：选中块的 details/props；工具栏：上一步/下一步/播放/重置）。
2. `src/archexplorer/CMakeLists.txt` 用 `create_ripes_lib(archexplorer LINK_TO_RIPES_LIB)`；`resources/arch/arch.qrc` 打包 JSON；在 `mainwindow.cpp/.h` 登记 Tab（agent 给出 patch 片段，集成时合并）。
3. 自带 `resources/arch/example_minimal.json` 以及 `docs/arch/arch_explorer.md`（schema 说明 + 如何添加架构）。
4. `test/tst_archmodel.cpp`：用 QJsonDocument 加载并校验所有 `resources/arch/*.json`（引用的 block/bus id 必须存在）。

### F. 工具链（owner: agent "toolchain"）
- 全部装到 `P:\workspace\soc\toolchain\`（不污染系统）：`pip install --user cmake ninja aqtinstall`，`aqt install-qt windows desktop 6.x win64_mingw -m qtcharts`，`aqt install-tool windows desktop tools_mingw*`。
- `git submodule update --init --recursive`。
- 写 `P:\workspace\soc\build.ps1`：配置 + 编译 + 跑 `test/tst_*`。
- 先在未修改的基线上编译通过，再供集成使用。
