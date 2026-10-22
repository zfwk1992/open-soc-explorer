// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "tracereplay.h"

#include <algorithm>
#include <numeric>

namespace Ripes {
namespace Explorer {

using Trace::EventType;

namespace {
bool isConditionalBranch(const QString &mnemonic) {
  static const std::set<QString> branches = {"beq", "bne",  "blt",
                                             "bge", "bltu", "bgeu"};
  return branches.count(mnemonic) != 0;
}
} // namespace

TraceReplay::TraceReplay(Trace::Trace trace) : m_trace(std::move(trace)) {
  build();
}

void TraceReplay::build() {
  const auto &h = m_trace.header;
  auto &events = m_trace.events;
  std::stable_sort(events.begin(), events.end(),
                   [](const Trace::TraceEvent &a, const Trace::TraceEvent &b) {
                     if (a.cycle != b.cycle)
                       return a.cycle < b.cycle;
                     return a.sequence < b.sequence;
                   });

  m_firstCycle = h.startCycle;
  m_lastCycle = std::max(m_firstCycle, m_trace.completeness.endCycle);
  if (!events.empty())
    m_lastCycle = std::max(m_lastCycle, events.back().cycle);

  for (size_t i = 0; i < h.stages.size(); ++i)
    m_stageByComponent[h.stages[i].component] = static_cast<int>(i);

  const size_t nCycles = static_cast<size_t>(m_lastCycle - m_firstCycle + 1);
  const size_t nStages = h.stages.size();
  m_occupancy.assign(nCycles, std::vector<Slot>(nStages));
  for (auto &row : m_occupancy)
    for (size_t s = 0; s < nStages; ++s)
      row[s].stage = static_cast<int>(s);
  m_eventRange.assign(nCycles, {events.size(), events.size()});

  auto stageOfEvent = [&](const Trace::TraceEvent &ev) {
    auto it = m_stageByComponent.find(ev.component);
    return it == m_stageByComponent.end() ? -1 : it->second;
  };
  auto rel = [&](qint64 cycle) {
    return static_cast<size_t>(std::clamp(cycle, m_firstCycle, m_lastCycle) -
                               m_firstCycle);
  };

  for (size_t i = 0; i < events.size(); ++i) {
    const auto &ev = events[i];
    auto &range = m_eventRange[rel(ev.cycle)];
    if (range.first == events.size())
      range.first = i;
    range.second = i + 1;

    Instance *inst = nullptr;
    if (ev.instance >= 0) {
      auto [it, inserted] = m_instances.try_emplace(ev.instance);
      inst = &it->second;
      if (inserted) {
        inst->id = ev.instance;
        inst->firstCycle = ev.cycle;
      }
      if (ev.pc.has_value())
        inst->pc = *ev.pc;
      if (!ev.assembly.isEmpty())
        inst->assembly = ev.assembly;
      inst->events.push_back(i);
    }

    const QString kind = ev.payload.value("kind").toString();
    switch (ev.type) {
    case EventType::StageEnter:
      if (inst && stageOfEvent(ev) >= 0)
        inst->stageEntries.push_back({ev.cycle, stageOfEvent(ev)});
      break;
    case EventType::PipelineStall:
      if (kind == "held" && inst)
        inst->heldCycles.push_back(ev.cycle);
      else if (kind == "bubble" && stageOfEvent(ev) >= 0)
        m_occupancy[rel(ev.cycle)][stageOfEvent(ev)].kind =
            SlotKind::StallBubble;
      break;
    case EventType::PipelineFlush:
      if (kind == "squash" && inst) {
        inst->outcome = Outcome::Squashed;
        inst->endCycle = ev.cycle;
      } else if (kind == "bubble" && stageOfEvent(ev) >= 0) {
        m_occupancy[rel(ev.cycle)][stageOfEvent(ev)].kind =
            SlotKind::FlushBubble;
      }
      break;
    case EventType::InstructionRetire:
      if (inst) {
        inst->outcome = Outcome::Retired;
        inst->endCycle = ev.cycle;
        inst->retireEvent = i;
        m_retirePosition[inst->id] = m_retireOrder.size();
        m_retireOrder.push_back(inst->id);
      }
      break;
    case EventType::RegisterWrite: {
      const auto value = Trace::fromHex(ev.payload.value("value").toString());
      if (value.has_value())
        m_regWrites.push_back({ev.cycle, ev.payload.value("regfile").toString(),
                               ev.payload.value("index").toInt(), *value,
                               ev.instance});
      break;
    }
    default:
      break;
    }
  }

  // Stage occupancy: an instance occupies a stage from its entry cycle until
  // the cycle before it enters the next stage, retires or is squashed.
  for (const auto &[id, inst] : m_instances) {
    for (size_t k = 0; k < inst.stageEntries.size(); ++k) {
      const qint64 start = inst.stageEntries[k].first;
      qint64 end = m_lastCycle;
      if (k + 1 < inst.stageEntries.size())
        end = inst.stageEntries[k + 1].first - 1;
      else if (inst.endCycle >= 0)
        end = inst.endCycle - 1;
      for (qint64 c = std::max(start, m_firstCycle);
           c <= std::min(end, m_lastCycle); ++c) {
        auto &slot = m_occupancy[rel(c)][inst.stageEntries[k].second];
        ++slot.occupants;
        // Instances are visited in id (fetch) order: keep the oldest one.
        if (slot.kind != SlotKind::Instruction) {
          slot.kind = SlotKind::Instruction;
          slot.instance = id;
          slot.held = std::find(inst.heldCycles.begin(), inst.heldCycles.end(),
                                c) != inst.heldCycles.end();
        }
      }
    }
  }

  // Register checkpoints.
  RegisterState state = h.initialRegisters;
  // Register files without initial values (e.g. gem5 traces) start as zero;
  // registerKnownAt() tells which values are actually known.
  for (const auto &rf : h.registerFiles)
    if (!state.count(rf.name))
      state[rf.name] = std::vector<quint64>(rf.names.size(), 0);
  size_t w = 0;
  for (qint64 c = m_firstCycle; c <= m_lastCycle; ++c) {
    while (w < m_regWrites.size() && m_regWrites[w].cycle <= c) {
      const auto &rw = m_regWrites[w];
      auto &file = state[rw.regfile];
      if (rw.index >= 0 && static_cast<size_t>(rw.index) < file.size())
        file[rw.index] = rw.value;
      ++w;
    }
    if ((c - m_firstCycle) % kCheckpointInterval == 0)
      m_checkpoints.push_back(state);
  }
}

std::vector<TraceReplay::Slot> TraceReplay::pipelineAt(qint64 cycle) const {
  if (m_occupancy.empty())
    return {};
  cycle = std::clamp(cycle, m_firstCycle, m_lastCycle);
  return m_occupancy[cycle - m_firstCycle];
}

TraceReplay::RegisterState TraceReplay::registersAt(qint64 cycle) const {
  if (m_checkpoints.empty())
    return m_trace.header.initialRegisters;
  cycle = std::clamp(cycle, m_firstCycle, m_lastCycle);
  const qint64 cpIndex = (cycle - m_firstCycle) / kCheckpointInterval;
  const qint64 cpCycle = m_firstCycle + cpIndex * kCheckpointInterval;
  RegisterState state = m_checkpoints[cpIndex];
  auto it = std::upper_bound(
      m_regWrites.begin(), m_regWrites.end(), cpCycle,
      [](qint64 c, const RegWrite &rw) { return c < rw.cycle; });
  for (; it != m_regWrites.end() && it->cycle <= cycle; ++it) {
    auto &file = state[it->regfile];
    if (it->index >= 0 && static_cast<size_t>(it->index) < file.size())
      file[it->index] = it->value;
  }
  return state;
}

bool TraceReplay::registerKnownAt(const QString &regfile, int index,
                                  qint64 cycle) const {
  const auto it = m_trace.header.initialRegisters.find(regfile);
  if (it != m_trace.header.initialRegisters.end() &&
      static_cast<size_t>(index) < it->second.size())
    return true;
  for (const auto &rw : m_regWrites) {
    if (rw.cycle > cycle)
      break;
    if (rw.regfile == regfile && rw.index == index)
      return true;
  }
  return false;
}

std::vector<size_t> TraceReplay::eventsAt(qint64 cycle) const {
  std::vector<size_t> result;
  if (cycle < m_firstCycle || cycle > m_lastCycle)
    return result;
  const auto &range = m_eventRange[cycle - m_firstCycle];
  for (size_t i = range.first; i < range.second; ++i)
    result.push_back(i);
  return result;
}

const TraceReplay::Instance *TraceReplay::instance(qint64 id) const {
  auto it = m_instances.find(id);
  return it == m_instances.end() ? nullptr : &it->second;
}

int TraceReplay::stageOf(qint64 id, qint64 cycle) const {
  const auto *inst = instance(id);
  if (!inst || cycle < m_firstCycle || cycle > m_lastCycle)
    return -1;
  // Same occupancy rule as in build(): from a stage entry until the cycle
  // before the next entry, retirement or squash.
  const auto &entries = inst->stageEntries;
  auto it = std::upper_bound(
      entries.begin(), entries.end(), cycle,
      [](qint64 c, const std::pair<qint64, int> &e) { return c < e.first; });
  if (it == entries.begin())
    return -1;
  const auto &entry = *(it - 1);
  qint64 end = m_lastCycle;
  if (it != entries.end())
    end = it->first - 1;
  else if (inst->endCycle >= 0)
    end = inst->endCycle - 1;
  return cycle <= end ? entry.second : -1;
}

bool TraceReplay::isHeld(qint64 id, qint64 cycle) const {
  const auto *inst = instance(id);
  return inst && std::find(inst->heldCycles.begin(), inst->heldCycles.end(),
                           cycle) != inst->heldCycles.end();
}

std::vector<qint64> TraceReplay::occupantsOf(int stage, qint64 cycle) const {
  std::vector<qint64> result;
  for (const auto &[id, inst] : m_instances)
    if (inst.firstCycle <= cycle && stageOf(id, cycle) == stage)
      result.push_back(id);
  return result;
}

bool TraceReplay::visibleBeforeRetire(const RegWrite &rw,
                                      const Instance &inst) const {
  if (rw.cycle < inst.endCycle)
    return true;
  if (rw.cycle > inst.endCycle || rw.instance < 0)
    return false;
  const auto writer = m_retirePosition.find(rw.instance);
  return writer != m_retirePosition.end() &&
         writer->second < m_retirePosition.at(inst.id);
}

std::optional<TraceReplay::RegisterState>
TraceReplay::registersBeforeRetire(qint64 id) const {
  const auto *inst = instance(id);
  if (!inst || inst->outcome != Outcome::Retired)
    return std::nullopt;
  RegisterState state = registersAt(inst->endCycle - 1);
  auto it = std::lower_bound(
      m_regWrites.begin(), m_regWrites.end(), inst->endCycle,
      [](const RegWrite &rw, qint64 c) { return rw.cycle < c; });
  for (; it != m_regWrites.end() && it->cycle == inst->endCycle; ++it) {
    auto &file = state[it->regfile];
    if (visibleBeforeRetire(*it, *inst) && it->index >= 0 &&
        static_cast<size_t>(it->index) < file.size())
      file[it->index] = it->value;
  }
  return state;
}

bool TraceReplay::registerKnownBeforeRetire(qint64 id, const QString &regfile,
                                            int index) const {
  const auto *inst = instance(id);
  if (!inst || inst->outcome != Outcome::Retired)
    return false;
  const auto it = m_trace.header.initialRegisters.find(regfile);
  if (it != m_trace.header.initialRegisters.end() &&
      static_cast<size_t>(index) < it->second.size())
    return true;
  for (const auto &rw : m_regWrites) {
    if (rw.cycle > inst->endCycle)
      break;
    if (rw.regfile == regfile && rw.index == index &&
        visibleBeforeRetire(rw, *inst))
      return true;
  }
  return false;
}

QString TraceReplay::mnemonicOf(qint64 id) const {
  const auto *inst = instance(id);
  if (!inst)
    return {};
  return inst->assembly.section(' ', 0, 0).toLower();
}

TraceReplay::Stats TraceReplay::stats() const {
  Stats s;
  s.cycles = m_lastCycle - m_firstCycle;
  std::set<qint64> stallCycles;
  for (const auto &ev : m_trace.events) {
    const QString kind = ev.payload.value("kind").toString();
    const bool inserted = ev.payload.value("origin").toString() == "inserted";
    switch (ev.type) {
    case EventType::PipelineStall:
      if (kind == "held") {
        stallCycles.insert(ev.cycle);
      } else if (kind == "bubble" && inserted) {
        ++s.stallBubbles;
        stallCycles.insert(ev.cycle);
      }
      break;
    case EventType::PipelineFlush:
      if (kind == "squash")
        ++s.squashed;
      else if (kind == "bubble" && inserted)
        ++s.flushBubbles;
      break;
    case EventType::RegisterWrite:
      ++s.registerWrites;
      if (ev.instance < 0)
        ++s.unattributedRegisterWrites;
      break;
    case EventType::MemoryRequest:
      if (ev.payload.value("access").toString() == "write")
        ++s.memoryWrites;
      else
        ++s.memoryReads;
      break;
    default:
      break;
    }
  }
  s.stallCycles = stallCycles.size();
  s.retired = m_retireOrder.size();
  for (size_t i = 0; i < m_retireOrder.size(); ++i) {
    const QString mn = mnemonicOf(m_retireOrder[i]);
    ++s.retiredMnemonics[mn];
    if (isConditionalBranch(mn)) {
      ++s.retiredBranches;
      if (i + 1 < m_retireOrder.size()) {
        const auto *cur = instance(m_retireOrder[i]);
        const auto *next = instance(m_retireOrder[i + 1]);
        if (next->pc != cur->pc + 4)
          ++s.retiredTakenBranches;
      }
    }
  }
  if (s.retired > 0)
    s.cpi = static_cast<double>(s.cycles) / static_cast<double>(s.retired);
  return s;
}

} // namespace Explorer
} // namespace Ripes
