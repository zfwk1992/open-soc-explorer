// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer. Mission format: docs/LEARNING_GUIDE.md.
#pragma once

#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

#include <map>
#include <optional>
#include <set>
#include <vector>

#include "cachemodel.h"
#include "tracereplay.h"
#include "tracetypes.h"

namespace Ripes {
namespace Explorer {

/// A single automatically verifiable condition, e.g. "x3 = 30" or "at most 0
/// stall cycles". See criterionKinds() for the supported kinds.
struct MissionCriterion {
  QString kind;
  QVariantMap params;
};

struct MissionStep {
  QString title;
  QString instruction;
  std::vector<MissionCriterion> checks;
};

struct Mission {
  QString id;
  int number = 0;
  QString title;
  QStringList learn;
  /// ProcessorID name the mission is designed for.
  QString processor;
  QString objective;
  /// HTML explanation of the concepts.
  QString explanation;
  /// Example program (RISC-V assembly).
  QString example;
  std::vector<MissionStep> steps;
  QString challenge;
  QString hint;
  std::vector<MissionCriterion> challengeChecks;
  QString completion;
  /// Known-good challenge solution, used by the automated tests to verify
  /// that each mission is solvable. Not shown in the GUI.
  QString referenceSolution;
  /// Data cache that the dcache_* criteria simulate (JSON "data_cache").
  std::optional<CacheGeometry> dataCache;
};

/// Facts about the current simulation that missions are verified against.
/// Live processor state and statistics derived from the recorded trace.
struct MissionState {
  QString processor;
  bool programLoaded = false;
  QString source;
  qint64 cycle = 0;
  bool finished = false;
  unsigned xlen = 32;
  std::vector<quint64> gpr;
  QStringList gprNames;
  QStringList gprAliases;
  bool traceStartsAtReset = false;
  bool traceTruncated = false;
  TraceReplay::Stats stats;
  /// Mnemonic of the instruction that last wrote each GPR (by index).
  std::map<int, QString> lastWriter;
  /// Data memory accesses of the recorded trace, in order.
  std::vector<DataAccess> dataAccesses;
};

struct CriterionResult {
  bool passed = false;
  QString description;
  /// The measured value, e.g. "measured: 2 stall cycles".
  QString detail;
};

struct CheckReport {
  bool passed = false;
  std::vector<CriterionResult> results;
};

/// Supported criterion kinds.
QStringList criterionKinds();

/// Parses a mission from JSON. Returns std::nullopt and sets @p error on
/// failure (unknown criterion kinds are rejected).
std::optional<Mission> parseMission(const QJsonObject &o, QString *error);

/// Loads the built-in missions (Qt resources under :/missions), ordered by
/// mission number.
std::vector<Mission> loadBuiltinMissions(QString *error = nullptr);

/// Builds a MissionState from the live ProcessorHandler state and @p trace.
MissionState captureMissionState(const Trace::Trace &trace,
                                 const QString &source);

CriterionResult evaluateCriterion(const MissionCriterion &c,
                                  const MissionState &s,
                                  const Mission &mission);
CheckReport evaluateChecks(const std::vector<MissionCriterion> &checks,
                           const MissionState &s, const Mission &mission);

/// Whitespace/comment-insensitive comparison of two assembly programs.
bool sameProgram(const QString &a, const QString &b);

} // namespace Explorer
} // namespace Ripes
