#include "archscene.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QGuiApplication>
#include <QHash>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPalette>
#include <QSet>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Ripes {

// ============================================================================
// Palette helpers
// ============================================================================

namespace {

constexpr qreal BlockGlowMargin = 7.0;
constexpr qreal BlockCornerRadius = 8.0;
constexpr qreal BusGlowExtra = 8.0;
constexpr qreal DimmedOpacity = 0.22;
constexpr qreal ArrowBaseSize = 9.0;
constexpr qreal AnchorSpacing = 16.0; // min. distance between bus anchors
constexpr qreal ParallelMidOffset = 20.0;
constexpr qreal LabelShiftStep = 20.0; // label spacing for parallel buses
constexpr qreal StraightJogTolerance = 8.0; // snap near-straight routes

qreal basePointSize() {
  const qreal pt = QGuiApplication::font().pointSizeF();
  return pt > 0 ? pt : 9.0;
}

// Scene fonts use unhinted metrics so that text measured at scale 1 (when
// items compute their geometry) keeps the same proportions when the view is
// zoomed; hinted fonts change their advance widths with the zoom factor and
// would overflow the rectangles measured for them.
QFont sceneFont() {
  QFont f = QGuiApplication::font();
  f.setHintingPreference(QFont::PreferNoHinting);
  return f;
}

QFont blockLabelFont() {
  QFont f = sceneFont();
  f.setBold(true);
  f.setPointSizeF(basePointSize() + 1);
  return f;
}

QFont blockSublabelFont() {
  QFont f = sceneFont();
  f.setPointSizeF(std::max(7.0, basePointSize() - 1));
  return f;
}

QFont busLabelFont() {
  QFont f = sceneFont();
  f.setPointSizeF(std::max(7.0, basePointSize() - 1));
  return f;
}

QFont legendFont() {
  QFont f = sceneFont();
  f.setPointSizeF(std::max(7.0, basePointSize() - 1));
  return f;
}

QColor textColor() { return QGuiApplication::palette().color(QPalette::Text); }
QColor highlightColor() {
  return QGuiApplication::palette().color(QPalette::Highlight);
}
QColor baseColor() { return QGuiApplication::palette().color(QPalette::Base); }

} // namespace

bool archPaletteIsDark() {
  return QGuiApplication::palette().color(QPalette::Base).lightness() < 128;
}

QColor archBlockKindColor(ArchBlockKind kind, bool dark) {
  // Hue per kind; light mode uses pastel fills (dark text), dark mode uses
  // deep fills (light text). Grey kinds use low saturation.
  int hue = 0;
  int sat = dark ? 110 : 140;
  switch (kind) {
  case ArchBlockKind::Cpu:
    hue = 215;
    break;
  case ArchBlockKind::Gpu:
    hue = 135;
    break;
  case ArchBlockKind::Accel:
    hue = 285;
    break;
  case ArchBlockKind::Dma:
    hue = 160;
    sat = dark ? 90 : 110;
    break;
  case ArchBlockKind::Memory:
    hue = 30;
    break;
  case ArchBlockKind::Cache:
    hue = 50;
    break;
  case ArchBlockKind::Io:
    hue = 185;
    break;
  case ArchBlockKind::Bridge:
    hue = 0;
    break;
  case ArchBlockKind::Audio:
    hue = 330;
    break;
  case ArchBlockKind::Video:
    hue = 255;
    break;
  case ArchBlockKind::Bus:
    hue = 0;
    sat = 0;
    break;
  case ArchBlockKind::Misc:
    hue = 25;
    sat = dark ? 25 : 35;
    break;
  }
  const int light = dark ? 72 : 208;
  return QColor::fromHsl(hue, sat, light);
}

QPen archBusKindPen(ArchBusKind kind, bool dark) {
  QPen pen;
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  switch (kind) {
  case ArchBusKind::Bus:
    pen.setColor(dark ? QColor(205, 205, 205) : QColor(55, 55, 55));
    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(2.0);
    break;
  case ArchBusKind::Dma:
    pen.setColor(dark ? QColor(120, 190, 255) : QColor(25, 85, 165));
    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(4.5);
    break;
  case ArchBusKind::Irq:
    pen.setColor(dark ? QColor(255, 125, 125) : QColor(190, 30, 30));
    pen.setStyle(Qt::DashLine);
    pen.setWidthF(2.0);
    break;
  case ArchBusKind::Mailbox:
    pen.setColor(dark ? QColor(235, 175, 90) : QColor(170, 95, 10));
    pen.setStyle(Qt::DashDotLine);
    pen.setWidthF(2.0);
    break;
  case ArchBusKind::Link:
    pen.setColor(dark ? QColor(95, 215, 200) : QColor(0, 128, 118));
    pen.setStyle(Qt::SolidLine);
    pen.setWidthF(2.5);
    break;
  }
  return pen;
}

// Kind names are translated in the "Ripes::ArchScene" context so that the
// scene legend (not a QObject) and the tab share one set of translations.
// (Written out in full because lupdate does not expand macros.)

QString archBlockKindDisplayName(ArchBlockKind kind) {
  switch (kind) {
  case ArchBlockKind::Cpu:
    return QCoreApplication::translate("Ripes::ArchScene", "CPU");
  case ArchBlockKind::Gpu:
    return QCoreApplication::translate("Ripes::ArchScene", "GPU");
  case ArchBlockKind::Accel:
    return QCoreApplication::translate("Ripes::ArchScene", "Accelerator");
  case ArchBlockKind::Dma:
    return QCoreApplication::translate("Ripes::ArchScene", "DMA engine");
  case ArchBlockKind::Memory:
    return QCoreApplication::translate("Ripes::ArchScene", "Memory");
  case ArchBlockKind::Cache:
    return QCoreApplication::translate("Ripes::ArchScene", "Cache");
  case ArchBlockKind::Io:
    return QCoreApplication::translate("Ripes::ArchScene", "I/O");
  case ArchBlockKind::Bridge:
    return QCoreApplication::translate("Ripes::ArchScene", "Bridge");
  case ArchBlockKind::Audio:
    return QCoreApplication::translate("Ripes::ArchScene", "Audio");
  case ArchBlockKind::Video:
    return QCoreApplication::translate("Ripes::ArchScene", "Video");
  case ArchBlockKind::Bus:
    return QCoreApplication::translate("Ripes::ArchScene", "Bus / Interconnect");
  case ArchBlockKind::Misc:
    return QCoreApplication::translate("Ripes::ArchScene", "Misc");
  }
  return QCoreApplication::translate("Ripes::ArchScene", "Misc");
}

QString archBusKindDisplayName(ArchBusKind kind) {
  switch (kind) {
  case ArchBusKind::Bus:
    return QCoreApplication::translate("Ripes::ArchScene", "Bus");
  case ArchBusKind::Dma:
    return QCoreApplication::translate("Ripes::ArchScene", "DMA");
  case ArchBusKind::Irq:
    return QCoreApplication::translate("Ripes::ArchScene", "Interrupt");
  case ArchBusKind::Mailbox:
    return QCoreApplication::translate("Ripes::ArchScene", "Mailbox");
  case ArchBusKind::Link:
    return QCoreApplication::translate("Ripes::ArchScene", "Link");
  }
  return QCoreApplication::translate("Ripes::ArchScene", "Bus");
}

QString archBlockKindDescription(ArchBlockKind kind) {
  switch (kind) {
  case ArchBlockKind::Cpu:
    return QCoreApplication::translate("Ripes::ArchScene", "General-purpose processor core");
  case ArchBlockKind::Gpu:
    return QCoreApplication::translate("Ripes::ArchScene", "GPU / SIMT compute unit");
  case ArchBlockKind::Accel:
    return QCoreApplication::translate("Ripes::ArchScene", "Other accelerator (systolic array, coprocessor, ...)");
  case ArchBlockKind::Dma:
    return QCoreApplication::translate("Ripes::ArchScene", "DMA engine / data mover");
  case ArchBlockKind::Memory:
    return QCoreApplication::translate("Ripes::ArchScene", "Main memory, ROM, local store or on-chip SRAM");
  case ArchBlockKind::Cache:
    return QCoreApplication::translate("Ripes::ArchScene", "Cache or buffer");
  case ArchBlockKind::Io:
    return QCoreApplication::translate("Ripes::ArchScene", "Peripheral or MMIO register window");
  case ArchBlockKind::Bridge:
    return QCoreApplication::translate("Ripes::ArchScene", "Bridge / interface chip");
  case ArchBlockKind::Audio:
    return QCoreApplication::translate("Ripes::ArchScene", "Sound chip");
  case ArchBlockKind::Video:
    return QCoreApplication::translate("Ripes::ArchScene", "Video / display chip");
  case ArchBlockKind::Bus:
    return QCoreApplication::translate("Ripes::ArchScene", "Bus or interconnect drawn as a block");
  case ArchBlockKind::Misc:
    return QCoreApplication::translate("Ripes::ArchScene", "Other component");
  }
  return QCoreApplication::translate("Ripes::ArchScene", "Other component");
}

QString archBusKindDescription(ArchBusKind kind) {
  switch (kind) {
  case ArchBusKind::Bus:
    return QCoreApplication::translate("Ripes::ArchScene", "Solid line: data / address bus or MMIO access");
  case ArchBusKind::Dma:
    return QCoreApplication::translate("Ripes::ArchScene", "Thick line: DMA data transfer");
  case ArchBusKind::Irq:
    return QCoreApplication::translate("Ripes::ArchScene", "Dashed line: interrupt request");
  case ArchBusKind::Mailbox:
    return QCoreApplication::translate("Ripes::ArchScene", "Dash-dot line: mailbox, doorbell or other notification");
  case ArchBusKind::Link:
    return QCoreApplication::translate("Ripes::ArchScene", "Teal line: point-to-point link or control path");
  }
  return QString();
}

// ============================================================================
// ArchBlockItem
// ============================================================================

ArchBlockItem::ArchBlockItem(const ArchBlock &block) : m_block(block) {
  setFlag(QGraphicsItem::ItemIsSelectable, true);
  QString tip = "<b>" + m_block.label.toHtmlEscaped() + "</b>";
  if (!m_block.sublabel.isEmpty())
    tip += "<br/>" + m_block.sublabel.toHtmlEscaped();
  tip += "<br/><i>" + archBlockKindDisplayName(m_block.kind).toHtmlEscaped() +
         "</i> &ndash; " +
         QCoreApplication::translate("Ripes::ArchScene", "click to show its details and connections").toHtmlEscaped();
  setToolTip(tip);
}

bool ArchBlockItem::labelElided() const {
  const QFontMetricsF lfm(blockLabelFont());
  return lfm.horizontalAdvance(m_block.label) > textRect().width() * 0.98;
}

bool ArchBlockItem::sublabelClipped() const {
  if (m_block.sublabel.isEmpty())
    return false;
  const QRectF tr = textRect();
  const QFontMetricsF lfm(blockLabelFont());
  const QFontMetricsF sfm(blockSublabelFont());
  const qreal avail = std::max(0.0, tr.height() - lfm.height() - 2);
  const qreal need =
      sfm.boundingRect(QRectF(0, 0, tr.width(), 10000),
                       Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                       m_block.sublabel)
          .height();
  return need > avail + 0.5;
}

QRectF ArchBlockItem::boundingRect() const {
  const qreal m = BlockGlowMargin + 2;
  return rect().adjusted(-m, -m, m, m);
}

QPainterPath ArchBlockItem::shape() const {
  QPainterPath path;
  path.addRoundedRect(rect(), BlockCornerRadius, BlockCornerRadius);
  return path;
}

QRectF ArchBlockItem::sceneRect() const {
  return QRectF(pos(), QSizeF(m_block.w, m_block.h));
}

void ArchBlockItem::setHighlighted(bool highlighted) {
  if (m_highlighted == highlighted)
    return;
  m_highlighted = highlighted;
  update();
}

void ArchBlockItem::setDimmed(bool dimmed) {
  if (m_dimmed == dimmed)
    return;
  m_dimmed = dimmed;
  setOpacity(dimmed ? DimmedOpacity : 1.0);
  update();
}

void ArchBlockItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
  // Let QGraphicsItem handle selection, then notify the scene.
  QGraphicsItem::mousePressEvent(event);
  event->accept();
  if (auto *archScene = qobject_cast<ArchScene *>(scene()))
    archScene->notifyBlockClicked(m_block.id);
}

void ArchBlockItem::paint(QPainter *painter,
                          const QStyleOptionGraphicsItem *option,
                          QWidget *widget) {
  Q_UNUSED(option);
  Q_UNUSED(widget);

  const bool dark = archPaletteIsDark();
  const QColor text = textColor();
  const QColor hl = highlightColor();
  const QRectF r = rect();

  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setRenderHint(QPainter::TextAntialiasing, true);

  if (m_highlighted) {
    QColor glow = hl;
    glow.setAlpha(dark ? 120 : 90);
    painter->setPen(Qt::NoPen);
    painter->setBrush(glow);
    painter->drawRoundedRect(r.adjusted(-BlockGlowMargin, -BlockGlowMargin,
                                        BlockGlowMargin, BlockGlowMargin),
                             BlockCornerRadius + 4, BlockCornerRadius + 4);
  }

  QPen pen;
  if (m_highlighted)
    pen = QPen(hl, 3.0);
  else if (isSelected())
    pen = QPen(hl, 2.2);
  else
    pen = QPen(text, 1.2);
  pen.setJoinStyle(Qt::RoundJoin);
  painter->setPen(pen);
  painter->setBrush(archBlockKindColor(m_block.kind, dark));
  painter->drawRoundedRect(r, BlockCornerRadius, BlockCornerRadius);

  // ----- text
  const QRectF textRect = this->textRect();
  if (textRect.width() <= 0 || textRect.height() <= 0)
    return;

  const QFont labelFont = blockLabelFont();
  const QFont subFont = blockSublabelFont();
  const QFontMetricsF lfm(labelFont);
  const QFontMetricsF sfm(subFont);

  const QString label =
      lfm.elidedText(m_block.label, Qt::ElideRight, textRect.width() * 0.98);
  const qreal labelH = lfm.height();

  qreal subH = 0;
  const int subFlags = Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap;
  if (!m_block.sublabel.isEmpty()) {
    const QRectF avail(0, 0, textRect.width(),
                       std::max(0.0, textRect.height() - labelH - 2));
    subH = sfm.boundingRect(avail, subFlags, m_block.sublabel).height();
    subH = std::min(subH, avail.height());
  }

  const qreal totalH = labelH + (subH > 0 ? subH + 2 : 0);
  const qreal top =
      textRect.top() + std::max(0.0, (textRect.height() - totalH) / 2);

  painter->setPen(text);
  painter->setFont(labelFont);
  painter->drawText(QRectF(textRect.left(), top, textRect.width(), labelH),
                    Qt::AlignHCenter | Qt::AlignVCenter, label);

  if (subH > 0) {
    painter->setFont(subFont);
    const QRectF subRect(textRect.left(), top + labelH + 2, textRect.width(),
                         subH);
    painter->drawText(subRect, subFlags, m_block.sublabel);
  }
}

// ============================================================================
// ArchBusItem
// ============================================================================

ArchBusItem::ArchBusItem(const ArchBus &bus, ArchBlockItem *from,
                         ArchBlockItem *to)
    : m_bus(bus), m_from(from), m_to(to) {
  Q_ASSERT(from != nullptr && to != nullptr);
  setFlag(QGraphicsItem::ItemIsSelectable, false);
  setAcceptHoverEvents(true);

  QString tip;
  if (!m_bus.label.isEmpty())
    tip += m_bus.label + "\n";
  // e.g. "Interrupt: GPU registers -> CPU"
  tip += archBusKindDisplayName(m_bus.kind) + ": " + m_from->block().label +
         (m_bus.bidirectional ? QStringLiteral(" ↔ ")
                              : QStringLiteral(" → ")) +
         m_to->block().label;
  for (const auto &p : m_bus.props)
    tip += "\n" + p.first + ": " + p.second;
  setToolTip(tip);

  updateGeometry();
}

QPolygonF ArchBusItem::arrowHead(const QPointF &tip, const QPointF &fromDir,
                                 qreal size) {
  const QLineF line(fromDir, tip);
  if (line.length() < 1e-3)
    return QPolygonF();
  const QPointF d = (tip - fromDir) / line.length();
  const QPointF n(-d.y(), d.x());
  QPolygonF poly;
  poly << tip << (tip - d * size + n * size * 0.45)
       << (tip - d * size - n * size * 0.45);
  return poly;
}

ArchBusItem::Route ArchBusItem::computeRoute(const QRectF &a,
                                             const QRectF &b) {
  const QPointF ca = a.center();
  const QPointF cb = b.center();
  const bool overlapX = a.right() > b.left() && b.right() > a.left();
  const bool overlapY = a.bottom() > b.top() && b.bottom() > a.top();

  Route r;
  if (overlapX && !overlapY)
    r.vertical = true;
  else if (overlapY && !overlapX)
    r.vertical = false;
  else
    r.vertical = std::abs(cb.y() - ca.y()) >= std::abs(cb.x() - ca.x());

  if (r.vertical) {
    const bool down = cb.y() >= ca.y();
    r.fromSide = down ? Side::Bottom : Side::Top;
    r.toSide = down ? Side::Top : Side::Bottom;
  } else {
    const bool rightwards = cb.x() >= ca.x();
    r.fromSide = rightwards ? Side::Right : Side::Left;
    r.toSide = rightwards ? Side::Left : Side::Right;
  }
  return r;
}

qreal ArchBusItem::preferredAnchor(const QRectF &rect, Side side,
                                   const QRectF &other) {
  const bool horizontalSide = (side == Side::Top || side == Side::Bottom);
  const qreal lo = horizontalSide ? rect.left() : rect.top();
  const qreal hi = horizontalSide ? rect.right() : rect.bottom();
  const qreal olo = horizontalSide ? other.left() : other.top();
  const qreal ohi = horizontalSide ? other.right() : other.bottom();
  const qreal overlapLo = std::max(lo, olo);
  const qreal overlapHi = std::min(hi, ohi);
  if (overlapHi - overlapLo > 1.0)
    return (overlapLo + overlapHi) / 2;
  const qreal margin = std::min(12.0, (hi - lo) / 4);
  const qreal otherCenter = (olo + ohi) / 2;
  return std::clamp(otherCenter, lo + margin, hi - margin);
}

QPointF ArchBusItem::anchorPoint(const QRectF &rect, Side side, qreal coord) {
  switch (side) {
  case Side::Top:
    return QPointF(coord, rect.top());
  case Side::Bottom:
    return QPointF(coord, rect.bottom());
  case Side::Left:
    return QPointF(rect.left(), coord);
  case Side::Right:
    return QPointF(rect.right(), coord);
  }
  return rect.center();
}

void ArchBusItem::updateGeometry() {
  const QRectF a = m_from->sceneRect();
  const QRectF b = m_to->sceneRect();
  const Route route = computeRoute(a, b);
  const QPointF pa =
      anchorPoint(a, route.fromSide, preferredAnchor(a, route.fromSide, b));
  const QPointF pb =
      anchorPoint(b, route.toSide, preferredAnchor(b, route.toSide, a));
  setGeometry(route, pa, pb, 0, 0);
}

void ArchBusItem::setGeometry(const Route &route, const QPointF &pa,
                              const QPointF &pb, qreal midOffset,
                              qreal labelShift) {
  prepareGeometryChange();

  m_points.clear();
  if (route.vertical) {
    if (std::abs(pa.x() - pb.x()) <= StraightJogTolerance) {
      // Nearly aligned anchors: draw a straight line instead of a tiny jog.
      const qreal x = (pa.x() + pb.x()) / 2;
      m_points << QPointF(x, pa.y()) << QPointF(x, pb.y());
    } else {
      const qreal midY = (pa.y() + pb.y()) / 2 + midOffset;
      m_points << pa << QPointF(pa.x(), midY) << QPointF(pb.x(), midY) << pb;
    }
  } else {
    if (std::abs(pa.y() - pb.y()) <= StraightJogTolerance) {
      const qreal y = (pa.y() + pb.y()) / 2;
      m_points << QPointF(pa.x(), y) << QPointF(pb.x(), y);
    } else {
      const qreal midX = (pa.x() + pb.x()) / 2 + midOffset;
      m_points << pa << QPointF(midX, pa.y()) << QPointF(midX, pb.y()) << pb;
    }
  }

  // Build the drawn path, shortening the arrow end(s) so that the thick line
  // does not poke out from under the arrow head.
  const QPen pen = archBusKindPen(m_bus.kind, archPaletteIsDark());
  const qreal arrowSize = ArrowBaseSize + pen.widthF();
  QPolygonF drawPoints = m_points;
  if (drawPoints.size() >= 2) {
    {
      QLineF last(drawPoints[drawPoints.size() - 2], drawPoints.last());
      if (last.length() > arrowSize)
        last.setLength(last.length() - arrowSize * 0.6);
      drawPoints.last() = last.p2();
    }
    if (m_bus.bidirectional) {
      QLineF first(drawPoints[1], drawPoints[0]);
      if (first.length() > arrowSize)
        first.setLength(first.length() - arrowSize * 0.6);
      drawPoints[0] = first.p2();
    }
  }
  m_path = QPainterPath();
  if (!drawPoints.isEmpty()) {
    m_path.moveTo(drawPoints.first());
    for (int i = 1; i < drawPoints.size(); ++i)
      m_path.lineTo(drawPoints[i]);
  }

  // ----- label placement: next to the midpoint of the longest segment
  m_labelRect = QRectF();
  if (!m_bus.label.isEmpty() && m_points.size() >= 2) {
    // Prefer a horizontal segment (label above the line) unless it is much
    // shorter than the longest segment.
    int longestIdx = 0;
    qreal longestLen = -1;
    int horizIdx = -1;
    qreal horizLen = -1;
    for (int i = 0; i + 1 < m_points.size(); ++i) {
      const qreal len = QLineF(m_points[i], m_points[i + 1]).length();
      if (len > longestLen) {
        longestLen = len;
        longestIdx = i;
      }
      const bool horizontal =
          std::abs(m_points[i + 1].y() - m_points[i].y()) < 1e-3;
      if (horizontal && len > horizLen) {
        horizLen = len;
        horizIdx = i;
      }
    }
    const bool useHoriz = horizIdx >= 0 && horizLen >= 0.5 * longestLen;
    const int segIdx = useHoriz ? horizIdx : longestIdx;
    const qreal segLen = useHoriz ? horizLen : longestLen;
    const QPointF p0 = m_points[segIdx];
    const QPointF p1 = m_points[segIdx + 1];
    m_labelSeg0 = p0;
    m_labelSeg1 = p1;
    QPointF mid = (p0 + p1) / 2;
    const bool segVertical = std::abs(p1.x() - p0.x()) < 1e-3;
    // Shift the label along the segment (parallel buses get distinct slots).
    if (segVertical)
      mid.setY(mid.y() + labelShift);
    else
      mid.setX(mid.x() + labelShift);

    const QFontMetricsF fm(busLabelFont());
    // Labels above a horizontal segment wrap to the segment length so that
    // they stay inside the gap between the two blocks (but never narrower
    // than a few words).
    qreal maxW = 220.0;
    if (!segVertical)
      maxW = std::clamp(segLen - 24.0, 72.0, 220.0);
    QRectF tr = fm.boundingRect(QRectF(0, 0, maxW, 1000),
                                Qt::AlignCenter | Qt::TextWordWrap,
                                m_bus.label);
    // Padding plus a little slack for rendering at non-integer zoom factors.
    tr.adjust(-6, -2, 6, 2);
    tr.setWidth(tr.width() * 1.04);
    if (segVertical) {
      // Place to the right of the line.
      m_labelRect = QRectF(mid.x() + 5, mid.y() - tr.height() / 2, tr.width(),
                           tr.height());
      // Keep the label within the segment's extent (when it fits), so that
      // the parallel-label shift never pushes it into an attached block.
      const qreal lo = std::min(p0.y(), p1.y()) + 2;
      const qreal hi = std::max(p0.y(), p1.y()) - 2;
      if (m_labelRect.height() <= hi - lo) {
        if (m_labelRect.top() < lo)
          m_labelRect.moveTop(lo);
        if (m_labelRect.bottom() > hi)
          m_labelRect.moveBottom(hi);
      }
    } else {
      // Place above the line.
      m_labelRect = QRectF(mid.x() - tr.width() / 2, mid.y() - tr.height() - 4,
                           tr.width(), tr.height());
      const qreal lo = std::min(p0.x(), p1.x()) + 2;
      const qreal hi = std::max(p0.x(), p1.x()) - 2;
      if (m_labelRect.width() <= hi - lo) {
        if (m_labelRect.left() < lo)
          m_labelRect.moveLeft(lo);
        if (m_labelRect.right() > hi)
          m_labelRect.moveRight(hi);
      }
    }
  }

  // ----- bounding rect
  const qreal m = pen.widthF() + BusGlowExtra + arrowSize;
  QRectF br = m_points.boundingRect().adjusted(-m, -m, m, m);
  if (!m_labelRect.isNull())
    br = br.united(m_labelRect.adjusted(-2, -2, 2, 2));
  m_boundingRect = br;
}

QVector<QRectF> ArchBusItem::labelCandidates() const {
  QVector<QRectF> out;
  if (m_labelRect.isNull())
    return out;
  out << m_labelRect;
  const QRectF r = m_labelRect;
  const bool vertical = std::abs(m_labelSeg1.x() - m_labelSeg0.x()) < 1e-3;
  if (vertical) {
    const qreal x = m_labelSeg0.x();
    const qreal lo = std::min(m_labelSeg0.y(), m_labelSeg1.y()) + 2;
    const qreal hi = std::max(m_labelSeg0.y(), m_labelSeg1.y()) - 2;
    const QRectF left(x - 5 - r.width(), r.top(), r.width(), r.height());
    out << left;
    for (const qreal dy : {r.height() + 4, -(r.height() + 4),
                           2 * (r.height() + 4), -2 * (r.height() + 4)}) {
      for (const QRectF &base : {r, left}) {
        const QRectF moved = base.translated(0, dy);
        if (moved.top() >= lo && moved.bottom() <= hi)
          out << moved;
      }
    }
  } else {
    const qreal y = m_labelSeg0.y();
    const qreal lo = std::min(m_labelSeg0.x(), m_labelSeg1.x()) + 2;
    const qreal hi = std::max(m_labelSeg0.x(), m_labelSeg1.x()) - 2;
    const QRectF below(r.left(), y + 4, r.width(), r.height());
    out << below;
    for (const qreal dx : {r.width() * 0.6, -r.width() * 0.6, r.width() * 1.2,
                           -r.width() * 1.2}) {
      for (const QRectF &base : {r, below}) {
        const QRectF moved = base.translated(dx, 0);
        if (moved.left() >= lo && moved.right() <= hi)
          out << moved;
      }
    }
  }
  return out;
}

void ArchBusItem::setLabelRect(const QRectF &rect) {
  if (rect == m_labelRect)
    return;
  prepareGeometryChange();
  m_labelRect = rect;
  const QPen pen = archBusKindPen(m_bus.kind, archPaletteIsDark());
  const qreal arrowSize = ArrowBaseSize + pen.widthF();
  const qreal m = pen.widthF() + BusGlowExtra + arrowSize;
  QRectF br = m_points.boundingRect().adjusted(-m, -m, m, m);
  if (!m_labelRect.isNull())
    br = br.united(m_labelRect.adjusted(-2, -2, 2, 2));
  m_boundingRect = br;
}

QRectF ArchBusItem::boundingRect() const { return m_boundingRect; }

QPainterPath ArchBusItem::shape() const {
  QPainterPathStroker stroker;
  const QPen pen = archBusKindPen(m_bus.kind, archPaletteIsDark());
  stroker.setWidth(std::max(10.0, pen.widthF() + 6));
  QPainterPath path = stroker.createStroke(m_path);
  if (!m_labelRect.isNull())
    path.addRect(m_labelRect);
  return path;
}

void ArchBusItem::setHighlighted(bool highlighted) {
  if (m_highlighted == highlighted)
    return;
  m_highlighted = highlighted;
  update();
}

void ArchBusItem::setDimmed(bool dimmed) {
  if (m_dimmed == dimmed)
    return;
  m_dimmed = dimmed;
  setOpacity(dimmed ? DimmedOpacity : 1.0);
  update();
}

void ArchBusItem::paint(QPainter *painter,
                        const QStyleOptionGraphicsItem *option,
                        QWidget *widget) {
  Q_UNUSED(option);
  Q_UNUSED(widget);
  if (m_points.size() < 2)
    return;

  const bool dark = archPaletteIsDark();
  QPen pen = archBusKindPen(m_bus.kind, dark);
  const QColor hl = highlightColor();
  const qreal arrowSize = ArrowBaseSize + pen.widthF();

  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setRenderHint(QPainter::TextAntialiasing, true);

  if (m_highlighted) {
    QColor glow = hl;
    glow.setAlpha(dark ? 110 : 80);
    QPen glowPen(glow);
    glowPen.setWidthF(pen.widthF() + BusGlowExtra);
    glowPen.setCapStyle(Qt::RoundCap);
    glowPen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(glowPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(m_path);

    pen.setColor(hl);
    pen.setWidthF(pen.widthF() + 1.5);
  }

  painter->setPen(pen);
  painter->setBrush(Qt::NoBrush);
  painter->drawPath(m_path);

  // ----- arrow heads
  QPen arrowPen(pen.color(), 1.0);
  arrowPen.setJoinStyle(Qt::RoundJoin);
  painter->setPen(arrowPen);
  painter->setBrush(pen.color());
  painter->drawPolygon(
      arrowHead(m_points.last(), m_points[m_points.size() - 2], arrowSize));
  if (m_bus.bidirectional)
    painter->drawPolygon(arrowHead(m_points[0], m_points[1], arrowSize));

  // ----- label
  if (!m_labelRect.isNull()) {
    QColor bg = baseColor();
    bg.setAlpha(225);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bg);
    painter->drawRoundedRect(m_labelRect, 3, 3);
    painter->setPen(m_highlighted ? hl : textColor());
    painter->setFont(busLabelFont());
    painter->drawText(m_labelRect,
                      Qt::AlignCenter | Qt::TextWordWrap | Qt::TextDontClip,
                      m_bus.label);
  }
}

// ============================================================================
// ArchLegendItem
// ============================================================================

ArchLegendItem::ArchLegendItem(const QVector<ArchBlockKind> &blockKinds,
                               const QVector<ArchBusKind> &busKinds,
                               qreal maxWidth) {
  for (const auto kind : blockKinds) {
    Entry e;
    e.isBus = false;
    e.blockKind = kind;
    e.text = archBlockKindDisplayName(kind);
    m_entries.push_back(e);
  }
  for (const auto kind : busKinds) {
    Entry e;
    e.isBus = true;
    e.busKind = kind;
    e.text = archBusKindDisplayName(kind);
    m_entries.push_back(e);
  }
  layout(maxWidth);
}

void ArchLegendItem::layout(qreal maxWidth) {
  const QFontMetricsF fm(legendFont());
  const qreal pad = 8;
  const qreal rowH = 22;
  const qreal swatchW = 26;
  const qreal gap = 6;
  const qreal spacing = 18;
  qreal x = pad;
  qreal y = pad;
  qreal width = 0;
  for (auto &e : m_entries) {
    const qreal w = swatchW + gap + fm.horizontalAdvance(e.text);
    if (x > pad && x + w + pad > maxWidth) {
      x = pad;
      y += rowH;
    }
    e.rect = QRectF(x, y, w, rowH);
    x += w + spacing;
    width = std::max(width, x - spacing + pad);
  }
  m_boundingRect = QRectF(0, 0, std::max(width, 60.0), y + rowH + pad);
}

void ArchLegendItem::paint(QPainter *painter,
                           const QStyleOptionGraphicsItem *option,
                           QWidget *widget) {
  Q_UNUSED(option);
  Q_UNUSED(widget);
  const bool dark = archPaletteIsDark();
  const QPalette pal = QGuiApplication::palette();

  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setRenderHint(QPainter::TextAntialiasing, true);

  QColor bg = pal.color(QPalette::Base);
  bg.setAlpha(200);
  painter->setPen(QPen(pal.color(QPalette::Mid), 1.0));
  painter->setBrush(bg);
  painter->drawRoundedRect(m_boundingRect, 6, 6);

  painter->setFont(legendFont());
  for (const auto &e : m_entries) {
    const QRectF swatch(e.rect.left(), e.rect.top() + 4, 26,
                        e.rect.height() - 8);
    if (e.isBus) {
      const QPen pen = archBusKindPen(e.busKind, dark);
      painter->setPen(pen);
      painter->setBrush(Qt::NoBrush);
      const qreal cy = swatch.center().y();
      painter->drawLine(QPointF(swatch.left(), cy),
                        QPointF(swatch.right() - 4, cy));
      // Small arrow head at the right end of the sample line.
      const qreal s = 7.0;
      QPolygonF head;
      head << QPointF(swatch.right(), cy)
           << QPointF(swatch.right() - s, cy - s * 0.45)
           << QPointF(swatch.right() - s, cy + s * 0.45);
      painter->setPen(QPen(pen.color(), 1.0));
      painter->setBrush(pen.color());
      painter->drawPolygon(head);
    } else {
      painter->setPen(QPen(pal.color(QPalette::Text), 1.0));
      painter->setBrush(archBlockKindColor(e.blockKind, dark));
      painter->drawRoundedRect(swatch, 3, 3);
    }
    painter->setPen(pal.color(QPalette::Text));
    painter->drawText(QRectF(swatch.right() + 6, e.rect.top(),
                             e.rect.width() - swatch.width() - 6,
                             e.rect.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, e.text);
  }
}

// ============================================================================
// ArchScene
// ============================================================================

ArchScene::ArchScene(QObject *parent) : QGraphicsScene(parent) {
  connect(this, &QGraphicsScene::selectionChanged, this, [this] {
    const QString id = selectedBlock();
    if (id != m_lastSelected) {
      m_lastSelected = id;
      emit blockSelectionChanged(id);
    }
  });
}

void ArchScene::setModel(const ArchModel &model) {
  clear();
  m_blockItems.clear();
  m_busItems.clear();
  m_busOrder.clear();
  m_legend = nullptr;
  m_lastSelected.clear();
  m_model = model;
  if (m_model.blocks.isEmpty()) {
    // Empty state (no topology): no legend, so the view can show its hint.
    setSceneRect(QRectF());
    return;
  }

  QRectF blocksRect;
  for (const auto &block : m_model.blocks) {
    auto *item = new ArchBlockItem(block);
    item->setPos(block.x, block.y);
    item->setZValue(2);
    addItem(item);
    m_blockItems[block.id] = item;
    blocksRect = blocksRect.isNull() ? item->sceneRect()
                                     : blocksRect.united(item->sceneRect());
  }

  for (const auto &bus : m_model.buses) {
    auto *from = m_blockItems.value(bus.from, nullptr);
    auto *to = m_blockItems.value(bus.to, nullptr);
    if (from == nullptr || to == nullptr)
      continue; // validated by the loader; defensive
    auto *item = new ArchBusItem(bus, from, to);
    item->setZValue(1);
    addItem(item);
    m_busItems[bus.id] = item;
    m_busOrder.push_back(item);
  }
  layoutBuses();

  // Legend with the kinds present in the model.
  QVector<ArchBlockKind> blockKinds;
  for (const auto kind : allArchBlockKinds()) {
    for (const auto &block : m_model.blocks) {
      if (block.kind == kind) {
        blockKinds.push_back(kind);
        break;
      }
    }
  }
  QVector<ArchBusKind> busKinds;
  for (const auto kind : allArchBusKinds()) {
    for (const auto &bus : m_model.buses) {
      if (bus.kind == kind) {
        busKinds.push_back(kind);
        break;
      }
    }
  }
  m_legend = new ArchLegendItem(blockKinds, busKinds,
                                std::max(320.0, blocksRect.width()));
  m_legend->setPos(blocksRect.left(), blocksRect.bottom() + 40);
  m_legend->setZValue(0);
  m_legend->setVisible(m_legendVisible);
  addItem(m_legend);

  setSceneRect(diagramRect());
}

void ArchScene::layoutBuses() {
  using Side = ArchBusItem::Side;
  using Route = ArchBusItem::Route;

  struct Attachment {
    ArchBusItem *bus = nullptr;
    bool isFrom = true;
    qreal preferred = 0;
    qreal pos = 0;
  };

  // Group attachments by (block, side).
  QMap<QString, QVector<Attachment>> groups;
  QHash<ArchBusItem *, Route> routes;
  const auto key = [](const QString &blockId, Side side) {
    return blockId + QChar('#') + QString::number(static_cast<int>(side));
  };

  for (auto *bus : std::as_const(m_busOrder)) {
    const QRectF a = bus->fromItem()->sceneRect();
    const QRectF b = bus->toItem()->sceneRect();
    const Route route = ArchBusItem::computeRoute(a, b);
    routes[bus] = route;
    groups[key(bus->bus().from, route.fromSide)].push_back(
        {bus, true, ArchBusItem::preferredAnchor(a, route.fromSide, b), 0});
    groups[key(bus->bus().to, route.toSide)].push_back(
        {bus, false, ArchBusItem::preferredAnchor(b, route.toSide, a), 0});
  }

  // Spread the attachments of each group along the side.
  QHash<ArchBusItem *, QPointF> fromPoints;
  QHash<ArchBusItem *, QPointF> toPoints;
  for (auto it = groups.begin(); it != groups.end(); ++it) {
    auto &atts = it.value();
    if (atts.isEmpty())
      continue;
    const Attachment &first = atts.first();
    ArchBlockItem *block = first.isFrom ? first.bus->fromItem()
                                        : first.bus->toItem();
    const Route &route = routes[first.bus];
    const Side side = first.isFrom ? route.fromSide : route.toSide;
    const QRectF rect = block->sceneRect();
    const bool horizontalSide = (side == Side::Top || side == Side::Bottom);
    const qreal lo = horizontalSide ? rect.left() : rect.top();
    const qreal hi = horizontalSide ? rect.right() : rect.bottom();
    const qreal margin = std::min(10.0, (hi - lo) / 4);
    const qreal minPos = lo + margin;
    const qreal maxPos = hi - margin;

    std::sort(atts.begin(), atts.end(),
              [](const Attachment &x, const Attachment &y) {
                return x.preferred < y.preferred;
              });
    const int n = atts.size();
    const qreal spacing =
        std::min(AnchorSpacing, n > 1 ? (maxPos - minPos) / (n - 1) : 0.0);
    for (int i = 0; i < n; ++i)
      atts[i].pos = std::clamp(atts[i].preferred, minPos, maxPos);
    // Push apart (left to right), then pull back from the far end so that
    // only anchors which actually collide are moved.
    for (int i = 1; i < n; ++i)
      atts[i].pos = std::max(atts[i].pos, atts[i - 1].pos + spacing);
    if (n > 0)
      atts[n - 1].pos = std::min(atts[n - 1].pos, maxPos);
    for (int i = n - 2; i >= 0; --i)
      atts[i].pos = std::min(atts[i].pos, atts[i + 1].pos - spacing);
    for (int i = 0; i < n; ++i)
      atts[i].pos = std::clamp(atts[i].pos, minPos, maxPos);

    for (const auto &att : std::as_const(atts)) {
      const QPointF p = ArchBusItem::anchorPoint(rect, side, att.pos);
      if (att.isFrom)
        fromPoints[att.bus] = p;
      else
        toPoints[att.bus] = p;
    }
  }

  // Stagger the middle segments of Z-shaped routes leaving the same block
  // side: the bus whose middle segment travels farthest sideways runs closest
  // to the block, so the segments nest instead of overlapping or crossing.
  QHash<ArchBusItem *, qreal> midOffsets;
  for (auto it = groups.begin(); it != groups.end(); ++it) {
    auto &atts = it.value();
    const int n = atts.size();
    if (n < 2)
      continue;
    const Route &route = routes[atts.first().bus];
    const Side side = atts.first().isFrom ? route.fromSide : route.toSide;
    const bool horizontalSide = (side == Side::Top || side == Side::Bottom);
    // Offset direction that moves the middle segment towards this block.
    const qreal towardsBlock =
        (side == Side::Bottom || side == Side::Right) ? -1.0 : 1.0;

    QVector<QPair<qreal, int>> travel; // (sideways distance, attachment idx)
    for (int i = 0; i < n; ++i) {
      const Attachment &att = atts[i];
      const QPointF other =
          att.isFrom ? toPoints.value(att.bus) : fromPoints.value(att.bus);
      const qreal otherCoord = horizontalSide ? other.x() : other.y();
      travel.push_back({std::abs(otherCoord - att.pos), i});
    }
    std::sort(travel.begin(), travel.end());
    for (int rank = 0; rank < n; ++rank) {
      const qreal offset = (rank - (n - 1) / 2.0) * ParallelMidOffset;
      midOffsets[atts[travel[rank].second].bus] += towardsBlock * offset;
    }
  }

  // Buses connecting the same pair of blocks: the per-side offsets above
  // cancel out for them (closer to one block means farther from the other),
  // so separate their middle segments explicitly.
  QMap<QString, QVector<ArchBusItem *>> pairs;
  for (auto *bus : std::as_const(m_busOrder)) {
    const QString pairKey = std::min(bus->bus().from, bus->bus().to) +
                            QChar('\n') +
                            std::max(bus->bus().from, bus->bus().to);
    pairs[pairKey].push_back(bus);
  }
  for (const auto &group : std::as_const(pairs)) {
    const int count = group.size();
    if (count < 2)
      continue;
    for (int idx = 0; idx < count; ++idx)
      midOffsets[group[idx]] += (idx - (count - 1) / 2.0) * ParallelMidOffset;
  }

  // Parallel buses (several attachments on one side) would stack their labels
  // at the same height; shift each label along the line by its rank.
  QHash<ArchBusItem *, qreal> labelShifts;
  for (auto it = groups.begin(); it != groups.end(); ++it) {
    const auto &atts = it.value();
    const int n = atts.size();
    if (n < 2)
      continue;
    for (int i = 0; i < n; ++i) { // atts are sorted by position
      const qreal shift = (i - (n - 1) / 2.0) * LabelShiftStep;
      if (std::abs(shift) > std::abs(labelShifts.value(atts[i].bus, 0.0)))
        labelShifts[atts[i].bus] = shift;
    }
  }

  for (auto *bus : std::as_const(m_busOrder)) {
    bus->setGeometry(routes[bus], fromPoints.value(bus), toPoints.value(bus),
                     midOffsets.value(bus, 0.0), labelShifts.value(bus, 0.0));
  }
  resolveLabelOverlaps();
}

void ArchScene::resolveLabelOverlaps() {
  // Greedy pass in model order: a label that overlaps an already placed label
  // or a block moves to the first candidate position that is free (longer
  // English labels made parallel buses collide). Labels without a free
  // candidate keep their default position.
  QVector<QRectF> blocks;
  for (const auto *item : std::as_const(m_blockItems))
    blocks << item->sceneRect().adjusted(-1, -1, 1, 1);
  QVector<QRectF> placed;
  // Thin rectangles around every bus segment, tagged with their bus.
  QVector<QPair<const ArchBusItem *, QRectF>> segments;
  for (const auto *bus : std::as_const(m_busOrder)) {
    const QPolygonF &pts = bus->points();
    for (int i = 0; i + 1 < pts.size(); ++i)
      segments.push_back(
          {bus, QRectF(pts[i], pts[i + 1]).normalized().adjusted(-2, -2, 2, 2)});
  }
  const auto collides = [&](const QRectF &r, const ArchBusItem *self) {
    for (const auto &b : std::as_const(blocks))
      if (b.intersects(r))
        return true;
    for (const auto &p : std::as_const(placed))
      if (p.intersects(r))
        return true;
    for (const auto &seg : std::as_const(segments))
      if (seg.first != self && seg.second.intersects(r))
        return true;
    return false;
  };
  for (auto *bus : std::as_const(m_busOrder)) {
    const QVector<QRectF> candidates = bus->labelCandidates();
    if (candidates.isEmpty())
      continue;
    QRectF chosen = candidates.first();
    for (const auto &c : candidates) {
      if (!collides(c, bus)) {
        chosen = c;
        break;
      }
    }
    bus->setLabelRect(chosen);
    placed << chosen.adjusted(-1, -1, 1, 1);
  }
}

QRectF ArchScene::diagramRect() const {
  // Union of the visible items (the legend may be hidden).
  QRectF r;
  const auto all = items();
  for (const auto *item : all) {
    if (item->isVisible() && item->parentItem() == nullptr)
      r = r.united(item->sceneBoundingRect());
  }
  if (r.isNull())
    return r;
  return r.adjusted(-30, -30, 30, 30);
}

void ArchScene::setLegendVisible(bool visible) {
  m_legendVisible = visible;
  if (m_legend != nullptr)
    m_legend->setVisible(visible);
}

ArchBlockItem *ArchScene::blockItem(const QString &id) const {
  return m_blockItems.value(id, nullptr);
}

ArchBusItem *ArchScene::busItem(const QString &id) const {
  return m_busItems.value(id, nullptr);
}

void ArchScene::highlight(const QStringList &blocks,
                          const QStringList &buses) {
  if (blocks.isEmpty() && buses.isEmpty()) {
    clearHighlight();
    return;
  }
  const QSet<QString> blockSet(blocks.begin(), blocks.end());
  const QSet<QString> busSet(buses.begin(), buses.end());
  for (auto it = m_blockItems.begin(); it != m_blockItems.end(); ++it) {
    const bool on = blockSet.contains(it.key());
    it.value()->setHighlighted(on);
    it.value()->setDimmed(!on);
  }
  for (auto it = m_busItems.begin(); it != m_busItems.end(); ++it) {
    const bool on = busSet.contains(it.key());
    it.value()->setHighlighted(on);
    it.value()->setDimmed(!on);
  }
}

void ArchScene::clearHighlight() {
  for (auto *item : std::as_const(m_blockItems)) {
    item->setHighlighted(false);
    item->setDimmed(false);
  }
  for (auto *item : std::as_const(m_busItems)) {
    item->setHighlighted(false);
    item->setDimmed(false);
  }
}

void ArchScene::selectBlock(const QString &id) {
  auto *item = m_blockItems.value(id, nullptr);
  if (item != nullptr && item->isSelected() && selectedItems().size() == 1)
    return;
  clearSelection();
  if (item != nullptr)
    item->setSelected(true);
}

QString ArchScene::selectedBlock() const {
  const auto items = selectedItems();
  for (auto *item : items) {
    if (item->type() == ArchBlockItem::Type)
      return static_cast<ArchBlockItem *>(item)->id();
  }
  return QString();
}

void ArchScene::notifyBlockClicked(const QString &id) { emit blockClicked(id); }

void ArchScene::mousePressEvent(QGraphicsSceneMouseEvent *event) {
  QGraphicsScene::mousePressEvent(event);
  // A press on empty space (or on a non-selectable item such as a bus) clears
  // the selection; the event stays unaccepted so the view may start panning.
  if (!event->isAccepted())
    clearSelection();
}

} // namespace Ripes
