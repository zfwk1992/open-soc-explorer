#include "i18n.h"

#include "ripessettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace Ripes {

QString uiLanguage() {
  const QString setting =
      RipesSettings::value(RIPES_SETTING_LANGUAGE).toString();
  if (setting == "en" || setting == "zh_CN")
    return setting;
  // "system" (or anything unknown): follow the OS locale.
  if (QLocale::system().language() == QLocale::Chinese)
    return "zh_CN";
  return "en";
}

bool uiIsChinese() { return uiLanguage() == "zh_CN"; }

int installTranslators(QCoreApplication &app) {
  const QString lang = uiLanguage();
  if (lang == "en")
    return 0; // Source strings are English.

  int loaded = 0;

  // Qt's own translations (standard dialog buttons etc.), if shipped with the
  // Qt installation we were built against.
  auto *qtTranslator = new QTranslator(&app);
  if (qtTranslator->load(QLocale(lang), "qtbase", "_",
                         QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
    app.installTranslator(qtTranslator);
    ++loaded;
  } else {
    delete qtTranslator;
  }

  // Application translations embedded under :/i18n/ (one .qm per module).
  QDir dir(":/i18n");
  const auto files =
      dir.entryList({"*_" + lang + ".qm"}, QDir::Files, QDir::Name);
  for (const auto &file : files) {
    auto *translator = new QTranslator(&app);
    if (translator->load(dir.filePath(file))) {
      app.installTranslator(translator);
      ++loaded;
    } else {
      delete translator;
    }
  }
  return loaded;
}

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
