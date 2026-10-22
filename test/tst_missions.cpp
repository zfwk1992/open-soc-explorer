// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: tests for the learning missions, the instruction
// explainer and the component knowledge base.
#include <QFileInfo>
#include <QtTest/QTest>

#include "explorer/componentinfo.h"
#include "explorer/instructionexplainer.h"
#include "explorer/mission.h"
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
  void tst_missionStepsProgressLive();
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
  QCOMPARE(m_missions.size(), size_t(3));
  for (int i = 0; i < 3; ++i) {
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
