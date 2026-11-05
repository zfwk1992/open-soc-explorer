#include "cacheplotwidget.h"
#include "ui_cacheplotwidget.h"

#include <QCheckBox>
#include <QClipboard>
#include <QEvent>
#include <QFileDialog>
#include <QGraphicsLayout>
#include <QGuiApplication>
#include <QPushButton>
#include <QToolBar>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QChartView>
#include <QtCharts/QLegend>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>

#include "colors.h"
#include "enumcombobox.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "tabheader.h"

#include "limits.h"

namespace Ripes {

static QPointF stepPoint(const QPointF &p1, const QPointF &p2) {
  return QPointF(p2.x(), p1.y());
}

CachePlotWidget::CachePlotWidget(QWidget *parent)
    : QWidget(parent), m_ui(new Ui::CachePlotWidget) {
  m_ui->setupUi(this);
  m_ui->plotView->setScene(new QGraphicsScene(this));
  setWindowTitle(tr("Cache Access Statistics"));

  // Cache the plot-window setting: plotWindowCycles() is on the per-refresh
  // path. The window is always bounded: a value below 1 (e.g. a stored
  // setting from before the spin box minimum was raised to 1) plots the last
  // cycle only, never "everything".
  const auto windowChanged = [this](const QVariant &v) {
    m_plotWindowCycles = std::max(1, v.toInt());
  };
  windowChanged(RipesSettings::value(RIPES_SETTING_CACHE_MAXCYCLES));
  connect(RipesSettings::getObserver(RIPES_SETTING_CACHE_MAXCYCLES),
          &SettingObserver::modified, this, windowChanged);

  setupEnumCombobox(m_ui->num, s_cacheVariableStrings);
  setupEnumCombobox(m_ui->den, s_cacheVariableStrings);
  // Translate the visible variable names; item data (enum) is unchanged.
  for (auto *cb : {m_ui->num, m_ui->den}) {
    for (int i = 0; i < cb->count(); ++i) {
      cb->setItemText(i, variableName(qvariant_cast<Variable>(cb->itemData(i))));
    }
  }
  setupToolTips();

  // Set default ratio plot to be hit rate
  setEnumIndex(m_ui->num, Variable::Hits);
  setEnumIndex(m_ui->den, Variable::Accesses);

  m_rangeWidgets = {m_ui->rangeMax, m_ui->rangeMin, m_ui->rangeSlider};

  connect(m_ui->num, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &CachePlotWidget::variablesChanged);
  connect(m_ui->den, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &CachePlotWidget::variablesChanged);
  connect(m_ui->showMAvg, &QCheckBox::toggled, this,
          &CachePlotWidget::variablesChanged);
  connect(m_ui->showRatio, &QCheckBox::toggled, this,
          &CachePlotWidget::variablesChanged);

  connect(m_ui->windowCycles, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &CachePlotWidget::variablesChanged);

  m_ui->rangeMin->setValue(0);
  m_ui->rangeMax->setValue(ProcessorHandler::getProcessor()->getCycleCount());

  connect(m_ui->rangeMin, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this] { rangeChanged(RangeChangeSource::Comboboxes); });
  connect(m_ui->rangeMax, QOverload<int>::of(&QSpinBox::valueChanged), this,
          [this] { rangeChanged(RangeChangeSource::Comboboxes); });
  connect(m_ui->rangeSlider, &ctkRangeSlider::valuesChanged, this,
          [this] { rangeChanged(RangeChangeSource::Slider); });

  const QIcon sizeBreakdownIcon = QIcon(":/icons/info.svg");
  m_ui->sizeBreakdownButton->setIcon(sizeBreakdownIcon);
  connect(m_ui->sizeBreakdownButton, &QPushButton::clicked, this,
          &CachePlotWidget::showSizeBreakdown);
  connect(m_ui->showMAvg, &QCheckBox::toggled, m_ui->windowCycles,
          &QWidget::setEnabled);

  // Size the statistics fields for 10-digit counts in any language.
  const int digitsWidth =
      m_ui->hits->fontMetrics().horizontalAdvance(QString(10, QChar('0')));
  for (auto *field : {m_ui->hitrate, m_ui->hits, m_ui->misses,
                      m_ui->writebacks, m_ui->size}) {
    field->setMinimumWidth(digitsWidth + 16);
  }

  m_ui->truncationHint->setVisible(false);
  m_ui->truncationHint->setTextFormat(Qt::PlainText);
}

void CachePlotWidget::showSizeBreakdown() {
  QString sizeText;

  const auto cacheSize = m_cache->getCacheSize();

  for (const auto &component : cacheSize.components) {
    sizeText += component + "\n";
  }

  sizeText += "\n" + tr("Total: %1 bits").arg(cacheSize.bits);

  QMessageBox::information(this, tr("Cache Size Breakdown"), sizeText);
}

void CachePlotWidget::setCache(const std::shared_ptr<CacheSim> &cache) {
  m_cache = cache;

  connect(m_cache.get(), &CacheSim::hitrateChanged, this,
          &CachePlotWidget::updateHitrate);
  connect(m_cache.get(), &CacheSim::configurationChanged, [this] {
    m_ui->size->setText(QString::number(m_cache->getCacheSize().bits));
  });
  m_ui->size->setText(QString::number(m_cache->getCacheSize().bits));

  const auto plotUpdateFunc = [this]() {
    updateRatioPlot();
    updateAllowedRange(RangeChangeSource::Cycles);
    updatePlotAxes();
  };
  connect(ProcessorHandler::get(), &ProcessorHandler::processorClockedNonRun,
          this, plotUpdateFunc);
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          plotUpdateFunc);
  connect(m_cache.get(), &CacheSim::cacheInvalidated, this,
          [this] { resetRatioPlot(); });

  m_plot = new QChart();
  m_series = new QLineSeries(m_plot);
  auto defaultPen = m_series->pen(); // Inherit default pen state
  defaultPen.setColor(Colors::FoundersRock);
  m_series->setPen(defaultPen);
  m_plot->addSeries(m_series);
  m_series->setName(tr("Ratio"));
  m_mavgSeries = new QLineSeries(m_plot);
  m_mavgSeries->setName(tr("Moving average"));
  defaultPen.setColor(Colors::Medalist);
  m_mavgSeries->setPen(defaultPen);
  m_plot->addSeries(m_mavgSeries);
  // One pair of axes for the lifetime of the chart. (Recreating them with
  // createDefaultAxes() on every update left stale axes drawn in the corner
  // of the plot.)
  m_axisX = new QValueAxis(m_plot);
  m_axisY = new QValueAxis(m_plot);
  m_plot->addAxis(m_axisX, Qt::AlignBottom);
  m_plot->addAxis(m_axisY, Qt::AlignLeft);
  for (auto *series : {m_series, m_mavgSeries}) {
    series->attachAxis(m_axisX);
    series->attachAxis(m_axisY);
  }
  m_plot->legend()->show();
  m_plot->legend()->setAlignment(Qt::AlignBottom);
  // Small margins: the plot area gets as much of the view as possible.
  m_plot->setMargins(QMargins(2, 2, 2, 2));
  m_plot->layout()->setContentsMargins(0, 0, 0, 0);
  m_plot->setBackgroundRoundness(0);
  m_ui->plotView->setPlot(m_plot);
  applyPlotTheme();
  connect(ThemeManager::get(), &ThemeManager::themeChanged, this,
          [this] { applyPlotTheme(); });
  setupPlotActions();
  updatePlotAxes();

  variablesChanged();
  rangeChanged(RangeChangeSource::Comboboxes);
  updateHitrate();
  updateTruncationHint();
}

void CachePlotWidget::updateAllowedRange(const RangeChangeSource src) {
  // Block all range signals while changing ranges
  for (const auto &w : m_rangeWidgets) {
    w->blockSignals(true);
  }

  const bool atMax =
      m_ui->rangeSlider->maximumPosition() == m_ui->rangeSlider->maximum();

  // The range starts at the first plotted cycle, which is later than 0 when
  // older cycles were dropped (plot window / trace limit).
  const int firstCycle = std::min<int>(plottedCycleRange().first,
                                       static_cast<int>(m_lastCyclePlotted));
  const bool atMin =
      m_ui->rangeSlider->minimumPosition() <= m_ui->rangeSlider->minimum();
  m_ui->rangeMin->setMinimum(firstCycle);
  m_ui->rangeMax->setMaximum(m_lastCyclePlotted);
  m_ui->rangeSlider->setMinimum(firstCycle);
  m_ui->rangeSlider->setMaximum(m_lastCyclePlotted);
  if ((src == RangeChangeSource::Cycles && atMin) ||
      m_ui->rangeSlider->minimumPosition() < firstCycle) {
    m_ui->rangeSlider->setMinimumPosition(firstCycle);
    m_ui->rangeMin->setValue(firstCycle);
  }

  if (src == RangeChangeSource::Cycles) {
    if (atMax) {
      // Keep the range slider at the new maximum position
      m_ui->rangeSlider->setMaximumPosition(m_lastCyclePlotted);
      m_ui->rangeMax->setValue(m_lastCyclePlotted);
    }

    m_ui->rangeMin->setValue(m_lastCyclePlotted < m_ui->rangeMin->value()
                                 ? m_lastCyclePlotted
                                 : m_ui->rangeMin->value());
    m_ui->rangeSlider->setMinimumPosition(
        m_lastCyclePlotted < m_ui->rangeSlider->minimumPosition()
            ? m_lastCyclePlotted
            : m_ui->rangeSlider->minimumPosition());

  } else if (src == RangeChangeSource::Comboboxes) {
    m_ui->rangeSlider->setMinimumPosition(m_ui->rangeMin->value());
    m_ui->rangeSlider->setMaximumPosition(m_ui->rangeMax->value());
  } else if (src == RangeChangeSource::Slider) {
    m_ui->rangeMax->setMinimum(m_ui->rangeSlider->minimumValue());
    m_ui->rangeMin->setMaximum(m_ui->rangeSlider->maximumValue());
    m_ui->rangeMin->setValue(m_ui->rangeSlider->minimumPosition());
    m_ui->rangeMax->setValue(m_ui->rangeSlider->maximumPosition());
  }

  for (const auto &w : m_rangeWidgets) {
    w->blockSignals(false);
  }
}

CachePlotWidget::~CachePlotWidget() {
  delete m_ui;
  delete m_plot;
}

void CachePlotWidget::setupPlotActions() {
  auto showMarkerFunctor = [this](QLineSeries *series, const QString &name) {
    return [=, this](bool visible) {
      if (visible) {
        m_ui->plotView->showSeriesMarker(series, name);
      } else {
        m_ui->plotView->hideSeriesMarker(series);
      }
    };
  };

  // Setup ratio marker.
  m_ratioMarkerAction = new QAction(tr("Enable ratio crosshair"), this);
  m_ratioMarkerAction->setIcon(QIcon(":/icons/crosshair_blue.svg"));
  m_ratioMarkerAction->setCheckable(true);
  m_ui->ratioCursor->setDefaultAction(m_ratioMarkerAction);
  connect(m_ratioMarkerAction, &QAction::toggled,
          showMarkerFunctor(m_series, tr("Ratio")));
  m_ratioMarkerAction->setChecked(true);
  connect(m_ui->showRatio, &QCheckBox::toggled, m_ratioMarkerAction,
          [this](bool enabled) {
            m_ratioMarkerAction->setChecked(enabled);
            m_ratioMarkerAction->setEnabled(enabled);
          });

  // Setup moving average marker.
  m_mavgMarkerAction = new QAction(tr("Enable moving average crosshair"), this);
  m_mavgMarkerAction->setIcon(QIcon(":/icons/crosshair_gold.svg"));
  m_mavgMarkerAction->setCheckable(true);
  m_ui->mavgCursor->setDefaultAction(m_mavgMarkerAction);
  connect(m_mavgMarkerAction, &QAction::toggled,
          showMarkerFunctor(m_mavgSeries, tr("Moving average")));
  m_mavgMarkerAction->setChecked(true);
  connect(m_ui->showMAvg, &QCheckBox::toggled, m_mavgMarkerAction,
          [this](bool enabled) {
            m_mavgMarkerAction->setChecked(enabled);
            m_mavgMarkerAction->setEnabled(enabled);
          });

  const QIcon copyIcon = QIcon(":/icons/documents.svg");
  m_copyDataAction = new QAction(tr("Copy data to clipboard (tab separated, one row per cycle)"), this);
  m_copyDataAction->setIcon(copyIcon);
  m_ui->copyData->setDefaultAction(m_copyDataAction);
  connect(m_copyDataAction, &QAction::triggered, this,
          &CachePlotWidget::copyPlotDataToClipboard);

  const QIcon saveIcon = QIcon(":/icons/saveas.svg");
  m_savePlotAction = new QAction(tr("Save plot to a PNG file"), this);
  m_savePlotAction->setIcon(saveIcon);
  m_ui->savePlot->setDefaultAction(m_savePlotAction);
  connect(m_savePlotAction, &QAction::triggered, this,
          &CachePlotWidget::savePlot);
}

void CachePlotWidget::savePlot() {
  const QString filename =
      QFileDialog::getSaveFileName(this, tr("Save file"), "", tr("Images (*.png)"));
  if (!filename.isEmpty()) {
    const QPixmap p = m_ui->plotView->getPlotPixmap();
    p.save(filename, "PNG");
  }
}

void CachePlotWidget::copyPlotDataToClipboard() const {
  std::vector<Variable> allVariables;
  for (int i = 0; i < N_TraceVars; ++i) {
    allVariables.push_back(static_cast<Variable>(i));
  }
  const auto &allData = gatherData();

  std::map<unsigned /*cycle*/, QStringList> dataStrings;
  QStringList header;

  // The export is machine-readable data: the column names are the
  // untranslated variable identifiers (s_cacheVariableStrings), so that the
  // clipboard contents do not depend on the UI language. Only on-screen
  // labels use variableName().
  header << QStringLiteral("cycle");
  for (const auto &dataPoint : allData.begin()->second) {
    dataStrings[dataPoint.x()] << QString::number(dataPoint.x());
  }

  // Write variables
  for (const auto &variableData : allData) {
    header << s_cacheVariableStrings.at(variableData.first);
    for (const auto &dataPoint : variableData.second) {
      dataStrings[dataPoint.x()] << QString::number(dataPoint.y());
    }
  }

  // Assemble string
  QString outString;
  outString += header.join('\t') + '\n';

  for (const auto &dataString : dataStrings) {
    outString += dataString.second.join('\t') + '\n';
  }

  QApplication::clipboard()->setText(outString);
}

void CachePlotWidget::rangeChanged(const RangeChangeSource src) {
  updateAllowedRange(src);
  if (m_plot) {
    m_plot->axes(Qt::Horizontal)
        .constFirst()
        ->setRange(m_ui->rangeMin->value(), m_ui->rangeMax->value());
  }
}

void CachePlotWidget::variablesChanged() {
  m_numerator = getEnumValue<Variable>(m_ui->num);
  m_denominator = getEnumValue<Variable>(m_ui->den);

  resetRatioPlot();
  updateRatioPlot();
  updateAllowedRange(RangeChangeSource::Cycles);
  updatePlotAxes();
}

std::map<CachePlotWidget::Variable, QList<QPoint>>
CachePlotWidget::gatherData(unsigned fromCycle) const {
  std::map<Variable, QList<QPoint>> cacheData;
  const auto &trace = m_cache->getAccessTrace();

  for (int i = 0; i < N_TraceVars; ++i) {
    cacheData[static_cast<Variable>(i)].reserve(trace.size());
  }

  // Gather the data after fromCycle that lies within the plot window: the
  // last "Max. cache plot cycles" cycles of the trace. Trace entries are
  // cumulative, so a window that does not start at cycle 0 still shows the
  // true (whole-run) ratio at each cycle.
  const int maxCycles = plotWindowCycles();
  unsigned start = fromCycle;
  if (!trace.empty()) {
    const unsigned last = trace.rbegin()->first;
    if (last > static_cast<unsigned>(maxCycles) &&
        last - static_cast<unsigned>(maxCycles) > start)
      start = last - static_cast<unsigned>(maxCycles);
  }

  for (auto it = trace.upper_bound(start); it != trace.end(); it++) {
    const auto &entry = it->second;
    cacheData[Variable::Writes].append(QPoint(it->first, entry.writes));
    cacheData[Variable::Reads].append(QPoint(it->first, entry.reads));
    cacheData[Variable::Hits].append(QPoint(it->first, entry.hits));
    cacheData[Variable::Misses].append(QPoint(it->first, entry.misses));
    cacheData[Variable::Writebacks].append(QPoint(it->first, entry.writebacks));
    cacheData[Variable::Accesses].append(
        QPoint(it->first, entry.hits + entry.misses));
    cacheData[Variable::WasHit].append(
        QPoint(it->first, entry.lastTransaction.isHit));
    cacheData[Variable::WasMiss].append(
        QPoint(it->first, !entry.lastTransaction.isHit));
  }

  return cacheData;
}

void resample(QLineSeries *series, unsigned target, double &step) {
  QVector<QPointF> newPoints;
  const auto &oldPoints = series->points();
  // Minimum distance (in cycles) between new points after resampling. Based
  // on the plotted span, which need not start at cycle 0 (plot window).
  const double span = oldPoints.last().x() - oldPoints.first().x();
  step = std::max(1.0, (span / static_cast<double>(target)) *
                           2); // *2 to account for steps
  newPoints.reserve(target);
  const int indexStep = std::max<int>(
      1, static_cast<int>(oldPoints.size() / std::max(1u, target / 2)));
  for (int i = 0; i < oldPoints.size(); i += indexStep) {
    newPoints << oldPoints.at(i);
  }
  if (newPoints.last() != oldPoints.last())
    newPoints << oldPoints.last();

  series->replace(newPoints);
}

void CachePlotWidget::updatePlotAxes() {
  // Add space to label to add space between labels and axis
  QValueAxis *axisY = m_axisY;
  QValueAxis *axisX = m_axisX;
  axisY->setTitleText(tr("%1 / %2 (%)")
                          .arg(variableName(m_numerator))
                          .arg(variableName(m_denominator)));
  int tickInterval = (m_maxY - m_minY) / axisY->tickCount();
  tickInterval =
      ((tickInterval + 5 - 1) / 5) * 5; // Round to nearest multiple of 5
  if (tickInterval >= 5) {
    axisY->setTickInterval(tickInterval);
    axisY->setTickType(QValueAxis::TicksDynamic);
    axisY->setLabelFormat("%d  ");
  } else {
    // The axes persist between updates: undo a previous dynamic setting.
    axisY->setTickType(QValueAxis::TicksFixed);
    axisY->setLabelFormat("%.1f  ");
  }
  int axisMaxY = m_maxY * 1.1;
  if (m_maxY <= 100 && m_maxY >= 90) {
    axisMaxY = 100;
  }

  axisX->setLabelFormat("%d  ");
  axisX->setTitleText(tr("Clock cycle"));

  if (m_series->points().isEmpty())
    axisY->setRange(0, 100);
  else
    // A little room below the lowest value, so that a flat curve (e.g. a
    // steady 99 % hit rate) is not drawn on top of the x axis.
    axisY->setRange(
        std::max(0.0, m_minY - 0.05 * std::max(1.0, axisMaxY - m_minY)),
        axisMaxY);
  axisX->setRange(m_ui->rangeSlider->minimumPosition(),
                  m_ui->rangeSlider->maximumPosition());

  // Empty-state hint shown as the chart title until data has been plotted.
  QString title;
  if (m_series->points().isEmpty()) {
    title = m_cache && m_cache->getHits() + m_cache->getMisses() > 0
                ? tr("No accesses inside the plot window - see "
                     "\"Max. cache plot cycles\" in Edit > Settings > "
                     "Environment.")
                : tr("No cache accesses recorded yet - step or run a "
                     "program to fill this plot.");
  }
  m_plot->setTitle(title);

  // Re-apply the theme colors (also to a changed axis title).
  applyPlotTheme();
}

void CachePlotWidget::updateRatioPlot() {
  const auto newCacheData = gatherData(m_lastCyclePlotted);
  const int nNewPoints = newCacheData.at(Accesses).size();
  if (nNewPoints == 0) {
    return;
  }

  // convenience function to move a series on/off the chart based on the number
  // of points to be added. We have to remove series due to append(QList(...))
  // calling redraw _for each_ point in the list. Not removing the series for
  // low nNewPoints avoids a flicker in plot labels. Everything is a balance...
  const auto plotMover = [=, this](QLineSeries *series, bool visible) {
    if (nNewPoints > 2) {
      if (visible) {
        this->m_plot->addSeries(series);
        series->attachAxis(m_axisX);
        series->attachAxis(m_axisY);
      } else {
        this->m_plot->removeSeries(series);
      }
    }
  };

  m_lastCyclePlotted = newCacheData.at(Accesses).last().x();

  QList<QPointF> newPoints;
  QList<QPointF> newWindowPoints;
  QPointF lastPoint;
  if (m_series->points().size() > 0) {
    lastPoint = m_series->points().constLast();
  } else {
    lastPoint = QPointF(-1, 0);
  }
  for (int i = 0; i < nNewPoints; ++i) {
    // Cummulative plot. For the unary variable, "Accesses" is just used to
    // index into the cache data for accessing the x variable.
    const auto &p1 = m_numerator == Unary
                         ? QPoint(newCacheData.at(Accesses).at(i).x(), 1)
                         : newCacheData.at(m_numerator).at(i);
    const auto &p2 = m_denominator == Unary
                         ? QPoint(newCacheData.at(Accesses).at(i).x(), 1)
                         : newCacheData.at(m_denominator).at(i);
    Q_ASSERT(p1.x() == p2.x() && "Data inconsistency");
    double ratio = 0;
    if (p2.y() != 0) {
      ratio = static_cast<double>(p1.y()) / p2.y();
      ratio *= 100.0;
    }
    bool skipPoint = false;
    const QPointF newPoint = QPointF(p1.x(), ratio);
    if (lastPoint.x() >= 0) {
      if (newPoint.x() - lastPoint.x() < m_xStep) {
        // Skip point; irrelevant at the current sampling level
        skipPoint = true;
      }
    }
    if (!skipPoint) {
      // The very first point has no predecessor to step from.
      if (lastPoint.x() >= 0)
        newPoints << stepPoint(lastPoint, newPoint);
      newPoints << newPoint;
      m_maxY = ratio > m_maxY ? ratio : m_maxY;
      m_minY = ratio < m_minY ? ratio : m_minY;
      lastPoint = newPoint;
    }

    // Moving average plot
    if (m_ui->showMAvg->isChecked()) {
      m_mavgData.push(newPoint.y());
      const double wAvg =
          std::accumulate(m_mavgData.begin(), m_mavgData.end(), 0.0) /
          m_mavgData.size();
      newWindowPoints << QPointF(p1.x(), wAvg);
    }
  }

  if (newPoints.size() == 0 && newWindowPoints.size() == 0) {
    return;
  }

  plotMover(m_series, false);
  m_series->append(newPoints);
  if (m_ui->showMAvg->isChecked()) {
    plotMover(m_mavgSeries, false);
    m_mavgSeries->append(newWindowPoints);
  }

  // Determine whether to resample;
  // *2 the allowed points to account for the addition of step points.
  // *2 to account for the fact that the setting specifies the minimum # of
  // points, and we resample at a 2x ratio
  const int maxPoints =
      RipesSettings::value(RIPES_SETTING_CACHE_MAXPOINTS).toInt() * 2 * 2;
  if (m_series->points().size() >= maxPoints) {
    resample(m_series, maxPoints / 2, m_xStep);
  }

  dropPointsBeforeWindow();

  plotMover(m_series, true);
  if (m_ui->showMAvg->isChecked()) {
    plotMover(m_mavgSeries, true);
  }

  updateTruncationHint();
}

void CachePlotWidget::dropPointsBeforeWindow() {
  const int maxCycles = plotWindowCycles();
  if (m_lastCyclePlotted <= maxCycles)
    return;
  const double windowStart =
      static_cast<double>(m_lastCyclePlotted - maxCycles);
  for (auto *series : {m_series, m_mavgSeries}) {
    const auto &points = series->points();
    int n = 0;
    while (n < points.size() && points.at(n).x() < windowStart)
      ++n;
    if (n > 0)
      series->removePoints(0, n);
  }
}

int CachePlotWidget::plottedPointCount() const {
  return m_series ? static_cast<int>(m_series->count()) : 0;
}

std::pair<int, int> CachePlotWidget::plottedCycleRange() const {
  if (!m_series || m_series->count() == 0)
    return {0, 0};
  return {static_cast<int>(m_series->at(0).x()),
          static_cast<int>(m_series->at(m_series->count() - 1).x())};
}

bool CachePlotWidget::isPlotTruncated() const {
  if (!m_cache || !m_series)
    return false;
  if (m_cache->isAccessTraceTruncated())
    return true;
  const auto &trace = m_cache->getAccessTrace();
  // The plot starts after cycle 0 (see gatherData); compare against the
  // first access it could have shown.
  const auto firstPlottable = trace.upper_bound(0);
  if (firstPlottable == trace.end() || m_series->count() == 0)
    return false;
  return m_series->at(0).x() > firstPlottable->first;
}

QString CachePlotWidget::truncationHintText() const {
  return m_ui->truncationHint->isVisibleTo(
             const_cast<CachePlotWidget *>(this))
             ? m_ui->truncationHint->text()
             : QString();
}

void CachePlotWidget::updateTruncationHint() {
  const bool truncated = isPlotTruncated();
  if (truncated) {
    const auto [first, last] = plottedCycleRange();
    m_ui->truncationHint->setText(
        tr("Only cycles %1 - %2 are plotted; older cycles were dropped. To "
           "plot more, raise \"Max. cache plot cycles\" in Edit > Settings "
           "> Environment.")
            .arg(first)
            .arg(last));
    const QString tip =
        tr("Long runs keep only the most recent cycles in the plot, so that "
           "plotting stays fast. The ratio is still counted from cycle 0, and "
           "the statistics above cover the whole run. \"Max. cache plot "
           "cycles\" sets how many cycles are plotted, \"Max. cache access "
           "traces\" how many cache accesses are kept (both in Edit > "
           "Settings > Environment).");
    m_ui->truncationHint->setToolTip(tip);
    m_ui->truncationHint->setStatusTip(tip);
  }
  m_ui->truncationHint->setVisible(truncated);
}

void CachePlotWidget::applyPlotTheme() {
  if (m_plot == nullptr)
    return;

  const QPalette pal = QGuiApplication::palette();
  const QColor text = pal.color(QPalette::WindowText);
  const QColor base = pal.color(QPalette::Base);
  const QColor grid = pal.color(QPalette::Mid);

  // Chart background follows the theme base color.
  m_plot->setBackgroundBrush(base);
  m_plot->setBackgroundPen(Qt::NoPen);
  m_plot->setTitleBrush(text);

  // Legend colors.
  if (auto *legend = m_plot->legend()) {
    legend->setLabelColor(text);
    legend->setBackgroundVisible(false);
  }

  // Axis label, title, line and grid colors.
  const auto axes = m_plot->axes();
  for (auto *axis : axes) {
    axis->setLabelsColor(text);
    axis->setTitleBrush(text);
    axis->setLinePenColor(text);
    axis->setGridLineColor(grid);
  }
}

void CachePlotWidget::resetRatioPlot() {
  m_maxY = -DBL_MAX;
  m_minY = DBL_MAX;
  m_series->clear();
  m_mavgSeries->clear();
  m_lastCyclePlotted = 0;
  m_xStep = 1;

  if (m_ui->showMAvg->isChecked()) {
    m_mavgData = FixedQueue<double>(m_ui->windowCycles->value());
    m_mavgSeries->setVisible(true);
  } else {
    m_mavgSeries->setVisible(false);
  }

  m_series->setVisible(m_ui->showRatio->isChecked());

  updateAllowedRange(RangeChangeSource::Cycles);
  updatePlotAxes();
  updateTruncationHint();
}

QString CachePlotWidget::variableName(Variable v) {
  const auto it = s_cacheVariableStrings.find(v);
  if (it == s_cacheVariableStrings.end())
    return QString();
  return QCoreApplication::translate("Ripes::CachePlotWidget",
                                     it->second.toUtf8().constData());
}

void CachePlotWidget::setupToolTips() {
  const QString numTip =
      tr("Numerator of the plotted ratio. The plot shows the cumulative value "
         "numerator / denominator x 100 % over time; the default Hits / "
         "Access count is the hit rate.");
  setTips(m_ui->num, numTip);
  setTips(m_ui->label, numTip);
  const QString denTip =
      tr("Denominator of the plotted ratio (see Numerator).");
  setTips(m_ui->den, denTip);
  setTips(m_ui->label_2, denTip);
  setTips(m_ui->showMAvg,
          tr("Also plot a moving average of the ratio over the last N "
             "cycles, which shows how the program behaves right now rather "
             "than since the start."));
  setTips(m_ui->windowCycles,
          tr("Window of the moving average, in clock cycles."));
  setTips(m_ui->size,
          tr("Total storage needed by this cache, in bits: data plus tag, "
             "valid, dirty and LRU bookkeeping bits."));
  setTips(m_ui->label_6, m_ui->size->toolTip());
  setTips(m_ui->sizeBreakdownButton,
          tr("Show how the cache size is split between data, tag and control "
             "bits"));
  setTips(m_ui->hitrate,
          tr("Hit rate = hits / (hits + misses), as a fraction between 0 and "
             "1."));
  setTips(m_ui->label_5, m_ui->hitrate->toolTip());
  setTips(m_ui->hits, tr("Number of accesses whose data was found in the "
                         "cache."));
  setTips(m_ui->label_11, m_ui->hits->toolTip());
  setTips(m_ui->misses,
          tr("Number of accesses whose data was not in the cache and had to "
             "be fetched from the next level (memory)."));
  setTips(m_ui->label_12, m_ui->misses->toolTip());
  setTips(m_ui->writebacks,
          tr("Number of dirty lines written back to memory when they were "
             "evicted (write-back policy only)."));
  setTips(m_ui->label_7, m_ui->writebacks->toolTip());
  setTips(m_ui->plotView,
          tr("Cache statistics over time. X axis: clock cycle; Y axis: the "
             "selected ratio in percent. The legend below the plot names each "
             "line. Click the crosshair buttons to inspect values."));
}

void CachePlotWidget::updateHitrate() {
  m_ui->hitrate->setText(QString::number(m_cache->getHitRate(), 'G', 4));
  m_ui->hits->setText(QString::number(m_cache->getHits()));
  m_ui->misses->setText(QString::number(m_cache->getMisses()));
  m_ui->writebacks->setText(QString::number(m_cache->getWritebacks()));
}

} // namespace Ripes
