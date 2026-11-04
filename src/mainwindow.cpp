#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "cachetab.h"
#include "edittab.h"
#include "help.h"
#include "i18n.h"
#include "iotab.h"
#include "archexplorer/archexplorertab.h"
#include "loaddialog.h"
#include "memorytab.h"
#include "processorhandler.h"
#include "processortab.h"
#include "registerwidget.h"
#include "ripessettings.h"
#include "savedialog.h"
#include "settingsdialog.h"
#include "syscall/syscallviewer.h"
#include "syscall/systemio.h"
#include "version/version.h"
#include "wasmSupport.h"

#include "fancytabbar/fancytabbar.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

namespace Ripes {

namespace {
/// FancyTabBar (external/fancytabbar) has no per-tab tooltips and keeps its
/// hit-testing protected. Taking the member pointer through a derived class is
/// the standard-conforming way to call it from MainWindow's event filter.
struct FancyTabBarAccess : FancyTabBar {
  static qint32 tabIndexAt(const FancyTabBar *bar, const QPoint &pos) {
    auto fn = &FancyTabBarAccess::getTabIndexByPoint;
    return (bar->*fn)(pos.x(), pos.y());
  }
};
} // namespace

/// Platform bindings of @p key plus @p fallback (if not already among them);
/// some standard keys (SaveAs, Quit) have no binding on Windows.
static QList<QKeySequence> withFallback(QKeySequence::StandardKey key,
                                        const QKeySequence &fallback) {
  QList<QKeySequence> keys = QKeySequence::keyBindings(key);
  if (!keys.contains(fallback))
    keys << fallback;
  return keys;
}

static void clearSaveFile() {
  RipesSettings::setValue(RIPES_SETTING_HAS_SAVEFILE, false);
  RipesSettings::setValue(RIPES_SETTING_SAVEPATH, "");
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_ui(new Ui::MainWindow) {
  m_ui->setupUi(this);
  setWindowTitle(tr("Ripes - RISC-V processor simulator"));
  setWindowIcon(QIcon(":/icons/logo.svg"));
  m_ui->actionOpen_wiki->setIcon(QIcon(":/icons/info.svg"));

  // Initialize processor handler
  ProcessorHandler::get();

  // Initialize fonts
  QFontDatabase::addApplicationFont(
      ":/fonts/Inconsolata/Inconsolata-Regular.ttf");
  QFontDatabase::addApplicationFont(":/fonts/Inconsolata/Inconsolata-Bold.ttf");

  // Create tabs. The welcome / quick start panel sits above the tab pages.
  m_stackedTabs = new QStackedWidget(this);
  auto *contentWidget = new QWidget(this);
  auto *contentLayout = new QVBoxLayout(contentWidget);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);
  m_welcomePanel = createWelcomePanel();
  contentLayout->addWidget(m_welcomePanel);
  contentLayout->addWidget(m_stackedTabs, 1);
  m_ui->centrallayout->addWidget(contentWidget);

  auto *controlToolbar = addToolBar(tr("Simulator control"));
  controlToolbar->setVisible(true); // Always visible

  auto *editToolbar = addToolBar(tr("Edit"));
  editToolbar->setVisible(false);
  auto *editTab = new EditTab(editToolbar, this);
  m_stackedTabs->insertWidget(EditTabID, editTab);
  m_tabWidgets[EditTabID] = {editTab, editToolbar};

  auto *processorToolbar = addToolBar(tr("Processor"));
  processorToolbar->setVisible(false);
  auto *processorTab = new ProcessorTab(controlToolbar, processorToolbar, this);
  m_stackedTabs->insertWidget(ProcessorTabID, processorTab);
  m_tabWidgets[ProcessorTabID] = {processorTab, processorToolbar};

  auto *cacheToolbar = addToolBar(tr("Cache"));
  cacheToolbar->setVisible(false);
  auto *cacheTab = new CacheTab(cacheToolbar, this);
  m_stackedTabs->insertWidget(CacheTabID, cacheTab);
  m_tabWidgets[CacheTabID] = {cacheTab, cacheToolbar};

  auto *memoryToolbar = addToolBar(tr("Memory"));
  memoryToolbar->setVisible(false);
  auto *memoryTab = new MemoryTab(memoryToolbar, this);
  m_stackedTabs->insertWidget(MemoryTabID, memoryTab);
  m_tabWidgets[MemoryTabID] = {memoryTab, memoryToolbar};

  auto *IOToolbar = addToolBar(tr("I/O"));
  IOToolbar->setVisible(false);
  auto *IOTab = new class IOTab(IOToolbar, this);
  m_stackedTabs->insertWidget(IOTabID, IOTab);
  m_tabWidgets[IOTabID] = {IOTab, IOToolbar};

  // Architecture explorer tab
  auto *archToolbar = addToolBar(tr("Architecture"));
  archToolbar->setVisible(false);
  auto *archTab = new ArchExplorerTab(archToolbar, this);
  m_stackedTabs->insertWidget(ArchTabID, archTab);
  m_tabWidgets[ArchTabID] = {archTab, archToolbar};

  // Setup tab bar.
  m_ui->tabbar->addFancyTab(style()->standardIcon(QStyle::SP_FileIcon),
                            tr("Editor"));
  m_ui->tabbar->addFancyTab(QIcon(":/icons/cpu.svg"), tr("Processor"));
  m_ui->tabbar->addFancyTab(QIcon(":/icons/server.svg"), tr("Cache"));
  m_ui->tabbar->addFancyTab(QIcon(":/icons/ram-memory.svg"), tr("Memory"));
  m_ui->tabbar->addFancyTab(QIcon(":/icons/led.svg"), tr("I/O"));
  m_ui->tabbar->addFancyTab(QIcon(":/icons/architecture.svg"),
                            tr("Architecture"));
  // Per-tab tooltips (FancyTabBar has none of its own).
  m_ui->tabbar->installEventFilter(this);
  m_ui->tabbar->setMouseTracking(true);
  connect(m_ui->tabbar, &FancyTabBar::activeIndexChanged, this,
          &MainWindow::tabChanged);
  connect(m_ui->tabbar, &FancyTabBar::activeIndexChanged, m_stackedTabs,
          &QStackedWidget::setCurrentIndex);
  connect(m_ui->tabbar, &FancyTabBar::activeIndexChanged, editTab,
          &EditTab::updateProgramViewerHighlighting);
  setupMenus();

  // setup and connect widgets
  connect(editTab, &EditTab::editorStateChanged, [this] { clearSaveFile(); });

  // Setup status bar
  setupStatusBar();

  // Reset and program reload signals
  connect(ProcessorHandler::get(), &ProcessorHandler::processorReset,
          [this] { SystemIO::reset(); });

  connect(m_ui->actionSystem_calls, &QAction::triggered, this, [this] {
    SyscallViewer v(this);
    v.exec();
  });
  connect(m_ui->actionOpen_wiki, &QAction::triggered, this, &MainWindow::wiki);
  connect(m_ui->actionVersion, &QAction::triggered, this, &MainWindow::version);
  connect(m_ui->actionSettings, &QAction::triggered, this,
          &MainWindow::settingsTriggered);
  m_ui->actionSettings->setStatusTip(
      tr("Configure the simulator, compiler, editor, language and appearance."));
  m_ui->actionSettings->setShortcut(QKeySequence::Preferences);

  // Welcome panel visibility follows its setting (Help > Show welcome,
  // Settings > Environment, or the panel's own close button).
  connect(RipesSettings::getObserver(RIPES_SETTING_SHOW_WELCOME),
          &SettingObserver::modified, this,
          [this](const QVariant &v) { m_welcomePanel->setVisible(v.toBool()); });
  m_welcomePanel->setVisible(
      RipesSettings::value(RIPES_SETTING_SHOW_WELCOME).toBool());

  connect(cacheTab, &CacheTab::focusAddressChanged, memoryTab,
          &MemoryTab::setCentralAddress);

  connect(this, &MainWindow::prepareSave, editTab, &EditTab::onSave);

  // Keep the processor diagram's dark mode in sync with the application theme
  // (system theme changes or the user's Light/Dark/System choice). ThemeManager
  // emits after the palette has been updated, so appInDarkMode() is accurate.
  connect(ThemeManager::get(), &ThemeManager::themeChanged, this, [this] {
    static_cast<ProcessorTab *>(m_tabWidgets.at(ProcessorTabID).tab)
        ->setDarkmode(appInDarkMode());
  });
  // Applying the persisted color-scheme preference updates the application
  // palette, which ThemeManager observes to drive the handler above.
  connect(RipesSettings::getObserver(RIPES_SETTING_COLORSCHEME),
          &SettingObserver::modified, this, [] { applyColorScheme(); });

  m_currentTabID = ProcessorTabID;
  m_ui->tabbar->setActiveIndex(m_currentTabID);
}

#define _setupStatusWidget(name, _permanent)                                   \
  auto *name##StatusLabel = new QLabel(this);                                  \
  statusBar()->add##_permanent##Widget(name##StatusLabel);                     \
  connect(&name##StatusManager::get().emitter, &StatusEmitter::statusChanged,  \
          name##StatusLabel, &QLabel::setText);                                \
  connect(&name##StatusManager::get().emitter, &StatusEmitter::clear,          \
          name##StatusLabel, &QLabel::clear);

#define setupPermanentStatusWidget(name)                                       \
  _setupStatusWidget(name, Permanent);                                         \
  name##StatusManager::get().setPermanent();

#define setupStatusWidget(name) _setupStatusWidget(name, )

void MainWindow::setupStatusBar() {
  statusBar()->showMessage("");

  // Setup selected processor & ISA info status widget (right-aligned =>
  // permanent)
  setupPermanentStatusWidget(ProcessorInfo);
  auto updateProcessorInfo = [this] {
    const auto &desc =
        ProcessorRegistry::getDescription(ProcessorHandler::getID());
    QString status =
        tr("Processor: %1    ISA: %2    Language: %3")
            .arg(desc.name,
                 ProcessorHandler::getProcessor()->implementsISA()->name(),
                 uiIsChinese() ? QStringLiteral("简体中文")
                               : QStringLiteral("English"));
    ProcessorInfoStatusManager::get().setStatusPermanent(status);
  };
  ProcessorInfoStatusLabel->setToolTip(
      tr("Selected processor model and instruction set (change them with the "
         "processor button in the toolbar), and the user-interface language "
         "(Edit > Settings > Environment)."));
  connect(ProcessorHandler::get(), &ProcessorHandler::processorChanged,
          updateProcessorInfo);
  updateProcessorInfo();

  // Setup processorhandler status widget
  setupStatusWidget(Processor);

  // Setup syscall status widget
  setupStatusWidget(Syscall);

  // Setup systemIO status widget
  setupStatusWidget(SystemIO);

  // Setup general info status widget
  setupStatusWidget(General);
}

void MainWindow::tabChanged(int index) {
  m_tabWidgets.at(m_currentTabID).toolbar->setVisible(false);
  m_tabWidgets.at(m_currentTabID).tab->tabVisibilityChanged(false);
  m_currentTabID = static_cast<TabIndex>(index);
  m_tabWidgets.at(m_currentTabID).toolbar->setVisible(true);
  m_tabWidgets.at(m_currentTabID).tab->tabVisibilityChanged(true);
}

void MainWindow::fitToView() {
  static_cast<ProcessorTab *>(m_tabWidgets.at(ProcessorTabID).tab)
      ->fitToScreen();
}

void MainWindow::restoreWindowGeometry() {
  const QByteArray geometry =
      RipesSettings::value<QByteArray>(RIPES_SETTING_MAINWINDOW_GEOMETRY);
  if (geometry.isEmpty() || !restoreGeometry(geometry)) {
    // First launch (or invalid stored geometry): start maximized, which has
    // historically provided the best default layout.
    setWindowState(windowState() | Qt::WindowMaximized);
  }
}

void MainWindow::showEvent(QShowEvent *event) {
  QMainWindow::showEvent(event);
  if (!m_hasPerformedInitialFit) {
    m_hasPerformedInitialFit = true;
    // Defer the initial processor fit until the event loop has applied the
    // window's final geometry, so the graphics view has its correct size. This
    // replaces a previously used fixed-delay timer.
    QTimer::singleShot(0, this, &MainWindow::fitToView);
  }
}

void MainWindow::setupMenus() {
  // Edit actions
  const QIcon newIcon = QIcon(":/icons/file.svg");
  auto *newAction = new QAction(newIcon, tr("New Program"), this);
  newAction->setShortcut(QKeySequence::New);
  newAction->setStatusTip(tr("Clear the editor and start a new program."));
  connect(newAction, &QAction::triggered, this,
          &MainWindow::newProgramTriggered);
  m_ui->menuFile->addAction(newAction);

  const QIcon loadIcon = QIcon(":/icons/loadfile.svg");
  auto *loadAction = new QAction(loadIcon, tr("Load Program..."), this);
  loadAction->setShortcut(QKeySequence::Open);
  loadAction->setStatusTip(tr("Load a source file (.s/.c), an ELF executable "
                              "or a flat binary."));
  connect(loadAction, &QAction::triggered, this,
          [this] { this->loadFileTriggered(); });
  m_ui->menuFile->addAction(loadAction);
  m_ui->menuFile->addSeparator();

  auto *examplesMenu = m_ui->menuFile->addMenu(tr("Load Example"));
  examplesMenu->setStatusTip(
      tr("Load one of the example programs bundled with Ripes."));
  examplesMenu->setToolTipsVisible(true);
  setupExamplesMenu(examplesMenu);

  m_ui->menuFile->addSeparator();

  const QIcon saveIcon = QIcon(":/icons/save.svg");
  auto *saveAction = new QAction(saveIcon, tr("Save File"), this);
  saveAction->setShortcut(QKeySequence::Save);
  saveAction->setStatusTip(
      tr("Save the program (source and/or binary) to the current file."));
  connect(saveAction, &QAction::triggered, this,
          &MainWindow::saveFilesTriggered);
  connect(static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab),
          &EditTab::editorStateChanged, saveAction,
          [saveAction](bool enabled) { saveAction->setEnabled(enabled); });
  m_ui->menuFile->addAction(saveAction);

  const QIcon saveAsIcon = QIcon(":/icons/saveas.svg");
  auto *saveAsAction = new QAction(saveAsIcon, tr("Save File As..."), this);
  // QKeySequence::SaveAs/Quit are unbound on Windows; add explicit fallbacks.
  saveAsAction->setShortcuts(
      withFallback(QKeySequence::SaveAs, QKeySequence("Ctrl+Shift+S")));
  saveAsAction->setStatusTip(tr("Save the program to a new file."));
  connect(saveAsAction, &QAction::triggered, this,
          &MainWindow::saveFilesAsTriggered);
  connect(static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab),
          &EditTab::editorStateChanged, saveAction,
          [saveAsAction](bool enabled) { saveAsAction->setEnabled(enabled); });
  m_ui->menuFile->addAction(saveAsAction);
  m_ui->menuFile->addSeparator();

  const QIcon exitIcon = QIcon(":/icons/cancel.svg");
  auto *exitAction = new QAction(exitIcon, tr("Exit"), this);
  exitAction->setShortcuts(
      withFallback(QKeySequence::Quit, QKeySequence("Ctrl+Q")));
  exitAction->setStatusTip(tr("Quit Ripes."));
  connect(exitAction, &QAction::triggered, this, &MainWindow::close);
  m_ui->menuFile->addAction(exitAction);

  m_ui->menuView->addAction(
      static_cast<ProcessorTab *>(m_tabWidgets.at(ProcessorTabID).tab)
          ->m_displayValuesAction);

  setupHelpMenu();

  // File I/O is not yet supported on WASM due to sandboxing.
  disableIfWasm(QList{loadAction, saveAction, saveAsAction, exitAction});
}

void MainWindow::setupHelpMenu() {
  // Rebuild the Help menu in a meaningful order: guide first, then reference
  // material, then "About".
  m_ui->menuHelp->clear();
  m_ui->menuHelp->setToolTipsVisible(true);

  auto *guideAction =
      new QAction(QIcon(":/icons/info.svg"), tr("User Guide"), this);
  guideAction->setShortcut(QKeySequence::HelpContents);
  guideAction->setStatusTip(
      tr("Open the built-in user guide (tutorials and reference)."));
  connect(guideAction, &QAction::triggered, this, &MainWindow::userGuide);
  m_ui->menuHelp->addAction(guideAction);

  auto *quickstartAction = new QAction(tr("Quick Start (10 minutes)"), this);
  quickstartAction->setStatusTip(
      tr("Open the quick start chapter of the user guide."));
  connect(quickstartAction, &QAction::triggered, this,
          [] { showHelp("02-quickstart"); });
  m_ui->menuHelp->addAction(quickstartAction);

  auto *welcomeAction = new QAction(tr("Show Welcome Panel"), this);
  welcomeAction->setStatusTip(
      tr("Show the welcome panel that explains the six tabs."));
  connect(welcomeAction, &QAction::triggered, this,
          [this] { setWelcomeVisible(true); });
  m_ui->menuHelp->addAction(welcomeAction);

  m_ui->menuHelp->addSeparator();
  m_ui->actionSystem_calls->setStatusTip(
      tr("List the system calls (ecall) supported by the simulator, with "
         "their arguments."));
  m_ui->menuHelp->addAction(m_ui->actionSystem_calls);
  m_ui->actionOpen_wiki->setStatusTip(
      tr("Open the upstream Ripes documentation on GitHub in your web "
         "browser."));
  m_ui->menuHelp->addAction(m_ui->actionOpen_wiki);

  m_ui->menuHelp->addSeparator();
  m_ui->actionVersion->setStatusTip(
      tr("Version, license and project links."));
  m_ui->menuHelp->addAction(m_ui->actionVersion);
}

QString MainWindow::tabToolTip(int index) const {
  switch (static_cast<TabIndex>(index)) {
  case EditTabID:
    return tr("<b>Editor</b><br/>Write or load a RISC-V assembly or C program. "
              "Assembly is assembled automatically while you type; the right "
              "side shows the resulting machine code.");
  case ProcessorTabID:
    return tr("<b>Processor</b><br/>Watch the program execute on the "
              "selected processor model, clock cycle by clock cycle, with "
              "registers, pipeline stages and execution statistics.");
  case CacheTabID:
    return tr("<b>Cache</b><br/>Configure instruction/data caches and see "
              "hits, misses and the hit rate while the program runs.");
  case MemoryTabID:
    return tr("<b>Memory</b><br/>Inspect the memory contents (program, data, "
              "stack) and the memory map of sections and peripherals.");
  case IOTabID:
    return tr("<b>I/O</b><br/>Add memory-mapped peripherals such as LEDs, "
              "switches, a display, the SIMT GPU or the NPU, and see their "
              "register maps.");
  case ArchTabID:
    return tr("<b>Architecture</b><br/>Explore block diagrams and step through "
              "data flows of whole systems (SoCs, game consoles such as "
              "NeoGeo and PS3/Cell).");
  case NTabsID:
    break;
  }
  return QString();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_ui->tabbar && event->type() == QEvent::ToolTip) {
    auto *helpEvent = static_cast<QHelpEvent *>(event);
    const int index =
        FancyTabBarAccess::tabIndexAt(m_ui->tabbar, helpEvent->pos());
    const QString tip = index >= 0 ? tabToolTip(index) : QString();
    if (tip.isEmpty())
      QToolTip::hideText();
    else
      QToolTip::showText(helpEvent->globalPos(), tip, m_ui->tabbar);
    return true;
  }
  if (watched == m_ui->tabbar && event->type() == QEvent::MouseMove) {
    // Mirror the tab description in the status bar while hovering.
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    const int index = FancyTabBarAccess::tabIndexAt(
        m_ui->tabbar, mouseEvent->position().toPoint());
    QString tip = index >= 0 ? tabToolTip(index) : QString();
    tip.replace("<br/>", " ");
    tip.remove(QRegularExpression("<[^>]*>"));
    statusBar()->showMessage(tip);
  } else if (watched == m_ui->tabbar && event->type() == QEvent::Leave) {
    statusBar()->clearMessage();
  }
  return QMainWindow::eventFilter(watched, event);
}

namespace {
/// Scroll area for the welcome text. Its preferred height is the text's
/// height at the current width (so it is fully shown when there is room),
/// but its minimum is only a few lines, so the panel never forces the main
/// window to be taller than the screen; the text scrolls instead.
class WelcomeScrollArea : public QScrollArea {
public:
  using QScrollArea::QScrollArea;
  QSize sizeHint() const override {
    const QSize base = QScrollArea::sizeHint();
    if (!widget())
      return base;
    const int w = viewport()->width() > 50 ? viewport()->width() : 900;
    int h = widget()->heightForWidth(w);
    if (h < 0)
      h = widget()->sizeHint().height();
    return {base.width(), h + 2 * frameWidth()};
  }
  QSize minimumSizeHint() const override {
    return {QScrollArea::minimumSizeHint().width(),
            3 * fontMetrics().lineSpacing() + 2 * frameWidth()};
  }

protected:
  void resizeEvent(QResizeEvent *e) override {
    QScrollArea::resizeEvent(e);
    // The text re-wraps at a new width: ask the layout for a new height.
    if (e->oldSize().width() != e->size().width())
      updateGeometry();
  }
};
} // namespace

QFrame *MainWindow::createWelcomePanel() {
  auto *panel = new QFrame(this);
  panel->setObjectName("welcomePanel");
  panel->setFrameShape(QFrame::StyledPanel);
  panel->setAutoFillBackground(true);
  panel->setBackgroundRole(QPalette::AlternateBase);

  auto *layout = new QHBoxLayout(panel);
  layout->setContentsMargins(12, 6, 8, 6);

  auto *text = new QLabel(panel);
  text->setWordWrap(true);
  text->setTextFormat(Qt::RichText);
  text->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                Qt::LinksAccessibleByKeyboard);
  // One entry per tab. Each translation is a table row "<tr><td>link</td>
  // <td>text</td></tr>"; two entries share a row so the panel stays short.
  const QStringList entries = {
      tr("<tr><td><a href='tab:0'>Editor</a></td><td>write or load a program "
         "(assembly or C); File &gt; Load Example has ready-made ones.</td>"
         "</tr>"),
      tr("<tr><td><a href='tab:1'>Processor</a></td><td>run it on a processor "
         "model: Clock (F5) steps one cycle, Run (F8) runs to the "
         "end.</td></tr>"),
      tr("<tr><td><a href='tab:2'>Cache</a></td><td>configure caches and watch "
         "hits and misses.</td></tr>"),
      tr("<tr><td><a href='tab:3'>Memory</a></td><td>inspect memory contents "
         "and the memory map.</td></tr>"),
      tr("<tr><td><a href='tab:4'>I/O</a></td><td>add memory-mapped "
         "peripherals (LEDs, switches, display, GPU, NPU).</td></tr>"),
      tr("<tr><td><a href='tab:5'>Architecture</a></td><td>explore block "
         "diagrams and data flows of whole systems.</td></tr>")};
  QString table = QStringLiteral("<table cellspacing='1' cellpadding='1'>");
  for (int i = 0; i < entries.size(); i += 2) {
    table += QStringLiteral("<tr>");
    for (int j = i; j < std::min<int>(i + 2, entries.size()); ++j) {
      QString cells = entries.at(j);
      cells.remove(QStringLiteral("<tr>")).remove(QStringLiteral("</tr>"));
      // Padding between the two columns.
      if (j > i)
        cells.replace(QStringLiteral("<td>"),
                      QStringLiteral("<td style='padding-left:16px'>"),
                      Qt::CaseSensitive);
      table += cells;
    }
    table += QStringLiteral("</tr>");
  }
  table += QStringLiteral("</table>");
  // Links: "tab:N" switches to tab N, anything else is a guide chapter id.
  text->setText(
      tr("<b>Welcome to Ripes!</b> The tabs on the left are:") + table +
      tr("New here? Read the <a href='02-quickstart'>10-minute quick "
         "start</a> or the <a href='03-gui-tour'>tour of the window</a>. "
         "Press F1 at any time for the user guide."));
  connect(text, &QLabel::linkActivated, this, [this](const QString &link) {
    if (link.startsWith("tab:"))
      m_ui->tabbar->setActiveIndex(link.mid(4).toInt());
    else
      showHelp(link);
  });

  auto *scroll = new WelcomeScrollArea(panel);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(text);
  // Show the panel's background through the scroll area.
  scroll->viewport()->setAutoFillBackground(false);
  text->setAutoFillBackground(false);
  scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  layout->addWidget(scroll, 1);

  auto *closeButton = new QToolButton(panel);
  closeButton->setAutoRaise(true);
  closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  closeButton->setToolTip(
      tr("Hide the welcome panel. Show it again with Help > Show Welcome "
         "Panel."));
  connect(closeButton, &QToolButton::clicked, this,
          [this] { setWelcomeVisible(false); });
  layout->addWidget(closeButton, 0, Qt::AlignTop);

  return panel;
}

void MainWindow::setWelcomeVisible(bool visible) {
  // Persisted, so that a dismissed panel stays hidden on the next start. The
  // setting observer (see constructor) updates the panel itself.
  RipesSettings::setValue(RIPES_SETTING_SHOW_WELCOME, visible);
}

void MainWindow::userGuide() { showHelp(QString()); }

MainWindow::~MainWindow() { delete m_ui; }

void MainWindow::setupExamplesMenu(QMenu *parent) {
  const auto assemblyExamples =
      QDir(":/examples/assembly/").entryList(QDir::Files);
  auto *assemblyMenu = parent->addMenu(tr("Assembly"));
  if (!assemblyExamples.isEmpty()) {
    for (const auto &fileName : assemblyExamples) {
      assemblyMenu->addAction(fileName, this, [this, fileName] {
        LoadFileParams parms;
        parms.filepath = QString(":/examples/assembly/") + fileName;
        parms.type = SourceType::Assembly;
        static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)
            ->loadExternalFile(parms);
        clearSaveFile();
      });
    }
  }

  const auto cExamples = QDir(":/examples/C/").entryList(QDir::Files);
  auto *cMenu = parent->addMenu(tr("C"));
  if (!cExamples.isEmpty()) {
    for (const auto &fileName : cExamples) {
      cMenu->addAction(fileName, this, [this, fileName] {
        LoadFileParams parms;
        parms.filepath = QString(":/examples/C/") + fileName;
        parms.type = SourceType::C;
        static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)
            ->loadExternalFile(parms);
        clearSaveFile();
      });
    }
  }

  const auto ELFExamples = QDir(":/examples/ELF/").entryList(QDir::Files);
  auto *elfMenu = parent->addMenu(tr("ELF (precompiled C)"));
  if (!ELFExamples.isEmpty()) {
    for (const auto &fileName : ELFExamples) {
      elfMenu->addAction(fileName, this, [this, fileName] {
        // ELFIO Cannot read directly from the bundled resource file, so copy
        // the ELF file to a temporary file before loading the program.
        QTemporaryFile *tmpELFFile =
            QTemporaryFile::createNativeFile(":/examples/ELF/" + fileName);
        if (!tmpELFFile->open()) {
          QMessageBox::warning(this, tr("Error"),
                               tr("Could not create temporary ELF file"));
          return;
        }

        LoadFileParams parms;
        parms.filepath = tmpELFFile->fileName();
        parms.type = SourceType::ExternalELF;
        static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)
            ->loadExternalFile(parms);
        clearSaveFile();
        tmpELFFile->remove();
      });
    }
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  // Stop the processor if running
  auto *proc = ProcessorHandler::get();
  if (proc->isRunning())
    proc->stopRun();

  if (static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)
          ->isEditorEnabled() &&
      !static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)
           ->getAssemblyText()
           .isEmpty()) {
    QMessageBox saveMsgBox(this);
    saveMsgBox.setWindowTitle("Ripes");
    saveMsgBox.setText(tr("Save current program before exiting?"));
    saveMsgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No |
                                  QMessageBox::Cancel);

    const auto result = saveMsgBox.exec();
    if (result == QMessageBox::Cancel) {
      // Dont exit
      event->ignore();
      return;
    } else if (result == QMessageBox::Yes) {
      saveFilesTriggered();
    }
  }

  // Persist the window geometry (size, position and maximized state) so it can
  // be restored on the next launch.
  RipesSettings::setValue(RIPES_SETTING_MAINWINDOW_GEOMETRY, saveGeometry());

  // Emit an observable signal to indicate that the application is about to
  // close
  RipesSettings::setValue(RIPES_GLOBALSIGNAL_QUIT, 0);
  QMainWindow::closeEvent(event);
}

void MainWindow::loadFileTriggered() {
  static_cast<ProcessorTab *>(m_tabWidgets.at(ProcessorTabID).tab)->pause();
  LoadDialog diag;
  if (!diag.exec())
    return;

  if (static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)
          ->loadExternalFile(diag.getParams())) {
    // Set the current save path to the loaded file. Only store the file
    // basename without extension.
    QFileInfo info = QFileInfo(diag.getParams().filepath);
    auto savePath = info.dir().path() + QDir::separator() + info.baseName();
    RipesSettings::setValue(RIPES_SETTING_SAVEPATH, savePath);
    RipesSettings::setValue(RIPES_SETTING_HAS_SAVEFILE, true);
  }
}

void MainWindow::wiki() {
  QDesktopServices::openUrl(QUrl(QString(
      "https://github.com/mortbopet/Ripes/blob/master/docs/README.md")));
}

void MainWindow::version() {
  const QString text =
      tr("<h3>Ripes %1</h3>"
         "<p>A graphical processor simulator and assembly editor for the "
         "RISC-V ISA, extended in this edition with teaching modules: a SIMT "
         "GPU, a tensor unit (NPU) and an architecture explorer.</p>"
         "<p>User guide: <b>Help &gt; User Guide</b> (F1).<br/>"
         "Upstream project: <a "
         "href='https://github.com/mortbopet/Ripes'>github.com/mortbopet/"
         "Ripes</a></p>"
         "<p>Built with Qt %2. Licensed under the MIT license.</p>")
          .arg(getRipesVersion(), QString::fromLatin1(qVersion()));
  QMessageBox::about(this, tr("About Ripes"), text);
}

static bool writeTextFile(QFile &file, const QString &data) {
  if (file.open(QIODevice::WriteOnly)) {
    QTextStream stream(&file);
    stream << data;
    file.close();
    return true;
  }
  return false;
}

static bool writeBinaryFile(QFile &file, const QByteArray &data) {
  if (file.open(QIODevice::WriteOnly)) {
    file.write(data);
    file.close();
    return true;
  }
  return false;
}

// Ensures that any parent directories of 'path' exists.
static bool ensurePath(const QString &path) {
  return QDir().mkpath(QFileInfo(path).absoluteDir().absolutePath());
}

void MainWindow::saveFilesTriggered() {
  SaveDialog diag(
      static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)->getSourceType());
  if (!RipesSettings::value(RIPES_SETTING_HAS_SAVEFILE).toBool()) {
    saveFilesAsTriggered();
    return;
  }

  emit prepareSave();
  QStringList savedFiles;
  if (!diag.sourcePath().isEmpty()) {
    if (!ensurePath(diag.sourcePath()))
      return;
    QFile file(diag.sourcePath());
    savedFiles << diag.sourcePath();
    if (!writeTextFile(file,
                       static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)
                           ->getAssemblyText())) {
      QMessageBox::information(
          this, tr("File error"),
          tr("Error when saving file: %1").arg(file.errorString()));
      return;
    }
  }

  if (!diag.binaryPath().isEmpty()) {
    if (!ensurePath(diag.binaryPath()))
      return;
    QFile file(diag.binaryPath());
    auto program = ProcessorHandler::getProgram();
    if (!program || (program.get()->sections.count(".text") == 0))
      return;

    savedFiles << diag.binaryPath();
    if (!writeBinaryFile(file, program.get()->sections.at(".text").data)) {
      QMessageBox::information(
          this, tr("File error"),
          tr("Error when saving file: %1").arg(file.errorString()));
      return;
    }
  }

  GeneralStatusManager::setStatusTimed(
      tr("Saved files %1").arg(savedFiles.join(", ")), 1000);
}

void MainWindow::saveFilesAsTriggered() {
  SaveDialog diag(
      static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)->getSourceType());
  auto ret = diag.exec();
  if (ret == QDialog::Rejected) {
    return;
  }
  RipesSettings::setValue(RIPES_SETTING_HAS_SAVEFILE, true);
  saveFilesTriggered();
}

void MainWindow::settingsTriggered() {
  SettingsDialog diag(this);
  diag.exec();
}

void MainWindow::newProgramTriggered() {
  QMessageBox mbox;
  mbox.setWindowTitle(tr("New Program"));
  mbox.setStandardButtons(QMessageBox::Yes | QMessageBox::No |
                          QMessageBox::Cancel);
  if (!static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)
           ->getAssemblyText()
           .isEmpty() ||
      RipesSettings::value(RIPES_SETTING_HAS_SAVEFILE).toBool()) {
    // User wrote a program but did not save it to a file yet
    mbox.setText(tr("Save program before creating new file?"));
    auto ret = mbox.exec();
    switch (ret) {
    case QMessageBox::Yes: {
      saveFilesTriggered();
      if (!RipesSettings::value(RIPES_SETTING_HAS_SAVEFILE).toBool()) {
        // User must have rejected the save file dialog
        return;
      }
      break;
    }
    case QMessageBox::No: {
      break;
    }
    case QMessageBox::Cancel: {
      return;
    }
    }
  }
  clearSaveFile();
  static_cast<EditTab *>(m_tabWidgets.at(EditTabID).tab)->newProgram();
}

} // namespace Ripes
