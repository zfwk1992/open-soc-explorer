// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: the Settings page of the phone user interface.
#pragma once

#include <QTimer>
#include <QWidget>

class QAbstractButton;
class QCheckBox;
class QLabel;
class QRadioButton;

namespace Ripes {
namespace Mobile {

class Card;
class ScrollPage;

/// Language, theme, color scheme, reduced motion, the switch to the desktop
/// version and resetting mission progress. All values are the application
/// settings that the desktop version uses as well.
class SettingsPage : public QWidget {
  Q_OBJECT
public:
  explicit SettingsPage(QWidget *parent = nullptr);
  ScrollPage *scrollPage() const { return m_page; }
  QAbstractButton *desktopButton() const;

signals:
  void switchToDesktopRequested();
  void resetProgressRequested();

protected:
  void changeEvent(QEvent *event) override;

private:
  void retranslate();
  void syncFromSettings();

  ScrollPage *m_page = nullptr;
  Card *m_languageCard = nullptr;
  QRadioButton *m_english = nullptr;
  QRadioButton *m_chinese = nullptr;
  Card *m_themeCard = nullptr;
  QRadioButton *m_game = nullptr;
  QRadioButton *m_classic = nullptr;
  QRadioButton *m_schemeSystem = nullptr;
  QRadioButton *m_schemeDark = nullptr;
  QRadioButton *m_schemeLight = nullptr;
  QLabel *m_schemeLabel = nullptr;
  QCheckBox *m_motion = nullptr;
  Card *m_versionCard = nullptr;
  QLabel *m_versionNote = nullptr;
  QAbstractButton *m_desktop = nullptr;
  Card *m_progressCard = nullptr;
  QLabel *m_progressNote = nullptr;
  QAbstractButton *m_reset = nullptr;
  QTimer m_resetArm;
  Card *m_aboutCard = nullptr;
  QLabel *m_about = nullptr;
};

} // namespace Mobile
} // namespace Ripes
