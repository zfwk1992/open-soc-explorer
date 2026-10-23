// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: test of the phone user interface (src/mobile)
// driven by simulated touches. Set OSX_SCREENSHOT_DIR to save screenshots of
// every page in portrait (390x844) and landscape (844x390).
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointingDevice>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTextBrowser>
#include <QtTest/QTest>

#include "explorer/mission.h"
#include "explorer/missionprogress.h"
#include "mobile/mobilemissions.h"
#include "mobile/mobilerun.h"
#include "mobile/mobilesettings.h"
#include "mobile/mobileshell.h"
#include "mobile/mobileviews.h"
#include "mobile/touch.h"
#include "platform/uimode.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "theme/theme.h"

using namespace Ripes;
using namespace Ripes::Mobile;

namespace {
const char *const kIsolatedSettings[] = {
    RIPES_SETTING_OSX_MISSION_STEPS, RIPES_SETTING_OSX_MISSIONS_COMPLETED,
    RIPES_SETTING_OSX_THEME,         RIPES_SETTING_OSX_REDUCED_MOTION,
    RIPES_SETTING_COLORSCHEME,       RIPES_SETTING_OSX_LANGUAGE,
    RIPES_SETTING_SOURCECODE};
} // namespace

class tst_mobilegui : public QObject {
  Q_OBJECT

private:
  MobileShell *m_shell = nullptr;
  QPointingDevice *m_touch = nullptr;
  std::map<QString, QVariant> m_savedSettings;

  /// A tap of one finger at @p pos (shell coordinates).
  void tap(const QPoint &pos);
  void tap(QWidget *w);
  /// A finger drag from @p from to @p to in @p steps moves.
  void swipe(const QPoint &from, const QPoint &to, int steps = 12);
  void tapNav(int page);
  /// Scrolls @p w into view in its scroll page (by swiping), then taps it.
  void tapInPage(QWidget *w);
  QStringList stepStatuses() const;
  void runToEndByTaps();
  void screenshots(const QString &suffix);
  void checkFits(const QString &context);

private slots:
  void initTestCase();
  void tst_uiModeSelection();
  void tst_kineticScrolling();
  void tst_layoutFitsPortrait();
  void tst_navigationByTouch();
  void tst_swipeScrollsLongPages();
  void tst_mission1CompletedByTaps();
  void tst_pipelineExplainRegisters();
  void tst_languageAndTheme();
  void tst_layoutFitsLandscape();
  void tst_screenshots();
  void cleanupTestCase();
};

void tst_mobilegui::initTestCase() {
  RipesSettings::setValue(RIPES_SETTING_OSX_TRACE_ENABLED, true);
  for (const char *key : kIsolatedSettings)
    m_savedSettings[key] = RipesSettings::value(key);
  RipesSettings::setValue(RIPES_SETTING_OSX_MISSION_STEPS, QStringList());
  RipesSettings::setValue(RIPES_SETTING_OSX_MISSIONS_COMPLETED, QStringList());
  RipesSettings::setValue(RIPES_SETTING_OSX_THEME, 0);
  RipesSettings::setValue(RIPES_SETTING_OSX_REDUCED_MOTION, false);
  RipesSettings::setValue(RIPES_SETTING_COLORSCHEME,
                          static_cast<int>(ColorScheme::Dark));
  RipesSettings::setValue(RIPES_SETTING_OSX_LANGUAGE, "en");
  RipesSettings::setValue(RIPES_SETTING_SOURCECODE, QString());
  QApplication::setStyle("Fusion");
  applyColorScheme();
  Mobile::prepareApplication(*qApp);
  m_touch = QTest::createTouchDevice();
  m_shell = new MobileShell();
  m_shell->resize(390, 844);
  m_shell->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_shell));
  QTest::qWait(100);
}

void tst_mobilegui::cleanupTestCase() {
  delete m_shell;
  for (const auto &[key, value] : m_savedSettings)
    RipesSettings::setValue(key, value);
}

void tst_mobilegui::tap(const QPoint &pos) {
  QTest::touchEvent(m_shell, m_touch).press(0, pos, m_shell);
  QTest::qWait(30);
  QTest::touchEvent(m_shell, m_touch).release(0, pos, m_shell);
  QTest::qWait(120);
}

void tst_mobilegui::tap(QWidget *w) {
  QVERIFY(w && w->isVisible());
  tap(w->mapTo(m_shell, w->rect().center()));
}

void tst_mobilegui::swipe(const QPoint &from, const QPoint &to, int steps) {
  QTest::touchEvent(m_shell, m_touch).press(0, from, m_shell);
  QTest::qWait(16);
  for (int i = 1; i <= steps; ++i) {
    const QPoint p = from + (to - from) * i / steps;
    QTest::touchEvent(m_shell, m_touch).move(0, p, m_shell);
    QTest::qWait(16);
  }
  QTest::touchEvent(m_shell, m_touch).release(0, to, m_shell);
  QTest::qWait(50);
}

void tst_mobilegui::tapNav(int page) {
  auto *nav = m_shell->navBar();
  const QRect r = nav->tabRect(page);
  QVERIFY(r.height() >= kTouchTarget && r.width() >= kTouchTarget);
  tap(nav->mapTo(m_shell, r.center()));
  QTRY_COMPARE(m_shell->currentPage(), page);
}

void tst_mobilegui::tapInPage(QWidget *w) {
  QVERIFY(w);
  // Find the scroll area that contains the widget and swipe until the widget
  // is fully visible in its viewport.
  QScrollArea *area = nullptr;
  for (QWidget *p = w->parentWidget(); p && !area; p = p->parentWidget())
    area = qobject_cast<QScrollArea *>(p);
  QVERIFY(area);
  QWidget *vp = area->viewport();
  for (int i = 0; i < 30; ++i) {
    const QRect r(w->mapTo(vp, QPoint(0, 0)), w->size());
    if (r.top() >= 0 && r.bottom() <= vp->height())
      break;
    const QPoint c = vp->mapTo(m_shell, vp->rect().center());
    const int d = std::min(vp->height() / 3, 220);
    if (r.top() < 0)
      swipe(c - QPoint(0, d / 2), c + QPoint(0, d / 2), 24);
    else
      swipe(c + QPoint(0, d / 2), c - QPoint(0, d / 2), 24);
    // Let the kinetic scroll come to rest (a tap would only stop it).
    QTRY_COMPARE_WITH_TIMEOUT(QScroller::scroller(vp)->state(),
                              QScroller::Inactive, 5000);
  }
  const QRect r(w->mapTo(vp, QPoint(0, 0)), w->size());
  QVERIFY2(
      r.top() >= 0 && r.bottom() <= vp->height(),
      qPrintable(QString("%1 not scrolled into view").arg(w->objectName())));
  tap(w);
}

QStringList tst_mobilegui::stepStatuses() const {
  QStringList s;
  for (auto *l :
       m_shell->missionsPage()->findChildren<QLabel *>("mobileStepStatus"))
    s << l->text();
  return s;
}

void tst_mobilegui::runToEndByTaps() {
  tapNav(MobileShell::RunPageId);
  tap(m_shell->runPage()->runButton());
  QTRY_VERIFY_WITH_TIMEOUT(!ProcessorHandler::isRunning(), 60000);
  QTRY_VERIFY_WITH_TIMEOUT(ProcessorHandler::getProcessor()->finished(), 5000);
  QTest::qWait(300);
}

void tst_mobilegui::checkFits(const QString &context) {
  // No widget of the visible page reaches beyond the screen's width, and
  // every visible button is a large touch target.
  QWidget *page = m_shell->findChild<QWidget *>(
      QString(), Qt::FindDirectChildrenOnly); // header
  Q_UNUSED(page);
  for (auto *w : m_shell->findChildren<QWidget *>()) {
    if (!w->isVisible() || w->isWindow())
      continue;
    // Item views paint their own content; their headers and viewports may
    // be wider (deliberate horizontal lists are not used on the pages).
    bool inCombo = false;
    for (QWidget *p = w; p; p = p->parentWidget())
      if (qobject_cast<QComboBox *>(p))
        inCombo = true;
    if (inCombo)
      continue;
    const QRect r(w->mapTo(m_shell, QPoint(0, 0)), w->size());
    QVERIFY2(r.right() <= m_shell->width() + 1,
             qPrintable(QString("%1: %2 (%3) is wider than the screen: %4..%5")
                            .arg(context, w->metaObject()->className(),
                                 w->objectName())
                            .arg(r.left())
                            .arg(r.right())));
    if (auto *b = qobject_cast<QAbstractButton *>(w))
      QVERIFY2(b->height() >= kTouchTarget,
               qPrintable(QString("%1: button '%2' is %3 px high")
                              .arg(context, b->text())
                              .arg(b->height())));
  }
}

void tst_mobilegui::tst_uiModeSelection() {
  QCOMPARE(uiModeFromArguments({"Ripes", "--ui", "mobile"}), UiMode::Mobile);
  QCOMPARE(uiModeFromArguments({"Ripes", "--ui=desktop"}), UiMode::Desktop);
  QCOMPARE(uiModeFromArguments({"Ripes", "--mode", "gui"}),
           std::optional<UiMode>());
  QCOMPARE(uiModeFromQuery("?ui=mobile"), UiMode::Mobile);
  QCOMPARE(uiModeFromQuery("?lang=zh&ui=desktop"), UiMode::Desktop);
  QCOMPARE(uiModeFromQuery("ui=MOBILE"), UiMode::Mobile);
  QCOMPARE(uiModeFromQuery("?ui=tablet"), std::optional<UiMode>());
  QCOMPARE(uiModeFromQuery(""), std::optional<UiMode>());
  // Default: the desktop main window (this test is started without --ui).
  QCOMPARE(currentUiMode(), UiMode::Desktop);
  setUiMode(UiMode::Mobile);
  QCOMPARE(currentUiMode(), UiMode::Mobile);
  QCOMPARE(uiModeName(UiMode::Mobile), QString("mobile"));
  QVERIFY(!canOpenUiModePage()); // native build: no browser page to open
}

void tst_mobilegui::tst_kineticScrolling() {
  // Every scrollable view can be scrolled by dragging a finger, except the
  // program editor, which grows with its text inside the scrolling page.
  int count = 0;
  for (auto *area : m_shell->findChildren<QAbstractScrollArea *>()) {
    bool inCombo = false;
    for (QWidget *p = area; p; p = p->parentWidget())
      if (qobject_cast<QComboBox *>(p))
        inCombo = true;
    if (inCombo || qobject_cast<QHeaderView *>(area) ||
        area->objectName() == "mobileProgramEditor")
      continue;
    QVERIFY2(hasKineticScrolling(area),
             qPrintable(QString("%1 (%2) has no touch scrolling")
                            .arg(area->metaObject()->className(),
                                 area->objectName())));
    // The explanation may contain wide tables: deliberately horizontal.
    if (area->objectName() != "mobileExplanation")
      QCOMPARE(area->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    ++count;
  }
  QVERIFY(count >= 7);
}

void tst_mobilegui::tst_layoutFitsPortrait() {
  for (int page = 0; page < MobileShell::NPages; ++page) {
    m_shell->setCurrentPage(page);
    QTest::qWait(50);
    checkFits(QString("portrait %1").arg(MobileShell::pageKey(page)));
  }
  m_shell->missionsPage()->openMission(1);
  QTest::qWait(50);
  m_shell->setCurrentPage(MobileShell::MissionsPageId);
  checkFits("portrait mission detail");
  m_shell->missionsPage()->showMap();
  m_shell->setCurrentPage(MobileShell::MissionsPageId);
}

void tst_mobilegui::tst_navigationByTouch() {
  for (int page : {MobileShell::RunPageId, MobileShell::PipelinePageId,
                   MobileShell::ExplainPageId, MobileShell::RegistersPageId,
                   MobileShell::SettingsPageId, MobileShell::MissionsPageId})
    tapNav(page);
  // The layout description used by the browser test.
  const auto layout = m_shell->describeLayout();
  QCOMPARE(layout.value("page").toString(), QString("missions"));
  QCOMPARE(layout.value("nav").toArray().size(), int(MobileShell::NPages));
  QVERIFY(layout.value("targets").toObject().contains("missions.card0"));
  QVERIFY(layout.value("scroll").toObject().value("kinetic").toBool());
}

void tst_mobilegui::tst_swipeScrollsLongPages() {
  // Mission detail: a long page; a finger swipe upwards scrolls it.
  tap(m_shell->missionsPage()->missionCardWidget(1));
  QTRY_VERIFY(m_shell->missionsPage()->showingDetail());
  auto *bar = m_shell->missionsPage()
                  ->currentScrollPage()
                  ->scrollArea()
                  ->verticalScrollBar();
  QVERIFY(bar->maximum() > 300);
  QCOMPARE(bar->value(), 0);
  swipe(QPoint(195, 650), QPoint(195, 250));
  QTRY_VERIFY(bar->value() > 150);
  QTest::qWait(600);
  const int after = bar->value();
  // And back down.
  swipe(QPoint(195, 250), QPoint(195, 650));
  QTRY_VERIFY(bar->value() < after);
  QTest::qWait(600);
  // A tap on a card does not scroll; the back button returns to the map.
  tap(m_shell->missionsPage()->findChild<QAbstractButton *>(
      "mobileBackButton"));
  QTRY_VERIFY(!m_shell->missionsPage()->showingDetail());

  // Registers: 32+ rows of 44 px, longer than the screen.
  tapNav(MobileShell::RegistersPageId);
  auto *table = m_shell->registersPage()->table();
  if (table->rowCount() > 0) {
    auto *tb = table->verticalScrollBar();
    const int before = tb->value();
    const QPoint c =
        table->viewport()->mapTo(m_shell, table->viewport()->rect().center());
    swipe(c + QPoint(0, 200), c - QPoint(0, 200));
    QTRY_VERIFY(tb->value() > before);
    QTest::qWait(600);
  }
  tapNav(MobileShell::MissionsPageId);
}

void tst_mobilegui::tst_mission1CompletedByTaps() {
  auto *missions = m_shell->missionsPage();
  const auto catalog = Explorer::loadBuiltinMissions();
  tap(missions->missionCardWidget(0));
  QTRY_COMPARE(missions->currentMission(), 0);
  const auto *previous = ProcessorHandler::getProgram().get();
  tapInPage(missions->findChild<QAbstractButton *>("mobileLoadExample"));
  QTRY_VERIFY(ProcessorHandler::getProgram().get() != previous);
  QVERIFY(m_shell->runPage()->source().contains("addi x1, x0, 10"));
  tapInPage(missions->findChild<QAbstractButton *>("mobileSelectProcessor"));
  QTRY_COMPARE(enumToString<ProcessorID>(ProcessorHandler::getID()),
               QString("RV32_SS"));
  QTRY_COMPARE(stepStatuses().value(0), QString("✓"));

  // One clock on the Run page completes "addi x1, x0, 10".
  tapNav(MobileShell::RunPageId);
  tap(m_shell->runPage()->clockButton());
  QTRY_VERIFY_WITH_TIMEOUT(
      ProcessorHandler::getProcessor()->getCycleCount() == 1, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(stepStatuses().value(1), QString("✓"), 3000);
  // A toast reports the verified step.
  QTRY_VERIFY(m_shell->toastText().contains("verified"));
  // Step back undoes the cycle in the simulator.
  tap(m_shell->runPage()->backButton());
  QTRY_COMPARE(ProcessorHandler::getProcessor()->getCycleCount(), 0u);
  runToEndByTaps();
  QTRY_COMPARE_WITH_TIMEOUT(stepStatuses(), QStringList({"✓", "✓", "✓"}), 3000);
  QCOMPARE(RipesSettings::value(RIPES_SETTING_OSX_MISSION_STEPS).toStringList(),
           QStringList({"m1-first-program/1", "m1-first-program/2",
                        "m1-first-program/3"}));

  // The example does not solve the challenge.
  tapNav(MobileShell::MissionsPageId);
  tapInPage(missions->findChild<QAbstractButton *>("mobileCheckChallenge"));
  QVERIFY(missions->findChild<QLabel *>("mobileMissionResult")
              ->text()
              .contains("Not solved yet"));

  // Type a solution on the (virtual) keyboard: tap the editor, replace the
  // program line by line.
  tapNav(MobileShell::RunPageId);
  auto *editor = m_shell->runPage()->editor();
  tapInPage(editor);
  QTRY_VERIFY(editor->hasFocus());
  editor->selectAll();
  QTest::keyClick(editor, Qt::Key_Delete);
  for (const QString &line : catalog[0].referenceSolution.split('\n')) {
    QTest::keyClicks(editor, line);
    QTest::keyClick(editor, Qt::Key_Return);
  }
  m_shell->runPage()->assembleNow();
  QVERIFY(m_shell->runPage()->assemblerStatus().contains("Assembled"));
  runToEndByTaps();
  tapNav(MobileShell::MissionsPageId);
  tapInPage(missions->findChild<QAbstractButton *>("mobileCheckChallenge"));
  QVERIFY(missions->findChild<QLabel *>("mobileMissionResult")
              ->text()
              .contains("Challenge solved"));
  // Stars and XP only from verified progress: 3 steps + the challenge.
  const auto totals = missions->progress().totals(missions->missions());
  QCOMPARE(totals.xp, 3 * Explorer::kXpPerStep + Explorer::kXpPerChallenge);
  QCOMPARE(totals.stars, 3);
  QCOMPARE(
      RipesSettings::value(RIPES_SETTING_OSX_MISSIONS_COMPLETED).toStringList(),
      QStringList({"m1-first-program"}));
}

void tst_mobilegui::tst_pipelineExplainRegisters() {
  auto *missions = m_shell->missionsPage();
  tapNav(MobileShell::MissionsPageId);
  if (missions->showingDetail())
    tap(missions->findChild<QAbstractButton *>("mobileBackButton"));
  tapInPage(missions->missionCardWidget(2));
  QTRY_COMPARE(missions->currentMission(), 2);
  tapInPage(missions->findChild<QAbstractButton *>("mobileLoadExample"));
  tapInPage(missions->findChild<QAbstractButton *>("mobileSelectProcessor"));
  QTRY_COMPARE(enumToString<ProcessorID>(ProcessorHandler::getID()),
               QString("RV32_5S"));
  runToEndByTaps();

  tapNav(MobileShell::PipelinePageId);
  QTRY_VERIFY(m_shell->replay() && m_shell->replay()->lastCycle() > 10);
  auto *pipeline = m_shell->pipelinePage();
  QCOMPARE(pipeline->cards().size(), size_t(5));
  QCOMPARE(m_shell->cycle(), m_shell->replay()->lastCycle());
  // Back to cycle 7 with the ◀ button; the stage cards follow the trace.
  const qint64 last = m_shell->cycle();
  for (qint64 c = last; c > 7; --c)
    tap(pipeline->cycleBar()->previousButton());
  QTRY_COMPARE(m_shell->cycle(), qint64(7));
  QVERIFY(pipeline->cycleText().contains("Cycle 7"));
  const auto occupancy = m_shell->replay()->pipelineAt(7);
  for (size_t i = 0; i < occupancy.size(); ++i)
    QCOMPARE(pipeline->cards()[i]->visual().instance,
             occupancy[i].kind == Explorer::TraceReplay::SlotKind::Instruction
                 ? occupancy[i].instance
                 : qint64(-1));
  // Tapping an instruction opens its explanation.
  StageCard *card = nullptr;
  for (auto *c : pipeline->cards())
    if (c->visual().instance >= 0 && !card)
      card = c;
  QVERIFY(card);
  const qint64 id = card->visual().instance;
  tapInPage(card);
  QTRY_COMPARE(m_shell->currentPage(), int(MobileShell::ExplainPageId));
  QCOMPARE(m_shell->selectedInstance(), id);
  QVERIFY(m_shell->explainPage()->title().contains(
      m_shell->replay()->instance(id)->assembly));
  QVERIFY(!m_shell->explainPage()->browser()->toPlainText().isEmpty());
  // ▶ walks to the next recorded instruction.
  tap(m_shell->explainPage()->findChild<QAbstractButton *>(
      "mobileNextInstruction"));
  QTRY_VERIFY(m_shell->selectedInstance() > id);

  // Registers at cycle 7, from the replay.
  tapNav(MobileShell::RegistersPageId);
  auto *table = m_shell->registersPage()->table();
  QCOMPARE(table->rowCount(), 32);
  QCOMPARE(table->item(0, 0)->text(), QString("x0 zero"));
}

void tst_mobilegui::tst_languageAndTheme() {
  tapNav(MobileShell::SettingsPageId);
  auto *settings = m_shell->settingsPage();
  tapInPage(settings->findChild<QAbstractButton *>("mobileLanguageChinese"));
  QTRY_COMPARE(m_shell->navBar()->tabText(MobileShell::MissionsPageId),
               QString("任务"));
  screenshots("-zh");
  tapInPage(settings->findChild<QAbstractButton *>("mobileLanguageEnglish"));
  QTRY_COMPARE(m_shell->navBar()->tabText(MobileShell::MissionsPageId),
               QString("Missions"));
  // Light color scheme and the classic theme apply to the phone pages too.
  tapInPage(settings->findChild<QAbstractButton *>("mobileSchemeLight"));
  QTRY_VERIFY(!Theme::tokens().dark);
  tapInPage(settings->findChild<QAbstractButton *>("mobileThemeClassic"));
  QTRY_COMPARE(Theme::kind(), Theme::Kind::Classic);
  tapInPage(settings->findChild<QAbstractButton *>("mobileThemeGame"));
  QTRY_COMPARE(Theme::kind(), Theme::Kind::Game);
  screenshots("-light");
  tapInPage(settings->findChild<QAbstractButton *>("mobileSchemeDark"));
  QTRY_VERIFY(Theme::tokens().dark);
  tapInPage(settings->findChild<QAbstractButton *>("mobileReducedMotion"));
  QTRY_VERIFY(Theme::reducedMotion());
  tapInPage(settings->findChild<QAbstractButton *>("mobileReducedMotion"));
  QTRY_VERIFY(!Theme::reducedMotion());
  // Native build: the switch asks for the desktop main window.
  QSignalSpy spy(m_shell, &MobileShell::switchToDesktopRequested);
  tapInPage(settings->desktopButton());
  QTRY_COMPARE(spy.count(), 1);
}

void tst_mobilegui::tst_layoutFitsLandscape() {
  m_shell->resize(844, 390);
  QTest::qWait(100);
  for (int page = 0; page < MobileShell::NPages; ++page) {
    m_shell->setCurrentPage(page);
    QTest::qWait(50);
    checkFits(QString("landscape %1").arg(MobileShell::pageKey(page)));
  }
  tapNav(MobileShell::RunPageId);
  tapNav(MobileShell::MissionsPageId);
  m_shell->resize(390, 844);
  QTest::qWait(100);
}

void tst_mobilegui::screenshots(const QString &suffix) {
  const QString dir = qEnvironmentVariable("OSX_SCREENSHOT_DIR");
  if (dir.isEmpty())
    return;
  QDir().mkpath(dir);
  const int page = m_shell->currentPage();
  for (int p = 0; p < MobileShell::NPages; ++p) {
    m_shell->setCurrentPage(p);
    QTest::qWait(120);
    const QString name = QString("%1/mobile-%2x%3-%4%5.png")
                             .arg(dir)
                             .arg(m_shell->width())
                             .arg(m_shell->height())
                             .arg(MobileShell::pageKey(p), suffix);
    QVERIFY(m_shell->grab().save(name));
  }
  m_shell->setCurrentPage(page);
}

void tst_mobilegui::tst_screenshots() {
  if (qEnvironmentVariable("OSX_SCREENSHOT_DIR").isEmpty())
    QSKIP("set OSX_SCREENSHOT_DIR to save screenshots");
  auto *missions = m_shell->missionsPage();
  for (const QSize size : {QSize(390, 844), QSize(844, 390)}) {
    m_shell->resize(size);
    QTest::qWait(150);
    missions->showMap();
    screenshots("");
    missions->openMission(2);
    m_shell->setCurrentPage(MobileShell::MissionsPageId);
    QTest::qWait(150);
    const QString dir = qEnvironmentVariable("OSX_SCREENSHOT_DIR");
    QVERIFY(m_shell->grab().save(QString("%1/mobile-%2x%3-mission3.png")
                                     .arg(dir)
                                     .arg(size.width())
                                     .arg(size.height())));
  }
  m_shell->resize(390, 844);
}

int main(int argc, char **argv) {
  // Default to the offscreen platform so that the test behaves the same on
  // every operating system and in CI.
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTEST_SET_MAIN_SOURCE_PATH
  tst_mobilegui test;
  return QTest::qExec(&test, argc, argv);
}

#include "tst_mobilegui.moc"
