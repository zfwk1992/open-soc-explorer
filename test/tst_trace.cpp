// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: tests for the trace protocol, the Ripes trace
// adapter (TraceRecorder) and deterministic replay (TraceReplay).
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QTest>

#include "explorer/instructionexplainer.h"
#include "explorer/modelprofile.h"
#include "explorer/riscvdecoder.h"
#include "explorer/tracejson.h"
#include "explorer/tracerecorder.h"
#include "explorer/tracereplay.h"
#include "isa/rvisainfo_common.h"
#include "processorhandler.h"
#include "processorregistry.h"
#include "programloader.h"
#include "ripessettings.h"

using namespace Ripes;
using namespace Ripes::Explorer;
using Trace::EventType;

namespace {

struct LiveSample {
  qint64 cycle = 0;
  TraceReplay::RegisterState regs;
  std::vector<std::pair<bool, quint64>> stages;
};

const char *kLoopProgram = R"(
.data
buf: .word 0, 0, 0, 0
.text
    la   a0, buf
    addi a1, x0, 4
    addi a2, x0, 0
loop:
    sw   a1, 0(a0)
    lw   t0, 0(a0)
    add  a2, a2, t0
    addi a0, a0, 4
    addi a1, a1, -1
    bne  a1, x0, loop
    addi a3, a2, 1
)";

quint64 reg(const TraceReplay::RegisterState &s, int idx) {
  return s.at("gpr").at(idx);
}

QByteArray normalized(Trace::Trace t) {
  t.header.runId = "run-test";
  t.header.createdUtc = "1970-01-01T00:00:00Z";
  return Trace::toJsonl(t);
}

} // namespace

class tst_trace : public QObject {
  Q_OBJECT

private:
  void load(ProcessorID id, const QString &program);
  std::vector<LiveSample> runToEnd(int maxCycles = 5000);
  LiveSample sample() const;
  ProgramLoader *m_loader = nullptr;

private slots:
  void initTestCase();
  void tst_eventTypeNames();
  void tst_decoder();
  void tst_firstProgram5S();
  void tst_firstProgramSingleCycle();
  void tst_loadUseStall();
  void tst_stallsWithoutForwarding();
  void tst_branchFlush();
  void tst_memoryEvents();
  void tst_replayMatchesSimulator_data();
  void tst_replayMatchesSimulator();
  void tst_jsonRoundTrip();
  void tst_rejectsUnknownSchema();
  void tst_determinism();
  void tst_reverseKeepsTraceConsistent();
  void tst_truncation();
  void tst_systemDescription();
  void tst_multiOccupancyReplay();
  void tst_sameCycleRetirement();
};

void tst_trace::initTestCase() { m_loader = new ProgramLoader(); }

void tst_trace::load(ProcessorID id, const QString &program) {
  ProcessorHandler::selectProcessor(
      id, ProcessorRegistry::getDescription(id).isaInfo().defaultExtensions);
  RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
  ProcessorHandler::getProcessorNonConst()->trapHandler = [] {};
  m_loader->loadTest(program);
}

LiveSample tst_trace::sample() const {
  const auto *proc = ProcessorHandler::getProcessor();
  LiveSample s;
  s.cycle = proc->getCycleCount();
  std::vector<quint64> gpr;
  for (unsigned i = 0; i < 32; ++i)
    gpr.push_back(proc->getRegister(RVISA::GPR, i));
  s.regs["gpr"] = gpr;
  for (auto idx : proc->structure().stageIt()) {
    const auto si = proc->stageInfo(idx);
    s.stages.push_back({si.stage_valid, si.pc});
  }
  return s;
}

std::vector<LiveSample> tst_trace::runToEnd(int maxCycles) {
  auto *proc = ProcessorHandler::getProcessorNonConst();
  std::vector<LiveSample> samples{sample()};
  while (!proc->finished() && proc->getCycleCount() < maxCycles) {
    proc->clock();
    samples.push_back(sample());
  }
  return samples;
}

void tst_trace::tst_eventTypeNames() {
  for (auto type : Trace::allEventTypes()) {
    const auto name = Trace::eventTypeName(type);
    QVERIFY(name.contains('.'));
    QCOMPARE(Trace::eventTypeFromName(name), std::optional<EventType>(type));
  }
  QVERIFY(!Trace::eventTypeFromName("pipeline.teleport").has_value());
  QCOMPARE(Trace::toHex(30, 32), QString("0x0000001e"));
  QCOMPARE(Trace::fromHex("0x1e"), std::optional<quint64>(30));
  QCOMPARE(Trace::instanceFromName(Trace::instanceName(42)), 42);
}

void tst_trace::tst_decoder() {
  // Encodings cross-checked against the RISC-V unprivileged specification.
  auto addi = decodeRV(0x00a00093, 32); // addi x1, x0, 10
  QVERIFY(addi.valid);
  QCOMPARE(addi.mnemonic, QString("addi"));
  QCOMPARE(addi.rd, 1u);
  QCOMPARE(addi.rs1, 0u);
  QCOMPARE(addi.imm, 10);
  QCOMPARE(addi.format, RVFormat::I);

  auto add = decodeRV(0x002081b3, 32); // add x3, x1, x2
  QCOMPARE(add.mnemonic, QString("add"));
  QCOMPARE(add.rd, 3u);
  QCOMPARE(add.rs1, 1u);
  QCOMPARE(add.rs2, 2u);
  QCOMPARE(add.format, RVFormat::R);
  QCOMPARE(evaluateIntegerResult(add, 0, 10, 20, 32),
           std::optional<quint64>(30));

  auto sub = decodeRV(0x40208033, 32); // sub x0, x1, x2
  QCOMPARE(sub.mnemonic, QString("sub"));
  QVERIFY(!sub.writesRd()); // x0 is never written

  auto lw = decodeRV(0x00052703, 32); // lw x14, 0(x10)
  QCOMPARE(lw.mnemonic, QString("lw"));
  QVERIFY(lw.isLoad);
  QCOMPARE(lw.memBytes, 4u);

  auto sw = decodeRV(0x00b52223, 32); // sw x11, 4(x10)
  QCOMPARE(sw.mnemonic, QString("sw"));
  QCOMPARE(sw.imm, 4);
  QCOMPARE(sw.rs2, 11u);
  QVERIFY(!sw.writesRd());

  auto bne = decodeRV(0xfe059ae3, 32); // bne x11, x0, -12
  QCOMPARE(bne.mnemonic, QString("bne"));
  QCOMPARE(bne.imm, -12);
  QCOMPARE(evaluateBranch(bne, 3, 0, 32), std::optional<bool>(true));
  QCOMPARE(evaluateBranch(bne, 0, 0, 32), std::optional<bool>(false));

  auto jal = decodeRV(0x008000ef, 32); // jal x1, 8
  QCOMPARE(jal.mnemonic, QString("jal"));
  QCOMPARE(jal.imm, 8);
  QCOMPARE(evaluateIntegerResult(jal, 0x100, 0, 0, 32),
           std::optional<quint64>(0x104));

  auto lui = decodeRV(0x123452b7, 32); // lui x5, 0x12345
  QCOMPARE(lui.mnemonic, QString("lui"));
  QCOMPARE(evaluateIntegerResult(lui, 0, 0, 0, 32),
           std::optional<quint64>(0x12345000));

  auto srai = decodeRV(0x4020d093, 32); // srai x1, x1, 2
  QCOMPARE(srai.mnemonic, QString("srai"));
  QCOMPARE(evaluateIntegerResult(srai, 0, 0xFFFFFFF0, 0, 32),
           std::optional<quint64>(0xFFFFFFFC));

  auto addiw = decodeRV(0x0010809b, 64); // addiw x1, x1, 1
  QCOMPARE(addiw.mnemonic, QString("addiw"));
  QVERIFY(!decodeRV(0x0010809b, 32).valid);

  auto mul = decodeRV(0x02208033, 32); // mul x0, x1, x2
  QCOMPARE(mul.mnemonic, QString("mul"));
  QVERIFY(mul.isMulDiv);

  auto compressed = decodeRV(0x4505, 32); // c.li a0, 1
  QVERIFY(compressed.compressed);

  QVERIFY(semanticsFor("add").has_value());
  QVERIFY(!semanticsFor("not-an-instruction").has_value());
}

void tst_trace::tst_firstProgram5S() {
  TraceRecorder recorder;
  load(ProcessorID::RV32_5S,
       "addi x1, x0, 10\naddi x2, x0, 20\nadd x3, x1, x2");
  runToEnd();
  const auto trace = recorder.trace();
  QVERIFY(trace.isComplete());
  QCOMPARE(trace.header.source.engine, QString("ripes"));
  QCOMPARE(trace.header.source.model, QString("RV32_5S"));
  QCOMPARE(trace.header.source.fidelity, QString("educational-model"));
  QCOMPARE(trace.header.stages.size(), size_t(5));

  TraceReplay replay(trace);
  const auto stats = replay.stats();
  QCOMPARE(stats.retired, quint64(3));
  QCOMPARE(stats.cycles, qint64(7)); // 3 instructions + 4 fill cycles
  QCOMPARE(stats.stallCycles, quint64(0));
  QCOMPARE(stats.squashed, quint64(0));
  QCOMPARE(stats.unattributedRegisterWrites, quint64(0));

  // Each instruction passes IF, ID, EX, MEM, WB in consecutive cycles.
  QCOMPARE(replay.instances().size(), size_t(3));
  qint64 expectedFetch = 0;
  for (const auto &[id, inst] : replay.instances()) {
    QCOMPARE(inst.outcome, TraceReplay::Outcome::Retired);
    QCOMPARE(inst.stageEntries.size(), size_t(5));
    for (int s = 0; s < 5; ++s) {
      QCOMPARE(inst.stageEntries[s].first, expectedFetch + s);
      QCOMPARE(inst.stageEntries[s].second, s);
    }
    QCOMPARE(inst.endCycle, expectedFetch + 5);
    ++expectedFetch;
  }

  // Register writes carry the written values and the responsible instruction.
  std::map<QString, QString> writes;
  for (const auto &ev : trace.events)
    if (ev.type == EventType::RegisterWrite) {
      writes[ev.payload["name"].toString()] = ev.payload["value"].toString();
      QVERIFY(ev.instance >= 0);
      QVERIFY(ev.causeSequence.has_value());
    }
  QCOMPARE(writes.size(), size_t(3));
  QCOMPARE(writes["x1"], QString("0x0000000a"));
  QCOMPARE(writes["x2"], QString("0x00000014"));
  QCOMPARE(writes["x3"], QString("0x0000001e"));
  QCOMPARE(reg(replay.registersAt(replay.lastCycle()), 3), quint64(30));
  // x3 is written when "add" retires in cycle 7; before that it is still 0.
  QCOMPARE(reg(replay.registersAt(6), 3), quint64(0));

  const auto before = replay.registersBeforeRetire(3);
  QVERIFY(before.has_value());
  QCOMPARE(reg(*before, 1), quint64(10));
  QCOMPARE(reg(*before, 2), quint64(20));
}

void tst_trace::tst_firstProgramSingleCycle() {
  TraceRecorder recorder;
  load(ProcessorID::RV32_SS,
       "addi x1, x0, 10\naddi x2, x0, 20\nadd x3, x1, x2");
  runToEnd();
  TraceReplay replay(recorder.trace());
  const auto stats = replay.stats();
  QCOMPARE(stats.retired, quint64(3));
  QCOMPARE(stats.cycles, qint64(3));
  QCOMPARE(replay.trace().header.stages.size(), size_t(1));
  QCOMPARE(replay.trace().header.stages[0].component, QString("soc.cpu0.core"));
  QCOMPARE(reg(replay.registersAt(1), 1), quint64(10));
  QCOMPARE(reg(replay.registersAt(3), 3), quint64(30));
}

void tst_trace::tst_loadUseStall() {
  TraceRecorder recorder;
  load(ProcessorID::RV32_5S, ".data\nA: .word 5\n.text\nla a0, A\n"
                             "lw t1, 0(a0)\nadd t2, t1, t1");
  runToEnd();
  TraceReplay replay(recorder.trace());
  const auto stats = replay.stats();
  QCOMPARE(stats.retired, quint64(4)); // la expands to auipc + addi
  QCOMPARE(stats.stallCycles, quint64(1));
  QCOMPARE(stats.stallBubbles, quint64(1));
  QCOMPARE(stats.cycles, qint64(9)); // 4 + 4 fill + 1 load-use stall
  QCOMPARE(reg(replay.registersAt(replay.lastCycle()), 7), quint64(10));

  // The dependent add is the instruction that was held.
  bool addHeld = false;
  for (const auto &[id, inst] : replay.instances())
    if (replay.mnemonicOf(id) == "add")
      addHeld = !inst.heldCycles.empty();
  QVERIFY(addHeld);
}

void tst_trace::tst_stallsWithoutForwarding() {
  const QString program =
      ".data\nA: .word 5\n.text\nla a0, A\nlw t1, 0(a0)\nadd t2, t1, t1";
  quint64 withForwarding, withoutForwarding;
  {
    TraceRecorder recorder;
    load(ProcessorID::RV32_5S, program);
    runToEnd();
    withForwarding = TraceReplay(recorder.trace()).stats().stallCycles;
  }
  {
    TraceRecorder recorder;
    load(ProcessorID::RV32_5S_NO_FW, program);
    runToEnd();
    TraceReplay replay(recorder.trace());
    withoutForwarding = replay.stats().stallCycles;
    QCOMPARE(reg(replay.registersAt(replay.lastCycle()), 7), quint64(10));
  }
  QVERIFY2(withoutForwarding > withForwarding,
           qPrintable(QString("no-forwarding stalls %1, forwarding stalls %2")
                          .arg(withoutForwarding)
                          .arg(withForwarding)));
  {
    // Without hazard detection the model never stalls.
    TraceRecorder recorder;
    load(ProcessorID::RV32_5S_NO_FW_HZ, program);
    runToEnd();
    QCOMPARE(TraceReplay(recorder.trace()).stats().stallCycles, quint64(0));
  }
}

void tst_trace::tst_branchFlush() {
  TraceRecorder recorder;
  // At least two instructions follow the branch, so that both younger
  // pipeline slots hold real instructions when the branch is taken.
  load(ProcessorID::RV32_5S, "addi a0, x0, 0\naddi a1, x0, 5\nloop:\n"
                             "add a0, a0, a1\naddi a1, a1, -1\n"
                             "bne a1, x0, loop\naddi a2, x0, 1\n"
                             "addi a3, x0, 2\naddi a4, x0, 3");
  runToEnd();
  const auto trace = recorder.trace();
  TraceReplay replay(trace);
  const auto stats = replay.stats();
  QCOMPARE(reg(replay.registersAt(replay.lastCycle()), 10), quint64(15));
  QCOMPARE(stats.retiredBranches, quint64(5));
  QCOMPARE(stats.retiredTakenBranches, quint64(4));
  // Branches resolve in EX: each taken branch squashes the two younger
  // instructions in IF and ID.
  QCOMPARE(stats.squashed, quint64(8));
  for (const auto &ev : trace.events)
    if (ev.type == EventType::PipelineFlush &&
        ev.payload["kind"].toString() == "squash") {
      const qint64 cause =
          Trace::instanceFromName(ev.payload["cause_instance"].toString());
      QCOMPARE(replay.mnemonicOf(cause), QString("bne"));
      QVERIFY(ev.causeSequence.has_value());
    }
  const auto spec = Trace::speculativeStatus(trace);
  int squashedSpeculative = 0;
  for (const auto &[id, s] : spec)
    if (s.value_or(false))
      ++squashedSpeculative;
  QCOMPARE(squashedSpeculative, 8);
}

void tst_trace::tst_memoryEvents() {
  TraceRecorder recorder;
  load(ProcessorID::RV32_5S, ".data\nA: .word 0\n.text\nla a0, A\n"
                             "addi t0, x0, 42\nsw t0, 0(a0)\nlw t1, 0(a0)");
  runToEnd();
  const auto trace = recorder.trace();
  TraceReplay replay(trace);
  const auto dataStart =
      RipesSettings::value(RIPES_SETTING_ASSEMBLER_DATASTART).toULongLong();

  const Trace::TraceEvent *writeReq = nullptr, *writeResp = nullptr,
                          *readResp = nullptr;
  for (const auto &ev : trace.events) {
    const auto access = ev.payload.value("access").toString();
    if (ev.type == EventType::MemoryRequest && access == "write")
      writeReq = &ev;
    if (ev.type == EventType::MemoryResponse && access == "write")
      writeResp = &ev;
    if (ev.type == EventType::MemoryResponse && access == "read")
      readResp = &ev;
  }
  QVERIFY(writeReq && writeResp && readResp);
  QCOMPARE(Trace::fromHex(writeReq->payload["address"].toString()),
           std::optional<quint64>(dataStart));
  QVERIFY(writeReq->assembly.startsWith("sw"));
  QCOMPARE(writeResp->cycle, writeReq->cycle + 1);
  QCOMPARE(writeResp->causeSequence,
           std::optional<quint64>(writeReq->sequence));
  QCOMPARE(writeResp->payload["value"].toString(), QString("0x0000002a"));
  QVERIFY(readResp->assembly.startsWith("lw"));
  QCOMPARE(readResp->payload["value"].toString(), QString("0x0000002a"));
  QCOMPARE(reg(replay.registersAt(replay.lastCycle()), 6), quint64(42));
  QCOMPARE(replay.stats().memoryReads, quint64(1));
  QCOMPARE(replay.stats().memoryWrites, quint64(1));
}

void tst_trace::tst_replayMatchesSimulator_data() {
  QTest::addColumn<int>("processor");
  for (auto id : {ProcessorID::RV32_SS, ProcessorID::RV32_5MC_2M,
                  ProcessorID::RV32_5MC_1M, ProcessorID::RV32_5S_NO_FW_HZ,
                  ProcessorID::RV32_5S_NO_HZ, ProcessorID::RV32_5S_NO_FW,
                  ProcessorID::RV32_5S, ProcessorID::RV32_6S_DUAL,
                  ProcessorID::RV32_ISS, ProcessorID::RV64_5S}) {
    QTest::newRow(qPrintable(enumToString<ProcessorID>(id)))
        << static_cast<int>(id);
  }
}

void tst_trace::tst_replayMatchesSimulator() {
  QFETCH(int, processor);
  const auto id = static_cast<ProcessorID>(processor);
  TraceRecorder recorder;
  load(id, kLoopProgram);
  const auto samples = runToEnd();
  const auto *proc = ProcessorHandler::getProcessor();
  QVERIFY(proc->finished());

  // Replay from the exported JSON Lines representation, i.e. without any
  // access to the simulator.
  QString error;
  const auto reloaded =
      Trace::fromJsonl(Trace::toJsonl(recorder.trace()), &error);
  QVERIFY2(reloaded.has_value(), qPrintable(error));
  QVERIFY(reloaded->isComplete());
  TraceReplay replay(*reloaded);

  QCOMPARE(replay.firstCycle(), samples.front().cycle);
  QCOMPARE(replay.lastCycle(), samples.back().cycle);
  for (const auto &s : samples) {
    QVERIFY2(replay.registersAt(s.cycle) == s.regs,
             qPrintable(QString("register mismatch at cycle %1").arg(s.cycle)));
    const auto pipeline = replay.pipelineAt(s.cycle);
    QCOMPARE(pipeline.size(), s.stages.size());
    for (size_t st = 0; st < s.stages.size(); ++st) {
      const auto &[valid, pc] = s.stages[st];
      const auto &slot = pipeline[st];
      QVERIFY2(valid == (slot.kind == TraceReplay::SlotKind::Instruction),
               qPrintable(QString("stage %1 occupancy mismatch at cycle %2")
                              .arg(st)
                              .arg(s.cycle)));
      if (valid)
        QCOMPARE(replay.instance(slot.instance)->pc, pc);
    }
  }

  const auto stats = replay.stats();
  QCOMPARE(stats.unattributedRegisterWrites, quint64(0));
  int inferred = 0;
  for (const auto &ev : replay.trace().events)
    if (ev.type == EventType::InstructionRetire) {
      QVERIFY(ev.instance >= 0);
      if (!ev.payload.contains("retire_index"))
        ++inferred;
    }
  const auto modelRetired = quint64(proc->getInstructionsRetired());
  if (id == ProcessorID::RV32_6S_DUAL) {
    // Known upstream behaviour (Ripes v2.3.0): the dual-issue model's
    // retirement counter misses the final instruction of this program, although
    // its result is written. The trace reports it as an inferred retirement.
    QCOMPARE(modelRetired, quint64(28));
    QCOMPARE(inferred, 1);
  } else {
    QCOMPARE(modelRetired, stats.retired);
    QCOMPARE(inferred, 0);
  }

  // Models that resolve data hazards compute the architecturally correct
  // result. The 5-stage models without hazard detection deliberately do not.
  const auto profile = modelProfile(id);
  const auto final = replay.registersAt(replay.lastCycle());
  if (!profile.pipelined || profile.hasHazardDetection) {
    // 29 dynamic instructions: la (2) + 2 + 4 iterations x 6 + 1.
    QCOMPARE(stats.retired, quint64(29));
    QCOMPARE(reg(final, 12), quint64(4 + 3 + 2 + 1)); // a2
    QCOMPARE(reg(final, 13), quint64(11));            // a3
  }
}

void tst_trace::tst_jsonRoundTrip() {
  TraceRecorder recorder;
  load(ProcessorID::RV32_5S, kLoopProgram);
  runToEnd();
  const auto trace = recorder.trace();
  const QByteArray once = Trace::toJsonl(trace);
  QString error;
  const auto parsed = Trace::fromJsonl(once, &error);
  QVERIFY2(parsed.has_value(), qPrintable(error));
  QCOMPARE(parsed->events.size(), trace.events.size());
  QCOMPARE(parsed->header.stages, trace.header.stages);
  QCOMPARE(parsed->header.source, trace.header.source);
  QCOMPARE(Trace::toJsonl(*parsed), once);

  // Every line is a self-contained JSON object with the required fields.
  const auto lines = once.trimmed().split('\n');
  QCOMPARE(lines.size(), static_cast<qsizetype>(trace.events.size() + 2));
  for (const auto &line : lines) {
    const auto o = QJsonDocument::fromJson(line).object();
    QCOMPARE(o["schema_version"].toString(), QString(Trace::kSchemaVersion));
    QVERIFY(!o["run_id"].toString().isEmpty());
    if (o["record"].toString() == "event") {
      QVERIFY(o.contains("sequence"));
      QCOMPARE(o["time"].toObject()["domain"].toString(), QString("cpu_clk"));
      QVERIFY(o["time"].toObject()["tick"].isNull());
      QVERIFY(o.contains("observability"));
      QVERIFY(o["state"].toObject().contains("speculative"));
    }
  }
}

void tst_trace::tst_rejectsUnknownSchema() {
  QString error;
  QVERIFY(!Trace::fromJsonl(R"({"record":"header","schema_version":"9.0.0"})",
                            &error)
               .has_value());
  QVERIFY(error.contains("schema"));
  QVERIFY(!Trace::fromJsonl("not json", &error).has_value());
}

void tst_trace::tst_determinism() {
  QByteArray first, second;
  for (QByteArray *out : {&first, &second}) {
    TraceRecorder recorder;
    load(ProcessorID::RV32_5S, kLoopProgram);
    runToEnd();
    *out = normalized(recorder.trace());
  }
  QVERIFY(!first.isEmpty());
  QCOMPARE(first, second);
}

void tst_trace::tst_reverseKeepsTraceConsistent() {
  QByteArray straight, reversed;
  {
    TraceRecorder recorder;
    load(ProcessorID::RV32_5S, kLoopProgram);
    runToEnd();
    straight = normalized(recorder.trace());
  }
  {
    TraceRecorder recorder;
    load(ProcessorID::RV32_5S, kLoopProgram);
    auto *proc = ProcessorHandler::getProcessorNonConst();
    for (int i = 0; i < 20; ++i)
      proc->clock();
    for (int i = 0; i < 7; ++i)
      proc->reverseProcessor();
    QCOMPARE(proc->getCycleCount(), 13);
    QCOMPARE(recorder.trace().completeness.endCycle, qint64(13));
    runToEnd();
    reversed = normalized(recorder.trace());
  }
  QCOMPARE(reversed, straight);
}

void tst_trace::tst_truncation() {
  TraceRecorder recorder;
  recorder.setMaxCycles(5);
  load(ProcessorID::RV32_5S, kLoopProgram);
  runToEnd();
  const auto trace = recorder.trace();
  QVERIFY(trace.completeness.truncated);
  QCOMPARE(trace.completeness.truncationReason, QString("max-cycles"));
  QCOMPARE(trace.completeness.endCycle, qint64(5));
  QVERIFY(!trace.isComplete());
  for (const auto &ev : trace.events)
    QVERIFY(ev.cycle <= 5);
}

void tst_trace::tst_systemDescription() {
  TraceRecorder recorder;
  load(ProcessorID::RV32_5S, kLoopProgram);
  runToEnd();
  const auto trace = recorder.trace();
  const auto &system = trace.header.system;
  QVERIFY(system.size() >= 2);
  QCOMPARE(system[0].path, QString("soc.cpu0"));
  QCOMPARE(system[0].role, QString("cpu"));
  QCOMPARE(system[0].type, QString("RV32_5S"));
  QCOMPARE(system[0].parameters["stages"].toInt(), 5);
  QCOMPARE(system[0].connections, QStringList{"soc.mem"});
  QCOMPARE(system[0].statistics["instructions_retired"].toLongLong(), 29);
  QCOMPARE(system[1].role, QString("memory"));
  QVERIFY(
      system[1].parameters["program_sections"].toString().contains(".text"));
  // Every listed peripheral is reachable through the memory, and nothing is
  // listed that Ripes does not simulate.
  for (size_t i = 2; i < system.size(); ++i) {
    QCOMPARE(system[i].role, QString("peripheral"));
    QVERIFY(system[1].connections.contains(system[i].path));
  }
  for (const auto &c : system)
    QVERIFY(c.role != "cache" && c.role != "interconnect");

  // Round trip of the schema 0.2.0 header fields.
  auto modified = trace;
  modified.header.ticksPerCycle = 500;
  modified.header.tickUnit = "ps";
  modified.header.clockDomainDescription = "test domain";
  const auto parsed = Trace::fromJsonl(Trace::toJsonl(modified));
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->header.system, modified.header.system);
  QCOMPARE(parsed->header.ticksPerCycle, qint64(500));
  QCOMPARE(parsed->header.tickUnit, QString("ps"));
  QCOMPARE(parsed->header.clockDomainDescription, QString("test domain"));
}

void tst_trace::tst_multiOccupancyReplay() {
  // Out-of-order engines (e.g. gem5's O3CPU) hold several instructions in
  // one stage. Hand-built trace: two instructions fetched together, the
  // younger one completes first; a third is squashed.
  Trace::Trace t;
  t.header.stages = {{0, 0, "F", "c.f"}, {0, 1, "IS", "c.is"}};
  t.completeness.endCycle = 10;
  quint64 seq = 0;
  auto ev = [&](qint64 cycle, EventType type, const QString &component,
                qint64 inst, const QVariantMap &payload = {}) {
    Trace::TraceEvent e;
    e.sequence = seq++;
    e.cycle = cycle;
    e.type = type;
    e.component = component;
    e.instance = inst;
    e.pc = 0x100 + 4 * inst;
    e.assembly = QString("op%1").arg(inst);
    e.payload = payload;
    t.events.push_back(e);
  };
  ev(1, EventType::StageEnter, "c.f", 1);
  ev(1, EventType::StageEnter, "c.f", 2);
  ev(1, EventType::StageEnter, "c.f", 3);
  ev(2, EventType::StageEnter, "c.is", 1);
  ev(2, EventType::StageEnter, "c.is", 2);
  ev(4, EventType::PipelineFlush, "c.f", 3, {{"kind", "squash"}});
  ev(5, EventType::InstructionRetire, "c.is", 2);
  ev(7, EventType::InstructionRetire, "c.is", 1);
  TraceReplay replay(t);

  auto fetch = replay.pipelineAt(1)[0];
  QCOMPARE(fetch.occupants, 3);
  QCOMPARE(fetch.instance, qint64(1)); // oldest
  QCOMPARE(replay.occupantsOf(0, 1), (std::vector<qint64>{1, 2, 3}));
  QCOMPARE(replay.occupantsOf(1, 4), (std::vector<qint64>{1, 2}));
  QCOMPARE(replay.occupantsOf(0, 3), (std::vector<qint64>{3}));
  QCOMPARE(replay.stageOf(2, 4), 1);
  QCOMPARE(replay.stageOf(2, 5), -1); // retired at 5
  QCOMPARE(replay.stageOf(1, 6), 1);
  QCOMPARE(replay.stageOf(3, 4), -1); // squashed at 4
  QCOMPARE(replay.pipelineAt(6)[1].occupants, 1);
  const auto stats = replay.stats();
  QCOMPARE(stats.retired, quint64(2));
  QCOMPARE(stats.squashed, quint64(1));
  QCOMPARE(replay.retirementOrder(), (std::vector<qint64>{2, 1}));
}

void tst_trace::tst_sameCycleRetirement() {
  // Superscalar engines commit dependent instructions in the same cycle and
  // do not report initial register values (gem5). The operands of the second
  // instruction must include the result of the first, and registers that
  // were never written must be reported as unknown, not as zero.
  Trace::Trace t;
  t.header.xlen = 32;
  t.header.stages = {{0, 0, "F", "c.f"}};
  QStringList names;
  for (int i = 0; i < 32; ++i)
    names << QString("x%1").arg(i);
  t.header.registerFiles = {{"gpr", names, names}};
  t.completeness.endCycle = 6;
  quint64 seq = 0;
  auto ev = [&](qint64 cycle, EventType type, qint64 inst,
                const QVariantMap &payload = {}) {
    Trace::TraceEvent e;
    e.sequence = seq++;
    e.cycle = cycle;
    e.type = type;
    e.component = "c.f";
    e.instance = inst;
    e.pc = 0x100 + 4 * inst;
    e.payload = payload;
    t.events.push_back(e);
  };
  ev(1, EventType::InstructionFetch, 1,
     {{"word", "0x00700293"}}); // addi x5, x0, 7
  ev(1, EventType::InstructionFetch, 2,
     {{"word", "0x00528333"}}); // add x6, x5, x5
  ev(1, EventType::InstructionFetch, 3,
     {{"word", "0x007383b3"}}); // add x7, x7, x7
  ev(5, EventType::InstructionRetire, 1);
  ev(5, EventType::RegisterWrite, 1,
     {{"regfile", "gpr"}, {"index", 5}, {"value", "0x00000007"}});
  ev(5, EventType::InstructionRetire, 2);
  ev(5, EventType::RegisterWrite, 2,
     {{"regfile", "gpr"}, {"index", 6}, {"value", "0x0000000e"}});
  ev(5, EventType::InstructionRetire, 3);
  ev(5, EventType::RegisterWrite, 3,
     {{"regfile", "gpr"}, {"index", 7}, {"value", "0x00000000"}});
  TraceReplay replay(t);

  QVERIFY(!replay.registerKnownBeforeRetire(1, "gpr", 5));
  QVERIFY(replay.registerKnownBeforeRetire(2, "gpr", 5));
  QCOMPARE(replay.registersBeforeRetire(2)->at("gpr")[5], quint64(7));
  QCOMPARE(replay.registersBeforeRetire(2)->at("gpr")[6], quint64(0));
  QVERIFY(!replay.registerKnownAt("gpr", 5, 4));
  QVERIFY(replay.registerKnownAt("gpr", 5, 5));

  const auto second = explainInstance(replay, 2);
  QCOMPARE(second.expected, std::optional<quint64>(14));
  QVERIFY(second.findings.join("\n").contains("matches the ISA"));

  // x7 was never written before instruction 3: no ISA check, no hazard claim.
  const auto third = explainInstance(replay, 3);
  QVERIFY(!third.expected.has_value());
  QVERIFY(!third.findings.join("\n").contains("hazard"));
  QVERIFY(!third.operands.empty());
  QVERIFY(!third.operands.front().value.has_value());
}

QTEST_MAIN(tst_trace)
#include "tst_trace.moc"
