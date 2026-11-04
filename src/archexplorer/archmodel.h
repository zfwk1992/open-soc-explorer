#pragma once

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace Ripes {

/**
 * Data model for the Architecture Explorer tab.
 *
 * The model is loaded from the JSON schema documented in
 * docs/arch/arch_explorer.md (and docs/arch/PLAN.md). Loading validates all
 * required fields and all cross references (bus endpoints, flow step highlight
 * ids) so that the scene/tab code may assume a consistent model.
 */

/// Block categories. Determines the fill color of a block and its legend entry.
enum class ArchBlockKind {
  Cpu,
  Gpu,
  Accel,
  Dma,
  Memory,
  Cache,
  Io,
  Bridge,
  Audio,
  Video,
  Bus,
  Misc
};

/// Bus/connection categories. Determines line style.
enum class ArchBusKind { Bus, Dma, Irq, Mailbox, Link };

/// Ordered list of all block kinds (used for legends and validation).
const QVector<ArchBlockKind> &allArchBlockKinds();
/// Ordered list of all bus kinds.
const QVector<ArchBusKind> &allArchBusKinds();

/// JSON name of a block kind ("cpu", "gpu", ...).
QString archBlockKindName(ArchBlockKind kind);
/// Parses a JSON block kind name; nullopt if unknown.
std::optional<ArchBlockKind> archBlockKindFromName(const QString &name);
/// JSON name of a bus kind ("bus", "dma", ...).
QString archBusKindName(ArchBusKind kind);
/// Parses a JSON bus kind name; nullopt if unknown.
std::optional<ArchBusKind> archBusKindFromName(const QString &name);

/// Key/value property list. Kept as an ordered vector so that the display
/// order is deterministic (sorted by key, as QJsonObject stores it).
using ArchProps = QVector<QPair<QString, QString>>;

struct ArchBlock {
  QString id;
  QString label;
  QString sublabel;
  QString details;
  ArchBlockKind kind = ArchBlockKind::Misc;
  double x = 0;
  double y = 0;
  double w = 160;
  double h = 60;
  ArchProps props;
};

struct ArchBus {
  QString id;
  QString from;
  QString to;
  QString label;
  ArchBusKind kind = ArchBusKind::Bus;
  bool bidirectional = false;
  ArchProps props;
};

struct ArchFlowStep {
  QStringList highlight; ///< block ids to highlight
  QStringList buses;     ///< bus ids to highlight
  QString text;          ///< step description
};

struct ArchFlow {
  QString id;
  QString title;
  QString description;
  QVector<ArchFlowStep> steps;
};

struct ArchModel {
  QString id;
  QString title;
  QString subtitle;
  QString description;
  QVector<ArchBlock> blocks;
  QVector<ArchBus> buses;
  QVector<ArchFlow> flows;

  /// Returns the block with the given id, or nullptr.
  const ArchBlock *findBlock(const QString &id) const;
  /// Returns the bus with the given id, or nullptr.
  const ArchBus *findBus(const QString &id) const;
  /// Returns the flow with the given id, or nullptr.
  const ArchFlow *findFlow(const QString &id) const;

  /**
   * @brief fromJson
   * Parses and validates a JSON document. On failure, returns nullopt and (if
   * @p error is non-null) stores a human readable description of the first
   * problem found, e.g. "buses[2].to: references unknown block id 'foo'".
   */
  static std::optional<ArchModel> fromJson(const QByteArray &json,
                                           QString *error = nullptr);

  /**
   * @brief fromFile
   * Reads @p path (a filesystem path or a ":/..." resource path) and calls
   * fromJson.
   */
  static std::optional<ArchModel> fromFile(const QString &path,
                                           QString *error = nullptr);

  /**
   * @brief builtinPaths
   * Enumerates all every ":/arch/<name>.json" resource (sorted by file name).
   */
  static QStringList builtinPaths();

  /**
   * @brief loadBuiltin
   * Loads every builtin ":/arch/<name>.json" resource. Files which fail to load are skipped;
   * their error messages (prefixed with the resource path) are appended to
   * @p errors if non-null.
   */
  static QVector<ArchModel> loadBuiltin(QStringList *errors = nullptr);
};

} // namespace Ripes
