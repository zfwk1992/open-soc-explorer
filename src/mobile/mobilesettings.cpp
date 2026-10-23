// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "mobilesettings.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QEvent>
#include <QLabel>
#include <QRadioButton>
#include <QStyle>
#include <QVBoxLayout>

#include "i18n/language.h"
#include "platform/uimode.h"
#include "ripessettings.h"
#include "theme/theme.h"
#include "touch.h"
#include "version/version.h"

namespace Ripes {
namespace Mobile {

namespace {

QRadioButton *radio(QWidget *parent, const char *name) {
  auto *r = new TouchRadioButton(parent);
  r->setObjectName(name);
  r->setMinimumHeight(kTouchTarget);
  r->setFocusPolicy(Qt::NoFocus);
  return r;
}

} // namespace

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent) {
  setObjectName("mobileSettingsPage");
  auto *v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  m_page = new ScrollPage(this);
  m_page->setObjectName("mobileSettingsScroll");
  v->addWidget(m_page);
  QWidget *c = m_page->content();
  auto *l = m_page->contentLayout();

  m_languageCard = new Card(c);
  m_english = radio(m_languageCard, "mobileLanguageEnglish");
  m_chinese = radio(m_languageCard, "mobileLanguageChinese");
  // Each language is named in its own language.
  m_english->setText("English");
  m_chinese->setText("简体中文");
  auto *languages = new QButtonGroup(this);
  languages->addButton(m_english);
  languages->addButton(m_chinese);
  m_languageCard->body()->addWidget(m_english);
  m_languageCard->body()->addWidget(m_chinese);
  connect(m_english, &QRadioButton::clicked, this, [] {
    RipesSettings::setValue(RIPES_SETTING_OSX_LANGUAGE,
                            I18n::languageCode(I18n::Language::English));
  });
  connect(m_chinese, &QRadioButton::clicked, this, [] {
    RipesSettings::setValue(
        RIPES_SETTING_OSX_LANGUAGE,
        I18n::languageCode(I18n::Language::ChineseSimplified));
  });
  l->addWidget(m_languageCard);

  m_themeCard = new Card(c);
  m_game = radio(m_themeCard, "mobileThemeGame");
  m_classic = radio(m_themeCard, "mobileThemeClassic");
  auto *themes = new QButtonGroup(this);
  themes->addButton(m_game);
  themes->addButton(m_classic);
  connect(m_game, &QRadioButton::clicked, this,
          [] { RipesSettings::setValue(RIPES_SETTING_OSX_THEME, 0); });
  connect(m_classic, &QRadioButton::clicked, this,
          [] { RipesSettings::setValue(RIPES_SETTING_OSX_THEME, 1); });
  m_themeCard->body()->addWidget(m_game);
  m_themeCard->body()->addWidget(m_classic);
  m_schemeLabel = makeLabel(m_themeCard);
  m_schemeLabel->setObjectName("mobileOverline");
  m_themeCard->body()->addWidget(m_schemeLabel);
  m_schemeSystem = radio(m_themeCard, "mobileSchemeSystem");
  m_schemeDark = radio(m_themeCard, "mobileSchemeDark");
  m_schemeLight = radio(m_themeCard, "mobileSchemeLight");
  auto *schemes = new QButtonGroup(this);
  const std::pair<QRadioButton *, ColorScheme> schemeButtons[] = {
      {m_schemeSystem, ColorScheme::System},
      {m_schemeDark, ColorScheme::Dark},
      {m_schemeLight, ColorScheme::Light}};
  for (const auto &[button, scheme] : schemeButtons) {
    schemes->addButton(button);
    m_themeCard->body()->addWidget(button);
    connect(button, &QRadioButton::clicked, this, [scheme = scheme] {
      RipesSettings::setValue(RIPES_SETTING_COLORSCHEME,
                              static_cast<int>(scheme));
    });
  }
  m_motion = new TouchCheckBox(m_themeCard);
  m_motion->setObjectName("mobileReducedMotion");
  m_motion->setMinimumHeight(kTouchTarget);
  m_motion->setFocusPolicy(Qt::NoFocus);
  connect(m_motion, &QCheckBox::toggled, this, [](bool on) {
    if (on != Theme::reducedMotion())
      RipesSettings::setValue(RIPES_SETTING_OSX_REDUCED_MOTION, on);
  });
  m_themeCard->body()->addWidget(m_motion);
  l->addWidget(m_themeCard);

  m_versionCard = new Card(c);
  m_versionNote = makeLabel(m_versionCard);
  m_versionNote->setObjectName("mobileMuted");
  m_desktop = makeButton(QString(), m_versionCard);
  m_desktop->setObjectName("mobileSwitchToDesktop");
  connect(m_desktop, &QAbstractButton::clicked, this,
          &SettingsPage::switchToDesktopRequested);
  m_versionCard->body()->addWidget(m_versionNote);
  m_versionCard->body()->addWidget(m_desktop);
  l->addWidget(m_versionCard);

  m_progressCard = new Card(c);
  m_progressNote = makeLabel(m_progressCard);
  m_progressNote->setObjectName("mobileMuted");
  m_reset = makeButton(QString(), m_progressCard);
  m_reset->setObjectName("mobileResetProgress");
  m_resetArm.setSingleShot(true);
  m_resetArm.setInterval(4000);
  connect(&m_resetArm, &QTimer::timeout, this, &SettingsPage::retranslate);
  // Two taps instead of a modal dialog: the first arms the button.
  connect(m_reset, &QAbstractButton::clicked, this, [this] {
    if (!m_resetArm.isActive()) {
      m_resetArm.start();
      m_reset->setText(tr("Tap again to forget all mission progress"));
      m_reset->setProperty("osxRole", "danger");
      m_reset->style()->unpolish(m_reset);
      m_reset->style()->polish(m_reset);
      return;
    }
    m_resetArm.stop();
    emit resetProgressRequested();
    retranslate();
  });
  m_progressCard->body()->addWidget(m_progressNote);
  m_progressCard->body()->addWidget(m_reset);
  l->addWidget(m_progressCard);

  m_aboutCard = new Card(c);
  m_about = makeLabel(m_aboutCard);
  m_aboutCard->body()->addWidget(m_about);
  l->addWidget(m_aboutCard);
  m_page->finish();

  for (const char *key :
       {RIPES_SETTING_OSX_LANGUAGE, RIPES_SETTING_OSX_THEME,
        RIPES_SETTING_COLORSCHEME, RIPES_SETTING_OSX_REDUCED_MOTION})
    connect(RipesSettings::getObserver(key), &SettingObserver::modified, this,
            &SettingsPage::syncFromSettings);
  retranslate();
  syncFromSettings();
}

QAbstractButton *SettingsPage::desktopButton() const { return m_desktop; }

void SettingsPage::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    retranslate();
}

void SettingsPage::syncFromSettings() {
  const bool zh =
      I18n::configuredLanguage() == I18n::Language::ChineseSimplified;
  m_english->setChecked(!zh);
  m_chinese->setChecked(zh);
  const bool classic =
      RipesSettings::value(RIPES_SETTING_OSX_THEME).toInt() == 1;
  m_game->setChecked(!classic);
  m_classic->setChecked(classic);
  switch (static_cast<ColorScheme>(
      RipesSettings::value(RIPES_SETTING_COLORSCHEME).toInt())) {
  case ColorScheme::Dark:
    m_schemeDark->setChecked(true);
    break;
  case ColorScheme::Light:
    m_schemeLight->setChecked(true);
    break;
  case ColorScheme::System:
    m_schemeSystem->setChecked(true);
    break;
  }
  m_motion->blockSignals(true);
  m_motion->setChecked(Theme::reducedMotion());
  m_motion->blockSignals(false);
}

void SettingsPage::retranslate() {
  m_languageCard->setTitle(tr("Language"));
  m_themeCard->setTitle(tr("Appearance"));
  m_game->setText(tr("Game theme (Open SoC Explorer)"));
  m_classic->setText(tr("Classic theme (Ripes)"));
  m_schemeLabel->setText(tr("COLOR SCHEME"));
  m_schemeSystem->setText(tr("Follow the system"));
  m_schemeDark->setText(tr("Dark"));
  m_schemeLight->setText(tr("Light"));
  m_motion->setText(tr("Reduce motion"));
  m_versionCard->setTitle(tr("Phone version"));
  m_versionNote->setText(
      tr("This is the phone layout of Open SoC Explorer, made for touch. The "
         "desktop version has every view of Ripes and the Explorer (editor, "
         "datapath, memory, cache, system view); it needs a large screen."));
  m_desktop->setText(tr("Switch to the desktop version"));
  m_progressCard->setTitle(tr("Mission progress"));
  m_progressNote->setText(
      tr("Verified steps and solved challenges are stored in this browser "
         "(or on this computer) and shared with the desktop version."));
  m_reset->setText(tr("Reset mission progress"));
  m_reset->setProperty("osxRole", QVariant());
  m_reset->style()->unpolish(m_reset);
  m_reset->style()->polish(m_reset);
  m_aboutCard->setTitle(tr("About"));
  m_about->setText(
      tr("<b>Open SoC Explorer</b>, based on Ripes %1 by Morten Borup "
         "Petersen and contributors (MIT License).<br>Ripes' processors are "
         "educational datapath models with ideal memories: their cycle "
         "counts describe these models, not any real core.")
          .arg(getRipesVersion().toHtmlEscaped()));
}

} // namespace Mobile
} // namespace Ripes
