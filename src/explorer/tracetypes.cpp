// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "tracetypes.h"

#include <utility>

namespace Ripes {
namespace Trace {

namespace {
const std::vector<std::pair<EventType, const char *>> &eventTypeNames() {
  static const std::vector<std::pair<EventType, const char *>> names = {
      {EventType::StateSnapshot, "state.snapshot"},
      {EventType::InstructionFetch, "instruction.fetch"},
      {EventType::InstructionDecode, "instruction.decode"},
      {EventType::InstructionRetire, "instruction.retire"},
      {EventType::StageEnter, "pipeline.stage_enter"},
      {EventType::PipelineStall, "pipeline.stall"},
      {EventType::PipelineFlush, "pipeline.flush"},
      {EventType::RegisterWrite, "register.write"},
      {EventType::MemoryRequest, "memory.request"},
      {EventType::MemoryResponse, "memory.response"},
      {EventType::CacheHit, "cache.hit"},
      {EventType::CacheMiss, "cache.miss"},
      {EventType::BusTransfer, "bus.transfer"},
      {EventType::DmaStart, "dma.start"},
      {EventType::DmaComplete, "dma.complete"},
      {EventType::AcceleratorDispatch, "accelerator.dispatch"},
      {EventType::AcceleratorComplete, "accelerator.complete"},
      {EventType::InterruptRaise, "interrupt.raise"},
  };
  return names;
}

const std::vector<std::pair<Observability, const char *>> &
observabilityNames() {
  static const std::vector<std::pair<Observability, const char *>> names = {
      {Observability::Direct, "direct"},
      {Observability::ModelDerived, "model-derived"},
      {Observability::StateDiff, "state-diff"},
      {Observability::Inferred, "inferred"},
      {Observability::IsaDecoded, "isa-decoded"},
  };
  return names;
}
} // namespace

QString eventTypeName(EventType type) {
  for (const auto &[t, name] : eventTypeNames())
    if (t == type)
      return name;
  Q_UNREACHABLE();
}

std::optional<EventType> eventTypeFromName(const QString &name) {
  for (const auto &[t, n] : eventTypeNames())
    if (name == QLatin1String(n))
      return t;
  return std::nullopt;
}

const std::vector<EventType> &allEventTypes() {
  static const std::vector<EventType> types = [] {
    std::vector<EventType> v;
    for (const auto &it : eventTypeNames())
      v.push_back(it.first);
    return v;
  }();
  return types;
}

QString observabilityName(Observability o) {
  for (const auto &[t, name] : observabilityNames())
    if (t == o)
      return name;
  Q_UNREACHABLE();
}

std::optional<Observability> observabilityFromName(const QString &name) {
  for (const auto &[t, n] : observabilityNames())
    if (name == QLatin1String(n))
      return t;
  return std::nullopt;
}

QString toHex(quint64 value, unsigned bits) {
  const int digits = static_cast<int>((bits + 3) / 4);
  if (bits < 64)
    value &= (quint64(1) << bits) - 1;
  return "0x" + QString::number(value, 16).rightJustified(digits, '0');
}

std::optional<quint64> fromHex(const QString &str) {
  bool ok = false;
  quint64 value = 0;
  if (str.startsWith("0x") || str.startsWith("0X"))
    value = str.mid(2).toULongLong(&ok, 16);
  else
    value = str.toULongLong(&ok, 10);
  if (!ok)
    return std::nullopt;
  return value;
}

QString instanceName(qint64 instance) {
  return "inst-" + QString::number(instance);
}

qint64 instanceFromName(const QString &name) {
  if (!name.startsWith("inst-"))
    return -1;
  bool ok = false;
  const qint64 v = name.mid(5).toLongLong(&ok);
  return ok ? v : -1;
}

} // namespace Trace
} // namespace Ripes
