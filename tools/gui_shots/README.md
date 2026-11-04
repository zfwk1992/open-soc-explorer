# gui_shots: screenshots for the user guide

`gui_shots` regenerates the screenshots in `docs/guide/images`. It builds the
real `Ripes::MainWindow` under Qt's `offscreen` platform, runs a short script of
GUI actions (load an example, clock, add a peripheral, open a dialog, …) and
saves a `grab()` of a widget as a PNG. Each screenshot is a fresh process, so
every shot starts from default settings.

The scripts live in the manifests `docs/guide/images/manifest_*.json`, in the
`script` array of each entry. `shot_note` records where a shot differs from the
original request (for example, no C compiler was available).

## Build

The target is off by default and is not a test. `build.ps1` does not forward
cmake options, so set the default with an environment variable at configure
time:

```powershell
$env:RIPES_BUILD_GUI_SHOTS = "ON"
powershell -ExecutionPolicy Bypass -File P:\workspace\soc\build.ps1 -Configure -Target gui_shots
```

Other CMake setups can pass `-DRIPES_BUILD_GUI_SHOTS=ON` directly. The tool links
`ripes_lib` like `Ripes.exe` and embeds the same resources (icons, examples,
layouts, fonts, translations and the guide).

## Run

```powershell
powershell -ExecutionPolicy Bypass -File tools\gui_shots\run.ps1            # every entry
powershell -ExecutionPolicy Bypass -File tools\gui_shots\run.ps1 -Filter "09-gpu-*" -Lang zh -Jobs 4
powershell -ExecutionPolicy Bypass -File tools\gui_shots\run.ps1 -List
```

`run.ps1` puts Qt's `bin` on `PATH` and sets `QT_QPA_PLATFORM=offscreen` and
`QT_QPA_FONTDIR=C:\Windows\Fonts` for the session only. A full run takes about
two minutes with `-Jobs 4`. Entries with an `arch_render` field (the chapter 14
diagram) are rendered with `build/arch_render/arch_render.exe`. Build that with
`bash tools/arch_render/build.sh`.

The tool can also be run by hand, either on a single manifest entry or on an
ad-hoc script:

```bash
build/gui_shots.exe --manifest docs/guide/images/manifest_docs-start.json --entry 02-quickstart-editor.en.png
build/gui_shots.exe --lang zh --script 'load :/examples/assembly/factorial.s;tab processor;clock 5;grab window' --out out.png
```

Relative program paths are resolved against `GUI_SHOTS_REPO` (default: the
current directory).

## Settings isolation

`QSettings` is switched to an INI file in a private temporary directory before
anything reads a setting, so the user's real settings
(`HKCU\Software\Ripes\Ripes`, including `ui_language`) are neither read nor
written. The same directory is used as the process's `TEMP`, because Ripes
writes `ripes_system.h` to the temp directory. It is deleted when the tool
exits.

## Script commands

Each line is split into words; `"double quotes"` group words.

| Command | Meaning |
|---|---|
| `size W H` | Main-window size (default 1280 800). *Before the window is created.* |
| `welcome on\|off` | Welcome panel (default off). *Before the window is created.* |
| `processor <ProcessorID> [layout N]` | For example, `RV32_SS` or `RV32_5S layout 1` (Extended). *Before the window is created.* |
| `setting <key> <value>` | Any `RipesSettings` key, for example `cacheplot_maxcycles 200000`. *Before the window is created.* |
| `tab editor\|processor\|cache\|memory\|io\|architecture` | Switch tab. |
| `load <path>` | `:/examples/...` resource or a repository path. `.s` is loaded as assembly, `.c` as C, anything else as an ELF. Waits until the program is assembled. |
| `source` | Load the entry's `source` field as assembly. |
| `peripheral "<device title>" [id=value ...]` | Add a device through the I/O tab's *Add selected device* button. The title is the English one from `IOTypeTitles`; the parameters are `IOBase` parameter ids, for example `peripheral "SIMT GPU" 0=1`. |
| `clock N` / `reset` | Trigger *Clock (F5)* N times, or *Reset (F3)*. |
| `run [timeoutMs]` / `runfor ms` | *Run (F8)* until it finishes (or until a breakpoint), or run for a while and stop. |
| `break <addr>` | Set a breakpoint. `<addr>` is `0x…`, `sym:label[+off]`, `section:.data` or `reg:sp`. |
| `until <cond> [max N]` | Clock until the condition holds: `pc <addr> [stage S] [count K]`, `gpu.reg <index>`, `gpu.launched`, `gpu.issued <sm> <pc> <count>`, `npu.state <STATE>`, `npu.compute <t>`, `npu.dma <bytes>`. |
| `memgoto <addr>` | Centre the Memory tab's viewer on an address. |
| `combo <objectName> <index> [nth]`, `spin <name> <v>`, `click <name or text>`, `select <tree/list> <text>`, `subtab <tabwidget> <index> [periph <name>]`, `resize <widget> W H` | Generic widget actions (inside the open dialog if there is one). |
| `mdi tile\|cascade`, `mdiwin <peripheral> x y w h` | Arrange the I/O tab's sub-windows. |
| `arch topology <id>`, `arch flow <id\|index>`, `arch next [n]`, `arch step <n>`, `arch block <id>`, `arch fit` | Architecture tab. |
| `dialog settings <page>` | Open the Settings dialog non-modally. Pages: 0 Environment, 1 Simulator, 2 Compiler, 3 Editor. |
| `dialog processor`, `dialog load elf\|binary\|source [file]`, `dialog pipeline`, `dialog help <chapter>` | Open other dialogs non-modally. Each accepts `size W H`. |
| `hover_error <line>` | Show the assembler-error tooltip of a source line; it is composited into the grab. |
| `browserfind <text>` | Scroll the text browser (for example the help dialog) so that the phrase is at the top. |
| `wait ms` | Process events for a while. |
| `dumpmin [minH] [minW] [all]` | Diagnostic: print the visible (with `all`: also hidden) widget tree (main window, or the open dialog) with each widget's minimum size; only widgets at or above either threshold are listed. |
| `grab window\|tab\|toolbar\|dialog\|archexport` | Save the PNG. Targets also include `periph <name>`, `periphwidget <name> [size W H]` and `widget <objectName or class>`. Options: `crop x y w h`, `maxwidth W`. |

Output PNGs are RGB, saved with maximum zlib compression. They are embedded
in `Ripes.exe` through the guide resource, so keep the total size small:
crop to the relevant widget where the whole window isn't needed.
