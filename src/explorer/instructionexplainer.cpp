// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "instructionexplainer.h"

#include <QMetaEnum>

#include <algorithm>

#include "modelprofile.h"

namespace Ripes {
namespace Explorer {

using Trace::EventType;

namespace {

std::optional<ProcessorID> processorFromName(const QString &name) {
  bool ok = false;
  const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
      name.toUtf8().constData(), &ok);
  if (!ok || v < 0 || v >= ProcessorID::NUM_PROCESSORS)
    return std::nullopt;
  return static_cast<ProcessorID>(v);
}

RVDecoded decodedOf(const TraceReplay &replay,
                    const TraceReplay::Instance &inst, unsigned xlen) {
  for (size_t idx : inst.events) {
    const auto &ev = replay.trace().events[idx];
    if ((ev.type == EventType::InstructionFetch ||
         ev.type == EventType::InstructionDecode) &&
        ev.payload.contains("word")) {
      const auto word = Trace::fromHex(ev.payload["word"].toString());
      if (word.has_value())
        return decodeRV(static_cast<quint32>(*word), xlen);
    }
  }
  return {};
}

QString registerName(const Trace::TraceHeader &h, const QString &file,
                     unsigned idx) {
  for (const auto &rf : h.registerFiles)
    if (rf.name == file && static_cast<int>(idx) < rf.names.size()) {
      const QString alias = rf.aliases.value(static_cast<int>(idx));
      return alias.isEmpty() || alias == rf.names[idx]
                 ? rf.names[idx]
                 : QString("%1 (%2)").arg(rf.names[idx], alias);
    }
  return QString("%1[%2]").arg(file).arg(idx);
}

QString stageOfEvent(const Trace::TraceHeader &h, const Trace::TraceEvent &ev) {
  for (const auto &s : h.stages)
    if (s.component == ev.component)
      return s.name;
  return ev.component;
}

QString hex(quint64 v, unsigned bits) { return Trace::toHex(v, bits); }

QString signedValue(quint64 v, unsigned xlen) {
  const quint64 signBit = quint64(1) << (xlen - 1);
  const quint64 m = xlen >= 64 ? ~quint64(0) : (signBit << 1) - 1;
  v &= m;
  const qint64 s = static_cast<qint64>((v ^ signBit) - signBit);
  return QString::number(s);
}

QString escape(const QString &s) { return s.toHtmlEscaped(); }

} // namespace

InstructionExplanation explainInstance(const TraceReplay &replay, qint64 id) {
  InstructionExplanation e;
  const auto *inst = replay.instance(id);
  if (!inst)
    return e;
  const auto &t = replay.trace();
  const auto &h = t.header;
  e.instance = id;
  e.pc = inst->pc;
  e.assembly = inst->assembly;
  e.outcome = inst->outcome;
  e.firstCycle = inst->firstCycle;
  e.endCycle = inst->endCycle;
  e.xlen = h.xlen;
  e.decoded = decodedOf(replay, *inst, h.xlen);
  const auto &d = e.decoded;
  const QString mnemonic = d.valid && !d.compressed
                               ? d.mnemonic
                               : inst->assembly.section(' ', 0, 0).toLower();
  e.semantics = semanticsFor(mnemonic);

  const auto pid = processorFromName(h.source.model);
  const std::optional<ModelProfile> profile =
      h.source.engine == "ripes" && pid.has_value()
          ? std::optional<ModelProfile>(modelProfile(*pid))
          : std::nullopt;

  // ---- Journey through the model -----------------------------------------
  std::set<qint64> fetchCycles;
  for (size_t idx : inst->events) {
    const auto &ev = t.events[idx];
    if (ev.type == EventType::InstructionFetch)
      fetchCycles.insert(ev.cycle);
  }
  for (size_t idx : inst->events) {
    const auto &ev = t.events[idx];
    const QString where = stageOfEvent(h, ev);
    const auto &p = ev.payload;
    QString what;
    switch (ev.type) {
    case EventType::InstructionFetch:
      what = "fetched from " + hex(inst->pc, h.xlen);
      break;
    case EventType::StageEnter:
      if (fetchCycles.count(ev.cycle))
        continue;
      what = p.contains("note") ? "observed (" + p["note"].toString() + ")"
                                : "entered stage";
      break;
    case EventType::InstructionDecode:
      what = "decoded";
      break;
    case EventType::PipelineStall:
      what = "held in this stage (stall)";
      break;
    case EventType::PipelineFlush:
      what = "squashed";
      if (p.contains("cause_instance"))
        what += " by " + p["cause_instance"].toString() + " (" +
                p.value("cause").toString() + ")";
      break;
    case EventType::InstructionRetire:
      what = p.contains("retire_index")
                 ? QString("retired (#%1)").arg(p["retire_index"].toLongLong())
                 : "retired (inferred: the model's counter did not count it)";
      break;
    case EventType::RegisterWrite:
      what = QString("wrote %1 = %2")
                 .arg(registerName(h, p["regfile"].toString(),
                                   p["index"].toUInt()),
                      p["value"].toString());
      break;
    case EventType::MemoryRequest:
      what = QString("data memory %1 request @ %2")
                 .arg(p["access"].toString(), p["address"].toString());
      if (p.contains("bytes"))
        what += QString(" (%1 bytes)").arg(p["bytes"].toUInt());
      break;
    case EventType::MemoryResponse:
      what = QString("data memory %1 value %2")
                 .arg(p["access"].toString(), p["value"].isNull()
                                                  ? QString("(not sampled)")
                                                  : p["value"].toString());
      break;
    default:
      what = Trace::eventTypeName(ev.type);
      break;
    }
    e.journey.push_back({ev.cycle, where, what});

    if (ev.type == EventType::RegisterWrite) {
      InstructionExplanation::RegisterResult r;
      r.name = registerName(h, p["regfile"].toString(), p["index"].toUInt());
      r.value = Trace::fromHex(p["value"].toString()).value_or(0);
      r.previous = Trace::fromHex(p["previous"].toString()).value_or(0);
      r.cycle = ev.cycle;
      e.result = r;
    } else if (ev.type == EventType::MemoryResponse) {
      InstructionExplanation::MemoryAccess m;
      m.access = p["access"].toString();
      m.address = Trace::fromHex(p["address"].toString()).value_or(0);
      m.bytes = p["bytes"].toUInt();
      if (!p["value"].isNull())
        m.value = Trace::fromHex(p["value"].toString());
      m.cycle = ev.cycle;
      e.memory.push_back(m);
    }
  }

  // ---- Operands and ISA checks --------------------------------------------
  const auto before = replay.registersBeforeRetire(id);
  auto valueOf = [&](RegClass cls, unsigned idx) -> std::optional<quint64> {
    if (!before.has_value())
      return std::nullopt;
    const QString file = cls == RegClass::FPR ? "fpr" : "gpr";
    // x0 is always known; other registers only if the trace provides them.
    if (!(cls == RegClass::GPR && idx == 0) &&
        !replay.registerKnownBeforeRetire(id, file, static_cast<int>(idx)))
      return std::nullopt;
    const auto it = before->find(file);
    if (it == before->end() || idx >= it->second.size())
      return std::nullopt;
    return it->second[idx];
  };
  if (d.valid && !d.compressed) {
    auto addOperand = [&](const QString &role, unsigned idx, RegClass cls) {
      if (cls == RegClass::None)
        return;
      InstructionExplanation::Operand op;
      op.role = role;
      op.index = idx;
      op.name = registerName(h, cls == RegClass::FPR ? "fpr" : "gpr", idx);
      op.value = valueOf(cls, idx);
      e.operands.push_back(op);
    };
    addOperand("rs1", d.rs1, d.rs1Class);
    addOperand("rs2", d.rs2, d.rs2Class);
    addOperand("rs3", d.rs3, d.rs3Class);

    const auto rs1 = valueOf(d.rs1Class, d.rs1);
    const auto rs2 = valueOf(d.rs2Class, d.rs2);
    // Only check against the ISA when every source operand is known.
    const bool operandsKnown =
        (d.rs1Class == RegClass::None || rs1.has_value()) &&
        (d.rs2Class == RegClass::None || rs2.has_value());
    if (before.has_value() && operandsKnown && d.rdClass == RegClass::GPR &&
        d.writesRd())
      e.expected = evaluateIntegerResult(d, e.pc, rs1.value_or(0),
                                         rs2.value_or(0), h.xlen);
    if (before.has_value() && operandsKnown && d.isBranch)
      e.branchCondition =
          evaluateBranch(d, rs1.value_or(0), rs2.value_or(0), h.xlen);
  }

  if (inst->outcome == TraceReplay::Outcome::Retired &&
      (d.isBranch || d.isJump)) {
    const auto &order = replay.retirementOrder();
    auto pos = std::find(order.begin(), order.end(), id);
    if (pos != order.end() && pos + 1 != order.end()) {
      e.nextRetiredPc = replay.instance(*(pos + 1))->pc;
      e.branchTaken = *e.nextRetiredPc != e.pc + (d.compressed ? 2 : 4);
    }
  }

  const unsigned bits = h.xlen;
  if (e.expected.has_value()) {
    if (e.result.has_value()) {
      if (e.result->value == *e.expected)
        e.findings << QString("✓ The value written to %1 (%2) matches the ISA "
                              "semantics applied to the architectural "
                              "operands.")
                          .arg(e.result->name, hex(*e.expected, bits));
      else
        e.findings
            << QString("✗ The model wrote %1 = %2, but the ISA semantics give "
                       "%3 for the architectural operands. The instruction "
                       "used a stale operand: a data hazard that this model "
                       "does not resolve.")
                   .arg(e.result->name, hex(e.result->value, bits),
                        hex(*e.expected, bits));
    } else if (inst->outcome == TraceReplay::Outcome::Retired) {
      const auto previous = valueOf(RegClass::GPR, d.rd);
      if (previous.has_value() && *previous == *e.expected)
        e.findings << QString("✓ No register change was observed: the ISA "
                              "result %1 equals the previous value of the "
                              "destination register (writes of an unchanged "
                              "value are not observable).")
                          .arg(hex(*e.expected, bits));
      else
        e.findings << QString("✗ The ISA semantics give %1, but no write to "
                              "the destination register was observed.")
                          .arg(hex(*e.expected, bits));
    }
  }
  if (e.branchCondition.has_value() && e.branchTaken.has_value()) {
    const QString outcome = *e.branchTaken ? "taken" : "not taken";
    if (*e.branchCondition == *e.branchTaken)
      e.findings << "✓ The branch was " + outcome +
                        ", consistent with the condition evaluated on the "
                        "architectural operands.";
    else
      e.findings << "✗ The branch was " + outcome +
                        ", but the condition evaluated on the architectural "
                        "operands says otherwise (stale operand).";
  }
  if (inst->outcome == TraceReplay::Outcome::Squashed)
    e.findings << QString("This instruction was fetched but squashed at cycle "
                          "%1. It never became architectural, so it has no "
                          "effect on registers or memory.")
                      .arg(inst->endCycle);

  // ---- Hazards ------------------------------------------------------------
  qint64 decodeCycle = inst->firstCycle;
  if (profile.has_value() && !profile->decodeStage.isEmpty())
    for (const auto &[cycle, stage] : inst->stageEntries)
      if (h.stages[stage].name == profile->decodeStage)
        decodeCycle = cycle;
  if (d.valid && !d.compressed) {
    const auto &order = replay.retirementOrder();
    for (const auto &op : e.operands) {
      if (op.index == 0 && op.name.startsWith("x0"))
        continue;
      const RegClass cls = op.role == "rs1"   ? d.rs1Class
                           : op.role == "rs2" ? d.rs2Class
                                              : d.rs3Class;
      // Most recent older instruction (in program order) writing this register.
      const TraceReplay::Instance *producer = nullptr;
      RVDecoded producerDecoded;
      for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (*it >= id)
          continue;
        const auto *cand = replay.instance(*it);
        const auto cd = decodedOf(replay, *cand, h.xlen);
        if (cd.writesRd() && cd.rdClass == cls && cd.rd == op.index) {
          producer = cand;
          producerDecoded = cd;
          break;
        }
      }
      if (!producer || producer->endCycle <= decodeCycle)
        continue;
      QString text =
          QString("RAW dependency on %1: produced by %2 (%3), which retired at "
                  "cycle %4, after this instruction was decoded at cycle %5.")
              .arg(op.name, Trace::instanceName(producer->id),
                   escape(producer->assembly))
              .arg(producer->endCycle)
              .arg(decodeCycle);
      if (!inst->heldCycles.empty())
        text += QString(" It was resolved by stalling for %1 cycle(s).")
                    .arg(inst->heldCycles.size());
      else if (profile.has_value() && profile->hasForwarding)
        text += " No stall occurred: consistent with this model's forwarding "
                "unit supplying the value.";
      else if (profile.has_value() && profile->pipelined)
        text += " No stall occurred and this model has no forwarding unit, so "
                "the operand may have been read before it was written.";
      e.hazards << text;
    }
  }
  if (!inst->heldCycles.empty()) {
    QStringList cycles;
    for (auto c : inst->heldCycles)
      cycles << QString::number(c);
    e.hazards << QString("Held (stalled) in its stage during cycle(s) %1.")
                     .arg(cycles.join(", "));
  }

  // ---- Notes and source references ----------------------------------------
  if (!h.source.fidelityNote.isEmpty())
    e.notes << h.source.fidelityNote;
  e.notes << "Operand values are the architectural register values "
             "immediately before this instruction retired, i.e. the values "
             "the ISA says it reads. They are reconstructed from the trace, "
             "not read from the model's datapath.";

  if (profile.has_value()) {
    e.sourceRefs << profile->sourceFile;
    if (profile->fidelity == "educational-model") {
      e.sourceRefs << "src/processors/RISC-V/rv_decode.h"
                   << "src/processors/RISC-V/rv_control.h";
      if (d.hasImm)
        e.sourceRefs << "src/processors/RISC-V/rv_immediate.h";
      if (d.isLoad || d.isStore)
        e.sourceRefs << "src/processors/RISC-V/rv_memory.h";
      if (d.isBranch)
        e.sourceRefs << "src/processors/RISC-V/rv_branch.h";
      if (!d.isLoad && !d.isStore && !d.isSystem)
        e.sourceRefs << "src/processors/RISC-V/rv_alu.h";
    }
  } else if (!h.source.sourceFile.isEmpty()) {
    // Other engines name their model's implementation (e.g. "gem5: ...").
    e.sourceRefs << h.source.sourceFile;
  }
  e.sourceRefs << (d.isMulDiv ? "src/isa/rv_m_ext.cpp"
                              : "src/isa/rv_i_ext.cpp");
  e.sourceRefs << "src/explorer/riscvdecoder.cpp";
  return e;
}

QString explanationToHtml(const InstructionExplanation &e, bool dark) {
  const QString ok = dark ? "#7bd88f" : "#1b7f3a";
  const QString bad = dark ? "#ff8a80" : "#b3261e";
  const QString muted = dark ? "#a0a7b4" : "#5f6368";
  if (e.instance < 0)
    return QString("<p style='color:%1'>Select an instruction in the timeline "
                   "to see its explanation.</p>")
        .arg(muted);

  const unsigned bits = e.xlen;
  const auto &d = e.decoded;
  QString html;
  html += "<h3 style='margin-bottom:2px'><code>" + escape(e.assembly) +
          "</code></h3>";
  QString outcome;
  switch (e.outcome) {
  case TraceReplay::Outcome::Retired:
    outcome = QString("retired at cycle %1").arg(e.endCycle);
    break;
  case TraceReplay::Outcome::Squashed:
    outcome = QString("<span style='color:%1'>squashed at cycle %2</span>")
                  .arg(bad)
                  .arg(e.endCycle);
    break;
  case TraceReplay::Outcome::InFlight:
    outcome = "still in flight at the end of the trace";
    break;
  }
  html += QString("<p style='color:%1;margin-top:0'>%2 · PC %3 · first seen at "
                  "cycle %4 · %5</p>")
              .arg(muted, Trace::instanceName(e.instance), hex(e.pc, bits))
              .arg(e.firstCycle)
              .arg(outcome);

  if (e.semantics.has_value()) {
    html += "<h4>What it does</h4>";
    html += "<p><code>" + escape(e.semantics->expression) + "</code><br>" +
            escape(e.semantics->description) + " <i style='color:" + muted +
            "'>(" + escape(e.semantics->category) + ")</i></p>";
  }

  html += "<h4>Encoding</h4>";
  if (d.compressed) {
    html += QString("<p>16-bit compressed instruction <code>%1</code>. The "
                    "processor expands it to its 32-bit equivalent before "
                    "decoding.</p>")
                .arg(hex(d.word, 16));
  } else if (d.valid) {
    html += QString("<p><code>%1</code> · %2</p>")
                .arg(hex(d.word, 32), formatName(d.format));
    html += "<table cellspacing='0' cellpadding='3' border='1' "
            "style='border-collapse:collapse'><tr>";
    QStringList heads, vals;
    heads << "opcode";
    vals << hex(d.opcode, 8);
    if (d.rdClass != RegClass::None) {
      heads << "rd";
      vals << QString::number(d.rd);
    }
    if (d.format == RVFormat::R || d.format == RVFormat::I ||
        d.format == RVFormat::S || d.format == RVFormat::B ||
        d.format == RVFormat::R4) {
      heads << "funct3";
      vals << QString::number(d.funct3);
    }
    if (d.rs1Class != RegClass::None) {
      heads << "rs1";
      vals << QString::number(d.rs1);
    }
    if (d.rs2Class != RegClass::None) {
      heads << "rs2";
      vals << QString::number(d.rs2);
    }
    if (d.format == RVFormat::R) {
      heads << "funct7";
      vals << hex(d.funct7, 8);
    }
    if (d.hasImm) {
      heads << "imm";
      vals << QString::number(d.imm);
    }
    for (const auto &hd : heads)
      html += "<th>" + hd + "</th>";
    html += "</tr><tr>";
    for (const auto &v : vals)
      html += "<td align='center'>" + v + "</td>";
    html += "</tr></table>";
  } else {
    html += "<p>Not decodable by the Open SoC Explorer RISC-V decoder.</p>";
  }

  if (!e.operands.empty()) {
    html += "<h4>Operands <span style='font-weight:normal;color:" + muted +
            "'>(architectural values before retirement)</span></h4><ul>";
    for (const auto &op : e.operands) {
      html += "<li><b>" + op.role + "</b> = " + escape(op.name) + " = ";
      html += op.value.has_value()
                  ? QString("<code>%1</code> (%2)")
                        .arg(hex(*op.value, bits), signedValue(*op.value, bits))
                  : QString("<i>not available (the instruction did not retire, "
                            "or the trace does not contain this register's "
                            "value)</i>");
      html += "</li>";
    }
    if (d.hasImm)
      html += QString("<li><b>imm</b> = %1</li>").arg(d.imm);
    html += "</ul>";
  }

  if (e.result.has_value() || !e.memory.empty() || e.branchTaken.has_value()) {
    html +=
        "<h4>Observed effect <span style='font-weight:normal;color:" + muted +
        "'>(simulator output)</span></h4><ul>";
    if (e.result.has_value())
      html += QString("<li>%1: <code>%2</code> → <code>%3</code> (%4) at cycle "
                      "%5</li>")
                  .arg(escape(e.result->name), hex(e.result->previous, bits),
                       hex(e.result->value, bits),
                       signedValue(e.result->value, bits))
                  .arg(e.result->cycle);
    for (const auto &m : e.memory)
      html += QString("<li>Memory %1 of %2 bytes at <code>%3</code>: %4 (cycle "
                      "%5)</li>")
                  .arg(m.access)
                  .arg(m.bytes)
                  .arg(hex(m.address, bits),
                       m.value.has_value()
                           ? "<code>" + hex(*m.value, m.bytes * 8) + "</code>"
                           : QString("value not sampled"))
                  .arg(m.cycle);
    if (e.branchTaken.has_value())
      html += QString("<li>Control flow: %1 — the next retired instruction is "
                      "at <code>%2</code></li>")
                  .arg(*e.branchTaken ? "taken" : "not taken",
                       hex(e.nextRetiredPc.value_or(0), bits));
    html += "</ul>";
  }

  if (!e.findings.isEmpty()) {
    html += "<h4>Checks against the ISA</h4><ul>";
    for (const auto &f : e.findings) {
      const QString color =
          f.startsWith("✓") ? ok : (f.startsWith("✗") ? bad : muted);
      html += "<li style='color:" + color + "'>" + escape(f) + "</li>";
    }
    html += "</ul>";
  }

  if (!e.hazards.isEmpty()) {
    html += "<h4>Hazards and stalls</h4><ul>";
    for (const auto &hz : e.hazards)
      html += "<li>" + hz + "</li>";
    html += "</ul>";
  }

  html += "<h4>Journey through the model</h4><table cellspacing='0' "
          "cellpadding='2'><tr><th align='left'>Cycle</th><th "
          "align='left'>Where</th><th align='left'>What</th></tr>";
  for (const auto &s : e.journey)
    html += QString("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                .arg(s.cycle)
                .arg(escape(s.where), escape(s.what));
  html += "</table>";

  html += "<h4>Implemented in</h4><ul>";
  for (const auto &ref : e.sourceRefs)
    html += "<li><code>" + escape(ref) + "</code></li>";
  html += "</ul>";

  html += "<h4>Fidelity</h4>";
  for (const auto &n : e.notes)
    html += "<p style='color:" + muted + "'>" + escape(n) + "</p>";
  return html;
}

} // namespace Explorer
} // namespace Ripes
