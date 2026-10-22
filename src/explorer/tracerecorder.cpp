// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "tracerecorder.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QUuid>

#include <algorithm>
#include <set>

#include "processorhandler.h"
#include "tracejson.h"
#include "version/version.h"

namespace Ripes {
namespace Explorer {

using Trace::EventType;
using Trace::Observability;
using Trace::TraceEvent;

namespace {

QString stageComponent(const QString &stageName, unsigned lane,
                       bool multiLane) {
  QString n = stageName.toLower();
  if (multiLane)
    return QString("soc.cpu0.lane%1.%2").arg(lane).arg(n);
  return "soc.cpu0.pipeline." + n;
}

bool isPlainName(const QString &name) {
  if (name.isEmpty())
    return false;
  return std::all_of(name.begin(), name.end(),
                     [](QChar c) { return c.isLetterOrNumber(); });
}

QString accessName(MemoryAccess::Type t) {
  return t == MemoryAccess::Write ? "write" : "read";
}

} // namespace

QString programImageHash(const Program &program) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (const auto &[name, section] : program.sections) {
    hash.addData(name.toUtf8());
    hash.addData(QByteArray::number(static_cast<qulonglong>(section.address)));
    hash.addData(section.data);
  }
  return QString::fromLatin1(hash.result().toHex());
}

TraceRecorder::TraceRecorder(QObject *parent, bool attach) : QObject(parent) {
  if (attach)
    attachToProcessorHandler();
}

TraceRecorder::~TraceRecorder() = default;

void TraceRecorder::attachToProcessorHandler() {
  auto *handler = ProcessorHandler::get();
  // The clocked handler must run in lockstep with the processor (it may be
  // emitted from the simulation worker thread), hence the direct connection.
  connect(handler, &ProcessorHandler::processorClocked, this,
          &TraceRecorder::onProcessorClocked, Qt::DirectConnection);
  connect(handler, &ProcessorHandler::processorReset, this,
          &TraceRecorder::onProcessorReset, Qt::DirectConnection);
  connect(handler, &ProcessorHandler::processorChanged, this,
          &TraceRecorder::onProcessorReset, Qt::DirectConnection);
  connect(handler, &ProcessorHandler::processorReversed, this,
          &TraceRecorder::onProcessorReversed, Qt::DirectConnection);
  connect(handler, &ProcessorHandler::procStateChangedNonRun, this,
          &TraceRecorder::onProcessorStateChanged, Qt::DirectConnection);
  restart();
}

bool TraceRecorder::isEnabled() const {
  std::lock_guard lock(m_mutex);
  return m_enabled;
}

void TraceRecorder::setEnabled(bool enabled) {
  bool started = false;
  {
    std::lock_guard lock(m_mutex);
    if (enabled == m_enabled)
      return;
    m_enabled = enabled;
    if (enabled) {
      beginRunLocked();
      started = true;
    } else {
      m_active = false;
      if (!m_last.finished) {
        m_truncated = true;
        m_truncationReason = "recording-disabled";
      }
      ++m_revision;
    }
  }
  if (started)
    emit runStarted();
}

qint64 TraceRecorder::maxCycles() const {
  std::lock_guard lock(m_mutex);
  return m_maxCycles;
}

void TraceRecorder::setMaxCycles(qint64 cycles) {
  std::lock_guard lock(m_mutex);
  m_maxCycles = std::max<qint64>(1, cycles);
}

quint64 TraceRecorder::eventCount() const {
  std::lock_guard lock(m_mutex);
  return m_trace.events.size();
}

quint64 TraceRecorder::revision() const {
  std::lock_guard lock(m_mutex);
  return m_revision;
}

Trace::Trace TraceRecorder::trace() const {
  std::lock_guard lock(m_mutex);
  Trace::Trace copy = m_trace;
  fillCompleteness(copy);
  return copy;
}

bool TraceRecorder::exportJsonl(const QString &path, QString *error) const {
  return Trace::writeJsonlFile(trace(), path, error);
}

void TraceRecorder::restart() {
  {
    std::lock_guard lock(m_mutex);
    beginRunLocked();
  }
  emit runStarted();
}

void TraceRecorder::fillCompleteness(Trace::Trace &t) const {
  auto &c = t.completeness;
  c.startCycle = t.header.startCycle;
  c.startsAtReset = t.header.startCycle == 0;
  c.endCycle = m_last.cycle;
  c.truncated = m_truncated;
  c.truncationReason = m_truncationReason;
  c.programFinished = m_last.finished;
  c.instructionsRetired =
      static_cast<quint64>(std::max<long long>(0, m_last.retired));
  c.caveats.clear();
  c.caveats << "Instruction instance identity and squash causes are inferred "
               "from per-stage program counters (observability 'inferred').";
  c.caveats << "Register writes are detected by comparing register files "
               "between cycles; writes that do not change a value are not "
               "observable.";
  if (m_unattributedWrites > 0)
    c.caveats << QString("%1 register write(s) could not be attributed to a "
                         "retiring instruction (e.g. system call side "
                         "effects or manual edits).")
                     .arg(m_unattributedWrites);
  if (m_inferredRetirements > 0)
    c.caveats << QString("%1 retirement(s) were inferred from register writes "
                         "because the model's retired-instruction counter did "
                         "not count them (see docs/SIMULATION_FIDELITY.md).")
                     .arg(m_inferredRetirements);
  if (m_sawMmio)
    c.caveats << "Values of memory-mapped I/O accesses are not sampled, since "
                 "reading peripheral registers may have side effects.";
}

void TraceRecorder::beginRunLocked() {
  const auto *proc = ProcessorHandler::getProcessor();
  const auto isa = ProcessorHandler::currentISA();
  m_profile = modelProfile(ProcessorHandler::getID());

  m_trace = Trace::Trace();
  auto &h = m_trace.header;
  h.runId = "run-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  h.createdUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  h.source.engine = "ripes";
  h.source.engineVersion = getRipesVersion();
  h.source.adapter = "osx-ripes-live";
  h.source.adapterVersion = Trace::kExplorerVersion;
  h.source.model = m_profile.idName;
  h.source.modelName = m_profile.name;
  h.source.isa = isa->name();
  h.source.extensions = isa->enabledExtensions();
  h.source.fidelity = m_profile.fidelity;
  h.source.fidelityNote = m_profile.fidelityNote;
  h.source.sourceFile = m_profile.sourceFile;
  h.xlen = isa->bits();

  const auto &structure = proc->structure();
  const bool multiLane = structure.size() > 1;
  for (auto idx : structure.stageIt()) {
    Trace::StageDesc sd;
    sd.lane = idx.lane();
    sd.index = idx.index();
    sd.name = proc->stageName(idx);
    if (!isPlainName(sd.name))
      sd.name =
          structure.numStages() == 1 ? "CORE" : QString("S%1").arg(idx.index());
    sd.component = structure.numStages() == 1
                       ? QString("soc.cpu0.core")
                       : stageComponent(sd.name, sd.lane, multiLane);
    h.stages.push_back(sd);
  }

  m_regFileIds.clear();
  for (const auto &rf : proc->registerFiles()) {
    auto info = isa->regInfo(rf);
    if (!info.has_value())
      continue;
    Trace::RegisterFileDesc d;
    d.name = QString::fromUtf8(rf.data(), static_cast<int>(rf.size()));
    for (unsigned i = 0; i < (*info)->regCnt(); ++i) {
      d.names << (*info)->regName(i);
      d.aliases << (*info)->regAlias(i);
    }
    h.registerFiles.push_back(d);
    m_regFileIds.emplace_back(rf);
  }

  if (auto program = ProcessorHandler::getProgram()) {
    h.entryPoint = program->entryPoint;
    if (const auto *text = program->getSection(TEXT_SECTION_NAME)) {
      h.textStart = text->address;
      h.textSize = text->data.size();
    }
    h.programHash = programImageHash(*program);
  }

  m_nextSequence = 0;
  m_nextInstance = 1;
  m_instances.clear();
  m_pendingWrites.clear();
  m_disasmCache.clear();
  m_history.clear();
  m_truncated = false;
  m_truncationReason.clear();
  m_unattributedWrites = 0;
  m_inferredRetirements = 0;
  m_sawMmio = false;

  m_last = captureLocked();
  m_initialRetired = m_last.retired;
  h.startCycle = m_last.cycle;
  h.initialPc = proc->getPcForStage({0, 0});
  h.initialRegisters = m_last.regs;
  m_active = m_enabled;

  // A self-contained snapshot of the architectural state at the start of
  // the trace.
  TraceEvent snap;
  snap.cycle = m_last.cycle;
  snap.type = EventType::StateSnapshot;
  snap.component = "soc.cpu0";
  snap.pc = h.initialPc;
  QVariantMap regs;
  for (const auto &[rf, values] : m_last.regs) {
    QStringList hex;
    for (auto v : values)
      hex << Trace::toHex(v, h.xlen);
    regs[rf] = hex;
  }
  snap.payload["registers"] = regs;
  snap.payload["reason"] =
      h.startCycle == 0 ? "processor-reset" : "recording-started";
  snap.observability = Observability::Direct;
  snap.architectural = true;
  snap.sourceRef = "src/processors/interface/ripesprocessor.h#getRegister";
  emitLocked(snap);
  m_snapshotEvent = 0;

  // Instructions already present in the pipeline. Older instructions occupy
  // later stages, so assign instance ids from the last stage backwards to
  // keep ids in program order.
  for (int i = static_cast<int>(m_last.stageSlots.size()) - 1; i >= 0; --i) {
    auto &slot = m_last.stageSlots[i];
    if (!slot.valid)
      continue;
    const auto &sd = h.stages[i];
    slot.instance = newInstanceLocked(slot.pc, m_last.cycle);
    const auto &rec = m_instances[slot.instance];
    TraceEvent ev;
    ev.cycle = m_last.cycle;
    ev.component = sd.component;
    ev.instance = slot.instance;
    ev.pc = slot.pc;
    ev.assembly = rec.assembly;
    ev.payload["stage"] = sd.name;
    ev.payload["lane"] = sd.lane;
    ev.sourceRef = m_profile.sourceFile + "#stageInfo";
    if (sd.index == 0) {
      ev.type = EventType::InstructionFetch;
      ev.payload["word"] = Trace::toHex(rec.decoded.word, 32);
      emitLocked(ev);
      ev.payload.remove("word");
    } else {
      ev.observability = Observability::Inferred;
      ev.payload["note"] = "already in this stage when recording started";
    }
    ev.type = EventType::StageEnter;
    emitLocked(ev);
  }
  m_history.push_back(m_last.stageSlots);
  ++m_revision;
}

TraceRecorder::CycleState TraceRecorder::captureLocked() const {
  const auto *proc = ProcessorHandler::getProcessor();
  CycleState s;
  s.cycle = proc->getCycleCount();
  s.retired = proc->getInstructionsRetired();
  s.finished = proc->finished();
  for (const auto &sd : m_trace.header.stages) {
    const auto si = proc->stageInfo({sd.lane, sd.index});
    StageSlot slot;
    slot.valid = si.stage_valid;
    slot.pc = si.pc;
    slot.state = si.state;
    s.stageSlots.push_back(slot);
  }
  for (size_t r = 0; r < m_regFileIds.size(); ++r) {
    const auto &desc = m_trace.header.registerFiles[r];
    std::vector<quint64> values;
    values.reserve(desc.names.size());
    for (int i = 0; i < desc.names.size(); ++i)
      values.push_back(proc->getRegister(m_regFileIds[r], i));
    s.regs[desc.name] = std::move(values);
  }
  return s;
}

int TraceRecorder::stageIndex(unsigned lane, unsigned index) const {
  const auto &stages = m_trace.header.stages;
  for (size_t i = 0; i < stages.size(); ++i)
    if (stages[i].lane == lane && stages[i].index == index)
      return static_cast<int>(i);
  return -1;
}

std::vector<unsigned> TraceRecorder::lanes() const {
  std::set<unsigned> l;
  for (const auto &sd : m_trace.header.stages)
    l.insert(sd.lane);
  return {l.begin(), l.end()};
}

qint64 TraceRecorder::instanceInStage(const CycleState &s,
                                      const QString &stageName) const {
  const auto &stages = m_trace.header.stages;
  for (size_t i = 0; i < stages.size(); ++i)
    if (stages[i].name == stageName && s.stageSlots[i].valid)
      return s.stageSlots[i].instance;
  return -1;
}

qint64 TraceRecorder::newInstanceLocked(quint64 pc, qint64) {
  const qint64 id = m_nextInstance++;
  InstanceRec rec;
  rec.pc = pc;
  rec.assembly = disassembleLocked(pc);
  const auto word =
      static_cast<quint32>(ProcessorHandler::getMemory().readMemConst(pc, 4));
  rec.decoded = decodeRV(word, m_trace.header.xlen);
  m_instances[id] = rec;
  return id;
}

quint64 TraceRecorder::emitLocked(TraceEvent ev) {
  ev.sequence = m_nextSequence++;
  if (ev.instance >= 0) {
    auto it = m_instances.find(ev.instance);
    if (it != m_instances.end())
      it->second.lastSequence = ev.sequence;
  }
  m_trace.events.push_back(std::move(ev));
  return m_trace.events.back().sequence;
}

QString TraceRecorder::disassembleLocked(quint64 pc) {
  auto it = m_disasmCache.find(pc);
  if (it != m_disasmCache.end())
    return it->second;
  const QString text = ProcessorHandler::disassembleInstr(pc);
  m_disasmCache[pc] = text;
  return text;
}

QVariantMap TraceRecorder::decodedPayload(const RVDecoded &d) const {
  QVariantMap p;
  p["word"] = Trace::toHex(d.word, d.compressed ? 16 : 32);
  p["format"] = formatName(d.format);
  if (!d.valid || d.compressed)
    return p;
  p["mnemonic"] = d.mnemonic;
  p["opcode"] = Trace::toHex(d.opcode, 8);
  if (d.rdClass != RegClass::None)
    p["rd"] = d.rd;
  if (d.rs1Class != RegClass::None)
    p["rs1"] = d.rs1;
  if (d.rs2Class != RegClass::None)
    p["rs2"] = d.rs2;
  if (d.hasImm)
    p["imm"] = static_cast<qlonglong>(d.imm);
  return p;
}

void TraceRecorder::onProcessorReset() {
  {
    std::lock_guard lock(m_mutex);
    if (!m_enabled)
      return;
    beginRunLocked();
  }
  emit runStarted();
}

void TraceRecorder::onProcessorStateChanged() {
  if (ProcessorHandler::isRunning())
    return;
  std::lock_guard lock(m_mutex);
  if (!m_active || m_history.size() != 1)
    return;
  // ProcessorHandler applies register initializations (sp, gp, ...) after the
  // processor has been reset. Before the first clock, fold those values into
  // the initial state rather than reporting them as register writes.
  const auto *proc = ProcessorHandler::getProcessor();
  if (proc->getCycleCount() != m_trace.header.startCycle)
    return;
  const auto now = captureLocked();
  if (now.regs == m_last.regs)
    return;
  m_last.regs = now.regs;
  m_trace.header.initialRegisters = now.regs;
  if (m_snapshotEvent.has_value()) {
    QVariantMap regs;
    for (const auto &[rf, values] : now.regs) {
      QStringList hex;
      for (auto v : values)
        hex << Trace::toHex(v, m_trace.header.xlen);
      regs[rf] = hex;
    }
    m_trace.events[*m_snapshotEvent].payload["registers"] = regs;
  }
  ++m_revision;
}

void TraceRecorder::onProcessorClocked() {
  std::lock_guard lock(m_mutex);
  if (!m_active)
    return;
  CycleState cur = captureLocked();
  if (cur.cycle <= m_last.cycle)
    return;
  if (cur.cycle != m_last.cycle + 1) {
    // Cycles were executed without being observed; the trace can no longer
    // be continued consistently.
    m_truncated = true;
    m_truncationReason = "missed-cycles";
    m_active = false;
    ++m_revision;
    return;
  }
  if (cur.cycle - m_trace.header.startCycle > m_maxCycles) {
    m_truncated = true;
    m_truncationReason = "max-cycles";
    m_active = false;
    ++m_revision;
    return;
  }
  recordTransitionLocked(cur);
  m_history.push_back(cur.stageSlots);
  m_last = std::move(cur);
  ++m_revision;
}

void TraceRecorder::recordTransitionLocked(CycleState &cur) {
  const CycleState &prev = m_last;
  const auto &stages = m_trace.header.stages;
  const int nStages = static_cast<int>(stages.size());
  const qint64 cycle = cur.cycle;
  const QString modelRef = m_profile.sourceFile;

  auto baseEvent = [&](EventType type, int stage, qint64 instance) {
    TraceEvent ev;
    ev.cycle = cycle;
    ev.type = type;
    if (stage >= 0) {
      ev.component = stages[stage].component;
      ev.payload["stage"] = stages[stage].name;
      if (lanes().size() > 1)
        ev.payload["lane"] = stages[stage].lane;
    } else {
      ev.component = "soc.cpu0";
    }
    ev.instance = instance;
    if (instance >= 0) {
      const auto &rec = m_instances[instance];
      ev.pc = rec.pc;
      ev.assembly = rec.assembly;
    }
    ev.sourceRef = modelRef + "#stageInfo";
    return ev;
  };

  // Valid instances of the previous cycle, oldest first. Older instructions
  // occupy later stages.
  std::vector<int> prevOrder;
  for (int i = 0; i < nStages; ++i)
    if (prev.stageSlots[i].valid && prev.stageSlots[i].instance >= 0)
      prevOrder.push_back(i);
  std::stable_sort(prevOrder.begin(), prevOrder.end(), [&](int a, int b) {
    if (stages[a].index != stages[b].index)
      return stages[a].index > stages[b].index;
    return stages[a].lane < stages[b].lane;
  });

  // ---------------------------------------------------------------------
  // 1. Retirement. The model reports how many instructions retired during
  // this clock. Attribute them to the oldest valid instructions, preferring
  // those occupying the last stage of a lane.
  const long long retiredCount =
      std::max<long long>(0, cur.retired - prev.retired);
  std::vector<int> retireStages;
  for (int idx : prevOrder) {
    const auto &sd = stages[idx];
    if (stageIndex(sd.lane, sd.index + 1) < 0 &&
        static_cast<long long>(retireStages.size()) < retiredCount)
      retireStages.push_back(idx);
  }
  for (int idx : prevOrder) {
    if (static_cast<long long>(retireStages.size()) >= retiredCount)
      break;
    if (std::find(retireStages.begin(), retireStages.end(), idx) ==
        retireStages.end())
      retireStages.push_back(idx);
  }

  // Instances that left the pipeline by retiring this cycle.
  std::set<qint64> gone;
  for (int idx : retireStages)
    gone.insert(prev.stageSlots[idx].instance);

  // ---------------------------------------------------------------------
  // 2. Instance correlation. For each valid stage, decide whether its
  // instruction moved in from the previous stage, stayed (was held), or is
  // newly fetched. Stages are processed from the back of the pipeline to the
  // front so that a moving instruction is claimed before a stalled one.
  enum class How { None, Moved, Stayed, New };
  std::vector<How> how(nStages, How::None);
  std::set<qint64> claimed(gone);
  auto tryClaim = [&](const StageSlot &p, quint64 pc) -> qint64 {
    if (p.valid && p.instance >= 0 && p.pc == pc && !claimed.count(p.instance))
      return p.instance;
    return -1;
  };

  unsigned maxIndex = 0;
  for (const auto &sd : stages)
    maxIndex = std::max(maxIndex, sd.index);
  const auto laneList = lanes();

  for (int s = static_cast<int>(maxIndex); s >= 0; --s) {
    std::vector<int> atStage;
    for (int i = 0; i < nStages; ++i)
      if (static_cast<int>(stages[i].index) == s && cur.stageSlots[i].valid)
        atStage.push_back(i);
    auto assign = [&](int i, qint64 inst, How h) {
      cur.stageSlots[i].instance = inst;
      how[i] = h;
      claimed.insert(inst);
    };
    // Same-lane move from the previous stage.
    if (s > 0)
      for (int i : atStage) {
        const int p = stageIndex(stages[i].lane, s - 1);
        const qint64 inst =
            p >= 0 ? tryClaim(prev.stageSlots[p], cur.stageSlots[i].pc) : -1;
        if (inst >= 0)
          assign(i, inst, How::Moved);
      }
    // Cross-lane move (multi-issue models).
    if (s > 0)
      for (int i : atStage) {
        if (how[i] != How::None)
          continue;
        for (unsigned lane : laneList) {
          const int p = stageIndex(lane, s - 1);
          const qint64 inst =
              p >= 0 ? tryClaim(prev.stageSlots[p], cur.stageSlots[i].pc) : -1;
          if (inst >= 0) {
            assign(i, inst, How::Moved);
            break;
          }
        }
      }
    // Held in the same stage.
    for (int i : atStage) {
      if (how[i] != How::None)
        continue;
      const qint64 inst = tryClaim(prev.stageSlots[i], cur.stageSlots[i].pc);
      if (inst >= 0)
        assign(i, inst, How::Stayed);
    }
    // Newly observed instruction.
    for (int i : atStage) {
      if (how[i] != How::None)
        continue;
      assign(i, newInstanceLocked(cur.stageSlots[i].pc, cycle), How::New);
    }
  }

  // Register file differences between the two cycles: (file index, register).
  std::vector<std::pair<size_t, size_t>> regDiffs;
  for (size_t r = 0; r < m_trace.header.registerFiles.size(); ++r) {
    const auto &name = m_trace.header.registerFiles[r].name;
    const auto &before = prev.regs.at(name);
    const auto &after = cur.regs.at(name);
    for (size_t i = 0; i < after.size() && i < before.size(); ++i)
      if (after[i] != before[i])
        regDiffs.push_back({r, i});
  }
  auto regClassOf = [&](size_t r) {
    return m_trace.header.registerFiles[r].name == "fpr" ? RegClass::FPR
                                                         : RegClass::GPR;
  };

  // An instruction that left the last stage of its lane without being
  // counted by the model's retirement counter, while its destination
  // register changed, did retire architecturally. Report it as an inferred
  // retirement instead of a squash (see docs/SIMULATION_FIDELITY.md).
  std::vector<int> inferredRetireStages;
  for (int idx : prevOrder) {
    const qint64 inst = prev.stageSlots[idx].instance;
    const auto &sd = stages[idx];
    if (claimed.count(inst) || stageIndex(sd.lane, sd.index + 1) >= 0)
      continue;
    const auto &d = m_instances[inst].decoded;
    if (!d.writesRd())
      continue;
    for (const auto &[r, i] : regDiffs)
      if (regClassOf(r) == d.rdClass && i == d.rd) {
        inferredRetireStages.push_back(idx);
        claimed.insert(inst);
        break;
      }
  }

  std::vector<qint64> retired;
  std::map<qint64, quint64> retireSeq;
  for (long long j = 0; j < retiredCount; ++j) {
    TraceEvent ev;
    if (j < static_cast<long long>(retireStages.size())) {
      const int idx = retireStages[j];
      ev = baseEvent(EventType::InstructionRetire, idx,
                     prev.stageSlots[idx].instance);
    } else {
      ev = baseEvent(EventType::InstructionRetire, -1, -1);
      ev.observability = Observability::Inferred;
      ev.payload["attribution"] = "unknown";
    }
    ev.payload["retire_index"] = static_cast<qlonglong>(prev.retired + j + 1);
    ev.architectural = true;
    ev.sourceRef = modelRef + "#clockProcessor";
    const quint64 seq = emitLocked(ev);
    if (ev.instance >= 0) {
      retired.push_back(ev.instance);
      retireSeq[ev.instance] = seq;
    }
  }
  for (int idx : inferredRetireStages) {
    auto ev = baseEvent(EventType::InstructionRetire, idx,
                        prev.stageSlots[idx].instance);
    ev.payload["attribution"] = "inferred-from-register-write";
    ev.payload["model_retire_counter"] = "did not count this instruction";
    ev.observability = Observability::Inferred;
    ev.architectural = true;
    ev.sourceRef = modelRef + "#getInstructionsRetired";
    const quint64 seq = emitLocked(ev);
    retired.push_back(ev.instance);
    retireSeq[ev.instance] = seq;
    ++m_inferredRetirements;
  }

  // Emit stage events from the back of the pipeline to the front.
  std::vector<int> order(nStages);
  for (int i = 0; i < nStages; ++i)
    order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    if (stages[a].index != stages[b].index)
      return stages[a].index > stages[b].index;
    return stages[a].lane < stages[b].lane;
  });

  for (int i : order) {
    const auto &slot = cur.stageSlots[i];
    const auto &sd = stages[i];
    if (slot.valid) {
      const qint64 inst = slot.instance;
      switch (how[i]) {
      case How::Moved: {
        auto ev = baseEvent(EventType::StageEnter, i, inst);
        emitLocked(ev);
        if (!m_profile.decodeStage.isEmpty() &&
            sd.name == m_profile.decodeStage) {
          auto dec = baseEvent(EventType::InstructionDecode, i, inst);
          dec.payload.insert(decodedPayload(m_instances[inst].decoded));
          dec.observability = Observability::IsaDecoded;
          dec.sourceRef = "src/explorer/riscvdecoder.cpp#decodeRV";
          emitLocked(dec);
        }
        break;
      }
      case How::Stayed: {
        auto ev = baseEvent(EventType::PipelineStall, i, inst);
        ev.payload["kind"] = "held";
        emitLocked(ev);
        break;
      }
      case How::New: {
        if (sd.index == 0) {
          auto f = baseEvent(EventType::InstructionFetch, i, inst);
          f.payload["word"] =
              Trace::toHex(m_instances[inst].decoded.word,
                           m_instances[inst].decoded.compressed ? 16 : 32);
          emitLocked(f);
          emitLocked(baseEvent(EventType::StageEnter, i, inst));
        } else {
          auto ev = baseEvent(EventType::StageEnter, i, inst);
          ev.observability = Observability::Inferred;
          ev.payload["note"] = "instruction first observed in this stage";
          emitLocked(ev);
        }
        break;
      }
      case How::None:
        break;
      }
      continue;
    }

    // Empty stage: report bubbles that the model labels as stalled/flushed.
    if (slot.state == StageInfo::State::Stalled ||
        slot.state == StageInfo::State::Flushed) {
      QString origin = "inserted";
      if (sd.index > 0) {
        const int p = stageIndex(sd.lane, sd.index - 1);
        if (p >= 0 && !prev.stageSlots[p].valid &&
            prev.stageSlots[p].state == slot.state)
          origin = "propagated";
      }
      auto ev = baseEvent(slot.state == StageInfo::State::Stalled
                              ? EventType::PipelineStall
                              : EventType::PipelineFlush,
                          i, -1);
      ev.payload["kind"] = "bubble";
      ev.payload["origin"] = origin;
      emitLocked(ev);
    }
  }

  // ---------------------------------------------------------------------
  // 3. Squashed instructions: valid in the previous cycle, neither retired
  // nor present anymore.
  for (int idx : prevOrder) {
    const qint64 inst = prev.stageSlots[idx].instance;
    if (claimed.count(inst))
      continue;
    auto ev = baseEvent(EventType::PipelineFlush, idx, inst);
    ev.payload["kind"] = "squash";
    ev.payload["reason"] = "removed from the pipeline without retiring";
    // Likely cause: the nearest older control-flow instruction in the
    // pipeline (observability: inferred).
    const auto &sd = stages[idx];
    for (int c : prevOrder) {
      const auto &csd = stages[c];
      if (csd.lane != sd.lane || csd.index <= sd.index)
        continue;
      const auto &crec = m_instances[prev.stageSlots[c].instance];
      if (crec.decoded.isBranch || crec.decoded.isJump ||
          crec.decoded.isSystem) {
        ev.payload["cause_instance"] =
            Trace::instanceName(prev.stageSlots[c].instance);
        ev.payload["cause"] =
            crec.decoded.isSystem ? "system call" : "control-flow change";
        ev.causeSequence = crec.lastSequence;
      }
    }
    ev.observability = Observability::Inferred;
    emitLocked(ev);
  }

  // ---------------------------------------------------------------------
  // 4. Register writes, detected by comparing register files.
  for (size_t r = 0; r < m_trace.header.registerFiles.size(); ++r) {
    const auto &desc = m_trace.header.registerFiles[r];
    const auto &before = prev.regs.at(desc.name);
    const auto &after = cur.regs.at(desc.name);
    const RegClass cls = desc.name == "fpr" ? RegClass::FPR : RegClass::GPR;
    for (size_t i = 0; i < after.size() && i < before.size(); ++i) {
      if (after[i] == before[i])
        continue;
      std::vector<qint64> exact, unknown;
      for (qint64 inst : retired) {
        const auto &d = m_instances[inst].decoded;
        if (d.compressed)
          unknown.push_back(inst);
        else if (d.writesRd() && d.rdClass == cls && d.rd == i)
          exact.push_back(inst);
      }
      qint64 owner = -1;
      Observability obs = Observability::StateDiff;
      QString attribution = "retiring-instruction";
      if (exact.size() == 1) {
        owner = exact.front();
      } else if (exact.empty() && unknown.size() == 1) {
        owner = unknown.front();
        obs = Observability::Inferred;
        attribution = "retiring-compressed-instruction";
      } else {
        attribution = "unexplained";
        ++m_unattributedWrites;
      }
      TraceEvent ev;
      ev.cycle = cycle;
      ev.type = EventType::RegisterWrite;
      ev.component = "soc.cpu0.regfile." + desc.name;
      ev.instance = owner;
      if (owner >= 0) {
        ev.pc = m_instances[owner].pc;
        ev.assembly = m_instances[owner].assembly;
        ev.causeSequence = retireSeq[owner];
      }
      ev.payload["regfile"] = desc.name;
      ev.payload["index"] = static_cast<int>(i);
      ev.payload["name"] = desc.names.value(static_cast<int>(i));
      ev.payload["alias"] = desc.aliases.value(static_cast<int>(i));
      ev.payload["value"] = Trace::toHex(after[i], m_trace.header.xlen);
      ev.payload["previous"] = Trace::toHex(before[i], m_trace.header.xlen);
      ev.payload["attribution"] = attribution;
      ev.observability = obs;
      ev.architectural = true;
      ev.sourceRef = modelRef + "#getRegister";
      emitLocked(ev);
    }
  }

  // ---------------------------------------------------------------------
  // 5. Data memory accesses.
  auto &mem = ProcessorHandler::getMemory();
  auto sample = [&](quint64 address, unsigned bytes, QVariantMap &payload) {
    const bool io =
        mem.regionType(address) == vsrtl::core::AddressSpace::RegionType::IO;
    payload["region"] = io ? "mmio" : "memory";
    if (io) {
      m_sawMmio = true;
      payload["value"] = QVariant();
    } else {
      payload["value"] =
          Trace::toHex(mem.readMemConst(address, bytes), bytes * 8);
    }
    return io;
  };

  for (const auto &pw : m_pendingWrites) {
    TraceEvent ev;
    ev.cycle = cycle;
    ev.type = EventType::MemoryResponse;
    ev.instance = pw.instance;
    if (pw.instance >= 0) {
      ev.pc = m_instances[pw.instance].pc;
      ev.assembly = m_instances[pw.instance].assembly;
    }
    ev.payload["access"] = "write";
    ev.payload["address"] = Trace::toHex(pw.address, m_trace.header.xlen);
    ev.payload["bytes"] = pw.bytes;
    ev.payload["timing"] = "committed at the preceding clock edge";
    const bool io = sample(pw.address, pw.bytes, ev.payload);
    ev.component = io ? "soc.mmio" : "soc.mem.data";
    ev.causeSequence = pw.requestSequence;
    ev.observability = Observability::Direct;
    ev.architectural = true;
    ev.sourceRef = modelRef + "#dataMemAccess";
    emitLocked(ev);
  }
  m_pendingWrites.clear();

  const auto access = ProcessorHandler::getProcessor()->dataMemAccess();
  if (access.type != MemoryAccess::None) {
    qint64 owner = -1;
    if (m_profile.memAccessReportedAfterExecution) {
      if (retired.size() == 1)
        owner = retired.front();
    } else if (!m_profile.memoryStage.isEmpty()) {
      owner = instanceInStage(cur, m_profile.memoryStage);
    } else {
      // Single-cycle or multi-cycle model: the only valid instruction.
      for (int i = 0; i < nStages; ++i)
        if (cur.stageSlots[i].valid) {
          owner = cur.stageSlots[i].instance;
          break;
        }
    }
    TraceEvent req;
    req.cycle = cycle;
    req.type = EventType::MemoryRequest;
    req.component = "soc.cpu0.lsu";
    req.instance = owner;
    if (owner >= 0) {
      req.pc = m_instances[owner].pc;
      req.assembly = m_instances[owner].assembly;
    }
    req.payload["access"] = accessName(access.type);
    req.payload["address"] = Trace::toHex(access.address, m_trace.header.xlen);
    req.payload["bytes"] = access.bytes;
    req.payload["timing"] =
        m_profile.memAccessReportedAfterExecution
            ? "performed while executing the instruction"
            : "presented on the data memory port in this cycle";
    req.payload["region"] = mem.regionType(access.address) ==
                                    vsrtl::core::AddressSpace::RegionType::IO
                                ? "mmio"
                                : "memory";
    req.observability = Observability::Direct;
    req.architectural = false;
    req.sourceRef = modelRef + "#dataMemAccess";
    const quint64 reqSeq = emitLocked(req);

    const bool respondNow = access.type == MemoryAccess::Read ||
                            m_profile.memAccessReportedAfterExecution;
    if (respondNow) {
      TraceEvent resp = req;
      resp.type = EventType::MemoryResponse;
      resp.payload.remove("timing");
      const bool io = sample(access.address, access.bytes, resp.payload);
      resp.component = io ? "soc.mmio" : "soc.mem.data";
      resp.causeSequence = reqSeq;
      resp.architectural = access.type == MemoryAccess::Write;
      if (access.type == MemoryAccess::Read)
        resp.payload["timing"] = "combinational read (zero-latency memory)";
      emitLocked(resp);
    } else {
      m_pendingWrites.push_back({access.address, access.bytes, owner, reqSeq});
    }
  }
}

void TraceRecorder::onProcessorReversed() {
  bool restarted = false;
  {
    std::lock_guard lock(m_mutex);
    if (!m_enabled)
      return;
    const qint64 cycle = ProcessorHandler::getProcessor()->getCycleCount();
    if (cycle < m_trace.header.startCycle) {
      beginRunLocked();
      restarted = true;
    } else {
      auto &events = m_trace.events;
      auto it = std::find_if(
          events.begin(), events.end(),
          [cycle](const TraceEvent &e) { return e.cycle > cycle; });
      events.erase(it, events.end());
      const size_t keep =
          static_cast<size_t>(cycle - m_trace.header.startCycle) + 1;
      if (m_history.size() > keep)
        m_history.resize(keep);
      rebuildAfterTruncationLocked();
    }
  }
  if (restarted)
    emit runStarted();
}

void TraceRecorder::rebuildAfterTruncationLocked() {
  const auto &events = m_trace.events;
  m_nextSequence = events.empty() ? 0 : events.back().sequence + 1;

  qint64 maxInstance = 0;
  long long modelRetired = m_initialRetired;
  m_unattributedWrites = 0;
  m_inferredRetirements = 0;
  for (const auto &ev : events) {
    maxInstance = std::max(maxInstance, ev.instance);
    if (ev.type == EventType::RegisterWrite && ev.instance < 0)
      ++m_unattributedWrites;
    if (ev.type == EventType::InstructionRetire) {
      if (ev.payload.contains("retire_index"))
        ++modelRetired;
      else
        ++m_inferredRetirements;
    }
  }
  m_instances.erase(m_instances.upper_bound(maxInstance), m_instances.end());
  m_nextInstance = maxInstance + 1;
  for (const auto &ev : events)
    if (ev.instance >= 0)
      m_instances[ev.instance].lastSequence = ev.sequence;

  CycleState now = captureLocked();
  const auto &recorded = m_history.back();
  bool consistent = recorded.size() == now.stageSlots.size();
  for (size_t i = 0; consistent && i < recorded.size(); ++i) {
    consistent &=
        recorded[i].valid == now.stageSlots[i].valid &&
        (!recorded[i].valid || recorded[i].pc == now.stageSlots[i].pc);
    now.stageSlots[i].instance = recorded[i].instance;
  }
  if (!consistent) {
    beginRunLocked();
    return;
  }
  // VSRTL emits its "reversed" signal before some models have rolled back
  // their retirement counter, so derive the counter from the trace itself.
  now.retired = modelRetired;
  m_last = std::move(now);

  m_pendingWrites.clear();
  if (!m_profile.memAccessReportedAfterExecution)
    for (const auto &ev : events)
      if (ev.cycle == m_last.cycle && ev.type == EventType::MemoryRequest &&
          ev.payload.value("access").toString() == "write")
        m_pendingWrites.push_back(
            {Trace::fromHex(ev.payload.value("address").toString()).value_or(0),
             ev.payload.value("bytes").toUInt(), ev.instance, ev.sequence});

  if (m_truncated && m_truncationReason == "max-cycles" &&
      m_last.cycle - m_trace.header.startCycle < m_maxCycles) {
    m_truncated = false;
    m_truncationReason.clear();
    m_active = m_enabled;
  }
  ++m_revision;
}

} // namespace Explorer
} // namespace Ripes
