# 15 Command-Line Mode

> **What you will learn**
> - How to assemble and run programs and print statistics (telemetry) without opening a window, using `--mode cli`
> - Every command-line option: source file, type, processor, ISA extensions, register initialization, timeout, report format, cache simulation
> - The text and JSON report formats, and how to run a batch of programs
> - The limitation of command-line mode: **it cannot create memory-mapped peripherals**, so the GPU/NPU examples must run in the GUI
> - How to get the output correctly when running a GUI-subsystem program on Windows
>
> **Prerequisites**
> - `build\Ripes.exe` has been built (see [Chapter 01](01-install.md))
> - You can run commands in PowerShell or bash
> - You know the basic meaning of processor models and CPI (see [Chapter 02](02-quickstart.md), [Chapter 06](06-processors-pipelines.md))

Command-line mode (CLI mode) is useful when a teacher grades student programs in bulk, when you compare the cycle count of one program across processors or cache configurations, and when automated tests check a program's output.

---

## 1. The first command

### 1.1 Prepare the environment (Windows)

`Ripes.exe` depends on Qt's DLLs. In PowerShell, first add Qt's `bin` directory to PATH **for this session** and tell Qt to use the offscreen platform:

```powershell
$env:PATH = "P:\workspace\soc\toolchain\Qt\6.8.3\mingw_64\bin;" + $env:PATH
$env:QT_QPA_PLATFORM = "offscreen"
```

On Linux/macOS you usually only need (when there is no display):

```bash
export QT_QPA_PLATFORM=offscreen
```

### 1.2 Run factorial.s

**Do** (Windows):

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\factorial.s -t asm --proc RV32_5S --isaexts M --cycles --iret --cpi"
```

(Run it from the repository root `P:\workspace\soc\open-soc-explorer`. On Linux/macOS replace `cmd /c "build\Ripes.exe ..."` with `./build/Ripes ...`.)

**What you see**:

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

**Why**:

- The first two lines are **the program's own output**: whatever `ecall` prints goes straight to standard output (stdout), and the final `Program exited with code: 0` comes from the exit system call.
- Each following `===== <name>` section is one **telemetry report**; only the items you asked for with options appear.
- The result matches what you saw in the GUI in [Chapter 02](02-quickstart.md): 189 cycles and 125 instructions on the default 5-stage pipeline.

> **Why `cmd /c`?** On Windows `Ripes.exe` is a "GUI subsystem" program (the `WIN32` option in CMake). When PowerShell starts such a program directly with `& build\Ripes.exe ...` it **does not wait for it to finish and does not show its output**, so you see "nothing printed". `cmd /c "..."` (or redirecting to a file) waits for the program and forwards its output. See section 7.

---

## 2. Options

The command-line options are defined in `main.cpp` and `src/cli/clioptions.cpp`.

### 2.1 Basic options

| Option | Required | Description |
|---|---|---|
| `--mode <mode>` | Yes (to enter CLI) | `gui` (default) or `cli` |
| `--src <path>` | Yes | Path to the source file |
| `-t <type>` | No | Source type: `asm` (assembly, the default), `c` (C source), `bin` (flat binary), `elf` (ELF executable). Assembly files can omit it (verified: `--src factorial.s` without `-t` assembles and runs); any other value reports `Invalid source type (--t)` |
| `--proc <name>` | Yes | Processor model ID, see section 2.2 |
| `--isaexts <ext,...>` | No | ISA extensions to enable, comma separated, e.g. `M` or `M,C`. **Without it only the base instruction set is available** (RV32I/RV64I) and `mul`, `div` etc. in the program report `Unknown opcode` |
| `--reginit <regfile>:<idx>=<v>,...` | No | Register initial values, see section 2.3. May be repeated |
| `--timeout <ms>` | No | Timeout in milliseconds, default `0` (no limit). On timeout the run is aborted with an error |
| `-v` | No | Verbose output: prints phase headers and, every second, the elapsed time, cycle count and retired instructions |
| `--output <path>` | No | Write the **report** to a file (the program's own output still goes to stdout) |
| `--json` | No | Use JSON for the report |
| `--all` | No | Enable all report items (except cache statistics) |
| `-h`, `--help` | No | Print the option list to stdout and exit with code 0 (see section 7) |

### 2.2 Processor IDs

`--proc` accepts the following names (case-sensitive):

| ID | Processor |
|---|---|
| `RV32_SS` / `RV64_SS` | Single-cycle processor |
| `RV32_5MC_2M` / `RV64_5MC` | Multi-cycle processor with separate memories |
| `RV32_5MC_1M` / `RV64_5MC_1M` | Multi-cycle processor with one memory |
| `RV32_5S_NO_FW_HZ` / `RV64_5S_NO_FW_HZ` | 5-stage, w/o forwarding or hazard detection |
| `RV32_5S_NO_HZ` / `RV64_5S_NO_HZ` | 5-stage, w/o hazard detection |
| `RV32_5S_NO_FW` / `RV64_5S_NO_FW` | 5-stage, w/o forwarding unit |
| `RV32_5S` / `RV64_5S` | 5-stage processor (full 5-stage pipeline) |
| `RV32_6S_DUAL` / `RV64_6S_DUAL` | 6-stage dual-issue processor |
| `RV32_ISS` / `RV64_ISS` | ISA simulator (fastest, good for long programs) |

The extensions in `--isaexts` must be supported by that processor; if not, the error message lists the supported ones, for example:

```text
Invalid ISA extension 'F' specified (--isaexts). Processor 'RV32_5S' supports extensions: M, C
```

(This is the real output: the 5-stage processor supports exactly `M` and `C`.)

### 2.3 Register initialization `--reginit`

Format: `<register file>:<index>=<value>,<index>=<value>,...`. The RISC-V general-purpose register file is called `gpr` (the floating-point register file is `fpr`). Values may be written in signed decimal, hexadecimal (`0x...`) or boolean notation.

```powershell
--reginit gpr:2=0x7ffffff0,3=0x10000000
```

> **Note**: command-line mode applies the same default register initial values as the GUI (`sp = 0x7ffffff0`, `gp = 0x10000000`) — measured with `--regs` and no `--reginit`, `x2` is `0x7ffffff0` and `x3` is `0x10000000`. Use `--reginit` only when you want to override them; a register you name replaces its default.

Initializing the same register twice reports `Duplicate register initialization`; a wrong register file name lists the valid names.

### 2.4 Report items (telemetry)

Each report item is a switch; `--all` enables all of them (except cache statistics):

| Option | Text report header | JSON key | Contents |
|---|---|---|---|
| `--cycles` | `cycles` | `cycles` | Total cycles |
| `--iret` | `instructions retired` | `# instructions retired` | Retired instructions |
| `--cpi` | `cycles per instruction (CPI)` | `CPI` | Cycles / instructions |
| `--ipc` | `instructions per cycle (IPC)` | `IPC` | Instructions / cycles |
| `--pipeline` | `pipeline state` | `pipeline` | Pipeline diagram: one row per instruction, one column per cycle (tab separated), like the GUI's **Show pipeline diagram** |
| `--regs` | `register values` | `registers` | All register values at the end (decimal and hexadecimal) |
| `--exectime` | `wall-clock model execution time (ms)` | `execution time (ms)` | Real time spent simulating, in milliseconds |
| `--runinfo` | `simulation information (processor configuration, input file, ...)` | `runinfo` | Processor, ISA extensions, source file and other run information |
| (automatic) | `L1 instruction/data cache statistics` | `cache statistics` | Only when a cache is configured, see section 2.5 |

### 2.5 Cache simulation

Choose one of two ways (giving both reports an error, `mutually exclusive`):

**By preset**: `--cache-preset "<preset name>"` applies the same preset to both the L1 instruction cache and the L1 data cache. Preset names are exactly those in the **Preset:** drop-down of the GUI **Cache** tab; the built-in ones are:

- `32-entry 4-word direct-mapped`
- `32-entry 4-word fully associative`
- `32-entry 4-word 2-way set associative`

(Custom presets you saved in the GUI work too, since both share the same settings.) A wrong name lists all available presets in the error message.

**By JSON file**: `--cache-config <path>`. The file may contain `"L1I"` and/or `"L1D"` objects; only the caches that appear are simulated:

```json
{
  "L1D": { "blocks": 2, "lines": 5, "ways": 1,
           "writePolicy": "writeback",
           "writeAllocatePolicy": "writeallocate",
           "replacementPolicy": "lru" },
  "L1I": { "blocks": 2, "lines": 4, "ways": 0 }
}
```

| Field | Required | Meaning |
|---|---|---|
| `blocks` | Yes | Power of two of the words per line (2 → 4 words), the GUI's **2^N Words/Line** |
| `lines` | Yes | Power of two of the number of sets (5 → 32 sets), **2^N Sets** |
| `ways` | Yes | Power of two of the number of ways (0 → direct-mapped, 1 → 2-way), **Ways** |
| `writePolicy` | No | `writeback` (default) or `writethrough` |
| `writeAllocatePolicy` | No | `writeallocate` (default) or `nowriteallocate` |
| `replacementPolicy` | No | `lru` (default) or `random` |

With a cache configured, cache statistics appear in the report automatically, even without `--all`:

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

> **Fixed**: earlier builds terminated abnormally on exit whenever a cache was configured (Windows exit code `-1073740940`, i.e. `0xC0000374` heap corruption, after the report had been printed). The current build exits with code 0 after a `--cache-preset` run (verified), so scripts can rely on the exit code again.

---

## 3. Text and JSON reports

### 3.1 Text report

The default format. Each item is:

```text
===== <description>
<value>
```

Meant for humans. The output of `--regs` looks like:

```text
===== register values
x0:	0	(0x00000000)
x1:	28	(0x0000001c)
x2:	2147483632	(0x7ffffff0)
...
```

### 3.2 JSON report

**Do**:

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\factorial.s -t asm --proc RV32_SS --isaexts M --json --cycles --iret --cpi --exectime --cache-preset ""32-entry 4-word direct-mapped"" > report.txt"
```

**What you see** (`report.txt`):

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

**Why**: on the single-cycle processor every instruction takes exactly one cycle, so CPI = 1. Note that the program's own output (the first two lines) and the JSON share the same stdout; to get pure JSON, write the report to its own file with `--output`:

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\factorial.s -t asm --proc RV32_SS --isaexts M --json --all --output report.json"
```

`report.json` then contains only JSON, while the program output still appears on screen.

---

## 4. More examples

### 4.1 Verbose mode + dual-issue processor + register initial values

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\consolePrinting.s -t asm --proc RV32_6S_DUAL --reginit gpr:2=0x7ffffff0,3=0x10000000 --cycles --ipc --regs -v"
```

`-v` prints phase headers:

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

### 4.2 Running a precompiled ELF

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\ELF\RanPi-RV32 -t elf --proc RV32_ISS --isaexts M --timeout 20000 --cycles --exectime"
```

`RanPi` estimates π with a Monte Carlo method and finally prints `Result:  3.04`. On the ISA simulator it takes about 120 thousand cycles and a dozen milliseconds. The ELF file first goes through the same compatibility checks as in the GUI's **Load Program** (machine type, 32/64 bit, must be an executable).

### 4.3 Compiling and running a C program

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\C\matrixmul.c -t c --proc RV32_5S --isaexts M --cycles"
```

C mode uses **the compiler configured in the GUI** (**Edit → Settings → Compiler → Compiler path**; the settings are shared). Without one it reports `No C compiler set.`; when compilation fails it prints `Compilation failed. Error output was: ...`. See [Chapter 05](05-c-programs.md).

### 4.4 Flat binary

```powershell
cmd /c "build\Ripes.exe --mode cli --src prog.bin -t bin --proc RV32_SS --cycles"
```

In command-line mode a flat binary is always loaded at address 0 with entry point 0 (the GUI's Load Program dialog lets you change both).

### 4.5 Guarding against infinite loops with a timeout

```powershell
cmd /c "build\Ripes.exe --mode cli --src student.s -t asm --proc RV32_ISS --isaexts M --timeout 5000 --cycles"
```

If the program has not finished within 5 seconds it prints `ERROR: Simulation did not finish within the specified timeout (5000 ms)` and the exit code is 1.

---

## 5. Batch runs

### 5.1 PowerShell: compare three processors on every program in a directory

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

### 5.3 Checking program output

The program output is at the beginning of stdout and ends with `Program exited with code: N`. For grading, redirect stdout to a file and compare it with the expected output (note that integers printed by `ecall` may be surrounded by spaces, e.g. `Factorial value of  7 is  5040`, so normalise whitespace before comparing).

---

## 6. Limitation: no peripherals in command-line mode

**Do**:

```powershell
cmd /c "build\Ripes.exe --mode cli --src examples\assembly\gpu_vecadd.s -t asm --proc RV32_SS --cycles"
```

**What you see**:

```text
ERROR: Error during assembly:
INFO: Unknown opcode 'li'
INFO: Unknown opcode 'divu'
INFO: Unknown opcode 'li'
INFO: Unknown opcode 'li'
```

**Why**:

- In the GUI, symbols such as `GPU_0_BASE` and `GPU_0_CTRL` are exported by peripherals **already created** in the **I/O** tab. Command-line mode **creates no memory-mapped peripherals at all** (LED Matrix, Switches, D-Pad, Real-time Clock, Screen, SIMT GPU, Tensor Unit (NPU) do not exist), so these symbols are undefined.
- When the assembler cannot resolve the symbol in a line such as `li a0, GPU_0_BASE`, it cannot match the line to any valid form of `li`, so it reports **`Unknown opcode 'li'`** rather than "unknown symbol". This message is misleading; remember: **in the CLI, when `li`/`la` reports Unknown opcode, first check whether a peripheral symbol is used**.
- `Unknown opcode 'divu'` has a different cause: `--isaexts M` was not given, so multiply/divide instructions are unavailable.

Therefore:

- Examples that rely on peripherals — `gpu_vecadd.s`, `npu_matmul.s`, `npu_matmul.c`, `leds.s`, `leds.c`, `switchesAndLeds.c`, `neogeo_mailbox.s` (uses the LED matrix) and others — **must be run in the GUI**: first create the peripheral in the **I/O** tab, then load the program.
- Even if you replace the symbols by numeric addresses by hand, no device answers at those addresses in the CLI: the address behaves like ordinary RAM (verified: a `sw` to `0xf0000000` followed by a `lw` returns the stored value), so the program runs, but nothing is displayed and status registers never change.

---

## 7. Notes for Windows

1. **PowerShell neither waits nor shows output**: use `cmd /c "..."`, or `Start-Process -Wait -NoNewWindow -RedirectStandardOutput out.txt`, or redirect to a file. With a plain `& build\Ripes.exe ...` you also cannot get `$LASTEXITCODE`.
2. **`--help` prints to the console**: `cmd /c "build\Ripes.exe --help"` writes the option list to stdout and exits with code 0, also under `QT_QPA_PLATFORM=offscreen` (verified). Earlier builds used Qt's default behaviour for GUI-subsystem programs — a message box, which hangs invisibly under offscreen — so if an old copy hangs on `--help`, that is why. Therefore:
   - To see the option list, run `--help` through `cmd /c` as above, or just read section 2 of this chapter;
   - **A mistyped option prints `ERROR: Unknown option '...'` followed by the help on stderr** and exits with code 1 (verified); nothing is printed to stdout.
3. **The exit code for option errors is 1**: `main.cpp` prints the error and the help to stderr and returns 1 after a parse failure, and `parseCLIOptions` failures (bad processor name, bad preset, ...) do the same. Scripts can rely on the exit code.
4. When running, assembling or a timeout fails, the exit code is 1 and a message starting with `ERROR:` is printed.

---

## Exercises

1. Run `factorial.s` with `--cycles --cpi` on `RV32_SS`, `RV32_5S_NO_FW_HZ`, `RV32_5S` and `RV32_6S_DUAL` (remember `--isaexts M`). Which takes the fewest cycles? Is the result correct on the processor without forwarding and hazard detection? Why? (Hint: look at the console output.)
2. Write a `--cache-config` file that simulates only L1D, vary `lines` from 0 to 6, note the hit rate and plot "number of sets vs hit rate".
3. Use `--pipeline` to print the pipeline diagram of `factorial.s` on `RV32_5S`, paste it into a spreadsheet and find the first flush.
4. Run `factorial.s` with `--regs` and look at `x2`: it is `0x7ffffff0` even without `--reginit`. Now add `--reginit gpr:2=0x1000` and run again. Does the program result change? Why not this time?
5. Use the script from section 5.1 to run every example in `examples/assembly`. Which ones fail, and why?

## Summary

- `--mode cli --src <file> -t <asm|c|bin|elf> --proc <ID>` is the minimal command; add `--isaexts M` when multiply/divide is needed.
- Report items: `--cycles`, `--iret`, `--cpi`, `--ipc`, `--pipeline`, `--regs`, `--exectime`, `--runinfo`, or `--all`; `--json` switches to JSON, `--output` writes to a file.
- Caches: `--cache-preset "<name>"` or `--cache-config <json>`; when configured, cache statistics are reported automatically.
- The CLI applies the same default register values as the GUI (`sp`, `gp`); use `--reginit gpr:<idx>=<value>` only to override them.
- **The CLI cannot create peripherals**: programs using peripheral symbols report `Unknown opcode 'li'`; the GPU/NPU/LED examples must run in the GUI.
- On Windows use `cmd /c` to get output; `--help` prints to stdout (exit code 0), option errors print to stderr (exit code 1).

Next chapter: [16 FAQ and troubleshooting](16-faq.md).
