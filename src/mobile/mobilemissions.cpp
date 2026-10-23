// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "mobilemissions.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaEnum>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "explorer/tracerecorder.h"
#include "i18n/language.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "theme/painting.h"
#include "theme/theme.h"
#include "touch.h"

namespace Ripes {
namespace Mobile {

using namespace Explorer;
using namespace Theme;

namespace {

QFont scaledFont(const QFont &f, qreal factor, bool bold = false) {
  QFont s = f;
  if (f.pointSizeF() > 0)
    s.setPointSizeF(f.pointSizeF() * factor);
  s.setBold(bold);
  return s;
}

QColor statusColor(MissionStatus s) {
  const auto &t = tokens();
  switch (s) {
  case MissionStatus::Solved:
    return t.gold;
  case MissionStatus::InProgress:
    return t.accent;
  case MissionStatus::Available:
    return t.accent2;
  case MissionStatus::Locked:
    break;
  }
  return t.muted;
}

QString processorName(const QString &id) {
  bool ok = false;
  const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
      id.toUtf8().constData(), &ok);
  return ok ? ProcessorRegistry::getDescription(static_cast<ProcessorID>(v))
                  .name
            : QString();
}

void repolish(QWidget *w) {
  w->style()->unpolish(w);
  w->style()->polish(w);
  w->update();
}

/// Hexagonal mission node with its number (or a lock).
void drawNode(QPainter &p, const QPointF &c, qreal r, MissionStatus status,
              const QString &label, const QFont &font) {
  const auto &t = tokens();
  const QColor col = statusColor(status);
  QColor fill = t.panel, fg = t.muted;
  if (status == MissionStatus::Solved || status == MissionStatus::InProgress) {
    fill = col;
    fg = textOn(col);
  } else if (status == MissionStatus::Available) {
    fill = mix(t.card, col, 0.18);
    fg = t.dark ? col.lighter(125) : col.darker(110);
  }
  drawHexagon(p, c, r, fill, col, 2);
  if (status == MissionStatus::Locked) {
    drawGlyph(p, QRectF(c.x() - r * 0.45, c.y() - r * 0.45, r * 0.9, r * 0.9),
              Glyph::Lock, fg);
    return;
  }
  p.save();
  p.setFont(font);
  p.setPen(fg);
  p.drawText(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), Qt::AlignCenter,
             label);
  p.restore();
}

} // namespace

// ============================================================================
// PlayerHud
// ============================================================================

PlayerHud::PlayerHud(QWidget *parent) : QWidget(parent) {
  setObjectName("mobilePlayerHud");
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PlayerHud::setTotals(const MissionProgress::Totals &totals) {
  m_totals = totals;
  update();
}

QSize PlayerHud::sizeHint() const { return QSize(340, 112); }
QSize PlayerHud::minimumSizeHint() const { return QSize(240, 112); }

void PlayerHud::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -2.5);
  drawCard(p, panel, 14, t.panel, t.border, 1, true);
  const auto level = playerLevel(m_totals.xp);
  const QPointF hc(panel.left() + 36, panel.top() + 38);
  drawHexagon(p, hc, 25, t.accent2, t.accent2.darker(140), 2);
  p.setFont(scaledFont(font(), 1.4, true));
  p.setPen(textOn(t.accent2));
  p.drawText(QRectF(hc.x() - 25, hc.y() - 25, 50, 50), Qt::AlignCenter,
             QString::number(level.level));
  const qreal x = panel.left() + 72;
  const qreal w = panel.right() - 12 - x;
  p.setFont(overlineFont(font(), 0.72));
  p.setPen(t.accent2);
  p.drawText(QRectF(x, panel.top() + 10, w, 16),
             Qt::AlignLeft | Qt::AlignVCenter, tr("LEVEL %1").arg(level.level));
  const QFont titleFont = scaledFont(font(), 1.15, true);
  p.setFont(titleFont);
  p.setPen(t.text);
  p.drawText(
      QRectF(x, panel.top() + 26, w, 22), Qt::AlignLeft | Qt::AlignVCenter,
      QFontMetricsF(titleFont).elidedText(level.title, Qt::ElideRight, w));
  drawProgressBar(p, QRectF(x, panel.top() + 52, w, 8),
                  double(level.xpIntoLevel) / kXpPerLevel, t.gold);
  const QFont small = scaledFont(font(), 0.82);
  p.setFont(small);
  p.setPen(t.muted);
  p.drawText(QRectF(x, panel.top() + 60, w, 18),
             Qt::AlignLeft | Qt::AlignVCenter,
             tr("%1 / %2 XP to level %3")
                 .arg(level.xpIntoLevel)
                 .arg(kXpPerLevel)
                 .arg(level.level + 1));
  // Stars, solved challenges and verified steps.
  struct Stat {
    Glyph glyph;
    QColor color;
    QString text;
  };
  const std::vector<Stat> stats = {
      {Glyph::Star, t.gold,
       QString("%1/%2").arg(m_totals.stars).arg(m_totals.maxStars)},
      {Glyph::Trophy, t.gold,
       QString("%1/%2").arg(m_totals.solved).arg(m_totals.missions)},
      {Glyph::Check, t.ok,
       QString("%1/%2").arg(m_totals.verifiedSteps).arg(m_totals.steps)},
      {Glyph::Bolt, t.accent, tr("%1 XP").arg(m_totals.xp)}};
  const qreal y = panel.bottom() - 28;
  const qreal colW = (panel.width() - 24) / stats.size();
  const QFont statFont = scaledFont(font(), 0.92, true);
  p.setFont(statFont);
  for (size_t i = 0; i < stats.size(); ++i) {
    const qreal cx = panel.left() + 12 + i * colW;
    drawGlyph(p, QRectF(cx, y + 2, 18, 18), stats[i].glyph, stats[i].color);
    p.setPen(t.text);
    p.drawText(QRectF(cx + 22, y, colW - 24, 22),
               Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(statFont).elidedText(stats[i].text, Qt::ElideRight,
                                                  colW - 24));
  }
}

// ============================================================================
// MissionCard
// ============================================================================

MissionCard::MissionCard(QWidget *parent) : QAbstractButton(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFocusPolicy(Qt::NoFocus);
}

void MissionCard::setInfo(const Info &info) {
  m_info = info;
  setText(tr("Mission %1: %2").arg(info.number).arg(info.title));
  setAccessibleName(text());
  update();
}

QSize MissionCard::sizeHint() const { return QSize(340, 112); }
QSize MissionCard::minimumSizeHint() const { return QSize(240, 112); }

void MissionCard::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -2.5);
  const QColor sc = statusColor(m_info.status);
  drawCard(p, card, 14, isDown() ? mix(t.card, t.accent, 0.12) : t.card,
           m_info.next ? t.accent : t.border, m_info.next ? 2 : 1, true);
  drawNode(p, QPointF(card.left() + 34, card.top() + 40), 22, m_info.status,
           QString::number(m_info.number), scaledFont(font(), 1.3, true));
  const qreal x = card.left() + 68;
  const qreal w = card.right() - 12 - x;
  const QFont over = overlineFont(font(), 0.7);
  p.setFont(over);
  p.setPen(t.dark ? sc.lighter(110) : sc.darker(115));
  p.drawText(QRectF(x, card.top() + 10, w, 16),
             Qt::AlignLeft | Qt::AlignVCenter,
             tr("MISSION %1").arg(m_info.number));
  QString statusText = missionStatusName(m_info.status);
  if (m_info.next && m_info.status != MissionStatus::InProgress)
    statusText = tr("Next");
  const QSizeF ps = pillSize(statusText, over);
  drawPill(p, QPointF(card.right() - 12 - ps.width(), card.top() + 9),
           statusText, sc, over,
           m_info.status == MissionStatus::Solved ||
               m_info.status == MissionStatus::InProgress);
  const QFont titleFont = scaledFont(font(), 1.08, true);
  p.setFont(titleFont);
  p.setPen(m_info.status == MissionStatus::Locked ? t.muted : t.text);
  p.drawText(
      QRectF(x, card.top() + 28, w, 24), Qt::AlignLeft | Qt::AlignVCenter,
      QFontMetricsF(titleFont).elidedText(m_info.title, Qt::ElideRight, w));
  const QFont chip = scaledFont(font(), 0.78);
  drawPill(p, QPointF(x, card.top() + 54), m_info.processor, t.info, chip);
  // Verified steps and stars.
  const auto &r = m_info.record;
  const qreal by = card.bottom() - 26;
  const QFont small = scaledFont(font(), 0.8);
  p.setFont(small);
  const QString stepText = tr("%1/%2 steps").arg(r.verifiedSteps).arg(r.steps);
  const qreal stw = QFontMetricsF(small).horizontalAdvance(stepText) + 4;
  const qreal starsW = 3 * 15 + 2 * 3;
  const qreal barW = std::max(24.0, w - stw - starsW - 20);
  drawProgressBar(p, QRectF(x, by + 6, barW, 7),
                  r.steps ? double(r.verifiedSteps) / r.steps : 0.0, t.ok);
  p.setPen(t.muted);
  p.drawText(QRectF(x + barW + 8, by, stw, 19),
             Qt::AlignLeft | Qt::AlignVCenter, stepText);
  drawStars(p, card.right() - 12 - starsW, by + 9.5, 15, missionStars(r),
            kStarsPerMission);
}

// ============================================================================
// StarsRow
// ============================================================================

StarsRow::StarsRow(QWidget *parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void StarsRow::setRecord(const MissionRecord &record) {
  m_record = record;
  update();
}

QSize StarsRow::sizeHint() const { return QSize(240, 34); }

void StarsRow::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const qreal w = drawStars(p, 0, height() / 2.0, 24, missionStars(m_record),
                            kStarsPerMission);
  const MissionRecord full{m_record.steps, m_record.steps, true};
  p.setFont(scaledFont(font(), 0.95, true));
  p.setPen(t.dark ? t.gold : t.gold.darker(140));
  p.drawText(QRectF(w + 12, 0, width() - w - 12, height()),
             Qt::AlignLeft | Qt::AlignVCenter,
             tr("%1 / %2 XP").arg(missionXp(m_record)).arg(missionXp(full)));
}

// ============================================================================
// MissionsPage
// ============================================================================

MissionsPage::MissionsPage(TraceRecorder *recorder,
                           std::function<QString()> sourceProvider,
                           QWidget *parent)
    : QWidget(parent), m_recorder(recorder),
      m_sourceProvider(std::move(sourceProvider)) {
  setObjectName("mobileMissionsPage");
  QString error;
  m_missions = loadBuiltinMissions(&error, I18n::missionLanguage());
  loadProgress();

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  m_stack = new QStackedWidget(this);
  root->addWidget(m_stack);
  buildMap();
  buildDetail();
  if (m_missions.empty())
    m_mapNote->setText(
        tr("No missions available: %1").arg(error.toHtmlEscaped()));

  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, &MissionsPage::evaluateSteps);
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          &MissionsPage::evaluateSteps);
  connect(m_recorder, &TraceRecorder::runStarted, this,
          &MissionsPage::evaluateSteps, Qt::QueuedConnection);
  // Progress changed elsewhere (a reset, another view): reload it.
  for (const char *key :
       {RIPES_SETTING_OSX_MISSION_STEPS, RIPES_SETTING_OSX_MISSIONS_COMPLETED})
    connect(RipesSettings::getObserver(key), &SettingObserver::modified, this,
            [this] {
              if (m_saving)
                return;
              loadProgress();
              refreshMap();
              if (m_current >= 0)
                openMission(m_current);
            });
  connect(Notifier::get(), &Notifier::changed, this, [this] {
    refreshMap();
    if (m_current >= 0)
      openMission(m_current);
  });
  retranslate();
  refreshMap();
}

void MissionsPage::buildMap() {
  m_mapPage = new ScrollPage(m_stack);
  m_mapPage->setObjectName("mobileMissionMap");
  auto *l = m_mapPage->contentLayout();
  m_hud = new PlayerHud(m_mapPage->content());
  l->addWidget(m_hud);
  m_mapTitle = new QLabel(m_mapPage->content());
  m_mapTitle->setObjectName("mobileOverline");
  l->addWidget(m_mapTitle);
  for (size_t i = 0; i < m_missions.size(); ++i) {
    auto *card = new MissionCard(m_mapPage->content());
    card->setObjectName(QString("mobileMissionCard%1").arg(i));
    connect(card, &QAbstractButton::clicked, this,
            [this, i] { openMission(static_cast<int>(i)); });
    l->addWidget(card);
    m_cards.push_back(card);
  }
  m_mapNote = makeLabel(m_mapPage->content());
  m_mapNote->setObjectName("mobileMuted");
  l->addWidget(m_mapNote);
  m_mapPage->finish();
  m_stack->addWidget(m_mapPage);
}

void MissionsPage::buildDetail() {
  m_detail = new QWidget(m_stack);
  auto *v = new QVBoxLayout(m_detail);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  auto *bar = new QWidget(m_detail);
  bar->setObjectName("mobileSubBar");
  auto *bl = new QHBoxLayout(bar);
  bl->setContentsMargins(4, 2, 12, 2);
  m_backButton = new QToolButton(bar);
  m_backButton->setObjectName("mobileBackButton");
  m_backButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_backButton->setMinimumSize(kTouchTarget * 2, kTouchTarget);
  m_backButton->setFocusPolicy(Qt::NoFocus);
  connect(m_backButton, &QToolButton::clicked, this, &MissionsPage::showMap);
  bl->addWidget(m_backButton);
  m_detailBarTitle = new QLabel(bar);
  m_detailBarTitle->setObjectName("mobileSubBarTitle");
  m_detailBarTitle->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  bl->addWidget(m_detailBarTitle, 1);
  v->addWidget(bar);

  m_detailPage = new ScrollPage(m_detail);
  m_detailPage->setObjectName("mobileMissionDetail");
  v->addWidget(m_detailPage, 1);
  QWidget *c = m_detailPage->content();
  auto *l = m_detailPage->contentLayout();

  m_bannerCard = new Card(c);
  m_overline = makeLabel(m_bannerCard);
  m_overline->setObjectName("mobileOverline");
  m_title = makeLabel(m_bannerCard, false);
  m_title->setObjectName("mobileTitle");
  m_title->setFont(scaledFont(font(), 1.35, true));
  m_stars = new StarsRow(m_bannerCard);
  m_meta = makeLabel(m_bannerCard);
  m_meta->setObjectName("mobileMuted");
  for (QWidget *w :
       std::initializer_list<QWidget *>{m_overline, m_title, m_stars, m_meta})
    m_bannerCard->body()->addWidget(w);
  l->addWidget(m_bannerCard);

  m_lockNote = makeLabel(c);
  m_lockNote->setObjectName("mobileWarnNote");
  m_lockNote->setVisible(false);
  l->addWidget(m_lockNote);

  m_objectiveCard = new Card(c);
  m_objective = makeLabel(m_objectiveCard);
  m_objectiveCard->body()->addWidget(m_objective);
  l->addWidget(m_objectiveCard);

  m_loadButton = makeButton(QString(), c, "primary");
  m_loadButton->setObjectName("mobileLoadExample");
  connect(m_loadButton, &QAbstractButton::clicked, this,
          &MissionsPage::loadExample);
  m_processorButton = makeButton(QString(), c);
  m_processorButton->setObjectName("mobileSelectProcessor");
  connect(m_processorButton, &QAbstractButton::clicked, this,
          &MissionsPage::selectProcessor);
  m_runButton = makeButton(QString(), c);
  m_runButton->setObjectName("mobileGoToRun");
  connect(m_runButton, &QAbstractButton::clicked, this,
          &MissionsPage::openRunRequested);
  l->addWidget(m_loadButton);
  l->addWidget(m_processorButton);
  l->addWidget(m_runButton);

  m_stepsCard = new Card(c);
  m_stepsLayout = new QVBoxLayout();
  m_stepsLayout->setSpacing(8);
  m_stepsCard->body()->addLayout(m_stepsLayout);
  l->addWidget(m_stepsCard);

  m_challengeCard = new Card(c);
  m_challengeCard->setAccent(tokens().gold);
  m_challenge = makeLabel(m_challengeCard);
  m_challengeCard->body()->addWidget(m_challenge);
  m_hintButton = makeButton(QString(), m_challengeCard);
  m_hintButton->setCheckable(true);
  m_hint = makeLabel(m_challengeCard);
  m_hint->setVisible(false);
  connect(m_hintButton, &QAbstractButton::toggled, m_hint,
          &QWidget::setVisible);
  m_checkButton = makeButton(QString(), m_challengeCard, "gold");
  m_checkButton->setObjectName("mobileCheckChallenge");
  connect(m_checkButton, &QAbstractButton::clicked, this,
          &MissionsPage::checkChallenge);
  m_result = makeLabel(m_challengeCard);
  m_result->setObjectName("mobileMissionResult");
  m_completion = makeLabel(m_challengeCard);
  for (QWidget *w : std::initializer_list<QWidget *>{
           m_hintButton, m_hint, m_checkButton, m_result, m_completion})
    m_challengeCard->body()->addWidget(w);
  l->addWidget(m_challengeCard);

  m_conceptCard = new Card(c);
  m_concepts = makeLabel(m_conceptCard);
  m_conceptCard->body()->addWidget(m_concepts);
  l->addWidget(m_conceptCard);

  m_exampleCard = new Card(c);
  m_example = makeLabel(m_exampleCard);
  m_exampleCard->body()->addWidget(m_example);
  l->addWidget(m_exampleCard);
  m_detailPage->finish();
  m_stack->addWidget(m_detail);
}

void MissionsPage::retranslate() {
  m_mapTitle->setText(tr("MISSION MAP"));
  m_backButton->setText("‹ " + tr("Missions"));
  m_loadButton->setText(tr("Load example"));
  m_processorButton->setText(tr("Select processor"));
  m_runButton->setText(tr("Go to Run ▶"));
  m_stepsCard->setTitle(tr("Guided steps (verified live)"));
  m_challengeCard->setTitle(tr("Challenge"));
  m_conceptCard->setTitle(tr("Briefing: concepts"));
  m_exampleCard->setTitle(tr("Example program"));
  m_objectiveCard->setTitle(tr("Objective"));
  m_hintButton->setText(tr("Show hint"));
  m_checkButton->setText(tr("Check challenge"));
  m_mapNote->setText(
      tr("Missions are verified against the simulator: steps tick off when "
         "the live processor state and the recorded trace meet their "
         "conditions. Stars and XP only come from verified steps and solved "
         "challenges."));
}

void MissionsPage::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() != QEvent::LanguageChange)
    return;
  QString error;
  auto missions = loadBuiltinMissions(&error, I18n::missionLanguage());
  if (missions.size() == m_missions.size())
    m_missions = std::move(missions);
  retranslate();
  refreshMap();
  if (m_current >= 0)
    openMission(m_current);
}

bool MissionsPage::showingDetail() const {
  return m_stack->currentWidget() == m_detail;
}

ScrollPage *MissionsPage::currentScrollPage() const {
  return showingDetail() ? m_detailPage : m_mapPage;
}

QWidget *MissionsPage::missionCardWidget(int index) const {
  return index >= 0 && index < int(m_cards.size()) ? m_cards[index] : nullptr;
}

void MissionsPage::loadProgress() {
  m_progress = MissionProgress(
      RipesSettings::value(RIPES_SETTING_OSX_MISSION_STEPS).toStringList(),
      RipesSettings::value(RIPES_SETTING_OSX_MISSIONS_COMPLETED)
          .toStringList());
}

void MissionsPage::saveProgress() {
  m_saving = true;
  if (RipesSettings::value(RIPES_SETTING_OSX_MISSION_STEPS).toStringList() !=
      m_progress.verifiedSteps())
    RipesSettings::setValue(RIPES_SETTING_OSX_MISSION_STEPS,
                            m_progress.verifiedSteps());
  if (RipesSettings::value(RIPES_SETTING_OSX_MISSIONS_COMPLETED)
          .toStringList() != m_progress.solved())
    RipesSettings::setValue(RIPES_SETTING_OSX_MISSIONS_COMPLETED,
                            m_progress.solved());
  m_saving = false;
}

void MissionsPage::resetProgress() {
  m_progress = MissionProgress();
  saveProgress();
  refreshMap();
  if (m_current >= 0)
    openMission(m_current);
}

void MissionsPage::refreshMap() {
  const auto records = m_progress.records(m_missions);
  int next = -1;
  for (size_t i = 0; i < records.size() && next < 0; ++i) {
    const auto s = missionStatus(records, i);
    if (s == MissionStatus::Available || s == MissionStatus::InProgress)
      next = static_cast<int>(i);
  }
  for (size_t i = 0; i < m_missions.size() && i < m_cards.size(); ++i) {
    MissionCard::Info info;
    info.number = m_missions[i].number;
    info.title = m_missions[i].title;
    info.processor = m_missions[i].processor;
    info.record = records[i];
    info.status = missionStatus(records, i);
    info.next = static_cast<int>(i) == next;
    m_cards[i]->setInfo(info);
  }
  m_hud->setTotals(m_progress.totals(m_missions));
}

void MissionsPage::showMap() {
  m_current = -1;
  refreshMap();
  m_stack->setCurrentWidget(m_mapPage);
  emit currentMissionChanged(-1);
}

void MissionsPage::refreshDetailHeader() {
  if (m_current < 0)
    return;
  const auto &m = m_missions[m_current];
  const auto records = m_progress.records(m_missions);
  const auto status = missionStatus(records, m_current);
  const QColor sc = statusColor(status);
  m_overline->setText(
      QString("<span style='color:%1'>%2</span>")
          .arg((tokens().dark ? sc.lighter(110) : sc.darker(120)).name(),
               tr("MISSION %1 · %2")
                   .arg(m.number)
                   .arg(missionStatusName(status).toUpper())));
  m_title->setText(m.title);
  m_stars->setRecord(records[m_current]);
  const auto &r = records[m_current];
  m_meta->setText((r.solved
                       ? tr("%1 of %2 guided steps verified · challenge solved")
                       : tr("%1 of %2 guided steps verified · challenge open"))
                      .arg(r.verifiedSteps)
                      .arg(r.steps) +
                  "<br>" +
                  tr("Processor: %1 (%2)")
                      .arg(processorName(m.processor).toHtmlEscaped(),
                           m.processor.toHtmlEscaped()));
  m_bannerCard->setAccent(sc);
  m_detailBarTitle->setText(tr("Mission %1").arg(m.number));
}

void MissionsPage::openMission(int index) {
  if (index < 0 || static_cast<size_t>(index) >= m_missions.size())
    return;
  const bool changed = index != m_current || !showingDetail();
  m_current = index;
  m_last = index;
  const auto &m = m_missions[index];
  const auto records = m_progress.records(m_missions);
  const auto status = missionStatus(records, index);
  refreshDetailHeader();
  if (status == MissionStatus::Locked && index > 0) {
    m_lockNote->setText(
        tr("<b>Locked</b> – recommended after Mission %1: verify its guided "
           "steps (or solve its challenge) to unlock it on the map. You can "
           "still play it now.")
            .arg(m_missions[index - 1].number));
    m_lockNote->setVisible(true);
  } else {
    m_lockNote->setVisible(false);
  }
  m_objective->setText(
      m.objective.toHtmlEscaped() +
      "<br><span style='color:" + tokens().muted.name() + "'>" +
      tr("You will learn: %1").arg(m.learn.join(" · ").toHtmlEscaped()) +
      "</span>");
  m_concepts->setText(m.explanation);
  m_example->setText("<pre style='font-family:monospace'>" +
                     m.example.toHtmlEscaped() + "</pre>");

  while (auto *item = m_stepsLayout->takeAt(0)) {
    if (auto *w = item->widget())
      w->deleteLater();
    delete item;
  }
  m_stepWidgets.clear();
  int n = 1;
  for (const auto &step : m.steps) {
    auto *box = new QFrame(m_stepsCard);
    box->setObjectName("mobileStep");
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(10, 10, 10, 10);
    row->setSpacing(10);
    auto *statusLabel = new QLabel(box);
    statusLabel->setObjectName("mobileStepStatus");
    statusLabel->setFixedWidth(30);
    statusLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto *col = new QVBoxLayout();
    col->setSpacing(4);
    auto *text = makeLabel(box);
    text->setText(tr("<b>Step %1: %2</b><br>%3")
                      .arg(n++)
                      .arg(step.title.toHtmlEscaped(), step.instruction));
    auto *checks = makeLabel(box);
    checks->setObjectName("mobileStepChecks");
    col->addWidget(text);
    col->addWidget(checks);
    row->addWidget(statusLabel, 0, Qt::AlignTop);
    row->addLayout(col, 1);
    m_stepsLayout->addWidget(box);
    m_stepWidgets.push_back({box, statusLabel, text, checks});
  }
  m_challenge->setText(m.challenge);
  m_hint->setText("<i>" + tr("Hint:") + "</i> " + m.hint.toHtmlEscaped());
  m_hintButton->setChecked(false);
  if (changed)
    m_result->clear();
  m_completion->setText(m_progress.isSolved(m.id)
                            ? QString("<span style='color:%1'>✓ %2</span> %3")
                                  .arg(tokens().ok.name(), tr("Completed."),
                                       m.completion.toHtmlEscaped())
                            : QString());
  m_stack->setCurrentWidget(m_detail);
  if (changed)
    m_detailPage->scrollToTop();
  evaluateSteps();
  if (changed)
    emit currentMissionChanged(index);
}

MissionState MissionsPage::currentState() const {
  return captureMissionState(m_recorder->trace(),
                             m_sourceProvider ? m_sourceProvider() : QString());
}

QString MissionsPage::criteriaHtml(const CheckReport &report) const {
  QString html;
  for (const auto &r : report.results) {
    html += QString("<span style='color:%1'>%2</span> %3")
                .arg(r.passed ? tokens().ok.name() : tokens().bad.name(),
                     r.passed ? "✓" : "✗", r.description.toHtmlEscaped());
    if (!r.detail.isEmpty())
      html += " <span style='color:" + tokens().muted.name() + "'>(" +
              r.detail.toHtmlEscaped() + ")</span>";
    html += "<br>";
  }
  return html;
}

void MissionsPage::evaluateSteps() {
  // The open mission is verified whichever page is shown, so that steps
  // done on the Run page count.
  if (m_current < 0 || ProcessorHandler::isRunning())
    return;
  const auto &m = m_missions[m_current];
  const auto state = currentState();
  bool previousDone = true;
  QStringList newlyVerified;
  for (size_t i = 0; i < m.steps.size() && i < m_stepWidgets.size(); ++i) {
    const auto report = evaluateChecks(m.steps[i].checks, state, m);
    if (report.passed && m_progress.recordStep(m.id, i))
      newlyVerified << QString::number(i + 1);
    const bool completed = m_progress.stepVerified(m.id, i);
    auto &w = m_stepWidgets[i];
    const char *stateName = completed      ? "done"
                            : previousDone ? "current"
                                           : "todo";
    w.status->setText(completed ? "✓" : (previousDone ? "▶" : "○"));
    w.status->setProperty("state", stateName);
    w.box->setProperty("state", stateName);
    repolish(w.status);
    repolish(w.box);
    QString html = criteriaHtml(report);
    if (completed && !report.passed)
      html =
          "<i>" + tr("Completed earlier. Live state now:") + "</i><br>" + html;
    w.checks->setText(html);
    previousDone = completed;
  }
  if (!newlyVerified.isEmpty()) {
    saveProgress();
    refreshMap();
    refreshDetailHeader();
    emit progressMessage(
        tr("Mission %1: step %2 verified · +%3 XP")
            .arg(m.number)
            .arg(newlyVerified.join(", "))
            .arg(kXpPerStep * static_cast<int>(newlyVerified.size())));
  }
}

void MissionsPage::checkChallenge() {
  if (m_current < 0)
    return;
  const auto &m = m_missions[m_current];
  const auto report = evaluateChecks(m.challengeChecks, currentState(), m);
  QString html = criteriaHtml(report);
  if (report.passed) {
    const bool first = m_progress.recordSolved(m.id);
    html =
        QString("<p style='color:%1'><b>%2</b> %3%4</p>")
            .arg(
                tokens().ok.name(), tr("Challenge solved!"),
                tr("Verified against the simulator state and the "
                   "recorded trace."),
                first
                    ? " <b>" +
                          tr("+%1 XP · ★ challenge star").arg(kXpPerChallenge) +
                          "</b>"
                    : " " + tr("(already counted)")) +
        html;
    m_completion->setText(QString("<span style='color:%1'>✓ %2</span> %3")
                              .arg(tokens().ok.name(), tr("Completed."),
                                   m.completion.toHtmlEscaped()));
    if (first) {
      saveProgress();
      refreshMap();
      refreshDetailHeader();
      emit progressMessage(tr("Mission %1: challenge solved · +%2 XP")
                               .arg(m.number)
                               .arg(kXpPerChallenge));
    }
  } else {
    html = QString("<p style='color:%1'><b>%2</b> %3</p>")
               .arg(tokens().bad.name(), tr("Not solved yet."),
                    tr("The failing conditions are marked below.")) +
           html;
  }
  m_result->setText(html);
}

void MissionsPage::loadExample() {
  if (m_current >= 0)
    emit loadProgramRequested(m_missions[m_current].example);
}

void MissionsPage::selectProcessor() {
  if (m_current < 0)
    return;
  bool ok = false;
  const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
      m_missions[m_current].processor.toUtf8().constData(), &ok);
  if (ok)
    emit selectProcessorRequested(static_cast<ProcessorID>(v));
}

} // namespace Mobile
} // namespace Ripes
