#include "archmodel.h"
#include "i18n.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

// Q_INIT_RESOURCE must be expanded in the global namespace: it declares the
// generated qInitResources_<name>() function as extern. The arch.qrc resources
// are compiled into archexplorer_lib (see src/archexplorer/CMakeLists.txt);
// referencing the init function here guarantees that the linker keeps the
// resource object when linking the static library into an executable.
static void initArchResources() { Q_INIT_RESOURCE(arch); }

namespace Ripes {

// ============================================================================
// Kind tables
// ============================================================================

namespace {

struct BlockKindEntry {
  ArchBlockKind kind;
  const char *name;
};

const BlockKindEntry s_blockKinds[] = {
    {ArchBlockKind::Cpu, "cpu"},       {ArchBlockKind::Gpu, "gpu"},
    {ArchBlockKind::Accel, "accel"},   {ArchBlockKind::Dma, "dma"},
    {ArchBlockKind::Memory, "memory"},
    {ArchBlockKind::Cache, "cache"},   {ArchBlockKind::Io, "io"},
    {ArchBlockKind::Bridge, "bridge"}, {ArchBlockKind::Audio, "audio"},
    {ArchBlockKind::Video, "video"},   {ArchBlockKind::Bus, "bus"},
    {ArchBlockKind::Misc, "misc"},
};

struct BusKindEntry {
  ArchBusKind kind;
  const char *name;
};

const BusKindEntry s_busKinds[] = {
    {ArchBusKind::Bus, "bus"},         {ArchBusKind::Dma, "dma"},
    {ArchBusKind::Irq, "irq"},         {ArchBusKind::Mailbox, "mailbox"},
    {ArchBusKind::Link, "link"},
};

QString joinedBlockKindNames() {
  QStringList names;
  for (const auto &e : s_blockKinds)
    names << QString::fromLatin1(e.name);
  return names.join(", ");
}

QString joinedBusKindNames() {
  QStringList names;
  for (const auto &e : s_busKinds)
    names << QString::fromLatin1(e.name);
  return names.join(", ");
}

} // namespace

const QVector<ArchBlockKind> &allArchBlockKinds() {
  static const QVector<ArchBlockKind> kinds = [] {
    QVector<ArchBlockKind> v;
    for (const auto &e : s_blockKinds)
      v.push_back(e.kind);
    return v;
  }();
  return kinds;
}

const QVector<ArchBusKind> &allArchBusKinds() {
  static const QVector<ArchBusKind> kinds = [] {
    QVector<ArchBusKind> v;
    for (const auto &e : s_busKinds)
      v.push_back(e.kind);
    return v;
  }();
  return kinds;
}

QString archBlockKindName(ArchBlockKind kind) {
  for (const auto &e : s_blockKinds) {
    if (e.kind == kind)
      return QString::fromLatin1(e.name);
  }
  return QStringLiteral("misc");
}

std::optional<ArchBlockKind> archBlockKindFromName(const QString &name) {
  for (const auto &e : s_blockKinds) {
    if (name == QLatin1String(e.name))
      return e.kind;
  }
  return std::nullopt;
}

QString archBusKindName(ArchBusKind kind) {
  for (const auto &e : s_busKinds) {
    if (e.kind == kind)
      return QString::fromLatin1(e.name);
  }
  return QStringLiteral("bus");
}

std::optional<ArchBusKind> archBusKindFromName(const QString &name) {
  for (const auto &e : s_busKinds) {
    if (name == QLatin1String(e.name))
      return e.kind;
  }
  return std::nullopt;
}

// ============================================================================
// Lookup helpers
// ============================================================================

const ArchBlock *ArchModel::findBlock(const QString &id) const {
  for (const auto &b : blocks) {
    if (b.id == id)
      return &b;
  }
  return nullptr;
}

const ArchBus *ArchModel::findBus(const QString &id) const {
  for (const auto &b : buses) {
    if (b.id == id)
      return &b;
  }
  return nullptr;
}

const ArchFlow *ArchModel::findFlow(const QString &id) const {
  for (const auto &f : flows) {
    if (f.id == id)
      return &f;
  }
  return nullptr;
}

// ============================================================================
// JSON parsing / validation
// ============================================================================

namespace {

/**
 * Small validation context. Every helper returns false and records the first
 * error message in a "path: message" format, where path is a JSON-pointer-like
 * location such as "blocks[2].kind".
 */
struct Parser {
  QString error;
  bool failed = false;

  bool fail(const QString &path, const QString &message) {
    if (!failed) {
      failed = true;
      error = path + ": " + message;
    }
    return false;
  }

  // ----- scalar field helpers ------------------------------------------------

  bool requiredString(const QJsonObject &obj, const QString &key,
                      const QString &path, QString &out,
                      bool allowEmpty = false) {
    if (!obj.contains(key))
      return fail(path + "." + key, "missing required string field");
    const QJsonValue v = obj.value(key);
    if (!v.isString())
      return fail(path + "." + key, "must be a string");
    out = v.toString();
    if (!allowEmpty && out.trimmed().isEmpty())
      return fail(path + "." + key, "must not be empty");
    return true;
  }

  // ----- localizable text helpers -------------------------------------------
  // A user-visible text field is either a plain string or a bilingual object
  // {"zh": "...", "en": "..."}; the latter is resolved with Ripes::localized()
  // (UI language first, then "en", then "zh").

  bool textValue(const QJsonValue &v, const QString &path, QString &out) {
    if (v.isString()) {
      out = v.toString();
      return true;
    }
    if (!v.isObject())
      return fail(path, "must be a string or a {\"zh\": ..., \"en\": ...} "
                        "object");
    const QJsonObject obj = v.toObject();
    if (!obj.contains("zh") && !obj.contains("en"))
      return fail(path, "bilingual text object must contain \"zh\" and/or "
                        "\"en\"");
    for (auto it = obj.begin(); it != obj.end(); ++it) {
      if (!it.value().isString())
        return fail(path + "." + it.key(), "must be a string");
    }
    out = Ripes::localized(v);
    return true;
  }

  bool requiredText(const QJsonObject &obj, const QString &key,
                    const QString &path, QString &out) {
    if (!obj.contains(key))
      return fail(path + "." + key, "missing required text field");
    if (!textValue(obj.value(key), path + "." + key, out))
      return false;
    if (out.trimmed().isEmpty())
      return fail(path + "." + key, "must not be empty");
    return true;
  }

  bool optionalText(const QJsonObject &obj, const QString &key,
                    const QString &path, QString &out) {
    if (!obj.contains(key))
      return true;
    const QJsonValue v = obj.value(key);
    if (v.isNull())
      return true;
    return textValue(v, path + "." + key, out);
  }

  bool propValue(const QJsonValue &pv, const QString &path, QString &text) {
    if (pv.isString() || pv.isObject())
      return textValue(pv, path, text);
    if (pv.isDouble()) {
      text = QString::number(pv.toDouble());
      return true;
    }
    if (pv.isBool()) {
      text = pv.toBool() ? QStringLiteral("true") : QStringLiteral("false");
      return true;
    }
    return fail(path, "property values must be strings, bilingual text "
                      "objects, numbers or booleans");
  }

  bool requiredNumber(const QJsonObject &obj, const QString &key,
                      const QString &path, double &out) {
    if (!obj.contains(key))
      return fail(path + "." + key, "missing required number field");
    const QJsonValue v = obj.value(key);
    if (!v.isDouble())
      return fail(path + "." + key, "must be a number");
    out = v.toDouble();
    return true;
  }

  bool optionalBool(const QJsonObject &obj, const QString &key,
                    const QString &path, bool &out) {
    if (!obj.contains(key))
      return true;
    const QJsonValue v = obj.value(key);
    if (v.isNull())
      return true;
    if (!v.isBool())
      return fail(path + "." + key, "must be a boolean");
    out = v.toBool();
    return true;
  }

  bool optionalProps(const QJsonObject &obj, const QString &path,
                     ArchProps &out) {
    if (!obj.contains("props"))
      return true;
    const QJsonValue v = obj.value("props");
    if (v.isNull())
      return true;
    if (v.isObject()) {
      // {"Key": value, ...}: displayed sorted by key (QJsonObject order).
      const QJsonObject props = v.toObject();
      for (auto it = props.begin(); it != props.end(); ++it) {
        QString text;
        if (!propValue(it.value(), path + ".props." + it.key(), text))
          return false;
        out.push_back({it.key(), text});
      }
      return true;
    }
    if (v.isArray()) {
      // [{"key": text, "value": value}, ...]: keeps the file order and allows
      // bilingual keys.
      const QJsonArray arr = v.toArray();
      for (int i = 0; i < arr.size(); ++i) {
        const QString ppath = path + ".props[" + QString::number(i) + "]";
        if (!arr.at(i).isObject())
          return fail(ppath, "must be an object {\"key\": ..., \"value\": ...}");
        const QJsonObject entry = arr.at(i).toObject();
        QString key;
        if (!requiredText(entry, "key", ppath, key))
          return false;
        if (!entry.contains("value"))
          return fail(ppath + ".value", "missing required field");
        QString text;
        if (!propValue(entry.value("value"), ppath + ".value", text))
          return false;
        out.push_back({key, text});
      }
      return true;
    }
    return fail(path + ".props",
                "must be an object of key -> value or an array of "
                "{\"key\", \"value\"} objects");
  }

  bool optionalStringList(const QJsonObject &obj, const QString &key,
                          const QString &path, QStringList &out) {
    if (!obj.contains(key))
      return true;
    const QJsonValue v = obj.value(key);
    if (v.isNull())
      return true;
    if (!v.isArray())
      return fail(path + "." + key, "must be an array of strings");
    const QJsonArray arr = v.toArray();
    for (int i = 0; i < arr.size(); ++i) {
      if (!arr.at(i).isString())
        return fail(path + "." + key + "[" + QString::number(i) + "]",
                    "must be a string");
      out << arr.at(i).toString();
    }
    return true;
  }

  bool requiredArray(const QJsonObject &obj, const QString &key,
                     const QString &path, QJsonArray &out, bool allowEmpty) {
    if (!obj.contains(key))
      return fail(path + "." + key, "missing required array field");
    const QJsonValue v = obj.value(key);
    if (!v.isArray())
      return fail(path + "." + key, "must be an array");
    out = v.toArray();
    if (!allowEmpty && out.isEmpty())
      return fail(path + "." + key, "must not be empty");
    return true;
  }

  bool optionalArray(const QJsonObject &obj, const QString &key,
                     const QString &path, QJsonArray &out) {
    if (!obj.contains(key))
      return true;
    const QJsonValue v = obj.value(key);
    if (v.isNull())
      return true;
    if (!v.isArray())
      return fail(path + "." + key, "must be an array");
    out = v.toArray();
    return true;
  }

  // ----- structured parsers --------------------------------------------------

  bool parseBlock(const QJsonValue &v, const QString &path, ArchBlock &out) {
    if (!v.isObject())
      return fail(path, "must be an object");
    const QJsonObject obj = v.toObject();
    if (!requiredString(obj, "id", path, out.id))
      return false;
    if (!requiredText(obj, "label", path, out.label))
      return false;
    if (!optionalText(obj, "sublabel", path, out.sublabel))
      return false;
    if (!optionalText(obj, "details", path, out.details))
      return false;

    QString kindName;
    if (!requiredString(obj, "kind", path, kindName))
      return false;
    const auto kind = archBlockKindFromName(kindName);
    if (!kind)
      return fail(path + ".kind", "unknown block kind '" + kindName +
                                      "' (expected one of: " +
                                      joinedBlockKindNames() + ")");
    out.kind = *kind;

    if (!requiredNumber(obj, "x", path, out.x))
      return false;
    if (!requiredNumber(obj, "y", path, out.y))
      return false;
    if (!requiredNumber(obj, "w", path, out.w))
      return false;
    if (!requiredNumber(obj, "h", path, out.h))
      return false;
    if (out.w <= 0)
      return fail(path + ".w", "must be > 0");
    if (out.h <= 0)
      return fail(path + ".h", "must be > 0");

    if (!optionalProps(obj, path, out.props))
      return false;
    return true;
  }

  bool parseBus(const QJsonValue &v, const QString &path, ArchBus &out) {
    if (!v.isObject())
      return fail(path, "must be an object");
    const QJsonObject obj = v.toObject();
    if (!requiredString(obj, "id", path, out.id))
      return false;
    if (!requiredString(obj, "from", path, out.from))
      return false;
    if (!requiredString(obj, "to", path, out.to))
      return false;
    if (!optionalText(obj, "label", path, out.label))
      return false;

    QString kindName;
    if (!requiredString(obj, "kind", path, kindName))
      return false;
    const auto kind = archBusKindFromName(kindName);
    if (!kind)
      return fail(path + ".kind", "unknown bus kind '" + kindName +
                                      "' (expected one of: " +
                                      joinedBusKindNames() + ")");
    out.kind = *kind;

    if (!optionalBool(obj, "bidirectional", path, out.bidirectional))
      return false;
    if (!optionalProps(obj, path, out.props))
      return false;
    return true;
  }

  bool parseStep(const QJsonValue &v, const QString &path, ArchFlowStep &out) {
    if (!v.isObject())
      return fail(path, "must be an object");
    const QJsonObject obj = v.toObject();
    if (!requiredText(obj, "text", path, out.text))
      return false;
    if (!optionalStringList(obj, "highlight", path, out.highlight))
      return false;
    if (!optionalStringList(obj, "buses", path, out.buses))
      return false;
    return true;
  }

  bool parseFlow(const QJsonValue &v, const QString &path, ArchFlow &out) {
    if (!v.isObject())
      return fail(path, "must be an object");
    const QJsonObject obj = v.toObject();
    if (!requiredString(obj, "id", path, out.id))
      return false;
    if (!requiredText(obj, "title", path, out.title))
      return false;
    if (!optionalText(obj, "description", path, out.description))
      return false;
    QJsonArray steps;
    if (!requiredArray(obj, "steps", path, steps, /*allowEmpty=*/false))
      return false;
    for (int i = 0; i < steps.size(); ++i) {
      ArchFlowStep step;
      if (!parseStep(steps.at(i), path + ".steps[" + QString::number(i) + "]",
                     step))
        return false;
      out.steps.push_back(step);
    }
    return true;
  }

  bool parseModel(const QJsonDocument &doc, ArchModel &out) {
    if (!doc.isObject())
      return fail("$", "top-level JSON value must be an object");
    const QJsonObject obj = doc.object();
    const QString path = "$";

    if (!requiredString(obj, "id", path, out.id))
      return false;
    if (!requiredText(obj, "title", path, out.title))
      return false;
    if (!optionalText(obj, "subtitle", path, out.subtitle))
      return false;
    if (!optionalText(obj, "description", path, out.description))
      return false;

    // ----- blocks
    QJsonArray blocks;
    if (!requiredArray(obj, "blocks", path, blocks, /*allowEmpty=*/false))
      return false;
    QSet<QString> blockIds;
    for (int i = 0; i < blocks.size(); ++i) {
      const QString bpath = "blocks[" + QString::number(i) + "]";
      ArchBlock block;
      if (!parseBlock(blocks.at(i), bpath, block))
        return false;
      if (blockIds.contains(block.id))
        return fail(bpath + ".id", "duplicate block id '" + block.id + "'");
      blockIds.insert(block.id);
      out.blocks.push_back(block);
    }

    // ----- buses (optional; an architecture may consist of a single block)
    QJsonArray buses;
    if (!optionalArray(obj, "buses", path, buses))
      return false;
    QSet<QString> busIds;
    for (int i = 0; i < buses.size(); ++i) {
      const QString bpath = "buses[" + QString::number(i) + "]";
      ArchBus bus;
      if (!parseBus(buses.at(i), bpath, bus))
        return false;
      if (busIds.contains(bus.id))
        return fail(bpath + ".id", "duplicate bus id '" + bus.id + "'");
      if (!blockIds.contains(bus.from))
        return fail(bpath + ".from",
                    "references unknown block id '" + bus.from + "'");
      if (!blockIds.contains(bus.to))
        return fail(bpath + ".to",
                    "references unknown block id '" + bus.to + "'");
      busIds.insert(bus.id);
      out.buses.push_back(bus);
    }

    // ----- flows (optional)
    QJsonArray flows;
    if (!optionalArray(obj, "flows", path, flows))
      return false;
    QSet<QString> flowIds;
    for (int i = 0; i < flows.size(); ++i) {
      const QString fpath = "flows[" + QString::number(i) + "]";
      ArchFlow flow;
      if (!parseFlow(flows.at(i), fpath, flow))
        return false;
      if (flowIds.contains(flow.id))
        return fail(fpath + ".id", "duplicate flow id '" + flow.id + "'");
      flowIds.insert(flow.id);
      for (int s = 0; s < flow.steps.size(); ++s) {
        const QString spath = fpath + ".steps[" + QString::number(s) + "]";
        const auto &step = flow.steps.at(s);
        for (const auto &id : step.highlight) {
          if (!blockIds.contains(id))
            return fail(spath + ".highlight",
                        "references unknown block id '" + id + "'");
        }
        for (const auto &id : step.buses) {
          if (!busIds.contains(id))
            return fail(spath + ".buses",
                        "references unknown bus id '" + id + "'");
        }
      }
      out.flows.push_back(flow);
    }

    return true;
  }
};

} // namespace

std::optional<ArchModel> ArchModel::fromJson(const QByteArray &json,
                                             QString *error) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    if (error) {
      *error = "JSON parse error at offset " +
               QString::number(parseError.offset) + ": " +
               parseError.errorString();
    }
    return std::nullopt;
  }

  Parser parser;
  ArchModel model;
  if (!parser.parseModel(doc, model)) {
    if (error)
      *error = parser.error;
    return std::nullopt;
  }
  if (error)
    error->clear();
  return model;
}

std::optional<ArchModel> ArchModel::fromFile(const QString &path,
                                             QString *error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = "cannot open '" + path + "': " + file.errorString();
    return std::nullopt;
  }
  return fromJson(file.readAll(), error);
}

QStringList ArchModel::builtinPaths() {
  initArchResources();
  QDir dir(":/arch");
  QStringList paths;
  const QStringList names = dir.entryList({"*.json"}, QDir::Files, QDir::Name);
  for (const auto &name : names)
    paths << dir.filePath(name);
  return paths;
}

QVector<ArchModel> ArchModel::loadBuiltin(QStringList *errors) {
  QVector<ArchModel> models;
  const QStringList paths = builtinPaths();
  for (const auto &path : paths) {
    QString error;
    auto model = fromFile(path, &error);
    if (model) {
      models.push_back(*model);
    } else if (errors) {
      errors->append(path + ": " + error);
    }
  }
  return models;
}

} // namespace Ripes
