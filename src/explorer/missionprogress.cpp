// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "missionprogress.h"

#include <algorithm>

namespace Ripes {
namespace Explorer {

int missionStars(const MissionRecord &r) {
  int stars = 0;
  if (r.steps > 0) {
    const int half = (r.steps + 1) / 2;
    if (r.verifiedSteps >= half)
      ++stars;
    if (r.verifiedSteps >= r.steps)
      ++stars;
  }
  if (r.solved)
    ++stars;
  return stars;
}

int missionXp(const MissionRecord &r) {
  return kXpPerStep * std::clamp(r.verifiedSteps, 0, std::max(0, r.steps)) +
         (r.solved ? kXpPerChallenge : 0);
}

QString levelTitle(int level) {
  static const QStringList titles = {"Fetch",       "Decode",     "Execute",
                                     "Memory",      "Write-back", "Superscalar",
                                     "Out-of-order"};
  if (level >= 1 && level <= titles.size())
    return titles[level - 1];
  return "SoC Architect";
}

PlayerLevel playerLevel(int xp) {
  PlayerLevel l;
  l.xp = std::max(0, xp);
  l.level = 1 + l.xp / kXpPerLevel;
  l.xpIntoLevel = l.xp % kXpPerLevel;
  l.title = levelTitle(l.level);
  return l;
}

MissionStatus missionStatus(const std::vector<MissionRecord> &records,
                            size_t index) {
  if (index >= records.size())
    return MissionStatus::Locked;
  const auto &r = records[index];
  if (r.solved)
    return MissionStatus::Solved;
  if (r.verifiedSteps > 0)
    return MissionStatus::InProgress;
  if (index == 0)
    return MissionStatus::Available;
  const auto &prev = records[index - 1];
  if (prev.solved || (prev.steps > 0 && prev.verifiedSteps >= prev.steps))
    return MissionStatus::Available;
  return MissionStatus::Locked;
}

QString missionStatusName(MissionStatus status) {
  switch (status) {
  case MissionStatus::Locked:
    return "Locked";
  case MissionStatus::Available:
    return "Available";
  case MissionStatus::InProgress:
    return "In progress";
  case MissionStatus::Solved:
    return "Solved";
  }
  return QString();
}

MissionProgress::MissionProgress(QStringList verifiedSteps, QStringList solved)
    : m_steps(std::move(verifiedSteps)), m_solved(std::move(solved)) {
  m_steps.removeDuplicates();
  m_solved.removeDuplicates();
}

QString MissionProgress::stepKey(const QString &missionId, size_t stepIndex) {
  return QString("%1/%2").arg(missionId).arg(stepIndex + 1);
}

bool MissionProgress::stepVerified(const QString &missionId,
                                   size_t stepIndex) const {
  return m_steps.contains(stepKey(missionId, stepIndex));
}

bool MissionProgress::recordStep(const QString &missionId, size_t stepIndex) {
  const QString key = stepKey(missionId, stepIndex);
  if (m_steps.contains(key))
    return false;
  m_steps << key;
  return true;
}

bool MissionProgress::isSolved(const QString &missionId) const {
  return m_solved.contains(missionId);
}

bool MissionProgress::recordSolved(const QString &missionId) {
  if (m_solved.contains(missionId))
    return false;
  m_solved << missionId;
  return true;
}

MissionRecord MissionProgress::record(const Mission &mission) const {
  MissionRecord r;
  r.steps = static_cast<int>(mission.steps.size());
  for (size_t i = 0; i < mission.steps.size(); ++i)
    if (stepVerified(mission.id, i))
      ++r.verifiedSteps;
  r.solved = isSolved(mission.id);
  return r;
}

std::vector<MissionRecord>
MissionProgress::records(const std::vector<Mission> &missions) const {
  std::vector<MissionRecord> out;
  out.reserve(missions.size());
  for (const auto &m : missions)
    out.push_back(record(m));
  return out;
}

MissionProgress::Totals
MissionProgress::totals(const std::vector<Mission> &missions) const {
  Totals t;
  for (const auto &m : missions) {
    const auto r = record(m);
    t.xp += missionXp(r);
    t.stars += missionStars(r);
    t.maxStars += kStarsPerMission;
    t.solved += r.solved ? 1 : 0;
    t.steps += r.steps;
    t.verifiedSteps += r.verifiedSteps;
    ++t.missions;
  }
  return t;
}

} // namespace Explorer
} // namespace Ripes
