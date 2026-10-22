// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "systemview.h"

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QLabel>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <map>

namespace Ripes {

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

QColor roleColor(const QString &role) {
  if (role == "cpu")
    return QColor(66, 133, 244, 90);
  if (role == "cache" || role == "prefetcher")
    return QColor(52, 168, 83, role == "cache" ? 90 : 45);
  if (role == "interconnect")
    return QColor(171, 71, 188, 80);
  if (role == "memory-controller" || role == "memory")
    return QColor(245, 166, 35, 90);
  if (role == "peripheral")
    return QColor(0, 172, 193, 80);
  return QColor(128, 128, 128, 60);
}

QString shortName(const QString &path) { return path.section('.', -1); }

constexpr qreal kBoxW = 200;
constexpr qreal kBoxH = 66;
constexpr qreal kGapX = 26;
constexpr qreal kGapY = 40;

} // namespace

SystemView::SystemView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
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
  splitter->setSizes({650, 350});
  layout->addWidget(splitter, 1);

  connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
    const auto items = m_scene->selectedItems();
    if (!items.isEmpty()) {
      m_selected = items.first()->data(0).toString();
      showDetails();
    }
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
  m_view->fitInView(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20),
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
  m_scene->clear();
  const QColor text = palette().color(QPalette::Text);
  const QString engine = m_header.source.engine;

  m_note->setText(
      QString("Components reported as <b>simulated</b> by <b>%1</b> (%2). "
              "Lines are connections reported by the engine. Dashed boxes are "
              "<b>not simulated</b> by this engine and only shown for "
              "orientation. Click a component for details; Ctrl + wheel "
              "zooms.")
          .arg(engine.toHtmlEscaped(), m_header.source.model.toHtmlEscaped()));

  // Group components into rows by role.
  std::map<int, std::vector<const Trace::SystemComponent *>> rows;
  for (const auto &c : m_header.system) {
    int row = roleOrder().indexOf(c.role);
    if (row < 0)
      row = roleOrder().size() - 1;
    rows[row].push_back(&c);
  }
  for (auto &[row, comps] : rows)
    std::sort(comps.begin(), comps.end(),
              [](const auto *a, const auto *b) { return a->path < b->path; });

  size_t widest = 1;
  for (const auto &[row, comps] : rows)
    widest = std::max(widest, comps.size());
  const qreal totalWidth = widest * kBoxW + (widest - 1) * kGapX;

  auto addBox = [&](const QString &key, const QString &title,
                    const QString &subtitle, const QColor &fill, qreal x,
                    qreal y, bool dashed) {
    auto *rect = m_scene->addRect(0, 0, kBoxW, kBoxH);
    rect->setPos(x, y);
    rect->setBrush(fill);
    QPen pen(dashed ? QColor(128, 128, 128) : text);
    pen.setStyle(dashed ? Qt::DashLine : Qt::SolidLine);
    pen.setWidthF(dashed ? 1.0 : 1.4);
    rect->setPen(pen);
    rect->setData(0, key);
    rect->setFlag(QGraphicsItem::ItemIsSelectable, !dashed);
    rect->setToolTip(key);
    auto *label = new QGraphicsTextItem(rect);
    label->setDefaultTextColor(dashed ? QColor(128, 128, 128) : text);
    label->setTextWidth(kBoxW - 8);
    label->setHtml("<div align='center'><b>" + title.toHtmlEscaped() +
                   "</b><br><small>" + subtitle.toHtmlEscaped() +
                   "</small></div>");
    label->setPos(4, 6);
    return rect;
  };

  std::map<QString, QRectF> boxes;
  qreal y = 0;
  for (const auto &[row, comps] : rows) {
    const qreal rowWidth = comps.size() * kBoxW + (comps.size() - 1) * kGapX;
    qreal x = (totalWidth - rowWidth) / 2;
    for (const auto *c : comps) {
      auto *rect = addBox(c->path, shortName(c->path), c->type,
                          roleColor(c->role), x, y, false);
      boxes[c->path] = rect->sceneBoundingRect();
      if (c->path == m_selected)
        rect->setSelected(true);
      x += kBoxW + kGapX;
    }
    y += kBoxH + kGapY;
  }

  // Connections reported by the engine.
  QPen linePen(text);
  linePen.setWidthF(1.2);
  for (const auto &c : m_header.system)
    for (const auto &target : c.connections) {
      if (!boxes.count(c.path) || !boxes.count(target))
        continue;
      const QRectF a = boxes[c.path];
      const QRectF b = boxes[target];
      QPointF from(a.center().x(), a.bottom());
      QPointF to(b.center().x(), b.top());
      if (b.top() <= a.top()) { // same row or upwards: connect sides
        from = QPointF(a.right(), a.center().y());
        to = QPointF(b.left(), b.center().y());
      }
      auto *line = m_scene->addLine(QLineF(from, to), linePen);
      line->setZValue(-1);
    }

  // Typical SoC components that are not part of this simulation.
  QStringList missing = {"dma", "accelerator"};
  for (const QString role : {"cache", "interconnect", "memory-controller"}) {
    bool present = false;
    for (const auto &c : m_header.system)
      present |= c.role == role;
    if (!present)
      missing.prepend(role);
  }
  qreal my = 0;
  const qreal mx = totalWidth + 3 * kGapX;
  for (const auto &role : missing) {
    addBox("not-simulated:" + role, roleTitle(role),
           "not simulated by " + engine, QColor(128, 128, 128, 20), mx, my,
           true);
    my += kBoxH + kGapX;
  }

  m_view->resetTransform();
  m_scene->setSceneRect(
      m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20));
  m_autoFit = true;
  fitTopology();
  showDetails();
}

void SystemView::showDetails() {
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
  auto table = [](const QVariantMap &map, const QVariantMap &info) {
    QString html = "<table cellspacing='0' cellpadding='2'>";
    for (auto it = map.begin(); it != map.end(); ++it)
      html += "<tr><td><code>" + it.key().toHtmlEscaped() +
              "</code></td><td align='right'>" +
              it.value().toString().toHtmlEscaped() + "</td><td><small>" +
              info.value(it.key()).toString().toHtmlEscaped() +
              "</small></td></tr>";
    return html + "</table>";
  };

  QString html = "<h3>" + shortName(comp->path).toHtmlEscaped() +
                 " <span style='font-weight:normal'>(" +
                 roleTitle(comp->role).toHtmlEscaped() + ")</span></h3>";
  html += "<p><b>Path:</b> <code>" + comp->path.toHtmlEscaped() +
          "</code><br><b>Type:</b> " + comp->type.toHtmlEscaped() + "</p>";
  if (!comp->description.isEmpty())
    html += "<p>" + comp->description.toHtmlEscaped() + "</p>";
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
