#include "archview.h"
#include "archscene.h"

#include <QEvent>
#include <QImage>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Ripes {

namespace {
constexpr qreal MinScale = 0.08;
constexpr qreal MaxScale = 6.0;
constexpr qreal FitMaxScale = 1.6;
constexpr qreal ZoomStep = 1.15;
} // namespace

ArchView::ArchView(QWidget *parent) : QGraphicsView(parent) {
  setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                 QPainter::SmoothPixmapTransform);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setDragMode(QGraphicsView::ScrollHandDrag);
  setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
  setInteractive(true);
  // Follow the application theme rather than a hardcoded white background.
  setBackgroundBrush(palette().base());
}

qreal ArchView::currentScale() const { return transform().m11(); }

void ArchView::setPlaceholderText(const QString &text) {
  m_placeholder = text;
  viewport()->update();
}

void ArchView::scaleBy(qreal factor) {
  const qreal target = std::clamp(currentScale() * factor, MinScale, MaxScale);
  const qreal effective = target / currentScale();
  if (std::abs(effective - 1.0) < 1e-6)
    return;
  scale(effective, effective);
  emit zoomChanged(currentScale());
}

void ArchView::zoomIn() {
  const auto anchor = transformationAnchor();
  setTransformationAnchor(QGraphicsView::AnchorViewCenter);
  scaleBy(ZoomStep);
  setTransformationAnchor(anchor);
}

void ArchView::zoomOut() {
  const auto anchor = transformationAnchor();
  setTransformationAnchor(QGraphicsView::AnchorViewCenter);
  scaleBy(1.0 / ZoomStep);
  setTransformationAnchor(anchor);
}

void ArchView::resetZoom() {
  resetTransform();
  if (scene() != nullptr) {
    QRectF rect;
    if (auto *archScene = qobject_cast<ArchScene *>(scene()))
      rect = archScene->diagramRect();
    else
      rect = scene()->itemsBoundingRect();
    if (!rect.isEmpty())
      centerOn(rect.center());
  }
  emit zoomChanged(currentScale());
}

void ArchView::fitDiagram() {
  if (scene() == nullptr)
    return;
  QRectF rect;
  if (auto *archScene = qobject_cast<ArchScene *>(scene()))
    rect = archScene->diagramRect();
  else
    rect = scene()->itemsBoundingRect();
  if (rect.isEmpty()) {
    resetTransform();
    emit zoomChanged(currentScale());
    return;
  }
  scene()->setSceneRect(rect);
  resetTransform();
  fitInView(rect, Qt::KeepAspectRatio);
  if (currentScale() > FitMaxScale) {
    resetTransform();
    scale(FitMaxScale, FitMaxScale);
    centerOn(rect.center());
  }
  emit zoomChanged(currentScale());
}

QImage ArchView::renderDiagram(qreal scale) const {
  if (scene() == nullptr)
    return QImage();
  QRectF rect;
  if (auto *archScene = qobject_cast<ArchScene *>(scene()))
    rect = archScene->diagramRect();
  else
    rect = scene()->itemsBoundingRect();
  if (rect.isEmpty())
    return QImage();
  const QSize size(int(std::ceil(rect.width() * scale)),
                   int(std::ceil(rect.height() * scale)));
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(palette().color(QPalette::Base));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  scene()->render(&painter, QRectF(QPointF(0, 0), QSizeF(size)), rect);
  painter.end();
  return image;
}

void ArchView::wheelEvent(QWheelEvent *event) {
  const int dy = event->angleDelta().y();
  if (dy == 0) {
    QGraphicsView::wheelEvent(event);
    return;
  }
  scaleBy(dy > 0 ? ZoomStep : 1.0 / ZoomStep);
  event->accept();
}

void ArchView::changeEvent(QEvent *event) {
  QGraphicsView::changeEvent(event);
  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::ApplicationPaletteChange) {
    // Keep the background in sync with the active light/dark palette and
    // repaint the items, which read their colors from the palette.
    setBackgroundBrush(palette().base());
    if (scene() != nullptr)
      scene()->update();
  }
}

void ArchView::drawForeground(QPainter *painter, const QRectF &rect) {
  QGraphicsView::drawForeground(painter, rect);
  if (m_placeholder.isEmpty() || scene() == nullptr ||
      !scene()->items().isEmpty())
    return;
  // Empty state: draw the hint in viewport coordinates.
  painter->save();
  painter->resetTransform();
  QColor color = palette().color(QPalette::Text);
  color.setAlphaF(0.6);
  painter->setPen(color);
  QFont font = this->font();
  font.setPointSizeF(font.pointSizeF() * 1.15);
  painter->setFont(font);
  const QRect area = viewport()->rect().adjusted(24, 24, -24, -24);
  painter->drawText(area, Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
  painter->restore();
}

} // namespace Ripes
