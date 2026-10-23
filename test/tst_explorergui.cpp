// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: end-to-end test of the Explorer and Missions tabs
// in the real main window. Set OSX_SCREENSHOT_DIR to save screenshots.
#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest/QTest>

#include "edittab.h"
#include "explorer/mission.h"
#include "explorer/missionprogress.h"
#include "explorer/tracejson.h"
#include "explorer/tracetext.h"
#include "explorertab.h"
#include "explorerwidgets.h"
#include "fancytabbar/fancytabbar.h"
#include "gem5sample.h"
#include "i18n/language.h"
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
    RIPES_SETTING_OSX_THEME,         RIPES_SETTING_OSX_REDUCED_MOTION,
    RIPES_SETTING_COLORSCHEME,       RIPES_SETTING_OSX_LANGUAGE};

/// Animation driver with a virtual clock for the demo recording: Qt's
/// animations advance only when a frame is taken, so that the video shows
/// them at their real duration however long grabbing a frame takes.
class FrameClock : public QAnimationDriver {
public:
  void step(qint64 ms) {
    m_ms += ms;
    if (isRunning())
      advance();
  }
  qint64 elapsed() const override { return m_ms - m_startMs; }

protected:
  void start() override {
    m_startMs = m_ms;
    QAnimationDriver::start();
  }

private:
  qint64 m_ms = 0;
  qint64 m_startMs = 0;
};

/// Records the main window as PNG frames (25 per second of virtual time)
/// with a drawn pointer, click ripples and captions for the demo video.
/// The window content is the real application; only the pointer, ripple and
/// caption are drawn on top of the frames.
class DemoRecorder {
public:
  DemoRecorder(QWidget *window, const QString &dir)
      : m_window(window), m_dir(dir), m_size(window->size()),
        m_pointer(window->width() / 2, window->height() / 2) {
    QDir().mkpath(dir);
    m_clock.install();
  }
  ~DemoRecorder() { m_clock.uninstall(); }

  void caption(const QString &text) { m_caption = text; }
  /// Pop-up windows (menus) drawn onto the frames while they are visible.
  void setOverlays(const QList<QWidget *> &overlays) { m_overlays = overlays; }
  void hold(int ms) {
    for (int t = 0; t < ms; t += kFrameMs)
      frame();
  }
  QPoint centerOf(const QWidget *w) const {
    return w->mapTo(m_window, w->rect().center());
  }
  void moveTo(const QPoint &target, int ms = 520) {
    const QPointF from = m_pointer;
    const int n = std::max(1, ms / kFrameMs);
    for (int i = 1; i <= n; ++i) {
      const qreal t = qreal(i) / n;
      const qreal e = t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
      m_pointer = from + (QPointF(target) - from) * e;
      frame();
    }
  }
  void moveTo(const QWidget *w, int ms = 520) { moveTo(centerOf(w), ms); }
  /// Shows a click at the pointer; the caller performs the action.
  void click() {
    m_rippleFrames = 8;
    hold(2 * kFrameMs);
  }
  int frames() const { return m_index; }

private:
  static constexpr int kFrameMs = 40;

  void frame() {
    QCoreApplication::processEvents();
    m_clock.step(kFrameMs);
    QCoreApplication::processEvents();
    // Every frame has the size of the first (the encoder needs one size).
    QImage img = m_window->grab()
                     .toImage()
                     .convertToFormat(QImage::Format_RGB32)
                     .copy(QRect(QPoint(0, 0), m_size));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    for (auto *w : m_overlays)
      if (w->isVisible())
        p.drawPixmap(m_window->mapFromGlobal(w->mapToGlobal(QPoint(0, 0))),
                     w->grab());
    if (!m_caption.isEmpty()) {
      QFont f = QApplication::font();
      f.setPointSizeF(f.pointSizeF() * 1.25);
      f.setBold(true);
      p.setFont(f);
      const QFontMetrics fm(f);
      const QRect r(0, 0, fm.horizontalAdvance(m_caption) + 44,
                    fm.height() + 20);
      const QRect box = r.translated((img.width() - r.width()) / 2,
                                     img.height() - r.height() - 26);
      p.setPen(QPen(QColor(56, 198, 255, 200), 1.5));
      p.setBrush(QColor(8, 12, 24, 215));
      p.drawRoundedRect(box, 14, 14);
      p.setPen(QColor(240, 245, 255));
      p.drawText(box, Qt::AlignCenter, m_caption);
    }
    if (m_rippleFrames > 0) {
      const qreal t = 1.0 - m_rippleFrames / 8.0;
      p.setPen(QPen(QColor(56, 198, 255, int(230 * (1 - t))), 3));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(m_pointer, 8 + 22 * t, 8 + 22 * t);
      --m_rippleFrames;
    }
    QPainterPath arrow;
    const QPointF o = m_pointer;
    arrow.moveTo(o);
    arrow.lineTo(o + QPointF(0, 21));
    arrow.lineTo(o + QPointF(5.5, 16));
    arrow.lineTo(o + QPointF(9.5, 24.5));
    arrow.lineTo(o + QPointF(13, 23));
    arrow.lineTo(o + QPointF(9, 14.5));
    arrow.lineTo(o + QPointF(16, 14.5));
    arrow.closeSubpath();
    p.setPen(QPen(QColor(10, 14, 24), 1.6));
    p.setBrush(QColor(255, 255, 255));
    p.drawPath(arrow);
    p.end();
    img.save(
        QString("%1/frame_%2.png").arg(m_dir).arg(m_index++, 5, 10, QChar('0')),
        "PNG", 85);
  }

  QWidget *m_window;
  QSize m_size;
  QList<QWidget *> m_overlays;
  QString m_dir;
  QPointF m_pointer;
  QString m_caption;
  int m_rippleFrames = 0;
  int m_index = 0;
  FrameClock m_clock;
};
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
  bool triggerAction(const QString &text);
  QString badgeText() const;

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
  void tst_language();
  void tst_demoVideo();
  void tst_languageDemoVideo();
  void tst_exampleGem5Trace();
  void tst_traceFromMemory();
  void tst_loadProgramData();
  void tst_webLayout();
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
  // The tests check English texts; tst_language switches the language.
  RipesSettings::setValue(RIPES_SETTING_OSX_LANGUAGE, "en");
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

bool tst_explorergui::triggerAction(const QString &text) {
  for (auto *action : m_window->findChildren<QAction *>())
    if (action->text() == text) {
      action->trigger();
      return true;
    }
  return false;
}

QString tst_explorergui::badgeText() const {
  QStringList parts;
  for (const auto &b : m_explorer->findChild<HudBanner *>()->badges())
    parts << b.label + ": " + b.value;
  return parts.join(" | ");
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

void tst_explorergui::tst_language() {
  auto *bar = m_window->findChild<FancyTabBar *>();
  auto *sideTabs = m_explorer->findChild<QTabWidget *>("explorerSideTabs");
  auto *mapTitle = m_missions->findChild<QLabel *>("missionOverline");
  auto *banner = m_explorer->findChild<QLabel *>("explorerBanner");
  auto stepChecks = [&] {
    QStringList texts;
    for (auto *frame : m_missions->findChildren<QFrame *>("missionStep"))
      for (auto *l : frame->findChildren<QLabel *>())
        if (l->objectName().isEmpty())
          texts << l->text();
    return texts.join("\n");
  };
  QCOMPARE(I18n::current(), I18n::Language::English);

  // Mission 3 at the first flush, on the live trace.
  QVERIFY(triggerAction("Return to the live trace"));
  loadMission(2);
  runToEnd();
  activateTab(kExplorerTab);
  sideTabs->setCurrentIndex(0);
  m_explorer->setCycle(7);
  m_explorer->selectInstance(6);
  QTest::qWait(100);
  const auto *explanation =
      m_explorer->findChild<QTextBrowser *>("explorerExplanation");
  const QString englishExplanation = explanation->toPlainText();
  QVERIFY(englishExplanation.contains("squashed at cycle 7"));
  QCOMPARE(bar->tabText(kExplorerTab), QString("Explorer"));
  QCOMPARE(mapTitle->text(), QString("MISSION MAP"));
  QVERIFY(banner->text().contains("<b>Engine:</b>"));
  QVERIFY(badgeText().contains("Fidelity: educational-model"));
  activateTab(kMissionsTab);
  QVERIFY(stepChecks().contains("Selected processor:"));
  screenshot("en-missions-map");

  // Switch at run time: View → Language → 简体中文.
  QVERIFY(triggerAction("简体中文"));
  QCOMPARE(RipesSettings::value(RIPES_SETTING_OSX_LANGUAGE).toString(),
           QString("zh_CN"));
  QTRY_COMPARE(bar->tabText(kExplorerTab), QString("探索器"));
  QCOMPARE(I18n::current(), I18n::Language::ChineseSimplified);
  QCOMPARE(bar->tabText(kMissionsTab), QString("任务"));
  QCOMPARE(bar->tabText(kProcessorTab), QString("处理器"));
  QCOMPARE(mapTitle->text(), QString("任务地图"));
  QCOMPARE(sideTabs->tabText(0), QString("指令"));
  // Mission content in Chinese, criteria too; the progress is the same.
  QVERIFY(stepChecks().contains("所选处理器："));
  QVERIFY(m_missions->findChild<QListWidget *>("missionList")
              ->item(2)
              ->text()
              .contains("分支与冲刷"));
  QVERIFY(m_missions->findChild<QListWidget *>("missionList")
              ->item(2)
              ->text()
              .startsWith("✓"));
  click(m_missions, "检查挑战");
  QVERIFY(m_missions->findChild<QLabel *>("missionResult")
              ->text()
              .contains("尚未解决"));
  // Upstream texts: menus and the simulator tool bar.
  bool clockFound = false;
  for (auto *a : m_window->findChildren<QAction *>())
    clockFound |= a->text() == "时钟 (F5)";
  QVERIFY(clockFound);
  bool viewMenu = false;
  for (auto *m : m_window->findChildren<QMenu *>())
    viewMenu |= m->title() == "视图";
  QVERIFY(viewMenu);
  screenshot("zh-missions-map");

  // Explorer: banner, badges, explanation in Chinese; identifiers kept.
  activateTab(kExplorerTab);
  sideTabs->setCurrentIndex(0);
  m_explorer->setCycle(7);
  m_explorer->selectInstance(6);
  QTRY_VERIFY(banner->text().contains("<b>引擎：</b>"));
  QVERIFY(banner->text().contains("educational-model"));
  QVERIFY(badgeText().contains("保真度: educational-model"));
  QVERIFY(badgeText().contains("RV32_5S"));
  const QString zhExplanation = explanation->toPlainText();
  QVERIFY(zhExplanation.contains("在周期 7 被作废"));
  QVERIFY(zhExplanation.contains("在模型中的经历"));
  QVERIFY(zhExplanation.contains("src/processors/RISC-V/rv5s/rv5s.h"));
  QVERIFY(!zhExplanation.contains("squashed at cycle"));
  // The fidelity caveat keeps its meaning.
  QVERIFY(zhExplanation.contains("不是任何特定商业或开源 RTL 核的模型"));
  screenshot("zh-explorer-mission3-flush");

  // A gem5 trace: every descriptive text the converter writes is known.
  QTemporaryDir dir;
  QString error;
  const QString path = Gem5Sample::convert(dir, "loop_sum_o3_l1l2", &error);
  if (!path.isEmpty()) {
    QVERIFY2(m_explorer->openTraceFile(path, &error), qPrintable(error));
    QTest::qWait(200);
    m_explorer->setCycle(85);
    m_explorer->selectInstance(3);
    const auto trace = Trace::readJsonlFile(path, &error);
    QVERIFY(trace.has_value());
    QStringList untranslated;
    auto check = [&](const QString &text) {
      if (!text.isEmpty() && Explorer::localizedTraceText(text) == text)
        untranslated << text;
    };
    check(trace->header.source.fidelityNote);
    check(trace->header.clockDomainDescription);
    for (const auto &c : trace->completeness.caveats)
      check(c);
    for (const auto &c : trace->header.system) {
      check(c.description);
      for (const auto &v : c.parameterInfo)
        check(v.toString());
    }
    QVERIFY2(untranslated.isEmpty(), qPrintable(untranslated.join("\n")));
    QVERIFY(m_explorer->findChild<HudBanner *>()->toolTip().contains(
        "系统调用仿真"));
    sideTabs->setCurrentIndex(4);
    auto *systemView = m_explorer->findChild<SystemView *>();
    systemView->selectComponent("board.cache_hierarchy.l1i-cache-0");
    const QString details =
        m_explorer->findChild<QTextBrowser *>("systemDetails")->toPlainText();
    QVERIFY(details.contains("参数"));
    QVERIFY(details.contains("容量（字节）"));
    QVERIFY(details.contains("demandMisses::total")); // statistic names kept
    auto *splitter = qobject_cast<QSplitter *>(sideTabs->parentWidget());
    const auto sizes = splitter->sizes();
    splitter->setSizes({sizes[0] + sizes[1] - 860, 860});
    QTest::qWait(100);
    screenshot("zh-explorer-gem5-system");
    splitter->setSizes(sizes);
    sideTabs->setCurrentIndex(0);
  }
  activateTab(kProcessorTab);
  QTest::qWait(300);
  screenshot("zh-processor");

  // And back: English everywhere again, including upstream texts.
  QVERIFY(triggerAction("English"));
  QTRY_COMPARE(bar->tabText(kExplorerTab), QString("Explorer"));
  QCOMPARE(I18n::current(), I18n::Language::English);
  QCOMPARE(mapTitle->text(), QString("MISSION MAP"));
  QCOMPARE(sideTabs->tabText(0), QString("Instruction"));
  clockFound = false;
  for (auto *a : m_window->findChildren<QAction *>())
    clockFound |= a->text() == "Clock (F5)";
  QVERIFY(clockFound);
  viewMenu = false;
  for (auto *m : m_window->findChildren<QMenu *>())
    viewMenu |= m->title() == "View";
  QVERIFY(viewMenu);
  QVERIFY(triggerAction("Return to the live trace"));
  activateTab(kExplorerTab);
  m_explorer->setCycle(7);
  m_explorer->selectInstance(6);
  QTRY_VERIFY(banner->text().contains("<b>Engine:</b>"));
  QCOMPARE(explanation->toPlainText(), englishExplanation);
  activateTab(kMissionsTab);
  QVERIFY(stepChecks().contains("Selected processor:"));
  QVERIFY(m_missions->findChild<QListWidget *>("missionList")
              ->item(2)
              ->text()
              .contains("Branch and Flush"));
}

// Opt-in: with OSX_DEMO_DIR set, records a demo of real usage as PNG frames
// (see tools/demo/make_video.sh). Run it on its own, from fresh progress:
//   OSX_DEMO_DIR=frames ./tst_explorergui tst_demoVideo
void tst_explorergui::tst_demoVideo() {
  const QString dir = qEnvironmentVariable("OSX_DEMO_DIR");
  if (dir.isEmpty())
    QSKIP("set OSX_DEMO_DIR to record the demo frames");
  auto *bar = m_window->findChild<FancyTabBar *>();
  auto *list = m_missions->findChild<QListWidget *>("missionList");
  auto *sideTabs = m_explorer->findChild<QTabWidget *>("explorerSideTabs");
  auto *strip = m_explorer->findChild<PipelineStrip *>();
  const int tabH = (bar->minimumHeight() - 12) / 7;
  auto sidebarPos = [&](int index) {
    return bar->mapTo(m_window,
                      QPoint(bar->width() / 2, 6 + tabH * index + tabH / 2));
  };
  auto button = [&](QWidget *parent, const QString &text) -> QWidget * {
    for (auto *b : parent->findChildren<QAbstractButton *>())
      if (b->text() == text && b->isVisible())
        return b;
    return nullptr;
  };
  auto toolButton = [&](const QString &actionText,
                        QAction **actionOut) -> QWidget * {
    for (auto *a : m_window->findChildren<QAction *>())
      if (a->text() == actionText)
        for (auto *tb : m_window->findChildren<QToolBar *>())
          if (auto *w = tb->widgetForAction(a); w && w->isVisible()) {
            *actionOut = a;
            return w;
          }
    return nullptr;
  };
  auto settle = [] {
    QTest::qWait(150); // let the simulator thread and GUI updates finish
  };

  activateTab(kMissionsTab);
  list->setCurrentRow(-1);
  DemoRecorder rec(m_window, dir);

  // 1. Mission map: progress only from checks on the simulator.
  rec.caption("Mission map · progress only comes from checks on the simulator");
  rec.hold(1400);
  rec.moveTo(list->viewport()->mapTo(
      m_window, list->visualItemRect(list->item(0)).center()));
  rec.click();
  list->setCurrentRow(0);
  rec.hold(700);
  rec.caption("Load the example program and select the processor");
  rec.moveTo(button(m_missions, "Load example"));
  rec.click();
  QTest::mouseClick(button(m_missions, "Load example"), Qt::LeftButton);
  settle();
  rec.hold(900);
  rec.moveTo(sidebarPos(kMissionsTab));
  rec.click();
  bar->setActiveIndex(kMissionsTab);
  rec.hold(400);
  rec.moveTo(button(m_missions, "Select processor"));
  rec.click();
  QTest::mouseClick(button(m_missions, "Select processor"), Qt::LeftButton);
  settle();
  rec.caption("Each guided step is verified live: +100 XP per verified step");
  rec.hold(1100);
  QAction *clockAction = nullptr, *runAction = nullptr;
  QWidget *clockButton = toolButton("Clock (F5)", &clockAction);
  QWidget *runButton = toolButton("Run (F8)", &runAction);
  QVERIFY(clockButton && runButton);
  rec.moveTo(clockButton);
  rec.click();
  clockAction->trigger();
  settle();
  rec.hold(1000);
  rec.moveTo(runButton);
  rec.click();
  runAction->trigger();
  QTRY_VERIFY_WITH_TIMEOUT(!ProcessorHandler::isRunning(), 20000);
  settle();
  rec.hold(1400);

  // 2. Mission 2 is unlocked: hazards on the 5-stage pipeline.
  rec.caption("Mission 2 unlocked · pipeline hazards on the 5-stage processor");
  rec.moveTo(list->viewport()->mapTo(
      m_window, list->visualItemRect(list->item(1)).center()));
  rec.click();
  list->setCurrentRow(1);
  rec.hold(800);
  rec.moveTo(button(m_missions, "Load example"));
  rec.click();
  QTest::mouseClick(button(m_missions, "Load example"), Qt::LeftButton);
  settle();
  rec.hold(500);
  rec.moveTo(sidebarPos(kMissionsTab), 400);
  rec.click();
  bar->setActiveIndex(kMissionsTab);
  rec.hold(300);
  rec.moveTo(button(m_missions, "Select processor"));
  rec.click();
  QTest::mouseClick(button(m_missions, "Select processor"), Qt::LeftButton);
  settle();
  rec.hold(900);

  // 3. Explorer: clock the processor; every cycle is recorded and replayed.
  rec.caption("Explorer · each clock is recorded; the strip moves as the "
              "trace says");
  rec.moveTo(sidebarPos(kExplorerTab));
  rec.click();
  bar->setActiveIndex(kExplorerTab);
  sideTabs->setCurrentIndex(0);
  rec.hold(600);
  rec.moveTo(clockButton);
  for (int i = 0; i < 7; ++i) {
    rec.click();
    clockAction->trigger();
    settle();
    rec.hold(720);
  }
  // Explain the held instruction (load-use hazard).
  for (auto *tile : strip->tiles())
    if (tile->visual().held) {
      rec.caption("A load-use hazard: the add is held, a stall bubble "
                  "enters EX");
      rec.moveTo(tile);
      rec.click();
      QTest::mouseClick(tile, Qt::LeftButton);
      rec.hold(1800);
      break;
    }

  // 4. Step backwards and forwards through the recording.
  rec.caption("Step backwards and forwards through recorded cycles");
  QWidget *prev = nullptr, *next = nullptr;
  for (auto *b : m_explorer->findChildren<QToolButton *>("explorerControl")) {
    if (b->toolTip() == "Previous cycle")
      prev = b;
    if (b->toolTip() == "Next cycle")
      next = b;
  }
  QVERIFY(prev && next);
  rec.moveTo(prev);
  for (int i = 0; i < 2; ++i) {
    rec.click();
    QTest::mouseClick(prev, Qt::LeftButton);
    rec.hold(640);
  }
  rec.moveTo(next);
  for (int i = 0; i < 2; ++i) {
    rec.click();
    QTest::mouseClick(next, Qt::LeftButton);
    rec.hold(640);
  }

  // 5. A gem5 O3CPU trace and the System view.
  QTemporaryDir tmp;
  QString error;
  const QString path = Gem5Sample::convert(tmp, "loop_sum_o3_l1l2", &error);
  QVERIFY2(!path.isEmpty(), qPrintable(error));
  rec.caption("Offline replay of a gem5 O3CPU run (converted with tools/gem5)");
  QAction *openAction = nullptr;
  QWidget *openButton =
      toolButton("Open a trace file for offline replay...", &openAction);
  QVERIFY(openButton);
  rec.moveTo(openButton);
  rec.click();
  QVERIFY2(m_explorer->openTraceFile(path, &error), qPrintable(error));
  m_explorer->setCycle(81);
  rec.hold(1000);
  rec.moveTo(next);
  for (int i = 0; i < 4; ++i) {
    rec.click();
    QTest::mouseClick(next, Qt::LeftButton);
    rec.hold(680);
  }
  rec.caption("System view · what gem5 simulated, and what it did not");
  rec.moveTo(sideTabs->tabBar()->mapTo(
      m_window, sideTabs->tabBar()->tabRect(4).center()));
  rec.click();
  sideTabs->setCurrentIndex(4);
  rec.hold(500);
  // Widen the side panel by dragging the splitter.
  auto *splitter = qobject_cast<QSplitter *>(sideTabs->parentWidget());
  const auto sizes = splitter->sizes();
  const int total = sizes[0] + sizes[1];
  const QPoint handle = splitter->handle(1)->mapTo(
      m_window, splitter->handle(1)->rect().center());
  rec.moveTo(handle, 400);
  for (int i = 1; i <= 10; ++i) {
    const int right = sizes[1] + (860 - sizes[1]) * i / 10;
    splitter->setSizes({total - right, right});
    rec.moveTo(handle - QPoint(right - sizes[1], 0), 40);
  }
  rec.hold(400);
  auto *view = m_explorer->findChild<QGraphicsView *>("systemTopology");
  for (const QString component :
       {"board.cache_hierarchy.l1i-cache-0", "board.memory.mem_ctrl"}) {
    QGraphicsItem *chip = nullptr;
    for (auto *item : view->scene()->items())
      if (item->data(0).toString() == component)
        chip = item;
    QVERIFY(chip);
    const QPoint inView =
        view->mapFromScene(chip->sceneBoundingRect().center());
    rec.moveTo(view->viewport()->mapTo(m_window, inView));
    QTest::mouseMove(view->viewport(), inView);
    rec.click();
    m_explorer->findChild<SystemView *>()->selectComponent(component);
    rec.hold(1700);
  }
  rec.caption("Open SoC Explorer · every value comes from a simulator");
  rec.hold(1500);
  splitter->setSizes(sizes);
  qInfo("recorded %d frames", rec.frames());
}

// Opt-in: with OSX_LANGUAGE_DEMO_DIR set, records switching the language
// between English and Simplified Chinese at run time as PNG frames (see
// tools/demo/make_video.sh). Run it on its own:
//   OSX_LANGUAGE_DEMO_DIR=frames ./tst_explorergui tst_languageDemoVideo
void tst_explorergui::tst_languageDemoVideo() {
  const QString dir = qEnvironmentVariable("OSX_LANGUAGE_DEMO_DIR");
  if (dir.isEmpty())
    QSKIP("set OSX_LANGUAGE_DEMO_DIR to record the language demo frames");
  auto *bar = m_window->findChild<FancyTabBar *>();
  auto *list = m_missions->findChild<QListWidget *>("missionList");
  auto *sideTabs = m_explorer->findChild<QTabWidget *>("explorerSideTabs");
  auto *menuBar = m_window->menuBar();
  QMenu *viewMenu = nullptr;
  for (auto *a : menuBar->actions())
    if (a->menu() && a->text() == "View")
      viewMenu = a->menu();
  QVERIFY(viewMenu);
  const int tabH = (bar->minimumHeight() - 12) / 7;
  auto sidebarPos = [&](int index) {
    return bar->mapTo(m_window,
                      QPoint(bar->width() / 2, 6 + tabH * index + tabH / 2));
  };

  // Mission 3 at its first flush, so that every view has content.
  RipesSettings::setValue(
      RIPES_SETTING_OSX_MISSION_STEPS,
      QStringList(
          {"m1-first-program/1", "m1-first-program/2", "m1-first-program/3"}));
  RipesSettings::setValue(RIPES_SETTING_OSX_MISSIONS_COMPLETED,
                          QStringList({"m1-first-program"}));
  loadMission(2);
  runToEnd();
  activateTab(kExplorerTab);
  sideTabs->setCurrentIndex(0);
  m_explorer->setCycle(7);
  m_explorer->selectInstance(6);
  activateTab(kMissionsTab);
  list->setCurrentRow(0);

  DemoRecorder rec(m_window, dir);
  // Opens View → Language and chooses @p language, as a user would.
  auto chooseLanguage = [&](const QString &language) {
    const QRect viewRect = menuBar->actionGeometry(viewMenu->menuAction());
    rec.moveTo(menuBar->mapTo(m_window, viewRect.center()));
    rec.click();
    viewMenu->popup(menuBar->mapToGlobal(viewRect.bottomLeft()));
    QMenu *languageMenu = nullptr;
    for (auto *a : viewMenu->actions())
      if (a->menu() && (a->text() == "Language" || a->text() == "语言"))
        languageMenu = a->menu();
    QVERIFY(languageMenu);
    rec.setOverlays({viewMenu, languageMenu});
    rec.hold(500);
    const QRect langRect = viewMenu->actionGeometry(languageMenu->menuAction());
    rec.moveTo(
        m_window->mapFromGlobal(viewMenu->mapToGlobal(langRect.center())), 420);
    viewMenu->setActiveAction(languageMenu->menuAction());
    languageMenu->popup(viewMenu->mapToGlobal(langRect.topRight()));
    rec.hold(450);
    QAction *target = nullptr;
    for (auto *a : languageMenu->actions())
      if (a->text() == language)
        target = a;
    QVERIFY(target);
    const QRect targetRect = languageMenu->actionGeometry(target);
    rec.moveTo(
        m_window->mapFromGlobal(languageMenu->mapToGlobal(targetRect.center())),
        420);
    languageMenu->setActiveAction(target);
    rec.hold(300);
    rec.click();
    languageMenu->hide();
    viewMenu->hide();
    rec.setOverlays({});
    target->trigger();
    QTest::qWait(100); // LanguageChange events
  };

  // 1. English.
  rec.caption("Open SoC Explorer in English");
  rec.hold(1600);
  rec.caption("View → Language → 简体中文 · switches at run time, no restart");
  chooseLanguage("简体中文");
  QTRY_COMPARE(bar->tabText(kExplorerTab), QString("探索器"));
  rec.hold(1800);

  // 2. Chinese: missions, Explorer, System view, processor.
  rec.caption("任务地图与任务内容（简体中文）");
  rec.moveTo(list->viewport()->mapTo(
      m_window, list->visualItemRect(list->item(2)).center()));
  rec.click();
  list->setCurrentRow(2);
  rec.hold(2200);
  rec.caption("探索器：所有数值都来自记录的轨迹");
  rec.moveTo(sidebarPos(kExplorerTab));
  rec.click();
  bar->setActiveIndex(kExplorerTab);
  rec.hold(2200);
  QWidget *prev = nullptr, *next = nullptr;
  for (auto *b : m_explorer->findChildren<QToolButton *>("explorerControl")) {
    if (b->toolTip() == "上一个周期")
      prev = b;
    if (b->toolTip() == "下一个周期")
      next = b;
  }
  QVERIFY(prev && next);
  rec.moveTo(prev);
  rec.click();
  QTest::mouseClick(prev, Qt::LeftButton);
  rec.hold(900);
  rec.moveTo(next);
  rec.click();
  QTest::mouseClick(next, Qt::LeftButton);
  rec.hold(1300);
  rec.caption("系统视图：模拟了什么，没有模拟什么");
  rec.moveTo(sideTabs->tabBar()->mapTo(
      m_window, sideTabs->tabBar()->tabRect(4).center()));
  rec.click();
  sideTabs->setCurrentIndex(4);
  rec.hold(2000);
  rec.caption("Ripes 的界面：菜单、工具栏和处理器视图");
  rec.moveTo(sidebarPos(kProcessorTab));
  rec.click();
  bar->setActiveIndex(kProcessorTab);
  rec.hold(2000);

  // 3. Back to English.
  rec.caption("视图 → 语言 → English");
  chooseLanguage("English");
  QTRY_COMPARE(bar->tabText(kExplorerTab), QString("Explorer"));
  rec.hold(1200);
  rec.caption("Back to English: the same views and the same progress");
  rec.moveTo(sidebarPos(kExplorerTab));
  rec.click();
  bar->setActiveIndex(kExplorerTab);
  sideTabs->setCurrentIndex(0);
  rec.hold(2000);
  rec.moveTo(sidebarPos(kMissionsTab));
  rec.click();
  bar->setActiveIndex(kMissionsTab);
  rec.hold(1800);
  qInfo("recorded %d frames", rec.frames());
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

// The example gem5 trace (File → Load Example, Ctrl+Shift+G, Explorer tool
// bar), built into the application for the web version.
void tst_explorergui::tst_exampleGem5Trace() {
  RipesSettings::setValue(RIPES_SETTING_OSX_LANGUAGE, "en");
  if (ExplorerTab::exampleGem5TracePath().isEmpty())
    QSKIP("built without the example gem5 trace (no Python 3)");
  activateTab(kProcessorTab);
  QTest::keyClick(m_window, Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
  QTest::qWait(200);
  QCOMPARE(m_window->findChild<FancyTabBar *>()->getActiveIndex(),
           kExplorerTab);
  const QString banner =
      m_explorer->findChild<QLabel *>("explorerBanner")->text();
  QVERIFY2(banner.contains("example gem5 O3CPU trace"), qPrintable(banner));
  QVERIFY(banner.contains("BaseO3CPU"));
  auto *timeline = m_explorer->findChild<QTableView *>("explorerTimeline");
  QCOMPARE(timeline->model()->rowCount(), 317);
  bool found = false;
  for (auto *a : m_window->findChildren<QAction *>())
    if (a->text() == "Open the example gem5 trace") {
      found = true;
      QVERIFY(a->isEnabled());
    }
  QVERIFY(found);
  screenshot("explorer-gem5-example");
  QVERIFY(triggerAction("Return to the live trace"));
}

// Traces handed over in memory, as the browser's file chooser does.
void tst_explorergui::tst_traceFromMemory() {
  if (ExplorerTab::exampleGem5TracePath().isEmpty())
    QSKIP("built without the example gem5 trace (no Python 3)");
  QFile file(ExplorerTab::exampleGem5TracePath());
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QByteArray data = file.readAll();
  QString error;
  QVERIFY2(m_explorer->openTraceData(data, "my-run.jsonl", &error),
           qPrintable(error));
  const auto *banner = m_explorer->findChild<QLabel *>("explorerBanner");
  QVERIFY(banner->text().contains("my-run.jsonl"));
  QCOMPARE(m_explorer->findChild<QTableView *>("explorerTimeline")
               ->model()
               ->rowCount(),
           317);
  // What the web version downloads on export reads back the same.
  auto trace = Trace::fromJsonl(data, &error);
  QVERIFY(trace.has_value());
  auto again = Trace::fromJsonl(Trace::toJsonl(*trace), &error);
  QVERIFY(again.has_value());
  QCOMPARE(again->events.size(), trace->events.size());
  QVERIFY(!m_explorer->openTraceData("not a trace", "bad.jsonl", &error));
  QVERIFY2(error.contains("bad.jsonl"), qPrintable(error));
  QVERIFY(banner->text().contains("my-run.jsonl")); // previous trace kept
  QVERIFY(triggerAction("Return to the live trace"));
}

// Programs handed over in memory (the web version's Load Program).
void tst_explorergui::tst_loadProgramData() {
  auto *editTab = m_window->findChild<EditTab *>();
  const QString source = "# loaded from memory\naddi a0, zero, 42\n";
  QVERIFY(m_window->loadProgramData("from-browser.s", source.toUtf8()));
  QCOMPARE(m_window->findChild<FancyTabBar *>()->getActiveIndex(), kEditorTab);
  QCOMPARE(editTab->getAssemblyText(), source);
  QCOMPARE(editTab->getSourceType(), SourceType::Assembly);

  const QStringList elves = QDir(":/examples/ELF/").entryList(QDir::Files);
  QVERIFY(!elves.isEmpty());
  QFile elf(":/examples/ELF/" + elves.first());
  QVERIFY(elf.open(QIODevice::ReadOnly));
  QVERIFY(m_window->loadProgramData(elves.first(), elf.readAll()));
  QCOMPARE(editTab->getSourceType(), SourceType::ExternalELF);
  QTRY_VERIFY(ProcessorHandler::getProgram() &&
              ProcessorHandler::getProgram()->sections.count(".text"));
  editTab->newProgram();
}

// The web version shows the window at the size of the browser window: no
// view may need more room than common desktop sizes offer (the bottom of the
// view would be cut off). Also checks the positions that web/smoke_test.py
// clicks at.
void tst_explorergui::tst_webLayout() {
  const QSize original = m_window->size();
  auto *bar = m_window->findChild<FancyTabBar *>();
  auto *stack = m_window->findChild<QStackedWidget *>();
  const bool haveExample = !ExplorerTab::exampleGem5TracePath().isEmpty();
  // Live trace first, then (if built in) the example gem5 trace, whose
  // banner has more badges.
  for (const bool example : {false, true}) {
    if (example && !haveExample)
      break;
    if (example)
      QVERIFY(m_window->openExampleGem5Trace());
    for (const QSize size : {QSize(1280, 720), QSize(1680, 1000)}) {
      m_window->resize(size);
      for (int tab : {kEditorTab, kProcessorTab, kExplorerTab, kMissionsTab}) {
        activateTab(tab);
        QTest::qWait(100);
        QCOMPARE(m_window->size(), size);
        auto *page = stack->currentWidget();
        const QRect pageRect(page->mapTo(m_window, QPoint(0, 0)), page->size());
        QVERIFY2(QRect(QPoint(0, 0), size).contains(pageRect),
                 qPrintable(QString("tab %1 is %2x%3 at %4,%5 in %6x%7")
                                .arg(tab)
                                .arg(pageRect.width())
                                .arg(pageRect.height())
                                .arg(pageRect.x())
                                .arg(pageRect.y())
                                .arg(size.width())
                                .arg(size.height())));
      }
    }
  }

  const QSize size(1680, 1000);
  m_window->resize(size);
  const int tabH = (bar->minimumHeight() - 12) / 7;
  const QPoint editorTab = bar->mapTo(
      m_window, QPoint(bar->width() / 2, 6 + tabH * kEditorTab + tabH / 2));
  activateTab(kExplorerTab);
  QTest::qWait(100);
  auto *timeline = m_explorer->findChild<QTableView *>("explorerTimeline");
  const QRect t(timeline->mapTo(m_window, QPoint(0, 0)), timeline->size());
  activateTab(kEditorTab);
  QTest::qWait(100);
  auto *editor = m_window->findChild<QWidget *>("codeEditor");
  const QRect e(editor->mapTo(m_window, QPoint(0, 0)), editor->size());
  qInfo("1680x1000: editor tab at %d,%d; editor %d,%d %dx%d; timeline "
        "%.2f,%.2f-%.2f,%.2f of the window",
        editorTab.x(), editorTab.y(), e.x(), e.y(), e.width(), e.height(),
        double(t.left()) / size.width(), double(t.top()) / size.height(),
        double(t.right()) / size.width(), double(t.bottom()) / size.height());
  QVERIFY(QRect(editorTab - QPoint(16, 16), QSize(32, 32))
              .contains(QPoint(38, 117)));
  QVERIFY(e.contains(QPoint(200, 200)));
  if (haveExample)
    QVERIFY(
        t.contains(QPoint(int(0.3 * size.width()), int(0.6 * size.height()))));
  if (haveExample)
    QVERIFY(triggerAction("Return to the live trace"));
  m_window->resize(original);
}

#include "tst_explorergui.moc"
