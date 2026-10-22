// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QWidget>

#include "explorer/tracetypes.h"

class QGraphicsScene;
class QGraphicsView;
class QTextBrowser;
class QLabel;

namespace Ripes {

/**
 * @brief The SystemView class
 * SoC topology of a trace: draws the hardware components that the producing
 * engine reported as simulated (Trace::TraceHeader::system) as chips on a
 * die, the connections the engine reported as arrows, and - clearly marked,
 * outside the die - typical SoC components that the engine does not
 * simulate. Components whose path lies below another component's path (e.g.
 * a cache's prefetcher) are drawn inside their parent. Clicking a component
 * shows its parameters and end-of-simulation statistics. Ctrl + mouse wheel
 * zooms.
 */
class SystemView : public QWidget {
  Q_OBJECT
public:
  explicit SystemView(QWidget *parent = nullptr);

  void setTrace(const Trace::TraceHeader &header);
  /// Path of the selected component ("" if none).
  QString selectedComponent() const { return m_selected; }
  void selectComponent(const QString &path);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void rebuild();
  void showDetails();
  void fitTopology();

  Trace::TraceHeader m_header;
  QString m_selected;
  QGraphicsScene *m_scene = nullptr;
  QGraphicsView *m_view = nullptr;
  QTextBrowser *m_details = nullptr;
  QLabel *m_note = nullptr;
  /// Fit the topology to the view on resize until the user zooms.
  bool m_autoFit = true;
};

} // namespace Ripes
