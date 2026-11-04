#pragma once

#include "archmodel.h"

#include <QColor>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QMap>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

QT_FORWARD_DECLARE_CLASS(QGraphicsSceneMouseEvent)
QT_FORWARD_DECLARE_CLASS(QPainter)
QT_FORWARD_DECLARE_CLASS(QStyleOptionGraphicsItem)
QT_FORWARD_DECLARE_CLASS(QWidget)

namespace Ripes {

class ArchScene;

/// True if the current application palette is dark.
bool archPaletteIsDark();
/// Fill color for a block kind, adapted to the light/dark palette.
QColor archBlockKindColor(ArchBlockKind kind, bool dark);
/// Pen (color + style + width) used to draw a bus kind.
QPen archBusKindPen(ArchBusKind kind, bool dark);
/// Human readable, capitalized name of a block kind for legends ("CPU", ...).
/// Translated in the "Ripes::ArchScene" context.
QString archBlockKindDisplayName(ArchBlockKind kind);
/// Human readable name of a bus kind for legends. Translated.
QString archBusKindDisplayName(ArchBusKind kind);
/// One-line explanation of a block kind (legend panel / tooltips).
QString archBlockKindDescription(ArchBlockKind kind);
/// One-line explanation of a bus kind, including how it is drawn.
QString archBusKindDescription(ArchBusKind kind);

/**
 * @brief The ArchBlockItem class
 * A rounded rectangle with a kind-dependent fill, a bold label and an optional
 * sublabel. Selectable; clicking it notifies the owning ArchScene.
 */
class ArchBlockItem : public QGraphicsItem {
public:
  enum { Type = UserType + 1 };

  explicit ArchBlockItem(const ArchBlock &block);

  int type() const override { return Type; }
  QRectF boundingRect() const override;
  QPainterPath shape() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget = nullptr) override;

  const ArchBlock &block() const { return m_block; }
  const QString &id() const { return m_block.id; }
  /// The block rectangle in scene coordinates.
  QRectF sceneRect() const;

  void setHighlighted(bool highlighted);
  bool isHighlighted() const { return m_highlighted; }
  void setDimmed(bool dimmed);
  bool isDimmed() const { return m_dimmed; }

  /// Text fit checks (used by tools/arch_render to detect layout problems):
  /// true if the bold one-line label does not fit and is elided.
  bool labelElided() const;
  /// True if the word-wrapped sublabel needs more height than is available.
  bool sublabelClipped() const;

protected:
  void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
  QRectF rect() const { return QRectF(0, 0, m_block.w, m_block.h); }
  QRectF textRect() const { return rect().adjusted(6, 4, -6, -4); }

  ArchBlock m_block;
  bool m_highlighted = false;
  bool m_dimmed = false;
};

/**
 * @brief The ArchBusItem class
 * An orthogonal polyline between the edges of two blocks, with an arrow head
 * at the destination (and at the source when bidirectional). Line style
 * depends on the bus kind. The label is drawn next to the midpoint.
 *
 * Geometry is normally assigned by ArchScene::layoutBuses(), which spreads the
 * attachment points of all buses sharing a block side so that they do not
 * overlap. updateGeometry() provides a stand-alone default.
 */
class ArchBusItem : public QGraphicsItem {
public:
  enum { Type = UserType + 2 };

  enum class Side { Top, Bottom, Left, Right };
  struct Route {
    bool vertical = true;
    Side fromSide = Side::Bottom;
    Side toSide = Side::Top;
  };

  ArchBusItem(const ArchBus &bus, ArchBlockItem *from, ArchBlockItem *to);

  int type() const override { return Type; }
  QRectF boundingRect() const override;
  QPainterPath shape() const override;
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget = nullptr) override;

  const ArchBus &bus() const { return m_bus; }
  const QString &id() const { return m_bus.id; }
  ArchBlockItem *fromItem() const { return m_from; }
  ArchBlockItem *toItem() const { return m_to; }
  /// Polyline (scene coordinates) and label rectangle, for tests/tools.
  const QPolygonF &points() const { return m_points; }
  QRectF labelRect() const { return m_labelRect; }

  void setHighlighted(bool highlighted);
  bool isHighlighted() const { return m_highlighted; }
  void setDimmed(bool dimmed);
  bool isDimmed() const { return m_dimmed; }

  /// Decides on which sides of the two block rectangles a bus attaches.
  static Route computeRoute(const QRectF &from, const QRectF &to);
  /// Preferred coordinate (x for top/bottom sides, y for left/right sides)
  /// along @p side of @p rect for a bus whose other end is @p other.
  static qreal preferredAnchor(const QRectF &rect, Side side,
                               const QRectF &other);
  /// Converts a side coordinate into a scene point on the edge of @p rect.
  static QPointF anchorPoint(const QRectF &rect, Side side, qreal coord);

  /// Sets the polyline from explicit anchor points. @p midOffset shifts the
  /// middle segment of Z-shaped routes and @p labelShift moves the label along
  /// its segment (both used to separate parallel buses).
  void setGeometry(const Route &route, const QPointF &from, const QPointF &to,
                   qreal midOffset = 0, qreal labelShift = 0);
  /// Recomputes a default geometry from the current block positions.
  void updateGeometry();

  /// Alternative label rectangles (preferred first): the default placement,
  /// the other side of the line, and positions shifted along the segment.
  /// Used by ArchScene to resolve label/label and label/block overlaps.
  QVector<QRectF> labelCandidates() const;
  /// Moves the label to @p rect (one of labelCandidates()).
  void setLabelRect(const QRectF &rect);

private:
  static QPolygonF arrowHead(const QPointF &tip, const QPointF &fromDir,
                             qreal size);

  ArchBus m_bus;
  ArchBlockItem *m_from = nullptr;
  ArchBlockItem *m_to = nullptr;
  bool m_highlighted = false;
  bool m_dimmed = false;

  QPolygonF m_points;    // polyline in scene coordinates (item is at 0,0)
  QPainterPath m_path;   // polyline path (shortened under the arrow heads)
  QRectF m_labelRect;    // label background rectangle
  QPointF m_labelSeg0;   // segment the label belongs to
  QPointF m_labelSeg1;
  QRectF m_boundingRect; // cached
};

/**
 * @brief The ArchLegendItem class
 * Legend listing the block kinds and bus kinds present in the diagram.
 */
class ArchLegendItem : public QGraphicsItem {
public:
  enum { Type = UserType + 3 };

  ArchLegendItem(const QVector<ArchBlockKind> &blockKinds,
                 const QVector<ArchBusKind> &busKinds, qreal maxWidth);

  int type() const override { return Type; }
  QRectF boundingRect() const override { return m_boundingRect; }
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget = nullptr) override;

private:
  struct Entry {
    bool isBus = false;
    ArchBlockKind blockKind = ArchBlockKind::Misc;
    ArchBusKind busKind = ArchBusKind::Bus;
    QString text;
    QRectF rect; // entry rectangle in item coordinates
  };
  void layout(qreal maxWidth);

  QVector<Entry> m_entries;
  QRectF m_boundingRect;
};

/**
 * @brief The ArchScene class
 * Owns the graphics items for one ArchModel and implements flow-step
 * highlighting: highlighted blocks/buses get a glow and a thick outline while
 * everything else is dimmed.
 */
class ArchScene : public QGraphicsScene {
  Q_OBJECT
public:
  explicit ArchScene(QObject *parent = nullptr);

  /// Replaces all items with a representation of @p model.
  void setModel(const ArchModel &model);
  const ArchModel &model() const { return m_model; }

  /// Highlights the given block ids and bus ids and dims everything else. If
  /// both lists are empty, behaves like clearHighlight().
  void highlight(const QStringList &blocks, const QStringList &buses);
  void clearHighlight();

  /// Bounding rectangle of the diagram (blocks, buses and legend) with a
  /// small margin. Useful for fit-in-view.
  QRectF diagramRect() const;

  ArchBlockItem *blockItem(const QString &id) const;
  ArchBusItem *busItem(const QString &id) const;

  /// Selects the given block (clearing any other selection). An empty id
  /// clears the selection.
  void selectBlock(const QString &id);
  /// Id of the currently selected block, or an empty string.
  QString selectedBlock() const;

  /// Called by ArchBlockItem on mouse press.
  void notifyBlockClicked(const QString &id);

  /// The legend drawn below the diagram (nullptr while no model is set).
  ArchLegendItem *legendItem() const { return m_legend; }
  /// Shows / hides the legend below the diagram (the Architecture Explorer
  /// tab shows its legend in a side panel and only includes the in-scene
  /// legend in exported images).
  void setLegendVisible(bool visible);
  bool legendVisible() const { return m_legendVisible; }

signals:
  /// Emitted when a block is clicked with the mouse.
  void blockClicked(const QString &id);
  /// Emitted whenever the selected block changes (empty id: no selection).
  void blockSelectionChanged(const QString &id);

protected:
  void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
  /// Assigns attachment points to all buses, spreading buses that share a
  /// block side so they do not overlap.
  void layoutBuses();
  /// Moves bus labels that overlap other labels or blocks to free positions.
  void resolveLabelOverlaps();

  ArchModel m_model;
  QMap<QString, ArchBlockItem *> m_blockItems;
  QMap<QString, ArchBusItem *> m_busItems;
  QVector<ArchBusItem *> m_busOrder; // in model order
  ArchLegendItem *m_legend = nullptr;
  bool m_legendVisible = true;
  QString m_lastSelected;
};

} // namespace Ripes
