// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "mission.h"

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

QString plural(qint64 n, const QString &word) {
  return QString("%1 %2%3").arg(n).arg(word).arg(n == 1 ? "" : "s");
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

std::optional<Mission> parseMission(const QJsonObject &o, QString *error) {
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

  Mission m;
  m.id = o["id"].toString();
  m.number = o["number"].toInt();
  m.title = o["title"].toString();
  for (const auto &l : o["learn"].toArray())
    m.learn << l.toString();
  m.processor = o["processor"].toString();
  m.objective = o["objective"].toString();
  m.explanation = o["explanation"].toString();
  m.example = o["example"].toString();
  m.completion = o["completion"].toString();

  bool ok = true;
  for (const auto &sv : o["steps"].toArray()) {
    const auto so = sv.toObject();
    MissionStep step;
    step.title = so["title"].toString();
    step.instruction = so["instruction"].toString();
    step.checks = parseChecks(so["checks"], error, ok);
    if (!ok)
      return std::nullopt;
    m.steps.push_back(step);
  }
  const auto ch = o["challenge"].toObject();
  m.challenge = ch["text"].toString();
  m.hint = ch["hint"].toString();
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

std::vector<Mission> loadBuiltinMissions(QString *error) {
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
    if (auto m = parseMission(doc.object(), &err))
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
      "run the program to completion (from reset) to measure this";

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
    r.description = "Selected processor: " + names.join(" or ");
    r.passed = ids.contains(s.processor);
    r.detail = "current: " + s.processor;
  } else if (c.kind == "program_loaded") {
    r.description = "A program is assembled and loaded";
    r.passed = s.programLoaded;
  } else if (c.kind == "program_matches_example") {
    r.description = "The editor contains the mission's example program";
    r.passed = s.programLoaded && sameProgram(s.source, mission.example);
  } else if (c.kind == "source_differs_from_example") {
    r.description = "The program differs from the example";
    r.passed = s.programLoaded && !sameProgram(s.source, mission.example);
  } else if (c.kind == "cycle_at_least") {
    const auto n = p.value("cycles").toLongLong();
    r.description = "Executed at least " + plural(n, "clock cycle");
    r.passed = s.programLoaded && s.cycle >= n;
    r.detail = QString("current cycle: %1").arg(s.cycle);
  } else if (c.kind == "finished") {
    r.description = "The program ran to completion";
    r.passed = s.finished;
    r.detail = s.finished ? "finished" : "not finished";
  } else if (c.kind == "trace_complete") {
    r.description = "The recorded trace covers the whole run (from reset, not "
                    "truncated)";
    r.passed = runComplete(s);
    r.detail = s.traceTruncated ? "trace incomplete or truncated"
                                : (s.finished ? "complete" : "still running");
  } else if (c.kind == "register_equals") {
    const auto reg = resolveRegister(p.value("register").toString(), s);
    const auto expected = parseValue(p.value("value"));
    if (!reg.has_value() || !expected.has_value()) {
      r.description = "Invalid register_equals criterion";
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
      r.detail = QString("current: %1")
                     .arg(static_cast<qint64>((actual ^ signBit) - signBit));
    }
  } else if (c.kind == "register_written_by") {
    const auto reg = resolveRegister(p.value("register").toString(), s);
    const QString mnemonic = p.value("mnemonic").toString().toLower();
    if (!reg.has_value()) {
      r.description = "Invalid register_written_by criterion";
      return r;
    }
    r.description = QString("The final value of %1 was produced by a '%2' "
                            "instruction")
                        .arg(displayRegister(*reg, s), mnemonic);
    auto it = s.lastWriter.find(*reg);
    r.passed =
        runComplete(s) && it != s.lastWriter.end() && it->second == mnemonic;
    r.detail =
        it == s.lastWriter.end()
            ? "never written"
            : "last written by: " +
                  (it->second.isEmpty() ? QString("(unknown)") : it->second);
  } else if (c.kind == "retired_at_least") {
    r.description = "At least " + plural(count, "instruction") + " executed";
    r.passed = s.stats.retired >= static_cast<quint64>(count);
    r.detail = QString("measured: %1").arg(s.stats.retired);
  } else if (c.kind == "retired_mnemonic_at_least") {
    const QString mn = p.value("mnemonic").toString().toLower();
    const auto it = s.stats.retiredMnemonics.find(mn);
    const quint64 n = it == s.stats.retiredMnemonics.end() ? 0 : it->second;
    r.description = QString("At least %1 '%2' instruction%3 executed")
                        .arg(count)
                        .arg(mn)
                        .arg(count == 1 ? "" : "s");
    r.passed = n >= static_cast<quint64>(count);
    r.detail = QString("measured: %1").arg(n);
  } else if (c.kind == "retired_branches_at_least") {
    r.description =
        "At least " + plural(count, "conditional branch") + " executed";
    r.description.replace("branchs", "branches");
    r.passed = s.stats.retiredBranches >= static_cast<quint64>(count);
    r.detail = QString("measured: %1").arg(s.stats.retiredBranches);
  } else if (c.kind == "stall_cycles_at_least") {
    r.description = "At least " + plural(count, "stall cycle") + " observed";
    r.passed = s.stats.stallCycles >= static_cast<quint64>(count);
    r.detail = QString("measured: %1").arg(s.stats.stallCycles);
  } else if (c.kind == "stall_cycles_at_most") {
    r.description = "At most " + plural(count, "stall cycle");
    r.passed =
        runComplete(s) && s.stats.stallCycles <= static_cast<quint64>(count);
    r.detail = runComplete(s) ? QString("measured: %1").arg(s.stats.stallCycles)
                              : needRun;
  } else if (c.kind == "squashed_at_least") {
    r.description =
        "At least " + plural(count, "squashed (flushed) instruction");
    r.passed = s.stats.squashed >= static_cast<quint64>(count);
    r.detail = QString("measured: %1").arg(s.stats.squashed);
  } else if (c.kind == "squashed_at_most") {
    r.description =
        "At most " + plural(count, "squashed (flushed) instruction");
    r.passed =
        runComplete(s) && s.stats.squashed <= static_cast<quint64>(count);
    r.detail = runComplete(s) ? QString("measured: %1").arg(s.stats.squashed)
                              : needRun;
  } else if (c.kind == "cycles_at_most") {
    const auto n = p.value("cycles").toLongLong();
    r.description = "Completed within " + plural(n, "cycle");
    r.passed = runComplete(s) && s.stats.cycles <= n;
    r.detail =
        runComplete(s) ? QString("measured: %1").arg(s.stats.cycles) : needRun;
  } else if (c.kind == "cpi_at_most") {
    const double v = p.value("value").toDouble();
    r.description = QString("CPI ≤ %1").arg(v, 0, 'f', 2);
    r.passed = runComplete(s) && s.stats.retired > 0 && s.stats.cpi <= v;
    r.detail = runComplete(s)
                   ? QString("measured: %1").arg(s.stats.cpi, 0, 'f', 2)
                   : needRun;
  } else if (c.kind == "memory_reads_at_least") {
    r.description = "At least " + plural(count, "data memory read");
    r.passed = runComplete(s) && s.stats.memoryReads >= quint64(count);
    r.detail = runComplete(s) ? QString("measured: %1").arg(s.stats.memoryReads)
                              : needRun;
  } else if (c.kind == "dcache_misses_at_least" ||
             c.kind == "dcache_hit_rate_at_least") {
    const unsigned wordBytes = s.xlen / 8;
    const auto counts =
        mission.dataCache
            ? simulateCache(*mission.dataCache, s.dataAccesses, wordBytes)
            : CacheCounts();
    if (c.kind == "dcache_misses_at_least") {
      r.description = QString("At least %1 data cache miss%2")
                          .arg(count)
                          .arg(count == 1 ? "" : "es");
      r.passed = runComplete(s) && counts.misses >= quint64(count);
    } else {
      const double percent = p.value("percent").toDouble();
      r.description = QString("Data cache hit rate ≥ %1 %").arg(percent);
      r.passed = runComplete(s) && counts.accesses() > 0 &&
                 counts.hitRatePercent() >= percent;
    }
    if (mission.dataCache)
      r.description += " (" + mission.dataCache->describe(wordBytes) + ")";
    r.detail = runComplete(s)
                   ? QString("measured: %1 hits, %2 misses, hit rate %3 %")
                         .arg(counts.hits)
                         .arg(counts.misses)
                         .arg(counts.hitRatePercent(), 0, 'f', 1)
                   : needRun;
  } else {
    r.description = "Unknown criterion '" + c.kind + "'";
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
