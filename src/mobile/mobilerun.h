// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: the Run page of the phone user interface
// (program, processor, simulator control).
#pragma once

#include <QTimer>
#include <QWidget>

#include "explorerwidgets.h"
#include "processorregistry.h"

class QAbstractButton;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QToolButton;

namespace Ripes {
namespace Mobile {

class Card;
class ScrollPage;

/**
 * @brief The RunPage class
 * Program editor (RISC-V assembly, assembled as it is typed, like the
 * desktop editor), processor selection, live simulator status and the
 * simulator controls Reset, Step back, Clock and Run/Stop, which call the
 * same ProcessorHandler functions as the desktop toolbar.
 */
class RunPage : public QWidget {
  Q_OBJECT
public:
  explicit RunPage(QWidget *parent = nullptr);

  QString source() const;
  /// Replaces the program and assembles it (e.g. a mission's example).
  void setSource(const QString &source);
  /// Selects processor @p id with its default ISA extensions and registers.
  static void selectProcessor(ProcessorID id);
  /// Mission context shown at the top: number and title (number < 0: none).
  void setMission(int number, const QString &title);
  void setBadges(const std::vector<HudBanner::Badge> &badges,
                 const QString &note);
  ScrollPage *scrollPage() const { return m_page; }
  QAbstractButton *clockButton() const { return m_clock; }
  QAbstractButton *runButton() const { return m_run; }
  QAbstractButton *resetButton() const { return m_reset; }
  QAbstractButton *backButton() const { return m_back; }
  QPlainTextEdit *editor() const { return m_editor; }
  /// Assembles the editor contents now (instead of after the typing delay).
  void assembleNow();
  QString assemblerStatus() const;

signals:
  void loadMissionExampleRequested();
  void selectMissionProcessorRequested();

protected:
  void changeEvent(QEvent *event) override;

private:
  void retranslate();
  void assemble();
  void updateStatus();
  void updateControls();
  void fitEditor();
  void reverse();
  void toggleRun();
  void syncProcessorChoice();

  ScrollPage *m_page = nullptr;
  Card *m_missionCard = nullptr;
  QLabel *m_missionTitle = nullptr;
  QAbstractButton *m_missionLoad = nullptr;
  QAbstractButton *m_missionProcessor = nullptr;
  Card *m_statusCard = nullptr;
  QLabel *m_status = nullptr;
  HudBanner *m_banner = nullptr;
  Card *m_processorCard = nullptr;
  QComboBox *m_processor = nullptr;
  QLabel *m_processorInfo = nullptr;
  Card *m_programCard = nullptr;
  QPlainTextEdit *m_editor = nullptr;
  QLabel *m_assemblerStatus = nullptr;
  QComboBox *m_examples = nullptr;
  QAbstractButton *m_reset = nullptr;
  QAbstractButton *m_back = nullptr;
  QAbstractButton *m_clock = nullptr;
  QAbstractButton *m_run = nullptr;
  QTimer m_assembleTimer;
  QTimer m_statusTimer;
  int m_missionNumber = -1;
  QString m_missionTitleText;
  bool m_settingSource = false;
  int m_programBytes = 0;
  QStringList m_errors;
};

} // namespace Mobile
} // namespace Ripes
