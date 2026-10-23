// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
//
// Texts that upstream Ripes sets from plain string literals (without tr()),
// listed here so that lupdate puts them into the translation catalogue
// (context "Upstream"). I18n::retranslateUpstream() looks them up when the
// language changes, which keeps the upstream sources unchanged. A literal
// must match the upstream text exactly; texts built at run time (e.g. the
// status bar's processor name) cannot be listed and stay English.
#include "upstreamstrings.h"

#include <QtGlobal>

namespace Ripes {
namespace I18n {

namespace {
const char *const kUpstreamTexts[] = {
    // src/mainwindow.cpp: tool bars, menus and actions
    QT_TRANSLATE_NOOP("Upstream", "Simulator control"),
    QT_TRANSLATE_NOOP("Upstream", "Edit"),
    QT_TRANSLATE_NOOP("Upstream", "Processor"),
    QT_TRANSLATE_NOOP("Upstream", "Cache"),
    QT_TRANSLATE_NOOP("Upstream", "Memory"),
    QT_TRANSLATE_NOOP("Upstream", "I/O"),
    QT_TRANSLATE_NOOP("Upstream", "New Program"),
    QT_TRANSLATE_NOOP("Upstream", "Load Program"),
    QT_TRANSLATE_NOOP("Upstream", "Load Example..."),
    QT_TRANSLATE_NOOP("Upstream", "Assembly"),
    QT_TRANSLATE_NOOP("Upstream", "ELF (precompiled C)"),
    QT_TRANSLATE_NOOP("Upstream", "Save File"),
    QT_TRANSLATE_NOOP("Upstream", "Save File As..."),
    QT_TRANSLATE_NOOP("Upstream", "Exit"),
    QT_TRANSLATE_NOOP("Upstream", "Save current program before exiting?"),
    QT_TRANSLATE_NOOP("Upstream", "New Program..."),
    QT_TRANSLATE_NOOP("Upstream", "Save program before creating new file?"),
    QT_TRANSLATE_NOOP("Upstream", "File error"),
    QT_TRANSLATE_NOOP("Upstream", "Could not create temporary ELF file"),
    // src/processortab.cpp: simulator control
    QT_TRANSLATE_NOOP("Upstream", "Select processor"),
    QT_TRANSLATE_NOOP("Upstream", "Reset (F3)"),
    QT_TRANSLATE_NOOP("Upstream", "Reset the simulator (F3)"),
    QT_TRANSLATE_NOOP("Upstream", "Reverse (F4)"),
    QT_TRANSLATE_NOOP("Upstream", "Undo a clock cycle (F4)"),
    QT_TRANSLATE_NOOP("Upstream", "Clock (F5)"),
    QT_TRANSLATE_NOOP("Upstream", "Clock the circuit (F5)"),
    QT_TRANSLATE_NOOP("Upstream", "Auto clock (F6)"),
    QT_TRANSLATE_NOOP("Upstream",
                      "Clock the circuit with the selected frequency (F6)"),
    QT_TRANSLATE_NOOP("Upstream", "Auto clock interval"),
    QT_TRANSLATE_NOOP("Upstream", "Run (F8)"),
    QT_TRANSLATE_NOOP("Upstream",
                      "Execute simulator without updating UI (fast "
                      "execution) (F8).\n Running will stop once the "
                      "program exits or a breakpoint is hit."),
    QT_TRANSLATE_NOOP("Upstream", "Show processor signal values"),
    QT_TRANSLATE_NOOP("Upstream", "Show pipeline diagram"),
    // src/edittab.cpp
    QT_TRANSLATE_NOOP("Upstream", "Show symbol navigator"),
    QT_TRANSLATE_NOOP("Upstream",
                      "Follow program execution.\nEnsures that the "
                      "instruction present in the\nfirst stage of the "
                      "processor is always visible\nin the disassembled "
                      "view."),
    QT_TRANSLATE_NOOP("Upstream", "Compile C program (Ctrl+B)"),
    QT_TRANSLATE_NOOP("Upstream", "Flat binary"),
    QT_TRANSLATE_NOOP("Upstream", "Executable (ELF)"),
    // src/iotab.cpp, src/cachesim
    QT_TRANSLATE_NOOP("Upstream", "Double-click to create"),
    QT_TRANSLATE_NOOP("Upstream", "Store cache preset"),
    QT_TRANSLATE_NOOP("Upstream", "Delete cache preset"),
    QT_TRANSLATE_NOOP("Upstream", "Cache Access Statistics"),
    QT_TRANSLATE_NOOP("Upstream", "Enable ratio crosshair"),
    QT_TRANSLATE_NOOP("Upstream", "Enable moving average crosshair"),
    QT_TRANSLATE_NOOP("Upstream", "Copy data to clipboard"),
    QT_TRANSLATE_NOOP("Upstream", "Save plot to file"),
};
} // namespace

QStringList upstreamSourceTexts() {
  QStringList texts;
  for (const char *t : kUpstreamTexts)
    texts << QString::fromUtf8(t);
  return texts;
}

} // namespace I18n
} // namespace Ripes
