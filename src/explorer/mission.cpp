// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "mission.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

#include "isa/rvisainfo_common.h"
#include "processorhandler.h"
#include "processorregistry.h"
#include "tracerecorder.h"

// Q_INIT_RESOURCE must be used outside of any namespace.
static void initMissionResources() { Q_INIT_RESOURCE(missions); }

namespace Ripes {
namespace Explorer {

namespace {

/// Translation context of the criterion descriptions.
struct Text {
  Q_DECLARE_TR_FUNCTIONS(Ripes::Explorer::Mission)
};

/// @p singular if @p n is 1, else @p plural, with %1 replaced by @p n.
/// (Two source texts instead of Qt's numerus forms, so that the English
/// texts need no translation catalogue.)
QString counted(qint64 n, const char *singular, const char *plural) {
  return Text::tr(n == 1 ? singular : plural).arg(n);
}

const QStringList &kinds() {
  static const QStringList k = {"processor_is",
                                "program_loaded",
                                "program_matches_example",
                                "source_differs_from_example",
                                "cycle_at_least",
                                "finished",
                                "trace_complete",
                                "register_equals",
                                "register_written_by",
                                "retired_at_least",
                                "retired_mnemonic_at_least",
                                "retired_branches_at_least",
                                "stall_cycles_at_least",
                                "stall_cycles_at_most",
                                "squashed_at_least",
                                "squashed_at_most",
                                "cycles_at_most",
                                "cpi_at_most",
                                "memory_reads_at_least",
                                "dcache_misses_at_least",
                                "dcache_hit_rate_at_least"};
  return k;
}

std::optional<int> resolveRegister(const QString &name, const MissionState &s) {
  const QString n = name.trimmed().toLower();
  for (int i = 0; i < s.gprNames.size(); ++i)
    if (s.gprNames[i].toLower() == n || s.gprAliases.value(i).toLower() == n)
      return i;
  if (n == "fp")
    return 8;
  return std::nullopt;
}

QString displayRegister(int idx, const MissionState &s) {
  const QString name = s.gprNames.value(idx, QString("x%1").arg(idx));
  const QString alias = s.gprAliases.value(idx);
  return alias.isEmpty() || alias == name ? name
                                          : QString("%1 (%2)").arg(alias, name);
}

std::optional<qint64> parseValue(const QVariant &v) {
  if (v.typeId() == QMetaType::QString) {
    const QString str = v.toString().trimmed();
    if (str.startsWith("0x") || str.startsWith("0X")) {
      if (auto u = Trace::fromHex(str))
        return static_cast<qint64>(*u);
      return std::nullopt;
    }
    bool ok = false;
    const qint64 val = str.toLongLong(&ok);
    return ok ? std::optional<qint64>(val) : std::nullopt;
  }
  bool ok = false;
  const qint64 val = v.toLongLong(&ok);
  return ok ? std::optional<qint64>(val) : std::nullopt;
}

QString normalizeProgram(const QString &program) {
  QStringList out;
  for (QString line : program.split('\n')) {
    const int comment = line.indexOf('#');
    if (comment >= 0)
      line.truncate(comment);
    line = line.simplified();
    line.replace(QRegularExpression("\\s*,\\s*"), ",");
    if (!line.isEmpty())
      out << line;
  }
  return out.join('\n');
}

bool runComplete(const MissionState &s) {
  return s.finished && s.traceStartsAtReset && !s.traceTruncated;
}

std::vector<MissionCriterion> parseChecks(const QJsonValue &v, QString *error,
                                          bool &ok) {
  std::vector<MissionCriterion> checks;
  for (const auto &cv : v.toArray()) {
    MissionCriterion c;
    QVariantMap params = cv.toObject().toVariantMap();
    c.kind = params.take("kind").toString();
    c.params = params;
    if (!kinds().contains(c.kind)) {
      if (error)
        *error = "Unknown criterion kind '" + c.kind + "'";
      ok = false;
      return {};
    }
    checks.push_back(c);
  }
  return checks;
}

} // namespace

QStringList criterionKinds() { return kinds(); }

bool sameProgram(const QString &a, const QString &b) {
  return normalizeProgram(a) == normalizeProgram(b);
}

std::optional<Mission> parseMission(const QJsonObject &o, QString *error,
                                    const QString &language) {
  auto fail = [&](const QString &msg) -> std::optional<Mission> {
    if (error)
      *error = msg;
    return std::nullopt;
  };
  for (const char *key : {"id", "title", "objective", "example", "processor"})
    if (!o[key].isString() || o[key].toString().isEmpty())
      return fail(QString("Mission is missing required field '%1'").arg(key));
  if (!o["steps"].isArray() || !o["challenge"].isObject())
    return fail("Mission requires 'steps' and 'challenge'");

  // Texts of @p language ("i18n" object), falling back to English.
  const QJsonObject loc = language.isEmpty()
                              ? QJsonObject()
                              : o["i18n"].toObject()[language].toObject();
  auto text = [](const QJsonObject &english, const QJsonObject &translated,
                 const char *key) {
    const QString t = translated[key].toString();
    return t.isEmpty() ? english[key].toString() : t;
  };

  Mission m;
  m.id = o["id"].toString();
  m.number = o["number"].toInt();
  m.title = text(o, loc, "title");
  const auto learn =
      loc["learn"].toArray().size() == o["learn"].toArray().size()
          ? loc["learn"].toArray()
          : o["learn"].toArray();
  for (const auto &l : learn)
    m.learn << l.toString();
  m.processor = o["processor"].toString();
  m.objective = text(o, loc, "objective");
  m.explanation = text(o, loc, "explanation");
  m.example = o["example"].toString();
  m.completion = text(o, loc, "completion");

  bool ok = true;
  const auto trSteps = loc["steps"].toArray();
  for (const auto &sv : o["steps"].toArray()) {
    const auto so = sv.toObject();
    const auto trStep = trSteps.at(int(m.steps.size())).toObject();
    MissionStep step;
    step.title = text(so, trStep, "title");
    step.instruction = text(so, trStep, "instruction");
    step.checks = parseChecks(so["checks"], error, ok);
    if (!ok)
      return std::nullopt;
    m.steps.push_back(step);
  }
  const auto ch = o["challenge"].toObject();
  const auto trCh = loc["challenge"].toObject();
  m.challenge = text(ch, trCh, "text");
  m.hint = text(ch, trCh, "hint");
  m.referenceSolution = ch["reference_solution"].toString();
  m.challengeChecks = parseChecks(ch["checks"], error, ok);
  if (!ok)
    return std::nullopt;
  if (m.challengeChecks.empty())
    return fail("Mission '" + m.id + "' has no challenge checks");

  if (o.contains("data_cache")) {
    const auto dc = o["data_cache"].toObject();
    CacheGeometry g;
    g.blocks = dc["blocks"].toInt(-1);
    g.lines = dc["lines"].toInt(-1);
    g.ways = dc["ways"].toInt(-1);
    g.writeAllocate = dc["write_allocate"].toBool(true);
    if (g.blocks < 0 || g.lines < 0 || g.ways < 0 || g.blocks > 8 ||
        g.lines > 16 || g.ways > 8)
      return fail("Mission '" + m.id +
                  "': data_cache needs blocks, lines and ways (log2 values)");
    m.dataCache = g;
  }
  auto needsCache = [](const std::vector<MissionCriterion> &checks) {
    return std::any_of(checks.begin(), checks.end(), [](const auto &c) {
      return c.kind.startsWith("dcache_");
    });
  };
  bool usesCache = needsCache(m.challengeChecks);
  for (const auto &step : m.steps)
    usesCache |= needsCache(step.checks);
  if (usesCache && !m.dataCache)
    return fail("Mission '" + m.id +
                "' uses dcache_* criteria without a data_cache");
  return m;
}

QStringList missingTranslations(const QJsonObject &o, const QString &language) {
  QStringList missing;
  const QJsonObject loc = o["i18n"].toObject()[language].toObject();
  auto check = [&](const QJsonObject &english, const QJsonObject &translated,
                   const char *key, const QString &path) {
    if (!english[key].toString().isEmpty() &&
        translated[key].toString().trimmed().isEmpty())
      missing << path;
  };
  for (const char *key : {"title", "objective", "explanation", "completion"})
    check(o, loc, key, key);
  const auto learn = o["learn"].toArray();
  const auto trLearn = loc["learn"].toArray();
  if (trLearn.size() != learn.size())
    missing << "learn";
  for (int i = 0; i < trLearn.size(); ++i)
    if (trLearn[i].toString().trimmed().isEmpty())
      missing << QString("learn[%1]").arg(i);
  const auto steps = o["steps"].toArray();
  const auto trSteps = loc["steps"].toArray();
  if (trSteps.size() != steps.size())
    missing << "steps";
  for (int i = 0; i < steps.size(); ++i) {
    const auto english = steps[i].toObject();
    const auto translated = trSteps.at(i).toObject();
    check(english, translated, "title", QString("steps[%1].title").arg(i));
    check(english, translated, "instruction",
          QString("steps[%1].instruction").arg(i));
  }
  const auto ch = o["challenge"].toObject();
  const auto trCh = loc["challenge"].toObject();
  check(ch, trCh, "text", "challenge.text");
  check(ch, trCh, "hint", "challenge.hint");
  return missing;
}

std::vector<Mission> loadBuiltinMissions(QString *error,
                                         const QString &language) {
  initMissionResources();
  std::vector<Mission> missions;
  QDir dir(":/missions");
  for (const auto &file : dir.entryList({"*.json"}, QDir::Files, QDir::Name)) {
    QFile f(dir.filePath(file));
    if (!f.open(QIODevice::ReadOnly))
      continue;
    QJsonParseError perr;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError) {
      if (error)
        *error = file + ": " + perr.errorString();
      continue;
    }
    QString err;
    if (auto m = parseMission(doc.object(), &err, language))
      missions.push_back(*m);
    else if (error)
      *error = file + ": " + err;
  }
  std::sort(
      missions.begin(), missions.end(),
      [](const Mission &a, const Mission &b) { return a.number < b.number; });
  return missions;
}

MissionState captureMissionState(const Trace::Trace &trace,
                                 const QString &source) {
  MissionState s;
  s.processor = enumToString<ProcessorID>(ProcessorHandler::getID());
  s.programLoaded = ProcessorHandler::getProgram() != nullptr;
  s.source = source;
  const auto *proc = ProcessorHandler::getProcessor();
  s.cycle = proc->getCycleCount();
  s.finished = s.programLoaded && proc->finished();
  const auto isa = ProcessorHandler::currentISA();
  s.xlen = isa->bits();
  if (auto info = isa->regInfo(RVISA::GPR)) {
    for (unsigned i = 0; i < (*info)->regCnt(); ++i) {
      s.gpr.push_back(proc->getRegister(RVISA::GPR, i));
      s.gprNames << (*info)->regName(i);
      s.gprAliases << (*info)->regAlias(i);
    }
  }

  // Only trust the trace if it describes the current program and processor.
  const bool traceMatches =
      trace.header.source.model == s.processor &&
      (!s.programLoaded ||
       trace.header.programHash ==
           programImageHash(*ProcessorHandler::getProgram()));
  if (traceMatches) {
    TraceReplay replay(trace);
    s.stats = replay.stats();
    s.traceStartsAtReset = trace.completeness.startsAtReset;
    s.traceTruncated = trace.completeness.truncated;
    for (const auto &ev : replay.trace().events) {
      if (ev.type == Trace::EventType::RegisterWrite &&
          ev.payload.value("regfile").toString() == "gpr")
        s.lastWriter[ev.payload.value("index").toInt()] =
            ev.instance >= 0 ? replay.mnemonicOf(ev.instance) : QString();
      if (ev.type == Trace::EventType::MemoryRequest)
        if (const auto address =
                Trace::fromHex(ev.payload.value("address").toString()))
          s.dataAccesses.push_back(
              {*address, ev.payload.value("access").toString() == "write"});
    }
  } else {
    s.traceTruncated = true;
  }
  return s;
}

CriterionResult evaluateCriterion(const MissionCriterion &c,
                                  const MissionState &s,
                                  const Mission &mission) {
  CriterionResult r;
  const auto &p = c.params;
  const auto count = p.value("count").toLongLong();
  const QString needRun =
      Text::tr("run the program to completion (from reset) to measure this");
  auto measured = [](const QString &value) {
    return Text::tr("measured: %1").arg(value);
  };

  if (c.kind == "processor_is") {
    QStringList ids = p.value("any_of").toStringList();
    if (ids.isEmpty())
      ids << mission.processor;
    QStringList names;
    for (const auto &id : ids) {
      bool ok = false;
      const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
          id.toUtf8().constData(), &ok);
      names << (ok ? ProcessorRegistry::getDescription(
                         static_cast<ProcessorID>(v))
                             .name +
                         " (" + id + ")"
                   : id);
    }
    r.description = Text::tr("Selected processor: %1")
                        .arg(names.join(" " + Text::tr("or") + " "));
    r.passed = ids.contains(s.processor);
    r.detail = Text::tr("current: %1").arg(s.processor);
  } else if (c.kind == "program_loaded") {
    r.description = Text::tr("A program is assembled and loaded");
    r.passed = s.programLoaded;
  } else if (c.kind == "program_matches_example") {
    r.description =
        Text::tr("The editor contains the mission's example program");
    r.passed = s.programLoaded && sameProgram(s.source, mission.example);
  } else if (c.kind == "source_differs_from_example") {
    r.description = Text::tr("The program differs from the example");
    r.passed = s.programLoaded && !sameProgram(s.source, mission.example);
  } else if (c.kind == "cycle_at_least") {
    const auto n = p.value("cycles").toLongLong();
    r.description =
        counted(n,
                QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                  "Executed at least %1 clock cycle"),
                QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                  "Executed at least %1 clock cycles"));
    r.passed = s.programLoaded && s.cycle >= n;
    r.detail = Text::tr("current cycle: %1").arg(s.cycle);
  } else if (c.kind == "finished") {
    r.description = Text::tr("The program ran to completion");
    r.passed = s.finished;
    r.detail = s.finished ? Text::tr("finished") : Text::tr("not finished");
  } else if (c.kind == "trace_complete") {
    r.description = Text::tr("The recorded trace covers the whole run (from "
                             "reset, not truncated)");
    r.passed = runComplete(s);
    r.detail = s.traceTruncated ? Text::tr("trace incomplete or truncated")
                                : (s.finished ? Text::tr("complete")
                                              : Text::tr("still running"));
  } else if (c.kind == "register_equals") {
    const auto reg = resolveRegister(p.value("register").toString(), s);
    const auto expected = parseValue(p.value("value"));
    if (!reg.has_value() || !expected.has_value()) {
      r.description = Text::tr("Invalid register_equals criterion");
      return r;
    }
    r.description =
        QString("%1 = %2").arg(displayRegister(*reg, s)).arg(*expected);
    if (static_cast<size_t>(*reg) < s.gpr.size()) {
      const quint64 m = s.xlen >= 64 ? ~quint64(0) : (quint64(1) << s.xlen) - 1;
      const quint64 actual = s.gpr[*reg] & m;
      r.passed =
          s.programLoaded && actual == (static_cast<quint64>(*expected) & m);
      const quint64 signBit = quint64(1) << (s.xlen - 1);
      r.detail = Text::tr("current: %1")
                     .arg(static_cast<qint64>((actual ^ signBit) - signBit));
    }
  } else if (c.kind == "register_written_by") {
    const auto reg = resolveRegister(p.value("register").toString(), s);
    const QString mnemonic = p.value("mnemonic").toString().toLower();
    if (!reg.has_value()) {
      r.description = Text::tr("Invalid register_written_by criterion");
      return r;
    }
    r.description = Text::tr("The final value of %1 was produced by a '%2' "
                             "instruction")
                        .arg(displayRegister(*reg, s), mnemonic);
    auto it = s.lastWriter.find(*reg);
    r.passed =
        runComplete(s) && it != s.lastWriter.end() && it->second == mnemonic;
    r.detail = it == s.lastWriter.end()
                   ? Text::tr("never written")
                   : Text::tr("last written by: %1")
                         .arg(it->second.isEmpty() ? Text::tr("(unknown)")
                                                   : it->second);
  } else if (c.kind == "retired_at_least") {
    r.description =
        counted(count,
                QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                  "At least %1 instruction executed"),
                QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                  "At least %1 instructions executed"));
    r.passed = s.stats.retired >= static_cast<quint64>(count);
    r.detail = measured(QString::number(s.stats.retired));
  } else if (c.kind == "retired_mnemonic_at_least") {
    const QString mn = p.value("mnemonic").toString().toLower();
    const auto it = s.stats.retiredMnemonics.find(mn);
    const quint64 n = it == s.stats.retiredMnemonics.end() ? 0 : it->second;
    r.description = (count == 1 ? Text::tr("At least %1 '%2' instruction "
                                           "executed")
                                : Text::tr("At least %1 '%2' instructions "
                                           "executed"))
                        .arg(count)
                        .arg(mn);
    r.passed = n >= static_cast<quint64>(count);
    r.detail = measured(QString::number(n));
  } else if (c.kind == "retired_branches_at_least") {
    r.description =
        counted(count,
                QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                  "At least %1 conditional branch executed"),
                QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                  "At least %1 conditional branches executed"));
    r.passed = s.stats.retiredBranches >= static_cast<quint64>(count);
    r.detail = measured(QString::number(s.stats.retiredBranches));
  } else if (c.kind == "stall_cycles_at_least") {
    r.description =
        counted(count,
                QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                  "At least %1 stall cycle observed"),
                QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                  "At least %1 stall cycles observed"));
    r.passed = s.stats.stallCycles >= static_cast<quint64>(count);
    r.detail = measured(QString::number(s.stats.stallCycles));
  } else if (c.kind == "stall_cycles_at_most") {
    r.description = counted(
        count,
        QT_TRANSLATE_NOOP("Ripes::Explorer::Mission", "At most %1 stall cycle"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                          "At most %1 stall cycles"));
    r.passed =
        runComplete(s) && s.stats.stallCycles <= static_cast<quint64>(count);
    r.detail = runComplete(s) ? measured(QString::number(s.stats.stallCycles))
                              : needRun;
  } else if (c.kind == "squashed_at_least") {
    r.description = counted(
        count,
        QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                          "At least %1 squashed (flushed) instruction"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                          "At least %1 squashed (flushed) instructions"));
    r.passed = s.stats.squashed >= static_cast<quint64>(count);
    r.detail = measured(QString::number(s.stats.squashed));
  } else if (c.kind == "squashed_at_most") {
    r.description = counted(
        count,
        QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                          "At most %1 squashed (flushed) instruction"),
        QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                          "At most %1 squashed (flushed) instructions"));
    r.passed =
        runComplete(s) && s.stats.squashed <= static_cast<quint64>(count);
    r.detail =
        runComplete(s) ? measured(QString::number(s.stats.squashed)) : needRun;
  } else if (c.kind == "cycles_at_most") {
    const auto n = p.value("cycles").toLongLong();
    r.description = counted(n,
                            QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                              "Completed within %1 cycle"),
                            QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                              "Completed within %1 cycles"));
    r.passed = runComplete(s) && s.stats.cycles <= n;
    r.detail =
        runComplete(s) ? measured(QString::number(s.stats.cycles)) : needRun;
  } else if (c.kind == "cpi_at_most") {
    const double v = p.value("value").toDouble();
    r.description = QString("CPI ≤ %1").arg(v, 0, 'f', 2);
    r.passed = runComplete(s) && s.stats.retired > 0 && s.stats.cpi <= v;
    r.detail = runComplete(s) ? measured(QString::number(s.stats.cpi, 'f', 2))
                              : needRun;
  } else if (c.kind == "memory_reads_at_least") {
    r.description = counted(count,
                            QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                              "At least %1 data memory read"),
                            QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                              "At least %1 data memory reads"));
    r.passed = runComplete(s) && s.stats.memoryReads >= quint64(count);
    r.detail = runComplete(s) ? measured(QString::number(s.stats.memoryReads))
                              : needRun;
  } else if (c.kind == "dcache_misses_at_least" ||
             c.kind == "dcache_hit_rate_at_least") {
    const unsigned wordBytes = s.xlen / 8;
    const auto counts =
        mission.dataCache
            ? simulateCache(*mission.dataCache, s.dataAccesses, wordBytes)
            : CacheCounts();
    if (c.kind == "dcache_misses_at_least") {
      r.description =
          counted(count,
                  QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                    "At least %1 data cache miss"),
                  QT_TRANSLATE_NOOP("Ripes::Explorer::Mission",
                                    "At least %1 data cache misses"));
      r.passed = runComplete(s) && counts.misses >= quint64(count);
    } else {
      const double percent = p.value("percent").toDouble();
      r.description = Text::tr("Data cache hit rate ≥ %1 %").arg(percent);
      r.passed = runComplete(s) && counts.accesses() > 0 &&
                 counts.hitRatePercent() >= percent;
    }
    if (mission.dataCache)
      r.description += " (" + mission.dataCache->describe(wordBytes) + ")";
    r.detail = runComplete(s)
                   ? Text::tr("measured: %1 hits, %2 misses, hit rate %3 %")
                         .arg(counts.hits)
                         .arg(counts.misses)
                         .arg(counts.hitRatePercent(), 0, 'f', 1)
                   : needRun;
  } else {
    r.description = Text::tr("Unknown criterion '%1'").arg(c.kind);
  }
  if (p.contains("label"))
    r.description = p.value("label").toString();
  return r;
}

CheckReport evaluateChecks(const std::vector<MissionCriterion> &checks,
                           const MissionState &s, const Mission &mission) {
  CheckReport report;
  report.passed = !checks.empty();
  for (const auto &c : checks) {
    report.results.push_back(evaluateCriterion(c, s, mission));
    report.passed &= report.results.back().passed;
  }
  return report;
}

} // namespace Explorer
} // namespace Ripes
