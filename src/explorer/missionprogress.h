// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer. Scoring rules: docs/LEARNING_GUIDE.md §3.1.
#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "mission.h"

namespace Ripes {
namespace Explorer {

/// What a learner achieved in one mission. Only two facts count, and both are
/// established by the mission's checks (evaluateChecks()): guided steps that
/// were verified live against the simulator, and a solved challenge.
struct MissionRecord {
  /// Number of guided steps of the mission.
  int steps = 0;
  /// Guided steps verified live (0..steps).
  int verifiedSteps = 0;
  /// The challenge checks passed.
  bool solved = false;
};

/// Experience points per verified guided step and per solved challenge.
inline constexpr int kXpPerStep = 100;
inline constexpr int kXpPerChallenge = 300;
/// Experience points needed for each level.
inline constexpr int kXpPerLevel = 500;
inline constexpr int kStarsPerMission = 3;

/// Stars of a mission (0..3):
///  - one when at least half of the guided steps (rounded up) are verified,
///  - one when all guided steps are verified,
///  - one when the challenge is solved.
int missionStars(const MissionRecord &r);
/// kXpPerStep per verified step + kXpPerChallenge if solved.
int missionXp(const MissionRecord &r);

struct PlayerLevel {
  int xp = 0;
  /// 1 + xp / kXpPerLevel.
  int level = 1;
  /// Experience points earned within the current level (0..kXpPerLevel-1).
  int xpIntoLevel = 0;
  /// Name of the level (pipeline stages: Fetch, Decode, ...).
  QString title;
};
PlayerLevel playerLevel(int xp);
QString levelTitle(int level);

enum class MissionStatus { Locked, Available, InProgress, Solved };

/// Status of mission @p index in @p records (missions in catalog order):
///  - Solved: its challenge is solved;
///  - InProgress: at least one of its guided steps is verified;
///  - Available: it is the first mission, or the previous mission has all
///    guided steps verified or its challenge solved;
///  - Locked: otherwise. Locked missions can still be opened and played; the
///    lock only recommends an order.
MissionStatus missionStatus(const std::vector<MissionRecord> &records,
                            size_t index);
QString missionStatusName(MissionStatus status);

/**
 * @brief The MissionProgress class
 * The learner's persistent mission progress: verified guided steps (keys
 * "<mission id>/<step number>", step numbers start at 1) and solved missions
 * (mission ids). The Missions tab stores both lists in the application
 * settings and only adds entries after the mission's checks passed.
 */
class MissionProgress {
public:
  MissionProgress() = default;
  MissionProgress(QStringList verifiedSteps, QStringList solved);

  static QString stepKey(const QString &missionId, size_t stepIndex);

  bool stepVerified(const QString &missionId, size_t stepIndex) const;
  /// Records a verified step. Returns true if it was not recorded before.
  bool recordStep(const QString &missionId, size_t stepIndex);
  bool isSolved(const QString &missionId) const;
  /// Records a solved challenge. Returns true if it was not recorded before.
  bool recordSolved(const QString &missionId);

  MissionRecord record(const Mission &mission) const;
  std::vector<MissionRecord>
  records(const std::vector<Mission> &missions) const;

  struct Totals {
    int xp = 0;
    int stars = 0;
    int maxStars = 0;
    int solved = 0;
    int missions = 0;
    int steps = 0;
    int verifiedSteps = 0;
  };
  Totals totals(const std::vector<Mission> &missions) const;

  const QStringList &verifiedSteps() const { return m_steps; }
  const QStringList &solved() const { return m_solved; }

private:
  QStringList m_steps;
  QStringList m_solved;
};

} // namespace Explorer
} // namespace Ripes
