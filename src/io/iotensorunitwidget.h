#pragma once

#include <QColor>
#include <QWidget>

#include <optional>
#include <vector>

#include "accel/npu/tensorunit.h"
#include "iotensorunit.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QProgressBar)
QT_FORWARD_DECLARE_CLASS(QScrollArea)
QT_FORWARD_DECLARE_CLASS(QSplitter)
QT_FORWARD_DECLARE_CLASS(QTabWidget)
QT_FORWARD_DECLARE_CLASS(QTableWidget)
QT_FORWARD_DECLARE_CLASS(QToolButton)

namespace Ripes {

/**
 * Colours used by the Tensor Unit widgets. Everything is derived from the
 * current QPalette (and whether it is dark), so the views stay readable in
 * light and dark themes; recomputed on ThemeManager::themeChanged.
 */
struct NpuColors {
  QColor base, text, dim, grid;
  QColor highlight, highlightText;
  QColor activeLow, activeHigh; ///< heat-map ends for active MAC cells
  QColor weight;                ///< PE holds a weight but no activation
  QColor empty;                 ///< PE outside the tile / padding
  QColor ok, warn, error, info;
  QColor dmaIn, dmaOut;
  static NpuColors fromPalette(const QPalette &pal);
};

/// Picks black or white text for a background colour.
QColor npuContrastText(const QColor &bg);

/**
 * Draws the descriptor state machine
 * IDLE -> FETCH_DESC -> DMA_IN -> COMPUTE -> DMA_OUT -> COMPLETE with its
 * loops, highlighting the current state by colour and by a text marker.
 */
class NpuStateMachineView : public QWidget {
  Q_OBJECT
public:
  explicit NpuStateMachineView(QWidget *parent = nullptr);
  void setData(const npu::Snapshot &snap, const NpuColors &colors);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  /// One-line description of what the unit does in the given state.
  static QString stateDescription(npu::State s);
  /// Detailed text for the current sub-step (weight load, pass t, ...).
  static QString currentActivity(const npu::Snapshot &snap);

protected:
  void paintEvent(QPaintEvent *event) override;
  bool event(QEvent *event) override;

private:
  QRect boxRect(int i) const;
  npu::Snapshot m_snap;
  NpuColors m_colors;
};

/**
 * Draws the N x N systolic array: every PE shows its activation `a`, its
 * stationary weight `w` and the partial sum `acc`; cells with a MAC in this
 * cycle are coloured by |acc| (heat map). Includes a legend.
 */
class NpuArrayView : public QWidget {
  Q_OBJECT
public:
  explicit NpuArrayView(QWidget *parent = nullptr);
  void setData(const npu::Snapshot &snap, unsigned cellSize,
               const NpuColors &colors);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;
  bool event(QEvent *event) override;

private:
  int cellSize() const;
  QPoint gridOrigin() const;
  int legendTop() const;
  bool isActive(const npu::PEState &pe) const;

  npu::Snapshot m_snap;
  unsigned m_cellSize = 56;
  NpuColors m_colors;
};

/**
 * The complete Tensor Unit peripheral widget: header with title,
 * explanation, state badge and help button; a notice line (empty state,
 * errors, reverse execution); the state machine; the systolic array; and
 * tabs for the descriptor ring, DMA / cycle statistics, interrupts and the
 * register table.
 */
class IOTensorUnitView : public QWidget {
  Q_OBJECT
public:
  explicit IOTensorUnitView(IOTensorUnit *unit);

  /// Updates every view from @p data (GUI thread).
  void refresh(const IOTensorUnit::ViewData &data);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  /// Stacks the array view above the detail tabs when the widget is narrow.
  void resizeEvent(QResizeEvent *e) override;

private:
  void buildHeader();
  void buildRingTab();
  void buildDmaTab();
  void buildIrqTab();
  void buildRegisterTab();
  void applyTheme();

  void updateHeader(const IOTensorUnit::ViewData &data);
  void updateNotice(const IOTensorUnit::ViewData &data);
  void updateRing(const npu::Snapshot &s);
  void updateDma(const npu::Snapshot &s);
  void updateIrq(const npu::Snapshot &s);
  void updateRegisters(const npu::Snapshot &s);

  QString opcodeName(uint32_t opcode) const;
  QString flagsText(uint32_t flags) const;
  QString irqBitsText(uint32_t bits) const;
  QString statusText(uint32_t status) const;
  QString errorText(const npu::Snapshot &s) const;

  IOTensorUnit *m_unit;
  NpuColors m_colors;
  bool m_haveData = false;
  IOTensorUnit::ViewData m_last;

  // Header
  QLabel *m_title = nullptr;
  QLabel *m_subtitle = nullptr;
  QLabel *m_stateBadge = nullptr;
  QLabel *m_statusLine = nullptr;
  QToolButton *m_helpButton = nullptr;
  QLabel *m_notice = nullptr;

  // Views
  NpuStateMachineView *m_stateView = nullptr;
  NpuArrayView *m_arrayView = nullptr;
  QScrollArea *m_arrayScroll = nullptr;
  QTabWidget *m_tabs = nullptr;
  QSplitter *m_lowerSplit = nullptr;

  // Descriptor ring tab
  QLabel *m_ringSummary = nullptr;
  QTableWidget *m_ringTable = nullptr;
  struct RingSlot {
    bool mmio = false; ///< slot address falls into an MMIO region
    std::optional<npu::Descriptor> desc;
  };
  std::vector<RingSlot> m_ringCache;
  uint32_t m_ringCacheBase = 0;

  // DMA tab
  QLabel *m_dmaLabel = nullptr;
  QProgressBar *m_dmaBar = nullptr;
  QTableWidget *m_dmaTable = nullptr;

  // IRQ tab
  QLabel *m_irqLine = nullptr;
  QTableWidget *m_irqTable = nullptr;

  // Register tab
  QTableWidget *m_regTable = nullptr;
};

} // namespace Ripes
