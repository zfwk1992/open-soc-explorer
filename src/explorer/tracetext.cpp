// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "tracetext.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <vector>

namespace Ripes {
namespace Explorer {

namespace {

// The texts of the Ripes adapter are marked where they are written
// (modelprofile.cpp, tracerecorder.cpp), in the same context.
// Texts written by tools/gem5/gem5_to_osx.py (generated from that file;
// keep in sync when its texts change - tst_explorergui checks that a
// converted gem5 trace has no untranslated descriptions).
const char *const kGem5Texts[] = {
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "Converted offline from gem5 debug output; see "
                      "tools/gem5/gem5_to_osx.py for the mapping."),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "Initial register values are not part of gem5 Exec traces; "
        "register views show values only once they are written."),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "Destination registers of register.write events are decoded from "
        "the instruction word (ELF given) or taken from gem5's "
        "disassembly; only integer registers are reported."),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "Instruction completion times are inferred from the start of the "
        "next instruction (upper bound)."),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "Squash times of instructions that never committed are not "
        "recorded by O3PipeView and are inferred."),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "decoded and started executing (gem5 Exec record tick)"),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "completed at or before this tick: the next instruction started "
        "here (one instruction at a time; the interval includes the next "
        "instruction's fetch)"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "last instruction; completed at or before the end of the "
                      "simulation"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "CPU core model simulated by gem5."),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "Cache simulated by gem5 (classic memory system)."),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "Crossbar/bus simulated by gem5; carries the packets between the "
        "connected components."),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "Memory controller simulated by gem5 (request queues, "
                      "scheduling, DRAM timing)."),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "Memory device/interface simulated by gem5."),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "Hardware prefetcher simulated by gem5; it issues prefetch "
        "requests into the cache it belongs to."),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "instructions fetched per cycle"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "instructions decoded per cycle"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "instructions renamed per cycle"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "instructions dispatched per cycle"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "instructions issued per cycle"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "instructions committed per cycle"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "reorder buffer entries"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "instruction queue entries"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "load queue entries"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "store queue entries"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "physical integer registers"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "capacity in bytes"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "associativity (ways)"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "tag lookup latency in cycles"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "data access latency in cycles"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "latency in cycles"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "miss status holding registers (outstanding misses)"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "requests that can wait on one MSHR"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "clean lines are written back on eviction"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "bytes transferred per cycle"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "request front-end latency in cycles"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "forwarding latency in cycles"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "write queue entries"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "read queue entries"),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "scheduling policy (frfcfs: first-ready, first-come-first-served)"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "size of one DRAM device in bytes"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "ranks per channel"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "banks per rank"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "clock period in ps (gem5 ticks)"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "CAS latency in ps"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "RAS-to-CAS delay in ps"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "row precharge time in ps"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "address range (bytes)"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "prefetches issued per detected stride"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "how many strides ahead the first prefetch is"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "latency in cycles"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "entries of the stride detection table"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "prefetch queue entries"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText",
                      "trains only on cache misses"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "trains on reads"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "trains on writes"),
};

// Texts with numbers or names filled in (%1, %2, ...); the arguments
// are localized as well.
const char *const kTemplates[] = {
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "%1 register write(s) could not be attributed to a retiring "
        "instruction (e.g. system call side effects or manual edits)."),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "%1 retirement(s) were inferred from register writes because the "
        "model's retired-instruction counter did not count them (see "
        "docs/SIMULATION_FIDELITY.md)."),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "%1 – %2"),
    QT_TRANSLATE_NOOP("Ripes::Explorer::TraceText", "%1 (memory-mapped I/O)"),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "gem5 %1 in syscall-emulation mode with the configuration "
        "recorded in config.json (see the System view). gem5 is a "
        "cycle-level architectural simulator; this CPU model is not "
        "validated against a specific silicon implementation."),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "gem5 %1: atomic memory accesses, no pipeline or memory timing."),
    QT_TRANSLATE_NOOP(
        "Ripes::Explorer::TraceText",
        "gem5 clock domain %1 of %2: one cycle = %3 ticks (1 tick = 1 ps)"),
};

QString translate(const QString &text) {
  return QCoreApplication::translate(kTraceTextContext, text.toUtf8());
}

struct Template {
  QRegularExpression pattern;
  QString source;
  /// Placeholder number (1, 2, ...) of each capture group.
  std::vector<int> placeholders;
};

const std::vector<Template> &templates() {
  static const std::vector<Template> list = [] {
    std::vector<Template> result;
    static const QRegularExpression placeholder("%(\\d)");
    for (const char *t : kTemplates) {
      Template tpl;
      tpl.source = QString::fromUtf8(t);
      QString regex = "^";
      qsizetype pos = 0;
      auto it = placeholder.globalMatch(tpl.source);
      while (it.hasNext()) {
        const auto m = it.next();
        regex += QRegularExpression::escape(
                     tpl.source.mid(pos, m.capturedStart() - pos)) +
                 "(.+?)";
        tpl.placeholders.push_back(m.captured(1).toInt());
        pos = m.capturedEnd();
      }
      regex += QRegularExpression::escape(tpl.source.mid(pos)) + "$";
      tpl.pattern = QRegularExpression(
          regex, QRegularExpression::DotMatchesEverythingOption);
      result.push_back(tpl);
    }
    return result;
  }();
  return list;
}

} // namespace

QString localizedTraceText(const QString &text) {
  if (text.isEmpty())
    return text;
  const QString exact = translate(text);
  if (exact != text)
    return exact;
  for (const auto &tpl : templates()) {
    const auto m = tpl.pattern.match(text);
    if (!m.hasMatch())
      continue;
    // Arguments by placeholder number, each localized itself.
    std::vector<QString> args(tpl.placeholders.size() + 1);
    for (size_t i = 0; i < tpl.placeholders.size(); ++i) {
      const int n = tpl.placeholders[i];
      if (n >= 1 && static_cast<size_t>(n) < args.size())
        args[n] = localizedTraceText(m.captured(static_cast<int>(i) + 1));
    }
    QString result = translate(tpl.source);
    for (size_t n = args.size() - 1; n >= 1; --n)
      result.replace(QString("%%1").arg(n), args[n]);
    return result;
  }
  return text;
}

QStringList knownTraceTexts() {
  QStringList texts;
  for (const char *t : kGem5Texts)
    texts << QString::fromUtf8(t);
  for (const char *t : kTemplates)
    texts << QString::fromUtf8(t);
  return texts;
}

} // namespace Explorer
} // namespace Ripes
