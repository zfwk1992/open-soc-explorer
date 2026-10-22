// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QString>

class QMenu;
class QObject;

namespace Ripes {
namespace I18n {

/// User interface languages (setting RIPES_SETTING_OSX_LANGUAGE).
enum class Language { English = 0, ChineseSimplified = 1 };

/// Setting value of @p language ("en", "zh_CN").
QString languageCode(Language language);
/// The language selected by the setting; "system" resolves to Simplified
/// Chinese if the system locale is Chinese, else to English.
Language configuredLanguage();
/// The language whose translations are currently installed.
Language current();
inline bool isChinese() { return current() == Language::ChineseSimplified; }
/// Language key of the optional "i18n" objects in mission files ("zh_CN"),
/// or an empty string for English (the missions' own fields).
QString missionLanguage();

/// Installs the translations of the persisted language and adds the
/// "Language" submenu to @p viewMenu (may be nullptr). Called once by
/// MainWindow; afterwards the translations follow the setting. Installing or
/// removing a translator sends QEvent::LanguageChange to all widgets.
void install(QMenu *viewMenu);
/// Installs the translations of configuredLanguage() now.
void apply();

/// Translates user-visible texts of upstream Ripes widgets and actions below
/// @p root (menus, actions, labels, buttons, group boxes, tab titles, window
/// titles, tool tips) that were set from plain string literals or set once
/// at construction. Each text is looked up in the contexts of the object's
/// class and of its parents' classes (the contexts of their tr() calls and
/// .ui files) and in the "Upstream" context (see upstreamstrings.cpp); the
/// English source is remembered, so that switching back restores it.
/// Objects below a widget with the property "osxOwnTranslation" set to true
/// (Open SoC Explorer's own views, which retranslate themselves) are
/// skipped.
void retranslateUpstream(QObject *root);

} // namespace I18n
} // namespace Ripes
