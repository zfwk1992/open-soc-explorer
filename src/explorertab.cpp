// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "explorertab.h"

#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
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
#include <cmath>
#include <functional>
#include <map>

#include "VSRTL/core/vsrtl_design.h"
#include "explorer/componentinfo.h"
#include "explorer/instructionexplainer.h"
#include "explorer/modelprofile.h"
#include "explorer/tracejson.h"
#include "explorer/tracerecorder.h"
#include "explorer/tracetext.h"
#include "explorerwidgets.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "systemview.h"
#include "theme/painting.h"
#include "theme/theme.h"

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

/// A horizontal meter in Qt rich text (a two-cell table) for a ratio
/// computed from recorded counts.
QString meterHtml(double fraction, const QColor &color) {
  const auto &t = Theme::tokens();
  const QString track = Theme::mix(t.base, t.border, 0.85).name();
  const int pct = std::clamp(int(std::lround(fraction * 100)), 0, 100);
  QString cells;
  if (pct > 0)
    cells += QString("<td width='%1%' bgcolor='%2'></td>")
                 .arg(pct)
                 .arg(color.name());
  if (pct < 100)
    cells +=
        QString("<td width='%1%' bgcolor='%2'></td>").arg(100 - pct).arg(track);
  return "<table width='100%' cellspacing='0' cellpadding='0' "
         "style='font-size:5pt'><tr>" +
         cells + "</tr></table>";
}

QString signedString(quint64 v, unsigned xlen) {
  const quint64 signBit = quint64(1) << (xlen - 1);
  const quint64 m = xlen >= 64 ? ~quint64(0) : (signBit << 1) - 1;
  v &= m;
  return QString::number(static_cast<qint64>((v ^ signBit) - signBit));
}

/// First sentence of a fidelity note (English or Chinese), without the
/// final full stop.
QString firstSentence(const QString &text) {
  const int zh = text.indexOf(QChar(0x3002)); // "。"
  QString note = zh >= 0 ? text.left(zh) : text.section(". ", 0, 0);
  note = note.trimmed();
  if (note.endsWith('.'))
    note.chop(1);
  return note;
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

void InstructionTimelineModel::refreshTexts() {
  if (rowCount() > 0 && columnCount() > 0) {
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                     {Qt::ToolTipRole});
    emit headerDataChanged(Qt::Vertical, 0, rowCount() - 1);
  }
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
  case TimelineRole::Stage:
    return stage;
  case TimelineRole::ColorIndex:
    return stage >= 0 ? static_cast<int>(stages[stage].index) : 0;
  case TimelineRole::Held:
    return held;
  case TimelineRole::WrongPath:
    return squashed;
  case TimelineRole::End:
    return stage < 0 && cycle == inst->endCycle ? (squashed ? 2 : 1) : 0;
  case TimelineRole::CurrentCycle:
    return cycle == m_currentCycle;
  case Qt::ToolTipRole: {
    QString tip = Trace::instanceName(id) + "  " + inst->assembly + "\n";
    if (stage >= 0)
      tip +=
          (held ? tr("cycle %1: in %2 (held: stalled)") : tr("cycle %1: in %2"))
              .arg(cycle)
              .arg(stages[stage].name);
    else if (cycle == inst->endCycle)
      tip += (squashed ? tr("cycle %1: squashed") : tr("cycle %1: retired"))
                 .arg(cycle);
    else
      tip += tr("cycle %1: not in the pipeline").arg(cycle);
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
  if (role == Qt::ForegroundRole) {
    if (inst->outcome == TraceReplay::Outcome::Squashed)
      return Theme::tokens().bad;
    if (inst->outcome == TraceReplay::Outcome::InFlight)
      return Theme::tokens().muted;
    return QVariant();
  }
  if (role == Qt::ToolTipRole) {
    switch (inst->outcome) {
    case TraceReplay::Outcome::Retired:
      return tr("Retired at cycle %1").arg(inst->endCycle);
    case TraceReplay::Outcome::Squashed:
      return tr("Squashed at cycle %1 (wrong path)").arg(inst->endCycle);
    case TraceReplay::Outcome::InFlight:
      return tr("In flight at the end of the trace");
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
  // This view and its tool bar actions translate themselves.
  setProperty("osxOwnTranslation", true);
  setupToolbar();
  setupUi();
  retranslateUi();

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
  connect(Theme::Notifier::get(), &Theme::Notifier::changed, this,
          &ExplorerTab::applyTheme);
  applyTheme();
}

void ExplorerTab::applyTheme() {
  const auto &t = Theme::tokens();
  using Theme::Glyph;
  for (auto [button, glyph] : m_controlButtons) {
    const bool play = button == m_playButton;
    const QColor color = play && Theme::isGame() ? t.accentText : t.text;
    button->setIcon(Theme::glyphIcon(
        play && button->isChecked() ? Glyph::Pause : static_cast<Glyph>(glyph),
        color, play ? 22 : 18));
  }
  m_followButton->setIcon(Theme::glyphIcon(Glyph::Follow, t.ok, 16));
  const QString css =
      QString("h4 { color: %1; } h3 { color: %2; }")
          .arg((Theme::isGame() ? t.accent : t.text).name(), t.text.name());
  for (auto *browser : {m_explanation, m_statistics, m_componentDetail})
    browser->document()->setDefaultStyleSheet(css);
  m_strip->update();
  m_legend->update();
  m_timeline->viewport()->update();
  m_timeline->verticalHeader()->viewport()->update();
  updateBanner();
  updateStatistics();
  updateExplanation();
  updatePipelineStrip();
}

ExplorerTab::~ExplorerTab() = default;

void ExplorerTab::retranslateUi() {
  m_recordAction->setText(tr("Record execution trace"));
  m_maxCyclesLabel->setText(" " + tr("Max cycles:") + " ");
  m_maxCycles->setToolTip(
      tr("Recording stops (and the trace is marked as truncated) after this "
         "many cycles. Reset the processor to apply a larger limit to a run "
         "that was already truncated."));
  m_exportAction->setText(tr("Export trace (JSON Lines)..."));
  m_openAction->setText(tr("Open a trace file for offline replay..."));
  m_exampleAction->setText(tr("Open the example gem5 trace"));
  m_exampleAction->setToolTip(
      tr("Replay a recorded gem5 O3CPU run with L1/L2 caches (no file "
         "needed)"));
  m_liveAction->setText(tr("Return to the live trace"));

  m_firstButton->setToolTip(tr("First cycle"));
  m_previousButton->setToolTip(tr("Previous cycle"));
  m_playButton->setToolTip(tr("Play back the recorded trace"));
  m_nextButton->setToolTip(tr("Next cycle"));
  m_lastButton->setToolTip(tr("Last recorded cycle"));
  m_slider->setToolTip(tr("Recorded cycles: drag to replay any cycle"));
  m_cycleLabel->setToolTip(tr("Selected cycle / last recorded cycle"));
  for (int i = 0; i < m_speed->count(); ++i) {
    const int cps = 1000 / m_speed->itemData(i).toInt();
    m_speed->setItemText(i, cps == 1 ? tr("1 cycle/s")
                                     : tr("%1 cycles/s").arg(cps));
  }
  m_speed->setToolTip(tr("Playback speed"));
  m_followButton->setText(tr("Follow simulator"));
  m_followButton->setToolTip(
      tr("Jump to the newest recorded cycle whenever the simulator advances"));

  m_stripGroup->setTitle(tr("Pipeline at the selected cycle"));
  m_timelineGroup->setTitle(
      tr("Instruction timeline (rows: dynamic instructions, columns: cycles; "
         "* = held, ✓ = retired, ✕ = squashed)"));
  m_eventsGroup->setTitle(tr("Events at the selected cycle"));
  m_events->setHorizontalHeaderLabels({tr("Seq"), tr("Event"), tr("Component"),
                                       tr("Instruction"), tr("Details"),
                                       tr("Observability")});
  m_registers->setHorizontalHeaderLabels(
      {tr("Register"), tr("Alias"), tr("Value"), tr("Signed")});
  m_componentTree->setHeaderLabels({tr("Component"), tr("Role")});
  m_sideTabs->setTabText(0, tr("Instruction"));
  m_sideTabs->setTabText(1, tr("Registers"));
  m_sideTabs->setTabText(2, tr("CPU components"));
  m_sideTabs->setTabText(3, tr("Statistics"));
  m_sideTabs->setTabText(4, tr("System"));
  updateCycleLabel();
}

void ExplorerTab::changeEvent(QEvent *event) {
  RipesTab::changeEvent(event);
  if (event->type() != QEvent::LanguageChange)
    return;
  // Static texts, then everything rendered from the trace and the model.
  retranslateUi();
  updateBanner();
  updateStatistics();
  updateCycleViews();
  updateExplanation();
  m_strip->update();
  m_legend->updateGeometry();
  m_legend->update();
  m_timelineModel->refreshTexts();
  std::function<void(QTreeWidgetItem *)> retitle = [&](QTreeWidgetItem *it) {
    const auto info = componentInfo(it->text(0));
    it->setText(1, info ? info->title : QString());
    for (int c = 0; c < it->childCount(); ++c)
      retitle(it->child(c));
  };
  for (int i = 0; i < m_componentTree->topLevelItemCount(); ++i)
    retitle(m_componentTree->topLevelItem(i));
  if (!m_componentTree->selectedItems().isEmpty())
    componentSelected();
  else if (m_componentDesign)
    rebuildComponentTree();
}

void ExplorerTab::updateCycleLabel() {
  if (!m_replay) {
    m_cycleLabel->clear();
    return;
  }
  m_cycleLabel->setText(
      tr("CYCLE %1 / %2").arg(m_cycle).arg(m_replay->lastCycle()));
}

bool ExplorerTab::isDark() const { return appInDarkMode(); }

void ExplorerTab::setupToolbar() {
  m_recordAction = new QAction(QIcon(":/icons/trace.svg"), QString(), this);
  m_recordAction->setCheckable(true);
  m_recordAction->setChecked(m_recorder->isEnabled());
  connect(m_recordAction, &QAction::toggled, this, [this](bool enabled) {
    m_recorder->setEnabled(enabled);
    RipesSettings::setValue(RIPES_SETTING_OSX_TRACE_ENABLED, enabled);
    m_dirty = true;
    refreshFromRecorder();
  });
  m_toolbar->addAction(m_recordAction);

  m_maxCyclesLabel = new QLabel(this);
  m_toolbar->addWidget(m_maxCyclesLabel);
  m_maxCycles = new QSpinBox(this);
  m_maxCycles->setRange(100, 10000000);
  m_maxCycles->setSingleStep(1000);
  m_maxCycles->setValue(static_cast<int>(m_recorder->maxCycles()));
  connect(m_maxCycles, &QSpinBox::valueChanged, this, [this](int value) {
    m_recorder->setMaxCycles(value);
    RipesSettings::setValue(RIPES_SETTING_OSX_TRACE_MAXCYCLES, value);
  });
  m_toolbar->addWidget(m_maxCycles);
  m_toolbar->addSeparator();

  m_exportAction = new QAction(QIcon(":/icons/saveas.svg"), QString(), this);
  connect(m_exportAction, &QAction::triggered, this, &ExplorerTab::exportTrace);
  m_toolbar->addAction(m_exportAction);

  m_openAction = new QAction(QIcon(":/icons/loadfile.svg"), QString(), this);
  connect(m_openAction, &QAction::triggered, this, &ExplorerTab::openTrace);
  m_toolbar->addAction(m_openAction);

  m_exampleAction = new QAction(QIcon(":/icons/server.svg"), QString(), this);
  m_exampleAction->setEnabled(!exampleGem5TracePath().isEmpty());
  connect(m_exampleAction, &QAction::triggered, this,
          &ExplorerTab::openExampleGem5Trace);
  m_toolbar->addAction(m_exampleAction);

  m_liveAction = new QAction(QIcon(":/icons/reset.svg"), QString(), this);
  m_liveAction->setEnabled(false);
  connect(m_liveAction, &QAction::triggered, this, &ExplorerTab::returnToLive);
  m_toolbar->addAction(m_liveAction);
}

void ExplorerTab::setupUi() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(8, 6, 8, 6);
  root->setSpacing(6);

  m_banner = new HudBanner(this);
  m_banner->setObjectName("explorerBanner");
  root->addWidget(m_banner);

  // Playback controls ("controller bar"). They replay the recording; they
  // never drive the simulator.
  auto *controls = new QFrame(this);
  controls->setObjectName("explorerControls");
  auto *bar = new QHBoxLayout(controls);
  bar->setContentsMargins(8, 4, 8, 4);
  bar->setSpacing(6);
  using Theme::Glyph;
  auto makeButton = [&](Glyph glyph) {
    auto *b = new QToolButton(controls);
    b->setObjectName("explorerControl");
    b->setIconSize(QSize(18, 18));
    b->setAutoRaise(true);
    bar->addWidget(b);
    m_controlButtons.emplace_back(b, static_cast<int>(glyph));
    return b;
  };
  auto *first = m_firstButton = makeButton(Glyph::First);
  auto *prev = m_previousButton = makeButton(Glyph::Previous);
  m_playButton = makeButton(Glyph::Play);
  m_playButton->setObjectName("explorerPlay");
  m_playButton->setIconSize(QSize(22, 22));
  m_playButton->setCheckable(true);
  auto *next = m_nextButton = makeButton(Glyph::Next);
  auto *last = m_lastButton = makeButton(Glyph::Last);
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
    applyTheme(); // play/pause icon
    if (play) {
      m_followButton->setChecked(false);
      if (m_replay && m_cycle >= m_replay->lastCycle())
        setCycle(m_replay->firstCycle());
      m_playTimer.start(m_speed->currentData().toInt());
    } else {
      m_playTimer.stop();
    }
  });

  bar->addSpacing(6);
  m_slider = new QSlider(Qt::Horizontal, controls);
  connect(m_slider, &QSlider::valueChanged, this,
          [this](int v) { setCycle(v); });
  bar->addWidget(m_slider, 1);
  m_cycleLabel = new QLabel(controls);
  m_cycleLabel->setObjectName("explorerCycle");
  m_cycleLabel->setFont(Theme::monoFont(13, true));
  m_cycleLabel->setAlignment(Qt::AlignCenter);
  m_cycleLabel->setMinimumWidth(150);
  bar->addWidget(m_cycleLabel);

  m_speed = new QComboBox(controls);
  for (int cps : {1, 2, 5, 10, 25})
    m_speed->addItem(QString(), 1000 / cps);
  m_speed->setCurrentIndex(1);
  connect(m_speed, &QComboBox::currentIndexChanged, this,
          [this] { m_playTimer.setInterval(m_speed->currentData().toInt()); });
  bar->addWidget(m_speed);

  m_followButton = new QToolButton(controls);
  m_followButton->setObjectName("explorerFollow");
  m_followButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_followButton->setCheckable(true);
  m_followButton->setChecked(true);
  connect(m_followButton, &QToolButton::toggled, this, [this](bool on) {
    if (on && m_replay)
      setCycle(m_replay->lastCycle());
  });
  bar->addWidget(m_followButton);
  root->addWidget(controls);

  // Main area
  auto *mainSplitter = new QSplitter(Qt::Horizontal, this);
  auto *left = new QSplitter(Qt::Vertical, mainSplitter);

  auto *stripGroup = m_stripGroup = new QGroupBox(left);
  auto *stripGroupLayout = new QVBoxLayout(stripGroup);
  stripGroupLayout->setSpacing(4);
  m_strip = new PipelineStrip(stripGroup);
  m_strip->setObjectName("explorerPipeline");
  connect(m_strip, &PipelineStrip::instanceClicked, this,
          [this](qint64 id) { selectInstance(id); });
  stripGroupLayout->addWidget(m_strip);
  m_legend = new StripLegend(stripGroup);
  stripGroupLayout->addWidget(m_legend);

  auto *timelineGroup = m_timelineGroup = new QGroupBox(left);
  auto *timelineLayout = new QVBoxLayout(timelineGroup);
  m_timeline = new QTableView(timelineGroup);
  // Small minimum heights (the splitter can enlarge the tables), so that the
  // tab fits windows of 720 pixels, e.g. a browser window on a laptop.
  m_timeline->setMinimumHeight(40);
  m_timeline->setObjectName("explorerTimeline");
  m_timelineModel = new InstructionTimelineModel(this);
  m_timeline->setModel(m_timelineModel);
  m_timeline->viewport()->installEventFilter(this);
  m_timeline->setItemDelegate(new TimelineDelegate(m_timeline));
  m_timeline->setShowGrid(false);
  m_timeline->horizontalHeader()->setDefaultSectionSize(44);
  m_timeline->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  m_timeline->verticalHeader()->setDefaultSectionSize(24);
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

  auto *eventsGroup = m_eventsGroup = new QGroupBox(left);
  auto *eventsLayout = new QVBoxLayout(eventsGroup);
  m_events = new QTableWidget(eventsGroup);
  m_events->setMinimumHeight(40);
  m_events->setObjectName("explorerEvents");
  m_events->setColumnCount(6);
  m_events->horizontalHeader()->setStretchLastSection(false);
  m_events->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  m_events->verticalHeader()->setVisible(false);
  m_events->verticalHeader()->setDefaultSectionSize(26);
  m_events->setShowGrid(false);
  m_events->setAlternatingRowColors(true);
  m_events->setItemDelegateForColumn(5, new ChipDelegate(m_events));
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
  m_sideTabs->addTab(m_explanation, QString());

  m_registers = new QTableWidget(m_sideTabs);
  m_registers->setObjectName("explorerRegisters");
  m_registers->setColumnCount(4);
  m_registers->verticalHeader()->setVisible(false);
  m_registers->verticalHeader()->setDefaultSectionSize(24);
  m_registers->setShowGrid(false);
  m_registers->setAlternatingRowColors(true);
  m_registers->horizontalHeader()->setStretchLastSection(true);
  m_registers->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_sideTabs->addTab(m_registers, QString());

  auto *componentSplitter = new QSplitter(Qt::Vertical, m_sideTabs);
  m_componentTree = new QTreeWidget(componentSplitter);
  m_componentTree->setObjectName("explorerComponents");
  m_componentTree->setColumnCount(2);
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
  m_sideTabs->addTab(componentSplitter, QString());
  connect(m_sideTabs, &QTabWidget::currentChanged, this, [this] {
    if (m_sideTabs->currentIndex() == 2)
      refreshComponents();
  });

  m_statistics = new QTextBrowser(m_sideTabs);
  m_statistics->setObjectName("explorerStatistics");
  m_sideTabs->addTab(m_statistics, QString());

  m_systemView = new SystemView(m_sideTabs);
  m_sideTabs->addTab(m_systemView, QString());

  m_sideTabs->setDocumentMode(false);
  m_sideTabs->setUsesScrollButtons(true);
  mainSplitter->setStretchFactor(0, 3);
  mainSplitter->setStretchFactor(1, 2);
  mainSplitter->setSizes({1000, 640});
  root->addWidget(mainSplitter, 1);

  updateExplanation();
}

bool ExplorerTab::eventFilter(QObject *watched, QEvent *event) {
  // Keep the selected cycle in view when the timeline is resized.
  if (watched == m_timeline->viewport() && event->type() == QEvent::Resize &&
      m_replay && m_timelineModel->rowCount() > 0)
    QMetaObject::invokeMethod(
        this,
        [this] {
          if (!m_replay || m_timelineModel->rowCount() == 0)
            return;
          const int row =
              std::max(0, m_timelineModel->rowForInstance(m_selected));
          m_timeline->scrollTo(
              m_timelineModel->index(row,
                                     m_timelineModel->columnForCycle(m_cycle)),
              QAbstractItemView::EnsureVisible);
        },
        Qt::QueuedConnection);
  return RipesTab::eventFilter(watched, event);
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
  const qint64 target = follow ? m_replay->lastCycle() : m_cycle;
  // Following a simulator that advanced by one cycle shows the transition.
  m_cycle = std::clamp(m_cycle, m_replay->firstCycle(), m_replay->lastCycle());
  setCycle(target);
  updateCycleViews();
  updateExplanation();
}

void ExplorerTab::setCycle(qint64 cycle) {
  if (!m_replay)
    return;
  cycle = std::clamp(cycle, m_replay->firstCycle(), m_replay->lastCycle());
  const bool changed = cycle != m_cycle;
  if (changed)
    m_previousCycle = m_cycle;
  m_cycle = cycle;
  m_slider->blockSignals(true);
  m_slider->setValue(static_cast<int>(cycle));
  m_slider->blockSignals(false);
  updateCycleLabel();
  m_timelineModel->setCurrentCycle(cycle);
  const int col = m_timelineModel->columnForCycle(cycle);
  const int row = std::max(0, m_timelineModel->rowForInstance(m_selected));
  if (m_timelineModel->rowCount() > 0)
    m_timeline->scrollTo(m_timelineModel->index(row, col),
                         QAbstractItemView::EnsureVisible);
  if (changed) {
    updateCycleViews();
    m_previousCycle = -1;
    m_sinceCycleChange.start();
  }
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
  std::vector<std::pair<unsigned, QString>> lanesAndNames;
  std::vector<std::pair<int, QString>> legend;
  for (const auto &sd : stages) {
    lanesAndNames.emplace_back(sd.lane, sd.name);
    if (sd.lane == stages.front().lane)
      legend.emplace_back(static_cast<int>(sd.index), sd.name);
  }
  if (m_strip->setStages(lanesAndNames))
    m_legend->setStages(legend);

  // Everything shown comes from the replayed trace at the selected cycle.
  auto visualOf = [&](const TraceReplay::Slot &slot, size_t i) {
    SlotVisual v;
    v.kind = slot.kind;
    v.stage = stages[i].name;
    v.colorIndex = static_cast<int>(stages[i].index);
    v.held = slot.held;
    v.occupants = slot.occupants;
    if (slot.kind == TraceReplay::SlotKind::Instruction) {
      const auto *inst = m_replay->instance(slot.instance);
      v.instance = slot.instance;
      v.assembly = inst ? inst->assembly : Trace::instanceName(slot.instance);
      v.wrongPath = inst && inst->outcome == TraceReplay::Outcome::Squashed;
      v.selected = slot.instance == m_selected;
    }
    return v;
  };

  const auto &tiles = m_strip->tiles();
  const auto pipeline = m_replay->pipelineAt(m_cycle);
  for (size_t i = 0;
       i < stages.size() && i < pipeline.size() && i < tiles.size(); ++i) {
    const auto &slot = pipeline[i];
    const SlotVisual v = visualOf(slot, i);
    auto *b = tiles[i];
    QString body;
    switch (slot.kind) {
    case TraceReplay::SlotKind::Instruction:
      body = v.assembly;
      if (slot.held)
        body += "  " + tr("(held)");
      if (slot.occupants > 1)
        body += "\n" + tr("+%1 more").arg(slot.occupants - 1);
      break;
    case TraceReplay::SlotKind::StallBubble:
      body = tr("stall bubble");
      break;
    case TraceReplay::SlotKind::FlushBubble:
      body = tr("flushed (bubble)");
      break;
    case TraceReplay::SlotKind::Empty:
      body = "—";
      break;
    }
    b->setText(stages[i].name + "\n" + body);
    b->setProperty("instance", slot.kind == TraceReplay::SlotKind::Instruction
                                   ? slot.instance
                                   : qint64(-1));
    b->setVisual(v);
    b->setToolTip(slot.kind == TraceReplay::SlotKind::Instruction
                      ? Trace::instanceName(slot.instance) + "  " + v.assembly +
                            (v.wrongPath
                                 ? "\n" + tr("wrong path: squashed later")
                                 : QString()) +
                            "\n" + tr("click to explain this instruction")
                      : QString());
  }

  // Transition between two consecutive recorded cycles. Moves are those of
  // the trace: an instance's stage in the previous and the selected cycle,
  // fetches (not in the pipeline before) and retirements/squashes recorded
  // for the selected cycle. Skipped while the simulator runs, when cycles
  // change faster than the transition, or with reduced motion.
  if (m_previousCycle < 0)
    return;
  const qint64 from = m_previousCycle;
  const bool animate =
      Theme::animationsEnabled() && !ProcessorHandler::isRunning() &&
      std::abs(m_cycle - from) == 1 && m_strip->isVisible() &&
      (!m_sinceCycleChange.isValid() ||
       m_sinceCycleChange.elapsed() > Theme::kTransitionMs + 60) &&
      from >= m_replay->firstCycle() && from <= m_replay->lastCycle();
  if (!animate) {
    m_strip->stopAnimation();
    return;
  }
  std::vector<PipelineStrip::Move> moves;
  for (size_t i = 0; i < pipeline.size() && i < tiles.size(); ++i) {
    const auto &slot = pipeline[i];
    if (slot.kind != TraceReplay::SlotKind::Instruction)
      continue;
    const int src = m_replay->stageOf(slot.instance, from);
    PipelineStrip::Move m;
    m.visual = tiles[i]->visual();
    m.to = static_cast<int>(i);
    if (src >= 0 && src != static_cast<int>(i)) {
      m.kind = PipelineStrip::Move::Slide;
      m.from = src;
      moves.push_back(m);
    } else if (src < 0) {
      m.kind = PipelineStrip::Move::Enter;
      moves.push_back(m);
    }
  }
  if (m_cycle == from + 1) {
    const auto before = m_replay->pipelineAt(from);
    for (size_t i = 0; i < before.size() && i < tiles.size(); ++i) {
      const auto &slot = before[i];
      if (slot.kind != TraceReplay::SlotKind::Instruction)
        continue;
      const auto *inst = m_replay->instance(slot.instance);
      if (!inst || inst->endCycle != m_cycle ||
          inst->outcome == TraceReplay::Outcome::InFlight ||
          m_replay->stageOf(slot.instance, m_cycle) >= 0)
        continue;
      PipelineStrip::Move m;
      m.kind = PipelineStrip::Move::Exit;
      m.from = static_cast<int>(i);
      m.visual = visualOf(slot, i);
      m.retired = inst->outcome == TraceReplay::Outcome::Retired;
      moves.push_back(m);
    }
  }
  m_strip->animate(std::move(moves));
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
  const QColor changedColor = Theme::withAlpha(Theme::tokens().warn, 90);
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
          known ? Trace::toHex(v, h.xlen) : tr("unknown"),
          known ? signedString(v, h.xlen) : QString("-")};
      for (int c = 0; c < cells.size(); ++c) {
        auto *item = new QTableWidgetItem(cells[c]);
        if (changed) {
          item->setBackground(changedColor);
          QFont f = item->font();
          f.setBold(true);
          item->setFont(f);
          item->setToolTip(tr("Written in cycle %1 (was %2)")
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
        "<p>" +
        tr("Select an instruction in the pipeline view or the timeline to "
           "see what it does, its operand and result values as recorded by "
           "the simulator, its hazards and its journey through the "
           "pipeline.") +
        "</p>");
    return;
  }
  m_explanation->setHtml(
      explanationToHtml(explainInstance(*m_replay, m_selected), isDark()));
}

void ExplorerTab::updateBanner() {
  const auto &tk = Theme::tokens();
  if (!m_replay) {
    m_banner->setText(tr("No trace recorded yet."));
    m_banner->setBadges(
        {{tr("Trace"), tr("No trace recorded yet"), tk.muted,
          tr("Load a program and clock the processor to record a trace.")}},
        QString());
    return;
  }
  const auto &t = m_replay->trace();
  const auto &s = t.header.source;
  QString mode;
  if (m_offline)
    mode = tr("Offline replay of %1")
               .arg("<i>" + offlineName().toHtmlEscaped() + "</i>");
  else if (!m_recorder->isEnabled())
    mode = tr("Recording disabled");
  else
    mode = tr("Live trace");
  QString completeness;
  const auto &c = t.completeness;
  if (c.truncated)
    completeness = tr("truncated (%1)").arg(c.truncationReason);
  else if (c.programFinished)
    completeness =
        c.startsAtReset ? tr("complete") : tr("finished, started mid-run");
  else
    completeness = tr("in progress");
  m_banner->setText(
      tr("<b>Engine:</b> %1 %2 &nbsp;·&nbsp; <b>Model:</b> %3 (%4) "
         "&nbsp;·&nbsp; <b>Fidelity:</b> %5 &nbsp;·&nbsp; <b>Clock "
         "domain:</b> %6 &nbsp;·&nbsp; %7 (%8)")
          .arg(s.engine.toHtmlEscaped(), s.engineVersion.toHtmlEscaped(),
               s.modelName.toHtmlEscaped(), s.model.toHtmlEscaped(),
               s.fidelity.toHtmlEscaped(), t.header.clockDomain.toHtmlEscaped(),
               mode, completeness));
  if (t.header.ticksPerCycle > 0)
    m_banner->setText(m_banner->text() + " &nbsp;·&nbsp; " +
                      tr("1 cycle = %1 %2 (engine ticks)")
                          .arg(t.header.ticksPerCycle)
                          .arg(t.header.tickUnit.toHtmlEscaped()));
  if (t.header.stages.empty())
    m_banner->setText(m_banner->text() + " &nbsp;·&nbsp; <i>" +
                      tr("no pipeline stage information in this trace") +
                      "</i>");
  const QString fidelityNote = localizedTraceText(s.fidelityNote);
  m_banner->setToolTip(fidelityNote);

  // The same facts as badges.
  QString shortMode = m_offline                 ? tr("Offline replay")
                      : m_recorder->isEnabled() ? tr("Live trace")
                                                : tr("Recording disabled");
  QColor statusColor = tk.info;
  if (c.truncated)
    statusColor = tk.bad;
  else if (c.programFinished)
    statusColor = c.startsAtReset ? tk.ok : tk.warn;
  QString traceTip =
      m_offline ? tr("Offline replay of %1").arg(offlineName()) : shortMode;
  traceTip += "\n" + tr("%1 events, cycles %2 to %3")
                         .arg(t.events.size())
                         .arg(m_replay->firstCycle())
                         .arg(m_replay->lastCycle());
  std::vector<HudBanner::Badge> badges = {
      {tr("Engine"), (s.engine + " " + s.engineVersion).trimmed(), tk.accent,
       tr("Simulation engine that produced this trace (adapter %1)")
           .arg((s.adapter + " " + s.adapterVersion).trimmed())},
      {tr("Model"), QString("%1 (%2)").arg(s.modelName, s.model), tk.accent2,
       tr("Model implementation: %1").arg(s.sourceFile)},
      {tr("Fidelity"), s.fidelity, Theme::fidelityColor(s.fidelity),
       fidelityNote},
      {tr("Clock domain"), t.header.clockDomain, tk.info,
       t.header.clockDomainDescription.isEmpty()
           ? tr("Cycles of the model's clock domain")
           : localizedTraceText(t.header.clockDomainDescription)}};
  if (t.header.ticksPerCycle > 0)
    badges.push_back({tr("Time base"),
                      tr("1 cycle = %1 %2")
                          .arg(t.header.ticksPerCycle)
                          .arg(t.header.tickUnit),
                      tk.info,
                      tr("Engine ticks per cycle of the clock domain")});
  badges.push_back(
      {tr("Trace"), shortMode + " · " + completeness, statusColor, traceTip});
  if (t.header.stages.empty())
    badges.push_back({tr("Pipeline"), tr("no stage information"), tk.muted,
                      tr("This trace has no pipeline stage information")});
  // First sentence of the fidelity note (the full note is in the tooltips).
  QString note = firstSentence(fidelityNote);
  m_banner->setBadges(badges, note.isEmpty() ? QString() : "ⓘ " + note);
}

void ExplorerTab::updateStatistics() {
  if (!m_replay)
    return;
  const auto st = m_replay->stats();
  const auto &t = m_replay->trace();
  const auto &tk = Theme::tokens();
  const qint64 fetched = static_cast<qint64>(m_replay->instances().size());

  // Headline numbers as tiles.
  auto tile = [&](const QString &value, const QString &label,
                  const QColor &color) {
    return QString("<td align='center' bgcolor='%1' width='33%'>"
                   "<span style='font-size:18pt; font-weight:700; "
                   "color:%2'>%3</span><br><span style='color:%4; "
                   "font-size:8pt; font-weight:600'>%5</span></td>")
        .arg(Theme::mix(tk.card, color, tk.dark ? 0.1 : 0.06).name(),
             color.name(), value, tk.muted.name(), label);
  };
  QString html = "<h3>" + tr("Statistics from the recorded trace") + "</h3>";
  html += "<table width='100%' cellspacing='6' cellpadding='8'><tr>" +
          tile(QString::number(st.cycles), tr("CYCLES"), tk.accent) +
          tile(QString::number(st.retired), tr("RETIRED"), tk.ok) +
          tile(st.retired ? QString::number(st.cpi, 'f', 2) : "-", tr("CPI"),
               tk.accent2) +
          "</tr></table>";

  // Counts; ratios of two recorded counts also get a meter.
  html += "<table width='100%' cellpadding='3' cellspacing='0'>";
  auto row = [&](const QString &k, const QString &v, double num = -1,
                 double den = 0, const QColor &color = QColor(),
                 const QString &of = QString()) {
    html += "<tr><td>" + k + "</td><td align='right'><b>" + v + "</b></td>";
    if (num >= 0 && den > 0)
      html += "<td width='34%'>" + meterHtml(num / den, color) +
              "</td><td><small style='color:" + tk.muted.name() + "'>" +
              tr("%1 % of %2").arg(100.0 * num / den, 0, 'f', 0).arg(of) +
              "</small></td>";
    else
      html += "<td></td><td></td>";
    html += "</tr>";
  };
  row(tr("Cycles"), QString::number(st.cycles));
  row(tr("Instructions retired"), QString::number(st.retired),
      double(st.retired), double(fetched), tk.ok, tr("fetched"));
  row(tr("CPI"), st.retired ? QString::number(st.cpi, 'f', 3) : "-");
  row(tr("Stall cycles"), QString::number(st.stallCycles),
      double(st.stallCycles), double(st.cycles), tk.warn, tr("cycles"));
  row(tr("Stall bubbles inserted"), QString::number(st.stallBubbles));
  row(tr("Squashed instructions"), QString::number(st.squashed),
      double(st.squashed), double(fetched), tk.bad, tr("fetched"));
  row(tr("Conditional branches (taken)"),
      QString("%1 (%2)").arg(st.retiredBranches).arg(st.retiredTakenBranches),
      double(st.retiredTakenBranches), double(st.retiredBranches), tk.accent2,
      tr("taken"));
  row(tr("Register writes"), QString::number(st.registerWrites));
  row(tr("Data memory reads / writes"),
      QString("%1 / %2").arg(st.memoryReads).arg(st.memoryWrites));
  row(tr("Events"), QString::number(t.events.size()));
  html += "</table>";
  html += QString("<p style='color:%1'><small>%2</small></p>")
              .arg(tk.muted.name(),
                   tr("Meters show the ratio of two counts of this trace "
                      "(fetched = dynamic instructions recorded)."));
  auto yesNo = [&](bool good, const QString &text) {
    return QString("<span style='color:%1'>%2</span> %3")
        .arg((good ? tk.ok : tk.bad).name(), good ? "✓" : "✗", text);
  };
  const QString yes = tr("yes"), no = tr("no");
  html += "<h4>" + tr("Trace completeness") + "</h4><ul>";
  html += "<li>" +
          yesNo(t.completeness.startsAtReset,
                tr("Starts at reset: %1")
                    .arg(t.completeness.startsAtReset ? yes : no)) +
          "</li>";
  html +=
      "<li>" +
      yesNo(!t.completeness.truncated,
            tr("Truncated: %1")
                .arg(t.completeness.truncated
                         ? tr("yes (%1)").arg(t.completeness.truncationReason)
                         : no)) +
      "</li>";
  html += "<li>" +
          yesNo(t.completeness.programFinished,
                tr("Program finished: %1")
                    .arg(t.completeness.programFinished ? yes : no)) +
          "</li>";
  html += "</ul><h4>" + tr("Caveats") + "</h4><ul>";
  for (const auto &cav : t.completeness.caveats)
    html += "<li>" + localizedTraceText(cav).toHtmlEscaped() + "</li>";
  html += "</ul><h4>" + tr("Source") + "</h4><p>" +
          localizedTraceText(t.header.source.fidelityNote).toHtmlEscaped() +
          "</p><p>" +
          tr("Model implementation: %1")
              .arg("<code>" + t.header.source.sourceFile.toHtmlEscaped() +
                   "</code>") +
          "</p>";
  m_statistics->setHtml(html);
}

void ExplorerTab::exportTrace() {
  const auto trace =
      m_offline && m_replay ? m_replay->trace() : m_recorder->trace();
#ifdef Q_OS_WASM
  // The browser has no file system to write to: the trace is downloaded.
  QFileDialog::saveFileContent(Trace::toJsonl(trace), "trace.jsonl", this);
#else
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export execution trace"), "trace.jsonl",
      tr("JSON Lines trace (*.jsonl);;All files (*)"));
  if (path.isEmpty())
    return;
  QString error;
  if (!Trace::writeJsonlFile(trace, path, &error))
    QMessageBox::warning(this, tr("Export trace"), error);
#endif
}

void ExplorerTab::openTrace() {
  const QString filter = tr("JSON Lines trace (*.jsonl);;All files (*)");
#ifdef Q_OS_WASM
  // The browser's file chooser: the file's contents are handed over, there
  // is no path to open.
  QPointer<ExplorerTab> self(this);
  QFileDialog::getOpenFileContent(
      filter,
      [self](const QString &name, const QByteArray &content) {
        if (!self || name.isEmpty())
          return;
        QString error;
        if (!self->openTraceData(content, name, &error))
          QMessageBox::warning(self, tr("Open trace"), error);
      },
      this);
#else
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open execution trace"), QString(), filter);
  if (path.isEmpty())
    return;
  QString error;
  if (!openTraceFile(path, &error))
    QMessageBox::warning(this, tr("Open trace"), error);
#endif
}

QString ExplorerTab::exampleGem5TracePath() {
  const QString path(":/osx-examples/gem5/loop_sum_o3_l1l2.jsonl");
  return QFile::exists(path) ? path : QString();
}

bool ExplorerTab::openExampleGem5Trace() {
  const QString path = exampleGem5TracePath();
  QString error = tr("This build has no example gem5 trace.");
  std::optional<Trace::Trace> trace;
  if (!path.isEmpty())
    trace = Trace::readJsonlFile(path, &error);
  if (!trace.has_value()) {
    QMessageBox::warning(this, tr("Open trace"), error);
    return false;
  }
  loadOffline(std::move(*trace), QString(), true);
  return true;
}

bool ExplorerTab::openTraceFile(const QString &path, QString *error) {
  auto trace = Trace::readJsonlFile(path, error);
  if (!trace.has_value())
    return false;
  loadOffline(std::move(*trace), path, false);
  return true;
}

bool ExplorerTab::openTraceData(const QByteArray &data, const QString &name,
                                QString *error) {
  auto trace = Trace::fromJsonl(data, error);
  if (!trace.has_value()) {
    if (error)
      *error = tr("Could not read '%1': %2").arg(name, *error);
    return false;
  }
  loadOffline(std::move(*trace), name, false);
  return true;
}

void ExplorerTab::loadOffline(Trace::Trace trace, const QString &name,
                              bool example) {
  m_offline = true;
  m_offlineFile = name;
  m_offlineExample = example;
  m_liveAction->setEnabled(true);
  m_followButton->setChecked(false);
  m_followButton->setEnabled(false);
  m_selected = -1;
  loadReplay(std::move(trace));
}

QString ExplorerTab::offlineName() const {
  return m_offlineExample ? tr("the example gem5 O3CPU trace (loop_sum)")
                          : m_offlineFile;
}

void ExplorerTab::returnToLive() {
  m_offline = false;
  m_offlineFile.clear();
  m_offlineExample = false;
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
        "<p>" + tr("The selected processor is not a VSRTL datapath model.") +
        "</p>");
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
      "<p>" +
      tr("Select a datapath component to see its purpose, its live input "
         "and output signals and the instruction it is currently working "
         "on.") +
      "</p>");
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
    html += "<p><b>" + tr("Purpose:") + "</b> " +
            info->purpose.toHtmlEscaped() + "</p>";
    html += "<p>" + info->behaviour.toHtmlEscaped() + "</p>";
  } else {
    html += "<p>" +
            tr("No educational description for this component yet. Its live "
               "signals are shown below.") +
            "</p>";
  }

  // Instruction currently associated with the component's stage.
  if (info && !info->stage.isEmpty() && proc) {
    for (auto idx : proc->structure().stageIt()) {
      if (proc->stageName(idx) != info->stage || idx.lane() != 0)
        continue;
      const auto si = proc->stageInfo(idx);
      html += "<p><b>" +
              tr("Instruction in %1 (cycle %2):")
                  .arg(info->stage)
                  .arg(proc->getCycleCount()) +
              "</b> ";
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
        html += "<i>" + tr("none (bubble or empty)") + "</i>";
      }
      html += "</p>";
    }
  }

  auto portTable = [&](const std::vector<vsrtl::SimPort *> &ports,
                       bool inputs) {
    if (ports.empty())
      return;
    html += QString("<h4>%1 <span style='font-weight:normal'>(%2)</span></h4>")
                .arg(inputs ? tr("Inputs") : tr("Outputs"),
                     tr("live simulator values"));
    html += "<table cellspacing='0' cellpadding='2'><tr><th align='left'>" +
            tr("Port") + "</th><th align='right'>" + tr("Bits") +
            "</th><th align='left'>" + tr("Value") + "</th><th align='left'>" +
            (inputs ? tr("Driven by") : tr("Drives")) + "</th></tr>";
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
    html += "<h4>" + tr("Implemented in") + "</h4><p><code>" +
            info->sourceFile.toHtmlEscaped() + "</code></p>";
    if (!info->related.isEmpty())
      html += "<p><b>" + tr("Related components:") + "</b> " +
              info->related.join(", ").toHtmlEscaped() + "</p>";
  }
  html += "<p style='color:gray'>" +
          tr("Signal values are read directly from the VSRTL model of the "
             "selected educational processor at the current simulator cycle; "
             "they are not recorded in the trace.") +
          "</p>";
  m_componentDetail->setHtml(html);
}

} // namespace Ripes
