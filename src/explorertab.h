// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QAbstractTableModel>
#include <QElapsedTimer>
#include <QTimer>

#include <memory>

#include "explorer/tracereplay.h"
#include "ripestab.h"

class QAction;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableView;
class QTableWidget;
class QTextBrowser;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QHBoxLayout;
class QVBoxLayout;

namespace Ripes {

class SystemView;
class HudBanner;
class PipelineStrip;
class StripLegend;
namespace Explorer {
class TraceRecorder;
}

/**
 * @brief The InstructionTimelineModel class
 * Table model of a replayed trace: one row per dynamic instruction (program
 * order), one column per cycle. Each cell shows the pipeline stage the
 * instruction occupied in that cycle, as reconstructed by TraceReplay.
 */
class InstructionTimelineModel : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit InstructionTimelineModel(QObject *parent = nullptr);
  void setReplay(const Explorer::TraceReplay *replay);
  qint64 instanceForRow(int row) const;
  int rowForInstance(qint64 id) const;
  qint64 cycleForColumn(int column) const;
  int columnForCycle(qint64 cycle) const;
  void setCurrentCycle(qint64 cycle);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;

private:
  const Explorer::TraceReplay *m_replay = nullptr;
  std::vector<qint64> m_rows;
  qint64 m_currentCycle = -1;
};

/**
 * @brief The ExplorerTab class
 * Open SoC Explorer's CPU explorer: replays the recorded execution trace on a
 * timeline (forwards and backwards, independent of the simulator), shows the
 * pipeline occupancy, events and register state at any recorded cycle,
 * explains selected instructions and lets users inspect the live datapath
 * components of the selected Ripes model.
 */
class ExplorerTab : public RipesTab {
  Q_OBJECT
public:
  ExplorerTab(QToolBar *toolbar, Explorer::TraceRecorder *recorder,
              QWidget *parent = nullptr);
  ~ExplorerTab() override;

  void tabVisibilityChanged(bool visible) override;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

public:
  /// Loads a trace file (e.g. converted from gem5) for offline replay.
  bool openTraceFile(const QString &path, QString *error = nullptr);

public slots:
  void setCycle(qint64 cycle);
  void selectInstance(qint64 id);

private slots:
  void refreshFromRecorder();
  void exportTrace();
  void openTrace();
  void returnToLive();
  void playbackTick();
  void refreshComponents();
  void componentSelected();

private:
  void setupToolbar();
  void setupUi();
  void loadReplay(Trace::Trace trace);
  void updateBanner();
  void updateCycleViews();
  void updatePipelineStrip();
  void updateEvents();
  void updateRegisters();
  void updateExplanation();
  void updateStatistics();
  void rebuildComponentTree();
  void applyTheme();
  bool isDark() const;

  Explorer::TraceRecorder *m_recorder = nullptr;
  std::unique_ptr<Explorer::TraceReplay> m_replay;
  quint64 m_revision = ~quint64(0);
  bool m_offline = false;
  QString m_offlineFile;
  bool m_visible = false;
  bool m_dirty = true;
  qint64 m_cycle = 0;
  qint64 m_selected = -1;
  /// Cycle shown before the last change, for the strip transition.
  qint64 m_previousCycle = -1;
  /// Time since the last cycle change; transitions are skipped when cycles
  /// change faster than a transition takes.
  QElapsedTimer m_sinceCycleChange;

  HudBanner *m_banner = nullptr;
  QLabel *m_cycleLabel = nullptr;
  QSlider *m_slider = nullptr;
  std::vector<std::pair<QToolButton *, int>> m_controlButtons;
  QToolButton *m_playButton = nullptr;
  QComboBox *m_speed = nullptr;
  QToolButton *m_followButton = nullptr;
  QTimer m_playTimer;

  PipelineStrip *m_strip = nullptr;
  StripLegend *m_legend = nullptr;

  QTableView *m_timeline = nullptr;
  InstructionTimelineModel *m_timelineModel = nullptr;
  QTableWidget *m_events = nullptr;
  QTableWidget *m_registers = nullptr;
  QTextBrowser *m_explanation = nullptr;
  QTextBrowser *m_statistics = nullptr;
  SystemView *m_systemView = nullptr;
  QTabWidget *m_sideTabs = nullptr;

  QTreeWidget *m_componentTree = nullptr;
  QTextBrowser *m_componentDetail = nullptr;
  const void *m_componentDesign = nullptr;

  QAction *m_recordAction = nullptr;
  QSpinBox *m_maxCycles = nullptr;
  QAction *m_liveAction = nullptr;
};

} // namespace Ripes
