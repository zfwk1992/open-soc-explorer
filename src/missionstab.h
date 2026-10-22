// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <functional>
#include <map>
#include <set>
#include <vector>

#include "explorer/mission.h"
#include "processorregistry.h"
#include "ripestab.h"

class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class QGroupBox;

namespace Ripes {

namespace Explorer {
class TraceRecorder;
}

/**
 * @brief The MissionsTab class
 * Guided learning missions. Each mission explains a concept, loads an example
 * program, guides the learner through steps that are verified live against
 * the simulator and its recorded trace, and ends with a challenge that is
 * scored using the measured results of the selected Ripes model.
 */
class MissionsTab : public RipesTab {
  Q_OBJECT
public:
  /// @p sourceProvider returns the program currently in the editor.
  MissionsTab(QToolBar *toolbar, Explorer::TraceRecorder *recorder,
              std::function<QString()> sourceProvider,
              QWidget *parent = nullptr);

  void tabVisibilityChanged(bool visible) override;

signals:
  void loadProgramRequested(const QString &program);
  void selectProcessorRequested(Ripes::ProcessorID id);
  void showExplorerRequested();

private slots:
  void showMission(int index);
  void evaluateSteps();
  void checkChallenge();

private:
  struct StepWidgets {
    QLabel *status = nullptr;
    QLabel *checks = nullptr;
  };
  Explorer::MissionState currentState() const;
  void refreshMissionList();
  bool isCompleted(const QString &id) const;
  void markCompleted(const QString &id);
  QString criteriaHtml(const Explorer::CheckReport &report) const;

  Explorer::TraceRecorder *m_recorder = nullptr;
  std::function<QString()> m_sourceProvider;
  std::vector<Explorer::Mission> m_missions;
  int m_current = -1;
  bool m_visible = false;
  /// Steps completed during this session, per mission id. A completed step
  /// stays completed when the learner moves on (e.g. edits the program).
  std::map<QString, std::set<size_t>> m_completedSteps;

  QListWidget *m_list = nullptr;
  QLabel *m_title = nullptr;
  QLabel *m_learn = nullptr;
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
};

} // namespace Ripes
