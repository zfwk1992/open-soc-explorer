// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "mobileviews.h"

#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSlider>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "explorer/instructionexplainer.h"
#include "explorer/tracereplay.h"
#include "theme/painting.h"
#include "theme/theme.h"
#include "touch.h"

namespace Ripes {
namespace Mobile {

using namespace Explorer;
using namespace Theme;

namespace {

QString signedString(quint64 v, unsigned xlen) {
  const quint64 signBit = quint64(1) << (xlen - 1);
  const quint64 m = xlen >= 64 ? ~quint64(0) : (signBit << 1) - 1;
  v &= m;
  return QString::number(static_cast<qint64>((v ^ signBit) - signBit));
}

QToolButton *navButton(const QString &text, QWidget *parent) {
  auto *b = new QToolButton(parent);
  b->setText(text);
  b->setObjectName("mobileIconButton");
  b->setMinimumSize(kTouchTarget + 4, kTouchTarget + 4);
  b->setFocusPolicy(Qt::NoFocus);
  b->setAutoRepeat(true);
  b->setAutoRepeatDelay(450);
  b->setAutoRepeatInterval(140);
  return b;
}

} // namespace

// ============================================================================
// StageCard
// ============================================================================

StageCard::StageCard(QWidget *parent) : QAbstractButton(parent) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFocusPolicy(Qt::NoFocus);
}

void StageCard::setVisual(const SlotVisual &visual, const QString &lane) {
  m_visual = visual;
  m_lane = lane;
  setText(visual.stage + "\n" + visual.assembly);
  update();
}

QSize StageCard::sizeHint() const { return QSize(340, 84); }
QSize StageCard::minimumSizeHint() const { return QSize(200, 84); }

void StageCard::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF f = QRectF(rect()).adjusted(1, 1, -1, -3);
  const QColor sc = stageColor(m_visual.colorIndex);
  if (m_visual.selected)
    drawGlow(p, f, 12, t.accent, 5);
  drawCard(p, f, 12, isDown() ? mix(t.card, t.accent, 0.1) : t.card,
           m_visual.selected ? t.accent : t.border, m_visual.selected ? 2 : 1,
           true);
  // Stage band.
  const qreal bandW = 70;
  p.save();
  QPainterPath clip;
  clip.addRoundedRect(f.adjusted(1, 1, -1, -1), 11, 11);
  p.setClipPath(clip);
  const QRectF band(f.left(), f.top(), bandW, f.height());
  if (isGame()) {
    QLinearGradient g(band.topLeft(), band.bottomLeft());
    g.setColorAt(0, sc.darker(t.dark ? 120 : 102));
    g.setColorAt(1, mix(sc, t.card, 0.3));
    p.fillRect(band, g);
  } else {
    p.fillRect(band, withAlpha(sc, 80));
  }
  p.restore();
  const QColor bandText =
      isGame() ? textOn(sc.darker(t.dark ? 120 : 102)) : t.text;
  QFont hf = overlineFont(font(), 0.9);
  p.setFont(hf);
  p.setPen(bandText);
  p.drawText(QRectF(band.left() + 4, band.top() + 8, bandW - 8,
                    band.height() - (m_lane.isEmpty() ? 16 : 34)),
             Qt::AlignCenter | Qt::TextWordWrap, m_visual.stage);
  if (!m_lane.isEmpty()) {
    p.setFont(overlineFont(font(), 0.62));
    p.drawText(QRectF(band.left() + 2, band.bottom() - 24, bandW - 4, 16),
               Qt::AlignCenter, m_lane);
  }
  StageTile::paintBody(p,
                       QRectF(band.right() + 8, f.top() + 7,
                              f.right() - band.right() - 15, f.height() - 14),
                       m_visual, font());
}

// ============================================================================
// CycleBar
// ============================================================================

CycleBar::CycleBar(QWidget *parent) : QWidget(parent) {
  setObjectName("mobileCycleBar");
  auto *l = new QHBoxLayout(this);
  l->setContentsMargins(8, 6, 8, 6);
  l->setSpacing(6);
  m_first = navButton("⏮", this);
  m_previous = navButton("◀", this);
  m_next = navButton("▶", this);
  m_last = navButton("⏭", this);
  m_first->setAutoRepeat(false);
  m_last->setAutoRepeat(false);
  m_first->setObjectName("mobileFirstCycle");
  m_previous->setObjectName("mobilePreviousCycle");
  m_next->setObjectName("mobileNextCycle");
  m_last->setObjectName("mobileLastCycle");
  m_slider = new QSlider(Qt::Horizontal, this);
  m_slider->setObjectName("mobileCycleSlider");
  m_slider->setMinimumHeight(kTouchTarget);
  m_slider->setFocusPolicy(Qt::NoFocus);
  l->addWidget(m_first);
  l->addWidget(m_previous);
  l->addWidget(m_slider, 1);
  l->addWidget(m_next);
  l->addWidget(m_last);
  connect(m_first, &QToolButton::clicked, this,
          [this] { emit cycleRequested(m_slider->minimum()); });
  connect(m_last, &QToolButton::clicked, this,
          [this] { emit cycleRequested(m_slider->maximum()); });
  connect(m_previous, &QToolButton::clicked, this,
          [this] { emit cycleRequested(m_cycle - 1); });
  connect(m_next, &QToolButton::clicked, this,
          [this] { emit cycleRequested(m_cycle + 1); });
  connect(m_slider, &QSlider::valueChanged, this,
          [this](int v) { emit cycleRequested(v); });
}

void CycleBar::setRange(qint64 first, qint64 last) {
  m_slider->blockSignals(true);
  m_slider->setRange(static_cast<int>(first), static_cast<int>(last));
  m_slider->blockSignals(false);
  setEnabled(last > first);
}

void CycleBar::setCycle(qint64 cycle) {
  m_cycle = cycle;
  m_slider->blockSignals(true);
  m_slider->setValue(static_cast<int>(cycle));
  m_slider->blockSignals(false);
  m_previous->setEnabled(cycle > m_slider->minimum());
  m_first->setEnabled(cycle > m_slider->minimum());
  m_next->setEnabled(cycle < m_slider->maximum());
  m_last->setEnabled(cycle < m_slider->maximum());
}

// ============================================================================
// PipelinePage
// ============================================================================

PipelinePage::PipelinePage(QWidget *parent) : QWidget(parent) {
  setObjectName("mobilePipelinePage");
  auto *v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  m_page = new ScrollPage(this);
  m_page->setObjectName("mobilePipelineScroll");
  auto *l = m_page->contentLayout();
  l->setSpacing(8);
  QWidget *c = m_page->content();
  m_cycleLabel = makeLabel(c);
  m_cycleLabel->setObjectName("mobileTitle");
  {
    QFont f = m_cycleLabel->font();
    if (f.pointSizeF() > 0)
      f.setPointSizeF(f.pointSizeF() * 1.2);
    f.setBold(true);
    m_cycleLabel->setFont(f);
  }
  l->addWidget(m_cycleLabel);
  m_statsLabel = makeLabel(c);
  m_statsLabel->setObjectName("mobileMuted");
  l->addWidget(m_statsLabel);
  m_cardsLayout = new QGridLayout();
  m_cardsLayout->setSpacing(8);
  l->addLayout(m_cardsLayout);
  m_hint = makeLabel(c);
  m_hint->setObjectName("mobileMuted");
  l->addWidget(m_hint);
  m_eventsCard = new Card(c);
  m_events = makeLabel(m_eventsCard);
  m_events->setObjectName("mobileEvents");
  m_eventsCard->body()->addWidget(m_events);
  l->addWidget(m_eventsCard);
  m_page->finish();
  v->addWidget(m_page, 1);
  m_cycleBar = new CycleBar(this);
  connect(m_cycleBar, &CycleBar::cycleRequested, this,
          &PipelinePage::cycleRequested);
  v->addWidget(m_cycleBar);
  retranslate();
  setState(nullptr, 0, -1);
}

void PipelinePage::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    retranslate();
    setState(m_replay, m_cycle, m_selected);
  }
}

void PipelinePage::retranslate() {
  m_eventsCard->setTitle(tr("Recorded in this cycle"));
  m_hint->setText(tr("Tap an instruction to see its explanation. Use ◀ ▶ or "
                     "the slider to move through the recorded cycles; the "
                     "simulator is not changed."));
}

QString PipelinePage::cycleText() const { return m_cycleLabel->text(); }

void PipelinePage::rebuildCards(const TraceReplay *replay) {
  std::vector<QString> keys;
  if (replay)
    for (const auto &sd : replay->trace().header.stages)
      keys.push_back(QString("%1/%2").arg(sd.lane).arg(sd.name));
  if (keys == m_stageKeys)
    return;
  m_stageKeys = keys;
  for (auto *card : m_cards)
    card->deleteLater();
  m_cards.clear();
  for (size_t i = 0; i < keys.size(); ++i) {
    auto *card = new StageCard(m_page->content());
    card->setObjectName(QString("mobileStageCard%1").arg(i));
    connect(card, &QAbstractButton::clicked, this, [this, card] {
      if (card->visual().instance >= 0)
        emit instanceActivated(card->visual().instance);
    });
    m_cards.push_back(card);
  }
  m_columns = 0;
  layoutCards();
}

void PipelinePage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  layoutCards();
}

void PipelinePage::layoutCards() {
  // Landscape phones: stages side by side (at least 260 px per card).
  const int columns =
      std::clamp(width() / 260, 1, std::max(1, int(m_cards.size())));
  if (columns == m_columns)
    return;
  m_columns = columns;
  for (auto *card : m_cards)
    m_cardsLayout->removeWidget(card);
  for (int c = 0; c < m_cardsLayout->columnCount(); ++c)
    m_cardsLayout->setColumnStretch(c, 0);
  for (size_t i = 0; i < m_cards.size(); ++i)
    m_cardsLayout->addWidget(m_cards[i], int(i) / columns, int(i) % columns);
  for (int c = 0; c < columns; ++c)
    m_cardsLayout->setColumnStretch(c, 1);
}

void PipelinePage::setState(const TraceReplay *replay, qint64 cycle,
                            qint64 selected) {
  m_replay = replay;
  m_cycle = cycle;
  m_selected = selected;
  rebuildCards(replay);
  if (!replay) {
    m_cycleLabel->setText(tr("No trace recorded yet"));
    m_statsLabel->setText(
        tr("Load a program (Missions or Run page) and press Clock or Run: "
           "every cycle is recorded and can be replayed here."));
    m_events->clear();
    m_eventsCard->setVisible(false);
    m_cycleBar->setRange(0, 0);
    m_cycleBar->setCycle(0);
    return;
  }
  m_eventsCard->setVisible(true);
  const auto &t = replay->trace();
  m_cycleBar->setRange(replay->firstCycle(), replay->lastCycle());
  m_cycleBar->setCycle(cycle);
  m_cycleLabel->setText(tr("Cycle %1 of %2–%3")
                            .arg(cycle)
                            .arg(replay->firstCycle())
                            .arg(replay->lastCycle()));
  const auto st = replay->stats();
  m_statsLabel->setText(
      tr("Whole trace: %1 retired · CPI %2 · %3 stall cycles · %4 squashed")
          .arg(st.retired)
          .arg(st.retired ? QString::number(st.cpi, 'f', 2) : QString("–"))
          .arg(st.stallCycles)
          .arg(st.squashed));

  const auto &stages = t.header.stages;
  const auto pipeline = replay->pipelineAt(cycle);
  std::set<unsigned> lanes;
  for (const auto &sd : stages)
    lanes.insert(sd.lane);
  for (size_t i = 0; i < stages.size() && i < m_cards.size(); ++i) {
    SlotVisual v;
    v.stage = stages[i].name;
    v.colorIndex = static_cast<int>(stages[i].index);
    if (i < pipeline.size()) {
      const auto &slot = pipeline[i];
      v.kind = slot.kind;
      v.held = slot.held;
      v.occupants = slot.occupants;
      if (slot.kind == TraceReplay::SlotKind::Instruction) {
        const auto *inst = replay->instance(slot.instance);
        v.instance = slot.instance;
        v.assembly = inst ? inst->assembly : Trace::instanceName(slot.instance);
        v.wrongPath = inst && inst->outcome == TraceReplay::Outcome::Squashed;
        v.selected = slot.instance == selected;
      }
    }
    m_cards[i]->setVisual(
        v, lanes.size() > 1 ? tr("lane %1").arg(stages[i].lane) : QString());
  }
  if (stages.empty())
    m_hint->setText(tr("This trace has no pipeline stage information (the "
                       "model is not pipelined). The instructions recorded "
                       "in this cycle are listed below."));
  else
    retranslate();

  // Events of the cycle, as recorded.
  QString html;
  const auto indices = replay->eventsAt(cycle);
  const auto &events = t.events;
  const QString muted = tokens().muted.name();
  for (size_t idx : indices) {
    const auto &ev = events[idx];
    QString line =
        "<b>" + Trace::eventTypeName(ev.type).toHtmlEscaped() + "</b>";
    if (ev.instance >= 0)
      line += " " + Trace::instanceName(ev.instance) + " <tt>" +
              ev.assembly.toHtmlEscaped() + "</tt>";
    line += QString(" <span style='color:%1'>%2 · %3</span>")
                .arg(muted, ev.component.section('.', -1).toHtmlEscaped(),
                     Trace::observabilityName(ev.observability));
    html += line + "<br>";
  }
  if (indices.empty())
    html = "<span style='color:" + muted + "'>" +
           tr("No events in this cycle.") + "</span>";
  m_events->setText(html);
}

// ============================================================================
// ExplainPage
// ============================================================================

ExplainPage::ExplainPage(QWidget *parent) : QWidget(parent) {
  setObjectName("mobileExplainPage");
  auto *v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  auto *bar = new QWidget(this);
  bar->setObjectName("mobileSubBar");
  auto *bl = new QHBoxLayout(bar);
  bl->setContentsMargins(8, 4, 8, 4);
  m_previous = navButton("◀", bar);
  m_previous->setObjectName("mobilePreviousInstruction");
  m_previous->setAutoRepeat(false);
  m_next = navButton("▶", bar);
  m_next->setObjectName("mobileNextInstruction");
  m_next->setAutoRepeat(false);
  m_title = makeLabel(bar, false);
  m_title->setObjectName("mobileSubBarTitle");
  m_title->setAlignment(Qt::AlignCenter);
  bl->addWidget(m_previous);
  bl->addWidget(m_title, 1);
  bl->addWidget(m_next);
  v->addWidget(bar);
  m_browser = new QTextBrowser(this);
  m_browser->setObjectName("mobileExplanation");
  m_browser->setOpenLinks(false);
  m_browser->setFrameShape(QFrame::NoFrame);
  m_browser->setLineWrapMode(QTextEdit::WidgetWidth);
  m_browser->setTextInteractionFlags(Qt::NoTextInteraction);
  m_browser->document()->setDocumentMargin(14);
  enableKineticScrolling(m_browser, true);
  v->addWidget(m_browser, 1);
  connect(m_previous, &QToolButton::clicked, this, [this] { step(-1); });
  connect(m_next, &QToolButton::clicked, this, [this] { step(1); });
  render();
}

void ExplainPage::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    render();
}

QString ExplainPage::title() const { return m_title->text(); }

void ExplainPage::setState(const TraceReplay *replay, qint64 selected) {
  const bool changed = replay != m_replay || selected != m_selected;
  m_replay = replay;
  m_selected = selected;
  if (changed)
    render();
}

void ExplainPage::step(int delta) {
  if (!m_replay || m_replay->instances().empty())
    return;
  const auto &instances = m_replay->instances();
  auto it = instances.find(m_selected);
  if (it == instances.end()) {
    emit instanceSelected(delta > 0 ? instances.begin()->first
                                    : instances.rbegin()->first);
    return;
  }
  if (delta > 0) {
    ++it;
    if (it != instances.end())
      emit instanceSelected(it->first);
  } else if (it != instances.begin()) {
    --it;
    emit instanceSelected(it->first);
  }
}

void ExplainPage::render() {
  const bool have = m_replay && !m_replay->instances().empty();
  const auto *inst = m_replay ? m_replay->instance(m_selected) : nullptr;
  m_previous->setEnabled(have);
  m_next->setEnabled(have);
  if (!inst) {
    m_title->setText(tr("No instruction selected"));
    m_browser->setHtml(
        "<p>" +
        tr("Tap an instruction on the Pipeline page, or use ◀ ▶ above to "
           "walk through the recorded instructions. The explanation shows "
           "what the instruction does, its operand and result values as "
           "recorded by the simulator, its hazards and its journey through "
           "the pipeline.") +
        "</p>" +
        (have ? QString()
              : "<p><i>" + tr("No instructions have been recorded yet.") +
                    "</i></p>"));
    return;
  }
  m_title->setText(Trace::instanceName(m_selected) + "  " + inst->assembly);
  const int pos = m_browser->verticalScrollBar()->value();
  m_browser->setHtml(
      explanationToHtml(explainInstance(*m_replay, m_selected), tokens().dark));
  Q_UNUSED(pos);
  m_browser->verticalScrollBar()->setValue(0);
}

// ============================================================================
// RegistersPage
// ============================================================================

RegistersPage::RegistersPage(QWidget *parent) : QWidget(parent) {
  setObjectName("mobileRegistersPage");
  auto *v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  m_title = makeLabel(this);
  m_title->setObjectName("mobileSubBarLabel");
  v->addWidget(m_title);
  m_table = new QTableWidget(this);
  m_table->setObjectName("mobileRegisterTable");
  m_table->setColumnCount(3);
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(kTouchTarget);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  m_table->setColumnWidth(0, 96);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionMode(QAbstractItemView::NoSelection);
  m_table->setFocusPolicy(Qt::NoFocus);
  m_table->setShowGrid(false);
  m_table->setAlternatingRowColors(true);
  m_table->setFrameShape(QFrame::NoFrame);
  m_table->setWordWrap(false);
  enableKineticScrolling(m_table);
  v->addWidget(m_table, 1);
  render();
}

void RegistersPage::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    render();
}

void RegistersPage::setState(const TraceReplay *replay, qint64 cycle) {
  m_replay = replay;
  m_cycle = cycle;
  render();
}

void RegistersPage::render() {
  m_table->setHorizontalHeaderLabels(
      {tr("Register"), tr("Value (hex)"), tr("Decimal")});
  if (!m_replay) {
    m_title->setText(tr("No trace recorded yet: registers are shown for the "
                        "recorded cycles."));
    m_table->setRowCount(0);
    return;
  }
  m_title->setText(
      tr("Registers at cycle %1 (replayed from the recorded trace). Values "
         "written in this cycle are highlighted.")
          .arg(m_cycle));
  const auto &h = m_replay->trace().header;
  const auto now = m_replay->registersAt(m_cycle);
  const auto before = m_cycle > m_replay->firstCycle()
                          ? m_replay->registersAt(m_cycle - 1)
                          : now;
  int rows = 0;
  for (const auto &rf : h.registerFiles)
    rows += rf.names.size();
  m_table->setRowCount(rows);
  const QColor changedColor = withAlpha(tokens().warn, 90);
  const QFont mono =
      monoFont(font().pointSizeF() > 0 ? font().pointSizeF() * 1.12 : 12);
  int row = 0;
  for (const auto &rf : h.registerFiles) {
    const auto itNow = now.find(rf.name);
    const auto itBefore = before.find(rf.name);
    for (int i = 0; i < rf.names.size(); ++i, ++row) {
      quint64 val = 0, prev = 0;
      if (itNow != now.end() && static_cast<size_t>(i) < itNow->second.size())
        val = itNow->second[i];
      if (itBefore != before.end() &&
          static_cast<size_t>(i) < itBefore->second.size())
        prev = itBefore->second[i];
      const bool changed = val != prev;
      const bool known = m_replay->registerKnownAt(rf.name, i, m_cycle);
      const QString alias = rf.aliases.value(i);
      const QStringList cells = {
          alias.isEmpty() ? rf.names[i] : rf.names[i] + " " + alias,
          known ? Trace::toHex(val, h.xlen) : tr("unknown"),
          known ? signedString(val, h.xlen) : QString("–")};
      for (int c = 0; c < cells.size(); ++c) {
        auto *item = new QTableWidgetItem(cells[c]);
        item->setFont(mono);
        if (changed) {
          item->setBackground(changedColor);
          QFont f = mono;
          f.setBold(true);
          item->setFont(f);
        }
        m_table->setItem(row, c, item);
      }
    }
  }
}

} // namespace Mobile
} // namespace Ripes
