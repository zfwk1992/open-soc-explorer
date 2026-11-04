#pragma once

#include <QColor>
#include <QWidget>

#include <memory>
#include <vector>

#include "iogpu.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QStackedWidget)
QT_FORWARD_DECLARE_CLASS(QTableWidget)
QT_FORWARD_DECLARE_CLASS(QTimer)
QT_FORWARD_DECLARE_CLASS(QToolButton)
QT_FORWARD_DECLARE_CLASS(QScrollArea)
QT_FORWARD_DECLARE_CLASS(QSplitter)

namespace Ripes {

/// Palette-derived colours of the GPU visualisation. Recomputed whenever the
/// application theme changes, so light and dark themes both stay readable.
struct GpuColors {
  QColor text, mutedText, frame;
  QColor active, masked, maskedFill, parked, exited, executed;
  QColor divergedRow, issueRow, pcRow;
  QColor idle, busy, done, error, badgeText;
  static GpuColors fromPalette(const QPalette &pal);
};

/**
 * @brief The GpuWarpView class
 * Custom-painted SM / warp / lane grid. One box per SM, one row per warp
 * slot: identity, next pc and instruction, one square per lane (shape + colour
 * encode the lane state) and a textual status ("stall 3", "BAR", "diverged").
 * Hovering a lane shows its thread id, state and registers.
 */
class GpuWarpView : public QWidget {
  Q_OBJECT
public:
  explicit GpuWarpView(QWidget *parent = nullptr);
  void setView(std::shared_ptr<const IOGpu::View> view, const GpuColors &c);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }

  /// Paints one lane square in the given state (shared with the legend).
  enum class LaneState { NoThread, Active, Masked, Parked, Exited };
  static void paintLane(QPainter &p, const QRect &r, LaneState s,
                        bool executed, const GpuColors &c);

protected:
  void paintEvent(QPaintEvent *) override;
  bool event(QEvent *e) override;

private:
  struct Hit {
    QRect rect;
    unsigned sm, warp;
    int lane; ///< -1: the row label
  };
  QString laneToolTip(unsigned sm, unsigned warp, int lane) const;
  QString instrAt(uint32_t pc) const;
  void relayout();

  std::shared_ptr<const IOGpu::View> m_view;
  GpuColors m_colors;
  std::vector<Hit> m_hits;
  QSize m_contentSize{400, 200};
  int m_labelW = 0, m_instrW = 0, m_statusW = 0;
};

/// Colour legend for the lane squares and row highlights.
class GpuLegend : public QWidget {
  Q_OBJECT
public:
  explicit GpuLegend(QWidget *parent = nullptr);
  void setColors(const GpuColors &c);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int w) const override;

protected:
  void paintEvent(QPaintEvent *) override;

private:
  struct Item {
    int kind; ///< 0..4 lane states, 5 executed outline, 6 diverged row
    QString text;
  };
  std::vector<Item> items() const;
  int layoutItems(int width, std::vector<QRect> *rects) const;
  GpuColors m_colors;
};

/**
 * @brief The IOGpuWidget class
 * The complete GPU peripheral view: header with help, status strip, warp
 * visualisation + legend, kernel disassembly with current PCs, statistics and
 * the register table. Polls IOGpu::version() at the global UI update rate
 * (RIPES_SETTING_UIUPDATEPS) and refreshes only when something changed.
 */
class IOGpuWidget : public QWidget {
  Q_OBJECT
public:
  IOGpuWidget(IOGpu *gpu, QWidget *parent = nullptr);

  /// Pulls a fresh view from the peripheral (also used by tests).
  void refresh(bool force = false);

protected:
  void showEvent(QShowEvent *e) override;
  /// Stacks the kernel listing above the statistics / registers tabs when
  /// the widget is narrow, so that it fits a small I/O tab.
  void resizeEvent(QResizeEvent *e) override;

private:
  void buildUi();
  void applyTheme();
  void updateTimerInterval();
  void updateStatus(const IOGpu::View &v);
  void updateKernel(const IOGpu::View &v);
  void updateStats(const IOGpu::View &v);
  void updateRegisters(const IOGpu::View &v);
  /// "?" button: Ripes::showHelp("09-gpu").
  void openGpuHelp();

  IOGpu *m_gpu = nullptr;
  GpuColors m_colors;
  QTimer *m_timer = nullptr;
  uint64_t m_lastVersion = ~uint64_t(0);

  QLabel *m_badge = nullptr;
  QLabel *m_cycles = nullptr;
  QLabel *m_launchInfo = nullptr;
  QLabel *m_configInfo = nullptr;
  QLabel *m_errorLabel = nullptr;
  QLabel *m_noticeLabel = nullptr;
  QToolButton *m_helpButton = nullptr;
  QStackedWidget *m_warpStack = nullptr;
  QScrollArea *m_warpScroll = nullptr;
  GpuWarpView *m_warpView = nullptr;
  GpuLegend *m_legend = nullptr;
  QLabel *m_kernelHeader = nullptr;
  QTableWidget *m_kernelTable = nullptr;
  QTableWidget *m_statsTable = nullptr;
  QTableWidget *m_regTable = nullptr;
  QSplitter *m_detailSplit = nullptr;
  std::shared_ptr<const std::vector<uint32_t>> m_shownListing;
  uint32_t m_shownListingBase = 0;
};

} // namespace Ripes
