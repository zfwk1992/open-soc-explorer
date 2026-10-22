# Licensing and attribution

This document explains how Open SoC Explorer handles its own license, the
license of Ripes and the licenses of third-party components. It records what
was verified in this repository; it is not legal advice. If you distribute
binaries commercially or combine this project with other code, check the
licenses yourself.

## 1. Open SoC Explorer and Ripes

* Open SoC Explorer is a fork of [Ripes](https://github.com/mortbopet/Ripes)
  with its complete Git history preserved (merge commit `2059036`).
* Ripes is licensed under the **MIT License**,
  `Copyright (c) 2019 Morten Borup Petersen` (`LICENSE`). The license text and
  this notice are kept unchanged.
* New Open SoC Explorer code is also licensed under the MIT License. New files
  carry `SPDX-License-Identifier: MIT`. The `LICENSE` file lists the Open SoC
  Explorer contributors in an additional copyright line; it does not replace
  the original notice.
* Upstream authorship is preserved in the Git history. Do not rewrite or
  squash upstream commits.

## 2. Components and their licenses

Verified from the license files in this checkout / fetched dependencies:

| Component | Where | License | Notes |
|---|---|---|---|
| Ripes | repository | MIT (`LICENSE`) | © 2019 Morten Borup Petersen |
| VSRTL | `external/VSRTL` (git submodule) | MIT (`external/VSRTL/LICENSE`) | © 2019 Morten Borup Petersen |
| FancyTabBar | `external/fancytabbar` (vendored) | MIT (`external/fancytabbar/LICENSE`) | © 2015 Jan Šipr |
| libelfin (mortbopet fork) | fetched by CMake (`external/CMakeLists.txt`) | MIT | © 2013 Austin T. Clements |
| ELFIO | fetched by CMake | MIT (`COPYING`) | © Serge Lamikhov-Center |
| cereal | fetched by VSRTL's CMake | BSD-3-Clause | © Randolph Voorhies, Shane Grant |
| Signals | fetched by VSRTL's CMake | `Signal.h`: MIT; `Delegate.h`: public domain (per its README) | |
| magic_enum | fetched by VSRTL's CMake | MIT | © Daniil Goncharov |
| `src/STLExtras.h` | repository | Apache-2.0 WITH LLVM-exception (file header) | Derived from LLVM; not MIT |
| riscv-tests | `test/riscv-tests*` | BSD-3-Clause (`test/riscv-tests/LICENSE`) | © The Regents of the University of California; test data only |
| Inconsolata font | `resources/fonts/Inconsolata` | SIL Open Font License 1.1 (`OFL.txt`) | © The Inconsolata Project Authors |
| Icons | `resources/icons` | Flaticon licence (attribution required) | Authors credited in `resources/icons/Credits.txt` (Smashicons, Freepik, Vectors Market, Pixel Buddha via flaticon.com). Upstream does not record the licence per file. Open SoC Explorer adds **no** new icons; it reuses `trace.svg` and `compass.svg` from this set. |
| Qt 6 base modules (Core, Gui, Widgets, Svg, ...) | linked at build time | LGPL-3.0 (open-source option) or commercial | Dynamic linking satisfies the LGPL when users can replace the Qt libraries. |
| **Qt Charts** | linked at build time | **GPL-3.0** (open-source option) or commercial | The conda-forge package used for development reports `GPL-3.0-only`. |

## 3. What the Qt Charts license means

Ripes (and therefore Open SoC Explorer) links Qt Charts for the cache plots.
Qt Charts is not available under the LGPL. As a consequence:

* The **source code** in this repository remains MIT (MIT is compatible with
  the GPL).
* A **binary** that links Qt Charts under its open-source license is a
  combined work that must be distributed under GPL-3.0 terms (complete
  corresponding source, license text, no additional restrictions). This
  applies to upstream Ripes binaries as well; Open SoC Explorer does not
  change it.
* Alternatives if this matters for a use case: a commercial Qt license, or a
  build without Qt Charts (not supported by the current code).

## 4. Rules for contributions

1. New original code: MIT, with an `SPDX-License-Identifier: MIT` line.
2. Never remove or alter existing copyright notices or license files.
3. Copying third-party code requires checking its license for compatibility
   and adding it to the table above (and keeping its notice in the file).
4. **Imported RTL, accelerator models or simulators** (for example Ibex,
   Gemmini, VTA, NVDLA, gem5) have their own licenses (often Apache-2.0,
   BSD-3-Clause or custom). They must be added as clearly separated
   submodules or fetched dependencies, listed here, and must not be relicensed.
   Do not assume they are covered by the MIT license of this repository.
5. New assets (images, fonts): only with a known license that permits
   redistribution; record the source and license here.
6. Generated traces and screenshots of the application are project
   documentation and are covered by the repository license.

## 5. External simulators (planned integrations)

These are *not* part of the repository today. When integrated, they will be
used as external tools or separate submodules:

| Project | License (upstream, to be re-verified at integration time) |
|---|---|
| gem5 | BSD-3-Clause-style (various copyright holders) |
| lowRISC Ibex | Apache-2.0 |
| Verilator | LGPL-3.0 or Artistic-2.0 (tool; generated C++ has its own terms, see Verilator docs) |
| Gemmini | BSD-3-Clause (UC Berkeley) |
| Apache TVM / VTA | Apache-2.0 |
| NVDLA | NVIDIA Open NVDLA License |
