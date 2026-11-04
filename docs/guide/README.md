# open-soc-explorer 用户手册 / User Guide

> 本手册有中文与英文两个版本，章节一一对应。中文目录在前，English index follows below.
> This guide exists in Chinese and English with identical chapter structure. Scroll down for the English index.

---

## 中文

open-soc-explorer 是基于 Ripes（RISC-V 可视化处理器模拟器）的教学用 SoC 探索器。除了 Ripes 原有的处理器流水线、缓存、内存与 MMIO 外设模拟之外，它还加入了 SIMT GPU、张量处理器（NPU）两个可编程加速器外设，以及一个能演示 Neo Geo、PS3/Cell 等真实机器数据流的 **Architecture** 标签页（架构浏览器）。

本手册面向**第一次使用的学生**：每章都从"你将学到 / 前置条件"开始，按"做一步 → 看到什么 → 为什么"推进，最后有练习和小结。

### 目录

| # | 章节 | 一句话简介 |
|---|---|---|
| 01 | [安装与构建](zh/01-install.md) | 在 Windows（用户态工具链 + `build.ps1`）和 Linux/macOS 上编译、运行测试、常见构建错误 |
| 02 | [10 分钟上手](zh/02-quickstart.md) | 打开示例、汇编、单步、看寄存器和内存、运行到结束、复位、反向执行 |
| 03 | [GUI 总览](zh/03-gui-tour.md) | 主窗口、六个标签页、工具栏、处理器选择、设置、语言切换、快捷键、应用内帮助 |
| 04 | [RISC-V 汇编教程](zh/04-riscv-assembly.md) | 寄存器、指令类、伪指令、数据段、分支/循环、函数调用、`ecall` |
| 05 | [用 C 编程](zh/05-c-programs.md) | 配置 RISC-V 编译器、编译参数、C 示例、常见问题 |
| 06 | [处理器模型与流水线](zh/06-processors-pipelines.md) | 单周期 / 5 级 / 6 级双发射、冒险与转发、流水线图、反向执行、VCD |
| 07 | [缓存模拟](zh/07-cache.md) | 缓存参数、命中/缺失、替换策略、预设、统计图表 |
| 08 | [MMIO 外设](zh/08-mmio-peripherals.md) | 地址映射、LED 矩阵/开关/方向键/实时时钟/屏幕、符号用法 |
| 09 | [SIMT GPU](zh/09-gpu.md) | 寄存器表、kernel ISA、`gpu_asm.py`、vecadd 逐步、分支发散/合并访存/bank 冲突实验 |
| 10 | [张量处理器 NPU](zh/10-npu.md) | 描述符环、门铃、轮询与中断、DMA、矩阵乘逐步、CPU 与 NPU 周期对比 |
| 11 | [架构浏览器](zh/11-arch-explorer.md) | 加载拓扑、数据流步进/播放、缩放、方块详情、内置拓扑 |
| 12 | [Neo Geo 导览](zh/12-neogeo.md) | 按数据流讲解 Neo Geo 架构 + `neogeo_mailbox.s` 实验 |
| 13 | [PS3 / Cell 导览](zh/13-ps3-cell.md) | 按数据流讲解 Cell BE + `spe_double_buffer.c` 实验 |
| 14 | [编写自己的拓扑](zh/14-authoring-topologies.md) | 拓扑 JSON schema、双语字段、校验器、渲染工具 |
| 15 | [命令行模式](zh/15-cli.md) | `--mode cli` 的全部参数、遥测报告、批量运行、限制 |
| 16 | [FAQ 与故障排除](zh/16-faq.md) | 构建、运行、汇编报错、外设、性能、设置存放位置、报告问题 |
| 17 | [术语表](zh/17-glossary.md) | 中英对照术语：SIMT、warp、coalescing、systolic、doorbell、EIB、MFC… |

### 阅读路线

**学生（第一次接触计算机体系结构）**

1. [01 安装](zh/01-install.md)（如果老师已经给你装好了程序，可以跳过）
2. [02 10 分钟上手](zh/02-quickstart.md) → [03 GUI 总览](zh/03-gui-tour.md)
3. [04 RISC-V 汇编](zh/04-riscv-assembly.md) → [06 处理器与流水线](zh/06-processors-pipelines.md) → [07 缓存](zh/07-cache.md)
4. [08 MMIO 外设](zh/08-mmio-peripherals.md) → [09 GPU](zh/09-gpu.md) → [10 NPU](zh/10-npu.md)
5. [11 架构浏览器](zh/11-arch-explorer.md) → [12 Neo Geo](zh/12-neogeo.md) → [13 PS3/Cell](zh/13-ps3-cell.md)
6. 遇到问题随时查 [16 FAQ](zh/16-faq.md) 和 [17 术语表](zh/17-glossary.md)

**教师（准备课程与实验）**

1. [01 安装](zh/01-install.md)：在机房批量部署时重点看"预编译版本与源码构建"和"无头运行"
2. [03 GUI 总览](zh/03-gui-tour.md)：了解学生会看到的全部界面，以及如何切换中英文界面
3. 选择实验章节：流水线（[06](zh/06-processors-pipelines.md)）、缓存（[07](zh/07-cache.md)）、加速器（[09](zh/09-gpu.md)、[10](zh/10-npu.md)）、系统架构（[11](zh/11-arch-explorer.md)–[13](zh/13-ps3-cell.md)）；每章结尾都有练习，09/10 带答案
4. [15 命令行模式](zh/15-cli.md)：批量运行学生提交的程序并收集周期数、CPI、缓存命中率等指标
5. [14 编写拓扑](zh/14-authoring-topologies.md)：为课程准备自己的架构图与数据流动画

**开发者（修改或扩展本项目）**

1. [01 安装](zh/01-install.md)：源码构建、`build.ps1` 参数、运行测试、常见构建错误
2. [15 命令行模式](zh/15-cli.md)：回归测试和自动化脚本的入口
3. [14 编写拓扑](zh/14-authoring-topologies.md)：架构浏览器的数据格式
4. 设计文档：`docs/arch/PLAN.md`、`docs/arch/PLAN2.md`、`docs/arch/gpu.md`、`docs/arch/tensor_cpu.md`、`docs/arch/arch_explorer.md`
5. [16 FAQ](zh/16-faq.md) 中的"如何报告问题"

### 切换界面语言

界面支持英文和简体中文：

1. 打开菜单 **Edit → Settings**（编辑 → 设置）。
2. 在左侧选择 **Environment**（环境）页。
3. 在 **Language / 语言** 下拉框中选择 **System**（跟随系统）、**English** 或 **简体中文**。
4. **重新启动程序**后生效。

选择 **System** 时，如果操作系统的区域设置是中文，界面就显示中文，否则显示英文。应用内帮助（**Help → User Guide / 用户手册**）会按当前界面语言打开本手册的 `zh/` 或 `en/` 版本。

> 本手册按正在开发中的目标界面编写（见 `docs/arch/PLAN2.md` §1.2），个别控件名称在最终版本中可能略有不同，此类位置在正文中标注了"（待核实）"。

---

## English

open-soc-explorer is a teaching-oriented SoC explorer built on Ripes, the visual RISC-V processor simulator. On top of Ripes' processor pipelines, caches, memory and MMIO peripheral simulation, it adds two programmable accelerator peripherals — a SIMT GPU and a tensor processor (NPU) — plus an **Architecture** tab (the architecture explorer) that animates the data flow of real machines such as the Neo Geo and the PS3/Cell.

This guide is written for **first-time students**: every chapter starts with "What you will learn / Prerequisites", proceeds as "do a step → what you see → why", and ends with exercises and a summary.

### Contents

| # | Chapter | In one sentence |
|---|---|---|
| 01 | [Installing and building](en/01-install.md) | Build on Windows (user-space toolchain + `build.ps1`) and on Linux/macOS, run the tests, fix common build errors |
| 02 | [10-minute quick start](en/02-quickstart.md) | Open an example, assemble, step, inspect registers and memory, run to completion, reset, reverse |
| 03 | [GUI tour](en/03-gui-tour.md) | Main window, the six tabs, toolbars, processor selection, settings, language switch, shortcuts, in-app help |
| 04 | [RISC-V assembly tutorial](en/04-riscv-assembly.md) | Registers, instruction classes, pseudo-instructions, data sections, branches/loops, function calls, `ecall` |
| 05 | [Programming in C](en/05-c-programs.md) | Configure a RISC-V compiler, compiler flags, C examples, common problems |
| 06 | [Processor models and pipelines](en/06-processors-pipelines.md) | Single-cycle / 5-stage / 6-stage dual-issue, hazards and forwarding, pipeline diagram, reverse execution, VCD |
| 07 | [Cache simulation](en/07-cache.md) | Cache parameters, hits/misses, replacement policies, presets, statistics plots |
| 08 | [MMIO peripherals](en/08-mmio-peripherals.md) | Address map, LED matrix / switches / D-Pad / real-time clock / screen, using symbols |
| 09 | [SIMT GPU](en/09-gpu.md) | Register table, kernel ISA, `gpu_asm.py`, vecadd step by step, divergence / coalescing / bank-conflict experiments |
| 10 | [Tensor processor (NPU)](en/10-npu.md) | Descriptor ring, doorbell, polling vs interrupts, DMA, matmul step by step, CPU vs NPU cycle comparison |
| 11 | [Architecture explorer](en/11-arch-explorer.md) | Loading topologies, stepping/playing flows, zooming, block details, the built-in topologies |
| 12 | [Neo Geo tour](en/12-neogeo.md) | The Neo Geo architecture explained through its data flows + the `neogeo_mailbox.s` lab |
| 13 | [PS3 / Cell tour](en/13-ps3-cell.md) | The Cell BE explained through its data flows + the `spe_double_buffer.c` lab |
| 14 | [Authoring topologies](en/14-authoring-topologies.md) | Topology JSON schema, bilingual fields, validator, render tool |
| 15 | [Command-line mode](en/15-cli.md) | Every `--mode cli` option, telemetry reports, batch runs, limitations |
| 16 | [FAQ and troubleshooting](en/16-faq.md) | Build, run, assembler errors, peripherals, performance, where settings live, reporting bugs |
| 17 | [Glossary](en/17-glossary.md) | Chinese/English terms: SIMT, warp, coalescing, systolic, doorbell, EIB, MFC… |

### Reading paths

**Students (new to computer architecture)**

1. [01 Install](en/01-install.md) (skip it if your teacher already installed the program)
2. [02 Quick start](en/02-quickstart.md) → [03 GUI tour](en/03-gui-tour.md)
3. [04 RISC-V assembly](en/04-riscv-assembly.md) → [06 Processors and pipelines](en/06-processors-pipelines.md) → [07 Cache](en/07-cache.md)
4. [08 MMIO peripherals](en/08-mmio-peripherals.md) → [09 GPU](en/09-gpu.md) → [10 NPU](en/10-npu.md)
5. [11 Architecture explorer](en/11-arch-explorer.md) → [12 Neo Geo](en/12-neogeo.md) → [13 PS3/Cell](en/13-ps3-cell.md)
6. Whenever you get stuck, look at [16 FAQ](en/16-faq.md) and [17 Glossary](en/17-glossary.md)

**Teachers (preparing lectures and labs)**

1. [01 Install](en/01-install.md): for lab-wide deployment, focus on "prebuilt vs source build" and "headless runs"
2. [03 GUI tour](en/03-gui-tour.md): everything students will see, and how to switch the UI between English and Chinese
3. Pick lab chapters: pipelines ([06](en/06-processors-pipelines.md)), caches ([07](en/07-cache.md)), accelerators ([09](en/09-gpu.md), [10](en/10-npu.md)), system architecture ([11](en/11-arch-explorer.md)–[13](en/13-ps3-cell.md)); every chapter ends with exercises, 09/10 include answers
4. [15 Command-line mode](en/15-cli.md): batch-run student submissions and collect cycles, CPI, cache hit rates and more
5. [14 Authoring topologies](en/14-authoring-topologies.md): build your own architecture diagrams and data-flow animations

**Developers (changing or extending the project)**

1. [01 Install](en/01-install.md): source build, `build.ps1` options, running tests, common build errors
2. [15 Command-line mode](en/15-cli.md): the entry point for regression tests and automation scripts
3. [14 Authoring topologies](en/14-authoring-topologies.md): the architecture explorer's data format
4. Design documents: `docs/arch/PLAN.md`, `docs/arch/PLAN2.md`, `docs/arch/gpu.md`, `docs/arch/tensor_cpu.md`, `docs/arch/arch_explorer.md`
5. "How to report a bug" in [16 FAQ](en/16-faq.md)

### Switching the UI language

The UI is available in English and Simplified Chinese:

1. Open **Edit → Settings**.
2. Select the **Environment** page on the left.
3. In the **Language / 语言** drop-down choose **System**, **English** or **简体中文**.
4. **Restart the program** for the change to take effect.

With **System**, the UI is Chinese when the operating system locale is Chinese and English otherwise. The in-app help (**Help → User Guide / 用户手册**) opens the `zh/` or `en/` version of this guide according to the current UI language.

> This guide describes the target GUI that is currently being developed (see `docs/arch/PLAN2.md` §1.2). A few control names may differ slightly in the final version; such places are marked "(to be verified)" in the text.
