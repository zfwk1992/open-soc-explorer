// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QStringList>

namespace Ripes {
namespace Explorer {

/// Translation context of descriptive texts stored in traces.
constexpr const char *kTraceTextContext = "Ripes::Explorer::TraceText";

/**
 * Display text of a descriptive string stored in a trace (fidelity notes,
 * caveats, component descriptions, parameter meanings, clock domain
 * descriptions, stage notes) in the current user interface language.
 *
 * Traces themselves always store the English texts written by the adapter
 * (the Ripes recorder or tools/gem5/gem5_to_osx.py), so that exported traces
 * do not depend on the language. Known texts, also with numbers or names
 * filled in, are translated when shown; unknown texts (e.g. gem5's own
 * statistic descriptions) are returned unchanged.
 */
QString localizedTraceText(const QString &text);

/// The gem5 converter's texts and the texts with placeholders that
/// localizedTraceText() knows (for tests).
QStringList knownTraceTexts();

} // namespace Explorer
} // namespace Ripes
