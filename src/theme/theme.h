// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QColor>
#include <QFont>
#include <QObject>

class QMenu;

namespace Ripes {
namespace Theme {

/// GUI themes (setting RIPES_SETTING_OSX_THEME).
enum class Kind {
  /// Open SoC Explorer's game-like theme: own palette (dark or light,
  /// following the color scheme setting), style sheet and richer painting in
  /// the Explorer, Missions and System views.
  Game = 0,
  /// The upstream Ripes look: platform palette, no style sheet; our views
  /// use flat, palette-derived colors.
  Classic = 1
};

/// Colors of the active theme. In the game theme these are the theme's own
/// colors; in the classic theme they are derived from the application palette.
struct Tokens {
  bool dark = false;
  QColor window, base, alternate, panel, card, raised;
  QColor border, borderStrong, text, muted;
  QColor accent, accentText, accent2;
  QColor ok, warn, bad, gold, info;
};

/// Applies the persisted theme to the application and adds the "Theme" and
/// "Reduce motion" entries to @p viewMenu (may be nullptr). Called once by
/// MainWindow; re-applies itself when the theme, color scheme or system
/// color scheme changes.
void install(QMenu *viewMenu);
/// Applies the persisted theme (palette and style sheet) now.
void apply();

Kind kind();
inline bool isGame() { return kind() == Kind::Game; }
const Tokens &tokens();

/// True if the user asked for reduced motion
/// (RIPES_SETTING_OSX_REDUCED_MOTION).
bool reducedMotion();
/// True if short decorative animations may run: game theme and reduced
/// motion off. Views must additionally skip animations while the processor
/// runs or when cycles change faster than the animation.
bool animationsEnabled();
/// Duration of view transitions in ms (≤ 300).
constexpr int kTransitionMs = 220;

/// Emits changed() after the theme or the reduced-motion option changed and
/// the application palette and style sheet were updated.
class Notifier : public QObject {
  Q_OBJECT
public:
  static Notifier *get();
signals:
  void changed();

private:
  using QObject::QObject;
};

// Semantic colors, valid for the active theme.
/// Color of pipeline stage number @p index (index within its lane).
QColor stageColor(int index);
/// Fill of a timeline cell / instruction card in stage @p index.
QColor stageFill(int index);
/// Color of a System view component role ("cpu", "cache", ...).
QColor roleColor(const QString &role);
/// Color of a fidelity class ("educational-model", ...).
QColor fidelityColor(const QString &fidelity);
/// Color of an observability level ("direct", "inferred", ...).
QColor observabilityColor(const QString &observability);

QColor mix(const QColor &a, const QColor &b, qreal t);
QColor withAlpha(QColor c, int alpha);
/// Black or white, whichever reads better on @p background.
QColor textOn(const QColor &background);

/// The bundled Inconsolata font (falls back to the system monospace font).
QFont monoFont(qreal pointSize = -1, bool bold = false);

/// The application style sheet of the game theme for @p t.
QString styleSheet(const Tokens &t);

} // namespace Theme
} // namespace Ripes
