// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QString>

#include <optional>

namespace Ripes {

/// The user interface the application shows: the desktop main window or the
/// phone shell (src/mobile, touch only, portrait and landscape).
enum class UiMode { Desktop, Mobile };

/// The user interface chosen at startup. WebAssembly: the page URL query
/// parameter "ui" (?ui=mobile or ?ui=desktop); native builds: the command
/// line option "--ui mobile|desktop". Default: Desktop. Determined once.
UiMode currentUiMode();

/// Overrides the mode (tests, switching the user interface at run time).
void setUiMode(UiMode mode);

/// "mobile" / "desktop".
QString uiModeName(UiMode mode);
/// Parses "mobile" or "desktop" (case-insensitive; "phone" is accepted for
/// Mobile); std::nullopt for anything else.
std::optional<UiMode> parseUiMode(const QString &text);
/// The value of "--ui <mode>" or "--ui=<mode>" in @p arguments.
std::optional<UiMode> uiModeFromArguments(const QStringList &arguments);
/// The value of the "ui" parameter of a URL query string such as
/// "?ui=mobile&x=1".
std::optional<UiMode> uiModeFromQuery(const QString &query);

/// True if the page of @p mode can be opened by navigating the browser
/// (WebAssembly build). The pages are those of the web site
/// (web/prepare_site.py): desktop.html?ui=desktop and mobile.html?ui=mobile.
bool canOpenUiModePage();
/// Navigates the browser to the page of @p mode (WebAssembly only). Returns
/// false if that is not possible.
bool openUiModePage(UiMode mode);

} // namespace Ripes
