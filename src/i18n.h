#pragma once

#include <QJsonValue>
#include <QString>

class QCoreApplication;

namespace Ripes {

/**
 * UI language support (English / 简体中文).
 *
 * The persisted setting RIPES_SETTING_LANGUAGE holds one of
 *   "system" - follow the OS locale (Chinese locales -> zh_CN, otherwise en)
 *   "en"     - English
 *   "zh_CN"  - Simplified Chinese
 *
 * Translations are Qt .qm files compiled from resources/i18n/<module>_zh_CN.ts
 * and embedded under the resource prefix ":/i18n/". installTranslators()
 * installs every "*_<lang>.qm" found there, so each GUI module may ship its
 * own .ts file without touching shared code.
 */

/// Language code the UI currently runs with: "en" or "zh_CN".
QString uiLanguage();

/// True when the UI language is Simplified Chinese.
bool uiIsChinese();

/// Installs the translators for uiLanguage(). Call once right after the
/// QApplication has been created and before any widget is constructed.
/// Returns the number of translation files that were loaded.
int installTranslators(QCoreApplication &app);

/**
 * Resolves a possibly-bilingual JSON value to a string:
 *   "text"                     -> "text"
 *   {"zh": "文本", "en": "text"} -> entry for uiLanguage() (zh_CN -> "zh"),
 *                                  falling back to "en", then "zh", then the
 *                                  first string member.
 * Any other value yields an empty string.
 */
QString localized(const QJsonValue &v);

/// Picks between two literals in code paths where tr() is not available
/// (e.g. strings assembled outside a QObject). Prefer tr() where possible.
QString bilingual(const QString &en, const QString &zh);

} // namespace Ripes
