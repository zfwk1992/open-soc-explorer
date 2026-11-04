# 15 命令行模式

> **你将学到**
> - 用 `--mode cli` 在不打开窗口的情况下汇编、运行程序并输出统计（遥测，telemetry）
> - 全部命令行参数：源文件、类型、处理器、ISA 扩展、寄存器初始化、超时、报告格式、缓存模拟
> - 文本报告与 JSON 报告的格式，以及如何批量运行一批程序
> - 命令行模式的限制：**不能创建内存映射外设**，所以 GPU/NPU 示例必须在图形界面里运行
> - Windows 上运行图形子系统程序时如何正确拿到输出
>
> **前置条件**
> - 已经构建出 `build\Ripes.exe`（见[第 01 章](01-install.md)）
> - 会在 PowerShell 或 bash 中运行命令
> - 知道处理器模型和 CPI 的基本含义（见[第 02 章](02-quickstart.md)、[第 06 章](06-processors-pipelines.md)）

命令行模式（CLI mode）适合：老师批量评测学生程序；比较同一程序在不同处理器、不同缓存配置下的周期数；在自动化测试里检查程序输出。

---

## 1. 第一个命令

### 1.1 准备环境（Windows）

`Ripes.exe` 依赖 Qt 的 DLL。在 PowerShell 中，先把 Qt 的 `bin` 目录加到**本会话**的 PATH，并让 Qt 使用离屏平台：

```powershell
$env:PATH = "P:\workspace\soc\toolchain\Qt\6.8.3\mingw_64\bin;" + $env:PATH
$env:QT_QPA_PLATFORM = "offscreen"
```

Linux/macOS 上通常只需要（无显示器时）：

```bash
export QT_QPA_PLATFORM=offscreen
```

### 1.2 运行 factorial.s

**做一步**（Windows）：

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\factorial.s -t asm --proc RV32_5S --isaexts M --cycles --iret --cpi"
```

（在仓库根目录 `P:\workspace\soc\open-soc-explorer` 下执行。Linux/macOS 上把 `cmd /c "build\Ripes.exe ..."` 换成 `./build/Ripes ...`。）

**看到什么**：

```text
Factorial value of 7 is 5040
Program exited with code: 0
===== cycles
189
===== instructions retired
125
===== cycles per instruction (CPI)
1.512
```

**为什么**：

- 前两行是**程序自己的输出**：`ecall` 打印的内容直接写到标准输出（stdout），最后一行 `Program exited with code: 0` 来自退出系统调用。
- 之后每一段 `===== <名称>` 是一项**遥测报告**，只有你用参数要求的项才会出现。
- 这个结果和[第 02 章](02-quickstart.md)在图形界面里看到的一致：默认 5 级流水线上 189 个周期、125 条指令。

> **为什么要用 `cmd /c`？** 在 Windows 上 `Ripes.exe` 是"图形子系统"程序（CMake 中 `WIN32` 选项）。PowerShell 直接用 `& build\Ripes.exe ...` 启动这类程序时**不会等待它结束，也不会显示它的输出**，你会看到"什么都没打印"。`cmd /c "..."`（或重定向到文件）会等待程序结束并转发输出。见第 7 节。

---

## 2. 参数一览

命令行参数由 `main.cpp` 和 `src/cli/clioptions.cpp` 定义。

### 2.1 基本参数

| 参数 | 是否必需 | 说明 |
|---|---|---|
| `--mode <mode>` | 是（要进入 CLI 时） | `gui`（默认）或 `cli` |
| `--src <path>` | 是 | 源文件路径 |
| `-t <type>` | 否 | 源文件类型：`asm`（汇编，默认值）、`c`（C 源码）、`bin`（平坦二进制）、`elf`（ELF 可执行文件）。汇编文件可以省略它（已验证：`--src factorial.s` 不带 `-t` 也能汇编运行）；写成其他值会报 `Invalid source type (--t)` |
| `--proc <name>` | 是 | 处理器模型 ID，见 2.2 节 |
| `--isaexts <ext,...>` | 否 | 要启用的 ISA 扩展，逗号分隔，例如 `M` 或 `M,C`。**不给时只有基础指令集**（RV32I/RV64I），程序里的 `mul`、`div` 等会报 `Unknown opcode` |
| `--reginit <regfile>:<idx>=<v>,...` | 否 | 寄存器初始值，见 2.3 节。可重复使用 |
| `--timeout <ms>` | 否 | 超时毫秒数，默认 `0`（不限制）。超时后中止并报错 |
| `-v` | 否 | 详细输出：显示阶段标题，并每秒打印一次已用时间、周期数、退休指令数 |
| `--output <path>` | 否 | 把**报告**写入文件（程序本身的输出仍然打印到 stdout） |
| `--json` | 否 | 报告使用 JSON 格式 |
| `--all` | 否 | 打开所有报告项（缓存统计除外） |
| `-h`, `--help` | 否 | 把参数列表打印到 stdout，退出码 0（见第 7 节） |

### 2.2 处理器 ID

`--proc` 接受下列名字（大小写敏感）：

| ID | 处理器 |
|---|---|
| `RV32_SS` / `RV64_SS` | Single-cycle processor（单周期） |
| `RV32_5MC_2M` / `RV64_5MC` | Multi-cycle processor with separate memories |
| `RV32_5MC_1M` / `RV64_5MC_1M` | Multi-cycle processor with one memory |
| `RV32_5S_NO_FW_HZ` / `RV64_5S_NO_FW_HZ` | 5-stage, w/o forwarding or hazard detection |
| `RV32_5S_NO_HZ` / `RV64_5S_NO_HZ` | 5-stage, w/o hazard detection |
| `RV32_5S_NO_FW` / `RV64_5S_NO_FW` | 5-stage, w/o forwarding unit |
| `RV32_5S` / `RV64_5S` | 5-stage processor（完整 5 级流水线） |
| `RV32_6S_DUAL` / `RV64_6S_DUAL` | 6-stage dual-issue processor |
| `RV32_ISS` / `RV64_ISS` | ISA simulator（最快，适合长程序） |

`--isaexts` 中的扩展必须是该处理器支持的；写错时报错信息会列出支持的扩展，例如：

```text
Invalid ISA extension 'F' specified (--isaexts). Processor 'RV32_5S' supports extensions: M, C
```

（这就是实际输出：5 级流水线处理器恰好支持 `M` 和 `C`。）

### 2.3 寄存器初始化 `--reginit`

格式：`<寄存器文件>:<编号>=<值>,<编号>=<值>,...`。RISC-V 的通用寄存器文件名是 `gpr`（浮点寄存器文件是 `fpr`）。值可以写成有符号十进制、十六进制（`0x...`）或布尔形式。

```powershell
--reginit gpr:2=0x7ffffff0,3=0x10000000
```

> **说明**：命令行模式会应用与图形界面相同的默认寄存器初值（`sp = 0x7ffffff0`、`gp = 0x10000000`）——实测用 `--regs` 且不带 `--reginit` 时，`x2` 为 `0x7ffffff0`、`x3` 为 `0x10000000`。只有想覆盖默认值时才需要 `--reginit`；你指定的寄存器会替换它的默认值。

同一寄存器重复初始化会报 `Duplicate register initialization`；寄存器文件名写错会列出合法名字。

### 2.4 报告项（遥测）

每个报告项是一个开关参数，`--all` 打开全部（缓存统计除外）：

| 参数 | 文本报告标题 | JSON 键 | 内容 |
|---|---|---|---|
| `--cycles` | `cycles` | `cycles` | 总周期数 |
| `--iret` | `instructions retired` | `# instructions retired` | 退休指令数 |
| `--cpi` | `cycles per instruction (CPI)` | `CPI` | 周期数 / 指令数 |
| `--ipc` | `instructions per cycle (IPC)` | `IPC` | 指令数 / 周期数 |
| `--pipeline` | `pipeline state` | `pipeline` | 流水线图：每行一条指令、每列一个周期（制表符分隔），类似 GUI 的 **Show pipeline diagram** |
| `--regs` | `register values` | `registers` | 结束时所有寄存器的值（十进制和十六进制） |
| `--exectime` | `wall-clock model execution time (ms)` | `execution time (ms)` | 模拟本身花费的真实时间（毫秒） |
| `--runinfo` | `simulation information (processor configuration, input file, ...)` | `runinfo` | 处理器、ISA 扩展、源文件等运行信息 |
| （自动） | `L1 instruction/data cache statistics` | `cache statistics` | 只在配置了缓存时出现，见 2.5 节 |

### 2.5 缓存模拟

两种方式二选一（同时给出会报错 `mutually exclusive`）：

**按预设**：`--cache-preset "<预设名>"`，把同一个预设同时用于 L1 指令缓存和 L1 数据缓存。预设名与 GUI **Cache** 标签页 **Preset:** 下拉框中的完全一致，内置的有：

- `32-entry 4-word direct-mapped`
- `32-entry 4-word fully associative`
- `32-entry 4-word 2-way set associative`

（你在 GUI 中保存的自定义预设也可以用，因为两者共用同一份设置。）名字写错时，错误信息会列出所有可用预设。

**按 JSON 文件**：`--cache-config <path>`。文件里可以有 `"L1I"` 和/或 `"L1D"` 对象，只有出现的缓存才会被模拟：

```json
{
  "L1D": { "blocks": 2, "lines": 5, "ways": 1,
           "writePolicy": "writeback",
           "writeAllocatePolicy": "writeallocate",
           "replacementPolicy": "lru" },
  "L1I": { "blocks": 2, "lines": 4, "ways": 0 }
}
```

| 字段 | 必需 | 含义 |
|---|---|---|
| `blocks` | 是 | 每行字数的 2 的幂次（2 → 4 个字），对应 GUI 的 **2^N Words/Line** |
| `lines` | 是 | 组数的 2 的幂次（5 → 32 组），对应 **2^N Sets** |
| `ways` | 是 | 路数的 2 的幂次（0 → 直接映射，1 → 2 路），对应 **Ways** |
| `writePolicy` | 否 | `writeback`（默认）或 `writethrough` |
| `writeAllocatePolicy` | 否 | `writeallocate`（默认）或 `nowriteallocate` |
| `replacementPolicy` | 否 | `lru`（默认）或 `random` |

配置了缓存后，报告里会自动出现缓存统计，即使没有给 `--all`：

```text
===== L1 instruction/data cache statistics
L1d accesses: 32
L1d hit rate: 0.71875
L1d hits: 23
L1d misses: 9
L1d writebacks: 0
L1i accesses: 126
L1i hit rate: 0.9126984126984127
L1i hits: 115
L1i misses: 11
L1i writebacks: 0
```

> **已修复**：早期构建中只要配置了缓存，程序在报告输出完之后退出时会异常结束（Windows 退出码 `-1073740940`，即 `0xC0000374` 堆损坏）。当前构建用 `--cache-preset` 运行后退出码为 0（已验证），脚本可以重新依赖退出码。

---

## 3. 文本报告与 JSON 报告

### 3.1 文本报告

默认格式。每一项是：

```text
===== <描述>
<值>
```

适合人看。`--regs` 的输出形如：

```text
===== register values
x0:	0	(0x00000000)
x1:	28	(0x0000001c)
x2:	2147483632	(0x7ffffff0)
...
```

### 3.2 JSON 报告

**做一步**：

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\factorial.s -t asm --proc RV32_SS --isaexts M --json --cycles --iret --cpi --exectime --cache-preset ""32-entry 4-word direct-mapped"" > report.txt"
```

**看到什么**（`report.txt`）：

```text
Factorial value of 7 is 5040
Program exited with code: 0
{
    "# instructions retired": 125,
    "CPI": 1,
    "cache statistics": {
        "L1d accesses": 32,
        "L1d hit rate": 0.71875,
        "L1d hits": 23,
        "L1d misses": 9,
        "L1d writebacks": 0,
        "L1i accesses": 126,
        "L1i hit rate": 0.9126984126984127,
        "L1i hits": 115,
        "L1i misses": 11,
        "L1i writebacks": 0
    },
    "cycles": 125,
    "execution time (ms)": 2
}
```

**为什么**：单周期处理器每条指令正好一个周期，所以 CPI = 1。注意程序自己的输出（前两行）和 JSON 混在同一个 stdout 里；要得到纯 JSON，请用 `--output` 把报告单独写到文件：

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\factorial.s -t asm --proc RV32_SS --isaexts M --json --all --output report.json"
```

此时 `report.json` 只包含 JSON，程序输出仍然在屏幕上。

---

## 4. 更多例子

### 4.1 详细模式 + 双发射处理器 + 寄存器初值

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\consolePrinting.s -t asm --proc RV32_6S_DUAL --reginit gpr:2=0x7ffffff0,3=0x10000000 --cycles --ipc --regs -v"
```

`-v` 会打印阶段标题：

```text
=============================== Ripes CLI mode ===============================
=========================== Processing input file ===========================
INFO: Assembling input file 'examples\assembly\consolePrinting.s'
=============================== Running model ===============================
A string
 -10,  -9, ...
Program exited with code: 0
================================== Post-run ==================================
===== cycles
711
===== instructions per cycle (IPC)
0.5921237693389592
...
```

### 4.2 运行预编译 ELF

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\ELF\RanPi-RV32 -t elf --proc RV32_ISS --isaexts M --timeout 20000 --cycles --exectime"
```

`RanPi` 用蒙特卡洛方法估算 π，最后打印 `Result:  3.04`。在 ISA 模拟器上约 12 万个周期、十几毫秒。ELF 文件会先做与 GUI **Load Program** 相同的兼容性检查（机器类型、32/64 位、必须是可执行文件）。

### 4.3 编译并运行 C 程序

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\C\matrixmul.c -t c --proc RV32_5S --isaexts M --cycles"
```

C 模式使用**图形界面中配置的编译器**（**Edit → Settings → Compiler → Compiler path**，设置是共用的）。没有配置时报 `No C compiler set.`；编译失败时打印 `Compilation failed. Error output was: ...`。见[第 05 章](05-c-programs.md)。

### 4.4 平坦二进制

```powershell
cmd /c "build\Ripes.exe --mode cli --src prog.bin -t bin --proc RV32_SS --cycles"
```

命令行模式下平坦二进制固定装载到地址 0、入口地址 0（GUI 的 Load Program 对话框可以修改这两个值）。

### 4.5 用超时防止死循环

```powershell
cmd /c "build\Ripes.exe --mode cli --src student.s -t asm --proc RV32_ISS --isaexts M --timeout 5000 --cycles"
```

5 秒内没结束则打印 `ERROR: Simulation did not finish within the specified timeout (5000 ms)`，退出码为 1。

---

## 5. 批量运行

### 5.1 PowerShell：对一个目录里的所有程序比较三种处理器

```powershell
$env:PATH = "P:\workspace\soc\toolchain\Qt\6.8.3\mingw_64\bin;" + $env:PATH
$env:QT_QPA_PLATFORM = "offscreen"
$ripes = "P:\workspace\soc\open-soc-explorer\build\Ripes.exe"
$procs = @("RV32_SS", "RV32_5S", "RV32_6S_DUAL")
$rows = @()
foreach ($f in Get-ChildItem submissions\*.s) {
    foreach ($p in $procs) {
        $out = "$env:TEMP\ripes_$($f.BaseName)_$p.json"
        cmd /c "`"$ripes`" --mode cli --src `"$($f.FullName)`" -t asm --proc $p --isaexts M --reginit gpr:2=0x7ffffff0,3=0x10000000 --timeout 10000 --json --cycles --cpi --output `"$out`" > nul"
        $ok = ($LASTEXITCODE -eq 0) -and (Test-Path $out)
        $r = if ($ok) { Get-Content $out -Raw | ConvertFrom-Json } else { $null }
        $rows += [pscustomobject]@{
            File = $f.Name; Proc = $p; OK = $ok
            Cycles = if ($r) { $r.cycles } else { "" }
            CPI = if ($r) { $r.CPI } else { "" }
        }
    }
}
$rows | Format-Table -AutoSize
$rows | Export-Csv results.csv -NoTypeInformation -Encoding UTF8
```

### 5.2 bash

```bash
export QT_QPA_PLATFORM=offscreen
for f in submissions/*.s; do
  for p in RV32_SS RV32_5S RV32_6S_DUAL; do
    if ./build/Ripes --mode cli --src "$f" -t asm --proc "$p" --isaexts M \
         --reginit gpr:2=0x7ffffff0,3=0x10000000 --timeout 10000 \
         --json --cycles --cpi --output /tmp/r.json > /dev/null; then
      echo "$f,$p,$(python3 -c 'import json;d=json.load(open("/tmp/r.json"));print(d["cycles"],d["CPI"],sep=",")')"
    else
      echo "$f,$p,FAILED"
    fi
  done
done
```

### 5.3 检查程序输出

程序输出在 stdout 的前面部分，以 `Program exited with code: N` 结尾。评测时可以把 stdout 重定向到文件，再与期望输出比较（注意 `ecall` 打印整数时数字前后可能带空格，例如 `Factorial value of  7 is  5040`，比较前最好规范化空白）。

---

## 6. 限制：命令行模式不能使用外设

**做一步**：

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\gpu_vecadd.s -t asm --proc RV32_SS --cycles"
```

**看到什么**：

```text
ERROR: Error during assembly:
INFO: Unknown opcode 'li'
INFO: Unknown opcode 'divu'
INFO: Unknown opcode 'li'
INFO: Unknown opcode 'li'
```

**为什么**：

- 在 GUI 中，`GPU_0_BASE`、`GPU_0_CTRL` 等符号是 **I/O** 标签页里**已经创建的外设**导出的。命令行模式**不会创建任何内存映射外设**（LED Matrix、Switches、D-Pad、Real-time Clock、Screen、SIMT GPU、Tensor Unit (NPU) 都不会存在），所以这些符号未定义。
- 汇编器在 `li a0, GPU_0_BASE` 这样的行里找不到符号时，无法把它匹配到任何合法的 `li` 形式，于是报告的是 **`Unknown opcode 'li'`**，而不是"未知符号"——这个信息容易误导，记住：**在 CLI 中看到 `li`/`la` 报 Unknown opcode，先检查是不是用了外设符号**。
- `Unknown opcode 'divu'` 则是另一个原因：没有给 `--isaexts M`，乘除法指令不可用。

因此：

- `gpu_vecadd.s`、`npu_matmul.s`、`npu_matmul.c`、`leds.s`、`leds.c`、`switchesAndLeds.c`、`neogeo_mailbox.s`（使用 LED 矩阵）等依赖外设的示例**必须在图形界面中运行**：先在 **I/O** 标签页创建对应外设，再加载程序。
- 即使你手工把符号换成数值地址，CLI 下该地址也没有设备响应：它表现得像普通内存（已验证：向 `0xf0000000` 做 `sw` 后再 `lw` 会读回写入的值），程序能跑，但什么都不会显示，状态寄存器也不会变化。

---

## 7. Windows 上的注意事项

1. **PowerShell 不等待、不显示输出**：用 `cmd /c "..."`，或 `Start-Process -Wait -NoNewWindow -RedirectStandardOutput out.txt`，或重定向到文件。直接 `& build\Ripes.exe ...` 时 `$LASTEXITCODE` 也拿不到。
2. **`--help` 打印到控制台**：`cmd /c "build\Ripes.exe --help"` 把参数列表写到 stdout 并以退出码 0 结束，在 `QT_QPA_PLATFORM=offscreen` 下也一样（已验证）。早期构建沿用 Qt 对图形子系统程序的默认做法——弹出消息框，在 offscreen 下看不见、程序会挂起——所以如果旧副本在 `--help` 时卡住，原因就在这里。所以：
   - 想看参数列表时，像上面那样通过 `cmd /c` 运行 `--help`，或直接查看本章第 2 节；
   - **参数写错时会在 stderr 打印 `ERROR: Unknown option '...'` 和帮助**，退出码为 1（已验证）；stdout 上什么都没有。
3. **参数错误的退出码是 1**：`main.cpp` 在解析失败后把错误和帮助打印到 stderr 并返回 1；`parseCLIOptions` 的失败（处理器名写错、预设名写错等）也一样。脚本可以依赖退出码。
4. 程序运行、汇编或超时失败时退出码为 1，并打印以 `ERROR:` 开头的信息。

---

## 练习

1. 用 `--cycles --cpi` 在 `RV32_SS`、`RV32_5S_NO_FW_HZ`、`RV32_5S`、`RV32_6S_DUAL` 上运行 `factorial.s`（记得 `--isaexts M`）。哪个周期最少？没有转发和冒险检测的处理器结果正确吗？为什么？（提示：看控制台输出。）
2. 写一个 `--cache-config` 文件，只模拟 L1D，把 `lines` 从 0 改到 6，记录命中率，画出"组数—命中率"曲线。
3. 用 `--pipeline` 输出 `factorial.s` 在 `RV32_5S` 上的流水线图，粘贴进电子表格，找出第一个冲刷（flush）。
4. 用 `--regs` 运行 `factorial.s`，看 `x2`：不带 `--reginit` 它也是 `0x7ffffff0`。再加上 `--reginit gpr:2=0x1000` 运行一次，程序结果变了吗？为什么这次没有？
5. 用第 5.1 节的脚本批量运行 `examples/assembly` 下所有示例，哪些失败了？失败原因分别是什么？

## 小结

- `--mode cli --src <文件> -t <asm|c|bin|elf> --proc <ID>` 是最小命令；需要乘除法时加 `--isaexts M`。
- 报告项：`--cycles`、`--iret`、`--cpi`、`--ipc`、`--pipeline`、`--regs`、`--exectime`、`--runinfo`，或 `--all`；`--json` 改为 JSON，`--output` 写文件。
- 缓存：`--cache-preset "<名字>"` 或 `--cache-config <json>`，配置后自动输出缓存统计。
- CLI 应用与 GUI 相同的默认寄存器初值（`sp`、`gp`）；只有要覆盖时才用 `--reginit gpr:<编号>=<值>`。
- **CLI 不能创建外设**：用了外设符号的程序会报 `Unknown opcode 'li'`，GPU/NPU/LED 等示例必须在图形界面运行。
- Windows 上用 `cmd /c` 获取输出；`--help` 打印到 stdout（退出码 0），参数错误打印到 stderr（退出码 1）。

下一章：[16 FAQ 与故障排除](16-faq.md)。
