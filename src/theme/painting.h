// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer: QPainter helpers shared by the Explorer, Missions
// and System views. All drawings are original; colors come from Theme.
#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QRectF>
#include <QString>

class QPainter;

namespace Ripes {
namespace Theme {

/// A rounded card. In the game theme it gets a soft drop shadow and a subtle
/// vertical gradient; in the classic theme it is flat.
void drawCard(QPainter &p, const QRectF &r, qreal radius, const QColor &fill,
              const QColor &border, qreal borderWidth = 1.0,
              bool shadow = true);
/// A soft outer glow around a rounded rectangle (game theme only).
void drawGlow(QPainter &p, const QRectF &r, qreal radius, const QColor &color,
              int spread = 6);
/// Diagonal stripes clipped to a rounded rectangle (held / stalled).
void drawStripes(QPainter &p, const QRectF &r, qreal radius,
                 const QColor &color, qreal spacing = 7);

/// A five-pointed star.
void drawStar(QPainter &p, const QPointF &center, qreal outerRadius,
              bool filled);
/// @p total stars in a row starting at @p left (vertical center @p cy), the
/// first @p filled of them filled. Returns the width used.
qreal drawStars(QPainter &p, qreal left, qreal cy, qreal size, int filled,
                int total);

/// A rounded progress bar; @p fraction is clamped to [0, 1].
void drawProgressBar(QPainter &p, const QRectF &r, qreal fraction,
                     const QColor &fill);

QSizeF pillSize(const QString &text, const QFont &font);
/// A rounded label ("chip"). @p solid fills it with @p color; otherwise it is
/// tinted. Returns the rectangle drawn.
QRectF drawPill(QPainter &p, const QPointF &topLeft, const QString &text,
                const QColor &color, const QFont &font, bool solid = false);

/// A pointy-top hexagon.
void drawHexagon(QPainter &p, const QPointF &center, qreal radius,
                 const QColor &fill, const QColor &border, qreal borderWidth);

/// Pictogram for a System view component role ("cpu", "cache", ...).
void drawRoleIcon(QPainter &p, const QRectF &r, const QString &role,
                  const QColor &color);

enum class Glyph {
  First,
  Previous,
  Play,
  Pause,
  Next,
  Last,
  Check,
  Cross,
  Lock,
  Trophy,
  Load,
  Chip,
  Explore,
  Hint,
  Bolt,
  Star,
  Follow,
};
/// Simple pictograms for buttons and badges.
void drawGlyph(QPainter &p, const QRectF &r, Glyph g, const QColor &color);
/// An icon with the glyph drawn in @p color.
QIcon glyphIcon(Glyph g, const QColor &color, int size = 20);

/// Small caps style font for "overline" labels (e.g. "ENGINE").
QFont overlineFont(const QFont &base, qreal scale = 0.78);

} // namespace Theme
} // namespace Ripes
