// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: the phone user interface (UiMode::Mobile).
#pragma once

#include <QJsonObject>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <vector>

#include "explorer/tracereplay.h"
#include "explorerwidgets.h"

class QApplication;
class QBoxLayout;
class QLabel;
class QStackedWidget;

namespace Ripes {
namespace Explorer {
class TraceRecorder;
}
namespace Mobile {

class ExplainPage;
class MissionsPage;
class NavBar;
class PipelinePage;
class RegistersPage;
class RunPage;
class ScrollPage;
class SettingsPage;

/// Engine, model, fidelity and cycle of the shown trace as one line of small
/// pills (the compact form of the Explorer's HUD banner).
class BadgeStrip : public QWidget {
  Q_OBJECT
public:
  explicit BadgeStrip(QWidget *parent = nullptr);
  void setBadges(const std::vector<HudBanner::Badge> &badges);
  const std::vector<HudBanner::Badge> &badges() const { return m_badges; }
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  std::vector<HudBanner::Badge> m_badges;
};

/**
 * @brief The MobileShell class
 * Phone user interface of the same application: a header with the page
 * title and the engine / model / fidelity badges, the pages Missions, Run,
 * Pipeline, Explanation, Registers and Settings, and a bottom navigation
 * bar. Every page scrolls vertically by touch (kinetic) and fits the width
 * of the screen (360–430 CSS px portrait, also landscape).
 *
 * It uses the same simulator (ProcessorHandler), trace recorder, replay,
 * explanations, missions and settings as the desktop main window; the views
 * only show what the simulator and the recorded trace report.
 */
class MobileShell : public QWidget {
  Q_OBJECT
public:
  enum Page {
    MissionsPageId,
    RunPageId,
    PipelinePageId,
    ExplainPageId,
    RegistersPageId,
    SettingsPageId,
    NPages
  };

  explicit MobileShell(QWidget *parent = nullptr);
  ~MobileShell() override;

  int currentPage() const;
  void setCurrentPage(int page);
  static QString pageKey(int page);

  NavBar *navBar() const { return m_nav; }
  MissionsPage *missionsPage() const { return m_missions; }
  RunPage *runPage() const { return m_run; }
  PipelinePage *pipelinePage() const { return m_pipeline; }
  ExplainPage *explainPage() const { return m_explain; }
  RegistersPage *registersPage() const { return m_registers; }
  SettingsPage *settingsPage() const { return m_settings; }
  BadgeStrip *badgeStrip() const { return m_badgeStrip; }
  Explorer::TraceRecorder *recorder() const { return m_recorder; }
  const Explorer::TraceReplay *replay() const { return m_replay.get(); }
  qint64 cycle() const { return m_cycle; }
  qint64 selectedInstance() const { return m_selected; }
  QString toastText() const;

  /// Geometry of the navigation tabs and of the main touch targets of the
  /// visible page (shell coordinates = CSS pixels of the page), the current
  /// page and its scroll position. Published to the web page as
  /// window.osxMobileLayout (JSON) in the WebAssembly build, for the phone
  /// browser test (web/smoke_mobile.py).
  QJsonObject describeLayout() const;

public slots:
  void setCycle(qint64 cycle);
  void selectInstance(qint64 id);
  /// Rebuilds the replay if the recorder's trace changed.
  void refreshReplay();
  void showToast(const QString &text);

signals:
  /// Native builds: the user asked for the desktop main window.
  void switchToDesktopRequested();

protected:
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void applyStyle();
  void retranslate();
  void updateHeader();
  void updateViews();
  void updateMissionContext();
  void publishLayout();
  ScrollPage *scrollPageOf(int page) const;

  Explorer::TraceRecorder *m_recorder = nullptr;
  std::unique_ptr<Explorer::TraceReplay> m_replay;
  quint64 m_revision = ~quint64(0);
  qint64 m_cycle = 0;
  qint64 m_selected = -1;

  QWidget *m_header = nullptr;
  QBoxLayout *m_headerLayout = nullptr;
  QLabel *m_title = nullptr;
  BadgeStrip *m_badgeStrip = nullptr;
  QStackedWidget *m_stack = nullptr;
  NavBar *m_nav = nullptr;
  QLabel *m_toast = nullptr;
  QTimer m_toastTimer;
  QTimer m_publishTimer;
  QTimer m_refreshTimer;

  MissionsPage *m_missions = nullptr;
  RunPage *m_run = nullptr;
  PipelinePage *m_pipeline = nullptr;
  ExplainPage *m_explain = nullptr;
  RegistersPage *m_registers = nullptr;
  SettingsPage *m_settings = nullptr;
};

/// Shows the phone user interface (full screen in the browser) and runs the
/// application. Called by main() when currentUiMode() is UiMode::Mobile.
int runMobileShell(QApplication &app);

/// Font and other application-wide settings of the phone user interface.
void prepareApplication(QApplication &app);

} // namespace Mobile
} // namespace Ripes
