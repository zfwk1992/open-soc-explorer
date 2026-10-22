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
#include "explorertab.h"
#include "fancytabbar/fancytabbar.h"
#include "gem5sample.h"
#include "mainwindow.h"
#include "missionstab.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "systemview.h"

using namespace Ripes;

namespace {
// Indices of the tabs in MainWindow (see MainWindow::TabIndex).
constexpr int kEditorTab = 0;
constexpr int kExplorerTab = 5;
constexpr int kMissionsTab = 6;
} // namespace

class tst_explorergui : public QObject {
  Q_OBJECT

private:
  void activateTab(int index);
  void click(QWidget *parent, const QString &text);
  void waitForNewProgram(const Program *previous);
  void loadMission(int row);
  void runToEnd();
  QStringList stepStatuses() const;
  void screenshot(const QString &name);

  MainWindow *m_window = nullptr;
  MissionsTab *m_missions = nullptr;
  ExplorerTab *m_explorer = nullptr;
  std::vector<Explorer::Mission> m_catalog;

private slots:
  void initTestCase();
  void tst_mission1Flow();
  void tst_explorerReplaysTrace();
  void tst_componentInspector();
  void tst_mission3ChallengeSolved();
  void tst_systemViewRipes();
  void tst_gem5TraceInExplorer();
  void cleanupTestCase();
};

void tst_explorergui::initTestCase() {
  RipesSettings::setValue(RIPES_SETTING_OSX_TRACE_ENABLED, true);
  m_window = new MainWindow();
  m_window->resize(1680, 1000);
  m_window->show();
  QVERIFY(QTest::qWaitForWindowExposed(m_window));
  m_missions = m_window->findChild<MissionsTab *>();
  m_explorer = m_window->findChild<ExplorerTab *>();
  QVERIFY(m_missions && m_explorer);
  m_catalog = Explorer::loadBuiltinMissions();
  QCOMPARE(m_catalog.size(), size_t(3));
}

void tst_explorergui::cleanupTestCase() { delete m_window; }

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

  // Solve the challenge with the reference solution.
  activateTab(kEditorTab);
  const auto *previous = ProcessorHandler::getProgram().get();
  m_window->findChild<EditTab *>()->loadSourceText(
      m_catalog[2].referenceSolution);
  waitForNewProgram(previous);
  runToEnd();
  activateTab(kMissionsTab);
  click(m_missions, "Check challenge");
  QVERIFY(m_missions->findChild<QLabel *>("missionResult")
              ->text()
              .contains("Challenge solved"));
  QVERIFY(m_missions->findChild<QListWidget *>("missionList")
              ->item(2)
              ->text()
              .startsWith("✓"));
  screenshot("missions-mission3-solved");
}

void tst_explorergui::tst_systemViewRipes() {
  // Live trace of the Mission 3 solution on RV32_5S (previous test).
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
  QTest::qWait(100);
  screenshot("explorer-gem5-system");
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
