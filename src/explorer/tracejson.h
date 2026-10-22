// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer. See docs/TRACE_PROTOCOL.md.
#pragma once

#include <QIODevice>
#include <QJsonObject>

#include <map>
#include <optional>

#include "tracetypes.h"

namespace Ripes {
namespace Trace {

/// Resolution of each instruction instance: true if it was squashed (it was
/// fetched down a path that never became architectural), false if it
/// retired, std::nullopt if still in flight when the trace ended.
std::map<qint64, std::optional<bool>> speculativeStatus(const Trace &trace);

QJsonObject headerToJson(const Trace &trace);
QJsonObject eventToJson(const Trace &trace, const TraceEvent &event,
                        const std::map<qint64, std::optional<bool>> &spec);
QJsonObject footerToJson(const Trace &trace);

/// Writes @p trace as JSON Lines: a header record, one record per event and a
/// footer record.
bool writeJsonl(const Trace &trace, QIODevice &out, QString *error = nullptr);
bool writeJsonlFile(const Trace &trace, const QString &path,
                    QString *error = nullptr);
/// Returns the JSON Lines representation of @p trace.
QByteArray toJsonl(const Trace &trace);

/// Parses a JSON Lines trace. Fails on unknown major schema versions.
std::optional<Trace> readJsonl(QIODevice &in, QString *error = nullptr);
std::optional<Trace> readJsonlFile(const QString &path,
                                   QString *error = nullptr);
std::optional<Trace> fromJsonl(const QByteArray &data,
                               QString *error = nullptr);

} // namespace Trace
} // namespace Ripes
