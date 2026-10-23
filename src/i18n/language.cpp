// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "language.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QDockWidget>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMutex>
#include <QPointer>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTranslator>

#include "ripessettings.h"

namespace Ripes {
namespace I18n {

namespace {

constexpr const char *kUpstreamContext = "Upstream";
constexpr const char *kOwnTranslationProperty = "osxOwnTranslation";

/// Wraps the translator of the application's catalogue and remembers, for
/// every translation it returned, the English source text. This lets
/// retranslateUpstream() restore English texts of upstream widgets that were
/// created (via tr() or .ui files) while Chinese was active.
class RecordingTranslator : public QTranslator {
public:
  bool loadCatalogue(const QString &path) { return m_inner.load(path); }
  bool isEmpty() const override { return m_inner.isEmpty(); }
  QString translate(const char *context, const char *sourceText,
                    const char *disambiguation = nullptr,
                    int n = -1) const override {
    const QString t = m_inner.translate(context, sourceText, disambiguation, n);
    if (!t.isEmpty() && sourceText && n < 0) {
      QMutexLocker lock(&m_mutex);
      m_sources.insert(t, QString::fromUtf8(sourceText));
    }
    return t;
  }
  /// English source of a text this translator produced (or @p text).
  QString sourceOf(const QString &text) const {
    QMutexLocker lock(&m_mutex);
    return m_sources.value(text, text);
  }

private:
  QTranslator m_inner;
  mutable QMutex m_mutex;
  mutable QHash<QString, QString> m_sources;
};

struct State {
  bool installed = false;
  bool translatorInstalled = false;
  Language current = Language::English;
  RecordingTranslator *translator = nullptr;
  bool catalogueLoaded = false;
  QPointer<QMenu> menu;
  QPointer<QAction> english;
  QPointer<QAction> chinese;
};

State &state() {
  static State s;
  return s;
}

/// Translation context of the Language menu.
struct Text {
  Q_DECLARE_TR_FUNCTIONS(Ripes::I18n)
};

void updateMenu() {
  auto &s = state();
  if (!s.menu)
    return;
  s.menu->setTitle(Text::tr("Language"));
  s.english->setChecked(s.current == Language::English);
  s.chinese->setChecked(s.current == Language::ChineseSimplified);
  s.english->setToolTip(Text::tr("Show the user interface in English"));
  s.chinese->setToolTip(
      Text::tr("Show the user interface in Simplified Chinese"));
}

/// Re-translates upstream dialogs when they are shown.
class DialogWatcher : public QObject {
public:
  using QObject::QObject;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Show && watched->isWidgetType() &&
        qobject_cast<QDialog *>(watched))
      retranslateUpstream(watched);
    return QObject::eventFilter(watched, event);
  }
};

bool ownTranslation(const QObject *o) {
  for (; o; o = o->parent())
    if (o->property(kOwnTranslationProperty).toBool())
      return true;
  return false;
}

/// Translation contexts of @p o: the classes of the object and of its
/// parents (full and namespace-less names), then the "Upstream" context.
QList<QByteArray> contextsOf(const QObject *o) {
  QList<QByteArray> contexts;
  for (const QObject *p = o; p; p = p->parent()) {
    for (const QMetaObject *mo = p->metaObject(); mo; mo = mo->superClass()) {
      const QByteArray name = mo->className();
      if (name.startsWith('Q') && !name.contains("::"))
        break; // Qt's own classes: no upstream strings.
      if (!contexts.contains(name))
        contexts << name;
      const int sep = name.lastIndexOf("::");
      if (sep >= 0 && !contexts.contains(name.mid(sep + 2)))
        contexts << name.mid(sep + 2);
    }
  }
  contexts << kUpstreamContext;
  return contexts;
}

/// The text to show for @p field of @p o, given its current text.
QString retranslated(QObject *o, const QByteArray &field,
                     const QString &current,
                     const QList<QByteArray> &contexts) {
  const QByteArray srcKey = "_osx_src_" + field;
  const QByteArray appliedKey = "_osx_applied_" + field;
  QString source;
  const QVariant applied = o->property(appliedKey.constData());
  if (applied.isValid() && applied.toString() == current)
    source = o->property(srcKey.constData()).toString();
  else
    source =
        state().translator ? state().translator->sourceOf(current) : current;
  QString result = source;
  if (!source.trimmed().isEmpty()) {
    const QByteArray utf8 = source.toUtf8();
    for (const auto &ctx : contexts) {
      const QString t = QCoreApplication::translate(ctx.constData(), utf8);
      if (t != source) {
        result = t;
        break;
      }
    }
  }
  o->setProperty(srcKey.constData(), source);
  o->setProperty(appliedKey.constData(), result);
  return result;
}

void retranslateObject(QObject *o) {
  if (ownTranslation(o))
    return;
  const auto contexts = contextsOf(o);
  if (auto *a = qobject_cast<QAction *>(o)) {
    if (a->menu() || a->isSeparator())
      return;
    const QString text = retranslated(o, "text", a->text(), contexts);
    if (text != a->text())
      a->setText(text);
    // Only explicit tool tips (QAction derives a default one from the text).
    if (o->property("_osx_applied_toolTip").isValid() ||
        a->toolTip() != QString(a->text()).remove('&')) {
      const QString tip = retranslated(o, "toolTip", a->toolTip(), contexts);
      if (tip != a->toolTip())
        a->setToolTip(tip);
    }
    return;
  }
  if (!o->isWidgetType())
    return;
  auto *w = static_cast<QWidget *>(o);
  if (auto *m = qobject_cast<QMenu *>(w)) {
    const QString title = retranslated(o, "title", m->title(), contexts);
    if (title != m->title())
      m->setTitle(title);
  }
  if (w->isWindow() || qobject_cast<QToolBar *>(w) ||
      qobject_cast<QDockWidget *>(w)) {
    const QString title =
        retranslated(o, "windowTitle", w->windowTitle(), contexts);
    if (title != w->windowTitle())
      w->setWindowTitle(title);
  }
  if (auto *l = qobject_cast<QLabel *>(w)) {
    const QString text = retranslated(o, "text", l->text(), contexts);
    if (text != l->text())
      l->setText(text);
  } else if (auto *b = qobject_cast<QAbstractButton *>(w)) {
    // Tool buttons of actions follow the action.
    if (auto *tb = qobject_cast<QToolButton *>(b); tb && tb->defaultAction())
      return;
    if (!b->text().isEmpty()) {
      const QString text = retranslated(o, "text", b->text(), contexts);
      if (text != b->text())
        b->setText(text);
    }
  } else if (auto *g = qobject_cast<QGroupBox *>(w)) {
    const QString title = retranslated(o, "title", g->title(), contexts);
    if (title != g->title())
      g->setTitle(title);
  } else if (auto *tabs = qobject_cast<QTabWidget *>(w)) {
    for (int i = 0; i < tabs->count(); ++i) {
      const QString text = retranslated(o, "tab" + QByteArray::number(i),
                                        tabs->tabText(i), contexts);
      if (text != tabs->tabText(i))
        tabs->setTabText(i, text);
    }
  }
  if (!w->toolTip().isEmpty() ||
      w->property("_osx_applied_toolTip").isValid()) {
    const QString tip = retranslated(o, "toolTip", w->toolTip(), contexts);
    if (tip != w->toolTip())
      w->setToolTip(tip);
  }
}

} // namespace

QString languageCode(Language language) {
  return language == Language::ChineseSimplified ? "zh_CN" : "en";
}

Language configuredLanguage() {
  const QString value =
      RipesSettings::value(RIPES_SETTING_OSX_LANGUAGE).toString();
  if (value == "zh_CN")
    return Language::ChineseSimplified;
  if (value == "en")
    return Language::English;
  return QLocale::system().language() == QLocale::Chinese
             ? Language::ChineseSimplified
             : Language::English;
}

Language current() { return state().current; }

QString missionLanguage() {
  return current() == Language::ChineseSimplified ? "zh_CN" : QString();
}

/// The default QLocale follows the language (Qt's dialogs, CJK-aware text
/// layout such as Theme::overlineFont()). English keeps the system locale
/// unless that is Chinese.
void applyLocale(Language lang) {
  if (lang == Language::ChineseSimplified)
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
  else if (QLocale::system().language() == QLocale::Chinese)
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
  else
    QLocale::setDefault(QLocale::system());
}

void apply() {
  auto &s = state();
  const Language lang = configuredLanguage();
  if (s.translatorInstalled == (lang == Language::ChineseSimplified)) {
    s.current = lang;
    updateMenu();
    return;
  }
  if (!s.translator)
    s.translator = new RecordingTranslator();
  if (lang == Language::ChineseSimplified) {
    if (!s.catalogueLoaded)
      s.catalogueLoaded =
          s.translator->loadCatalogue(":/i18n/open-soc-explorer_zh_CN.qm");
    if (s.catalogueLoaded) {
      s.current = lang;
      s.translatorInstalled = QCoreApplication::installTranslator(s.translator);
    } else {
      qWarning("Open SoC Explorer: the Chinese translation is not available "
               "in this build.");
      s.current = Language::English;
    }
  } else {
    s.current = lang;
    QCoreApplication::removeTranslator(s.translator);
    s.translatorInstalled = false;
  }
  applyLocale(s.current);
  updateMenu();
}

void install(QMenu *viewMenu) {
  auto &s = state();
  if (!s.installed) {
    s.installed = true;
    QObject::connect(RipesSettings::getObserver(RIPES_SETTING_OSX_LANGUAGE),
                     &SettingObserver::modified, qApp, [] { apply(); });
    qApp->installEventFilter(new DialogWatcher(qApp));
  }
  if (viewMenu && !s.menu) {
    s.menu = viewMenu->addMenu(Text::tr("Language"));
    auto *group = new QActionGroup(s.menu);
    // Each language is named in its own language.
    s.english = s.menu->addAction("English");
    s.chinese = s.menu->addAction("简体中文");
    for (auto *a : {s.english.data(), s.chinese.data()}) {
      a->setCheckable(true);
      group->addAction(a);
    }
    QObject::connect(s.english, &QAction::triggered, s.menu, [] {
      RipesSettings::setValue(RIPES_SETTING_OSX_LANGUAGE,
                              languageCode(Language::English));
    });
    QObject::connect(s.chinese, &QAction::triggered, s.menu, [] {
      RipesSettings::setValue(RIPES_SETTING_OSX_LANGUAGE,
                              languageCode(Language::ChineseSimplified));
    });
    // The menu translates itself (updateMenu()).
    s.menu->setProperty(kOwnTranslationProperty, true);
  }
  apply();
}

void retranslateUpstream(QObject *root) {
  if (!root)
    return;
  retranslateObject(root);
  for (auto *o : root->findChildren<QObject *>())
    retranslateObject(o);
}

} // namespace I18n
} // namespace Ripes
