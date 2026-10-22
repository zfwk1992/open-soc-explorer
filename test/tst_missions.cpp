// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: tests for the learning missions, the instruction
// explainer and the component knowledge base.
#include <QFileInfo>
#include <QtTest/QTest>

#include "cachesim/cachesim.h"
#include "cachesim/l1cacheshim.h"
#include "explorer/cachemodel.h"
#include "explorer/componentinfo.h"
#include "explorer/instructionexplainer.h"
#include "explorer/mission.h"
#include "explorer/missionprogress.h"
#include "explorer/modelprofile.h"
#include "explorer/tracerecorder.h"
#include "explorer/tracereplay.h"
#include "processorhandler.h"
#include "processorregistry.h"
#include "programloader.h"
#include "ripessettings.h"

using namespace Ripes;
using namespace Ripes::Explorer;

class tst_missions : public QObject {
  Q_OBJECT

private:
  /// Selects @p processor, loads @p program and runs it to completion while
  /// recording a trace. Returns the mission state at the end.
  MissionState run(const QString &processor, const QString &program,
                   int maxCycles = 5000);
  const Mission &mission(int number) const;
  static ProcessorID processorId(const QString &name);
  static QString failures(const CheckReport &report);

  ProgramLoader *m_loader = nullptr;
  TraceRecorder *m_recorder = nullptr;
  std::vector<Mission> m_missions;

private slots:
  void initTestCase();
  void tst_catalog();
  void tst_sourceReferencesExist();
  void tst_mission1();
  void tst_mission2();
  void tst_mission3();
  void tst_mission4();
  void tst_cacheModel();
  void tst_cacheModelMatchesRipes();
  void tst_missionStepsProgressLive();
  void tst_progressScoring();
  void tst_progressFromVerifiedChecks();
  void tst_explainerFirstProgram();
  void tst_explainerLoadUseHazard();
  void tst_explainerDetectsUnresolvedHazard();
  void tst_explainerSquashedInstruction();
};

void tst_missions::initTestCase() {
  m_loader = new ProgramLoader();
  m_recorder = new TraceRecorder(this);
  QString error;
  m_missions = loadBuiltinMissions(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
}

ProcessorID tst_missions::processorId(const QString &name) {
  bool ok = false;
  const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
      name.toUtf8().constData(), &ok);
  Q_ASSERT(ok);
  return static_cast<ProcessorID>(v);
}

MissionState tst_missions::run(const QString &processor, const QString &program,
                               int maxCycles) {
  const auto id = processorId(processor);
  ProcessorHandler::selectProcessor(
      id, ProcessorRegistry::getDescription(id).isaInfo().defaultExtensions,
      ProcessorRegistry::getDescription(id).defaultRegisterVals);
  RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
  m_loader->loadTest(program);
  auto *proc = ProcessorHandler::getProcessorNonConst();
  while (!proc->finished() && proc->getCycleCount() < maxCycles)
    proc->clock();
  return captureMissionState(m_recorder->trace(), program);
}

const Mission &tst_missions::mission(int number) const {
  for (const auto &m : m_missions)
    if (m.number == number)
      return m;
  Q_ASSERT(false);
  return m_missions.front();
}

QString tst_missions::failures(const CheckReport &report) {
  QStringList failed;
  for (const auto &r : report.results)
    if (!r.passed)
      failed << r.description + " [" + r.detail + "]";
  return failed.join("; ");
}

void tst_missions::tst_catalog() {
  QCOMPARE(m_missions.size(), size_t(4));
  for (int i = 0; i < 4; ++i) {
    const auto &m = m_missions[i];
    QCOMPARE(m.number, i + 1);
    QVERIFY(!m.title.isEmpty());
    QVERIFY(!m.objective.isEmpty());
    QVERIFY(!m.explanation.isEmpty());
    QVERIFY(!m.learn.isEmpty());
    QVERIFY(m.steps.size() >= 3);
    QVERIFY(!m.challenge.isEmpty());
    QVERIFY(!m.hint.isEmpty());
    QVERIFY(!m.completion.isEmpty());
    QVERIFY(!m.referenceSolution.isEmpty());
    processorId(m.processor); // asserts on unknown processors
  }
  QString error;
  QVERIFY(!parseMission(QJsonObject{{"id", "x"}}, &error).has_value());
  QVERIFY(!error.isEmpty());
}

void tst_missions::tst_sourceReferencesExist() {
  // Every source location shown to users must exist in this checkout.
  QStringList refs;
  for (int id = 0; id < ProcessorID::NUM_PROCESSORS; ++id)
    refs << modelProfile(static_cast<ProcessorID>(id)).sourceFile;
  for (const auto &name : describedComponents())
    refs << componentInfo(name)->sourceFile;
  refs << "src/processors/RISC-V/rv_decode.h"
       << "src/processors/RISC-V/rv_control.h"
       << "src/processors/RISC-V/rv_immediate.h"
       << "src/processors/RISC-V/rv_memory.h"
       << "src/processors/RISC-V/rv_branch.h"
       << "src/processors/RISC-V/rv_alu.h"
       << "src/isa/rv_i_ext.cpp"
       << "src/isa/rv_m_ext.cpp"
       << "src/explorer/riscvdecoder.cpp"
       << "src/processors/interface/ripesprocessor.h";
  for (const auto &ref : refs) {
    QVERIFY2(!ref.isEmpty(), "empty source reference");
    QVERIFY2(QFileInfo::exists(QString(OSX_SOURCE_DIR) + "/" + ref),
             qPrintable("missing source file: " + ref));
  }
}

void tst_missions::tst_mission1() {
  const auto &m = mission(1);
  auto state = run(m.processor, m.example);
  for (const auto &step : m.steps) {
    const auto report = evaluateChecks(step.checks, state, m);
    QVERIFY2(report.passed, qPrintable(step.title + ": " + failures(report)));
  }
  // The example itself does not solve the challenge.
  QVERIFY(!evaluateChecks(m.challengeChecks, state, m).passed);

  // x5 = 100 produced by addi instead of add is rejected.
  state = run(m.processor, "addi x5, x0, 100\nadd x6, x5, x0");
  auto report = evaluateChecks(m.challengeChecks, state, m);
  QVERIFY(!report.passed);
  QVERIFY(failures(report).contains("last written by: addi"));

  // The reference solution passes, on any processor.
  for (const QString proc : {"RV32_SS", "RV32_5S"}) {
    state = run(proc, m.referenceSolution);
    report = evaluateChecks(m.challengeChecks, state, m);
    QVERIFY2(report.passed, qPrintable(proc + ": " + failures(report)));
  }
}

void tst_missions::tst_mission2() {
  const auto &m = mission(2);
  auto state = run(m.processor, m.example);
  QVERIFY(evaluateChecks(m.steps[0].checks, state, m).passed);
  auto report = evaluateChecks(m.steps[1].checks, state, m);
  QVERIFY2(report.passed, qPrintable(failures(report)));
  QCOMPARE(state.stats.stallCycles, quint64(1));
  const double cpiForwarding = state.stats.cpi;

  // Challenge not solved by the example: one load-use stall remains.
  report = evaluateChecks(m.challengeChecks, state, m);
  QVERIFY(!report.passed);
  QVERIFY(failures(report).contains("measured: 1"));

  // Step 3: without forwarding the same program stalls more.
  state = run("RV32_5S_NO_FW", m.example);
  report = evaluateChecks(m.steps[2].checks, state, m);
  QVERIFY2(report.passed, qPrintable(failures(report)));
  QVERIFY(state.stats.cpi > cpiForwarding);
  qInfo() << "Mission 2: stall cycles without forwarding:"
          << state.stats.stallCycles << "CPI" << state.stats.cpi
          << "vs CPI with forwarding" << cpiForwarding;

  // Reference solution: same results, no stall.
  state = run(m.processor, m.referenceSolution);
  report = evaluateChecks(m.challengeChecks, state, m);
  QVERIFY2(report.passed, qPrintable(failures(report)));
  QCOMPARE(state.stats.stallCycles, quint64(0));

  // The reference solution does not count on another processor.
  state = run("RV32_5S_NO_FW", m.referenceSolution);
  QVERIFY(!evaluateChecks(m.challengeChecks, state, m).passed);
}

void tst_missions::tst_mission3() {
  const auto &m = mission(3);
  auto state = run(m.processor, m.example);
  for (const auto &step : m.steps) {
    const auto report = evaluateChecks(step.checks, state, m);
    QVERIFY2(report.passed, qPrintable(step.title + ": " + failures(report)));
  }
  QCOMPARE(state.stats.squashed, quint64(18)); // 9 taken branches x 2
  QCOMPARE(state.stats.retiredBranches, quint64(10));
  QCOMPARE(state.stats.retiredTakenBranches, quint64(9));
  auto report = evaluateChecks(m.challengeChecks, state, m);
  QVERIFY(!report.passed);
  QVERIFY(failures(report).contains("measured: 18"));

  // Straight-line code without a loop is rejected.
  state = run(m.processor, "addi a0, x0, 55");
  QVERIFY(!evaluateChecks(m.challengeChecks, state, m).passed);

  state = run(m.processor, m.referenceSolution);
  report = evaluateChecks(m.challengeChecks, state, m);
  QVERIFY2(report.passed, qPrintable(failures(report)));
  QCOMPARE(state.stats.squashed, quint64(8));
}

void tst_missions::tst_mission4() {
  const auto &m = mission(4);
  QVERIFY(m.dataCache.has_value());
  QCOMPARE(m.dataCache->sizeBytes(4), quint64(256));
  auto state = run(m.processor, m.example);
  for (const auto &step : m.steps) {
    const auto report = evaluateChecks(step.checks, state, m);
    QVERIFY2(report.passed, qPrintable(step.title + ": " + failures(report)));
  }
  // Column by column: every one of the 256 loads misses (16 lines of a
  // column compete for 4 sets and are evicted before the next column).
  QCOMPARE(state.dataAccesses.size(), size_t(256));
  auto counts = simulateCache(*m.dataCache, state.dataAccesses, 4);
  QCOMPARE(counts.misses, quint64(256));
  QCOMPARE(counts.hits, quint64(0));
  auto report = evaluateChecks(m.challengeChecks, state, m);
  QVERIFY(!report.passed);
  QVERIFY(failures(report).contains("hit rate 0.0 %"));

  // Avoiding the loads (summing a constant) is rejected.
  state = run(m.processor, "li a0, 32896");
  QVERIFY(!evaluateChecks(m.challengeChecks, state, m).passed);

  // Row by row: one miss per 16-byte line, the next three words hit.
  state = run(m.processor, m.referenceSolution);
  report = evaluateChecks(m.challengeChecks, state, m);
  QVERIFY2(report.passed, qPrintable(failures(report)));
  counts = simulateCache(*m.dataCache, state.dataAccesses, 4);
  QCOMPARE(counts.misses, quint64(64));
  QCOMPARE(counts.hits, quint64(192));
}

void tst_missions::tst_cacheModel() {
  // 2-way set-associative, 2 sets, 1-word lines: blocks 0, lines 1, ways 1.
  CacheGeometry g;
  g.lines = 1;
  g.ways = 1;
  // Set 0 receives word addresses 0x0, 0x8, 0x10 (tags 0, 1, 2).
  const std::vector<DataAccess> a = {
      {0x0, false},  // miss, fill way
      {0x8, false},  // miss, fill second way
      {0x0, false},  // hit, 0x8 becomes least recently used
      {0x10, false}, // miss, evicts 0x8
      {0x0, false},  // hit
      {0x8, false},  // miss
      {0x3, false},  // hit: the two low bits are ignored, as in Ripes
  };
  auto c = simulateCache(g, a, 4);
  QCOMPARE(c.hits, quint64(3));
  QCOMPARE(c.misses, quint64(4));
  // Without write allocation, a write miss does not fill the cache.
  g.writeAllocate = false;
  c = simulateCache(g, {{0x20, true}, {0x20, false}}, 4);
  QCOMPARE(c.misses, quint64(2));
  QCOMPARE(g.describe(4), QString("16-byte 2-way set-associative cache, 4 "
                                  "line(s) of 4 bytes in 2 set(s)"));
}

void tst_missions::tst_cacheModelMatchesRipes() {
  // The mission's cache model must give the same counts as Ripes' own cache
  // simulator (as wired by the CLI's --cache options) for the same accesses.
  const auto &m = mission(4);
  const std::vector<CacheGeometry> geometries = {
      *m.dataCache, [] {
        CacheGeometry g; // 2-way, 8 sets, 2-word lines: exercises LRU
        g.blocks = 1;
        g.lines = 3;
        g.ways = 1;
        return g;
      }()};
  for (const QString &program : {m.example, m.referenceSolution}) {
    for (const auto &g : geometries) {
      const auto id = processorId(m.processor);
      ProcessorHandler::selectProcessor(
          id, ProcessorRegistry::getDescription(id).isaInfo().defaultExtensions,
          ProcessorRegistry::getDescription(id).defaultRegisterVals);
      auto cache = std::make_shared<CacheSim>(nullptr);
      CachePreset preset;
      preset.name = "test";
      preset.blocks = g.blocks;
      preset.lines = g.lines;
      preset.ways = g.ways;
      preset.wrPolicy = WritePolicy::WriteBack;
      preset.wrAllocPolicy = WriteAllocPolicy::WriteAllocate;
      preset.replPolicy = ReplPolicy::LRU;
      cache->setPreset(preset);
      L1CacheShim shim(L1CacheShim::CacheType::DataCache, nullptr);
      shim.setRequireCacheInterface(false);
      shim.setNextLevelCache(cache);
      RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
      m_loader->loadTest(program);
      auto *proc = ProcessorHandler::getProcessorNonConst();
      while (!proc->finished() && proc->getCycleCount() < 5000)
        proc->clock();
      const auto state = captureMissionState(m_recorder->trace(), program);
      const auto counts = simulateCache(g, state.dataAccesses, 4);
      QCOMPARE(counts.hits, quint64(cache->getHits()));
      QCOMPARE(counts.misses, quint64(cache->getMisses()));
    }
  }
}

void tst_missions::tst_missionStepsProgressLive() {
  // Steps are verified against live state while the user clocks the
  // processor, not only at the end of a run.
  const auto &m = mission(3);
  const auto id = processorId(m.processor);
  ProcessorHandler::selectProcessor(
      id, ProcessorRegistry::getDescription(id).isaInfo().defaultExtensions);
  RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
  m_loader->loadTest(m.example);
  auto *proc = ProcessorHandler::getProcessorNonConst();
  auto state = captureMissionState(m_recorder->trace(), m.example);
  QVERIFY(evaluateChecks(m.steps[0].checks, state, m).passed);
  QVERIFY(!evaluateChecks(m.steps[1].checks, state, m).passed);
  qint64 firstFlushCycle = -1;
  while (!proc->finished() && proc->getCycleCount() < 100) {
    proc->clock();
    state = captureMissionState(m_recorder->trace(), m.example);
    if (evaluateChecks(m.steps[1].checks, state, m).passed) {
      firstFlushCycle = proc->getCycleCount();
      break;
    }
  }
  // The first bne enters EX in cycle 6 and is resolved at the clock edge to
  // cycle 7, which squashes the instructions in IF and ID.
  QCOMPARE(firstFlushCycle, qint64(7));
  QVERIFY(!evaluateChecks(m.steps[2].checks, state, m).passed);
}

void tst_missions::tst_progressScoring() {
  // Stars: half of the steps (rounded up), all steps, solved challenge.
  QCOMPARE(missionStars({3, 0, false}), 0);
  QCOMPARE(missionStars({3, 1, false}), 0);
  QCOMPARE(missionStars({3, 2, false}), 1);
  QCOMPARE(missionStars({3, 3, false}), 2);
  QCOMPARE(missionStars({3, 3, true}), 3);
  QCOMPARE(missionStars({3, 0, true}), 1);
  QCOMPARE(missionStars({4, 2, false}), 1);
  QCOMPARE(missionStars({1, 1, false}), 2);
  QCOMPARE(missionStars({0, 0, true}), 1);

  QCOMPARE(missionXp({3, 0, false}), 0);
  QCOMPARE(missionXp({3, 2, false}), 2 * kXpPerStep);
  QCOMPARE(missionXp({3, 3, true}), 3 * kXpPerStep + kXpPerChallenge);
  QCOMPARE(missionXp({3, 7, false}), 3 * kXpPerStep); // clamped to the steps

  QCOMPARE(playerLevel(0).level, 1);
  QCOMPARE(playerLevel(0).title, QString("Fetch"));
  QCOMPARE(playerLevel(kXpPerLevel - 1).level, 1);
  QCOMPARE(playerLevel(kXpPerLevel).level, 2);
  QCOMPARE(playerLevel(kXpPerLevel).title, QString("Decode"));
  QCOMPARE(playerLevel(kXpPerLevel).xpIntoLevel, 0);
  const auto all = playerLevel(4 * (3 * kXpPerStep + kXpPerChallenge));
  QCOMPARE(all.level, 5);
  QCOMPARE(all.title, QString("Write-back"));
  QCOMPARE(all.xpIntoLevel, 400);

  // Unlocks follow the catalog order; progress in a mission unlocks it.
  const std::vector<MissionRecord> records = {
      {3, 3, false}, {3, 1, false}, {3, 0, false}, {3, 0, true}, {3, 0, false}};
  QCOMPARE(missionStatus(records, 0), MissionStatus::InProgress);
  QCOMPARE(missionStatus(records, 1), MissionStatus::InProgress);
  QCOMPARE(missionStatus(records, 2), MissionStatus::Locked);
  QCOMPARE(missionStatus(records, 3), MissionStatus::Solved);
  QCOMPARE(missionStatus(records, 4), MissionStatus::Available);
  QCOMPARE(missionStatus({{3, 0, false}}, 0), MissionStatus::Available);
  QCOMPARE(missionStatus(records, 9), MissionStatus::Locked);

  // Persistent progress over the real catalog.
  MissionProgress progress({"m1-first-program/1", "m1-first-program/1"}, {});
  QCOMPARE(progress.verifiedSteps(), QStringList({"m1-first-program/1"}));
  QVERIFY(progress.stepVerified("m1-first-program", 0));
  QVERIFY(!progress.recordStep("m1-first-program", 0));
  QVERIFY(progress.recordStep("m1-first-program", 1));
  QVERIFY(progress.recordStep("m1-first-program", 2));
  QVERIFY(progress.recordSolved("m3-branch-and-flush"));
  QVERIFY(!progress.recordSolved("m3-branch-and-flush"));
  QCOMPARE(MissionProgress::stepKey("m2-pipeline-hazards", 0),
           QString("m2-pipeline-hazards/1"));
  const auto totals = progress.totals(m_missions);
  QCOMPARE(totals.missions, 4);
  QCOMPARE(totals.steps, 12);
  QCOMPARE(totals.verifiedSteps, 3);
  QCOMPARE(totals.solved, 1);
  QCOMPARE(totals.xp, 3 * kXpPerStep + kXpPerChallenge);
  QCOMPARE(totals.stars, 3);
  QCOMPARE(totals.maxStars, 12);
  const auto recs = progress.records(m_missions);
  QCOMPARE(missionStatus(recs, 0), MissionStatus::InProgress);
  QCOMPARE(missionStatus(recs, 1), MissionStatus::Available);
  QCOMPARE(missionStatus(recs, 2), MissionStatus::Solved);
  QCOMPARE(missionStatus(recs, 3), MissionStatus::Available);
}

void tst_missions::tst_progressFromVerifiedChecks() {
  // Scores only come from checks that passed on the simulator: run Mission
  // 1's example, record the steps whose checks pass, then solve the
  // challenge with the reference solution.
  const auto &m = mission(1);
  MissionProgress progress;
  auto state = run(m.processor, m.example);
  for (size_t i = 0; i < m.steps.size(); ++i)
    if (evaluateChecks(m.steps[i].checks, state, m).passed)
      progress.recordStep(m.id, i);
  QVERIFY(!evaluateChecks(m.challengeChecks, state, m).passed);
  QCOMPARE(progress.record(m).verifiedSteps, 3);
  QCOMPARE(missionStars(progress.record(m)), 2);
  QCOMPARE(progress.totals(m_missions).xp, 3 * kXpPerStep);

  state = run(m.processor, m.referenceSolution);
  QVERIFY(evaluateChecks(m.challengeChecks, state, m).passed);
  progress.recordSolved(m.id);
  QCOMPARE(missionStars(progress.record(m)), 3);
  QCOMPARE(progress.totals(m_missions).xp, 3 * kXpPerStep + kXpPerChallenge);
  QCOMPARE(missionStatus(progress.records(m_missions), 1),
           MissionStatus::Available);
  QCOMPARE(missionStatus(progress.records(m_missions), 2),
           MissionStatus::Locked);
}

void tst_missions::tst_explainerFirstProgram() {
  run("RV32_5S", "addi x1, x0, 10\naddi x2, x0, 20\nadd x3, x1, x2");
  TraceReplay replay(m_recorder->trace());
  const auto e = explainInstance(replay, 3);
  QCOMPARE(e.decoded.mnemonic, QString("add"));
  QVERIFY(e.semantics.has_value());
  QCOMPARE(e.operands.size(), size_t(2));
  QCOMPARE(e.operands[0].value, std::optional<quint64>(10));
  QCOMPARE(e.operands[1].value, std::optional<quint64>(20));
  QVERIFY(e.result.has_value());
  QCOMPARE(e.result->value, quint64(30));
  QCOMPARE(e.expected, std::optional<quint64>(30));
  QVERIFY(!e.findings.isEmpty());
  QVERIFY(e.findings.first().startsWith("✓"));
  // add depends on both addi instructions, which were still in flight; the
  // forwarding unit resolves this without stalling.
  QCOMPARE(e.hazards.size(), qsizetype(2));
  QVERIFY(e.hazards.first().contains("forwarding"));
  QCOMPARE(e.journey.front().cycle, qint64(2));
  QVERIFY(e.sourceRefs.contains("src/processors/RISC-V/rv5s/rv5s.h"));
  QVERIFY(e.sourceRefs.contains("src/processors/RISC-V/rv_alu.h"));
  const QString html = explanationToHtml(e, false);
  QVERIFY(html.contains("add x3 x1 x2"));
  QVERIFY(html.contains("0x0000001e"));
}

void tst_missions::tst_explainerLoadUseHazard() {
  run("RV32_5S", mission(2).example);
  TraceReplay replay(m_recorder->trace());
  qint64 addId = -1;
  for (const auto &[id, inst] : replay.instances())
    if (inst.assembly.startsWith("add x7"))
      addId = id;
  QVERIFY(addId > 0);
  const auto e = explainInstance(replay, addId);
  QCOMPARE(e.result->value, quint64(14));
  QVERIFY(e.findings.first().startsWith("✓"));
  bool stallMentioned = false;
  for (const auto &h : e.hazards)
    stallMentioned |= h.contains("RAW") && h.contains("stalling");
  QVERIFY(stallMentioned);
}

void tst_missions::tst_explainerDetectsUnresolvedHazard() {
  // Without forwarding and hazard detection, an immediate use reads a stale
  // register value. The explainer must flag the mismatch with the ISA.
  run("RV32_5S_NO_FW_HZ", "addi x1, x0, 5\naddi x2, x1, 1");
  TraceReplay replay(m_recorder->trace());
  const auto e = explainInstance(replay, 2);
  QVERIFY(e.result.has_value() || !e.findings.isEmpty());
  QCOMPARE(e.expected, std::optional<quint64>(6));
  QVERIFY(!e.findings.isEmpty());
  QVERIFY2(e.findings.first().startsWith("✗"), qPrintable(e.findings.first()));
  bool noForwardingNote = false;
  for (const auto &h : e.hazards)
    noForwardingNote |= h.contains("no forwarding unit");
  QVERIFY(noForwardingNote);
}

void tst_missions::tst_explainerSquashedInstruction() {
  run("RV32_5S", mission(3).example);
  TraceReplay replay(m_recorder->trace());
  for (const auto &[id, inst] : replay.instances()) {
    if (inst.outcome != TraceReplay::Outcome::Squashed)
      continue;
    const auto e = explainInstance(replay, id);
    QVERIFY(!e.result.has_value());
    QVERIFY(e.findings.last().contains("squashed"));
    QVERIFY(e.journey.back().what.contains("bne") ||
            e.journey.back().what.contains("inst-"));
    return;
  }
  QFAIL("no squashed instruction found");
}

QTEST_MAIN(tst_missions)
#include "tst_missions.moc"
