# open-soc-explorer

open-soc-explorer 是基于 [Ripes](https://github.com/mortbopet/Ripes) 的教学用 SoC 探索器。在 Ripes 的 RISC-V 可视化处理器、缓存与 MMIO 模拟之上，增加了异构 SoC 的教学模块：

| 模块 | 入口 | 说明 |
|---|---|---|
| GPU 运行原理 | I/O tab → "SIMT GPU" 外设；[docs/arch/gpu.md](docs/arch/gpu.md) | 可配置 SM/warp/lane 的 SIMT 模型，RISC-V 主机程序通过 MMIO 启动 kernel，观察 warp 调度、分支发散、合并访存 |
| 张量处理器与 CPU 交互 | I/O tab → "Tensor Unit (NPU)" 外设；[docs/arch/tensor_cpu.md](docs/arch/tensor_cpu.md) | 脉动阵列 NPU，描述符环 + 门铃 + DMA + 中断的主从交互模型 |
| Neo Geo 架构 | Architecture tab；[docs/arch/neogeo.md](docs/arch/neogeo.md) | 68000 + Z80 + LSPC + YM2610 双 CPU 街机架构与数据流演示 |
| PS3 / Cell 架构 | Architecture tab；[docs/arch/ps3_cell.md](docs/arch/ps3_cell.md) | PPE + SPE + EIB + MFC + RSX 架构与数据流演示 |
| 架构浏览器 | Architecture tab；[docs/arch/arch_explorer.md](docs/arch/arch_explorer.md) | JSON 驱动的框图/数据流演示器，可添加新架构 |

## 用户手册 / User Guide

**完整的中英双语用户手册：[docs/guide/README.md](docs/guide/README.md)**（17 章：安装、10 分钟上手、界面总览、RISC-V 汇编、C 编程、流水线、缓存、MMIO、GPU、NPU、架构浏览器、NeoGeo、PS3、编写拓扑、命令行、FAQ、术语表）。程序内按 **F1** 或 **Help → User Guide / 帮助 → 用户手册** 打开同一份手册。

The complete bilingual (Chinese/English) user guide is at [docs/guide/README.md](docs/guide/README.md); inside the app press **F1** or use **Help → User Guide**.

界面语言 / UI language：**Edit → Settings → Environment → Language / 语言**（System / English / 简体中文，重启生效 / applies after restart）。

示例程序见 `examples/`（`gpu_vecadd.s`、`npu_matmul.s/.c`、`neogeo_mailbox.s`、`spe_double_buffer.c`，以及练习 `exercise_gpu_saxpy.s`、`exercise_gpu_divergence.s`、`exercise_npu_batch.s`）。扩展计划与约定见 [docs/arch/PLAN.md](docs/arch/PLAN.md) 与 [docs/arch/PLAN2.md](docs/arch/PLAN2.md)。

Windows 下的用户态构建脚本：`P:\workspace\soc\build.ps1`（见 PLAN.md 模块 F）。

---

# Ripes
[![Windows / Qt 6.5.0](https://github.com/mortbopet/Ripes/actions/workflows/windows-release.yml/badge.svg)](https://github.com/mortbopet/Ripes/actions/workflows/windows-release.yml)
[![Mac release / Qt 6.5.0](https://github.com/mortbopet/Ripes/actions/workflows/mac-release.yml/badge.svg)](https://github.com/mortbopet/Ripes/actions/workflows/mac-release.yml)
[![Ubuntu release 16.04 / Qt 6.5.0](https://github.com/mortbopet/Ripes/actions/workflows/linux-release.yml/badge.svg)](https://github.com/mortbopet/Ripes/actions/workflows/linux-release.yml)
[![WASM / Qt 6.6.0](https://github.com/mortbopet/Ripes/actions/workflows/wasm-release.yml/badge.svg?branch=master)](https://github.com/mortbopet/Ripes/actions/workflows/wasm-release.yml)
[![Gitter](https://badges.gitter.im/Ripes-VSRTL/Ripes.svg)](https://gitter.im/Ripes-VSRTL/)

Ripes is a visual computer architecture simulator and assembly code editor built for the [RISC-V instruction set architecture](https://content.riscv.org/wp-content/uploads/2017/05/riscv-spec-v2.2.pdf).

**Experimental**: Try Ripes directly in your browser: [https://ripes.dk/](https://ripes.dk/)

For questions, comments, feature requests, or new ideas, don't hesitate to share these at the [discussions page](https://github.com/mortbopet/Ripes/discussions).  
For bugs or issues, please report these at the [issues page](https://github.com/mortbopet/Ripes/issues).

<p align="center">
    <img src="https://github.com/mortbopet/Ripes/blob/master/resources/images/animation.gif?raw=true" />
</p>

## Usage
Ripes may be used to explore concepts such as:
- How machine code is executed on a variety of microarchitectures (RV32IMC/RV64IMC based)
- How different cache designs influence performance
- How C and assembly code is compiled and assembled to executable machine code
- How a processor interacts with memory-mapped I/O

If this is your first time using Ripes, please refer to the [introduction/tutorial](docs/introduction.md).  
For further information, please refer to the [Ripes documentation](docs/README.md).

## Downloading & Installation
Prebuilt binaries are available for Linux, Windows & Mac through the [Releases page](https://github.com/mortbopet/Ripes/releases).  
Ripes is also available in your browser at [https://ripes.dk/](https://ripes.dk/) through a Qt for WebAssembly deployment.

### Linux
Releases for Linux are distributed in the AppImage format. To run an AppImage:
* Run `chmod a+x` on the AppImage file
* Run the file!
The AppImage for Linux should be compatible with most Linux distributions.

### Windows
For Windows, the C++ runtime library must be available (if not, a msvcp140.dll error will be produced). You most likely already have this installed, but if this is not the case, you download it [here](https://www.microsoft.com/en-us/download/details.aspx?id=48145).

## Building
Initially, the following dependencies must be made available:
- A recent (>=6.5.0) version of [Qt](https://www.qt.io/download) + Qt Charts (**not** bundled with Qt by default, but can be selected during Qt installation)
- [CMake](https://cmake.org/)
- `sudo apt-get install libegl1-mesa-dev`

Then, Ripes can be checked out and built as a standard CMake project:
```
git clone --recursive https://github.com/mortbopet/Ripes.git
cd Ripes/
cmake .
Unix:               Windows:
make                jom.exe / nmake.exe / ...
```
Note, that you must have Qt available in your `CMAKE_PREFIX_PATH`. For further information on building Qt projects with CMake, refer to [Qt: Build with CMake](https://doc.qt.io/qt-5/cmake-manual.html).

---
In papers and reports, please refer to Ripes as follows: 'Morten Borup Petersen. Ripes. https://github.com/mortbopet/Ripes' or by referring to the [WCAE'21 paper on the project](https://ieeexplore.ieee.org/document/9707149), e.g. using the following BibTeX code:
```
@MISC{Ripes,
	author = {Morten Borup Petersen},
	title = {Ripes},
	howpublished = "\url{https://github.com/mortbopet/Ripes}"
}

@inproceedings{petersen2021ripes,
  title={Ripes: A Visual Computer Architecture Simulator},
  author={Petersen, Morten B},
  booktitle={2021 ACM/IEEE Workshop on Computer Architecture Education (WCAE)},
  pages={1--8},
  year={2021},
  organization={IEEE}
}
```
