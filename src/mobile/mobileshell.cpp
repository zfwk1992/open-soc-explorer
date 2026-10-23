// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "mobileshell.h"

#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QStyleHints>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolButton>

#include <algorithm>

#include "explorer/modelprofile.h"
#include "explorer/tracerecorder.h"
#include "explorer/tracetext.h"
#include "i18n/language.h"
#include "mainwindow.h"
#include "mobilemissions.h"
#include "mobilerun.h"
#include "mobilesettings.h"
#include "mobileviews.h"
#include "platform/uimode.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "syscall/systemio.h"
#include "theme/painting.h"
#include "theme/theme.h"
#include "touch.h"

#ifdef Q_OS_WASM
#include <emscripten/val.h>
#endif

namespace Ripes {
namespace Mobile {

using namespace Explorer;
using namespace Theme;

namespace {

const char *const kPageNames[] = {
    QT_TRANSLATE_NOOP("Ripes::Mobile::MobileShell", "Missions"),
    QT_TRANSLATE_NOOP("Ripes::Mobile::MobileShell", "Run"),
    QT_TRANSLATE_NOOP("Ripes::Mobile::MobileShell", "Pipeline"),
    QT_TRANSLATE_NOOP("Ripes::Mobile::MobileShell", "Explain"),
    QT_TRANSLATE_NOOP("Ripes::Mobile::MobileShell", "Registers"),
    QT_TRANSLATE_NOOP("Ripes::Mobile::MobileShell", "Settings")};

const char *const kPageKeys[] = {"missions", "run",       "pipeline",
                                 "explain",  "registers", "settings"};

QFont scaledFont(const QFont &f, qreal factor, bool bold = false) {
  QFont s = f;
  if (f.pointSizeF() > 0)
    s.setPointSizeF(f.pointSizeF() * factor);
  s.setBold(bold);
  return s;
}

QString firstSentence(const QString &text) {
  const int zh = text.indexOf(QChar(0x3002)); // "。"
  QString note = zh >= 0 ? text.left(zh) : text.section(". ", 0, 0);
  note = note.trimmed();
  if (note.endsWith('.'))
    note.chop(1);
  return note;
}

QJsonObject rectJson(const QRect &r) {
  return QJsonObject{{"x", r.x()},           {"y", r.y()},
                     {"w", r.width()},       {"h", r.height()},
                     {"cx", r.center().x()}, {"cy", r.center().y()}};
}

/// The phone style sheet: large touch targets, readable text. Colors from
/// the active theme's tokens (game or classic).
QString mobileStyleSheet(const Tokens &t) {
  QString s = R"(
#mobileShell { background: @window; }
QWidget#mobilePageContent { background: @window; }
QScrollArea { background: @window; border: none; }
#mobileHeader { background: @panel; border-bottom: 1px solid @border; }
QLabel#mobileHeaderTitle { color: @text; font-weight: 800; }
QLabel#mobileTitle { color: @text; font-weight: 700; }
QLabel#mobileOverline { color: @accent; font-weight: 700; }
QLabel#mobileMuted, QLabel#mobileStepChecks { color: @muted; }
QLabel#mobileWarnNote { background: @warnTint; border: 1px solid @warn; border-radius: 12px; padding: 10px; color: @text; }
QLabel#mobileSubBarLabel { background: @panel; color: @muted; border-bottom: 1px solid @border; padding: 8px 12px; }
#mobileSubBar { background: @panel; border-bottom: 1px solid @border; }
QLabel#mobileSubBarTitle { color: @text; font-weight: 700; }
#mobileControlBar, #mobileCycleBar { background: @panel; border-top: 1px solid @border; }
QPushButton { min-height: 46px; border-radius: 12px; padding: 4px 10px; font-weight: 600;
  background: @raised; color: @text; border: 1px solid @borderStrong; }
QPushButton:pressed { background: @accentTint; }
QPushButton:disabled { color: @mutedA; background: @panel; border-color: @border; }
QPushButton[osxRole="primary"] { background: @accent; color: @accentText; border: 1px solid @accentDark; }
QPushButton[osxRole="gold"] { background: @gold; color: #2a1d00; border: 1px solid @goldDark; }
QPushButton[osxRole="danger"] { background: @bad; color: #ffffff; border: 1px solid @bad; }
QPushButton[osxRole="primary"]:disabled, QPushButton[osxRole="gold"]:disabled { background: @panel; color: @mutedA; border-color: @border; }
QPushButton:checked { background: @accentTint; border-color: @accent; }
QToolButton#mobileBackButton { color: @accent; font-weight: 700; border: none; background: transparent; padding: 4px 8px; }
QToolButton#mobileIconButton { min-width: 48px; min-height: 48px; border-radius: 12px; border: 1px solid @borderStrong;
  background: @raised; color: @text; font-weight: 700; }
QToolButton#mobileIconButton:pressed { background: @accentTint; }
QToolButton#mobileIconButton:disabled { color: @mutedA; background: @panel; border-color: @border; }
QComboBox { min-height: 46px; padding: 2px 12px; border-radius: 12px; border: 1px solid @borderStrong; background: @raised; color: @text; }
QComboBox QAbstractItemView { background: @card; color: @text; selection-background-color: @accentTint; selection-color: @text; }
QComboBox QAbstractItemView::item { min-height: 46px; padding: 0 10px; }
QRadioButton, QCheckBox { min-height: 44px; color: @text; spacing: 12px; }
QRadioButton::indicator, QCheckBox::indicator { width: 24px; height: 24px; }
QSlider { min-height: 44px; }
QSlider::groove:horizontal { height: 8px; border-radius: 4px; background: @base; border: 1px solid @border; }
QSlider::sub-page:horizontal { height: 8px; border-radius: 4px; background: @accent; }
QSlider::handle:horizontal { width: 30px; height: 30px; margin: -12px 0; border-radius: 15px; background: @accent; border: 2px solid @card; }
QFrame#mobileStep { border: 1px solid @border; border-radius: 12px; background: @base; }
QFrame#mobileStep[state="done"] { border: 1px solid @ok; }
QFrame#mobileStep[state="current"] { border: 2px solid @accent; }
QLabel#mobileStepStatus { font-weight: 800; color: @muted; }
QLabel#mobileStepStatus[state="done"] { color: @ok; }
QLabel#mobileStepStatus[state="current"] { color: @accent; }
QPlainTextEdit#mobileProgramEditor { background: @base; color: @text; border: 1px solid @borderStrong; border-radius: 10px; padding: 6px; }
QTextBrowser#mobileExplanation { background: @base; color: @text; border: none; }
QTableWidget#mobileRegisterTable { background: @base; alternate-background-color: @alternate; color: @text; border: none; gridline-color: @border; }
QTableWidget#mobileRegisterTable QHeaderView::section { background: @panel; color: @muted; border: none; border-bottom: 1px solid @border; padding: 8px 6px; font-weight: 700; }
QLabel#mobileToast { background: @card; color: @text; border: 2px solid @gold; border-radius: 14px; padding: 10px 14px; font-weight: 600; }
QScrollBar:vertical { width: 4px; background: transparent; margin: 0; }
QScrollBar::handle:vertical { background: @mutedA; border-radius: 2px; min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)";
  const auto rgba = [](const QColor &c) {
    return QString("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alpha());
  };
  const std::vector<std::pair<QString, QString>> vars = {
      {"@window", t.window.name()},
      {"@panel", t.panel.name()},
      {"@card", t.card.name()},
      {"@base", t.base.name()},
      {"@alternate", t.alternate.name()},
      {"@raised", t.raised.name()},
      {"@borderStrong", t.borderStrong.name()},
      {"@border", t.border.name()},
      {"@text", t.text.name()},
      {"@mutedA", rgba(withAlpha(t.muted, 150))},
      {"@muted", t.muted.name()},
      {"@accentTint", rgba(withAlpha(t.accent, t.dark ? 60 : 40))},
      {"@accentText", t.accentText.name()},
      {"@accentDark", t.accent.darker(t.dark ? 150 : 130).name()},
      {"@accent", t.accent.name()},
      {"@goldDark", t.gold.darker(140).name()},
      {"@gold", t.gold.name()},
      {"@warnTint", rgba(withAlpha(t.warn, t.dark ? 40 : 30))},
      {"@warn", t.warn.name()},
      {"@ok", t.ok.name()},
      {"@bad", t.bad.name()}};
  for (const auto &[k, v] : vars)
    s.replace(k, v);
  return s;
}

} // namespace

// ============================================================================
// BadgeStrip
// ============================================================================

BadgeStrip::BadgeStrip(QWidget *parent) : QWidget(parent) {
  setObjectName("mobileBadgeStrip");
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void BadgeStrip::setBadges(const std::vector<HudBanner::Badge> &badges) {
  m_badges = badges;
  QStringList parts;
  for (const auto &b : badges)
    parts << b.label + ": " + b.value;
  setAccessibleName(parts.join(" · "));
  setToolTip(parts.join("\n"));
  update();
}

QSize BadgeStrip::sizeHint() const { return QSize(340, 26); }
QSize BadgeStrip::minimumSizeHint() const { return QSize(100, 26); }

void BadgeStrip::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QFont f = scaledFont(font(), 0.78, true);
  qreal x = 0;
  const qreal right = width();
  for (const auto &b : m_badges) {
    QString text = b.value;
    QSizeF s = pillSize(text, f);
    if (x + s.width() > right) {
      // Elide the last pill that is still partly visible.
      const qreal avail = right - x - 22;
      if (avail < 40)
        break;
      text = QFontMetricsF(f).elidedText(text, Qt::ElideMiddle, avail);
      s = pillSize(text, f);
    }
    drawPill(p, QPointF(x, (height() - s.height()) / 2), text, b.color, f);
    x += s.width() + 5;
    if (x >= right)
      break;
  }
}

// ============================================================================
// MobileShell
// ============================================================================

MobileShell::MobileShell(QWidget *parent) : QWidget(parent) {
  setObjectName("mobileShell");
  setAttribute(Qt::WA_StyledBackground);
  setWindowTitle(tr("Open SoC Explorer (based on Ripes)"));
  setWindowIcon(QIcon(":/icons/logo.svg"));
  // The view translates itself.
  setProperty("osxOwnTranslation", true);

  Theme::install(nullptr);
  I18n::install(nullptr);
  ProcessorHandler::get();
  QFontDatabase::addApplicationFont(
      ":/fonts/Inconsolata/Inconsolata-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/Inconsolata/Inconsolata-Bold.ttf");

  m_recorder = new TraceRecorder(this);
  m_recorder->setMaxCycles(
      RipesSettings::value(RIPES_SETTING_OSX_TRACE_MAXCYCLES).toLongLong());
  m_recorder->setEnabled(
      RipesSettings::value(RIPES_SETTING_OSX_TRACE_ENABLED).toBool());

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  m_header = new QWidget(this);
  m_header->setObjectName("mobileHeader");
  m_header->setAttribute(Qt::WA_StyledBackground);
  m_headerLayout = new QBoxLayout(QBoxLayout::TopToBottom, m_header);
  m_headerLayout->setContentsMargins(14, 6, 12, 6);
  m_headerLayout->setSpacing(2);
  m_title = new QLabel(m_header);
  m_title->setObjectName("mobileHeaderTitle");
  m_title->setFont(scaledFont(font(), 1.3, true));
  m_badgeStrip = new BadgeStrip(m_header);
  m_headerLayout->addWidget(m_title);
  m_headerLayout->addWidget(m_badgeStrip, 1);
  root->addWidget(m_header);

  m_stack = new QStackedWidget(this);
  root->addWidget(m_stack, 1);
  m_missions = new MissionsPage(
      m_recorder, [this] { return m_run ? m_run->source() : QString(); },
      m_stack);
  m_run = new RunPage(m_stack);
  m_pipeline = new PipelinePage(m_stack);
  m_explain = new ExplainPage(m_stack);
  m_registers = new RegistersPage(m_stack);
  m_settings = new SettingsPage(m_stack);
  for (QWidget *w : std::initializer_list<QWidget *>{
           m_missions, m_run, m_pipeline, m_explain, m_registers, m_settings})
    m_stack->addWidget(w);

  m_nav = new NavBar(this);
  const NavIcon icons[] = {NavIcon::Missions,  NavIcon::Run,
                           NavIcon::Pipeline,  NavIcon::Explain,
                           NavIcon::Registers, NavIcon::Settings};
  for (int i = 0; i < NPages; ++i)
    m_nav->addTab(icons[i], tr(kPageNames[i]));
  root->addWidget(m_nav);
  connect(m_nav, &NavBar::currentChanged, this, [this](int index) {
    m_stack->setCurrentIndex(index);
    updateHeader();
    if (index == PipelinePageId || index == ExplainPageId ||
        index == RegistersPageId || index == RunPageId)
      refreshReplay();
    publishLayout();
  });

  m_toast = new QLabel(this);
  m_toast->setObjectName("mobileToast");
  m_toast->setWordWrap(true);
  m_toast->setAlignment(Qt::AlignCenter);
  // Shown over the top of the page; taps go through to the page.
  m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_toast->hide();
  m_toastTimer.setSingleShot(true);
  m_toastTimer.setInterval(3200);
  connect(&m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);

  // Missions drive the program, the processor and the Run page.
  connect(m_missions, &MissionsPage::loadProgramRequested, this,
          [this](const QString &program) {
            m_run->setSource(program);
            showToast(tr("Example program loaded"));
          });
  connect(m_missions, &MissionsPage::selectProcessorRequested, this,
          [this](ProcessorID id) {
            RunPage::selectProcessor(id);
            showToast(tr("Processor: %1")
                          .arg(ProcessorRegistry::getDescription(id).name));
          });
  connect(m_missions, &MissionsPage::openRunRequested, this,
          [this] { setCurrentPage(RunPageId); });
  connect(m_missions, &MissionsPage::progressMessage, this,
          &MobileShell::showToast);
  connect(m_missions, &MissionsPage::currentMissionChanged, this,
          [this] { updateMissionContext(); });
  connect(m_run, &RunPage::loadMissionExampleRequested, this, [this] {
    const int i = m_missions->lastMission();
    if (i >= 0) {
      m_run->setSource(m_missions->missions()[i].example);
      showToast(tr("Example program loaded"));
    }
  });
  connect(m_run, &RunPage::selectMissionProcessorRequested, this, [this] {
    const int i = m_missions->lastMission();
    if (i < 0)
      return;
    bool ok = false;
    const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
        m_missions->missions()[i].processor.toUtf8().constData(), &ok);
    if (ok) {
      RunPage::selectProcessor(static_cast<ProcessorID>(v));
      showToast(tr("Processor: %1")
                    .arg(ProcessorRegistry::getDescription(
                             static_cast<ProcessorID>(v))
                             .name));
    }
  });

  connect(m_pipeline, &PipelinePage::cycleRequested, this,
          &MobileShell::setCycle);
  connect(m_pipeline, &PipelinePage::instanceActivated, this,
          [this](qint64 id) {
            selectInstance(id);
            setCurrentPage(ExplainPageId);
          });
  connect(m_explain, &ExplainPage::instanceSelected, this,
          &MobileShell::selectInstance);

  connect(m_settings, &SettingsPage::switchToDesktopRequested, this, [this] {
    if (!openUiModePage(UiMode::Desktop))
      emit switchToDesktopRequested();
  });
  connect(m_settings, &SettingsPage::resetProgressRequested, this, [this] {
    m_missions->resetProgress();
    showToast(tr("Mission progress reset"));
  });

  // Simulator and trace updates (throttled by ProcessorHandler).
  m_refreshTimer.setSingleShot(true);
  m_refreshTimer.setInterval(30);
  connect(&m_refreshTimer, &QTimer::timeout, this, &MobileShell::refreshReplay);
  auto *handler = ProcessorHandler::get();
  for (auto signal : {&ProcessorHandler::procStateChangedNonRun,
                      &ProcessorHandler::runFinished})
    connect(handler, signal, this, [this] { m_refreshTimer.start(); });
  connect(handler, &ProcessorHandler::processorReset, this,
          [] { SystemIO::reset(); });
  connect(
      m_recorder, &TraceRecorder::runStarted, this,
      [this] { m_refreshTimer.start(); }, Qt::QueuedConnection);
  connect(Notifier::get(), &Notifier::changed, this, [this] {
    applyStyle();
    updateHeader();
    updateViews();
  });
  // The persisted color scheme (also changed on the Settings page).
  connect(RipesSettings::getObserver(RIPES_SETTING_COLORSCHEME),
          &SettingObserver::modified, this, [] { applyColorScheme(); });

  // Scroll positions and layout for the browser test.
  for (int i = 0; i < NPages; ++i)
    if (auto *page = scrollPageOf(i))
      connect(page->scrollArea()->verticalScrollBar(),
              &QScrollBar::valueChanged, this, [this] { publishLayout(); });
  for (QAbstractScrollArea *area :
       {static_cast<QAbstractScrollArea *>(m_explain->browser()),
        static_cast<QAbstractScrollArea *>(m_registers->table())})
    connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { publishLayout(); });
  // WebAssembly: republished regularly, so that the browser test always
  // reads the current geometry (e.g. after a page finished its layout).
  m_publishTimer.setInterval(300);
  connect(&m_publishTimer, &QTimer::timeout, this, &MobileShell::publishLayout);

  applyStyle();
  retranslate();
  refreshReplay();
  updateMissionContext();
  setCurrentPage(MissionsPageId);
#ifdef Q_OS_WASM
  m_publishTimer.start();
#endif
}

MobileShell::~MobileShell() = default;

QString MobileShell::pageKey(int page) {
  return page >= 0 && page < NPages ? kPageKeys[page] : QString();
}

int MobileShell::currentPage() const { return m_stack->currentIndex(); }

void MobileShell::setCurrentPage(int page) {
  if (page == m_nav->currentIndex()) {
    m_stack->setCurrentIndex(page);
    updateHeader();
    publishLayout();
    return;
  }
  m_nav->setCurrentIndex(page);
}

void MobileShell::applyStyle() { setStyleSheet(mobileStyleSheet(tokens())); }

void MobileShell::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    retranslate();
}

void MobileShell::retranslate() {
  setWindowTitle(tr("Open SoC Explorer (based on Ripes)"));
  for (int i = 0; i < NPages; ++i)
    m_nav->setTabText(i, tr(kPageNames[i]));
  updateHeader();
  updateViews();
  updateMissionContext();
}

void MobileShell::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  // Landscape: title and badges share one row to leave room for content.
  const bool wide = width() >= 600;
  m_headerLayout->setDirection(wide ? QBoxLayout::LeftToRight
                                    : QBoxLayout::TopToBottom);
  m_headerLayout->setSpacing(wide ? 14 : 2);
  m_headerLayout->setContentsMargins(14, wide ? 4 : 6, 12, wide ? 4 : 6);
  if (m_toast->isVisible())
    showToast(m_toast->text());
  publishLayout();
}

void MobileShell::showToast(const QString &text) {
  m_toast->setText(text);
  const int w = std::min(width() - 32, 420);
  m_toast->setFixedWidth(w);
  m_toast->adjustSize();
  m_toast->move((width() - w) / 2, m_header->geometry().bottom() + 10);
  m_toast->raise();
  m_toast->show();
  m_toastTimer.start();
}

QString MobileShell::toastText() const {
  return m_toast->isVisible() ? m_toast->text() : QString();
}

void MobileShell::updateMissionContext() {
  const int i = m_missions->lastMission();
  if (i >= 0)
    m_run->setMission(m_missions->missions()[i].number,
                      m_missions->missions()[i].title);
  else
    m_run->setMission(-1, QString());
}

void MobileShell::refreshReplay() {
  const quint64 revision = m_recorder->revision();
  if (m_replay && revision == m_revision) {
    updateHeader();
    return;
  }
  const bool atEnd = !m_replay || m_cycle >= m_replay->lastCycle();
  m_revision = revision;
  m_replay = std::make_unique<TraceReplay>(m_recorder->trace());
  if (m_selected >= 0 && !m_replay->instance(m_selected))
    m_selected = -1;
  // Follow the simulator unless the learner moved back in time.
  m_cycle = atEnd ? m_replay->lastCycle()
                  : std::clamp(m_cycle, m_replay->firstCycle(),
                               m_replay->lastCycle());
  updateHeader();
  updateViews();
}

void MobileShell::setCycle(qint64 cycle) {
  if (!m_replay)
    return;
  cycle = std::clamp(cycle, m_replay->firstCycle(), m_replay->lastCycle());
  if (cycle == m_cycle)
    return;
  m_cycle = cycle;
  updateHeader();
  updateViews();
}

void MobileShell::selectInstance(qint64 id) {
  m_selected = id;
  updateViews();
}

void MobileShell::updateViews() {
  const TraceReplay *r = m_replay.get();
  m_pipeline->setState(r, m_cycle, m_selected);
  m_explain->setState(r, m_selected);
  m_registers->setState(r, m_cycle);
  publishLayout();
}

void MobileShell::updateHeader() {
  const int page = currentPage();
  m_title->setText(page >= 0 && page < NPages ? tr(kPageNames[page])
                                              : QString());
  const auto &tk = tokens();
  std::vector<HudBanner::Badge> compact;
  std::vector<HudBanner::Badge> full;
  QString note;
  if (m_replay) {
    const auto &t = m_replay->trace();
    const auto &s = t.header.source;
    const auto &c = t.completeness;
    QString completeness;
    if (c.truncated)
      completeness = tr("truncated (%1)").arg(c.truncationReason);
    else if (c.programFinished)
      completeness =
          c.startsAtReset ? tr("complete") : tr("finished, started mid-run");
    else
      completeness = tr("in progress");
    QColor statusColor = tk.info;
    if (c.truncated)
      statusColor = tk.bad;
    else if (c.programFinished)
      statusColor = c.startsAtReset ? tk.ok : tk.warn;
    const QString mode =
        m_recorder->isEnabled() ? tr("Live trace") : tr("Recording disabled");
    const QString fidelityNote = localizedTraceText(s.fidelityNote);
    full = {
        {tr("Engine"), (s.engine + " " + s.engineVersion).trimmed(), tk.accent,
         QString()},
        {tr("Model"), QString("%1 (%2)").arg(s.modelName, s.model), tk.accent2,
         s.sourceFile},
        {tr("Fidelity"), s.fidelity, fidelityColor(s.fidelity), fidelityNote},
        {tr("Clock domain"), t.header.clockDomain, tk.info, QString()},
        {tr("Trace"), mode + " · " + completeness, statusColor, QString()}};
    if (t.header.stages.empty())
      full.push_back(
          {tr("Pipeline"), tr("no stage information"), tk.muted, QString()});
    compact = {
        {tr("Model"), s.model, tk.accent2, QString()},
        {tr("Fidelity"), s.fidelity, fidelityColor(s.fidelity), QString()},
        {tr("Engine"), s.engine, tk.accent, QString()},
        {tr("Cycle"), tr("cycle %1").arg(m_cycle), tk.info, QString()}};
    note = firstSentence(fidelityNote);
  } else {
    const auto profile = modelProfile(ProcessorHandler::getID());
    compact = {{tr("Model"), profile.idName, tk.accent2, QString()},
               {tr("Fidelity"), profile.fidelity,
                fidelityColor(profile.fidelity), QString()}};
    full = compact;
  }
  m_badgeStrip->setBadges(compact);
  m_run->setBadges(full, note.isEmpty() ? QString() : "ⓘ " + note);
}

ScrollPage *MobileShell::scrollPageOf(int page) const {
  switch (page) {
  case MissionsPageId:
    return m_missions->currentScrollPage();
  case RunPageId:
    return m_run->scrollPage();
  case PipelinePageId:
    return m_pipeline->scrollPage();
  case SettingsPageId:
    return m_settings->scrollPage();
  default:
    break;
  }
  return nullptr;
}

void MobileShell::publishLayout() {
#ifdef Q_OS_WASM
  const QByteArray json =
      QJsonDocument(describeLayout()).toJson(QJsonDocument::Compact);
  emscripten::val::global("window").set("osxMobileLayout",
                                        emscripten::val(json.toStdString()));
#endif
}

QJsonObject MobileShell::describeLayout() const {
  QJsonObject o;
  const int page = currentPage();
  o["page"] = pageKey(page);
  o["width"] = width();
  o["height"] = height();
  o["cycle"] = m_cycle;
  o["selected"] = m_selected;
  o["language"] = I18n::languageCode(I18n::current());
  o["missionDetail"] = m_missions->showingDetail();
  o["mission"] = m_missions->currentMission();
  QJsonArray nav;
  for (int i = 0; i < m_nav->count(); ++i) {
    QJsonObject tab =
        rectJson(QRect(m_nav->mapTo(this, m_nav->tabRect(i).topLeft()),
                       m_nav->tabRect(i).size()));
    tab["name"] = pageKey(i);
    tab["text"] = m_nav->tabText(i);
    nav.append(tab);
  }
  o["nav"] = nav;

  // Scroll position of the visible page.
  QAbstractScrollArea *area = nullptr;
  if (auto *sp = scrollPageOf(page))
    area = sp->scrollArea();
  else if (page == ExplainPageId)
    area = m_explain->browser();
  else if (page == RegistersPageId)
    area = m_registers->table();
  if (area) {
    QJsonObject scroll = rectJson(QRect(
        area->viewport()->mapTo(this, QPoint(0, 0)), area->viewport()->size()));
    scroll["value"] = area->verticalScrollBar()->value();
    scroll["maximum"] = area->verticalScrollBar()->maximum();
    scroll["kinetic"] = hasKineticScrolling(area);
    o["scroll"] = scroll;
  }

  // Main touch targets that are currently visible on the screen.
  QJsonObject targets;
  auto add = [&](const QString &name, const QWidget *w) {
    if (!w || !w->isVisible())
      return;
    QRect r(w->mapTo(this, QPoint(0, 0)), w->size());
    r = r.intersected(m_stack->geometry().united(m_nav->geometry()));
    if (r.height() < 8 || r.width() < 8)
      return;
    QJsonObject t = rectJson(r);
    t["enabled"] = w->isEnabled();
    targets[name] = t;
  };
  if (page == MissionsPageId) {
    for (int i = 0; i < int(m_missions->missions().size()); ++i)
      add(QString("missions.card%1").arg(i), m_missions->missionCardWidget(i));
    add("missions.back",
        m_missions->findChild<QToolButton *>("mobileBackButton"));
    add("missions.loadExample",
        m_missions->findChild<QAbstractButton *>("mobileLoadExample"));
    add("missions.selectProcessor",
        m_missions->findChild<QAbstractButton *>("mobileSelectProcessor"));
    add("missions.checkChallenge",
        m_missions->findChild<QAbstractButton *>("mobileCheckChallenge"));
  } else if (page == RunPageId) {
    add("run.reset", m_run->resetButton());
    add("run.back", m_run->backButton());
    add("run.clock", m_run->clockButton());
    add("run.run", m_run->runButton());
    add("run.editor", m_run->editor());
  } else if (page == PipelinePageId) {
    const auto &cards = m_pipeline->cards();
    for (size_t i = 0; i < cards.size(); ++i)
      add(QString("pipeline.stage%1").arg(i), cards[i]);
    add("pipeline.previous", m_pipeline->cycleBar()->previousButton());
    add("pipeline.next", m_pipeline->cycleBar()->nextButton());
    add("pipeline.slider", m_pipeline->cycleBar()->slider());
  } else if (page == ExplainPageId) {
    add("explain.previous",
        m_explain->findChild<QToolButton *>("mobilePreviousInstruction"));
    add("explain.next",
        m_explain->findChild<QToolButton *>("mobileNextInstruction"));
  } else if (page == SettingsPageId) {
    add("settings.english",
        m_settings->findChild<QWidget *>("mobileLanguageEnglish"));
    add("settings.chinese",
        m_settings->findChild<QWidget *>("mobileLanguageChinese"));
    add("settings.desktop", m_settings->desktopButton());
  }
  o["targets"] = targets;
  return o;
}

// ============================================================================
// Startup
// ============================================================================

void prepareApplication(QApplication &app) {
  // Readable text on phones: ~15 CSS px body text.
  QFont f = app.font();
  f.setPointSizeF(11.5);
  app.setFont(f);
  // Touches that no widget handles arrive as mouse events (Qt's default),
  // which the kinetic scrollers and the buttons use.
  QApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents,
                             true);
  QApplication::setAttribute(Qt::AA_SynthesizeTouchForUnhandledMouseEvents,
                             false);
  // No double clicks: a press that follows a tap within the double-click
  // interval is delivered as a double click only, which the kinetic
  // scrollers ignore, so a quick swipe after a tap would not scroll.
  QGuiApplication::styleHints()->setMouseDoubleClickInterval(1);
}

int runMobileShell(QApplication &app) {
  prepareApplication(app);
  auto *shell = new MobileShell();
  shell->setAttribute(Qt::WA_DeleteOnClose);
#ifdef Q_OS_WASM
  // Full screen in the browser page; the page itself does not scroll.
  shell->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  shell->showMaximized();
#else
  shell->resize(390, 844);
  shell->show();
  QObject::connect(shell, &MobileShell::switchToDesktopRequested, &app,
                   [shell] {
                     setUiMode(UiMode::Desktop);
                     auto *window = new MainWindow();
                     window->setAttribute(Qt::WA_DeleteOnClose);
                     window->restoreWindowGeometry();
                     window->show();
                     shell->close();
                   });
#endif
  return app.exec();
}

} // namespace Mobile
} // namespace Ripes
