// gui_shots: offscreen screenshot tool for the user guide (docs/guide).
//
// Builds the real Ripes::MainWindow under the "offscreen" QPA platform, runs a
// small script of GUI actions (switch tab, load an example, clock, add a
// peripheral, open a dialog, ...) and grab()s a widget to a PNG. The scripts
// live in docs/guide/images/manifest_*.json ("script" array of each entry);
// tools/gui_shots/run.ps1 runs every entry. See tools/gui_shots/README.md for
// the command reference.
//
// Settings isolation: QSettings is switched to an INI file in a private
// temporary directory before anything reads a setting, so the user's real
// settings (HKCU\Software\Ripes\Ripes on Windows) are neither read nor
// written.

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMetaEnum>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QResource>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleHints>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QTreeWidget>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>

#include "archexplorer/archexplorertab.h"
#include "archexplorer/archscene.h"
#include "archexplorer/archview.h"
#include "cachetab.h"
#include "edittab.h"
#include "editor/codeeditor.h"
#include "fancytabbar/fancytabbar.h"
#include "helpdialog.h"
#include "i18n.h"
#include "io/iobase.h"
#include "io/iogpu.h"
#include "io/iomanager.h"
#include "io/ioregistry.h"
#include "io/iotensorunit.h"
#include "iotab.h"
#include "loaddialog.h"
#include "mainwindow.h"
#include "memorytab.h"
#include "memoryviewerwidget.h"
#include "pipelinediagrammodel.h"
#include "pipelinediagramwidget.h"
#include "processorhandler.h"
#include "processorregistry.h"
#include "processorselectiondialog.h"
#include "processortab.h"
#include "ripessettings.h"
#include "settingsdialog.h"

using namespace Ripes;

namespace {

// ----------------------------------------------------------------- helpers

/// Private temporary directory (settings INI + scratch files); removed on
/// exit because the tool leaves through std::_Exit.
QTemporaryDir *g_tmpDir = nullptr;

[[noreturn]] void quit(int code) {
  std::fflush(stdout);
  std::fflush(stderr);
  if (g_tmpDir)
    QDir(g_tmpDir->path()).removeRecursively();
  std::_Exit(code);
}

[[noreturn]] void fail(const QString &msg) {
  std::cerr << "gui_shots: ERROR: " << msg.toStdString() << std::endl;
  quit(2);
}

void info(const QString &msg) {
  std::cout << "gui_shots: " << msg.toStdString() << std::endl;
}

void settle(int ms = 150) {
  QElapsedTimer t;
  t.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  } while (t.elapsed() < ms);
}

/// Same as main.cpp: the offscreen platform only knows Qt's own fonts, which
/// have no CJK glyphs; register a system CJK font as fallback.
void addOffscreenCJKFallbackFont() {
  if (QGuiApplication::platformName() != "offscreen")
    return;
  const QStringList candidates = {
      "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simsun.ttc",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
      "/System/Library/Fonts/PingFang.ttc"};
  for (const QString &file : candidates) {
    if (!QFileInfo::exists(file))
      continue;
    const int id = QFontDatabase::addApplicationFont(file);
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty())
      continue;
    for (auto script : {QChar::Script_Han, QChar::Script_Common})
      QFontDatabase::addApplicationFallbackFontFamily(script, families.first());
    return;
  }
}

/// Splits a script line into words; "double quoted" words may contain spaces.
QStringList splitWords(const QString &line) {
  QStringList out;
  QString cur;
  bool quoted = false, any = false;
  for (QChar c : line) {
    if (c == '"') {
      quoted = !quoted;
      any = true;
    } else if (c.isSpace() && !quoted) {
      if (any || !cur.isEmpty())
        out << cur;
      cur.clear();
      any = false;
    } else {
      cur += c;
    }
  }
  if (any || !cur.isEmpty())
    out << cur;
  return out;
}

qulonglong parseNumber(const QString &s) {
  bool ok = false;
  const qulonglong v = s.startsWith("0x", Qt::CaseInsensitive)
                           ? s.mid(2).toULongLong(&ok, 16)
                           : s.toULongLong(&ok, 10);
  if (!ok)
    fail("not a number: " + s);
  return v;
}

/// Finds the first widget below @p root (or any top-level widget when root is
/// null) whose objectName or class name (with or without "Ripes::") is
/// @p name. Visible widgets are preferred.
QWidget *findWidget(QWidget *root, const QString &name) {
  QList<QWidget *> candidates;
  auto consider = [&](QWidget *w) {
    const QString cls = w->metaObject()->className();
    if (w->objectName() == name || cls == name || cls == "Ripes::" + name)
      candidates << w;
  };
  if (root) {
    consider(root);
    for (auto *w : root->findChildren<QWidget *>())
      consider(w);
  } else {
    for (auto *top : QApplication::topLevelWidgets()) {
      consider(top);
      for (auto *w : top->findChildren<QWidget *>())
        consider(w);
    }
  }
  for (auto *w : candidates)
    if (w->isVisible())
      return w;
  return candidates.isEmpty() ? nullptr : candidates.first();
}

// ----------------------------------------------------------------- the tool

class Shooter {
public:
  Shooter(const QString &lang, const QJsonObject &entry, const QString &outPath)
      : m_lang(lang), m_entry(entry), m_out(outPath) {}

  /// Commands that must run before the main window exists (they only set
  /// settings). Returns the remaining commands.
  QStringList applyPreWindow(const QStringList &script);
  void createWindow();
  void run(const QStringList &script);

private:
  void exec(const QStringList &w);

  // Accessors.
  template <typename T>
  T *tab() const {
    auto *t = m_mw->findChild<T *>();
    if (!t)
      fail(QString("tab not found: ") + T::staticMetaObject.className());
    return t;
  }
  QAction *actionByShortcut(const QString &key) const;
  QToolBar *controlToolbar() const;
  IOBase *peripheral(const QString &name) const;
  IOGpu *gpu() const;
  IOTensorUnit *npu() const;
  AInt resolveAddress(const QString &expr) const;

  // Actions.
  void setTab(const QString &name);
  void loadProgram(const QString &path);
  void loadSource(const QString &text);
  void waitForProgram(const std::function<void()> &trigger);
  void addPeripheral(const QStringList &w);
  void clock(int n);
  void runToEnd(int timeoutMs);
  void runFor(int ms);
  void clockUntil(const QStringList &w);
  void archCommand(const QStringList &w);
  void openDialog(const QStringList &w);
  void hoverError(int line);
  void grab(const QStringList &w);

  QString m_lang;
  QJsonObject m_entry;
  QString m_out;
  QSize m_size{1280, 800};
  MainWindow *m_mw = nullptr;
  QWidget *m_dialog = nullptr;
  QWidget *m_hoverTarget = nullptr;
  bool m_overlayPopups = false;
  QRect m_crop;
  int m_maxWidth = 0;
  QDir m_tmp{g_tmpDir->path()};
};

QStringList Shooter::applyPreWindow(const QStringList &script) {
  QStringList rest;
  // Defaults for a reproducible screenshot: welcome panel hidden, light
  // theme, default processor.
  RipesSettings::setValue(RIPES_SETTING_SHOW_WELCOME, false);
  RipesSettings::setValue(RIPES_SETTING_COLORSCHEME, 1);
  for (const QString &line : script) {
    const QStringList w = splitWords(line);
    if (w.isEmpty())
      continue;
    const QString &cmd = w[0];
    if (cmd == "size" && w.size() == 3) {
      m_size = QSize(w[1].toInt(), w[2].toInt());
    } else if (cmd == "welcome" && w.size() == 2) {
      RipesSettings::setValue(RIPES_SETTING_SHOW_WELCOME, w[1] == "on");
    } else if (cmd == "processor" && w.size() >= 2) {
      bool ok = false;
      const int id = QMetaEnum::fromType<ProcessorID>().keyToValue(
          w[1].toLatin1().constData(), &ok);
      if (!ok)
        fail("unknown processor id " + w[1]);
      RipesSettings::setValue(RIPES_SETTING_PROCESSOR_ID, id);
      RipesSettings::setValue(
          RIPES_SETTING_PROCESSOR_EXTENSIONS,
          ProcessorRegistry::getDescription(static_cast<ProcessorID>(id))
              .isaInfo()
              .supportedExtensions);
      int layout = 0;
      if (w.size() >= 4 && w[2] == "layout")
        layout = w[3].toInt();
      RipesSettings::setValue(RIPES_SETTING_PROCESSOR_LAYOUT_ID, layout);
    } else if (cmd == "setting" && w.size() == 3) {
      bool isInt = false;
      const int v = w[2].toInt(&isInt);
      RipesSettings::setValue(w[1], isInt ? QVariant(v) : QVariant(w[2]));
    } else {
      rest << line;
    }
  }
  return rest;
}

void Shooter::createWindow() {
  m_mw = new MainWindow();
  m_mw->resize(m_size);
  m_mw->show();
  settle(300);
  m_mw->resize(m_size);
  m_mw->fitToView();
  settle(200);
}

void Shooter::run(const QStringList &script) {
  for (const QString &line : script) {
    const QStringList w = splitWords(line);
    if (w.isEmpty())
      continue;
    info("> " + line);
    exec(w);
    settle(60);
  }
}

QAction *Shooter::actionByShortcut(const QString &key) const {
  const QKeySequence seq(key);
  for (auto *a : m_mw->findChildren<QAction *>())
    if (a->shortcuts().contains(seq))
      return a;
  fail("no action with shortcut " + key);
}

QToolBar *Shooter::controlToolbar() const {
  QAction *clockAction = actionByShortcut("F5");
  for (auto *tb : m_mw->findChildren<QToolBar *>())
    if (tb->actions().contains(clockAction))
      return tb;
  fail("simulator control toolbar not found");
}

IOBase *Shooter::peripheral(const QString &name) const {
  for (auto *p : IOManager::get().peripherals())
    if (p->name() == name || p->baseName() == name ||
        QString(p->metaObject()->className()) == "Ripes::" + name)
      return p;
  fail("peripheral not found: " + name);
}

IOGpu *Shooter::gpu() const {
  for (auto *p : IOManager::get().peripherals())
    if (auto *g = dynamic_cast<IOGpu *>(p))
      return g;
  fail("no SIMT GPU peripheral");
}

IOTensorUnit *Shooter::npu() const {
  for (auto *p : IOManager::get().peripherals())
    if (auto *n = dynamic_cast<IOTensorUnit *>(p))
      return n;
  fail("no Tensor Unit peripheral");
}

/// "0x1234", "section:.data", "reg:sp" / "reg:x2", "sym:label" or
/// "sym:label+8".
AInt Shooter::resolveAddress(const QString &expr) const {
  if (expr.startsWith("section:")) {
    auto prog = ProcessorHandler::getProgram();
    if (!prog)
      fail("no program loaded");
    const QString name = expr.mid(8);
    for (const auto &sec : prog->sections)
      if (sec.second.name == name)
        return sec.second.address;
    fail("no section " + name);
  }
  if (expr.startsWith("reg:")) {
    static const QStringList abi = {
        "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
        "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
        "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
    const QString r = expr.mid(4);
    int idx = abi.indexOf(r);
    if (idx < 0 && r.startsWith('x'))
      idx = r.mid(1).toInt();
    return ProcessorHandler::getRegisterValue("gpr", idx);
  }
  if (expr.startsWith("sym:")) {
    auto prog = ProcessorHandler::getProgram();
    if (!prog)
      fail("no program loaded");
    QString name = expr.mid(4);
    AInt offset = 0;
    const int plus = name.indexOf('+');
    if (plus > 0) {
      offset = parseNumber(name.mid(plus + 1));
      name = name.left(plus);
    }
    for (const auto &s : prog->symbols)
      if (s.second.v == name)
        return s.first + offset;
    fail("no symbol " + name);
  }
  return parseNumber(expr);
}

void Shooter::setTab(const QString &name) {
  static const QStringList names = {"editor", "processor", "cache",
                                    "memory", "io",        "architecture"};
  const int idx = names.indexOf(name.toLower());
  if (idx < 0)
    fail("unknown tab " + name);
  auto *bar = m_mw->findChild<FancyTabBar *>();
  if (!bar)
    fail("tab bar not found");
  bar->setActiveIndex(idx);
  settle(250);
  if (idx == 1)
    m_mw->fitToView();
}

void Shooter::waitForProgram(const std::function<void()> &trigger) {
  bool changed = false;
  auto c = QObject::connect(ProcessorHandler::get(),
                            &ProcessorHandler::programChanged,
                            [&] { changed = true; });
  trigger();
  QElapsedTimer t;
  t.start();
  while (!changed && t.elapsed() < 5000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  QObject::disconnect(c);
  settle(300);
  if (!changed)
    info("warning: no programChanged signal (assembler error?)");
}

void Shooter::loadProgram(const QString &pathIn) {
  // ":/examples/..." resources, or a path relative to the repository root.
  QString path = pathIn;
  if (!path.startsWith(":/") && QFileInfo(path).isRelative())
    path = QDir(qEnvironmentVariable("GUI_SHOTS_REPO", ".")).filePath(path);
  if (!QFileInfo::exists(path))
    fail("program not found: " + path);
  LoadFileParams params;
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (suffix == "s" || suffix == "asm") {
    params.filepath = path;
    params.type = SourceType::Assembly;
  } else if (suffix == "c") {
    params.filepath = path;
    params.type = SourceType::C;
  } else {
    // ELF. ELFIO cannot read from resources; copy those to a real file.
    params.filepath = path;
    if (path.startsWith(":/")) {
      const QString copy = m_tmp.filePath(QFileInfo(path).fileName());
      QFile::remove(copy);
      if (!QFile::copy(path, copy))
        fail("cannot copy " + path);
      params.filepath = copy;
    }
    params.type = SourceType::ExternalELF;
  }
  params.binaryEntryPoint = 0;
  params.binaryLoadAt = 0;
  auto *edit = tab<EditTab>();
  waitForProgram([&] { edit->loadExternalFile(params); });
}

void Shooter::loadSource(const QString &text) {
  const QString file = m_tmp.filePath("source.s");
  QFile f(file);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    fail("cannot write " + file);
  f.write(text.toUtf8());
  f.close();
  loadProgram(file);
}

void Shooter::addPeripheral(const QStringList &w) {
  // peripheral "<English device title>" [paramId=value ...]
  if (w.size() < 2)
    fail("peripheral: missing device title");
  IOType type = IOType::NPERIPHERALS;
  for (const auto &it : IOTypeTitles)
    if (w[1] == it.second)
      type = it.first;
  if (type == IOType::NPERIPHERALS)
    fail("unknown device " + w[1]);
  auto *io = tab<IOTab>();
  auto *table = io->findChild<QTableWidget *>("peripheralsTable");
  auto *add = io->findChild<QPushButton *>("addPeripheralButton");
  if (!table || !add)
    fail("I/O tab device list not found");
  const auto before = IOManager::get().peripherals();
  for (int r = 0; r < table->rowCount(); ++r)
    if (table->item(r, 0)->data(Qt::UserRole).value<IOType>() == type)
      table->selectRow(r);
  add->click();
  settle(200);
  IOBase *created = nullptr;
  for (auto *p : IOManager::get().peripherals())
    if (!before.count(p))
      created = p;
  if (!created)
    fail("peripheral was not created");
  for (int i = 2; i < w.size(); ++i) {
    const QStringList kv = w[i].split('=');
    if (kv.size() != 2)
      fail("bad parameter " + w[i]);
    if (!created->setParameter(kv[0].toUInt(), kv[1].toInt()))
      fail("cannot set parameter " + w[i]);
  }
  settle(200);
}

void Shooter::clock(int n) {
  QAction *clk = actionByShortcut("F5");
  for (int i = 0; i < n; ++i) {
    if (!clk->isEnabled())
      break;
    clk->trigger();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }
  settle(250);
}

void Shooter::runToEnd(int timeoutMs) {
  QAction *runAction = actionByShortcut("F8");
  if (!runAction->isEnabled())
    fail("Run is disabled");
  bool finished = false;
  auto c = QObject::connect(ProcessorHandler::get(),
                            &ProcessorHandler::runFinished,
                            [&] { finished = true; });
  runAction->trigger();
  QElapsedTimer t;
  t.start();
  while (!finished && t.elapsed() < timeoutMs)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  QObject::disconnect(c);
  if (!finished) {
    info("run did not finish in time; stopping");
    if (runAction->isChecked())
      runAction->trigger();
  }
  settle(800);
}

void Shooter::runFor(int ms) {
  QAction *runAction = actionByShortcut("F8");
  runAction->trigger();
  settle(ms);
  if (runAction->isChecked())
    runAction->trigger();
  settle(800);
}

void Shooter::clockUntil(const QStringList &w) {
  // until <condition...> [max N]
  int max = 5000;
  QStringList cond = w.mid(1);
  const int maxIdx = cond.indexOf("max");
  if (maxIdx >= 0 && maxIdx + 1 < cond.size()) {
    max = cond[maxIdx + 1].toInt();
    cond = cond.mid(0, maxIdx);
  }
  if (cond.isEmpty())
    fail("until: missing condition");

  std::function<bool()> pred;
  const QString what = cond[0];
  if (what == "pc" && cond.size() >= 2) {
    // until pc <addr expr> [stage N] [count K]: the instruction at addr is in
    // stage N (default 0 = IF).
    // "count K": the K-th cycle in which this holds.
    const AInt addr = resolveAddress(cond[1]);
    unsigned stage = 0, count = 1;
    for (int k = 2; k + 1 < cond.size(); k += 2) {
      if (cond[k] == "stage")
        stage = cond[k + 1].toUInt();
      else if (cond[k] == "count")
        count = cond[k + 1].toUInt();
    }
    auto seen = std::make_shared<unsigned>(0);
    pred = [addr, stage, count, seen] {
      if (ProcessorHandler::getProcessor()->getPcForStage({0, stage}) == addr)
        ++*seen;
      return *seen >= count;
    };
  } else if (what == "gpu.reg" && cond.size() >= 2) {
    // until gpu.reg <index> nonzero
    const unsigned reg = cond[1].toUInt();
    pred = [this, reg] { return gpu()->view().regs.at(reg) != 0; };
  } else if (what == "gpu.launched") {
    pred = [this] { return gpu()->view().launched; };
  } else if (what == "gpu.issued" && cond.size() >= 4) {
    // until gpu.issued <sm> <pc> <count>: SM <sm> has issued an instruction
    // at kernel pc <pc> <count> times.
    const unsigned sm = cond[1].toUInt(), pc = cond[2].toUInt(),
                   count = cond[3].toUInt();
    auto seen = std::make_shared<unsigned>(0);
    auto lastIssued = std::make_shared<quint64>(0);
    pred = [this, sm, pc, count, seen, lastIssued] {
      const auto v = gpu()->view();
      if (sm >= v.sms.size())
        return false;
      const auto &s = v.sms[sm];
      if (s.issued != *lastIssued && s.issuedWarp >= 0 &&
          s.issuedWarp < int(s.warps.size())) {
        const auto &warp = s.warps[s.issuedWarp];
        if (warp.hasLastInstr && warp.lastPc == pc)
          ++*seen;
      }
      *lastIssued = s.issued;
      return *seen >= count;
    };
  } else if (what == "npu.state" && cond.size() >= 2) {
    // until npu.state <IDLE|FETCH_DESC|DMA_IN|COMPUTE|DMA_OUT|COMPLETE>
    static const QStringList states = {"IDLE",    "FETCH_DESC", "DMA_IN",
                                       "COMPUTE", "DMA_OUT",    "COMPLETE"};
    const int s = states.indexOf(cond[1]);
    if (s < 0)
      fail("unknown NPU state " + cond[1]);
    pred = [this, s] {
      return static_cast<int>(npu()->viewData().snap.state) == s;
    };
  } else if (what == "npu.compute" && cond.size() >= 2) {
    // until npu.compute <t>: systolic pass (not weight load) at cycle >= t
    const unsigned t = cond[1].toUInt();
    pred = [this, t] {
      const auto snap = npu()->viewData().snap;
      return snap.state == npu::State::COMPUTE && !snap.weightLoading &&
             snap.computeCycle >= t;
    };
  } else if (what == "npu.dma" && cond.size() >= 2) {
    // until npu.dma <bytes>: the current DMA has transferred >= bytes
    const unsigned b = cond[1].toUInt();
    pred = [this, b] { return npu()->viewData().snap.dma.bytesDone >= b; };
  } else {
    fail("unknown until condition " + cond.join(' '));
  }

  QAction *clk = actionByShortcut("F5");
  int i = 0;
  for (; i < max && !pred(); ++i) {
    if (!clk->isEnabled())
      fail("processor finished before the condition was met");
    clk->trigger();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }
  if (i >= max)
    fail("condition not met within " + QString::number(max) + " cycles");
  info(QString("  condition met after %1 cycles").arg(i));
  settle(300);
}

void Shooter::archCommand(const QStringList &w) {
  auto *arch = tab<ArchExplorerTab>();
  const QString sub = w.value(1);
  if (sub == "topology") {
    for (int i = 0; i < arch->topologyCount(); ++i) {
      arch->setCurrentTopology(i);
      if (arch->currentModel() && arch->currentModel()->id == w.value(2)) {
        settle(300);
        return;
      }
    }
    fail("unknown topology " + w.value(2));
  } else if (sub == "flow") {
    const ArchModel *m = arch->currentModel();
    for (int i = 0; m && i < m->flows.size(); ++i)
      if (m->flows[i].id == w.value(2) || QString::number(i) == w.value(2)) {
        arch->selectFlow(i);
        settle(200);
        return;
      }
    fail("unknown flow " + w.value(2));
  } else if (sub == "next") {
    const int n = w.value(2, "1").toInt();
    for (int i = 0; i < n; ++i) {
      arch->nextStep();
      settle(100);
    }
  } else if (sub == "step") {
    arch->setStep(w.value(2).toInt());
  } else if (sub == "block") {
    auto *scene = arch->findChild<ArchScene *>();
    if (!scene)
      fail("ArchScene not found");
    scene->selectBlock(w.value(2));
    if (arch->detailsBlockId() != w.value(2))
      fail("block not selected: " + w.value(2));
  } else if (sub == "fit") {
    auto *view = arch->findChild<ArchView *>();
    if (!view)
      fail("ArchView not found");
    view->fitDiagram();
  } else if (sub == "splitter") {
    // arch splitter <objectName-or-index> <size,size,...>
    const auto splitters = arch->findChildren<QSplitter *>();
    QSplitter *s = nullptr;
    bool isIndex = false;
    const int idx = w.value(2).toInt(&isIndex);
    if (isIndex && idx < splitters.size())
      s = splitters[idx];
    for (auto *c : splitters)
      if (c->objectName() == w.value(2))
        s = c;
    if (!s)
      fail("splitter not found " + w.value(2));
    QList<int> sizes;
    for (const auto &v : w.value(3).split(','))
      sizes << v.toInt();
    s->setSizes(sizes);
  } else {
    fail("unknown arch command " + sub);
  }
  settle(250);
}

void Shooter::openDialog(const QStringList &w) {
  const QString kind = w.value(1);
  QDialog *d = nullptr;
  if (kind == "settings") {
    // dialog settings <page index: 0 Environment, 1 Simulator, 2 Compiler,
    // 3 Editor>
    RipesSettings::setValue(RIPES_SETTING_SETTING_TAB, w.value(2, "0").toInt());
    d = new SettingsDialog(m_mw);
  } else if (kind == "processor") {
    d = new ProcessorSelectionDialog(m_mw);
  } else if (kind == "load") {
    // dialog load <elf|binary|source> [file]
    auto *ld = new LoadDialog(m_mw);
    d = ld;
    const QString type = w.value(2, "source");
    const char *radio = type == "elf"      ? "elfRadioButton"
                        : type == "binary" ? "binaryRadioButton"
                                           : "sourceRadioButton";
    if (auto *rb = ld->findChild<QRadioButton *>(radio))
      rb->click();
    if (w.size() > 3) {
      QString path = w[3];
      if (path.startsWith(":/")) {
        const QString copy = m_tmp.filePath(QFileInfo(path).fileName());
        QFile::remove(copy);
        QFile::copy(path, copy);
        path = copy;
      } else if (QFileInfo(path).isRelative()) {
        path = QDir(qEnvironmentVariable("GUI_SHOTS_REPO", "."))
                   .absoluteFilePath(path);
      }
      if (auto *le = ld->findChild<QLineEdit *>("filePath")) {
        le->setText(QDir::toNativeSeparators(path));
        le->setCursorPosition(0);
      }
    }
  } else if (kind == "pipeline") {
    auto *ptab = tab<ProcessorTab>();
    auto *model = ptab->findChild<PipelineDiagramModel *>();
    if (!model)
      fail("pipeline diagram model not found");
    d = new PipelineDiagramWidget(model, m_mw);
  } else if (kind == "help") {
    d = HelpDialog::open(w.value(2));
  } else {
    fail("unknown dialog " + kind);
  }
  if (w.contains("size")) {
    const int i = w.indexOf("size");
    d->resize(w.value(i + 1).toInt(), w.value(i + 2).toInt());
  }
  d->show();
  settle(500);
  if (w.contains("size")) {
    const int i = w.indexOf("size");
    d->resize(w.value(i + 1).toInt(), w.value(i + 2).toInt());
    settle(300);
  }
  m_dialog = d;
}

void Shooter::hoverError(int line) {
  // Shows the assembler error tooltip of (1-based) source line @p line.
  auto *editor = m_mw->findChild<CodeEditor *>();
  if (!editor)
    fail("code editor not found");
  QTextBlock block = editor->document()->findBlockByNumber(line - 1);
  QTextCursor cursor(block);
  QRect r = editor->cursorRect(cursor);
  const QPoint pos = r.center() + QPoint(40, 0);
  QHelpEvent ev(QEvent::ToolTip, pos, editor->viewport()->mapToGlobal(pos));
  QCoreApplication::sendEvent(editor, &ev);
  settle(500);
  m_overlayPopups = true;
}

void Shooter::grab(const QStringList &w) {
  // grab window | tab | toolbar | dialog | periph <name> | widget <name>
  //      [crop x y w h] [maxwidth W]
  const QString what = w.value(1, "window");
  QWidget *target = nullptr;
  QImage exported;
  int i = 2;
  if (what == "archexport") {
    // The Architecture tab's own "Export PNG" (whole diagram + legend, 2x).
    const QString file = m_tmp.filePath("archexport.png");
    if (!tab<ArchExplorerTab>()->exportDiagram(file) || !exported.load(file))
      fail("architecture export failed");
  } else if (what == "window") {
    target = m_mw;
  } else if (what == "tab") {
    auto *stack = m_mw->findChild<QStackedWidget *>();
    // The tab stack is the one holding the EditTab.
    for (auto *s : m_mw->findChildren<QStackedWidget *>())
      if (s->indexOf(tab<EditTab>()) >= 0)
        stack = s;
    target = stack->currentWidget();
  } else if (what == "toolbar") {
    target = controlToolbar();
  } else if (what == "dialog") {
    target = m_dialog;
  } else if (what == "periph") {
    // The MDI subwindow containing the peripheral (title bar included).
    IOBase *p = peripheral(w.value(2));
    target = p;
    for (QWidget *pw = p; pw; pw = pw->parentWidget())
      if (qobject_cast<QMdiSubWindow *>(pw)) {
        target = pw;
        break;
      }
    i = 3;
  } else if (what == "periphwidget") {
    // The peripheral widget alone; "size W H" first resizes its MDI subwindow
    // (the I/O tab's MDI area is usually narrower than the widget wants).
    target = peripheral(w.value(2));
    i = 3;
    const int sz = w.indexOf("size");
    if (sz > 0 && sz + 2 < w.size()) {
      for (QWidget *pw = target; pw; pw = pw->parentWidget())
        if (auto *sub = qobject_cast<QMdiSubWindow *>(pw)) {
          sub->resize(w[sz + 1].toInt(), w[sz + 2].toInt());
          break;
        }
      settle(400);
    }
  } else if (what == "widget") {
    QWidget *root = m_dialog ? m_dialog : m_mw;
    target = findWidget(root, w.value(2));
    if (!target)
      target = findWidget(nullptr, w.value(2));
    i = 3;
  } else {
    fail("unknown grab target " + what);
  }
  if (!target && exported.isNull())
    fail("grab target not found: " + w.join(' '));
  for (; i < w.size(); ++i) {
    if (w[i] == "size") {
      i += 2;
    } else if (w[i] == "crop" && i + 4 < w.size()) {
      m_crop = QRect(w[i + 1].toInt(), w[i + 2].toInt(), w[i + 3].toInt(),
                     w[i + 4].toInt());
      i += 4;
    } else if (w[i] == "maxwidth" && i + 1 < w.size()) {
      m_maxWidth = w[++i].toInt();
    }
  }

  settle(300);
  QPixmap pm = target ? target->grab() : QPixmap::fromImage(exported);
  if (m_overlayPopups && target) {
    // Paint visible tooltips/popups (separate top-level windows) on top.
    QPainter p(&pm);
    for (auto *top : QApplication::topLevelWidgets()) {
      if (!top->isVisible() || top == target || top == m_mw ||
          top == m_dialog)
        continue;
      const auto type = top->windowType();
      if (type != Qt::ToolTip && type != Qt::Popup)
        continue;
      const QPoint at = target->mapFromGlobal(top->mapToGlobal(QPoint(0, 0)));
      p.drawPixmap(at, top->grab());
    }
  }
  QImage img = pm.toImage();
  if (!m_crop.isNull())
    img = img.copy(m_crop);
  if (m_maxWidth > 0 && img.width() > m_maxWidth)
    img = img.scaledToWidth(m_maxWidth, Qt::SmoothTransformation);
  img = img.convertToFormat(QImage::Format_RGB888);
  QDir().mkpath(QFileInfo(m_out).absolutePath());
  if (!img.save(m_out, "PNG", 0))
    fail("cannot save " + m_out);
  info(QString("saved %1 (%2x%3)")
           .arg(m_out)
           .arg(img.width())
           .arg(img.height()));
}

void Shooter::exec(const QStringList &w) {
  const QString &cmd = w[0];
  if (cmd == "tab") {
    setTab(w.value(1));
  } else if (cmd == "load") {
    loadProgram(w.value(1));
  } else if (cmd == "source") {
    // Inline program from the manifest entry's "source" field.
    loadSource(m_entry.value("source").toString());
  } else if (cmd == "peripheral") {
    addPeripheral(w);
  } else if (cmd == "clock") {
    clock(w.value(1, "1").toInt());
  } else if (cmd == "run") {
    runToEnd(w.value(1, "60000").toInt());
  } else if (cmd == "runfor") {
    runFor(w.value(1, "500").toInt());
  } else if (cmd == "reset") {
    actionByShortcut("F3")->trigger();
    settle(300);
  } else if (cmd == "until") {
    clockUntil(w);
  } else if (cmd == "break") {
    ProcessorHandler::setBreakpoint(resolveAddress(w.value(1)), true);
  } else if (cmd == "fit") {
    m_mw->fitToView();
  } else if (cmd == "memgoto") {
    auto *viewer = tab<MemoryTab>()->findChild<MemoryViewerWidget *>();
    if (!viewer)
      fail("memory viewer not found");
    viewer->setCentralAddress(resolveAddress(w.value(1)));
  } else if (cmd == "combo") {
    // combo <objectName> <index> [nth]
    QList<QComboBox *> boxes;
    QWidget *root = m_dialog ? m_dialog : m_mw;
    for (auto *b : root->findChildren<QComboBox *>(w.value(1)))
      boxes << b;
    const int nth = w.value(3, "0").toInt();
    if (nth >= boxes.size())
      fail("combo box not found " + w.value(1));
    boxes[nth]->setCurrentIndex(w.value(2).toInt());
  } else if (cmd == "spin") {
    QWidget *root = m_dialog ? m_dialog : m_mw;
    auto *s = root->findChild<QSpinBox *>(w.value(1));
    if (!s)
      fail("spin box not found " + w.value(1));
    s->setValue(w.value(2).toInt());
  } else if (cmd == "click") {
    // click <objectName or text of a button>
    QWidget *root = m_dialog ? m_dialog : m_mw;
    QAbstractButton *btn = root->findChild<QAbstractButton *>(w.value(1));
    if (!btn)
      for (auto *b : root->findChildren<QAbstractButton *>())
        if (b->text() == w.value(1))
          btn = b;
    if (!btn)
      fail("button not found " + w.value(1));
    btn->click();
  } else if (cmd == "subtab") {
    // subtab <QTabWidget objectName or class> <index> [periph <name>]
    QWidget *root = m_dialog ? m_dialog : m_mw;
    if (w.value(3) == "periph")
      root = peripheral(w.value(4));
    auto *tw = qobject_cast<QTabWidget *>(findWidget(root, w.value(1)));
    if (!tw)
      fail("tab widget not found " + w.value(1));
    tw->setCurrentIndex(w.value(2).toInt());
  } else if (cmd == "select") {
    // select <QTreeWidget/QListWidget objectName> <text>
    QWidget *root = m_dialog ? m_dialog : m_mw;
    QWidget *wd = findWidget(root, w.value(1));
    if (auto *tree = qobject_cast<QTreeWidget *>(wd)) {
      const auto items =
          tree->findItems(w.value(2), Qt::MatchExactly | Qt::MatchRecursive);
      if (items.isEmpty())
        fail("tree item not found " + w.value(2));
      tree->setCurrentItem(items.first());
    } else if (auto *list = qobject_cast<QListWidget *>(wd)) {
      const auto items = list->findItems(w.value(2), Qt::MatchExactly);
      if (items.isEmpty())
        fail("list item not found " + w.value(2));
      list->setCurrentItem(items.first());
    } else {
      fail("no tree/list " + w.value(1));
    }
  } else if (cmd == "resize") {
    // resize <widget name> W H
    QWidget *root = m_dialog ? m_dialog : m_mw;
    QWidget *wd = findWidget(root, w.value(1));
    if (!wd)
      fail("widget not found " + w.value(1));
    wd->resize(w.value(2).toInt(), w.value(3).toInt());
  } else if (cmd == "mdiwin") {
    // mdiwin <peripheral> x y w h: place a peripheral's MDI subwindow.
    QWidget *pw = peripheral(w.value(1));
    for (; pw; pw = pw->parentWidget())
      if (auto *sub = qobject_cast<QMdiSubWindow *>(pw)) {
        sub->setGeometry(w.value(2).toInt(), w.value(3).toInt(),
                         w.value(4).toInt(), w.value(5).toInt());
        break;
      }
    if (!pw)
      fail("no MDI subwindow for " + w.value(1));
    settle(200);
  } else if (cmd == "mdi") {
    // mdi tile | mdi cascade
    auto *area = tab<IOTab>()->findChild<QMdiArea *>();
    if (!area)
      fail("MDI area not found");
    if (w.value(1) == "cascade")
      area->cascadeSubWindows();
    else
      area->tileSubWindows();
  } else if (cmd == "arch") {
    archCommand(w);
  } else if (cmd == "dialog") {
    openDialog(w);
  } else if (cmd == "hover_error") {
    hoverError(w.value(1).toInt());
  } else if (cmd == "browserfind") {
    // browserfind <text>: scroll the (help) text browser to a phrase and put
    // it at the top of the view.
    QWidget *root = m_dialog ? m_dialog : m_mw;
    auto *tb = qobject_cast<QTextBrowser *>(findWidget(root, "QTextBrowser"));
    if (!tb)
      fail("no text browser");
    const QString text = w.mid(1).join(' ');
    tb->moveCursor(QTextCursor::Start);
    if (!tb->find(text))
      fail("text not found in browser: " + text);
    const QRect r = tb->cursorRect();
    auto *bar = tb->verticalScrollBar();
    bar->setValue(bar->value() + r.top() - 8);
    settle(300);
  } else if (cmd == "wait") {
    settle(w.value(1, "500").toInt());
  } else if (cmd == "dumpmin") {
    // dumpmin [minHeight] [minWidth]: prints the visible widget tree with
    // each widget's effective minimum size, to find what makes a window
    // tall or wide. Only widgets at or above either threshold are listed.
    const int minH = w.value(1, "0").toInt();
    const int minW = w.value(2, "0").toInt();
    const bool all = w.value(3) == "all";
    std::function<void(QWidget *, int)> dump = [&](QWidget *wd, int depth) {
      if (!all && !wd->isVisible())
        return;
      const QSize m = wd->minimumSizeHint().expandedTo(wd->minimumSize());
      const QSize eff = wd->minimumSize().isNull()
                            ? m
                            : wd->minimumSize();
      if (eff.height() >= minH || eff.width() >= minW)
        info(QString(depth * 2, ' ') + (wd->isVisible() ? "" : "(hidden) ") +
             wd->metaObject()->className() + " '" +
             wd->objectName() + "' min=" + QString::number(eff.width()) + "x" +
             QString::number(eff.height()) + " hint=" +
             QString::number(wd->minimumSizeHint().width()) + "x" +
             QString::number(wd->minimumSizeHint().height()) + " size=" +
             QString::number(wd->width()) + "x" +
             QString::number(wd->height()));
      for (auto *c : wd->findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
        if (!c->isWindow())
          dump(c, depth + 1);
    };
    dump(m_dialog ? static_cast<QWidget *>(m_dialog) : m_mw, 0);
  } else if (cmd == "grab") {
    grab(w);
  } else {
    fail("unknown command: " + w.join(' '));
  }
}

} // namespace

int main(int argc, char **argv) {
  Q_INIT_RESOURCE(icons);
  Q_INIT_RESOURCE(examples);
  Q_INIT_RESOURCE(layouts);
  Q_INIT_RESOURCE(fonts);

  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");

  // Settings isolation: INI file in a private directory (see file header).
  // Private temporary directory, also used as the process' TEMP: Ripes writes
  // the I/O symbol header to QDir::tempPath()/ripes_system.h, which must not be
  // shared between parallel gui_shots runs (or with a running Ripes).
  g_tmpDir = new QTemporaryDir(QDir::tempPath() + "/gui_shots-XXXXXX");
  if (!g_tmpDir->isValid())
    fail("cannot create a temporary directory");
  for (const char *var : {"TMP", "TEMP", "TMPDIR"})
    qputenv(var, QDir::toNativeSeparators(g_tmpDir->path()).toLocal8Bit());
  const QString settingsPath = g_tmpDir->filePath("settings");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsPath);
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                     settingsPath);

  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("Ripes");
  QCoreApplication::setApplicationName("Ripes");

  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Offscreen screenshot tool for the Ripes user guide.");
  parser.addHelpOption();
  QCommandLineOption langOpt("lang", "UI language: zh or en.", "lang", "en");
  QCommandLineOption manifestOpt("manifest", "Manifest JSON file.", "file");
  QCommandLineOption entryOpt("entry", "File name of the manifest entry.",
                              "name");
  QCommandLineOption scriptOpt(
      "script", "Script lines separated by ';' (instead of a manifest).",
      "script");
  QCommandLineOption outOpt("out", "Output PNG (default: <manifest dir>/<entry>).",
                            "png");
  parser.addOptions({langOpt, manifestOpt, entryOpt, scriptOpt, outOpt});
  parser.process(app);

  QJsonObject entry;
  QStringList script;
  QString out = parser.value(outOpt);
  QString lang = parser.value(langOpt);
  if (parser.isSet(manifestOpt)) {
    QFile f(parser.value(manifestOpt));
    if (!f.open(QIODevice::ReadOnly))
      fail("cannot open manifest " + f.fileName());
    const auto doc = QJsonDocument::fromJson(f.readAll());
    for (const auto &v : doc.array())
      if (v.toObject().value("file").toString() == parser.value(entryOpt))
        entry = v.toObject();
    if (entry.isEmpty())
      fail("entry not found: " + parser.value(entryOpt));
    for (const auto &l : entry.value("script").toArray())
      script << l.toString();
    if (script.isEmpty())
      fail("entry has no \"script\": " + parser.value(entryOpt));
    if (!parser.isSet(langOpt) && entry.value("lang").isString())
      lang = entry.value("lang").toString();
    if (out.isEmpty())
      out = QFileInfo(f.fileName()).absoluteDir().filePath(
          entry.value("file").toString());
  } else if (parser.isSet(scriptOpt)) {
    script = parser.value(scriptOpt).split(';', Qt::SkipEmptyParts);
    if (out.isEmpty())
      fail("--out is required with --script");
  } else {
    std::cout << parser.helpText().toStdString();
    quit(1);
  }
  if (lang != "zh" && lang != "en" && lang != "zh_CN")
    fail("--lang must be zh or en");

  // Language: same mechanism as the application (setting + translators).
  RipesSettings::setValue(RIPES_SETTING_LANGUAGE,
                          lang == "en" ? "en" : "zh_CN");
  installTranslators(app);
  addOffscreenCJKFallbackFont();
  QApplication::setStyle("Fusion");
  QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
  QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);

  Shooter shooter(lang, entry, out);
  const QStringList rest = shooter.applyPreWindow(script);
  applyColorScheme();
  shooter.createWindow();
  shooter.run(rest);

  // Leave without running destructors / close handlers (the main window would
  // ask whether to save the program).
  quit(0);
}
