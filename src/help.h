#pragma once

#include <QString>

namespace Ripes {

/**
 * Opens the user guide (docs/guide/<lang>/<chapter>.md) for the current UI
 * language. @p chapter is the file stem, e.g. "09-gpu" or "03-gui-tour"; an
 * empty string opens the guide index. GUI modules call this from their
 * help ("?") buttons so that every part of the UI links to its manual page.
 */
void showHelp(const QString &chapter = QString());

/// Directory that contains the "zh" and "en" guide folders, or an empty
/// string when the guide could not be located next to the executable or the
/// source tree.
QString userGuideDir();

} // namespace Ripes
