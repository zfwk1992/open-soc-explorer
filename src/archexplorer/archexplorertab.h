#pragma once

#include "archexplorer/archmodel.h"
#include "ripestab.h"

#include <QString>
#include <QVector>

QT_FORWARD_DECLARE_CLASS(QAction)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QListWidget)
QT_FORWARD_DECLARE_CLASS(QSplitter)
QT_FORWARD_DECLARE_CLASS(QTableWidget)
QT_FORWARD_DECLARE_CLASS(QTextBrowser)
QT_FORWARD_DECLARE_CLASS(QTimer)
QT_FORWARD_DECLARE_CLASS(QToolButton)

namespace Ripes {

namespace Ui {
class ArchExplorerTab;
}

class ArchScene;
class ArchView;

/**
 * @brief The ArchExplorerTab class
 * JSON-driven block diagram / data flow viewer.
 *
 * Layout (all panes in QSplitters whose sizes are remembered):
 *  - header: topology title, one-line description and a "?" help button;
 *  - left: topology chooser + description, data-flow panel (flow chooser,
 *    clickable step list, previous / play-pause / next / reset, speed);
 *  - center: zoomable/pannable diagram and, below it, the text of the
 *    current step;
 *  - right: details of the selected block (fields, properties, attached
 *    buses) and a legend of the block and bus kinds;
 *  - bottom: status line ("Step i of n", zoom).
 * The main-window toolbar holds open / export and the zoom actions.
 * Keyboard: Left / Right step, Space plays / pauses, Home resets the flow.
 */
class ArchExplorerTab : public RipesTab {
  Q_OBJECT

public:
  explicit ArchExplorerTab(QToolBar *toolbar, QWidget *parent = nullptr);
  ~ArchExplorerTab() override;

  void tabVisibilityChanged(bool visible) override;

  /**
   * @brief loadArchitectureFile
   * Loads an architecture JSON file from disk, appends it to the topology
   * list and selects it. On error, shows a message box (unless @p quiet) and
   * returns false.
   */
  bool loadArchitectureFile(const QString &path, bool quiet = false);

  /// Re-reads the selected topology from its file (no-op for builtins).
  bool reloadCurrent();

  /// Number of topologies in the chooser.
  int topologyCount() const { return m_models.size(); }
  /// Selects the topology at @p index of the chooser.
  void setCurrentTopology(int index);
  /// Selects the flow at @p index of the current topology (-1: none).
  void selectFlow(int index);

  const ArchModel *currentModel() const;
  const ArchFlow *currentFlow() const;
  int currentFlowIndex() const { return m_flowIndex; }
  /// -1 while no step is active (overview), otherwise the 0-based step.
  int currentStepIndex() const { return m_stepIndex; }
  bool isPlaying() const;
  /// Status line text, e.g. "Step 3 of 10 · ...".
  QString statusText() const;
  /// Id of the block whose details are shown (empty: none).
  QString detailsBlockId() const { return m_detailsBlock; }

  /// Saves the diagram as PNG (2x resolution). Returns false on failure.
  bool exportDiagram(const QString &path);

public slots:
  void nextStep();
  void previousStep();
  void resetFlow();
  /// Jumps to step @p step (0-based; -1 = overview) of the current flow.
  void setStep(int step);
  void setPlaying(bool playing);
  void togglePlaying();

private:
  void buildUi();
  void setupToolbar();
  void setupShortcuts();
  void populateArchitectures();
  void setCurrentArchitecture(int modelIndex);
  void populateFlows();
  void setCurrentFlow(int flowIndex);
  void populateSteps();
  void applyStep();
  void updateActions();
  void updateHeader();
  void updateStepList();
  void updateStatus();
  void updateLegend();
  void showBlockDetails(const QString &id);
  void clearBlockDetails();
  void openArchitectureFile();
  void rememberOpenedFiles() const;
  QString comboText(int modelIndex) const;
  void exportDiagramInteractive();
  void updatePlayInterval();
  void saveSplitterState() const;
  void restoreSplitterState();
  void onThemeChanged();

  Ui::ArchExplorerTab *m_ui = nullptr;
  ArchScene *m_scene = nullptr;
  ArchView *m_view = nullptr;
  QVector<ArchModel> m_models;
  QVector<QString> m_modelPaths; ///< file path per model; empty = builtin
  int m_modelIndex = -1;
  int m_flowIndex = -1;
  int m_stepIndex = -1;
  bool m_initialFitDone = false;
  QString m_detailsBlock;
  qreal m_zoom = 1.0;

  // Header
  QLabel *m_titleLabel = nullptr;
  QLabel *m_subtitleLabel = nullptr;
  QToolButton *m_helpButton = nullptr;

  // Left: topology + flow panel
  QComboBox *m_archCombo = nullptr;
  QTextBrowser *m_archDescription = nullptr;
  QComboBox *m_flowCombo = nullptr;
  QLabel *m_flowDescription = nullptr;
  QListWidget *m_stepList = nullptr;
  QToolButton *m_resetButton = nullptr;
  QToolButton *m_prevButton = nullptr;
  QToolButton *m_playButton = nullptr;
  QToolButton *m_nextButton = nullptr;
  QComboBox *m_speedCombo = nullptr;

  // Center: current step
  QLabel *m_stepHeader = nullptr;
  QTextBrowser *m_stepText = nullptr;

  // Right: details + legend
  QLabel *m_blockTitle = nullptr;
  QLabel *m_blockKind = nullptr;
  QLabel *m_blockSubtitle = nullptr;
  QLabel *m_propsLabel = nullptr;
  QTableWidget *m_propsTable = nullptr;
  QLabel *m_connLabel = nullptr;
  QTableWidget *m_connTable = nullptr;
  QLabel *m_detailsLabel = nullptr;
  QTextBrowser *m_detailsBrowser = nullptr;
  QListWidget *m_legendList = nullptr;

  // Bottom
  QLabel *m_statusLabel = nullptr;
  QLabel *m_zoomLabel = nullptr;

  QSplitter *m_mainSplitter = nullptr;
  QSplitter *m_leftSplitter = nullptr;
  QSplitter *m_centerSplitter = nullptr;
  QSplitter *m_rightSplitter = nullptr;

  QTimer *m_playTimer = nullptr;
  QAction *m_openAction = nullptr;
  QAction *m_exportAction = nullptr;
  QAction *m_reloadAction = nullptr;
  QAction *m_prevAction = nullptr;
  QAction *m_nextAction = nullptr;
  QAction *m_playAction = nullptr;
  QAction *m_resetAction = nullptr;
  QAction *m_fitAction = nullptr;
  QAction *m_resetZoomAction = nullptr;
  QAction *m_zoomInAction = nullptr;
  QAction *m_zoomOutAction = nullptr;
};

} // namespace Ripes
