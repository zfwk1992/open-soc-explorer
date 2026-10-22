// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "tracejson.h"

#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace Ripes {
namespace Trace {

namespace {

QJsonValue optHex(const std::optional<quint64> &v, unsigned bits) {
  return v.has_value() ? QJsonValue(toHex(*v, bits)) : QJsonValue();
}

QJsonObject sourceToJson(const TraceSource &s, bool full) {
  QJsonObject o;
  o["engine"] = s.engine;
  o["model"] = s.model;
  o["fidelity"] = s.fidelity;
  if (full) {
    o["engine_version"] = s.engineVersion;
    o["adapter"] = s.adapter;
    o["adapter_version"] = s.adapterVersion;
    o["model_name"] = s.modelName;
    o["isa"] = s.isa;
    o["extensions"] = QJsonArray::fromStringList(s.extensions);
    o["fidelity_note"] = s.fidelityNote;
    o["source_file"] = s.sourceFile;
  }
  return o;
}

TraceSource sourceFromJson(const QJsonObject &o) {
  TraceSource s;
  s.engine = o["engine"].toString();
  s.engineVersion = o["engine_version"].toString();
  s.adapter = o["adapter"].toString();
  s.adapterVersion = o["adapter_version"].toString();
  s.model = o["model"].toString();
  s.modelName = o["model_name"].toString();
  s.isa = o["isa"].toString();
  for (const auto &e : o["extensions"].toArray())
    s.extensions << e.toString();
  s.fidelity = o["fidelity"].toString();
  s.fidelityNote = o["fidelity_note"].toString();
  s.sourceFile = o["source_file"].toString();
  return s;
}

QStringList toStringList(const QJsonValue &v) {
  QStringList l;
  for (const auto &e : v.toArray())
    l << e.toString();
  return l;
}

bool checkSchema(const QJsonObject &o, QString *error) {
  const QString v = o["schema_version"].toString();
  if (v.section('.', 0, 0) != QString(kSchemaVersion).section('.', 0, 0)) {
    if (error)
      *error = "Unsupported trace schema version '" + v + "' (expected " +
               kSchemaVersion + ")";
    return false;
  }
  return true;
}

} // namespace

std::map<qint64, std::optional<bool>> speculativeStatus(const Trace &trace) {
  std::map<qint64, std::optional<bool>> status;
  for (const auto &ev : trace.events) {
    if (ev.instance < 0)
      continue;
    auto &s = status[ev.instance];
    if (ev.type == EventType::InstructionRetire)
      s = false;
    else if (ev.type == EventType::PipelineFlush &&
             ev.payload.value("kind").toString() == "squash")
      s = true;
  }
  return status;
}

QJsonObject headerToJson(const Trace &trace) {
  const auto &h = trace.header;
  QJsonObject o;
  o["record"] = "header";
  o["schema_version"] = h.schemaVersion;
  o["run_id"] = h.runId;
  o["created_utc"] = h.createdUtc;
  o["source"] = sourceToJson(h.source, true);

  QJsonObject domain;
  domain["name"] = h.clockDomain;
  domain["unit"] = "cycle";
  domain["description"] =
      h.clockDomainDescription.isEmpty()
          ? QString("One clock() of the simulated model. Not comparable to "
                    "wall-clock time, gem5 ticks or cycles of other engines.")
          : h.clockDomainDescription;
  if (h.ticksPerCycle > 0) {
    domain["ticks_per_cycle"] = h.ticksPerCycle;
    domain["tick_unit"] = h.tickUnit;
  }
  o["time_domains"] = QJsonArray{domain};

  QJsonObject program;
  program["entry_point"] = toHex(h.entryPoint, h.xlen);
  program["text_start"] = toHex(h.textStart, h.xlen);
  program["text_size"] = static_cast<qint64>(h.textSize);
  program["image_sha256"] = h.programHash;
  o["program"] = program;

  QJsonObject processor;
  processor["xlen"] = static_cast<int>(h.xlen);
  QJsonArray stages;
  for (const auto &s : h.stages) {
    QJsonObject so;
    so["lane"] = static_cast<int>(s.lane);
    so["index"] = static_cast<int>(s.index);
    so["name"] = s.name;
    so["component"] = s.component;
    stages.append(so);
  }
  processor["stages"] = stages;
  QJsonArray regFiles;
  for (const auto &rf : h.registerFiles) {
    QJsonObject ro;
    ro["name"] = rf.name;
    ro["names"] = QJsonArray::fromStringList(rf.names);
    ro["aliases"] = QJsonArray::fromStringList(rf.aliases);
    regFiles.append(ro);
  }
  processor["register_files"] = regFiles;
  o["processor"] = processor;

  QJsonObject init;
  init["cycle"] = h.startCycle;
  init["pc"] = toHex(h.initialPc, h.xlen);
  QJsonObject regs;
  for (const auto &[rf, values] : h.initialRegisters) {
    QJsonArray a;
    for (auto v : values)
      a.append(toHex(v, h.xlen));
    regs[rf] = a;
  }
  init["registers"] = regs;
  o["initial_state"] = init;

  if (!h.system.empty()) {
    QJsonArray system;
    for (const auto &c : h.system) {
      QJsonObject co;
      co["path"] = c.path;
      co["type"] = c.type;
      co["role"] = c.role;
      co["description"] = c.description;
      co["parameters"] = QJsonObject::fromVariantMap(c.parameters);
      co["statistics"] = QJsonObject::fromVariantMap(c.statistics);
      co["connections"] = QJsonArray::fromStringList(c.connections);
      system.append(co);
    }
    o["system"] = system;
  }
  return o;
}

QJsonObject eventToJson(const Trace &trace, const TraceEvent &ev,
                        const std::map<qint64, std::optional<bool>> &spec) {
  const auto &h = trace.header;
  QJsonObject o;
  o["record"] = "event";
  o["schema_version"] = h.schemaVersion;
  o["run_id"] = h.runId;
  o["sequence"] = static_cast<qint64>(ev.sequence);
  o["source"] = sourceToJson(h.source, false);

  QJsonObject time;
  time["domain"] = h.clockDomain;
  time["cycle"] = ev.cycle;
  time["tick"] = ev.tick.has_value() ? QJsonValue(*ev.tick) : QJsonValue();
  o["time"] = time;

  QJsonObject e;
  e["type"] = eventTypeName(ev.type);
  e["component"] = ev.component;
  e["instruction_id"] =
      ev.instance >= 0 ? QJsonValue(instanceName(ev.instance)) : QJsonValue();
  e["pc"] = optHex(ev.pc, h.xlen);
  e["assembly"] =
      ev.assembly.isEmpty() ? QJsonValue() : QJsonValue(ev.assembly);
  e["payload"] = QJsonObject::fromVariantMap(ev.payload);
  o["event"] = e;

  QJsonObject corr;
  corr["cause_sequence"] =
      ev.causeSequence.has_value()
          ? QJsonValue(static_cast<qint64>(*ev.causeSequence))
          : QJsonValue();
  o["correlation"] = corr;
  o["observability"] = observabilityName(ev.observability);

  QJsonObject state;
  state["architectural"] = ev.architectural;
  QJsonValue speculative;
  if (ev.instance >= 0) {
    auto it = spec.find(ev.instance);
    if (it != spec.end() && it->second.has_value())
      speculative = *it->second;
  } else {
    speculative = false;
  }
  state["speculative"] = speculative;
  o["state"] = state;
  o["source_ref"] = ev.sourceRef;
  return o;
}

QJsonObject footerToJson(const Trace &trace) {
  const auto &c = trace.completeness;
  QJsonObject o;
  o["record"] = "footer";
  o["schema_version"] = trace.header.schemaVersion;
  o["run_id"] = trace.header.runId;
  QJsonObject comp;
  comp["complete"] = trace.isComplete();
  comp["starts_at_reset"] = c.startsAtReset;
  comp["start_cycle"] = c.startCycle;
  comp["end_cycle"] = c.endCycle;
  comp["truncated"] = c.truncated;
  comp["truncation_reason"] = c.truncationReason.isEmpty()
                                  ? QJsonValue()
                                  : QJsonValue(c.truncationReason);
  comp["program_finished"] = c.programFinished;
  comp["caveats"] = QJsonArray::fromStringList(c.caveats);
  o["completeness"] = comp;
  QJsonObject summary;
  summary["cycles"] = c.endCycle - c.startCycle;
  summary["instructions_retired"] = static_cast<qint64>(c.instructionsRetired);
  summary["events"] = static_cast<qint64>(trace.events.size());
  o["summary"] = summary;
  return o;
}

bool writeJsonl(const Trace &trace, QIODevice &out, QString *error) {
  const auto spec = speculativeStatus(trace);
  auto writeLine = [&](const QJsonObject &o) {
    QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact);
    line.append('\n');
    return out.write(line) == line.size();
  };
  bool ok = writeLine(headerToJson(trace));
  for (const auto &ev : trace.events)
    ok = ok && writeLine(eventToJson(trace, ev, spec));
  ok = ok && writeLine(footerToJson(trace));
  if (!ok && error)
    *error = "Failed to write trace: " + out.errorString();
  return ok;
}

bool writeJsonlFile(const Trace &trace, const QString &path, QString *error) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error)
      *error = "Could not open '" + path + "' for writing: " + f.errorString();
    return false;
  }
  return writeJsonl(trace, f, error);
}

QByteArray toJsonl(const Trace &trace) {
  QByteArray data;
  QBuffer buffer(&data);
  buffer.open(QIODevice::WriteOnly);
  writeJsonl(trace, buffer);
  return data;
}

std::optional<Trace> readJsonl(QIODevice &in, QString *error) {
  Trace t;
  bool haveHeader = false;
  int lineNo = 0;
  auto fail = [&](const QString &msg) -> std::optional<Trace> {
    if (error)
      *error = QString("Line %1: %2").arg(lineNo).arg(msg);
    return std::nullopt;
  };

  while (!in.atEnd()) {
    const QByteArray line = in.readLine().trimmed();
    ++lineNo;
    if (line.isEmpty())
      continue;
    QJsonParseError perr;
    const auto doc = QJsonDocument::fromJson(line, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
      return fail("invalid JSON: " + perr.errorString());
    const QJsonObject o = doc.object();
    if (!checkSchema(o, error))
      return std::nullopt;
    const QString record = o["record"].toString();

    if (record == "header") {
      auto &h = t.header;
      h.schemaVersion = o["schema_version"].toString();
      h.runId = o["run_id"].toString();
      h.createdUtc = o["created_utc"].toString();
      h.source = sourceFromJson(o["source"].toObject());
      const auto domains = o["time_domains"].toArray();
      if (!domains.isEmpty()) {
        const auto d = domains.first().toObject();
        h.clockDomain = d["name"].toString();
        h.clockDomainDescription = d["description"].toString();
        h.ticksPerCycle = d["ticks_per_cycle"].toInteger();
        h.tickUnit = d["tick_unit"].toString();
      }
      const auto program = o["program"].toObject();
      h.entryPoint = fromHex(program["entry_point"].toString()).value_or(0);
      h.textStart = fromHex(program["text_start"].toString()).value_or(0);
      h.textSize = program["text_size"].toInteger();
      h.programHash = program["image_sha256"].toString();
      const auto processor = o["processor"].toObject();
      h.xlen = processor["xlen"].toInt(32);
      for (const auto &sv : processor["stages"].toArray()) {
        const auto so = sv.toObject();
        h.stages.push_back({static_cast<unsigned>(so["lane"].toInt()),
                            static_cast<unsigned>(so["index"].toInt()),
                            so["name"].toString(), so["component"].toString()});
      }
      for (const auto &rv : processor["register_files"].toArray()) {
        const auto ro = rv.toObject();
        h.registerFiles.push_back({ro["name"].toString(),
                                   toStringList(ro["names"]),
                                   toStringList(ro["aliases"])});
      }
      const auto init = o["initial_state"].toObject();
      h.startCycle = init["cycle"].toInteger();
      h.initialPc = fromHex(init["pc"].toString()).value_or(0);
      const auto regs = init["registers"].toObject();
      for (auto it = regs.begin(); it != regs.end(); ++it) {
        std::vector<quint64> values;
        for (const auto &v : it.value().toArray())
          values.push_back(fromHex(v.toString()).value_or(0));
        h.initialRegisters[it.key()] = values;
      }
      for (const auto &cv : o["system"].toArray()) {
        const auto co = cv.toObject();
        h.system.push_back({co["path"].toString(), co["type"].toString(),
                            co["role"].toString(), co["description"].toString(),
                            co["parameters"].toObject().toVariantMap(),
                            co["statistics"].toObject().toVariantMap(),
                            toStringList(co["connections"])});
      }
      haveHeader = true;
    } else if (record == "event") {
      if (!haveHeader)
        return fail("event before header");
      TraceEvent ev;
      ev.sequence = o["sequence"].toInteger();
      ev.cycle = o["time"].toObject()["cycle"].toInteger();
      const auto tick = o["time"].toObject()["tick"];
      if (!tick.isNull() && !tick.isUndefined())
        ev.tick = tick.toInteger();
      const auto e = o["event"].toObject();
      const auto type = eventTypeFromName(e["type"].toString());
      if (!type.has_value())
        return fail("unknown event type '" + e["type"].toString() + "'");
      ev.type = *type;
      ev.component = e["component"].toString();
      ev.instance = e["instruction_id"].isNull()
                        ? -1
                        : instanceFromName(e["instruction_id"].toString());
      if (!e["pc"].isNull())
        ev.pc = fromHex(e["pc"].toString());
      ev.assembly = e["assembly"].toString();
      ev.payload = e["payload"].toObject().toVariantMap();
      const auto cause = o["correlation"].toObject()["cause_sequence"];
      if (!cause.isNull() && !cause.isUndefined())
        ev.causeSequence = cause.toInteger();
      ev.observability = observabilityFromName(o["observability"].toString())
                             .value_or(Observability::ModelDerived);
      ev.architectural = o["state"].toObject()["architectural"].toBool();
      ev.sourceRef = o["source_ref"].toString();
      t.events.push_back(std::move(ev));
    } else if (record == "footer") {
      const auto c = o["completeness"].toObject();
      auto &tc = t.completeness;
      tc.startsAtReset = c["starts_at_reset"].toBool();
      tc.startCycle = c["start_cycle"].toInteger();
      tc.endCycle = c["end_cycle"].toInteger();
      tc.truncated = c["truncated"].toBool();
      tc.truncationReason = c["truncation_reason"].toString();
      tc.programFinished = c["program_finished"].toBool();
      tc.caveats = toStringList(c["caveats"]);
      tc.instructionsRetired =
          o["summary"].toObject()["instructions_retired"].toInteger();
    } else {
      return fail("unknown record type '" + record + "'");
    }
  }
  if (!haveHeader)
    return fail("missing header record");
  return t;
}

std::optional<Trace> readJsonlFile(const QString &path, QString *error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error)
      *error = "Could not open '" + path + "': " + f.errorString();
    return std::nullopt;
  }
  return readJsonl(f, error);
}

std::optional<Trace> fromJsonl(const QByteArray &data, QString *error) {
  QByteArray copy = data;
  QBuffer buffer(&copy);
  buffer.open(QIODevice::ReadOnly);
  return readJsonl(buffer, error);
}

} // namespace Trace
} // namespace Ripes
