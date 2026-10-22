// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "missionstab.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

#include "explorer/tracerecorder.h"
#include "processorhandler.h"
#include "ripessettings.h"

namespace Ripes {

using namespace Explorer;

namespace {
QLabel *richLabel(QWidget *parent) {
  auto *l = new QLabel(parent);
  l->setWordWrap(true);
  l->setTextFormat(Qt::RichText);
  l->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return l;
}

QString okColor() { return appInDarkMode() ? "#7bd88f" : "#1b7f3a"; }
QString badColor() { return appInDarkMode() ? "#ff8a80" : "#b3261e"; }
} // namespace

MissionsTab::MissionsTab(QToolBar *toolbar, TraceRecorder *recorder,
                         std::function<QString()> sourceProvider,
                         QWidget *parent)
    : RipesTab(toolbar, parent), m_recorder(recorder),
      m_sourceProvider(std::move(sourceProvider)) {
  QString error;
  m_missions = loadBuiltinMissions(&error);

  auto *root = new QHBoxLayout(this);
  auto *splitter = new QSplitter(Qt::Horizontal, this);
  root->addWidget(splitter);

  m_list = new QListWidget(splitter);
  m_list->setObjectName("missionList");
  m_list->setMinimumWidth(280);
  m_list->setWordWrap(true);
  connect(m_list, &QListWidget::currentRowChanged, this,
          &MissionsTab::showMission);

  auto *scroll = new QScrollArea(splitter);
  scroll->setWidgetResizable(true);
  auto *page = new QWidget(scroll);
  auto *layout = new QVBoxLayout(page);
  scroll->setWidget(page);

  m_title = richLabel(page);
  QFont titleFont = m_title->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
  titleFont.setBold(true);
  m_title->setFont(titleFont);
  layout->addWidget(m_title);
  m_learn = richLabel(page);
  layout->addWidget(m_learn);
  m_objective = richLabel(page);
  layout->addWidget(m_objective);

  auto *conceptGroup = new QGroupBox("Concepts", page);
  auto *conceptLayout = new QVBoxLayout(conceptGroup);
  m_explanation = richLabel(conceptGroup);
  conceptLayout->addWidget(m_explanation);
  layout->addWidget(conceptGroup);

  auto *buttons = new QHBoxLayout();
  auto *loadButton = new QPushButton("Load example", page);
  loadButton->setToolTip(
      "Replace the editor contents with the example program");
  connect(loadButton, &QPushButton::clicked, this, [this] {
    if (m_current >= 0)
      emit loadProgramRequested(m_missions[m_current].example);
  });
  auto *procButton = new QPushButton("Select processor", page);
  connect(procButton, &QPushButton::clicked, this, [this] {
    if (m_current < 0)
      return;
    bool ok = false;
    const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
        m_missions[m_current].processor.toUtf8().constData(), &ok);
    if (ok)
      emit selectProcessorRequested(static_cast<ProcessorID>(v));
  });
  auto *explorerButton = new QPushButton("Open Explorer", page);
  connect(explorerButton, &QPushButton::clicked, this,
          &MissionsTab::showExplorerRequested);
  buttons->addWidget(loadButton);
  buttons->addWidget(procButton);
  buttons->addWidget(explorerButton);
  buttons->addStretch();
  layout->addLayout(buttons);
  m_processorLabel = richLabel(page);
  layout->addWidget(m_processorLabel);

  m_stepsGroup = new QGroupBox("Guided steps (verified live)", page);
  m_stepsLayout = new QVBoxLayout(m_stepsGroup);
  layout->addWidget(m_stepsGroup);

  auto *challengeGroup = new QGroupBox("Challenge", page);
  auto *challengeLayout = new QVBoxLayout(challengeGroup);
  m_challenge = richLabel(challengeGroup);
  challengeLayout->addWidget(m_challenge);
  auto *challengeButtons = new QHBoxLayout();
  m_hintButton = new QPushButton("Show hint", challengeGroup);
  m_hintButton->setCheckable(true);
  connect(m_hintButton, &QPushButton::toggled, this,
          [this](bool on) { m_hint->setVisible(on); });
  auto *checkButton = new QPushButton("Check challenge", challengeGroup);
  checkButton->setToolTip("Verify your solution against the simulator state "
                          "and the recorded trace");
  connect(checkButton, &QPushButton::clicked, this,
          &MissionsTab::checkChallenge);
  challengeButtons->addWidget(m_hintButton);
  challengeButtons->addWidget(checkButton);
  challengeButtons->addStretch();
  challengeLayout->addLayout(challengeButtons);
  m_hint = richLabel(challengeGroup);
  m_hint->setVisible(false);
  challengeLayout->addWidget(m_hint);
  m_result = richLabel(challengeGroup);
  m_result->setObjectName("missionResult");
  challengeLayout->addWidget(m_result);
  m_completion = richLabel(challengeGroup);
  challengeLayout->addWidget(m_completion);
  layout->addWidget(challengeGroup);
  layout->addStretch();

  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, &MissionsTab::evaluateSteps);
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          &MissionsTab::evaluateSteps);
  connect(m_recorder, &TraceRecorder::runStarted, this,
          &MissionsTab::evaluateSteps, Qt::QueuedConnection);

  refreshMissionList();
  if (!m_missions.empty())
    m_list->setCurrentRow(0);
  else
    m_title->setText("No missions available: " + error.toHtmlEscaped());
}

void MissionsTab::tabVisibilityChanged(bool visible) {
  m_visible = visible;
  if (visible)
    evaluateSteps();
}

bool MissionsTab::isCompleted(const QString &id) const {
  return RipesSettings::value(RIPES_SETTING_OSX_MISSIONS_COMPLETED)
      .toStringList()
      .contains(id);
}

void MissionsTab::markCompleted(const QString &id) {
  auto done =
      RipesSettings::value(RIPES_SETTING_OSX_MISSIONS_COMPLETED).toStringList();
  if (!done.contains(id)) {
    done << id;
    RipesSettings::setValue(RIPES_SETTING_OSX_MISSIONS_COMPLETED, done);
  }
}

void MissionsTab::refreshMissionList() {
  const int row = m_list->currentRow();
  m_list->blockSignals(true);
  m_list->clear();
  for (const auto &m : m_missions)
    m_list->addItem(QString("%1 Mission %2: %3")
                        .arg(isCompleted(m.id) ? "✓" : "○")
                        .arg(m.number)
                        .arg(m.title));
  m_list->setCurrentRow(row);
  m_list->blockSignals(false);
}

void MissionsTab::showMission(int index) {
  if (index < 0 || static_cast<size_t>(index) >= m_missions.size())
    return;
  m_current = index;
  const auto &m = m_missions[index];
  m_title->setText(
      QString("Mission %1 — %2").arg(m.number).arg(m.title.toHtmlEscaped()));
  m_learn->setText("<b>You will learn:</b> " +
                   m.learn.join(" · ").toHtmlEscaped());
  m_objective->setText("<b>Objective:</b> " + m.objective.toHtmlEscaped());
  m_explanation->setText(m.explanation);
  bool ok = false;
  const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
      m.processor.toUtf8().constData(), &ok);
  m_processorLabel->setText(
      QString("<b>Processor:</b> %1 (%2). <b>Example program:</b><pre>%3</pre>")
          .arg(ok ? ProcessorRegistry::getDescription(
                        static_cast<ProcessorID>(v))
                        .name.toHtmlEscaped()
                  : QString(),
               m.processor.toHtmlEscaped(), m.example.toHtmlEscaped()));

  // Rebuild the step widgets.
  while (auto *item = m_stepsLayout->takeAt(0)) {
    if (auto *w = item->widget())
      w->deleteLater();
    delete item;
  }
  m_stepWidgets.clear();
  int n = 1;
  for (const auto &step : m.steps) {
    auto *box = new QWidget(m_stepsGroup);
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 4, 0, 4);
    auto *status = new QLabel(box);
    status->setObjectName("missionStepStatus");
    status->setMinimumWidth(28);
    status->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    QFont f = status->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    status->setFont(f);
    auto *col = new QVBoxLayout();
    auto *text = richLabel(box);
    text->setText(QString("<b>Step %1: %2</b><br>%3")
                      .arg(n++)
                      .arg(step.title.toHtmlEscaped(), step.instruction));
    auto *checks = richLabel(box);
    col->addWidget(text);
    col->addWidget(checks);
    row->addWidget(status);
    row->addLayout(col, 1);
    m_stepsLayout->addWidget(box);
    m_stepWidgets.push_back({status, checks});
  }

  m_challenge->setText(m.challenge);
  m_hint->setText("<i>Hint:</i> " + m.hint.toHtmlEscaped());
  m_hintButton->setChecked(false);
  m_result->clear();
  m_completion->setText(
      isCompleted(m.id)
          ? QString("<span style='color:%1'>✓ Completed.</span> %2")
                .arg(okColor(), m.completion.toHtmlEscaped())
          : QString());
  evaluateSteps();
}

MissionState MissionsTab::currentState() const {
  return captureMissionState(m_recorder->trace(),
                             m_sourceProvider ? m_sourceProvider() : QString());
}

QString MissionsTab::criteriaHtml(const CheckReport &report) const {
  QString html;
  for (const auto &r : report.results) {
    html += QString("<span style='color:%1'>%2</span> %3")
                .arg(r.passed ? okColor() : badColor(), r.passed ? "✓" : "✗",
                     r.description.toHtmlEscaped());
    if (!r.detail.isEmpty())
      html +=
          " <span style='color:gray'>(" + r.detail.toHtmlEscaped() + ")</span>";
    html += "<br>";
  }
  return html;
}

void MissionsTab::evaluateSteps() {
  if (!m_visible || m_current < 0 || ProcessorHandler::isRunning())
    return;
  const auto &m = m_missions[m_current];
  const auto state = currentState();
  auto &done = m_completedSteps[m.id];
  bool previousDone = true;
  for (size_t i = 0; i < m.steps.size() && i < m_stepWidgets.size(); ++i) {
    const auto report = evaluateChecks(m.steps[i].checks, state, m);
    if (report.passed)
      done.insert(i);
    const bool completed = done.count(i) != 0;
    auto &w = m_stepWidgets[i];
    w.status->setText(completed ? "✓" : (previousDone ? "▶" : "○"));
    w.status->setStyleSheet(completed ? QString("color:%1").arg(okColor())
                                      : QString());
    QString html = criteriaHtml(report);
    if (completed && !report.passed)
      html = "<i>Completed earlier. Live state now:</i><br>" + html;
    w.checks->setText(html);
    previousDone = completed;
  }
}

void MissionsTab::checkChallenge() {
  if (m_current < 0)
    return;
  const auto &m = m_missions[m_current];
  const auto report = evaluateChecks(m.challengeChecks, currentState(), m);
  QString html = criteriaHtml(report);
  if (report.passed) {
    html = QString("<p style='color:%1'><b>Challenge solved!</b> Verified "
                   "against the simulator state and the recorded trace.</p>")
               .arg(okColor()) +
           html;
    markCompleted(m.id);
    m_completion->setText(QString("<span style='color:%1'>✓ Completed.</span> "
                                  "%2")
                              .arg(okColor(), m.completion.toHtmlEscaped()));
    refreshMissionList();
  } else {
    html = QString("<p style='color:%1'><b>Not solved yet.</b> The failing "
                   "conditions are marked below.</p>")
               .arg(badColor()) +
           html;
  }
  m_result->setText(html);
}

} // namespace Ripes
