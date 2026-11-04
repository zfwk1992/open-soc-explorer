# 16 FAQ 与故障排除

> **你将学到**
> - 构建、启动、汇编、运行中最常见问题的原因和解决办法
> - 为什么命令行模式里用外设符号会报 `Unknown opcode`
> - C 示例提示"没有编译器"时怎么办
> - 深色/浅色主题、界面语言、设置保存在哪里
> - 反向执行在加速器上的限制，以及让模拟更快的技巧
> - 如何报告问题（bug）
>
> **前置条件**
> - 读过[第 02 章](02-quickstart.md)和[第 03 章](03-gui-tour.md)，知道各标签页和工具栏按钮的名字

本章按"问题 → 原因 → 解决"组织。先在下面的目录里找到最接近的问题。

---

## 1. 构建与启动

### Q1.1 `build.ps1` 报 `Toolchain component missing`

**原因**：`P:\workspace\soc\toolchain` 下缺少 `cmake.exe`、`ninja.exe`、MinGW 的 `gcc.exe`/`g++.exe`、`cmake\mingw-bigobj-fixup.cmake` 或 Qt 的 `Qt6Core.dll` 之一。

**解决**：按 `docs/arch/PLAN.md` 模块 F 在用户目录下补装缺失组件（`pip` 装进 `toolchain\venv`，`aqt` 装 Qt 与 MinGW）。不要改用系统级安装，也不要修改系统 PATH。

### Q1.2 CMake 找不到 Qt6 / Qt6Charts

**原因**：Qt 版本低于 6.8、没有安装 Charts 模块，或 `CMAKE_PREFIX_PATH` 没指向 Qt。

**解决**：安装带 Charts 的 Qt 6.8+，配置时传 `-DCMAKE_PREFIX_PATH=<Qt 目录>`；改完后用 `build.ps1 -Configure`（或删除 `build/CMakeCache.txt`）重新配置。详见[第 01 章](01-install.md)第 6 节。

### Q1.3 编译到一半内存耗尽或机器卡死

**原因**：处理器模型的源文件模板很重，每个编译进程约需 1.5 GB 内存。

**解决**：减少并行数：`build.ps1 -Jobs 2`，或 `cmake --build build -j 2`。

### Q1.4 缺少子模块（`external/VSRTL` 等不存在）

**解决**：`git submodule update --init --recursive`。

### Q1.5 下载时出现 TLS / 证书错误

**解决**：配置正确的代理或根证书。**不要**关闭 TLS 校验（例如 `git config http.sslVerify false`、`pip --trusted-host` 之类的做法），这会让下载内容可被篡改。

### Q1.6 双击 `Ripes.exe` 提示缺少 `Qt6Core.dll`

**原因**：Qt 运行库不在 PATH 上。

**解决**：在启动它的 PowerShell 会话中执行 `$env:PATH = "P:\workspace\soc\toolchain\Qt\6.8.3\mingw_64\bin;" + $env:PATH` 后再运行；或把所需 DLL 部署到 `build` 目录（例如用 Qt 的 `windeployqt`，待核实）。

### Q1.7 程序在服务器 / 远程终端上一启动就退出

**原因**：没有显示器。

**解决**：设置 `QT_QPA_PLATFORM=offscreen`。注意离屏模式下任何对话框都看不见，见 Q3.4。

---

## 2. 汇编与运行

### Q2.1 命令行模式运行 GPU/NPU 示例报 `Unknown opcode 'li'`

**现象**：

```text
ERROR: Error during assembly:
INFO: Unknown opcode 'li'
```

**原因**：`GPU_0_BASE`、`TENSOR_UNIT_0_*`、`LED_MATRIX_0_BASE` 等符号由 **I/O** 标签页中**已经创建的外设**导出。命令行模式不创建任何外设，这些符号未定义；汇编器无法把带未知符号的 `li`/`la` 匹配到合法形式，于是报 "Unknown opcode"，而不是"未知符号"。

**解决**：这类示例只能在图形界面运行：

1. 切到 **I/O** 标签页，在 **Available devices** 列表中双击需要的外设（例如 **SIMT GPU**）。
2. 检查左下 **I/O exports** 里出现了程序用到的符号（例如 `GPU_0_BASE`）。
3. 再用 **File → Load Example...** 加载程序。

详见[第 15 章](15-cli.md)第 6 节。

### Q2.2 图形界面里也报 `Unknown opcode 'li'`（或符号相关错误）

**原因**：外设没创建，或者创建的编号不同。符号名里带外设编号：第一个 GPU 是 `GPU_0_...`；如果你删掉后再创建，可能变成 `GPU_1_...`（待核实）。

**解决**：看 **I/O exports** 中实际的符号名，与程序一致即可；必要时关闭多余外设、重新创建。

### Q2.3 `mul`、`div`、`rem` 报 `Unknown opcode`

**原因**：当前处理器没有启用 **M** 扩展。

**解决**：GUI：**Select processor** → 右侧 **ISA Exts.** 勾选 `M` → **OK**。CLI：加 `--isaexts M`。

### Q2.4 某一行标红，但我看不出哪里错

**解决**：把鼠标悬停在红色行上看错误信息。常见原因：

- 拼错指令或寄存器名（`addi x1, x1, 1` 写成 `adi`）。
- 立即数超出范围（`addi` 的立即数是 12 位有符号数，-2048…2047；更大的数用 `li`）。
- 标签重复定义或未定义。
- 在 `.data` 段写了指令，或在 `.text` 段写了 `.word` 以外不支持的伪操作（待核实）。

修好之前，右栏的 **Executable code** 保持上一次成功汇编的程序——所以"运行结果没变"往往说明源码其实有错误。

### Q2.5 C 示例提示 "No C compiler set"

**现象**：把 **Input type** 切到 **C**，或加载 C 示例时弹出：

```text
No C compiler set.

Provide a path to a valid C compiler under:
 Edit->Settings->Editor
```

**原因**：没有配置 RISC-V 交叉编译器。注意：提示里说的是 **Editor** 页，但编译器路径实际在 **Edit → Settings → Compiler** 页。

**解决**：

1. 安装裸机 RISC-V 工具链（`riscv64-unknown-elf-gcc`，需支持 32 位 multilib）。
2. **Edit → Settings → Compiler**，在 **Compiler path:** 中填入或 **Browse** 到 `riscv64-unknown-elf-gcc`；路径有效时输入框变绿。
3. 回到 **Editor**，选 **C**，按 **Ctrl+B** 编译。

如果暂时没有编译器，可以先用 **Load Example... → ELF (precompiled C)** 中预编译好的 `RanPi-RV32`。详见[第 05 章](05-c-programs.md)。

### Q2.6 程序一直运行不结束

**原因**：程序没有调用退出 `ecall`（`li a7, 10` + `ecall`），PC 跑出了程序末尾，或者在等待外设（例如轮询 GPU 的 DONE 位，但 GPU 没创建或没启动）。

**解决**：再按一次 **Run**（F8）暂停；在 **Instruction memory** 中看 PC 停在哪里；在可疑循环上设断点用 **Auto clock** 观察。CLI 中使用 `--timeout`。

### Q2.7 Clock / Run 按钮是灰的

**原因**：程序已经退出（执行了 exit `ecall`），或者正在 Run / Auto clock。

**解决**：按 **Reset**（F3）。

### Q2.8 控制台里的数字前后多了空格

这是 `ecall` 打印整数的格式，例如 `Factorial value of  7 is  5040`。比较输出时请规范化空白。

---

## 3. 外设与加速器

### Q3.1 I/O 标签页里不能新建外设 / 不能改参数 / 不能关闭外设

**原因**：处理器正在 **Run**。运行期间外设列表、参数表和关闭按钮都被禁用。

**解决**：先暂停（再按一次 F8）。

### Q3.2 外设改了参数后，程序里的地址不对了

**原因**：外设的大小可能随参数变化（例如 LED 矩阵的宽高），后面外设的地址会随之移动。

**解决**：在程序中**始终使用导出的符号**（`LED_MATRIX_0_BASE` 等），不要写死数值地址。修改参数后程序会自动重新汇编（待核实）；C 程序需要重新编译。

### Q3.3 GPU / NPU 部件没有变化

**检查**：

1. 是否加载了对应示例（`gpu_vecadd.s` / `npu_matmul.s`），并且没有汇编错误？
2. 是否在运行（F5/F6/F8）？加速器每个 CPU 周期前进一步，处理器不动它就不动。
3. 用 **Run** 时部件刷新会被节流，可改用 **Auto clock** 观察过程。

更多见[第 09 章](09-gpu.md)、[第 10 章](10-npu.md)。

### Q3.4 程序 / 截图工具在离屏模式下"卡住"

**原因**：某个操作弹出了对话框（例如 Windows 上的 `--help`、命令行参数错误、文件错误提示），在 `QT_QPA_PLATFORM=offscreen` 下对话框不可见，程序在等待你点击。

**解决**：先在正常桌面环境里复现，看是什么对话框；脚本中给外层调用加超时。

---

## 4. 反向执行（Reverse）

### Q4.1 Reverse 按钮是灰的

**原因**：

- 刚复位，或已经撤销到头；
- 已达到 **Edit → Settings → Simulator → Max. undo cycles**（默认 100）的上限；
- 刚用 **Run** 跑过——Run 模式不保留逐周期的撤销信息（待核实精确规则）。

**解决**：需要更长的撤销历史时调大 **Max. undo cycles**（会占用更多内存）；要反复观察某一段，在其前面设断点，用 **Auto clock** 或 **Clock** 走过去。

### Q4.2 反向执行后，GPU / NPU 的状态不对

**原因**：Reverse 恢复的是处理器、寄存器和内存（缓存模拟器也会跟着撤销）。**SIMT GPU** 和 **Tensor Unit (NPU)** 外设是独立的模型，第一阶段的实现**不响应反向执行**：处理器倒退了，加速器却停留在"未来"的状态，例如 DONE 位已置位、计数器已增加。

按第二阶段计划，修复后会是以下两种之一（以实际版本为准，待核实）：

- 加速器在忙碌时为每个周期保存快照，Reverse 时一起倒退（快照数量同样受 **Max. undo cycles** 限制）；或
- Reverse 时把加速器**复位到启动前**，并在部件上明确显示"反向执行后加速器已复位"。

**建议**：观察加速器时，用 **Reset**（F3）从头重跑，而不是用 Reverse 倒退；需要精确观察某个周期时，在启动 kernel / 敲门铃的指令上设断点，再用 F5 单步。

---

## 5. 界面、主题与设置

### Q5.1 如何切换深色 / 浅色主题？

**Edit → Settings → Environment → Color scheme**：**System**（跟随操作系统）、**Light**、**Dark**。立即生效，电路图、工具栏图标和 I/O 背景都会跟着变。

如果选了 **System** 但没有变暗，说明操作系统没有报告深色模式，或者你的平台 / Qt 版本不支持自动检测（待核实）；直接选 **Dark** 即可。

### Q5.2 如何切换界面语言？

**Edit → Settings → Environment → Language / 语言**，选 **English** 或 **简体中文**，**重启**程序。选 **System** 时按操作系统区域设置决定。详见[第 03 章](03-gui-tour.md)第 7 节。

### Q5.3 部分文字切换语言后仍是英文

**原因**：翻译还没有覆盖到（例如新加的外设说明），或者这段文字来自程序 / 拓扑数据本身。

**解决**：按第 7 节报告缺失的翻译，注明所在标签页和原文。

### Q5.4 设置保存在哪里？如何恢复默认？

程序使用 Qt 的 `QSettings`，组织名和应用名都是 `Ripes`：

| 平台 | 位置 |
|---|---|
| Windows | 注册表 `HKEY_CURRENT_USER\Software\Ripes\Ripes` |
| Linux | `~/.config/Ripes/Ripes.conf` |
| macOS | `~/Library/Preferences/` 下以 Ripes 命名的 `.plist` 文件（具体文件名待核实） |

这里保存的内容包括：所有 **Settings** 对话框里的选项、当前处理器与扩展与布局、编辑器中的源代码、已创建的外设及参数、缓存预设、窗口位置、界面语言等。

**恢复默认**：关闭程序后删除上表中的注册表键或文件，再启动。**注意**：这会同时丢掉编辑器中未保存的程序和自定义缓存预设，请先保存。

> 与上游 Ripes 共用同一个设置位置（待核实）：如果同一台电脑上还装了上游 Ripes，两者会互相读写这些设置。

### Q5.5 窗口太小，信息显示不全

各区域之间有可拖动的分隔条（splitter），可以调整大小；I/O 外设子窗口可以拖出主窗口。Processor 标签页的电路图可以 Ctrl+滚轮缩放；启动时会自动"适配窗口"。

窗口可以放进 1280×800 的屏幕。放不下的部分会出现滚动条，而不是被截断：欢迎面板（可用右上角的 × 关闭）、Cache 标签页的左栏、I/O 标签页的设备区。SIMT GPU 和 Tensor Unit 窗口较窄时会把各面板上下排列；拖动分隔条把设备区拉宽后又会左右并排。

---

## 6. 性能

### Q6.1 Run 很慢

按影响从大到小：

1. **换处理器**：**ISA simulator** 最快（没有电路模拟），**Single-cycle processor** 其次；流水线处理器最慢。缓存模拟也推荐用单周期处理器。
2. **降低缓存曲线开销**：**Settings → Environment → Max. cache plot cycles** 调小（例如 1000）。
3. **降低界面刷新**：**Max. UI updates per second** 调小。
4. **关闭不需要的外设**：尤其是 **Screen** 和加速器的可视化部件。
5. **关闭 VCD 跟踪**：**Settings → Simulator → Enable simulation VCD traces** 取消勾选。
6. 批量实验用命令行模式（[第 15 章](15-cli.md)），完全没有界面开销。

### Q6.2 Auto clock 太慢 / 太快

调整工具栏中 **Auto clock** 旁边的间隔框（1–10000 ms）。

### Q6.3 Clock rate 数值跳动很大

调大 **Settings → Simulator → Clock rate averaging window (ms)**。

### Q6.4 程序运行很久后变卡、内存增长

调小 **Max. cache access traces**、**Max. pipeline diagram cycles**、**Max. undo cycles**。

---

## 7. 如何报告问题

1. **先确认能复现**：记下最少的操作步骤（"打开示例 X → 选择处理器 Y → 按 F8 → 看到 Z"）。
2. **收集信息**：
   - 版本：**Help → About**（或当前版本的 **Help → Version**）显示的版本号；
   - 处理器与 ISA：状态栏右侧 `Processor: ... ISA: ...`；
   - 操作系统、界面语言、配色方案；
   - 使用的程序（附上 `.s`/`.c` 文件或示例名），已创建的外设及参数；
   - 错误信息原文、截图；命令行问题附上完整命令和输出。
3. **能用命令行复现的，附上 CLI 命令**——这样维护者可以直接运行。
4. **提交位置**：本项目（open-soc-explorer）新增的功能（GPU、NPU、Architecture 标签页、中文翻译、本手册）的问题，请提交到本项目的问题跟踪处或告诉课程老师（待核实具体地址）；与上游 Ripes 相同的基础功能问题，可以先在上游仓库 <https://github.com/mortbopet/Ripes/issues> 搜索是否已知。
5. 手册内容错误：注明章节文件名（例如 `docs/guide/zh/16-faq.md`）和段落。

---

## 练习

1. 在命令行模式下运行 `examples/assembly/leds.s`，看到什么错误？用本章的解释说明原因，然后在 GUI 中让它正确运行。
2. 打开 **Select processor**，取消 `M` 扩展，加载 `factorial.s`，哪一行标红？错误信息是什么？
3. 找到你电脑上保存 Ripes 设置的位置，看看里面有哪些键。
4. 用 Single-cycle processor 和 5-stage processor 分别运行同一个长程序（例如 ELF 示例 `RanPi-RV32`），比较 Run 用时。

## 小结

- 构建问题多半是 Qt 版本/模块、子模块、内存或 PATH；别关 TLS 校验。
- 命令行模式没有外设，外设符号会导致误导性的 `Unknown opcode 'li'`；这类示例在 GUI 中先创建外设再加载。
- 乘除法需要 `M` 扩展；C 程序需要在 **Settings → Compiler** 配置 RISC-V 编译器。
- Reverse 有撤销上限，且加速器外设不一定跟着倒退；观察加速器时优先用 Reset 重跑。
- 主题与语言在 **Settings → Environment**；设置保存在 `QSettings`（Windows 注册表 `HKCU\Software\Ripes\Ripes`）。
- 想要更快：ISA simulator / 单周期处理器、减少绘图与刷新、关闭不需要的外设，批量实验用 CLI。

下一章：[17 术语表](17-glossary.md)。
