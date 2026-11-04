# 16 FAQ and Troubleshooting

> **What you will learn**
> - Causes and fixes for the most common problems when building, starting, assembling and running
> - Why using peripheral symbols in command-line mode reports `Unknown opcode`
> - What to do when a C example says "no compiler"
> - Dark/light theme, UI language, and where settings are stored
> - The limits of reverse execution with accelerators, and tips for faster simulation
> - How to report a bug
>
> **Prerequisites**
> - You have read [Chapter 02](02-quickstart.md) and [Chapter 03](03-gui-tour.md) and know the names of the tabs and toolbar buttons

This chapter is organised as "problem → cause → fix". Find the closest question in the sections below first.

---

## 1. Building and starting

### Q1.1 `build.ps1` reports `Toolchain component missing`

**Cause**: one of `cmake.exe`, `ninja.exe`, MinGW's `gcc.exe`/`g++.exe`, `cmake\mingw-bigobj-fixup.cmake` or Qt's `Qt6Core.dll` is missing under `P:\workspace\soc\toolchain`.

**Fix**: reinstall the missing component in the user directory following module F of `docs/arch/PLAN.md` (`pip` into `toolchain\venv`, `aqt` for Qt and MinGW). Do not switch to a system-wide install and do not modify the system PATH.

### Q1.2 CMake cannot find Qt6 / Qt6Charts

**Cause**: Qt is older than 6.8, the Charts module is not installed, or `CMAKE_PREFIX_PATH` does not point to Qt.

**Fix**: install Qt 6.8+ with Charts and pass `-DCMAKE_PREFIX_PATH=<Qt dir>` when configuring; afterwards reconfigure with `build.ps1 -Configure` (or delete `build/CMakeCache.txt`). See section 6 of [Chapter 01](01-install.md).

### Q1.3 The build runs out of memory or freezes the machine half-way

**Cause**: the processor model sources are template-heavy; each compiler process needs about 1.5 GB of memory.

**Fix**: reduce parallelism: `build.ps1 -Jobs 2`, or `cmake --build build -j 2`.

### Q1.4 Missing submodules (`external/VSRTL` etc. do not exist)

**Fix**: `git submodule update --init --recursive`.

### Q1.5 TLS / certificate errors while downloading

**Fix**: configure the correct proxy or root certificates. **Never** disable TLS verification (e.g. `git config http.sslVerify false`, `pip --trusted-host` and the like); that makes downloads open to tampering.

### Q1.6 Double-clicking `Ripes.exe` reports a missing `Qt6Core.dll`

**Cause**: the Qt runtime libraries are not on PATH.

**Fix**: in the PowerShell session that starts it, run `$env:PATH = "P:\workspace\soc\toolchain\Qt\6.8.3\mingw_64\bin;" + $env:PATH` and then start it; or deploy the needed DLLs and plugins next to the program once with Qt's `windeployqt build\Ripes.exe` (verified: the deployed copy starts without Qt on PATH; note that it does not include the `offscreen` platform plugin).

### Q1.7 The program exits immediately on a server / remote terminal

**Cause**: there is no display.

**Fix**: set `QT_QPA_PLATFORM=offscreen`. Note that dialogs are invisible in offscreen mode, see Q3.4.

---

## 2. Assembling and running

### Q2.1 Running a GPU/NPU example in command-line mode reports `Unknown opcode 'li'`

**Symptom**:

```text
ERROR: Error during assembly:
INFO: Unknown opcode 'li'
```

**Cause**: symbols such as `GPU_0_BASE`, `TENSOR_UNIT_0_*` and `LED_MATRIX_0_BASE` are exported by peripherals **already created** in the **I/O** tab. Command-line mode creates no peripherals, so these symbols are undefined; the assembler cannot match an `li`/`la` with an unknown symbol to any valid form and reports "Unknown opcode" rather than "unknown symbol".

**Fix**: such examples can only run in the GUI:

1. Switch to the **I/O** tab and double-click the needed peripheral (e.g. **SIMT GPU**) in the **Available devices** list.
2. Check that the symbols the program uses (e.g. `GPU_0_BASE`) appear in **I/O exports** at the bottom left.
3. Then load the program with **File → Load Example...**.

See section 6 of [Chapter 15](15-cli.md).

### Q2.2 The GUI also reports `Unknown opcode 'li'` (or a symbol-related error)

**Cause**: the peripheral has not been created, or it was created with a different number. Symbol names contain the peripheral's number, and the lowest free number of that device type is always reused (`claimPeripheralId` in `src/io/iobase.h`): the first GPU is `GPU_0_...`, deleting it and creating it again gives `GPU_0_...` again, but a second GPU created while the first still exists is `GPU_1_...`.

**Fix**: look at the actual symbol names in **I/O exports** and make the program match them; close extra peripherals and recreate them if necessary.

### Q2.3 `mul`, `div`, `rem` report `Unknown opcode`

**Cause**: the current processor does not have the **M** extension enabled.

**Fix**: GUI: **Select processor** → tick `M` under **ISA Exts.** on the right → **OK**. CLI: add `--isaexts M`.

### Q2.4 A line is marked red but I cannot see what is wrong

**Fix**: hover over the red line to see the error message. Common causes:

- A misspelled instruction or register name (`adi` instead of `addi x1, x1, 1`).
- An immediate out of range (the `addi` immediate is a 12-bit signed number, -2048…2047; use `li` for larger values).
- A label defined twice or not defined.
- An unknown directive (`Unknown directive '.foo'`), or a `.string`/`.byte` in `.text` whose length leaves the next instruction unaligned (`Instruction misaligned (24-bit boundary). This instruction must be aligned on a 32-bit boundary.`); `.word`, `.zero` and whole words of `.byte` are fine, and instructions in `.data` are accepted (they are just stored as data).

Until it is fixed, **Executable code** on the right keeps the last successfully assembled program — so "the result did not change" often means the source actually has an error.

### Q2.5 A C example says "No C compiler set"

**Symptom**: switching **Input type** to **C**, or loading a C example, shows:

```text
No C compiler set.

Provide a path to a valid C compiler under:
 Edit->Settings->Editor
```

**Cause**: no RISC-V cross compiler is configured. Note: the message says the **Editor** page, but the compiler path is actually on the **Edit → Settings → Compiler** page.

**Fix**:

1. Install a bare-metal RISC-V toolchain (`riscv64-unknown-elf-gcc`, with 32-bit multilib support).
2. **Edit → Settings → Compiler**: type or **Browse** to `riscv64-unknown-elf-gcc` in **Compiler path:**; the field turns green when the path is valid.
3. Back in **Editor**, select **C** and press **Ctrl+B** to compile.

If you do not have a compiler yet, start with the precompiled `RanPi-RV32` from **Load Example... → ELF (precompiled C)**. See [Chapter 05](05-c-programs.md).

### Q2.6 The program never finishes

**Cause**: the program never calls the exit `ecall` (`li a7, 10` + `ecall`), the PC ran past the end of the program, or it is waiting for a peripheral (e.g. polling the GPU's DONE bit while the GPU was not created or not started).

**Fix**: press **Run** (F8) again to pause; check where the PC is in **Instruction memory**; set a breakpoint on the suspicious loop and watch it with **Auto clock**. In the CLI use `--timeout`.

### Q2.7 The Clock / Run buttons are grey

**Cause**: the program has exited (it executed the exit `ecall`), or Run / Auto clock is active.

**Fix**: press **Reset** (F3).

### Q2.8 Numbers in the console have extra spaces around them

That is the format `ecall` uses for integers, e.g. `Factorial value of  7 is  5040`. Normalise whitespace when comparing output.

---

## 3. Peripherals and accelerators

### Q3.1 I cannot create peripherals / change parameters / close peripherals in the I/O tab

**Cause**: the processor is in **Run**. While running, the peripheral list, the parameter table and the close buttons are disabled.

**Fix**: pause first (press F8 again).

### Q3.2 After changing a peripheral parameter, the addresses in my program are wrong

**Cause**: a peripheral's size may change with its parameters (e.g. the LED matrix width/height), which moves the addresses of the peripherals after it.

**Fix**: **always use the exported symbols** in programs (`LED_MATRIX_0_BASE` etc.), never hard-coded addresses. After a parameter change the program is **not** reassembled automatically — the editor only reassembles when the source text or the processor changes (`EditTab::sourceCodeChanged` in `src/edittab.cpp`), and the peripheral symbols are read at assembly time — so make any edit to the source (or reload the program) to pick up the new addresses; C programs must be recompiled.

### Q3.3 The GPU / NPU widget does not change

**Check**:

1. Is the matching example loaded (`gpu_vecadd.s` / `npu_matmul.s`) without assembler errors?
2. Is the processor running (F5/F6/F8)? Accelerators advance one step per CPU cycle; if the processor does not move, neither do they.
3. During **Run** the widget refresh is throttled; use **Auto clock** to watch the process.

See [Chapter 09](09-gpu.md) and [Chapter 10](10-npu.md) for more.

### Q3.4 The program / screenshot tool "hangs" in offscreen mode

**Cause**: some action opened a dialog (e.g. a file error message, or `--help` / a command-line option error in an **old** build — the current build prints these to the console instead, see section 7 of [Chapter 15](15-cli.md)); under `QT_QPA_PLATFORM=offscreen` the dialog is invisible and the program waits for you to click it.

**Fix**: reproduce it first on a normal desktop to see which dialog it is; put a timeout around the outer call in scripts.

---

## 4. Reverse execution

### Q4.1 The Reverse button is grey

**Cause**:

- You just reset, or you have already undone everything;
- The limit **Edit → Settings → Simulator → Max. undo cycles** (default 100) has been reached;
- **Run** or **Auto clock** is still active — all stepping buttons are disabled while running (`ProcessorTab::run` in `src/processortab.cpp`). Once Run stops, the last **Max. undo cycles** cycles can be reversed like any others: Run clocks the same circuit, so the undo history is kept (verified: after running `neogeo_mailbox.s` to the end, Reverse is enabled).

**Fix**: increase **Max. undo cycles** for a longer undo history (uses more memory); to watch a section repeatedly, set a breakpoint before it and walk through it with **Auto clock** or **Clock**.

### Q4.2 After reversing, the GPU / NPU state is wrong

**Cause**: Reverse restores the processor, registers and memory (the cache simulator is rewound as well). The **SIMT GPU** and **Tensor Unit (NPU)** peripherals are separate models that record their own state every CPU cycle and rewind together with Reverse — but only as far as their own history reaches (at most **Max. undo cycles** entries, the GPU also within a memory budget, and nothing from before the peripheral was created).

What happens when you reverse past that history (confirmed in `src/io/iogpu.cpp` and `src/io/iotensorunit.cpp`):

- The **GPU** is **reset to its pre-launch state** (launch registers kept; memory the kernel already wrote is not undone) and the widget says "Reverse execution went past the recorded GPU history";
- The **NPU keeps its current state** for that step and the widget says "Reverse execution went further back than the NPU history"; a normal step shows "Reversed one cycle: the NPU state and the memory written by its DMA in that cycle were rewound together with the CPU".

**Advice**: when studying an accelerator, rerun from the start with **Reset** (F3) instead of stepping back with Reverse; to observe an exact cycle, set a breakpoint on the instruction that launches the kernel / rings the doorbell and then single-step with F5.

---

## 5. UI, theme and settings

### Q5.1 How do I switch between dark and light themes?

**Edit → Settings → Environment → Color scheme**: **System** (follow the OS), **Light**, **Dark**. The change is immediate; the circuit diagram, toolbar icons and I/O backdrop follow.

If you chose **System** and nothing turned dark, the OS did not report dark mode: **System** just hands the choice back to Qt (`QStyleHints::unsetColorScheme` in `src/ripessettings.cpp`), which follows the OS setting on Windows, macOS and most Linux desktops; simply choose **Dark**.

### Q5.2 How do I switch the UI language?

**Edit → Settings → Environment → Language / 语言**, choose **English** or **简体中文**, and **restart** the program. **System** follows the OS locale. See section 7 of [Chapter 03](03-gui-tour.md).

### Q5.3 Some text is still English after switching to Chinese

**Cause**: the translation does not cover it yet (e.g. a newly added peripheral description), or the text comes from the program / topology data itself.

**Fix**: report the missing translation as described in section 7, stating the tab and the original text.

### Q5.4 Where are settings stored? How do I restore the defaults?

The program uses Qt's `QSettings` with organisation and application name both `Ripes`:

| Platform | Location |
|---|---|
| Windows | Registry `HKEY_CURRENT_USER\Software\Ripes\Ripes` |
| Linux | `~/.config/Ripes/Ripes.conf` |
| macOS | A `.plist` file named after Ripes under `~/Library/Preferences/` (exact file name to be verified) |

Stored there are: every option of the **Settings** dialog, the current processor with its extensions and layout, the source code in the editor, created peripherals and their parameters, cache presets, the window position, the UI language and more.

**Restoring defaults**: close the program, delete the registry key or file from the table above, and start again. **Note**: this also discards an unsaved program in the editor and custom cache presets; save them first.

> The same location is shared with upstream Ripes (both set organisation and application name to `Ripes`, see `src/ripessettings.cpp`): if upstream Ripes is also installed on the same computer, both read and write these settings.

### Q5.5 The window is too small and information is cut off

The areas are separated by draggable splitters that you can resize; I/O peripheral sub-windows can be dragged out of the main window. The circuit in the Processor tab can be zoomed with Ctrl+wheel and is fitted to the window automatically at start-up.

The window fits a 1280×800 screen. Where a part does not fit, it scrolls instead of being cut off: the welcome panel (close it with its × button), the left column of the Cache tab, and the device area of the I/O tab. The SIMT GPU and Tensor Unit windows stack their panels vertically when they are narrow; widen the device area (drag the splitters) to put them side by side again.

---

## 6. Performance

### Q6.1 Run is slow

From the largest to the smallest effect:

1. **Change the processor**: the **ISA simulator** is fastest (no circuit simulation), the **Single-cycle processor** next; pipelined processors are slowest. The single-cycle processor is also recommended for cache simulation.
2. **Reduce cache plot overhead**: lower **Settings → Environment → Max. cache plot cycles** (e.g. to 1000).
3. **Reduce UI refreshes**: lower **Max. UI updates per second**.
4. **Close peripherals you do not need**: especially **Screen** and the accelerators' visualisations.
5. **Turn off VCD tracing**: untick **Settings → Simulator → Enable simulation VCD traces**.
6. Use command-line mode for batch experiments ([Chapter 15](15-cli.md)); it has no GUI overhead at all.

### Q6.2 Auto clock is too slow / too fast

Change the interval box next to **Auto clock** in the toolbar (1–10000 ms).

### Q6.3 The Clock rate value jumps around a lot

Increase **Settings → Simulator → Clock rate averaging window (ms)**.

### Q6.4 After running for a long time the program becomes sluggish and memory grows

Lower **Max. cache access traces**, **Max. pipeline diagram cycles** and **Max. undo cycles**.

---

## 7. How to report a bug

1. **Make sure you can reproduce it**: write down the minimal steps ("open example X → select processor Y → press F8 → see Z").
2. **Collect information**:
   - Version: the version number shown by **Help → About Ripes...**;
   - Processor and ISA: `Processor: ... ISA: ...` on the right of the status bar;
   - Operating system, UI language, colour scheme;
   - The program used (attach the `.s`/`.c` file or name the example), created peripherals and their parameters;
   - The exact error message and screenshots; for command-line problems, the full command and its output.
3. **If it reproduces on the command line, include the CLI command** — maintainers can then run it directly.
4. **Where to report**: problems with features added by this project (open-soc-explorer) — GPU, NPU, Architecture tab, Chinese translation, this guide — go to this project's issue tracker or to your course teacher (exact address to be verified); for basic features shared with upstream Ripes, first search the upstream repository <https://github.com/mortbopet/Ripes/issues> to see whether it is already known.
5. Errors in this guide: name the chapter file (e.g. `docs/guide/en/16-faq.md`) and the paragraph.

---

## Exercises

1. Run `examples/assembly/leds.s` in command-line mode. What error do you see? Explain the cause with this chapter, then make it run correctly in the GUI.
2. Open **Select processor**, untick the `M` extension and load `factorial.s`. Which line turns red? What is the error message?
3. Find where Ripes stores its settings on your computer and look at the keys stored there.
4. Run the same long program (e.g. the ELF example `RanPi-RV32`) on the Single-cycle processor and on the 5-stage processor and compare how long Run takes.

## Summary

- Build problems are mostly about the Qt version/modules, submodules, memory or PATH; never disable TLS verification.
- Command-line mode has no peripherals, and peripheral symbols lead to a misleading `Unknown opcode 'li'`; run such examples in the GUI after creating the peripheral.
- Multiply/divide needs the `M` extension; C programs need a RISC-V compiler configured in **Settings → Compiler**.
- Reverse has an undo limit, and accelerator peripherals do not necessarily rewind; prefer rerunning with Reset when studying accelerators.
- Theme and language are in **Settings → Environment**; settings are stored via `QSettings` (Windows registry `HKCU\Software\Ripes\Ripes`).
- To go faster: ISA simulator / single-cycle processor, less plotting and refreshing, close unneeded peripherals, use the CLI for batch experiments.

Next chapter: [17 Glossary](17-glossary.md).
