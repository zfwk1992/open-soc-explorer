// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "explorertab.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <map>

#include "VSRTL/core/vsrtl_design.h"
#include "explorer/componentinfo.h"
#include "explorer/instructionexplainer.h"
#include "explorer/modelprofile.h"
#include "explorer/tracejson.h"
#include "explorer/tracerecorder.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "systemview.h"

namespace Ripes {

using namespace Explorer;
using Trace::EventType;

namespace {

/// Colors are semi-transparent so that they work on light and dark palettes.
QColor slotColor(TraceReplay::SlotKind kind, bool held, bool wrongPath) {
  switch (kind) {
  case TraceReplay::SlotKind::Instruction:
    if (held)
      return QColor(245, 166, 35, 150);
    return wrongPath ? QColor(229, 57, 53, 90) : QColor(66, 133, 244, 90);
  case TraceReplay::SlotKind::StallBubble:
    return QColor(245, 166, 35, 70);
  case TraceReplay::SlotKind::FlushBubble:
    return QColor(229, 57, 53, 70);
  case TraceReplay::SlotKind::Empty:
    break;
  }
  return QColor(128, 128, 128, 25);
}

QString rgba(const QColor &c) {
  return QString("rgba(%1,%2,%3,%4)")
      .arg(c.red())
      .arg(c.green())
      .arg(c.blue())
      .arg(c.alpha());
}

QString signedString(quint64 v, unsigned xlen) {
  const quint64 signBit = quint64(1) << (xlen - 1);
  const quint64 m = xlen >= 64 ? ~quint64(0) : (signBit << 1) - 1;
  v &= m;
  return QString::number(static_cast<qint64>((v ^ signBit) - signBit));
}

QString payloadSummary(const QVariantMap &payload) {
  QStringList parts;
  for (auto it = payload.begin(); it != payload.end(); ++it) {
    if (it.key() == "registers")
      continue;
    QString value = it.value().toString();
    if (it.value().isNull())
      value = "null";
    parts << it.key() + "=" + value;
  }
  return parts.join(", ");
}

} // namespace

// ============================================================================
// InstructionTimelineModel
// ============================================================================

InstructionTimelineModel::InstructionTimelineModel(QObject *parent)
    : QAbstractTableModel(parent) {}

void InstructionTimelineModel::setReplay(const TraceReplay *replay) {
  beginResetModel();
  m_replay = replay;
  m_rows.clear();
  if (m_replay)
    for (const auto &it : m_replay->instances())
      m_rows.push_back(it.first);
  endResetModel();
}

qint64 InstructionTimelineModel::instanceForRow(int row) const {
  return row >= 0 && static_cast<size_t>(row) < m_rows.size() ? m_rows[row]
                                                              : -1;
}

int InstructionTimelineModel::rowForInstance(qint64 id) const {
  auto it = std::lower_bound(m_rows.begin(), m_rows.end(), id);
  return it != m_rows.end() && *it == id
             ? static_cast<int>(std::distance(m_rows.begin(), it))
             : -1;
}

qint64 InstructionTimelineModel::cycleForColumn(int column) const {
  return m_replay ? m_replay->firstCycle() + column : 0;
}

int InstructionTimelineModel::columnForCycle(qint64 cycle) const {
  return m_replay ? static_cast<int>(cycle - m_replay->firstCycle()) : 0;
}

void InstructionTimelineModel::setCurrentCycle(qint64 cycle) {
  const qint64 old = m_currentCycle;
  m_currentCycle = cycle;
  if (!m_replay || m_rows.empty())
    return;
  for (qint64 c : {old, cycle}) {
    const int col = columnForCycle(c);
    if (col >= 0 && col < columnCount())
      emit dataChanged(index(0, col), index(rowCount() - 1, col));
  }
  emit headerDataChanged(Qt::Horizontal, columnForCycle(cycle),
                         columnForCycle(cycle));
}

int InstructionTimelineModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int InstructionTimelineModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid() || !m_replay)
    return 0;
  return static_cast<int>(m_replay->lastCycle() - m_replay->firstCycle() + 1);
}

QVariant InstructionTimelineModel::data(const QModelIndex &index,
                                        int role) const {
  if (!index.isValid() || !m_replay)
    return QVariant();
  const qint64 id = instanceForRow(index.row());
  const qint64 cycle = cycleForColumn(index.column());
  const auto *inst = m_replay->instance(id);
  if (!inst)
    return QVariant();

  const int stage = m_replay->stageOf(id, cycle);
  const bool held = stage >= 0 && m_replay->isHeld(id, cycle);
  const auto &stages = m_replay->trace().header.stages;
  const bool squashed = inst->outcome == TraceReplay::Outcome::Squashed;

  switch (role) {
  case Qt::DisplayRole:
    if (stage >= 0)
      return stages[stage].name + (held ? "*" : "");
    if (cycle == inst->endCycle)
      return squashed ? "✕" : "✓";
    return QVariant();
  case Qt::BackgroundRole: {
    if (stage >= 0)
      return slotColor(TraceReplay::SlotKind::Instruction, held, squashed);
    if (cycle == inst->endCycle)
      return squashed ? QColor(229, 57, 53, 60) : QColor(52, 168, 83, 70);
    if (cycle == m_currentCycle)
      return QColor(128, 128, 128, 40);
    return QVariant();
  }
  case Qt::TextAlignmentRole:
    return Qt::AlignCenter;
  case Qt::ToolTipRole: {
    QString tip = Trace::instanceName(id) + "  " + inst->assembly +
                  QString("\ncycle %1: ").arg(cycle);
    if (stage >= 0)
      tip += "in " + stages[stage].name + (held ? " (held: stalled)" : "");
    else if (cycle == inst->endCycle)
      tip += squashed ? "squashed" : "retired";
    else
      tip += "not in the pipeline";
    return tip;
  }
  default:
    return QVariant();
  }
}

QVariant InstructionTimelineModel::headerData(int section,
                                              Qt::Orientation orientation,
                                              int role) const {
  if (!m_replay)
    return QVariant();
  if (orientation == Qt::Horizontal) {
    if (role == Qt::DisplayRole)
      return QString::number(cycleForColumn(section));
    if (role == Qt::FontRole && cycleForColumn(section) == m_currentCycle) {
      QFont f;
      f.setBold(true);
      return f;
    }
    return QVariant();
  }
  const auto *inst = m_replay->instance(instanceForRow(section));
  if (!inst)
    return QVariant();
  if (role == Qt::DisplayRole)
    return QString("%1  %2").arg(Trace::instanceName(inst->id), inst->assembly);
  if (role == Qt::ToolTipRole) {
    switch (inst->outcome) {
    case TraceReplay::Outcome::Retired:
      return QString("Retired at cycle %1").arg(inst->endCycle);
    case TraceReplay::Outcome::Squashed:
      return QString("Squashed at cycle %1 (wrong path)").arg(inst->endCycle);
    case TraceReplay::Outcome::InFlight:
      return "In flight at the end of the trace";
    }
  }
  return QVariant();
}

// ============================================================================
// ExplorerTab
// ============================================================================

ExplorerTab::ExplorerTab(QToolBar *toolbar, TraceRecorder *recorder,
                         QWidget *parent)
    : RipesTab(toolbar, parent), m_recorder(recorder) {
  setupToolbar();
  setupUi();

  connect(m_recorder, &TraceRecorder::runStarted, this,
          &ExplorerTab::refreshFromRecorder, Qt::QueuedConnection);
  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, &ExplorerTab::refreshFromRecorder);
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          &ExplorerTab::refreshFromRecorder);
  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, &ExplorerTab::refreshComponents);
  connect(ProcessorHandler::get(), &ProcessorHandler::processorChanged, this,
          [this] {
            m_componentDesign = nullptr;
            if (m_visible)
              rebuildComponentTree();
          });
  connect(&m_playTimer, &QTimer::timeout, this, &ExplorerTab::playbackTick);
  connect(ThemeManager::get(), &ThemeManager::themeChanged, this, [this] {
    updateCycleViews();
    updateExplanation();
  });
}

ExplorerTab::~ExplorerTab() = default;

bool ExplorerTab::isDark() const { return appInDarkMode(); }

void ExplorerTab::setupToolbar() {
  m_recordAction =
      new QAction(QIcon(":/icons/trace.svg"), "Record execution trace", this);
  m_recordAction->setCheckable(true);
  m_recordAction->setChecked(m_recorder->isEnabled());
  connect(m_recordAction, &QAction::toggled, this, [this](bool enabled) {
    m_recorder->setEnabled(enabled);
    RipesSettings::setValue(RIPES_SETTING_OSX_TRACE_ENABLED, enabled);
    m_dirty = true;
    refreshFromRecorder();
  });
  m_toolbar->addAction(m_recordAction);

  m_toolbar->addWidget(new QLabel(" Max cycles: ", this));
  m_maxCycles = new QSpinBox(this);
  m_maxCycles->setRange(100, 10000000);
  m_maxCycles->setSingleStep(1000);
  m_maxCycles->setValue(static_cast<int>(m_recorder->maxCycles()));
  m_maxCycles->setToolTip(
      "Recording stops (and the trace is marked as truncated) after this "
      "many cycles. Reset the processor to apply a larger limit to a run "
      "that was already truncated.");
  connect(m_maxCycles, &QSpinBox::valueChanged, this, [this](int value) {
    m_recorder->setMaxCycles(value);
    RipesSettings::setValue(RIPES_SETTING_OSX_TRACE_MAXCYCLES, value);
  });
  m_toolbar->addWidget(m_maxCycles);
  m_toolbar->addSeparator();

  auto *exportAction = new QAction(QIcon(":/icons/saveas.svg"),
                                   "Export trace (JSON Lines)...", this);
  connect(exportAction, &QAction::triggered, this, &ExplorerTab::exportTrace);
  m_toolbar->addAction(exportAction);

  auto *openAction =
      new QAction(QIcon(":/icons/loadfile.svg"),
                  "Open a trace file for offline replay...", this);
  connect(openAction, &QAction::triggered, this, &ExplorerTab::openTrace);
  m_toolbar->addAction(openAction);

  m_liveAction =
      new QAction(QIcon(":/icons/reset.svg"), "Return to the live trace", this);
  m_liveAction->setEnabled(false);
  connect(m_liveAction, &QAction::triggered, this, &ExplorerTab::returnToLive);
  m_toolbar->addAction(m_liveAction);
}

void ExplorerTab::setupUi() {
  auto *root = new QVBoxLayout(this);

  m_banner = new QLabel(this);
  m_banner->setObjectName("explorerBanner");
  m_banner->setWordWrap(true);
  m_banner->setTextFormat(Qt::RichText);
  m_banner->setFrameShape(QFrame::StyledPanel);
  m_banner->setMargin(6);
  root->addWidget(m_banner);

  // Playback controls
  auto *bar = new QHBoxLayout();
  auto makeButton = [&](QStyle::StandardPixmap icon, const QString &tip) {
    auto *b = new QToolButton(this);
    b->setIcon(style()->standardIcon(icon));
    b->setToolTip(tip);
    bar->addWidget(b);
    return b;
  };
  auto *first = makeButton(QStyle::SP_MediaSkipBackward, "First cycle");
  auto *prev = makeButton(QStyle::SP_MediaSeekBackward, "Previous cycle");
  m_playButton =
      makeButton(QStyle::SP_MediaPlay, "Play back the recorded trace");
  m_playButton->setCheckable(true);
  auto *next = makeButton(QStyle::SP_MediaSeekForward, "Next cycle");
  auto *last = makeButton(QStyle::SP_MediaSkipForward, "Last recorded cycle");
  connect(first, &QToolButton::clicked, this, [this] {
    if (m_replay)
      setCycle(m_replay->firstCycle());
  });
  connect(prev, &QToolButton::clicked, this, [this] { setCycle(m_cycle - 1); });
  connect(next, &QToolButton::clicked, this, [this] { setCycle(m_cycle + 1); });
  connect(last, &QToolButton::clicked, this, [this] {
    if (m_replay)
      setCycle(m_replay->lastCycle());
  });
  connect(m_playButton, &QToolButton::toggled, this, [this](bool play) {
    m_playButton->setIcon(style()->standardIcon(play ? QStyle::SP_MediaPause
                                                     : QStyle::SP_MediaPlay));
    if (play) {
      m_followButton->setChecked(false);
      if (m_replay && m_cycle >= m_replay->lastCycle())
        setCycle(m_replay->firstCycle());
      m_playTimer.start(m_speed->currentData().toInt());
    } else {
      m_playTimer.stop();
    }
  });

  m_slider = new QSlider(Qt::Horizontal, this);
  connect(m_slider, &QSlider::valueChanged, this,
          [this](int v) { setCycle(v); });
  bar->addWidget(m_slider, 1);
  m_cycleLabel = new QLabel(this);
  m_cycleLabel->setMinimumWidth(130);
  bar->addWidget(m_cycleLabel);

  m_speed = new QComboBox(this);
  for (int cps : {1, 2, 5, 10, 25})
    m_speed->addItem(QString("%1 cycle%2/s").arg(cps).arg(cps == 1 ? "" : "s"),
                     1000 / cps);
  m_speed->setCurrentIndex(1);
  m_speed->setToolTip("Playback speed");
  connect(m_speed, &QComboBox::currentIndexChanged, this,
          [this] { m_playTimer.setInterval(m_speed->currentData().toInt()); });
  bar->addWidget(m_speed);

  m_followButton = new QToolButton(this);
  m_followButton->setText("Follow simulator");
  m_followButton->setCheckable(true);
  m_followButton->setChecked(true);
  m_followButton->setToolTip(
      "Jump to the newest recorded cycle whenever the simulator advances");
  connect(m_followButton, &QToolButton::toggled, this, [this](bool on) {
    if (on && m_replay)
      setCycle(m_replay->lastCycle());
  });
  bar->addWidget(m_followButton);
  root->addLayout(bar);

  // Main area
  auto *mainSplitter = new QSplitter(Qt::Horizontal, this);
  auto *left = new QSplitter(Qt::Vertical, mainSplitter);

  auto *stripGroup = new QGroupBox("Pipeline at the selected cycle", left);
  auto *stripGroupLayout = new QVBoxLayout(stripGroup);
  m_stripContainer = new QWidget(stripGroup);
  m_stripLayout = new QVBoxLayout(m_stripContainer);
  m_stripLayout->setContentsMargins(0, 0, 0, 0);
  stripGroupLayout->addWidget(m_stripContainer);
  auto *legend = new QLabel(
      QString("<span style='background:%1'>&nbsp;instruction&nbsp;</span> "
              "<span style='background:%2'>&nbsp;held (stall)&nbsp;</span> "
              "<span style='background:%3'>&nbsp;stall bubble&nbsp;</span> "
              "<span style='background:%4'>&nbsp;flushed / wrong path&nbsp;"
              "</span>")
          .arg(
              rgba(slotColor(TraceReplay::SlotKind::Instruction, false, false)),
              rgba(slotColor(TraceReplay::SlotKind::Instruction, true, false)),
              rgba(slotColor(TraceReplay::SlotKind::StallBubble, false, false)),
              rgba(
                  slotColor(TraceReplay::SlotKind::FlushBubble, false, false))),
      stripGroup);
  stripGroupLayout->addWidget(legend);

  auto *timelineGroup = new QGroupBox(
      "Instruction timeline (rows: dynamic instructions, columns: cycles; "
      "* = held, ✓ = retired, ✕ = squashed)",
      left);
  auto *timelineLayout = new QVBoxLayout(timelineGroup);
  m_timeline = new QTableView(timelineGroup);
  m_timeline->setObjectName("explorerTimeline");
  m_timelineModel = new InstructionTimelineModel(this);
  m_timeline->setModel(m_timelineModel);
  m_timeline->horizontalHeader()->setDefaultSectionSize(42);
  m_timeline->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  m_timeline->verticalHeader()->setDefaultSectionSize(22);
  m_timeline->setSelectionMode(QAbstractItemView::SingleSelection);
  m_timeline->setEditTriggers(QAbstractItemView::NoEditTriggers);
  connect(m_timeline, &QTableView::clicked, this,
          [this](const QModelIndex &idx) {
            m_followButton->setChecked(false);
            selectInstance(m_timelineModel->instanceForRow(idx.row()));
            setCycle(m_timelineModel->cycleForColumn(idx.column()));
          });
  connect(m_timeline->verticalHeader(), &QHeaderView::sectionClicked, this,
          [this](int row) {
            selectInstance(m_timelineModel->instanceForRow(row));
          });
  timelineLayout->addWidget(m_timeline);

  auto *eventsGroup = new QGroupBox("Events at the selected cycle", left);
  auto *eventsLayout = new QVBoxLayout(eventsGroup);
  m_events = new QTableWidget(eventsGroup);
  m_events->setObjectName("explorerEvents");
  m_events->setColumnCount(6);
  m_events->setHorizontalHeaderLabels(
      {"Seq", "Event", "Component", "Instruction", "Details", "Observability"});
  m_events->horizontalHeader()->setStretchLastSection(false);
  m_events->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  m_events->verticalHeader()->setVisible(false);
  m_events->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_events->setSelectionBehavior(QAbstractItemView::SelectRows);
  connect(m_events, &QTableWidget::cellClicked, this, [this](int row, int) {
    const auto *item = m_events->item(row, 3);
    if (item && item->data(Qt::UserRole).toLongLong() >= 0)
      selectInstance(item->data(Qt::UserRole).toLongLong());
  });
  eventsLayout->addWidget(m_events);
  left->setStretchFactor(0, 0);
  left->setStretchFactor(1, 3);
  left->setStretchFactor(2, 2);

  m_sideTabs = new QTabWidget(mainSplitter);
  m_sideTabs->setObjectName("explorerSideTabs");
  m_explanation = new QTextBrowser(m_sideTabs);
  m_explanation->setObjectName("explorerExplanation");
  m_explanation->setOpenLinks(false);
  m_sideTabs->addTab(m_explanation, "Instruction");

  m_registers = new QTableWidget(m_sideTabs);
  m_registers->setObjectName("explorerRegisters");
  m_registers->setColumnCount(4);
  m_registers->setHorizontalHeaderLabels(
      {"Register", "Alias", "Value", "Signed"});
  m_registers->verticalHeader()->setVisible(false);
  m_registers->horizontalHeader()->setStretchLastSection(true);
  m_registers->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_sideTabs->addTab(m_registers, "Registers");

  auto *componentSplitter = new QSplitter(Qt::Vertical, m_sideTabs);
  m_componentTree = new QTreeWidget(componentSplitter);
  m_componentTree->setObjectName("explorerComponents");
  m_componentTree->setHeaderLabels({"Component", "Role"});
  connect(m_componentTree, &QTreeWidget::itemSelectionChanged, this,
          &ExplorerTab::componentSelected);
  m_componentDetail = new QTextBrowser(componentSplitter);
  m_componentDetail->setObjectName("explorerComponentDetail");
  m_componentDetail->setOpenLinks(false);
  connect(m_componentDetail, &QTextBrowser::anchorClicked, this,
          [this](const QUrl &url) {
            const qint64 id = Trace::instanceFromName(url.toString());
            if (id >= 0) {
              selectInstance(id);
              m_sideTabs->setCurrentWidget(m_explanation);
            }
          });
  componentSplitter->setStretchFactor(0, 1);
  componentSplitter->setStretchFactor(1, 2);
  m_sideTabs->addTab(componentSplitter, "CPU components");
  connect(m_sideTabs, &QTabWidget::currentChanged, this, [this] {
    if (m_sideTabs->currentIndex() == 2)
      refreshComponents();
  });

  m_statistics = new QTextBrowser(m_sideTabs);
  m_statistics->setObjectName("explorerStatistics");
  m_sideTabs->addTab(m_statistics, "Statistics");

  m_systemView = new SystemView(m_sideTabs);
  m_sideTabs->addTab(m_systemView, "System");

  mainSplitter->setStretchFactor(0, 3);
  mainSplitter->setStretchFactor(1, 2);
  root->addWidget(mainSplitter, 1);

  updateExplanation();
}

void ExplorerTab::tabVisibilityChanged(bool visible) {
  m_visible = visible;
  if (visible) {
    refreshFromRecorder();
    if (!m_componentDesign)
      rebuildComponentTree();
  } else {
    m_playButton->setChecked(false);
  }
}

void ExplorerTab::refreshFromRecorder() {
  if (m_offline)
    return;
  if (!m_visible) {
    m_dirty = true;
    return;
  }
  const quint64 revision = m_recorder->revision();
  if (revision == m_revision && !m_dirty)
    return;
  m_revision = revision;
  m_dirty = false;
  loadReplay(m_recorder->trace());
}

void ExplorerTab::loadReplay(Trace::Trace trace) {
  m_replay = std::make_unique<TraceReplay>(std::move(trace));
  m_timelineModel->setReplay(m_replay.get());
  m_slider->blockSignals(true);
  m_slider->setRange(static_cast<int>(m_replay->firstCycle()),
                     static_cast<int>(m_replay->lastCycle()));
  m_slider->blockSignals(false);
  if (m_selected >= 0 && !m_replay->instance(m_selected))
    m_selected = -1;
  updateBanner();
  updateStatistics();
  m_systemView->setTrace(m_replay->trace().header);
  const bool follow = !m_offline && m_followButton->isChecked();
  m_cycle = std::clamp(follow ? m_replay->lastCycle() : m_cycle,
                       m_replay->firstCycle(), m_replay->lastCycle());
  setCycle(m_cycle);
  updateCycleViews();
  updateExplanation();
}

void ExplorerTab::setCycle(qint64 cycle) {
  if (!m_replay)
    return;
  cycle = std::clamp(cycle, m_replay->firstCycle(), m_replay->lastCycle());
  const bool changed = cycle != m_cycle;
  m_cycle = cycle;
  m_slider->blockSignals(true);
  m_slider->setValue(static_cast<int>(cycle));
  m_slider->blockSignals(false);
  m_cycleLabel->setText(
      QString("Cycle %1 / %2").arg(cycle).arg(m_replay->lastCycle()));
  m_timelineModel->setCurrentCycle(cycle);
  const int col = m_timelineModel->columnForCycle(cycle);
  const int row = std::max(0, m_timelineModel->rowForInstance(m_selected));
  if (m_timelineModel->rowCount() > 0)
    m_timeline->scrollTo(m_timelineModel->index(row, col),
                         QAbstractItemView::EnsureVisible);
  if (changed)
    updateCycleViews();
}

void ExplorerTab::selectInstance(qint64 id) {
  m_selected = id;
  const int row = m_timelineModel->rowForInstance(id);
  if (row >= 0)
    m_timeline->selectRow(row);
  updateExplanation();
  updatePipelineStrip();
  updateEvents();
}

void ExplorerTab::updateCycleViews() {
  if (!m_replay)
    return;
  updatePipelineStrip();
  updateEvents();
  updateRegisters();
}

void ExplorerTab::updatePipelineStrip() {
  if (!m_replay)
    return;
  const auto &stages = m_replay->trace().header.stages;
  if (m_stageButtons.size() != stages.size()) {
    // Hide the old buttons and show the new ones right away: deleteLater()
    // and the layout's own show are both deferred to the event loop.
    for (auto *b : m_stageButtons) {
      b->hide();
      b->deleteLater();
    }
    m_stageButtons.clear();
    // Nested lane layouts are their own layout items; deleting them leaves
    // the (already scheduled for deletion) buttons alone.
    while (auto *item = m_stripLayout->takeAt(0))
      delete item;
    std::map<unsigned, QHBoxLayout *> lanes;
    for (const auto &sd : stages) {
      if (!lanes.count(sd.lane)) {
        lanes[sd.lane] = new QHBoxLayout();
        m_stripLayout->addLayout(lanes[sd.lane]);
      }
      auto *b = new QToolButton(m_stripContainer);
      b->setToolButtonStyle(Qt::ToolButtonTextOnly);
      b->setMinimumSize(120, 52);
      b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
      connect(b, &QToolButton::clicked, this, [this, b] {
        const qint64 id = b->property("instance").toLongLong();
        if (id >= 0)
          selectInstance(id);
      });
      lanes[sd.lane]->addWidget(b);
      b->setVisible(m_stripContainer->isVisible());
      m_stageButtons.push_back(b);
    }
  }

  const auto pipeline = m_replay->pipelineAt(m_cycle);
  for (size_t i = 0; i < stages.size() && i < pipeline.size(); ++i) {
    const auto &slot = pipeline[i];
    auto *b = m_stageButtons[i];
    QString body;
    bool wrongPath = false;
    switch (slot.kind) {
    case TraceReplay::SlotKind::Instruction: {
      const auto *inst = m_replay->instance(slot.instance);
      body = inst ? inst->assembly : Trace::instanceName(slot.instance);
      wrongPath = inst && inst->outcome == TraceReplay::Outcome::Squashed;
      if (slot.held)
        body += "  (held)";
      if (slot.occupants > 1)
        body += QString("\n+%1 more").arg(slot.occupants - 1);
      break;
    }
    case TraceReplay::SlotKind::StallBubble:
      body = "stall bubble";
      break;
    case TraceReplay::SlotKind::FlushBubble:
      body = "flushed (bubble)";
      break;
    case TraceReplay::SlotKind::Empty:
      body = "—";
      break;
    }
    b->setText(stages[i].name + "\n" + body);
    b->setProperty("instance", slot.kind == TraceReplay::SlotKind::Instruction
                                   ? slot.instance
                                   : qint64(-1));
    const bool selected = slot.kind == TraceReplay::SlotKind::Instruction &&
                          slot.instance == m_selected;
    b->setStyleSheet(QString("QToolButton { background-color: %1; border: %2; "
                             "border-radius: 4px; padding: 4px; }")
                         .arg(rgba(slotColor(slot.kind, slot.held, wrongPath)),
                              selected ? "2px solid palette(highlight)"
                                       : "1px solid palette(mid)"));
    b->setToolTip(slot.kind == TraceReplay::SlotKind::Instruction
                      ? Trace::instanceName(slot.instance) +
                            " - click to explain this instruction"
                      : QString());
  }
}

void ExplorerTab::updateEvents() {
  if (!m_replay)
    return;
  const auto indices = m_replay->eventsAt(m_cycle);
  const auto &events = m_replay->trace().events;
  m_events->setRowCount(static_cast<int>(indices.size()));
  for (size_t r = 0; r < indices.size(); ++r) {
    const auto &ev = events[indices[r]];
    auto set = [&](int col, const QString &text) {
      auto *item = new QTableWidgetItem(text);
      if (ev.instance >= 0 && ev.instance == m_selected) {
        QFont f = item->font();
        f.setBold(true);
        item->setFont(f);
      }
      m_events->setItem(static_cast<int>(r), col, item);
      return item;
    };
    set(0, QString::number(ev.sequence));
    set(1, Trace::eventTypeName(ev.type));
    set(2, ev.component);
    auto *instItem =
        set(3, ev.instance >= 0
                   ? Trace::instanceName(ev.instance) + "  " + ev.assembly
                   : QString());
    instItem->setData(Qt::UserRole, ev.instance);
    set(4, payloadSummary(ev.payload));
    set(5, Trace::observabilityName(ev.observability));
  }
  m_events->resizeColumnsToContents();
  m_events->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
}

void ExplorerTab::updateRegisters() {
  if (!m_replay)
    return;
  const auto &h = m_replay->trace().header;
  const auto now = m_replay->registersAt(m_cycle);
  const auto before = m_cycle > m_replay->firstCycle()
                          ? m_replay->registersAt(m_cycle - 1)
                          : now;
  int rows = 0;
  for (const auto &rf : h.registerFiles)
    rows += rf.names.size();
  m_registers->setRowCount(rows);
  int row = 0;
  const QColor changedColor(245, 166, 35, 110);
  for (const auto &rf : h.registerFiles) {
    const auto itNow = now.find(rf.name);
    const auto itBefore = before.find(rf.name);
    for (int i = 0; i < rf.names.size(); ++i, ++row) {
      quint64 v = 0, pv = 0;
      if (itNow != now.end() && static_cast<size_t>(i) < itNow->second.size())
        v = itNow->second[i];
      if (itBefore != before.end() &&
          static_cast<size_t>(i) < itBefore->second.size())
        pv = itBefore->second[i];
      const bool changed = v != pv;
      const bool known = m_replay->registerKnownAt(rf.name, i, m_cycle);
      const QStringList cells = {
          rf.names[i], rf.aliases.value(i),
          known ? Trace::toHex(v, h.xlen) : QString("unknown"),
          known ? signedString(v, h.xlen) : QString("-")};
      for (int c = 0; c < cells.size(); ++c) {
        auto *item = new QTableWidgetItem(cells[c]);
        if (changed) {
          item->setBackground(changedColor);
          QFont f = item->font();
          f.setBold(true);
          item->setFont(f);
          item->setToolTip(QString("Written in cycle %1 (was %2)")
                               .arg(m_cycle)
                               .arg(Trace::toHex(pv, h.xlen)));
        }
        m_registers->setItem(row, c, item);
      }
    }
  }
}

void ExplorerTab::updateExplanation() {
  if (!m_replay || m_selected < 0) {
    m_explanation->setHtml(
        "<p>Select an instruction in the pipeline view or the timeline to "
        "see what it does, its operand and result values as recorded by the "
        "simulator, its hazards and its journey through the pipeline.</p>");
    return;
  }
  m_explanation->setHtml(
      explanationToHtml(explainInstance(*m_replay, m_selected), isDark()));
}

void ExplorerTab::updateBanner() {
  if (!m_replay) {
    m_banner->setText("No trace recorded yet.");
    return;
  }
  const auto &t = m_replay->trace();
  const auto &s = t.header.source;
  QString mode;
  if (m_offline)
    mode = "Offline replay of <i>" + m_offlineFile.toHtmlEscaped() + "</i>";
  else if (!m_recorder->isEnabled())
    mode = "Recording disabled";
  else
    mode = "Live trace";
  QString completeness;
  const auto &c = t.completeness;
  if (c.truncated)
    completeness = "truncated (" + c.truncationReason + ")";
  else if (c.programFinished)
    completeness = c.startsAtReset ? "complete" : "finished, started mid-run";
  else
    completeness = "in progress";
  m_banner->setText(
      QString("<b>Engine:</b> %1 %2 &nbsp;·&nbsp; <b>Model:</b> %3 (%4) "
              "&nbsp;·&nbsp; <b>Fidelity:</b> %5 &nbsp;·&nbsp; <b>Clock "
              "domain:</b> %6 &nbsp;·&nbsp; %7 (%8)")
          .arg(s.engine.toHtmlEscaped(), s.engineVersion.toHtmlEscaped(),
               s.modelName.toHtmlEscaped(), s.model.toHtmlEscaped(),
               s.fidelity.toHtmlEscaped(), t.header.clockDomain.toHtmlEscaped(),
               mode, completeness));
  if (t.header.ticksPerCycle > 0)
    m_banner->setText(m_banner->text() +
                      QString(" &nbsp;·&nbsp; 1 cycle = %1 %2 (engine ticks)")
                          .arg(t.header.ticksPerCycle)
                          .arg(t.header.tickUnit.toHtmlEscaped()));
  if (t.header.stages.empty())
    m_banner->setText(m_banner->text() +
                      " &nbsp;·&nbsp; <i>no pipeline stage information in "
                      "this trace</i>");
  m_banner->setToolTip(s.fidelityNote);
}

void ExplorerTab::updateStatistics() {
  if (!m_replay)
    return;
  const auto st = m_replay->stats();
  const auto &t = m_replay->trace();
  QString html = "<h3>Statistics from the recorded trace</h3><table "
                 "cellpadding='3'>";
  auto row = [&](const QString &k, const QString &v) {
    html +=
        "<tr><td>" + k + "</td><td align='right'><b>" + v + "</b></td></tr>";
  };
  row("Cycles", QString::number(st.cycles));
  row("Instructions retired", QString::number(st.retired));
  row("CPI", st.retired ? QString::number(st.cpi, 'f', 3) : "-");
  row("Stall cycles", QString::number(st.stallCycles));
  row("Stall bubbles inserted", QString::number(st.stallBubbles));
  row("Squashed instructions", QString::number(st.squashed));
  row("Conditional branches (taken)",
      QString("%1 (%2)").arg(st.retiredBranches).arg(st.retiredTakenBranches));
  row("Register writes", QString::number(st.registerWrites));
  row("Data memory reads / writes",
      QString("%1 / %2").arg(st.memoryReads).arg(st.memoryWrites));
  row("Events", QString::number(t.events.size()));
  html += "</table>";
  html += "<h4>Trace completeness</h4><ul>";
  html += QString("<li>Starts at reset: %1</li>")
              .arg(t.completeness.startsAtReset ? "yes" : "no");
  html += QString("<li>Truncated: %1</li>")
              .arg(t.completeness.truncated
                       ? "yes (" + t.completeness.truncationReason + ")"
                       : QString("no"));
  html += QString("<li>Program finished: %1</li>")
              .arg(t.completeness.programFinished ? "yes" : "no");
  html += "</ul><h4>Caveats</h4><ul>";
  for (const auto &cav : t.completeness.caveats)
    html += "<li>" + cav.toHtmlEscaped() + "</li>";
  html += "</ul><h4>Source</h4><p>" +
          t.header.source.fidelityNote.toHtmlEscaped() +
          "</p><p>Model "
          "implementation: <code>" +
          t.header.source.sourceFile.toHtmlEscaped() + "</code></p>";
  m_statistics->setHtml(html);
}

void ExplorerTab::exportTrace() {
  const QString path = QFileDialog::getSaveFileName(
      this, "Export execution trace", "trace.jsonl",
      "JSON Lines trace (*.jsonl);;All files (*)");
  if (path.isEmpty())
    return;
  const auto trace =
      m_offline && m_replay ? m_replay->trace() : m_recorder->trace();
  QString error;
  if (!Trace::writeJsonlFile(trace, path, &error))
    QMessageBox::warning(this, "Export trace", error);
}

void ExplorerTab::openTrace() {
  const QString path =
      QFileDialog::getOpenFileName(this, "Open execution trace", QString(),
                                   "JSON Lines trace (*.jsonl);;All files (*)");
  if (path.isEmpty())
    return;
  QString error;
  if (!openTraceFile(path, &error))
    QMessageBox::warning(this, "Open trace", error);
}

bool ExplorerTab::openTraceFile(const QString &path, QString *error) {
  auto trace = Trace::readJsonlFile(path, error);
  if (!trace.has_value())
    return false;
  m_offline = true;
  m_offlineFile = path;
  m_liveAction->setEnabled(true);
  m_followButton->setChecked(false);
  m_followButton->setEnabled(false);
  m_selected = -1;
  loadReplay(std::move(*trace));
  return true;
}

void ExplorerTab::returnToLive() {
  m_offline = false;
  m_offlineFile.clear();
  m_liveAction->setEnabled(false);
  m_followButton->setEnabled(true);
  m_selected = -1;
  m_dirty = true;
  refreshFromRecorder();
}

void ExplorerTab::playbackTick() {
  if (!m_replay || m_cycle >= m_replay->lastCycle()) {
    m_playButton->setChecked(false);
    return;
  }
  setCycle(m_cycle + 1);
}

void ExplorerTab::rebuildComponentTree() {
  m_componentTree->clear();
  const auto *design = dynamic_cast<const vsrtl::core::Design *>(
      ProcessorHandler::getProcessor());
  m_componentDesign = design;
  if (!design) {
    m_componentDetail->setHtml(
        "<p>The selected processor is not a VSRTL datapath model.</p>");
    return;
  }
  std::function<void(QTreeWidgetItem *, const vsrtl::SimComponent *, int)> add =
      [&](QTreeWidgetItem *parent, const vsrtl::SimComponent *c, int depth) {
        const QString name = QString::fromStdString(c->getName());
        const auto info = componentInfo(name);
        auto *item = parent ? new QTreeWidgetItem(parent)
                            : new QTreeWidgetItem(m_componentTree);
        item->setText(0, name);
        item->setText(1, info ? info->title : QString());
        item->setData(0, Qt::UserRole,
                      QVariant::fromValue(reinterpret_cast<quintptr>(c)));
        if (depth < 1)
          for (const auto *sub : c->getSubComponents())
            add(item, sub, depth + 1);
      };
  for (const auto *c : design->getSubComponents())
    add(nullptr, c, 0);
  m_componentTree->sortItems(0, Qt::AscendingOrder);
  m_componentTree->resizeColumnToContents(0);
  m_componentDetail->setHtml(
      "<p>Select a datapath component to see its purpose, its live input "
      "and output signals and the instruction it is currently working "
      "on.</p>");
}

void ExplorerTab::refreshComponents() {
  if (!m_visible || m_sideTabs->currentIndex() != 2)
    return;
  if (m_componentDesign != dynamic_cast<const vsrtl::core::Design *>(
                               ProcessorHandler::getProcessor()))
    rebuildComponentTree();
  else
    componentSelected();
}

void ExplorerTab::componentSelected() {
  const auto items = m_componentTree->selectedItems();
  if (items.isEmpty() || !m_componentDesign || ProcessorHandler::isRunning())
    return;
  const auto *c = reinterpret_cast<const vsrtl::SimComponent *>(
      items.first()->data(0, Qt::UserRole).value<quintptr>());
  if (!c)
    return;
  const QString name = QString::fromStdString(c->getName());
  const auto info = componentInfo(name);
  const auto *proc = ProcessorHandler::getProcessor();

  QString html = QString("<h3>%1 <span style='font-weight:normal'>(<code>%2"
                         "</code>)</span></h3>")
                     .arg((info ? info->title : name).toHtmlEscaped(),
                          name.toHtmlEscaped());
  if (info) {
    html += "<p><b>Purpose:</b> " + info->purpose.toHtmlEscaped() + "</p>";
    html += "<p>" + info->behaviour.toHtmlEscaped() + "</p>";
  } else {
    html += "<p>No educational description for this component yet. Its live "
            "signals are shown below.</p>";
  }

  // Instruction currently associated with the component's stage.
  if (info && !info->stage.isEmpty() && proc) {
    for (auto idx : proc->structure().stageIt()) {
      if (proc->stageName(idx) != info->stage || idx.lane() != 0)
        continue;
      const auto si = proc->stageInfo(idx);
      html += QString("<p><b>Instruction in %1 (cycle %2):</b> ")
                  .arg(info->stage)
                  .arg(proc->getCycleCount());
      if (si.stage_valid) {
        QString link;
        if (!m_offline && m_replay)
          for (const auto &slot : m_replay->pipelineAt(m_replay->lastCycle()))
            if (slot.kind == TraceReplay::SlotKind::Instruction &&
                m_replay->trace().header.stages[slot.stage].name ==
                    info->stage &&
                m_replay->instance(slot.instance)->pc == si.pc)
              link = Trace::instanceName(slot.instance);
        const QString text =
            "<code>" +
            ProcessorHandler::disassembleInstr(si.pc).toHtmlEscaped() +
            "</code>";
        html +=
            link.isEmpty() ? text : "<a href='" + link + "'>" + text + "</a>";
      } else {
        html += "<i>none (bubble or empty)</i>";
      }
      html += "</p>";
    }
  }

  auto portTable = [&](const std::vector<vsrtl::SimPort *> &ports,
                       bool inputs) {
    if (ports.empty())
      return;
    html += QString("<h4>%1 <span style='font-weight:normal'>(live simulator "
                    "values)</span></h4>")
                .arg(inputs ? "Inputs" : "Outputs");
    html += "<table cellspacing='0' cellpadding='2'><tr><th "
            "align='left'>Port</th><th align='right'>Bits</th><th "
            "align='left'>Value</th><th align='left'>" +
            QString(inputs ? "Driven by" : "Drives") + "</th></tr>";
    for (auto *p : ports) {
      QString value = Trace::toHex(p->uValue(), std::max(4u, p->getWidth()));
      if (p->isEnumPort())
        value += " (" + QString::fromStdString(p->valueToEnumString()) + ")";
      QStringList peers;
      if (inputs) {
        if (auto *src = p->getInputPort<vsrtl::SimPort>())
          if (auto *owner = src->getParent<vsrtl::SimComponent>())
            peers << QString::fromStdString(owner->getName() + "." +
                                            src->getName());
      } else {
        for (auto *dst : p->getOutputPorts<vsrtl::SimPort>())
          if (auto *owner = dst->getParent<vsrtl::SimComponent>())
            peers << QString::fromStdString(owner->getName() + "." +
                                            dst->getName());
      }
      html += QString("<tr><td><code>%1</code></td><td "
                      "align='right'>%2</td><td><code>%3</code></td><td>%4"
                      "</td></tr>")
                  .arg(QString::fromStdString(p->getName()).toHtmlEscaped())
                  .arg(p->getWidth())
                  .arg(value.toHtmlEscaped(), peers.join(", ").toHtmlEscaped());
    }
    html += "</table>";
  };
  portTable(c->getInputPorts(), true);
  portTable(c->getOutputPorts(), false);

  if (info) {
    html += "<h4>Implemented in</h4><p><code>" +
            info->sourceFile.toHtmlEscaped() + "</code></p>";
    if (!info->related.isEmpty())
      html += "<p><b>Related components:</b> " +
              info->related.join(", ").toHtmlEscaped() + "</p>";
  }
  html += "<p style='color:gray'>Signal values are read directly from the "
          "VSRTL model of the selected educational processor at the current "
          "simulator cycle; they are not recorded in the trace.</p>";
  m_componentDetail->setHtml(html);
}

} // namespace Ripes
