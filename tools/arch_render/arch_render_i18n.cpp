// Stand-in for src/i18n.cpp so that arch_render does not need RipesSettings:
// the language comes from the --lang command line option instead of the
// persisted UI language setting. Same resolution order as Ripes::localized().
#include "i18n.h"

#include <QJsonObject>

namespace Ripes {

namespace {
QString g_lang = "zh_CN";
} // namespace

void setRenderLanguage(const QString &lang) {
  g_lang = lang == "en" ? "en" : "zh_CN";
}

QString uiLanguage() { return g_lang; }

bool uiIsChinese() { return g_lang == "zh_CN"; }

QString localized(const QJsonValue &v) {
  if (v.isString())
    return v.toString();
  if (!v.isObject())
    return QString();
  const QJsonObject obj = v.toObject();
  const QString primary = uiIsChinese() ? "zh" : "en";
  for (const QString &key : {primary, QString("en"), QString("zh")}) {
    const auto it = obj.constFind(key);
    if (it != obj.constEnd() && it->isString())
      return it->toString();
  }
  for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    if (it->isString())
      return it->toString();
  return QString();
}

QString bilingual(const QString &en, const QString &zh) {
  return uiIsChinese() ? zh : en;
}

} // namespace Ripes
