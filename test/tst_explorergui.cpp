// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: end-to-end test of the Explorer and Missions tabs
// in the real main window. Set OSX_SCREENSHOT_DIR to save screenshots.
#include <QApplication>
#include <QDir>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest/QTest>

#include "edittab.h"
#include "explorer/mission.h"
#include "explorer/missionprogress.h"
#include "explorertab.h"
#include "explorerwidgets.h"
#include "fancytabbar/fancytabbar.h"
#include "gem5sample.h"
#include "mainwindow.h"
#include "missionstab.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "systemview.h"
#include "theme/theme.h"

using namespace Ripes;

namespace {
// Indices of the tabs in MainWindow (see MainWindow::TabIndex).
constexpr int kEditorTab = 0;
constexpr int kProcessorTab = 1;
constexpr int kExplorerTab = 5;
constexpr int kMissionsTab = 6;
// Data role of the mission map cards (MissionsTab).
constexpr int kCardRole = Qt::UserRole + 1;
// Settings the test changes; restored in cleanupTestCase().
const char *const kIsolatedSettings[] = {
    RIPES_SETTING_OSX_MISSION_STEPS, RIPES_SETTING_OSX_MISSIONS_COMPLETED,
    RIPES_SETTING_OSX_THEME, RIPES_SETTING_OSX_REDUCED_MOTION,
    RIPES_SETTING_COLORSCHEME};
} // namespace

class tst_explorergui : public QObject {
  Q_OBJECT

private:
  void activateTab(int index);
  void click(QWidget *parent, const QString &text);
  void waitForNewProgram(const Program *previous);
  void loadMission(int row);
  void runToEnd();
  void runFast();
  QStringList stepStatuses() const;
  QVariantMap missionCard(int row) const;
  QStringList stripMoves() const;
  void screenshot(const QString &name);

  MainWindow *m_window = nullptr;
  MissionsTab *m_missions = nullptr;
  ExplorerTab *m_explorer = nullptr;
  std::vector<Explorer::Mission> m_catalog;
  std::map<QString, QVariant> m_savedSettings;

private slots:
  void initTestCase();
  void tst_mission1Flow();
  void tst_explorerReplaysTrace();
  void tst_componentInspector();
  void tst_mission3ChallengeSolved();
  void tst_mission4CacheChallenge();
  void tst_systemViewRipes();
  void tst_gem5TraceInExplorer();
  void tst_themes();
  void cleanupTestCase();
};

void tst_explorergui::initTestCase() {
  RipesSettings::setValue(RIPES_SETTING_OSX_TRACE_ENABLED, true);
  // Start without mission progress, in the default (game) theme, with
  // animations on. The user's values are restored in cleanupTestCase().
  for (const char *key : kIsolatedSettings)
    m_savedSettings[key] = RipesSettings::value(key);
  RipesSettings::setValue(RIPES_SETTING_OSX_MISSION_STEPS, QStringList());
  RipesSettings::setValue(RIPES_SETTING_OSX_MISSIONS_COMPLETED, QStringList());
  RipesSettings::setValue(RIPES_SETTING_OSX_THEME, 0);
  RipesSettings::setValue(RIPES_SETTING_OSX_REDUCED_MOTION, false);
  RipesSettings::setValue(RIPES_SETTING_COLORSCHEME,
                          static_cast<int>(ColorScheme::System));
  m_window = new MainWindow();
  m_window->resize(1680, 1000);
  m_window->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_window));
  m_missions = m_window->findChild<MissionsTab *>();
  m_explorer = m_window->findChild<ExplorerTab *>();
  QVERIFY(m_missions && m_explorer);
  m_catalog = Explorer::loadBuiltinMissions();
  QCOMPARE(m_catalog.size(), size_t(4));
}

void tst_explorergui::cleanupTestCase() {
  delete m_window;
  for (const auto &[key, value] : m_savedSettings)
    RipesSettings::setValue(key, value);
}

QVariantMap tst_explorergui::missionCard(int row) const {
  return m_missions->findChild<QListWidget *>("missionList")
      ->item(row)
      ->data(kCardRole)
      .toMap();
}

QStringList tst_explorergui::stripMoves() const {
  auto *strip = m_explorer->findChild<PipelineStrip *>();
  const auto &tiles = strip->tiles();
  auto stage = [&](int i) { return tiles.at(i)->visual().stage; };
  QStringList moves;
  for (const auto &m : strip->moves()) {
    const QString inst = Trace::instanceName(m.visual.instance);
    switch (m.kind) {
    case PipelineStrip::Move::Slide:
      moves << QString("slide %1->%2 %3").arg(stage(m.from), stage(m.to), inst);
      break;
    case PipelineStrip::Move::Enter:
      moves << QString("enter %1 %2").arg(stage(m.to), inst);
      break;
    case PipelineStrip::Move::Exit:
      moves << QString("exit %1 %2 %3")
                   .arg(stage(m.from), inst,
                        m.retired ? "retired" : "squashed");
      break;
    }
  }
  moves.sort();
  return moves;
}

void tst_explorergui::activateTab(int index) {
  m_window->findChild<FancyTabBar *>()->setActiveIndex(index);
  QTest::qWait(50);
}

void tst_explorergui::click(QWidget *parent, const QString &text) {
  for (auto *b : parent->findChildren<QPushButton *>())
    if (b->text() == text && b->isVisible()) {
      QTest::mouseClick(b, Qt::LeftButton);
      QTest::qWait(50);
      return;
    }
  QFAIL(qPrintable("button not found: " + text));
}

void tst_explorergui::waitForNewProgram(const Program *previous) {
  QTRY_VERIFY_WITH_TIMEOUT(ProcessorHandler::getProgram() &&
                               ProcessorHandler::getProgram().get() != previous,
                           5000);
  QTest::qWait(100);
}

void tst_explorergui::loadMission(int row) {
  activateTab(kMissionsTab);
  m_missions->findChild<QListWidget *>("missionList")->setCurrentRow(row);
  const auto *previous = ProcessorHandler::getProgram().get();
  click(m_missions, "Load example");
  waitForNewProgram(previous);
  activateTab(kMissionsTab);
  click(m_missions, "Select processor");
  QTest::qWait(200);
  QCOMPARE(enumToString<ProcessorID>(ProcessorHandler::getID()),
           m_catalog[row].processor);
}

void tst_explorergui::runFast() {
  // Like the toolbar's Run button: simulates without per-cycle GUI updates.
  ProcessorHandler::run();
  QTRY_VERIFY_WITH_TIMEOUT(!ProcessorHandler::isRunning(), 60000);
  QTRY_VERIFY_WITH_TIMEOUT(ProcessorHandler::getProcessor()->finished(), 5000);
  QTest::qWait(250);
}

void tst_explorergui::runToEnd() {
  auto *proc = ProcessorHandler::getProcessorNonConst();
  while (!proc->finished() && proc->getCycleCount() < 5000)
    proc->clock();
  QTest::qWait(250); // let the throttled GUI update signals arrive
}

QStringList tst_explorergui::stepStatuses() const {
  QStringList statuses;
  for (auto *l : m_missions->findChildren<QLabel *>("missionStepStatus"))
    if (l->isVisible())
      statuses << l->text();
  return statuses;
}

void tst_explorergui::screenshot(const QString &name) {
  const QString dir = qEnvironmentVariable("OSX_SCREENSHOT_DIR");
  if (dir.isEmpty())
    return;
  QTest::qWait(Theme::kTransitionMs + 120); // let transitions finish
  QDir().mkpath(dir);
  QVERIFY(m_window->grab().save(dir + "/" + name + ".png"));
}

void tst_explorergui::tst_mission1Flow() {
  loadMission(0);
  activateTab(kMissionsTab);
  QTRY_COMPARE(stepStatuses().value(0), QString("✓"));
  QVERIFY(stepStatuses().value(1) != "✓");

  // One clock of the single-cycle processor completes "addi x1, x0, 10".
  ProcessorHandler::getProcessorNonConst()->clock();
  QTRY_COMPARE_WITH_TIMEOUT(stepStatuses().value(1), QString("✓"), 2000);

  runToEnd();
  QTRY_COMPARE_WITH_TIMEOUT(stepStatuses(), QStringList({"✓", "✓", "✓"}), 2000);
  screenshot("missions-mission1");

  // The verified steps are persisted and are all that is scored so far: two
  // stars (half and all of the steps); Mission 2 unlocks, Mission 3 not yet.
  QCOMPARE(RipesSettings::value(RIPES_SETTING_OSX_MISSION_STEPS).toStringList(),
           QStringList({"m1-first-program/1", "m1-first-program/2",
                        "m1-first-program/3"}));
  QCOMPARE(missionCard(0).value("stars").toInt(), 2);
  QCOMPARE(missionCard(0).value("status").toInt(),
           int(Explorer::MissionStatus::InProgress));
  QCOMPARE(missionCard(1).value("status").toInt(),
           int(Explorer::MissionStatus::Available));
  QCOMPARE(missionCard(2).value("status").toInt(),
           int(Explorer::MissionStatus::Locked));

  // The example does not solve the challenge.
  click(m_missions, "Check challenge");
  QVERIFY(m_missions->findChild<QLabel *>("missionResult")
              ->text()
              .contains("Not solved yet"));
}

void tst_explorergui::tst_explorerReplaysTrace() {
  activateTab(kExplorerTab);
  auto *timeline = m_explorer->findChild<QTableView *>("explorerTimeline");
  QTRY_COMPARE(timeline->model()->rowCount(), 3);
  QCOMPARE(timeline->model()->columnCount(), 4); // cycles 0..3

  const QString banner =
      m_explorer->findChild<QLabel *>("explorerBanner")->text();
  QVERIFY(banner.contains("ripes"));
  QVERIFY(banner.contains("RV32_SS"));
  QVERIFY(banner.contains("educational-model"));
  QVERIFY(banner.contains("complete"));

  m_explorer->selectInstance(3);
  m_explorer->setCycle(2);
  const QString html =
      m_explorer->findChild<QTextBrowser *>("explorerExplanation")
          ->toPlainText();
  QVERIFY(html.contains("add x3 x1 x2"));
  QVERIFY(html.contains("0x0000001e"));
  QVERIFY(html.contains("✓"));
  screenshot("explorer-mission1");

  // Stepping backwards reconstructs the earlier register state.
  m_explorer->setCycle(1);
  auto *regs = m_explorer->findChild<QTableWidget *>("explorerRegisters");
  QCOMPARE(regs->item(1, 2)->text(), QString("0x0000000a")); // x1
  // x3 is the ABI global pointer (gp); Ripes initializes it to 0x10000000.
  QCOMPARE(regs->item(3, 2)->text(), QString("0x10000000"));
  m_explorer->setCycle(3);
  QCOMPARE(regs->item(3, 2)->text(), QString("0x0000001e"));
}

void tst_explorergui::tst_componentInspector() {
  loadMission(1); // 5-stage processor
  auto *proc = ProcessorHandler::getProcessorNonConst();
  for (int i = 0; i < 4; ++i)
    proc->clock();
  QTest::qWait(250);
  activateTab(kExplorerTab);
  m_explorer->findChild<QTabWidget *>("explorerSideTabs")->setCurrentIndex(2);
  auto *tree = m_explorer->findChild<QTreeWidget *>("explorerComponents");
  QTRY_VERIFY(tree->topLevelItemCount() > 10);
  const auto items = tree->findItems("alu", Qt::MatchExactly);
  QCOMPARE(items.size(), qsizetype(1));
  tree->setCurrentItem(items.first());
  const QString detail =
      m_explorer->findChild<QTextBrowser *>("explorerComponentDetail")
          ->toPlainText();
  QVERIFY(detail.contains("Arithmetic logic unit"));
  QVERIFY(detail.contains("Inputs"));
  QVERIFY(detail.contains("Instruction in EX (cycle 4)"));
  QVERIFY(detail.contains("rv_alu.h"));
  screenshot("explorer-components");

  // Mission 2 is now in progress on the mission map.
  activateTab(kMissionsTab);
  QTRY_COMPARE(stepStatuses().value(0), QString("✓"));
  QCOMPARE(missionCard(1).value("status").toInt(),
           int(Explorer::MissionStatus::InProgress));
  screenshot("missions-mission2-progress");
}

void tst_explorergui::tst_mission3ChallengeSolved() {
  loadMission(2);
  runToEnd();
  activateTab(kExplorerTab);
  m_explorer->findChild<QTabWidget *>("explorerSideTabs")->setCurrentIndex(3);
  QTRY_VERIFY(m_explorer->findChild<QTextBrowser *>("explorerStatistics")
                  ->toPlainText()
                  .contains(QRegularExpression("Squashed instructions\\s*18")));
  m_explorer->findChild<QTabWidget *>("explorerSideTabs")->setCurrentIndex(0);
  m_explorer->setCycle(7);
  m_explorer->selectInstance(6); // first squashed instruction
  QVERIFY(m_explorer->findChild<QTextBrowser *>("explorerExplanation")
              ->toPlainText()
              .contains("squashed"));
  screenshot("explorer-mission3-flush");

  // Stepping one cycle plays a short transition made only of what the trace
  // records between cycles 6 and 7: stage changes, the fetch of inst-8, the
  // retirement of inst-3 and the squash of inst-6 and inst-7.
  auto *strip = m_explorer->findChild<PipelineStrip *>();
  m_explorer->setCycle(6);
  QTest::qWait(Theme::kTransitionMs + 150);
  m_explorer->setCycle(7);
  QVERIFY(strip->isAnimating());
  QCOMPARE(stripMoves(),
           QStringList({"enter IF inst-8", "exit ID inst-6 squashed",
                        "exit IF inst-7 squashed", "exit WB inst-3 retired",
                        "slide EX->MEM inst-5", "slide MEM->WB inst-4"}));
  QTRY_VERIFY(!strip->isAnimating());
  // No transition with reduced motion, nor for jumps of several cycles.
  RipesSettings::setValue(RIPES_SETTING_OSX_REDUCED_MOTION, true);
  m_explorer->setCycle(6);
  QTest::qWait(Theme::kTransitionMs + 150);
  m_explorer->setCycle(7);
  QVERIFY(!strip->isAnimating());
  RipesSettings::setValue(RIPES_SETTING_OSX_REDUCED_MOTION, false);
  QTest::qWait(Theme::kTransitionMs + 150);
  m_explorer->setCycle(9);
  QVERIFY(!strip->isAnimating());

  // Solve the challenge with the reference solution.
  activateTab(kEditorTab);
  const auto *previous = ProcessorHandler::getProgram().get();
  m_window->findChild<EditTab *>()->loadSourceText(
      m_catalog[2].referenceSolution);
  waitForNewProgram(previous);
  runToEnd();
  activateTab(kMissionsTab);
  click(m_missions, "Check challenge");
  const QString result =
      m_missions->findChild<QLabel *>("missionResult")->text();
  QVERIFY(result.contains("Challenge solved"));
  QVERIFY(result.contains(QString("+%1 XP").arg(Explorer::kXpPerChallenge)));
  QVERIFY(m_missions->findChild<QListWidget *>("missionList")
              ->item(2)
              ->text()
              .startsWith("✓"));
  QCOMPARE(missionCard(2).value("status").toInt(),
           int(Explorer::MissionStatus::Solved));
  QTest::qWait(400); // XP counter transition
  screenshot("missions-mission3-solved");
  // Checking again does not reward the same challenge twice.
  click(m_missions, "Check challenge");
  QVERIFY(m_missions->findChild<QLabel *>("missionResult")
              ->text()
              .contains("already counted"));
}

void tst_explorergui::tst_mission4CacheChallenge() {
  loadMission(3);
  runFast();
  activateTab(kMissionsTab);
  QTRY_COMPARE_WITH_TIMEOUT(stepStatuses(), QStringList({"✓", "✓", "✓"}), 2000);
  click(m_missions, "Check challenge");
  const QString result =
      m_missions->findChild<QLabel *>("missionResult")->text();
  QVERIFY(result.contains("Not solved yet"));

  activateTab(kEditorTab);
  const auto *previous = ProcessorHandler::getProgram().get();
  m_window->findChild<EditTab *>()->loadSourceText(
      m_catalog[3].referenceSolution);
  waitForNewProgram(previous);
  runFast();
  activateTab(kMissionsTab);
  click(m_missions, "Check challenge");
  QVERIFY(m_missions->findChild<QLabel *>("missionResult")
              ->text()
              .contains("Challenge solved"));
  screenshot("missions-mission4-solved");
}

void tst_explorergui::tst_systemViewRipes() {
  // Live trace of the Mission 4 solution on RV32_5S (previous test).
  activateTab(kExplorerTab);
  m_explorer->findChild<QTabWidget *>("explorerSideTabs")->setCurrentIndex(4);
  auto *view = m_explorer->findChild<QGraphicsView *>("systemTopology");
  QVERIFY(view);
  QStringList simulated, notSimulated;
  for (auto *item : view->scene()->items()) {
    const QString key = item->data(0).toString();
    if (key.startsWith("not-simulated:"))
      notSimulated << key.section(':', 1);
    else if (!key.isEmpty())
      simulated << key;
  }
  simulated.sort();
  // Ripes simulates a CPU and an ideal memory; this program uses no MMIO.
  QCOMPARE(simulated, QStringList({"soc.cpu0", "soc.mem"}));
  for (const QString role :
       {"cache", "interconnect", "memory-controller", "dma", "accelerator"})
    QVERIFY2(notSimulated.contains(role), qPrintable(role));

  auto *systemView = m_explorer->findChild<SystemView *>();
  systemView->selectComponent("soc.cpu0");
  QCOMPARE(systemView->selectedComponent(), QString("soc.cpu0"));
  const QString details =
      m_explorer->findChild<QTextBrowser *>("systemDetails")->toPlainText();
  QVERIFY(details.contains("RV32_5S"));
  QVERIFY(details.contains("Parameters"));
  QVERIFY(details.contains("instructions_retired"));
  screenshot("explorer-system-ripes");
}

void tst_explorergui::tst_gem5TraceInExplorer() {
  QTemporaryDir dir;
  QString error;
  const QString path = Gem5Sample::convert(dir, "loop_sum_o3_l1l2", &error);
  if (path.isEmpty())
    QSKIP(qPrintable(error));
  activateTab(kExplorerTab);
  QVERIFY2(m_explorer->openTraceFile(path, &error), qPrintable(error));
  QTest::qWait(200); // let deferred layout updates run, as in the app

  const QString banner =
      m_explorer->findChild<QLabel *>("explorerBanner")->text();
  QVERIFY(banner.contains("gem5"));
  QVERIFY(banner.contains("BaseO3CPU"));
  QVERIFY(banner.contains("cycle-level-simulator"));
  QVERIFY(banner.contains("1 cycle = 1000 ps"));
  auto *timeline = m_explorer->findChild<QTableView *>("explorerTimeline");
  QCOMPARE(timeline->model()->rowCount(), 317); // dynamic instructions

  // The pipeline strip shows gem5's O3 stages, not the previous model's.
  QStringList strip;
  for (auto *b : m_explorer->findChildren<QToolButton *>())
    if (b->isVisible() && b->property("instance").isValid())
      strip << b->text().section('\n', 0, 0);
  QCOMPARE(strip, QStringList({"F", "DEC", "REN", "DIS", "ISS", "CMP"}));

  // Instruction 3 (add a0, a0, a1) in the out-of-order pipeline.
  m_explorer->findChild<QTabWidget *>("explorerSideTabs")->setCurrentIndex(0);
  m_explorer->setCycle(85);
  m_explorer->selectInstance(3);
  const QString explanation =
      m_explorer->findChild<QTextBrowser *>("explorerExplanation")
          ->toPlainText();
  QVERIFY(explanation.contains("add a0, a0, a1"));
  QVERIFY(explanation.contains("✓"));
  // O3PipeView decode time of instruction 3 (fetched at 81).
  QVERIFY(
      explanation.contains("after this instruction was decoded at cycle 82"));
  QTest::qWait(100);
  screenshot("explorer-gem5-o3");

  m_explorer->findChild<QTabWidget *>("explorerSideTabs")->setCurrentIndex(4);
  auto *systemView = m_explorer->findChild<SystemView *>();
  systemView->selectComponent("board.cache_hierarchy.l1i-cache-0");
  const QString details =
      m_explorer->findChild<QTextBrowser *>("systemDetails")->toPlainText();
  QVERIFY(details.contains("demandMisses::total"));
  QVERIFY(details.contains("capacity in bytes"));
  QStringList notSimulated;
  for (auto *item : m_explorer->findChild<QGraphicsView *>("systemTopology")
                        ->scene()
                        ->items())
    if (item->data(0).toString().startsWith("not-simulated:"))
      notSimulated << item->data(0).toString().section(':', 1);
  notSimulated.sort();
  // gem5 simulates caches, crossbars and a memory controller here.
  QCOMPARE(notSimulated, QStringList({"accelerator", "dma"}));
  // Give the System view more room, as a user would by dragging the splitter.
  auto *splitter = qobject_cast<QSplitter *>(
      m_explorer->findChild<QTabWidget *>("explorerSideTabs")->parentWidget());
  QVERIFY(splitter);
  const auto sizes = splitter->sizes();
  splitter->setSizes({sizes[0] + sizes[1] - 860, 860});
  QTest::qWait(100);
  screenshot("explorer-gem5-system");
  splitter->setSizes(sizes);
}

void tst_explorergui::tst_themes() {
  auto trigger = [&](const QString &text) {
    for (auto *action : m_window->findChildren<QAction *>())
      if (action->text() == text) {
        action->trigger();
        return true;
      }
    return false;
  };
  auto *sideTabs = m_explorer->findChild<QTabWidget *>("explorerSideTabs");

  // The game theme is the default: own palette and style sheet.
  QVERIFY(Theme::isGame());
  QVERIFY(!qApp->styleSheet().isEmpty());
  QCOMPARE(appInDarkMode(), Theme::tokens().dark);
  QVERIFY(Theme::animationsEnabled());
  activateTab(kProcessorTab);
  QTest::qWait(300);
  screenshot("processor-game");
  activateTab(kEditorTab);
  QTest::qWait(200);
  screenshot("editor-game");

  // Light variant of the game theme (follows the color scheme setting).
  RipesSettings::setValue(RIPES_SETTING_COLORSCHEME,
                          static_cast<int>(ColorScheme::Light));
  QTRY_VERIFY(!Theme::tokens().dark);
  QVERIFY(!appInDarkMode());
  activateTab(kMissionsTab);
  QTest::qWait(200);
  screenshot("missions-game-light");
  activateTab(kExplorerTab);
  sideTabs->setCurrentIndex(0);
  QTest::qWait(200);
  screenshot("explorer-gem5-game-light");
  RipesSettings::setValue(RIPES_SETTING_COLORSCHEME,
                          static_cast<int>(ColorScheme::System));

  // Classic restores the upstream look: no style sheet, platform palette,
  // no transitions. The Explorer keeps its contracts.
  QVERIFY(trigger("Classic (Ripes)"));
  QTRY_VERIFY(!Theme::isGame());
  QCOMPARE(RipesSettings::value(RIPES_SETTING_OSX_THEME).toInt(), 1);
  QVERIFY(qApp->styleSheet().isEmpty());
  QVERIFY(!Theme::animationsEnabled());
  QVERIFY(m_explorer->findChild<QLabel *>("explorerBanner")
              ->text()
              .contains("cycle-level-simulator"));
  QTest::qWait(200);
  screenshot("classic-explorer-gem5");
  activateTab(kMissionsTab);
  QTest::qWait(200);
  screenshot("classic-missions");

  QVERIFY(trigger("Game (Open SoC Explorer)"));
  QTRY_VERIFY(Theme::isGame());
  QVERIFY(!qApp->styleSheet().isEmpty());
  // Reduced motion turns the transitions off.
  QVERIFY(trigger("Reduce motion"));
  QVERIFY(RipesSettings::value(RIPES_SETTING_OSX_REDUCED_MOTION).toBool());
  QVERIFY(!Theme::animationsEnabled());
  QVERIFY(trigger("Reduce motion"));
  QVERIFY(Theme::animationsEnabled());
}

int main(int argc, char **argv) {
  // Default to the offscreen platform so that the test behaves the same on
  // every operating system and in CI.
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTEST_SET_MAIN_SOURCE_PATH
  tst_explorergui test;
  return QTest::qExec(&test, argc, argv);
}

#include "tst_explorergui.moc"
