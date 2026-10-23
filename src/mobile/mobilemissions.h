// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: the Missions page of the phone user interface.
#pragma once

#include <QAbstractButton>
#include <QWidget>

#include <functional>
#include <vector>

#include "explorer/mission.h"
#include "explorer/missionprogress.h"
#include "processorregistry.h"

class QFrame;
class QLabel;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace Ripes {
namespace Explorer {
class TraceRecorder;
}
namespace Mobile {

class Card;
class ScrollPage;

/// Player progress (level, experience points, stars, solved challenges,
/// verified steps), painted from MissionProgress::totals().
class PlayerHud : public QWidget {
  Q_OBJECT
public:
  explicit PlayerHud(QWidget *parent = nullptr);
  void setTotals(const Explorer::MissionProgress::Totals &totals);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  Explorer::MissionProgress::Totals m_totals;
};

/// One mission of the mission map as a large touch card.
class MissionCard : public QAbstractButton {
  Q_OBJECT
public:
  struct Info {
    int number = 0;
    QString title;
    QString processor;
    Explorer::MissionRecord record;
    Explorer::MissionStatus status = Explorer::MissionStatus::Available;
    bool next = false;
  };
  explicit MissionCard(QWidget *parent = nullptr);
  void setInfo(const Info &info);
  const Info &info() const { return m_info; }
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  Info m_info;
};

/// Stars of a mission and its experience points.
class StarsRow : public QWidget {
  Q_OBJECT
public:
  explicit StarsRow(QWidget *parent = nullptr);
  void setRecord(const Explorer::MissionRecord &record);
  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  Explorer::MissionRecord m_record;
};

/**
 * @brief The MissionsPage class
 * Mission map (player progress and one card per mission) and mission detail
 * (objective, concepts, guided steps verified live, challenge). Missions,
 * criteria, scoring and the persisted progress are the same as in the
 * desktop Missions tab (Explorer::Mission, Explorer::MissionProgress, the
 * settings RIPES_SETTING_OSX_MISSION_STEPS/_COMPLETED): stars and experience
 * points only come from steps verified against the simulator and from
 * solved challenges.
 */
class MissionsPage : public QWidget {
  Q_OBJECT
public:
  MissionsPage(Explorer::TraceRecorder *recorder,
               std::function<QString()> sourceProvider,
               QWidget *parent = nullptr);

  const std::vector<Explorer::Mission> &missions() const { return m_missions; }
  /// Index of the open mission, -1 on the mission map.
  int currentMission() const { return m_current; }
  /// The mission that was opened last (stays set on the mission map), or -1.
  int lastMission() const { return m_last; }
  const Explorer::MissionProgress &progress() const { return m_progress; }
  bool showingDetail() const;
  /// The scroll page currently shown (map or detail).
  ScrollPage *currentScrollPage() const;
  /// Clears all verified steps and solved challenges.
  void resetProgress();
  QWidget *missionCardWidget(int index) const;

public slots:
  void openMission(int index);
  void showMap();
  /// Verifies the guided steps of the open mission against the simulator.
  void evaluateSteps();
  void checkChallenge();
  void loadExample();
  void selectProcessor();

signals:
  void loadProgramRequested(const QString &program);
  void selectProcessorRequested(Ripes::ProcessorID id);
  void openRunRequested();
  /// A short message about newly verified progress (shown as a toast).
  void progressMessage(const QString &message);
  void currentMissionChanged(int index);

protected:
  void changeEvent(QEvent *event) override;

private:
  struct StepWidgets {
    QFrame *box = nullptr;
    QLabel *status = nullptr;
    QLabel *text = nullptr;
    QLabel *checks = nullptr;
  };
  void buildMap();
  void buildDetail();
  void retranslate();
  void loadProgress();
  void saveProgress();
  void refreshMap();
  void refreshDetailHeader();
  QString criteriaHtml(const Explorer::CheckReport &report) const;
  Explorer::MissionState currentState() const;

  Explorer::TraceRecorder *m_recorder = nullptr;
  std::function<QString()> m_sourceProvider;
  std::vector<Explorer::Mission> m_missions;
  Explorer::MissionProgress m_progress;
  int m_current = -1;
  int m_last = -1;
  bool m_saving = false;

  QStackedWidget *m_stack = nullptr;
  // Map.
  ScrollPage *m_mapPage = nullptr;
  PlayerHud *m_hud = nullptr;
  QLabel *m_mapTitle = nullptr;
  QLabel *m_mapNote = nullptr;
  std::vector<MissionCard *> m_cards;
  // Detail.
  QWidget *m_detail = nullptr;
  ScrollPage *m_detailPage = nullptr;
  QToolButton *m_backButton = nullptr;
  QLabel *m_detailBarTitle = nullptr;
  Card *m_bannerCard = nullptr;
  QLabel *m_overline = nullptr;
  QLabel *m_title = nullptr;
  StarsRow *m_stars = nullptr;
  QLabel *m_meta = nullptr;
  QLabel *m_lockNote = nullptr;
  Card *m_objectiveCard = nullptr;
  QLabel *m_objective = nullptr;
  QAbstractButton *m_loadButton = nullptr;
  QAbstractButton *m_processorButton = nullptr;
  QAbstractButton *m_runButton = nullptr;
  Card *m_conceptCard = nullptr;
  QLabel *m_concepts = nullptr;
  Card *m_stepsCard = nullptr;
  QVBoxLayout *m_stepsLayout = nullptr;
  std::vector<StepWidgets> m_stepWidgets;
  Card *m_challengeCard = nullptr;
  QLabel *m_challenge = nullptr;
  QAbstractButton *m_hintButton = nullptr;
  QLabel *m_hint = nullptr;
  QAbstractButton *m_checkButton = nullptr;
  QLabel *m_result = nullptr;
  QLabel *m_completion = nullptr;
  Card *m_exampleCard = nullptr;
  QLabel *m_example = nullptr;
};

} // namespace Mobile
} // namespace Ripes
