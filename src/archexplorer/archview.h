#pragma once

#include <QGraphicsView>
#include <QString>

QT_FORWARD_DECLARE_CLASS(QEvent)
QT_FORWARD_DECLARE_CLASS(QImage)
QT_FORWARD_DECLARE_CLASS(QWheelEvent)

namespace Ripes {

/**
 * @brief The ArchView class
 * QGraphicsView used by the Architecture Explorer: mouse wheel zooms (anchored
 * under the cursor), left-drag on empty space pans, and the background follows
 * the application palette. While the scene is empty, a placeholder text is
 * drawn in the middle of the viewport.
 */
class ArchView : public QGraphicsView {
  Q_OBJECT
public:
  explicit ArchView(QWidget *parent = nullptr);

  /// Fits the whole diagram into the viewport (never zooming in beyond 1.6x).
  void fitDiagram();
  qreal currentScale() const;

  /// Text shown in the middle of the viewport while the scene has no items.
  void setPlaceholderText(const QString &text);

  /// Renders the whole diagram (ArchScene::diagramRect) into an image at
  /// @p scale device pixels per scene unit, on the palette's base color.
  QImage renderDiagram(qreal scale) const;

public slots:
  void zoomIn();
  void zoomOut();
  /// Back to 100 % (1 scene unit = 1 pixel), centered on the diagram.
  void resetZoom();

signals:
  /// Emitted whenever the zoom factor changes (1.0 = 100 %).
  void zoomChanged(qreal scale);

protected:
  void wheelEvent(QWheelEvent *event) override;
  void changeEvent(QEvent *event) override;
  void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
  void scaleBy(qreal factor);

  QString m_placeholder;
};

} // namespace Ripes
