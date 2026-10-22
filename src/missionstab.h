// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <functional>
#include <map>
#include <set>
#include <vector>

#include "explorer/mission.h"
#include "explorer/missionprogress.h"
#include "processorregistry.h"
#include "ripestab.h"

class QFrame;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class QGroupBox;

namespace Ripes {

namespace Explorer {
class TraceRecorder;
}
class MissionHud;
class MissionBanner;

/**
 * @brief The MissionsTab class
 * Guided learning missions. Each mission explains a concept, loads an example
 * program, guides the learner through steps that are verified live against
 * the simulator and its recorded trace, and ends with a challenge that is
 * scored using the measured results of the selected Ripes model.
 *
 * The mission map, stars, experience points and levels are computed by
 * Explorer::MissionProgress from the persisted record of verified steps and
 * solved challenges; nothing else is scored.
 */
class MissionsTab : public RipesTab {
  Q_OBJECT
public:
  /// @p sourceProvider returns the program currently in the editor.
  MissionsTab(QToolBar *toolbar, Explorer::TraceRecorder *recorder,
              std::function<QString()> sourceProvider,
              QWidget *parent = nullptr);

  void tabVisibilityChanged(bool visible) override;

protected:
  void changeEvent(QEvent *event) override;

signals:
  void loadProgramRequested(const QString &program);
  void selectProcessorRequested(Ripes::ProcessorID id);
  void showExplorerRequested();

private slots:
  void showMission(int index);
  void evaluateSteps();
  void checkChallenge();
  void resetProgress();

private:
  struct StepWidgets {
    QFrame *box = nullptr;
    QLabel *status = nullptr;
    QLabel *checks = nullptr;
  };
  Explorer::MissionState currentState() const;
  void loadProgress();
  void saveProgress();
  void refreshMissionList();
  void refreshProgressViews();
  QString criteriaHtml(const Explorer::CheckReport &report) const;
  void applyTheme();
  /// Sets the static texts in the current language.
  void retranslateUi();

  Explorer::TraceRecorder *m_recorder = nullptr;
  std::function<QString()> m_sourceProvider;
  std::vector<Explorer::Mission> m_missions;
  int m_current = -1;
  bool m_visible = false;
  /// Verified steps and solved challenges (persisted in the settings). A
  /// verified step stays verified when the learner moves on (e.g. edits the
  /// program).
  Explorer::MissionProgress m_progress;
  bool m_saving = false;

  MissionHud *m_hud = nullptr;
  QListWidget *m_list = nullptr;
  MissionBanner *m_banner = nullptr;
  QLabel *m_lockNote = nullptr;
  QLabel *m_objective = nullptr;
  QLabel *m_explanation = nullptr;
  QLabel *m_processorLabel = nullptr;
  QVBoxLayout *m_stepsLayout = nullptr;
  QGroupBox *m_stepsGroup = nullptr;
  std::vector<StepWidgets> m_stepWidgets;
  QLabel *m_challenge = nullptr;
  QLabel *m_hint = nullptr;
  QPushButton *m_hintButton = nullptr;
  QLabel *m_result = nullptr;
  QLabel *m_completion = nullptr;
  QLabel *m_mapTitle = nullptr;
  QGroupBox *m_conceptGroup = nullptr;
  QGroupBox *m_challengeGroup = nullptr;
  QPushButton *m_loadButton = nullptr;
  QPushButton *m_processorButton = nullptr;
  QPushButton *m_explorerButton = nullptr;
  QPushButton *m_checkButton = nullptr;
  std::vector<std::pair<QPushButton *, int>> m_iconButtons;
};

} // namespace Ripes
