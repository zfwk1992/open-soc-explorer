# Development guide

## 1. Requirements

* CMake ≥ 3.13 (3.28 used), Ninja, a C++20 compiler (GCC 13 / Clang 18 /
  MSVC 2022 / Apple Clang).
* **Qt ≥ 6.8** with the *Qt Charts* and *Qt SVG* modules (upstream CI uses
  Qt 6.9.3). Ubuntu 24.04's Qt (6.4) is too old.
* OpenGL development files (Linux: `libgl1-mesa-dev`).
* Git with submodules (`external/VSRTL`); CMake fetches libelfin, ELFIO,
  cereal, Signals and magic_enum from GitHub at configure time.
* Optional: `clang-format-15` (the CI formatter version), Python ≥ 3.8 for
  `tools/trace`.

## 2. Getting Qt

Any of these works:

* Qt online installer / `aqtinstall` (as upstream CI does via
  `jurplel/install-qt-action`): `aqt install-qt linux desktop 6.9.3 linux_gcc_64 -m qtcharts`.
* conda-forge (used for the development of Open SoC Explorer, because
  `download.qt.io` was not reachable from that environment):

  ```bash
  micromamba create -y -p /opt/qt -c conda-forge "qt6-main=6.9.*" "qt6-charts=6.9.*"
  # then pass -DCMAKE_PREFIX_PATH=/opt/qt to CMake
  ```

## 3. Build

```bash
git clone --recursive https://github.com/zfwk1992/open-soc-explorer.git
cd open-soc-explorer
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DRIPES_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/path/to/qt
ninja -C build
./build/Ripes                     # GUI (executable name kept as in upstream)
```

The sources are globbed at configure time: after adding a `.cpp/.h` file,
re-run `cmake -S . -B build`. If AutoMoc does not pick up a new `Q_OBJECT`
class, re-run CMake (or delete `build/src/ripes_lib_autogen`).

## 4. Tests

```bash
# all QtTest suites, as CI does (from the build directory's test folder)
cd build/test && QT_QPA_PLATFORM=offscreen bash ../../.github/workflows/scripts/run_tests.sh

# a single suite / single function
QT_QPA_PLATFORM=offscreen ./build/test/tst_trace
QT_QPA_PLATFORM=offscreen ./build/test/tst_missions tst_mission2

# Python trace tool (+ CLI export checks when the binary is given)
OSX_RIPES_BIN=$PWD/build/Ripes python3 -m unittest discover -s tools/trace -v

# gem5 converter on recorded gem5 output (+ comparison with Ripes)
OSX_RIPES_BIN=$PWD/build/Ripes python3 -m unittest discover -s tools/gem5 -v

# opt-in: load/replay time of a large trace
OSX_LARGE_TRACE=/path/to/trace.jsonl ./build/test/tst_trace tst_largeTrace

# GUI test with screenshots
OSX_SCREENSHOT_DIR=/tmp/shots ./build/test/tst_explorergui
```

| Suite | Covers |
|---|---|
| `tst_riscv`, `tst_assembler`, `tst_expreval`, `tst_cosimulate`, `tst_reverse`, `tst_stall` | upstream Ripes |
| `tst_trace` | schema, decoder, recorder on all models, replay vs. live model, JSON round trip, determinism, reverse, truncation, system description, multi-occupancy and same-cycle commit, a real gem5 O3 trace |
| `tst_missions` | mission catalogue, solvability, measured numbers, explainer, existence of cited source files, cache model (incl. equality with Ripes' cache simulator) |
| `tst_explorergui` | main window end to end: missions, explorer, component inspector, System view, a gem5 trace in the Explorer |
| `tools/trace/test_osx_trace.py` | Python trace tool |
| `tools/gem5/test_gem5_to_osx.py` | gem5 converter on recorded gem5 output; comparison with Ripes |

The tests that use gem5 data run `tools/gem5/gem5_to_osx.py` on the
recorded output in `tools/gem5/testdata` (gem5 itself is not needed) and
skip if no Python interpreter is found.

Results at the time of writing: 9 suites, 119 test functions, all passing
(56 upstream + 63 Open SoC Explorer, three of them opt-in), plus 7 Python
trace-tool tests and 20 gem5 converter tests.

### gem5

Building and running gem5 is described in
[tools/gem5/README.md](../tools/gem5/README.md). It is only needed to
produce new gem5 traces or test data.

## 5. Translations

The user interface is English with a Simplified Chinese translation
(`src/i18n`, *View → Language*). Strings use Qt's standard mechanism:

* Open SoC Explorer code wraps user-visible text in `tr()` (or
  `Q_DECLARE_TR_FUNCTIONS` helpers / `QT_TRANSLATE_NOOP` in free functions
  and tables) and re-renders on `QEvent::LanguageChange`. Keep English
  plurals as two source texts (singular/plural), as `mission.cpp` does.
* Descriptive texts stored in traces stay English; mark them with
  `QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", ...)` where they are
  written and they are translated when shown (`explorer/tracetext.h`).
  Texts of `tools/gem5/gem5_to_osx.py` are listed in
  `src/explorer/tracetext.cpp`; `tst_explorergui::tst_language` fails if a
  converted gem5 trace contains a text that is not translated.
* Upstream Ripes sources are not edited for translation: their `.ui` and
  `tr()` strings are in the same catalogue, and texts set from plain
  literals are listed in `src/i18n/upstreamstrings.cpp` (context
  `Upstream`) and applied by `I18n::retranslateUpstream()`.

Updating the catalogue `translations/open-soc-explorer_zh_CN.ts`:

```bash
cmake --build build --target osx_update_translations   # lupdate over src/
/path/to/qt/bin/linguist translations/open-soc-explorer_zh_CN.ts   # or edit the XML
cmake --build build                                     # lrelease, embedded as :/i18n/*.qm
```

The `.qm` file is compiled at build time (`qt_add_lrelease`, Qt's own
`qtbase` catalogue merged in with Qt ≥ 6.9) and embedded as a resource, so
it also works in the WebAssembly build. `tst_missions::tst_translationCatalogue`
fails on unfinished or empty translations and on changed placeholders.

Missions: each mission JSON may contain `"i18n": {"zh_CN": {...}}` with
`title`, `objective`, `explanation`, `learn` (same length as the English
list), `completion`, `steps` (`title`, `instruction`, same order) and
`challenge` (`text`, `hint`). Ids, processors, examples and checks are
shared. Missing texts fall back to English;
`tst_missions::tst_missionTranslations` requires complete translations.
Terminology used: 流水线, 取指, 译码, 执行, 访存, 写回, 冒险, 前递, 停顿,
冲刷, 作废 (squash), 退休 (retire), 提交 (commit), 缓存行, 组相联, 命中率,
预取器, 保真度, 可观测性, 轨迹 (trace).

## 6. CLI trace export

```bash
./build/Ripes --mode cli -t asm --src prog.s --proc RV32_5S \
              --cycles --cpi --trace-out trace.jsonl
python3 tools/trace/osx_trace.py summary trace.jsonl
python3 tools/trace/osx_trace.py pipeline trace.jsonl
```

## 7. Code style and conventions

* `clang-format-15 -i <files>` (LLVM style, `.clang-format`). CI checks the
  files changed in the last commit of each push.
* Match the surrounding code; Open SoC Explorer code lives in
  `namespace Ripes::Explorer` / `Ripes::Trace` and new files start with
  `// SPDX-License-Identifier: MIT`.
* Qt gotchas found during development: do not name members `slots`,
  `signals` or `emit` inside `Q_OBJECT` classes (moc keywords).
* Anything shown to users must be derived from simulator output or the ISA
  specification and labelled with its observability; see
  [SIMULATION_FIDELITY.md](SIMULATION_FIDELITY.md).

## 8. Syncing with upstream Ripes

```bash
git remote add upstream https://github.com/mortbopet/Ripes.git   # once
git fetch upstream
git checkout -b sync/ripes-$(date +%Y%m%d) main
git merge upstream/master        # never rebase or force-push shared branches
git submodule update --init --recursive
cmake -S . -B build && ninja -C build && (cd build/test && bash ../../.github/workflows/scripts/run_tests.sh)
```

Expected conflict points (kept small on purpose): `README.md` (Open SoC
Explorer README; the upstream README lives in `docs/RIPES_README.md` – update
that copy), `LICENSE` (extra copyright line), `src/mainwindow.*`,
`src/processortab.*`, `src/ripessettings.*`, `src/cli/*`, `src/CMakeLists.txt`,
`external/fancytabbar` (`setTabText`), `translations/*.ts` (re-run
`osx_update_translations` and translate new upstream strings),
`test/CMakeLists.txt`. After merging, re-check
[RIPES_ARCHITECTURE.md](RIPES_ARCHITECTURE.md) and run `tst_trace`, whose
`tst_replayMatchesSimulator` and dual-issue assertions detect model changes.

## 9. Continuous integration

* `test.yml` (upstream): builds with Qt 6.9.3 on Ubuntu 24.04/22.04, macOS and
  Windows and runs every `tst_*` binary – this includes the Open SoC Explorer
  suites.
* `clang-format.yml` (upstream), `wasm-build.yml`, `docker-build.yml`
  (upstream).
* `osx-checks.yml`: Python trace tool tests and mission JSON validation.
* Upstream release/deploy jobs are guarded with
  `github.repository == 'mortbopet/Ripes'` or only publish from `master`/tags,
  so they do not publish anything from this repository.
