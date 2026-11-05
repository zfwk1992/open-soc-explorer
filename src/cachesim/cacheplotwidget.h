#pragma once

#include <QMetaType>
#include <QWidget>
#include <QtCharts/QChartGlobal>

#include "cachesim.h"
#include "float.h"
#include <queue>

QT_FORWARD_DECLARE_CLASS(QToolBar);
QT_FORWARD_DECLARE_CLASS(QAction);

class QChartView;
class QChart;
class QLineSeries;
class QValueAxis;

template <typename T>
class FixedQueue : public std::deque<T> {
public:
  FixedQueue(const unsigned N = 1) : m_n(N) {}
  void push(const T &value) {
    if (this->size() == m_n) {
      this->pop_front();
    }
    std::deque<T>::push_back(value);
  }

private:
  unsigned m_n;
};

namespace Ripes {

namespace Ui {
class CachePlotWidget;
}

class CachePlotWidget : public QWidget {
  Q_OBJECT

  enum class RangeChangeSource { Slider, Comboboxes, Cycles };

public:
  enum Variable {
    Writes = 0,
    Reads,
    Hits,
    Misses,
    WasHit,
    WasMiss,
    Writebacks,
    Accesses,
    N_TraceVars,
    Unary
  };
  explicit CachePlotWidget(QWidget *parent = nullptr);
  void setCache(const std::shared_ptr<CacheSim> &cache);
  ~CachePlotWidget();

  /// Number of points of the ratio series (for tests).
  int plottedPointCount() const;
  /// First and last clock cycle of the plotted ratio series; {0, 0} if empty.
  std::pair<int, int> plottedCycleRange() const;
  /// True if the plot does not reach back to the first cache access, because
  /// the plot window ("Max. cache plot cycles") or the cache's access trace
  /// ("Max. cache access traces") dropped older cycles. A hint naming the
  /// settings is then shown above the plot.
  bool isPlotTruncated() const;
  /// Text of the hint shown above the plot (empty if hidden).
  QString truncationHintText() const;

public slots:

private slots:
  void variablesChanged();
  void rangeChanged(const CachePlotWidget::RangeChangeSource src);
  void updateHitrate();

private:
  /**
   * @brief gatherData
   * @returns a list of QPoints containing plotable data gathered from the cache
   * simulator, starting from the specified cycle
   */
  std::map<Variable, QList<QPoint>> gatherData(unsigned fromCycle = 0) const;
  void setupPlotActions();
  void showSizeBreakdown();
  void copyPlotDataToClipboard() const;
  void savePlot();
  void updateRatioPlot();
  void updatePlotAxes();
  void updateAllowedRange(const RangeChangeSource src);
  void updateTruncationHint();
  /// Removes points before the plot window (see
  /// RIPES_SETTING_CACHE_MAXCYCLES) from both series.
  void dropPointsBeforeWindow();
  /// Width of the plot window in cycles: RIPES_SETTING_CACHE_MAXCYCLES,
  /// clamped to at least 1 (cached; kept current through the setting's
  /// observer).
  int plotWindowCycles() const { return m_plotWindowCycles; }
  int m_plotWindowCycles = 1;

  /**
   * @brief resampleToScreen
   * Resamples @param series to only contain as many points as would be visible
   * on the associated plot view
   */
  void resampleToScreen(QLineSeries *series);

  void resetRatioPlot();
  void applyPlotTheme();
  void setupToolTips();
  /// Translated display name of a plot variable.
  static QString variableName(Variable v);

private:
  QChart *m_plot = nullptr;
  QLineSeries *m_series = nullptr;
  QValueAxis *m_axisX = nullptr;
  QValueAxis *m_axisY = nullptr;
  double m_maxY = -DBL_MAX;
  double m_minY = DBL_MAX;
  int64_t m_lastCyclePlotted = 0;
  double m_xStep = 1.0;
  static constexpr int s_resamplingRatio = 2;

  QLineSeries *m_mavgSeries = nullptr;
  // N last computations of the change in ratio value
  FixedQueue<double> m_mavgData;

  Ui::CachePlotWidget *m_ui;
  std::shared_ptr<CacheSim> m_cache;

  CachePlotWidget::Variable m_numerator = Variable::Hits;
  CachePlotWidget::Variable m_denominator = Variable::Accesses;

  QAction *m_copyDataAction = nullptr;
  QAction *m_savePlotAction = nullptr;
  QAction *m_ratioMarkerAction = nullptr;
  QAction *m_mavgMarkerAction = nullptr;

  std::vector<QWidget *> m_rangeWidgets;
};

const static std::map<CachePlotWidget::Variable, QString>
    s_cacheVariableStrings{
        {CachePlotWidget::Variable::Writes, QT_TRANSLATE_NOOP("Ripes::CachePlotWidget", "Writes")},
        {CachePlotWidget::Variable::Reads, QT_TRANSLATE_NOOP("Ripes::CachePlotWidget", "Reads")},
        {CachePlotWidget::Variable::Hits, QT_TRANSLATE_NOOP("Ripes::CachePlotWidget", "Hits")},
        {CachePlotWidget::Variable::Misses, QT_TRANSLATE_NOOP("Ripes::CachePlotWidget", "Misses")},
        {CachePlotWidget::Variable::Writebacks, QT_TRANSLATE_NOOP("Ripes::CachePlotWidget", "Writebacks")},
        {CachePlotWidget::Variable::Accesses, QT_TRANSLATE_NOOP("Ripes::CachePlotWidget", "Access count")},
        {CachePlotWidget::Variable::Unary, "1"},
        {CachePlotWidget::Variable::WasHit, QT_TRANSLATE_NOOP("Ripes::CachePlotWidget", "Was hit")},
        {CachePlotWidget::Variable::WasMiss, QT_TRANSLATE_NOOP("Ripes::CachePlotWidget", "Was miss")}};

} // namespace Ripes

Q_DECLARE_METATYPE(Ripes::CachePlotWidget::Variable);
