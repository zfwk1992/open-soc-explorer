#pragma once

#include <QMainWindow>

#include "assembler/program.h"
#include "statusmanager.h"

QT_FORWARD_DECLARE_CLASS(QToolBar)
QT_FORWARD_DECLARE_CLASS(QStackedWidget)
QT_FORWARD_DECLARE_CLASS(QActionGroup)

namespace Ripes {
namespace Ui {
class MainWindow;
}

class EditTab;
class MemoryTab;
class ProcessorTab;
class CacheTab;
class IOTab;
class ProcessorHandler;
class RipesTab;
namespace Explorer {
class TraceRecorder;
}
struct LoadFileParams;

struct TabWidgets {
  RipesTab *tab;
  QToolBar *toolbar;
};

class MainWindow : public QMainWindow {
  Q_OBJECT

  enum TabIndex {
    EditTabID,
    ProcessorTabID,
    CacheTabID,
    MemoryTabID,
    IOTabID,
    ExplorerTabID,
    MissionsTabID,
    NTabsID
  };

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  void closeEvent(QCloseEvent *event) override;
  void fitToView();

  /**
   * @brief restoreWindowGeometry
   * Restores the window geometry (size, position and maximized state) persisted
   * from a previous session. On first launch, or if no valid geometry has been
   * stored, the window defaults to a maximized state.
   */
  void restoreWindowGeometry();

protected:
  void showEvent(QShowEvent *event) override;
  void changeEvent(QEvent *event) override;

signals:
  void prepareSave();

private slots:
  void wiki();
  void version();

  void loadFileTriggered();

  void saveFilesTriggered();
  void saveFilesAsTriggered();
  void newProgramTriggered();
  void settingsTriggered();
  void tabChanged(int index);

private:
  void loadFile(const QString &filename, SourceType type);

  void setupStatusBar();
  void setupMenus();
  void setupExamplesMenu(QMenu *parent);
  /// Open SoC Explorer: applies the current language to the window's own
  /// texts and to upstream texts set from string literals.
  void retranslate();

  Ui::MainWindow *m_ui = nullptr;
  QActionGroup *m_binaryStoreAction;
  QToolBar *m_toolbar = nullptr;

  // Tabs
  QStackedWidget *m_stackedTabs = nullptr;
  std::map<TabIndex, TabWidgets> m_tabWidgets;
  TabIndex m_currentTabID = ProcessorTabID;
  bool m_hasPerformedInitialFit = false;

  // Open SoC Explorer: records the execution trace shown in the Explorer and
  // used to verify missions.
  Explorer::TraceRecorder *m_traceRecorder = nullptr;
};
} // namespace Ripes
