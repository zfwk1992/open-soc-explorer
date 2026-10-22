// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QFontDatabase>
#include <QMenu>
#include <QPalette>
#include <QStyleHints>

#include "ripessettings.h"

// Q_INIT_RESOURCE must be used outside of any namespace.
static void initThemeResources() { Q_INIT_RESOURCE(theme); }

namespace Ripes {
namespace Theme {

namespace {

Tokens darkTokens() {
  Tokens t;
  t.dark = true;
  t.window = QColor("#0d1220");
  t.base = QColor("#10172a");
  t.alternate = QColor("#141c31");
  t.panel = QColor("#131a2d");
  t.card = QColor("#182239");
  t.raised = QColor("#1f2b47");
  t.border = QColor("#2a3858");
  t.borderStrong = QColor("#3d5080");
  t.text = QColor("#e7edff");
  t.muted = QColor("#97a4c5");
  t.accent = QColor("#38c6ff");
  t.accentText = QColor("#04121d");
  t.accent2 = QColor("#a07cff");
  t.ok = QColor("#3ddc97");
  t.warn = QColor("#ffb547");
  t.bad = QColor("#ff5d7d");
  t.gold = QColor("#ffd166");
  t.info = QColor("#5aa2ff");
  return t;
}

Tokens lightTokens() {
  Tokens t;
  t.dark = false;
  t.window = QColor("#e9eef8");
  t.base = QColor("#ffffff");
  t.alternate = QColor("#f4f7fd");
  t.panel = QColor("#f7f9ff");
  t.card = QColor("#ffffff");
  t.raised = QColor("#ffffff");
  t.border = QColor("#d3dbec");
  t.borderStrong = QColor("#aebbd9");
  t.text = QColor("#18213b");
  t.muted = QColor("#56627f");
  t.accent = QColor("#0b74c9");
  t.accentText = QColor("#ffffff");
  t.accent2 = QColor("#6a4ae0");
  t.ok = QColor("#12804c");
  t.warn = QColor("#a96400");
  t.bad = QColor("#c9274a");
  t.gold = QColor("#e0a100");
  t.info = QColor("#2f6fe0");
  return t;
}

/// Classic theme: colors derived from the (platform) application palette.
Tokens classicTokens() {
  const QPalette p = QGuiApplication::palette();
  Tokens t;
  t.dark = p.color(QPalette::Window).lightness() < 128;
  t.window = p.color(QPalette::Window);
  t.base = p.color(QPalette::Base);
  t.alternate = p.color(QPalette::AlternateBase);
  t.panel = p.color(QPalette::Window);
  t.card = p.color(QPalette::Base);
  t.raised = p.color(QPalette::Button);
  t.border = p.color(QPalette::Mid);
  t.borderStrong = p.color(QPalette::Dark);
  t.text = p.color(QPalette::Text);
  t.muted = mix(p.color(QPalette::Text), p.color(QPalette::Base), 0.42);
  t.accent = p.color(QPalette::Highlight);
  t.accentText = p.color(QPalette::HighlightedText);
  t.accent2 = QColor(t.dark ? "#a58bff" : "#5b3fc4");
  t.ok = QColor(t.dark ? "#7bd88f" : "#1b7f3a");
  t.warn = QColor(t.dark ? "#f5b041" : "#a86400");
  t.bad = QColor(t.dark ? "#ff8a80" : "#b3261e");
  t.gold = QColor(t.dark ? "#f1c40f" : "#c48a00");
  t.info = QColor(t.dark ? "#7fb2ff" : "#1f5fbf");
  return t;
}

struct State {
  Kind kind = Kind::Game;
  Tokens game;
  Tokens classic;
  qint64 classicPaletteKey = -1;
  bool installed = false;
};

State &state() {
  static State s;
  return s;
}

/// True if the game theme should use its dark variant: an explicit Dark
/// color scheme, or System when the platform does not report Light.
bool wantDark() {
  switch (static_cast<ColorScheme>(
      RipesSettings::value(RIPES_SETTING_COLORSCHEME).toInt())) {
  case ColorScheme::Light:
    return false;
  case ColorScheme::Dark:
    return true;
  case ColorScheme::System:
    break;
  }
  return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

QPalette paletteFor(const Tokens &t) {
  QPalette pal;
  for (auto group : {QPalette::Active, QPalette::Inactive}) {
    pal.setColor(group, QPalette::Window, t.window);
    pal.setColor(group, QPalette::WindowText, t.text);
    pal.setColor(group, QPalette::Base, t.base);
    pal.setColor(group, QPalette::AlternateBase, t.alternate);
    pal.setColor(group, QPalette::ToolTipBase, t.raised);
    pal.setColor(group, QPalette::ToolTipText, t.text);
    pal.setColor(group, QPalette::PlaceholderText, t.muted);
    pal.setColor(group, QPalette::Text, t.text);
    pal.setColor(group, QPalette::Button, t.raised);
    pal.setColor(group, QPalette::ButtonText, t.text);
    pal.setColor(group, QPalette::BrightText, t.bad);
    pal.setColor(group, QPalette::Light, t.raised.lighter(t.dark ? 140 : 102));
    pal.setColor(group, QPalette::Midlight, mix(t.raised, t.border, 0.5));
    pal.setColor(group, QPalette::Mid, t.border);
    pal.setColor(group, QPalette::Dark, t.borderStrong);
    pal.setColor(group, QPalette::Shadow, t.dark ? QColor(0, 0, 0) : t.muted);
    pal.setColor(group, QPalette::Highlight,
                 t.dark ? t.accent.darker(125) : t.accent);
    pal.setColor(group, QPalette::HighlightedText,
                 t.dark ? QColor("#ffffff") : t.accentText);
    pal.setColor(group, QPalette::Link, t.accent);
    pal.setColor(group, QPalette::LinkVisited, t.accent2);
    pal.setColor(group, QPalette::Accent, t.accent);
  }
  const QColor disabledText = withAlpha(t.muted, 150);
  pal.setColor(QPalette::Disabled, QPalette::Window, t.window);
  pal.setColor(QPalette::Disabled, QPalette::Base, t.window);
  pal.setColor(QPalette::Disabled, QPalette::Button, t.panel);
  pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
  pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
  pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
  pal.setColor(QPalette::Disabled, QPalette::Highlight, t.border);
  pal.setColor(QPalette::Disabled, QPalette::HighlightedText, t.muted);
  pal.setColor(QPalette::Disabled, QPalette::Light, t.panel);
  pal.setColor(QPalette::Disabled, QPalette::Mid, t.border);
  pal.setColor(QPalette::Disabled, QPalette::Dark, t.border);
  return pal;
}

QString rgba(const QColor &c) {
  return QString("rgba(%1,%2,%3,%4)")
      .arg(c.red())
      .arg(c.green())
      .arg(c.blue())
      .arg(c.alpha());
}

} // namespace

Notifier *Notifier::get() {
  static auto *instance = new Notifier(QCoreApplication::instance());
  return instance;
}

Kind kind() { return state().kind; }

const Tokens &tokens() {
  auto &s = state();
  if (s.kind == Kind::Game)
    return s.game;
  const qint64 key = QGuiApplication::palette().cacheKey();
  if (key != s.classicPaletteKey) {
    s.classic = classicTokens();
    s.classicPaletteKey = key;
  }
  return s.classic;
}

bool reducedMotion() {
  return RipesSettings::value(RIPES_SETTING_OSX_REDUCED_MOTION).toBool();
}

bool animationsEnabled() { return isGame() && !reducedMotion(); }

void apply() {
  auto &s = state();
  s.kind = RipesSettings::value(RIPES_SETTING_OSX_THEME).toInt() == 1
               ? Kind::Classic
               : Kind::Game;
  // The palette first: applying a style sheet derives the palettes of the
  // styled widgets from the application palette at that moment.
  if (s.kind == Kind::Game) {
    s.game = wantDark() ? darkTokens() : lightTokens();
    QApplication::setPalette(paletteFor(s.game));
    qApp->setStyleSheet(styleSheet(s.game));
  } else {
    // A palette without explicitly set roles restores the platform palette,
    // which then follows the color scheme again.
    QApplication::setPalette(QPalette());
    qApp->setStyleSheet(QString());
  }
  s.classicPaletteKey = -1;
  emit Notifier::get()->changed();
}

void install(QMenu *viewMenu) {
  auto &s = state();
  if (!s.installed) {
    s.installed = true;
    initThemeResources();
    auto *notifier = Notifier::get();
    // Queued: the color scheme observer of MainWindow applies the scheme
    // first; system scheme changes update the platform palette first.
    QObject::connect(
        RipesSettings::getObserver(RIPES_SETTING_OSX_THEME),
        &SettingObserver::modified, notifier, [] { apply(); },
        Qt::QueuedConnection);
    QObject::connect(
        RipesSettings::getObserver(RIPES_SETTING_COLORSCHEME),
        &SettingObserver::modified, notifier, [] { apply(); },
        Qt::QueuedConnection);
    QObject::connect(
        QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
        notifier, [] { apply(); }, Qt::QueuedConnection);
    QObject::connect(
        RipesSettings::getObserver(RIPES_SETTING_OSX_REDUCED_MOTION),
        &SettingObserver::modified, notifier, &Notifier::changed);
    apply();
  }
  if (!viewMenu)
    return;

  auto *themeMenu = viewMenu->addMenu("Theme");
  auto *group = new QActionGroup(themeMenu);
  auto *game = themeMenu->addAction("Game (Open SoC Explorer)");
  auto *classic = themeMenu->addAction("Classic (Ripes)");
  for (auto *a : {game, classic}) {
    a->setCheckable(true);
    group->addAction(a);
  }
  game->setToolTip("Open SoC Explorer's game-like theme");
  classic->setToolTip("The original Ripes look");
  auto syncChecks = [game, classic] {
    game->setChecked(kind() == Kind::Game);
    classic->setChecked(kind() == Kind::Classic);
  };
  syncChecks();
  QObject::connect(Notifier::get(), &Notifier::changed, themeMenu, syncChecks);
  QObject::connect(game, &QAction::triggered, themeMenu,
                   [] { RipesSettings::setValue(RIPES_SETTING_OSX_THEME, 0); });
  QObject::connect(classic, &QAction::triggered, themeMenu,
                   [] { RipesSettings::setValue(RIPES_SETTING_OSX_THEME, 1); });

  auto *motion = viewMenu->addAction("Reduce motion");
  motion->setCheckable(true);
  motion->setChecked(reducedMotion());
  motion->setToolTip("Turn off the short transitions of the game theme");
  QObject::connect(motion, &QAction::toggled, motion, [](bool on) {
    RipesSettings::setValue(RIPES_SETTING_OSX_REDUCED_MOTION, on);
  });
  viewMenu->addSeparator();
}

// ============================================================================
// Semantic colors
// ============================================================================

QColor mix(const QColor &a, const QColor &b, qreal t) {
  t = std::clamp(t, 0.0, 1.0);
  return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                          a.greenF() * (1 - t) + b.greenF() * t,
                          a.blueF() * (1 - t) + b.blueF() * t,
                          a.alphaF() * (1 - t) + b.alphaF() * t);
}

QColor withAlpha(QColor c, int alpha) {
  c.setAlpha(alpha);
  return c;
}

QColor textOn(const QColor &background) {
  const qreal l = 0.2126 * background.redF() + 0.7152 * background.greenF() +
                  0.0722 * background.blueF();
  return l > 0.55 ? QColor("#0b1020") : QColor("#ffffff");
}

QColor stageColor(int index) {
  // Hues avoid amber (held) and red (wrong path / flush).
  static const int hues[] = {212, 266, 318, 150, 188, 95, 238, 292};
  const int hue = hues[std::abs(index) % 8];
  const auto &t = tokens();
  return t.dark ? QColor::fromHsl(hue, 190, 150)
                : QColor::fromHsl(hue, 170, 105);
}

QColor stageFill(int index) {
  const auto &t = tokens();
  return mix(t.base, stageColor(index), t.dark ? 0.36 : 0.2);
}

QColor roleColor(const QString &role) {
  int hue = -1;
  if (role == "cpu")
    hue = 214;
  else if (role == "cache")
    hue = 150;
  else if (role == "prefetcher")
    hue = 170;
  else if (role == "interconnect")
    hue = 276;
  else if (role == "memory-controller")
    hue = 38;
  else if (role == "memory")
    hue = 24;
  else if (role == "peripheral")
    hue = 190;
  else if (role == "dma")
    hue = 330;
  else if (role == "accelerator")
    hue = 300;
  const auto &t = tokens();
  if (hue < 0)
    return t.muted;
  return t.dark ? QColor::fromHsl(hue, 170, 140)
                : QColor::fromHsl(hue, 160, 100);
}

QColor fidelityColor(const QString &fidelity) {
  const auto &t = tokens();
  if (fidelity == "educational-model")
    return t.warn;
  if (fidelity == "functional-iss")
    return t.info;
  if (fidelity == "cycle-level-simulator")
    return t.ok;
  if (fidelity.startsWith("rtl"))
    return t.gold;
  return t.muted;
}

QColor observabilityColor(const QString &o) {
  const auto &t = tokens();
  if (o == "direct")
    return t.ok;
  if (o == "model-derived")
    return t.info;
  if (o == "state-diff")
    return t.accent2;
  if (o == "inferred")
    return t.warn;
  if (o == "isa-decoded")
    return t.accent;
  return t.muted;
}

QFont monoFont(qreal pointSize, bool bold) {
  QFont f("Inconsolata");
  f.setStyleHint(QFont::Monospace);
  if (pointSize > 0)
    f.setPointSizeF(pointSize);
  f.setBold(bold);
  return f;
}

// ============================================================================
// Style sheet
// ============================================================================

QString styleSheet(const Tokens &t) {
  const QString arrows = t.dark ? "dark" : "light";
  const QColor accentDark = t.accent.darker(t.dark ? 150 : 130);
  const QColor buttonTop = t.dark ? t.raised.lighter(118) : QColor("#ffffff");
  const QColor buttonBottom = t.dark ? t.raised : QColor("#eef2fb");
  QString qss = R"(
QMainWindow::separator { background: @border; width: 1px; height: 1px; }
QToolBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @raised, stop:1 @panel);
  border: none; border-bottom: 1px solid @border; padding: 3px 6px; spacing: 3px; }
QToolBar::separator { background: @border; width: 1px; margin: 5px 6px; }
QToolBar QToolButton { background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 4px; }
QToolBar QToolButton:hover { background: @accentA30; border-color: @borderStrong; }
QToolBar QToolButton:pressed { background: @accentA60; }
QToolBar QToolButton:checked { background: @accentA40; border-color: @accent; }
QToolBar QLabel { color: @muted; font-weight: 600; }
QMenuBar { background: @window; border-bottom: 1px solid @border; padding: 2px; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: @raised; }
QMenu { background: @card; border: 1px solid @borderStrong; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 6px 24px 6px 28px; border-radius: 6px; }
QMenu::item:selected { background: @accentA50; color: @text; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @buttonTop, stop:1 @buttonBottom);
  border: 1px solid @borderStrong; border-bottom: 2px solid @borderStrong; border-radius: 9px;
  padding: 5px 14px; font-weight: 600; color: @text; }
QPushButton:hover { border-color: @accent; }
QPushButton:pressed { background: @card; border-bottom-width: 1px; padding-top: 6px; }
QPushButton:checked { background: @accentA40; border-color: @accent; }
QPushButton:disabled { color: @muted; background: @panel; border-color: @border; }
QPushButton[osxRole="primary"] { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @accentLight, stop:1 @accent);
  color: @accentText; border: 1px solid @accentDark; border-bottom: 2px solid @accentDark; }
QPushButton[osxRole="primary"]:hover { background: @accentLight; }
QPushButton[osxRole="gold"] { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @goldLight, stop:1 @gold);
  color: #2a1d00; border: 1px solid @goldDark; border-bottom: 2px solid @goldDark; }
QPushButton[osxRole="gold"]:hover { background: @goldLight; }
QTabWidget::pane { border: 1px solid @border; border-radius: 10px; background: @panel; top: -1px; }
QTabBar::tab { background: transparent; color: @muted; border: 1px solid transparent; border-bottom: none; min-width: 40px;
  border-top-left-radius: 9px; border-top-right-radius: 9px; padding: 6px 10px; margin-right: 1px; font-weight: 600; }
QTabBar::tab:selected { background: @panel; color: @text; border-color: @border; border-top: 2px solid @accent; }
QTabBar::tab:hover:!selected { background: @card; color: @text; }
QGroupBox { background: @panel; border: 1px solid @border; border-radius: 12px; margin-top: 16px;
  padding: 10px 6px 6px 6px; font-weight: 700; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 14px; top: 1px;
  padding: 0 4px; color: @accent; }
QHeaderView { background: @card; border: none; }
QHeaderView::section { background: @card; border: none; border-right: 1px solid @border;
  border-bottom: 1px solid @border; padding: 3px 6px; font-weight: 600; }
QTableCornerButton::section { background: @card; border: none; }
QAbstractItemView { background: @base; alternate-background-color: @alternate; border: 1px solid @border;
  gridline-color: @grid; selection-background-color: @accentA60; selection-color: @text; outline: 0; }
QListWidget#missionList { background: transparent; border: none; }
QTextBrowser, QTextEdit, QPlainTextEdit { background: @base; border: 1px solid @border;
  selection-background-color: @accentA90; }
QGraphicsView { border: 1px solid @border; }
QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar::handle:vertical { background: @scroll; border-radius: 4px; min-height: 28px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle:horizontal { background: @scroll; border-radius: 4px; min-width: 28px; margin: 2px; }
QScrollBar::handle:hover { background: @accent; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QSlider::groove:horizontal { height: 6px; background: @base; border: 1px solid @border; border-radius: 3px; }
QSlider::sub-page:horizontal { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent2, stop:1 @accent);
  border-radius: 3px; }
QSlider::handle:horizontal { background: qradialgradient(cx:0.5, cy:0.5, radius:0.6, fx:0.4, fy:0.35,
  stop:0 #ffffff, stop:0.5 @accent, stop:1 @accentDark); border: 2px solid @handleRing;
  width: 14px; height: 14px; margin: -6px 0; border-radius: 9px; }
QLineEdit, QAbstractSpinBox, QComboBox { background: @base; border: 1px solid @borderStrong; border-radius: 7px;
  padding: 2px 6px; selection-background-color: @accentA90; }
QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: @accent; }
QComboBox { padding-right: 22px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 20px; border: none; }
QComboBox::down-arrow { image: url(:/theme/chevron-down-@arrows.svg); width: 10px; height: 10px; }
QComboBox QAbstractItemView { background: @card; border: 1px solid @borderStrong; selection-background-color: @accentA60; }
QAbstractSpinBox { padding-right: 20px; }
QAbstractSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 18px;
  border: none; border-top-right-radius: 7px; background: transparent; }
QAbstractSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 18px;
  border: none; border-bottom-right-radius: 7px; background: transparent; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: @accentA40; }
QAbstractSpinBox::up-arrow { image: url(:/theme/chevron-up-@arrows.svg); width: 9px; height: 9px; }
QAbstractSpinBox::down-arrow { image: url(:/theme/chevron-down-@arrows.svg); width: 9px; height: 9px; }
QToolTip { background: @card; color: @text; border: 1px solid @accent; border-radius: 6px; padding: 4px 6px; }
QStatusBar { background: @panel; color: @muted; border-top: 1px solid @border; }
QStatusBar QLabel { color: @muted; }
QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: @accentA50; }
QFrame#explorerControls { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @raised, stop:1 @card);
  border: 1px solid @border; border-radius: 14px; }
QToolButton#explorerControl { background: @base; border: 1px solid @borderStrong; border-radius: 15px;
  min-width: 30px; min-height: 30px; max-width: 30px; max-height: 30px; padding: 0; }
QToolButton#explorerControl:hover { border-color: @accent; background: @accentA30; }
QToolButton#explorerControl:pressed { background: @accentA60; }
QToolButton#explorerPlay { background: qradialgradient(cx:0.5, cy:0.4, radius:0.7, fx:0.45, fy:0.3,
  stop:0 @accentLight, stop:1 @accent); border: 2px solid @accentDark; border-radius: 19px;
  min-width: 34px; min-height: 34px; max-width: 34px; max-height: 34px; padding: 0; }
QToolButton#explorerPlay:hover { border-color: @text; }
QToolButton#explorerPlay:checked { background: qradialgradient(cx:0.5, cy:0.4, radius:0.7, fx:0.45, fy:0.3,
  stop:0 @goldLight, stop:1 @gold); border-color: @goldDark; }
QLabel#explorerCycle { background: @base; color: @accent; border: 1px solid @borderStrong; border-radius: 9px;
  padding: 3px 10px; }
QToolButton#explorerFollow { background: @base; border: 1px solid @borderStrong; border-radius: 13px;
  padding: 4px 10px; font-weight: 600; }
QToolButton#explorerFollow:checked { background: @okA40; border-color: @ok; }
QToolButton#explorerFollow:disabled { color: @muted; }
QFrame#missionCard { background: @card; border: 1px solid @border; border-radius: 12px; }
QFrame#missionStep { background: @card; border: 1px solid @border; border-radius: 12px; }
QFrame#missionStep[state="done"] { background: @okTint; border: 1px solid @okA150; }
QFrame#missionStep[state="current"] { background: @accentTint; border: 2px solid @accent; }
QLabel#missionStepStatus { background: @base; color: @muted; border: 1px solid @borderStrong; border-radius: 15px;
  min-width: 30px; max-width: 30px; min-height: 30px; max-height: 30px; }
QLabel#missionStepStatus[state="done"] { background: @ok; color: @onOk; border: 1px solid @ok; }
QLabel#missionStepStatus[state="current"] { background: @accentA40; color: @accent; border: 2px solid @accent; }
QLabel#missionLockNote { background: @warnTint; border: 1px solid @warn; border-radius: 10px; padding: 8px 12px; }
QLabel#missionOverline { color: @accent; }
QProgressBar { border: 1px solid @border; border-radius: 5px; background: @base; text-align: center; }
QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent2, stop:1 @accent); border-radius: 4px; }
)";
  // Longer names first: a name must not be a prefix of a later one.
  const std::vector<std::pair<QString, QString>> vars = {
      {"@accentTint", mix(t.card, t.accent, t.dark ? 0.08 : 0.05).name()},
      {"@accentA30", rgba(withAlpha(t.accent, 40))},
      {"@accentA40", rgba(withAlpha(t.accent, 55))},
      {"@accentA50", rgba(withAlpha(t.accent, 70))},
      {"@accentA60", rgba(withAlpha(t.accent, t.dark ? 80 : 60))},
      {"@accentA90", rgba(withAlpha(t.accent, 120))},
      {"@accentLight", t.accent.lighter(t.dark ? 120 : 130).name()},
      {"@accentDark", accentDark.name()},
      {"@accentText", t.accentText.name()},
      {"@accent2", t.accent2.name()},
      {"@accent", t.accent.name()},
      {"@okA40", rgba(withAlpha(t.ok, 50))},
      {"@okA150", rgba(withAlpha(t.ok, 150))},
      {"@okTint", mix(t.card, t.ok, t.dark ? 0.12 : 0.08).name()},
      {"@onOk", textOn(t.ok).name()},
      {"@ok", t.ok.name()},
      {"@warnTint", mix(t.card, t.warn, t.dark ? 0.14 : 0.1).name()},
      {"@warn", t.warn.name()},
      {"@goldLight", t.gold.lighter(118).name()},
      {"@goldDark", t.gold.darker(135).name()},
      {"@gold", t.gold.name()},
      {"@buttonTop", buttonTop.name()},
      {"@buttonBottom", buttonBottom.name()},
      {"@handleRing", (t.dark ? t.window : QColor("#ffffff")).name()},
      {"@borderStrong", t.borderStrong.name()},
      {"@border", t.border.name()},
      {"@grid", mix(t.base, t.border, 0.55).name()},
      {"@scroll", mix(t.base, t.borderStrong, 0.85).name()},
      {"@window", t.window.name()},
      {"@base", t.base.name()},
      {"@alternate", t.alternate.name()},
      {"@panel", t.panel.name()},
      {"@card", t.card.name()},
      {"@raised", t.raised.name()},
      {"@text", t.text.name()},
      {"@muted", t.muted.name()},
      {"@arrows", arrows},
  };
  for (const auto &[key, value] : vars)
    qss.replace(key, value);
  return qss;
}

} // namespace Theme
} // namespace Ripes
