// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "systemview.h"

#include <QApplication>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "theme/painting.h"
#include "theme/theme.h"

namespace Ripes {

using namespace Theme;

namespace {

const QStringList &roleOrder() {
  static const QStringList order = {
      "cpu",    "prefetcher", "cache", "interconnect", "memory-controller",
      "memory", "peripheral", "other"};
  return order;
}

QString roleTitle(const QString &role) {
  if (role == "cpu")
    return "CPU";
  if (role == "memory-controller")
    return "Memory controller";
  if (role == "dma")
    return "DMA engine";
  if (role == "accelerator")
    return "AI accelerator";
  QString t = role;
  if (!t.isEmpty())
    t[0] = t[0].toUpper();
  return t;
}

QString shortName(const QString &path) { return path.section('.', -1); }

bool isDescendant(const QString &path, const QString &ancestor) {
  return path.startsWith(ancestor + ".");
}

QString formatBytes(qint64 bytes) {
  if (bytes >= (1 << 20) && bytes % (1 << 20) == 0)
    return QString("%1 MiB").arg(bytes >> 20);
  if (bytes >= 1024 && bytes % 1024 == 0)
    return QString("%1 KiB").arg(bytes >> 10);
  return QString("%1 B").arg(bytes);
}

constexpr qreal kChipW = 204;
constexpr qreal kChipH = 76;
constexpr qreal kSubH = 30;
constexpr qreal kSubGap = 6;
constexpr qreal kGapX = 30;
constexpr qreal kGapY = 52;
constexpr qreal kDieMargin = 30;
constexpr qreal kDieTop = 38;
constexpr qreal kNsW = 172;
constexpr qreal kNsH = 56;

/// One statistic (or parameter) shown on a chip, taken verbatim from the
/// trace. fraction >= 0 adds a meter; only rates reported by the engine get
/// one.
struct KeyStat {
  QString text;
  double fraction = -1;
  QColor color;
};

KeyStat keyStat(const Trace::SystemComponent &c) {
  const auto &t = tokens();
  const auto &s = c.statistics;
  auto num = [&](const QString &key) { return s.value(key).toDouble(); };
  if (s.contains("demandMissRate::total"))
    return {QString("miss rate %1 %")
                .arg(100 * num("demandMissRate::total"), 0, 'f', 1),
            num("demandMissRate::total"), t.warn};
  if (s.contains("readRowHitRate"))
    return {
        QString("row-buffer hits %1 %").arg(num("readRowHitRate"), 0, 'f', 1),
        num("readRowHitRate") / 100.0, t.ok};
  if (s.contains("ipc"))
    return {s.contains("cpi") ? QString("IPC %1 · CPI %2")
                                    .arg(num("ipc"), 0, 'f', 3)
                                    .arg(num("cpi"), 0, 'f', 2)
                              : QString("IPC %1").arg(num("ipc"), 0, 'f', 3),
            -1, t.text};
  if (s.contains("instructions_retired") && s.contains("cycles"))
    return {QString("%1 retired · %2 cycles")
                .arg(s.value("instructions_retired").toLongLong())
                .arg(s.value("cycles").toLongLong()),
            -1, t.text};
  if (s.contains("pktCount::total"))
    return {QString("%1 packets").arg(s.value("pktCount::total").toLongLong()),
            -1, t.text};
  if (s.contains("readReqs"))
    return {QString("%1 reads · %2 writes")
                .arg(s.value("readReqs").toLongLong())
                .arg(s.value("writeReqs").toLongLong()),
            -1, t.text};
  if (s.contains("pfIssued"))
    return {
        QString("%1 prefetches issued").arg(s.value("pfIssued").toLongLong()),
        -1, t.text};
  if (s.contains("bytesRead::total"))
    return {
        QString("%1 bytes read").arg(s.value("bytesRead::total").toLongLong()),
        -1, t.text};
  const auto &p = c.parameters;
  if (c.role == "cache" && p.contains("size"))
    return {p.contains("assoc")
                ? QString("%1 · %2-way")
                      .arg(formatBytes(p.value("size").toLongLong()))
                      .arg(p.value("assoc").toLongLong())
                : formatBytes(p.value("size").toLongLong()),
            -1, t.muted};
  if (p.contains("stages"))
    return {QString("%1 stage(s) · %2 lane(s)")
                .arg(p.value("stages").toInt())
                .arg(p.value("lanes").toInt()),
            -1, t.muted};
  if (p.contains("base_address"))
    return {"@ " + p.value("base_address").toString(), -1, t.muted};
  return {};
}

class EdgeItem : public QGraphicsPathItem {
public:
  EdgeItem(const QPainterPath &path, const QPointF &tip, bool down)
      : QGraphicsPathItem(path), m_tip(tip), m_down(down) {
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    setZValue(-1);
  }
  void setHighlight(int delta) {
    m_highlight = std::max(0, m_highlight + delta);
    update();
  }
  QRectF boundingRect() const override {
    return QGraphicsPathItem::boundingRect().adjusted(-8, -8, 8, 8);
  }
  void paint(QPainter *p, const QStyleOptionGraphicsItem *,
             QWidget *) override {
    const auto &t = tokens();
    const bool hi = m_highlight > 0;
    const QColor c =
        hi ? t.accent
           : (isGame() ? mix(t.borderStrong, t.accent, 0.25) : t.text);
    p->setRenderHint(QPainter::Antialiasing);
    if (hi && isGame()) {
      p->setPen(QPen(withAlpha(t.accent, 60), 7, Qt::SolidLine, Qt::RoundCap,
                     Qt::RoundJoin));
      p->drawPath(path());
    }
    p->setPen(
        QPen(c, hi ? 2.4 : 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path());
    const qreal d = m_down ? -1 : 1;
    const QPolygonF head({m_tip, QPointF(m_tip.x() - 5, m_tip.y() + d * 9),
                          QPointF(m_tip.x() + 5, m_tip.y() + d * 9)});
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    p->drawPolygon(head);
  }

private:
  QPointF m_tip;
  bool m_down;
  int m_highlight = 0;
};

class ChipItem : public QGraphicsRectItem {
public:
  ChipItem(const QString &key, const QString &role, const QString &title,
           const QString &subtitle, bool simulated, KeyStat stat, bool nested)
      : m_role(role), m_title(title), m_subtitle(subtitle),
        m_simulated(simulated), m_stat(std::move(stat)), m_nested(nested) {
    setData(0, key);
    setAcceptHoverEvents(simulated);
    setFlag(QGraphicsItem::ItemIsSelectable, simulated);
    setCursor(simulated ? Qt::PointingHandCursor : Qt::ArrowCursor);
  }
  void addEdge(EdgeItem *e) { m_edges.push_back(e); }
  /// Height of the chip's own content (children are drawn below it).
  void setHeaderHeight(qreal h) { m_headerH = h; }

  QRectF boundingRect() const override {
    return rect().adjusted(-12, -12, 12, 12);
  }

  void paint(QPainter *p, const QStyleOptionGraphicsItem *,
             QWidget *) override {
    const auto &t = tokens();
    const QColor rc = roleColor(m_role);
    const QRectF r = rect();
    const QFont base = scene() ? scene()->font() : QApplication::font();
    p->setRenderHint(QPainter::Antialiasing);
    const qreal radius = m_nested ? 7 : 10;

    if (!m_simulated) {
      p->setPen(QPen(withAlpha(t.muted, 190), 1.3, Qt::DashLine));
      p->setBrush(withAlpha(t.muted, t.dark ? 14 : 10));
      p->drawRoundedRect(r, radius, radius);
      drawStripes(*p, r, radius, withAlpha(t.muted, t.dark ? 22 : 26), 11);
      const QRectF icon(r.left() + 10, r.center().y() - 13, 26, 26);
      drawRoleIcon(*p, icon, m_role, withAlpha(t.muted, 200));
      QFont tf = base;
      tf.setBold(true);
      p->setFont(tf);
      p->setPen(t.muted);
      p->drawText(QRectF(icon.right() + 8, r.top() + 8, r.width() - 50, 18),
                  Qt::AlignLeft | Qt::AlignVCenter, m_title);
      QFont sf = base;
      sf.setItalic(true);
      if (sf.pointSizeF() > 0)
        sf.setPointSizeF(sf.pointSizeF() * 0.86);
      p->setFont(sf);
      p->drawText(QRectF(icon.right() + 8, r.top() + 27, r.width() - 50, 18),
                  Qt::AlignLeft | Qt::AlignVCenter, m_subtitle);
      return;
    }

    const bool selected = isSelected();
    if (!m_nested) {
      // Package pins.
      p->setPen(Qt::NoPen);
      p->setBrush(mix(t.borderStrong, rc, 0.45));
      const int pins = std::max(2, int((r.height() - 20) / 16));
      for (int i = 0; i < pins; ++i) {
        const qreal y = r.top() + 12 + i * 16;
        p->drawRoundedRect(QRectF(r.left() - 5, y, 6, 4), 1, 1);
        p->drawRoundedRect(QRectF(r.right() - 1, y, 6, 4), 1, 1);
      }
    }
    if (selected)
      drawGlow(*p, r, radius, t.accent, 7);
    drawCard(*p, r, radius, mix(t.card, rc, t.dark ? 0.17 : 0.08),
             selected  ? t.accent
             : m_hover ? rc
                       : withAlpha(rc, t.dark ? 150 : 170),
             selected ? 2.2 : 1.3, !m_nested);

    const qreal iconSize = m_nested ? 20 : 30;
    const QRectF iconBox(r.left() + (m_nested ? 6 : 10),
                         m_nested ? r.center().y() - iconSize / 2
                                  : r.top() + 10,
                         iconSize, iconSize);
    p->setPen(Qt::NoPen);
    p->setBrush(withAlpha(rc, t.dark ? 60 : 40));
    p->drawRoundedRect(iconBox, 6, 6);
    drawRoleIcon(*p, iconBox.adjusted(3, 3, -3, -3), m_role,
                 t.dark ? rc.lighter(125) : rc.darker(115));

    const qreal tx = iconBox.right() + 8;
    QFont tf = base;
    tf.setBold(true);
    QFont sf = base;
    if (sf.pointSizeF() > 0)
      sf.setPointSizeF(sf.pointSizeF() * 0.84);
    const QFont rf = overlineFont(base, 0.66);
    if (m_nested) {
      p->setFont(sf);
      p->setPen(t.text);
      const QString line =
          m_stat.text.isEmpty() ? m_title : m_title + " · " + m_stat.text;
      p->drawText(QRectF(tx, r.top(), r.right() - tx - 6, r.height()),
                  Qt::AlignLeft | Qt::AlignVCenter,
                  QFontMetricsF(sf).elidedText(line, Qt::ElideRight,
                                               r.right() - tx - 6));
      return;
    }
    const QFontMetricsF tfm(tf);
    const qreal titleW = tfm.horizontalAdvance(m_title);
    const qreal roleW = QFontMetricsF(rf).horizontalAdvance(roleTitle(m_role));
    if (titleW + roleW + 12 < r.right() - tx - 8) {
      p->setFont(rf);
      p->setPen(t.dark ? rc.lighter(120) : rc.darker(120));
      p->drawText(QRectF(tx, r.top() + 6, r.right() - tx - 8, 14),
                  Qt::AlignRight | Qt::AlignVCenter, roleTitle(m_role));
    }
    p->setFont(tf);
    p->setPen(t.text);
    p->drawText(QRectF(tx, r.top() + 8, r.right() - tx - 8, 20),
                Qt::AlignLeft | Qt::AlignVCenter,
                tfm.elidedText(m_title, Qt::ElideRight, r.right() - tx - 8));
    p->setFont(sf);
    p->setPen(t.muted);
    p->drawText(QRectF(tx, r.top() + 27, r.right() - tx - 8, 16),
                Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetricsF(sf).elidedText(m_subtitle, Qt::ElideRight,
                                             r.right() - tx - 8));
    if (!m_stat.text.isEmpty()) {
      const qreal y = r.top() + m_headerH - 22;
      qreal x = r.left() + 12;
      if (m_stat.fraction >= 0) {
        drawProgressBar(*p, QRectF(x, y + 6, 56, 7), m_stat.fraction,
                        m_stat.color);
        x += 64;
      }
      p->setFont(sf);
      p->setPen(m_stat.fraction >= 0 ? t.text : m_stat.color);
      p->drawText(QRectF(x, y, r.right() - x - 8, 18),
                  Qt::AlignLeft | Qt::AlignVCenter,
                  QFontMetricsF(sf).elidedText(m_stat.text, Qt::ElideRight,
                                               r.right() - x - 8));
    }
  }

protected:
  void hoverEnterEvent(QGraphicsSceneHoverEvent *e) override {
    if (!m_hover) {
      m_hover = true;
      for (auto *edge : m_edges)
        edge->setHighlight(+1);
    }
    update();
    QGraphicsRectItem::hoverEnterEvent(e);
  }
  void hoverLeaveEvent(QGraphicsSceneHoverEvent *e) override {
    if (m_hover) {
      m_hover = false;
      for (auto *edge : m_edges)
        edge->setHighlight(-1);
    }
    update();
    QGraphicsRectItem::hoverLeaveEvent(e);
  }
  QVariant itemChange(GraphicsItemChange change,
                      const QVariant &value) override {
    if (change == ItemSelectedHasChanged)
      for (auto *edge : m_edges)
        edge->setHighlight(value.toBool() ? +1 : -1);
    return QGraphicsRectItem::itemChange(change, value);
  }

private:
  QString m_role, m_title, m_subtitle;
  bool m_simulated;
  KeyStat m_stat;
  bool m_nested;
  bool m_hover = false;
  qreal m_headerH = kChipH;
  std::vector<EdgeItem *> m_edges;
};

/// The die: background of the simulated components.
class DieItem : public QGraphicsRectItem {
public:
  DieItem(const QRectF &r, QString label) : m_label(std::move(label)) {
    setRect(r);
    setZValue(-10);
    setAcceptedMouseButtons(Qt::NoButton);
  }
  void paint(QPainter *p, const QStyleOptionGraphicsItem *,
             QWidget *) override {
    const auto &t = tokens();
    const QRectF r = rect();
    p->setRenderHint(QPainter::Antialiasing);
    if (isGame()) {
      drawCard(*p, r, 18, mix(t.base, t.accent, t.dark ? 0.035 : 0.025),
               withAlpha(t.accent, t.dark ? 110 : 120), 1.5, true);
      // Fine grid, like a die photo's standard cells.
      p->save();
      QPainterPath clip;
      clip.addRoundedRect(r.adjusted(2, 2, -2, -2), 16, 16);
      p->setClipPath(clip);
      p->setPen(QPen(withAlpha(t.accent, t.dark ? 16 : 20), 1));
      for (qreal x = r.left() + 24; x < r.right(); x += 24)
        p->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
      for (qreal y = r.top() + 24; y < r.bottom(); y += 24)
        p->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
      p->restore();
      // Orientation mark in the corner.
      p->setPen(Qt::NoPen);
      p->setBrush(withAlpha(t.accent, 150));
      p->drawPolygon(QPolygonF({QPointF(r.left() + 10, r.top() + 10),
                                QPointF(r.left() + 24, r.top() + 10),
                                QPointF(r.left() + 10, r.top() + 24)}));
    } else {
      p->setPen(QPen(t.border, 1.2));
      p->setBrush(t.base);
      p->drawRoundedRect(r, 8, 8);
    }
    const QFont f = overlineFont(QApplication::font(), 0.8);
    p->setFont(f);
    p->setPen(isGame() ? t.accent : t.muted);
    p->drawText(QRectF(r.left() + 32, r.top() + 6, r.width() - 44, 22),
                Qt::AlignLeft | Qt::AlignVCenter, m_label);
  }

private:
  QString m_label;
};

QPainterPath roundedPolyline(const std::vector<QPointF> &pts, qreal radius) {
  QPainterPath path;
  if (pts.empty())
    return path;
  path.moveTo(pts.front());
  for (size_t i = 1; i + 1 < pts.size(); ++i) {
    const QPointF a = pts[i - 1], b = pts[i], c = pts[i + 1];
    const QLineF ab(b, a), bc(b, c);
    const qreal r = std::min({radius, ab.length() / 2, bc.length() / 2});
    if (r <= 0.5) {
      path.lineTo(b);
      continue;
    }
    path.lineTo(ab.pointAt(r / ab.length()));
    path.quadTo(b, bc.pointAt(r / bc.length()));
  }
  path.lineTo(pts.back());
  return path;
}

} // namespace

SystemView::SystemView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  m_note = new QLabel(this);
  m_note->setWordWrap(true);
  m_note->setTextFormat(Qt::RichText);
  layout->addWidget(m_note);

  auto *splitter = new QSplitter(Qt::Vertical, this);
  m_scene = new QGraphicsScene(this);
  m_view = new QGraphicsView(m_scene, splitter);
  m_view->setObjectName("systemTopology");
  m_view->setRenderHint(QPainter::Antialiasing);
  m_view->setDragMode(QGraphicsView::ScrollHandDrag);
  m_view->viewport()->installEventFilter(this);
  m_details = new QTextBrowser(splitter);
  m_details->setObjectName("systemDetails");
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  splitter->setSizes({680, 320});
  layout->addWidget(splitter, 1);

  connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
    const auto items = m_scene->selectedItems();
    if (!items.isEmpty()) {
      m_selected = items.first()->data(0).toString();
      showDetails();
    }
  });
  connect(Notifier::get(), &Notifier::changed, this, [this] {
    if (!m_scene->items().isEmpty())
      rebuild();
  });
}

bool SystemView::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_view->viewport() && event->type() == QEvent::Wheel) {
    auto *wheel = static_cast<QWheelEvent *>(event);
    if (wheel->modifiers() & Qt::ControlModifier) {
      const qreal factor = std::pow(1.15, wheel->angleDelta().y() / 120.0);
      m_view->scale(factor, factor);
      m_autoFit = false;
      return true;
    }
  }
  // Keep the whole topology visible until the user zooms.
  if (watched == m_view->viewport() && m_autoFit &&
      (event->type() == QEvent::Resize || event->type() == QEvent::Show))
    fitTopology();
  return QWidget::eventFilter(watched, event);
}

void SystemView::fitTopology() {
  if (m_scene->items().isEmpty())
    return;
  m_view->fitInView(m_scene->itemsBoundingRect().adjusted(-16, -16, 16, 16),
                    Qt::KeepAspectRatio);
}

void SystemView::setTrace(const Trace::TraceHeader &header) {
  const bool sameSystem =
      header.system == m_header.system && header.source == m_header.source;
  m_header = header;
  if (sameSystem && !m_scene->items().isEmpty()) {
    showDetails();
    return;
  }
  bool selectionExists = false;
  for (const auto &c : m_header.system)
    selectionExists |= c.path == m_selected;
  if (!selectionExists)
    m_selected.clear();
  rebuild();
}

void SystemView::selectComponent(const QString &path) {
  for (auto *item : m_scene->items())
    if (item->data(0).toString() == path) {
      m_scene->clearSelection();
      item->setSelected(true);
      return;
    }
}

void SystemView::rebuild() {
  const auto &t = tokens();
  const QString selected = m_selected;
  m_scene->blockSignals(true);
  m_scene->clear();
  m_scene->blockSignals(false);
  m_view->setBackgroundBrush(t.base);
  const QString engine = m_header.source.engine;

  m_note->setText(
      QString("<b>Simulated by %1</b> (%2): the chips on the die. Arrows are "
              "connections reported by the engine; components inside a chip "
              "are its sub-components. <span style='color:%3'>Dashed: <b>not "
              "simulated</b>, shown for orientation only.</span> Click a "
              "component for details; Ctrl + wheel zooms.")
          .arg(engine.toHtmlEscaped(), m_header.source.model.toHtmlEscaped(),
               t.muted.name()));

  // Top-level components (sub-components are drawn inside their parent).
  std::vector<const Trace::SystemComponent *> top;
  std::map<QString, std::vector<const Trace::SystemComponent *>> children;
  for (const auto &c : m_header.system) {
    const Trace::SystemComponent *parent = nullptr;
    for (const auto &o : m_header.system)
      if (isDescendant(c.path, o.path) &&
          (!parent || o.path.size() > parent->path.size()))
        parent = &o;
    if (parent)
      children[parent->path].push_back(&c);
    else
      top.push_back(&c);
  }
  for (auto &[path, list] : children)
    std::sort(list.begin(), list.end(),
              [](const auto *a, const auto *b) { return a->path < b->path; });

  // Group top-level components into rows by role.
  std::map<int, std::vector<const Trace::SystemComponent *>> rows;
  for (const auto *c : top) {
    int row = roleOrder().indexOf(c->role);
    if (row < 0)
      row = roleOrder().size() - 1;
    rows[row].push_back(c);
  }
  for (auto &[row, comps] : rows)
    std::sort(comps.begin(), comps.end(),
              [](const auto *a, const auto *b) { return a->path < b->path; });

  size_t widest = 1;
  for (const auto &[row, comps] : rows)
    widest = std::max(widest, comps.size());
  const qreal contentWidth = widest * kChipW + (widest - 1) * kGapX;

  struct Placed {
    ChipItem *item;
    QRectF rect; // scene coordinates
    int row;
  };
  std::map<QString, Placed> placed;
  std::vector<qreal> rowTop, rowBottom;
  qreal y = 0;
  int rowIndex = 0;
  for (const auto &[row, comps] : rows) {
    const qreal rowWidth = comps.size() * kChipW + (comps.size() - 1) * kGapX;
    qreal x = (contentWidth - rowWidth) / 2;
    qreal rowH = 0;
    for (const auto *c : comps) {
      const auto &kids = children[c->path];
      const qreal h =
          kChipH + (kids.empty() ? 0 : kids.size() * (kSubH + kSubGap) + 2);
      auto *chip = new ChipItem(c->path, c->role, shortName(c->path), c->type,
                                true, keyStat(*c), false);
      chip->setRect(0, 0, kChipW, h);
      chip->setHeaderHeight(kChipH);
      chip->setPos(x, y);
      chip->setToolTip(c->path + "\n" + c->type + "\nsimulated by " + engine);
      m_scene->addItem(chip);
      placed[c->path] = {chip, QRectF(x, y, kChipW, h), rowIndex};
      qreal sy = kChipH - 4;
      for (const auto *kid : kids) {
        auto *sub = new ChipItem(kid->path, kid->role, shortName(kid->path),
                                 kid->type, true, keyStat(*kid), true);
        sub->setParentItem(chip);
        sub->setRect(0, 0, kChipW - 20, kSubH);
        sub->setPos(10, sy);
        sub->setToolTip(kid->path + "\n" + kid->type + "\nsimulated by " +
                        engine);
        placed[kid->path] = {sub, QRectF(x + 10, y + sy, kChipW - 20, kSubH),
                             rowIndex};
        sy += kSubH + kSubGap;
      }
      rowH = std::max(rowH, h);
      x += kChipW + kGapX;
    }
    rowTop.push_back(y);
    rowBottom.push_back(y + rowH);
    y += rowH + kGapY;
    ++rowIndex;
  }

  // Route the connections reported by the engine as orthogonal traces.
  // Adjacent rows: through the gap between them; longer spans: through a
  // channel at the side of the die. Connections between a component and its
  // own sub-component are shown by containment.
  struct Plan {
    QString from, to;
    bool down = true;
    bool channel = false;
    int gap1 = -1, gap2 = -1;
    qreal exitX = 0, enterX = 0, y1 = 0, y2 = 0, chX = 0;
  };
  std::vector<Plan> plans;
  for (const auto &c : m_header.system)
    for (const auto &target : c.connections) {
      if (!placed.count(c.path) || !placed.count(target) ||
          isDescendant(target, c.path) || isDescendant(c.path, target))
        continue;
      const auto &a = placed[c.path];
      const auto &b = placed[target];
      Plan pl;
      pl.from = c.path;
      pl.to = target;
      pl.down = b.row >= a.row;
      const int span = std::abs(b.row - a.row);
      if (span <= 1) {
        pl.gap1 = pl.down ? a.row : b.row;
      } else {
        pl.channel = true;
        pl.gap1 = pl.down ? a.row : a.row - 1;
        pl.gap2 = pl.down ? b.row - 1 : b.row;
      }
      plans.push_back(pl);
    }

  // Ports: spread the exits and entries on each side (top/bottom) of a chip,
  // ordered by the position of the other end to avoid crossings.
  struct Port {
    qreal otherX;
    size_t plan;
    bool exit;
    bool operator<(const Port &o) const {
      return std::tie(otherX, plan, exit) < std::tie(o.otherX, o.plan, o.exit);
    }
  };
  std::map<std::pair<QString, bool>, std::vector<Port>> ports; // bool: bottom
  for (size_t i = 0; i < plans.size(); ++i) {
    const auto &pl = plans[i];
    ports[{pl.from, pl.down}].push_back(
        {placed[pl.to].rect.center().x(), i, true});
    ports[{pl.to, !pl.down}].push_back(
        {placed[pl.from].rect.center().x(), i, false});
  }
  for (auto &[key, list] : ports) {
    std::sort(list.begin(), list.end());
    const QRectF r = placed[key.first].rect;
    for (size_t k = 0; k < list.size(); ++k) {
      const qreal x = r.left() + r.width() * (k + 1) / (list.size() + 1);
      (list[k].exit ? plans[list[k].plan].exitX : plans[list[k].plan].enterX) =
          x;
    }
  }

  // Lanes: distinct heights for the horizontal runs within each gap.
  std::map<int, std::vector<std::pair<size_t, int>>> lanes;
  for (size_t i = 0; i < plans.size(); ++i) {
    lanes[plans[i].gap1].emplace_back(i, 1);
    if (plans[i].channel)
      lanes[plans[i].gap2].emplace_back(i, 2);
  }
  for (auto &[gap, list] : lanes) {
    if (gap < 0 || gap + 1 >= static_cast<int>(rowTop.size()))
      continue;
    const qreal top = rowBottom[gap], bottom = rowTop[gap + 1];
    for (size_t k = 0; k < list.size(); ++k) {
      const qreal ly = top + (bottom - top) * (k + 1) / (list.size() + 1);
      (list[k].second == 1 ? plans[list[k].first].y1
                           : plans[list[k].first].y2) = ly;
    }
  }
  int leftChannels = 0, rightChannels = 0;
  for (auto &pl : plans)
    if (pl.channel) {
      const bool left = placed[pl.to].rect.center().x() <= contentWidth / 2;
      pl.chX = left ? -kDieMargin / 2 - 7 * leftChannels++
                    : contentWidth + kDieMargin / 2 + 7 * rightChannels++;
    }

  for (const auto &pl : plans) {
    const auto &a = placed[pl.from];
    const auto &b = placed[pl.to];
    const qreal exitY = pl.down ? a.rect.bottom() : a.rect.top();
    const qreal enterY = pl.down ? b.rect.top() : b.rect.bottom();
    std::vector<QPointF> pts = {{pl.exitX, exitY}, {pl.exitX, pl.y1}};
    if (pl.channel) {
      pts.push_back({pl.chX, pl.y1});
      pts.push_back({pl.chX, pl.y2});
      pts.push_back({pl.enterX, pl.y2});
    } else {
      pts.push_back({pl.enterX, pl.y1});
    }
    pts.push_back({pl.enterX, enterY});
    auto *edge = new EdgeItem(roundedPolyline(pts, 8), pts.back(), pl.down);
    edge->setToolTip(pl.from + " → " + pl.to);
    m_scene->addItem(edge);
    a.item->addEdge(edge);
    b.item->addEdge(edge);
  }

  // The die around the simulated components.
  const qreal channelSpace =
      7.0 * std::max(leftChannels, rightChannels) + (plans.empty() ? 0 : 4);
  QRectF die(-kDieMargin - channelSpace, -kDieTop,
             contentWidth + 2 * (kDieMargin + channelSpace),
             (rowBottom.empty() ? 0 : rowBottom.back()) + kDieTop + kDieMargin);
  if (!m_header.system.empty())
    m_scene->addItem(
        new DieItem(die, QString("Simulated by %1 · %2")
                             .arg(engine, m_header.source.modelName.isEmpty()
                                              ? m_header.source.model
                                              : m_header.source.modelName)));

  // Typical SoC components that are not part of this simulation, outside
  // the die.
  QStringList missing = {"dma", "accelerator"};
  for (const QString role : {"cache", "interconnect", "memory-controller"}) {
    bool present = false;
    for (const auto &c : m_header.system)
      present |= c.role == role;
    if (!present)
      missing.prepend(role);
  }
  const qreal nsRowWidth = missing.size() * kNsW + (missing.size() - 1) * 16;
  const bool below = nsRowWidth <= die.width();
  qreal nx = below ? die.center().x() - nsRowWidth / 2 : die.right() + 40;
  qreal ny = below ? die.bottom() + 46 : die.top() + 28;
  auto *header = m_scene->addSimpleText(
      "Not simulated by " + engine, overlineFont(QApplication::font(), 0.8));
  header->setBrush(t.muted);
  header->setPos(nx, ny - 24);
  for (const auto &role : missing) {
    auto *chip =
        new ChipItem("not-simulated:" + role, role, roleTitle(role),
                     "not simulated by " + engine, false, KeyStat(), false);
    chip->setRect(0, 0, kNsW, kNsH);
    chip->setPos(nx, ny);
    chip->setToolTip(roleTitle(role) + ": not simulated by " + engine +
                     ". Shown for orientation only.");
    m_scene->addItem(chip);
    if (below)
      nx += kNsW + 16;
    else
      ny += kNsH + 14;
  }

  m_selected = selected;
  if (!m_selected.isEmpty() && placed.count(m_selected))
    placed[m_selected].item->setSelected(true);

  m_view->resetTransform();
  m_scene->setSceneRect(
      m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20));
  m_autoFit = true;
  fitTopology();
  showDetails();
}

void SystemView::showDetails() {
  const auto &t = tokens();
  const Trace::SystemComponent *comp = nullptr;
  for (const auto &c : m_header.system)
    if (c.path == m_selected)
      comp = &c;

  if (!comp) {
    QString html = QString("<h3>System simulated by %1</h3><p>%2 component(s) "
                           "reported. Select a component to see its "
                           "parameters and statistics.</p>")
                       .arg(m_header.source.engine.toHtmlEscaped())
                       .arg(m_header.system.size());
    if (m_header.system.empty())
      html += "<p>This trace does not describe the simulated system (traces "
              "with schema &lt; 0.2.0 or engines that do not report it).</p>";
    m_details->setHtml(html);
    return;
  }

  // Name, value and - if the trace provides it - the meaning of the value
  // (for gem5 statistics: gem5's own description, quoted verbatim).
  auto table = [&](const QVariantMap &map, const QVariantMap &info) {
    QString html = "<table cellspacing='0' cellpadding='2'>";
    for (auto it = map.begin(); it != map.end(); ++it)
      html += "<tr><td><code>" + it.key().toHtmlEscaped() +
              "</code></td><td align='right'><b>" +
              it.value().toString().toHtmlEscaped() +
              "</b></td><td><small style='color:" + t.muted.name() + "'>" +
              info.value(it.key()).toString().toHtmlEscaped() +
              "</small></td></tr>";
    return html + "</table>";
  };

  const QColor rc = roleColor(comp->role);
  QString html = "<h3>" + shortName(comp->path).toHtmlEscaped() +
                 " <span style='font-weight:normal; color:" + rc.name() +
                 "'>(" + roleTitle(comp->role).toHtmlEscaped() +
                 ")</span></h3>";
  html += "<p><b>Path:</b> <code>" + comp->path.toHtmlEscaped() +
          "</code><br><b>Type:</b> " + comp->type.toHtmlEscaped() + "</p>";
  if (!comp->description.isEmpty())
    html += "<p>" + comp->description.toHtmlEscaped() + "</p>";

  // Rates the engine reported, as meters (values verbatim from the trace).
  QString rates;
  const QString track = mix(t.base, t.border, 0.85).name();
  for (auto it = comp->statistics.begin(); it != comp->statistics.end(); ++it) {
    const QString key = it.key();
    double fraction = -1;
    if (key.contains("MissRate") || key.endsWith("HitRate") ||
        key.contains("HitRate::")) {
      fraction = it.value().toDouble();
      if (fraction > 1.0)
        fraction /= 100.0; // reported in percent
    }
    if (fraction < 0 || fraction > 1)
      continue;
    const int pct = int(std::lround(fraction * 100));
    const QColor col = key.contains("Miss") ? t.warn : t.ok;
    rates += QString("<tr><td><code>%1</code></td><td width='45%'><table "
                     "width='100%' cellspacing='0' cellpadding='0' "
                     "style='font-size:5pt'><tr>%2%3</tr></table></td><td "
                     "align='right'><b>%4 %</b></td></tr>")
                 .arg(key.toHtmlEscaped(),
                      pct > 0 ? QString("<td width='%1%' bgcolor='%2'></td>")
                                    .arg(pct)
                                    .arg(col.name())
                              : QString(),
                      pct < 100 ? QString("<td width='%1%' bgcolor='%2'></td>")
                                      .arg(100 - pct)
                                      .arg(track)
                                : QString())
                 .arg(fraction * 100, 0, 'f', 1);
  }
  if (!rates.isEmpty())
    html += QString("<h4>Rates <span style='font-weight:normal'>(reported by "
                    "%1)</span></h4><table width='100%' cellspacing='0' "
                    "cellpadding='3'>")
                .arg(m_header.source.engine.toHtmlEscaped()) +
            rates + "</table>";
  if (!comp->parameters.isEmpty())
    html +=
        "<h4>Parameters</h4>" + table(comp->parameters, comp->parameterInfo);
  if (!comp->statistics.isEmpty())
    html += QString("<h4>Statistics <span style='font-weight:normal'>(end of "
                    "simulation, reported by %1)</span></h4>")
                .arg(m_header.source.engine.toHtmlEscaped()) +
            table(comp->statistics, comp->statisticInfo);
  if (!comp->connections.isEmpty())
    html += "<h4>Connected to</h4><p><code>" +
            comp->connections.join("</code>, <code>") + "</code></p>";
  m_details->setHtml(html);
}

} // namespace Ripes
