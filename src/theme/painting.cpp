// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "painting.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>

#include "theme.h"

namespace Ripes {
namespace Theme {

namespace {
constexpr qreal kPi = 3.14159265358979323846;

QRectF square(const QRectF &r, qreal inset = 0) {
  const qreal s = std::min(r.width(), r.height()) * (1 - 2 * inset);
  return QRectF(r.center().x() - s / 2, r.center().y() - s / 2, s, s);
}

QPolygonF triangle(const QRectF &r, bool right) {
  if (right)
    return QPolygonF(
        {r.topLeft(), QPointF(r.right(), r.center().y()), r.bottomLeft()});
  return QPolygonF(
      {r.topRight(), QPointF(r.left(), r.center().y()), r.bottomRight()});
}
} // namespace

void drawCard(QPainter &p, const QRectF &r, qreal radius, const QColor &fill,
              const QColor &border, qreal borderWidth, bool shadow) {
  const auto &t = tokens();
  const bool game = isGame();
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  if (shadow && game) {
    QColor sc = t.dark ? QColor(0, 0, 0) : QColor(30, 45, 90);
    const int strength = t.dark ? 90 : 34;
    p.setPen(Qt::NoPen);
    for (int i = 3; i >= 1; --i) {
      sc.setAlpha(strength / (i + 1));
      p.setBrush(sc);
      p.drawRoundedRect(r.adjusted(-i * 0.5, i * 0.9, i * 0.5, i * 1.6),
                        radius + i, radius + i);
    }
  }
  QBrush brush(fill);
  if (game) {
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, fill.lighter(t.dark ? 114 : 101));
    g.setColorAt(1, fill);
    brush = QBrush(g);
  }
  p.setBrush(brush);
  if (border.alpha() > 0 && borderWidth > 0)
    p.setPen(QPen(border, borderWidth));
  else
    p.setPen(Qt::NoPen);
  const qreal h = borderWidth / 2;
  p.drawRoundedRect(r.adjusted(h, h, -h, -h), radius, radius);
  p.restore();
}

void drawGlow(QPainter &p, const QRectF &r, qreal radius, const QColor &color,
              int spread) {
  if (!isGame() || spread <= 0)
    return;
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(Qt::NoBrush);
  for (int i = spread; i >= 1; --i) {
    QColor c = color;
    c.setAlphaF(color.alphaF() * 0.42 * (1.0 - qreal(i - 1) / spread));
    p.setPen(QPen(c, 2));
    p.drawRoundedRect(r.adjusted(-i, -i, i, i), radius + i, radius + i);
  }
  p.restore();
}

void drawStripes(QPainter &p, const QRectF &r, qreal radius,
                 const QColor &color, qreal spacing) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  QPainterPath clip;
  clip.addRoundedRect(r, radius, radius);
  p.setClipPath(clip, Qt::IntersectClip);
  p.setPen(QPen(color, spacing * 0.36));
  for (qreal x = r.left() - r.height(); x < r.right(); x += spacing)
    p.drawLine(QPointF(x, r.bottom()), QPointF(x + r.height(), r.top()));
  p.restore();
}

void drawStar(QPainter &p, const QPointF &c, qreal R, bool filled) {
  const auto &t = tokens();
  QPainterPath path;
  for (int i = 0; i < 10; ++i) {
    const qreal rr = (i % 2 == 0) ? R : R * 0.47;
    const qreal a = -kPi / 2 + i * kPi / 5;
    const QPointF pt(c.x() + rr * std::cos(a), c.y() + rr * std::sin(a));
    if (i == 0)
      path.moveTo(pt);
    else
      path.lineTo(pt);
  }
  path.closeSubpath();
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  const qreal w = std::max(1.0, R * 0.12);
  if (filled) {
    if (isGame()) {
      QRadialGradient g(c - QPointF(R * 0.3, R * 0.35), R * 1.5);
      g.setColorAt(0, t.gold.lighter(135));
      g.setColorAt(1, t.gold);
      p.setBrush(g);
    } else {
      p.setBrush(t.gold);
    }
    p.setPen(QPen(t.gold.darker(t.dark ? 170 : 140), w, Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
  } else {
    p.setBrush(withAlpha(t.muted, t.dark ? 36 : 26));
    p.setPen(QPen(withAlpha(t.muted, 170), w, Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin));
  }
  p.drawPath(path);
  p.restore();
}

qreal drawStars(QPainter &p, qreal left, qreal cy, qreal size, int filled,
                int total) {
  const qreal gap = size * 0.2;
  for (int i = 0; i < total; ++i)
    drawStar(p, QPointF(left + size / 2 + i * (size + gap), cy), size / 2,
             i < filled);
  return total * size + std::max(0, total - 1) * gap;
}

void drawProgressBar(QPainter &p, const QRectF &r, qreal fraction,
                     const QColor &fill) {
  const auto &t = tokens();
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  const qreal rad = r.height() / 2;
  QPainterPath track;
  track.addRoundedRect(r, rad, rad);
  p.fillPath(track, mix(t.base, t.border, t.dark ? 0.75 : 0.9));
  const qreal f = std::clamp(fraction, 0.0, 1.0);
  if (f > 0) {
    p.setClipPath(track);
    const QRectF fr(r.left(), r.top(), r.width() * f, r.height());
    if (isGame()) {
      QLinearGradient g(r.topLeft(), r.topRight());
      g.setColorAt(0, fill.darker(t.dark ? 125 : 110));
      g.setColorAt(1, fill.lighter(t.dark ? 115 : 108));
      p.fillRect(fr, g);
      p.fillRect(QRectF(fr.left(), fr.top(), fr.width(), fr.height() * 0.42),
                 QColor(255, 255, 255, t.dark ? 40 : 60));
    } else {
      p.fillRect(fr, fill);
    }
  }
  p.restore();
}

QSizeF pillSize(const QString &text, const QFont &font) {
  const QFontMetricsF fm(font);
  return QSizeF(fm.horizontalAdvance(text) + fm.height() * 0.95,
                fm.height() + 4);
}

QRectF drawPill(QPainter &p, const QPointF &topLeft, const QString &text,
                const QColor &color, const QFont &font, bool solid) {
  const auto &t = tokens();
  const QRectF r(topLeft, pillSize(text, font));
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  QColor fg;
  if (solid) {
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    fg = textOn(color);
  } else {
    p.setBrush(withAlpha(color, t.dark ? 46 : 28));
    p.setPen(QPen(withAlpha(color, t.dark ? 150 : 130), 1));
    fg = t.dark ? color.lighter(112) : color.darker(118);
  }
  p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), r.height() / 2,
                    r.height() / 2);
  p.setFont(font);
  p.setPen(fg);
  p.drawText(r, Qt::AlignCenter, text);
  p.restore();
  return r;
}

void drawHexagon(QPainter &p, const QPointF &c, qreal radius,
                 const QColor &fill, const QColor &border, qreal borderWidth) {
  QPolygonF poly;
  for (int i = 0; i < 6; ++i) {
    const qreal a = -kPi / 2 + i * kPi / 3;
    poly << QPointF(c.x() + radius * std::cos(a), c.y() + radius * std::sin(a));
  }
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(fill);
  if (borderWidth > 0)
    p.setPen(
        QPen(border, borderWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  else
    p.setPen(Qt::NoPen);
  p.drawPolygon(poly);
  p.restore();
}

void drawRoleIcon(QPainter &p, const QRectF &rect, const QString &role,
                  const QColor &color) {
  const QRectF r = square(rect, 0.04);
  const qreal s = r.width();
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  QPen pen(color, std::max(1.2, s * 0.075), Qt::SolidLine, Qt::RoundCap,
           Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  const QColor soft = withAlpha(color, 70);

  if (role == "cpu" || role == "chip") {
    const QRectF body = r.adjusted(s * 0.2, s * 0.2, -s * 0.2, -s * 0.2);
    p.setBrush(soft);
    p.drawRoundedRect(body, s * 0.06, s * 0.06);
    p.setBrush(color);
    p.drawRect(body.adjusted(s * 0.17, s * 0.17, -s * 0.17, -s * 0.17));
    for (int i = 0; i < 3; ++i) {
      const qreal o = body.left() + body.width() * (0.22 + 0.28 * i);
      const qreal v = body.top() + body.height() * (0.22 + 0.28 * i);
      p.drawLine(QPointF(o, r.top() + s * 0.06), QPointF(o, body.top()));
      p.drawLine(QPointF(o, body.bottom()), QPointF(o, r.bottom() - s * 0.06));
      p.drawLine(QPointF(r.left() + s * 0.06, v), QPointF(body.left(), v));
      p.drawLine(QPointF(body.right(), v), QPointF(r.right() - s * 0.06, v));
    }
  } else if (role == "cache") {
    for (int i = 0; i < 3; ++i) {
      const QRectF line(r.left() + s * 0.1, r.top() + s * (0.14 + 0.26 * i),
                        s * 0.8, s * 0.18);
      p.setBrush(soft);
      p.drawRoundedRect(line, s * 0.04, s * 0.04);
      p.setBrush(color);
      p.drawRect(QRectF(line.left(), line.top(), s * 0.18, line.height()));
    }
  } else if (role == "prefetcher") {
    for (int i = 0; i < 2; ++i) {
      const qreal x = r.left() + s * (0.18 + 0.3 * i);
      p.drawPolyline(QPolygonF({QPointF(x, r.top() + s * 0.2),
                                QPointF(x + s * 0.28, r.center().y()),
                                QPointF(x, r.bottom() - s * 0.2)}));
    }
  } else if (role == "interconnect") {
    for (int i = 0; i < 3; ++i) {
      const qreal x = r.left() + s * (0.2 + 0.3 * i);
      p.drawLine(QPointF(x, r.top() + s * 0.1),
                 QPointF(x, r.bottom() - s * 0.1));
    }
    for (int j = 0; j < 2; ++j) {
      const qreal y = r.top() + s * (0.35 + 0.3 * j);
      p.drawLine(QPointF(r.left() + s * 0.08, y),
                 QPointF(r.right() - s * 0.08, y));
    }
    p.setBrush(color);
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 2; ++j)
        if ((i + j) % 2 == 0)
          p.drawEllipse(QPointF(r.left() + s * (0.2 + 0.3 * i),
                                r.top() + s * (0.35 + 0.3 * j)),
                        s * 0.07, s * 0.07);
  } else if (role == "memory-controller") {
    p.setBrush(soft);
    p.drawRoundedRect(r.adjusted(s * 0.08, s * 0.16, -s * 0.08, -s * 0.16),
                      s * 0.08, s * 0.08);
    const qreal y1 = r.top() + s * 0.4, y2 = r.top() + s * 0.6;
    p.drawLine(QPointF(r.left() + s * 0.24, y1),
               QPointF(r.right() - s * 0.24, y1));
    p.drawPolyline(QPolygonF({QPointF(r.right() - s * 0.34, y1 - s * 0.08),
                              QPointF(r.right() - s * 0.24, y1),
                              QPointF(r.right() - s * 0.34, y1 + s * 0.08)}));
    p.drawLine(QPointF(r.left() + s * 0.24, y2),
               QPointF(r.right() - s * 0.24, y2));
    p.drawPolyline(QPolygonF({QPointF(r.left() + s * 0.34, y2 - s * 0.08),
                              QPointF(r.left() + s * 0.24, y2),
                              QPointF(r.left() + s * 0.34, y2 + s * 0.08)}));
  } else if (role == "memory") {
    const QRectF stick(r.left() + s * 0.04, r.top() + s * 0.26, s * 0.92,
                       s * 0.38);
    p.setBrush(soft);
    p.drawRoundedRect(stick, s * 0.04, s * 0.04);
    p.setBrush(color);
    for (int i = 0; i < 3; ++i)
      p.drawRect(QRectF(stick.left() + s * (0.1 + 0.27 * i),
                        stick.top() + s * 0.08, s * 0.18, s * 0.2));
    for (int i = 0; i < 6; ++i) {
      const qreal x = stick.left() + s * (0.1 + 0.145 * i);
      p.drawLine(QPointF(x, stick.bottom()),
                 QPointF(x, stick.bottom() + s * 0.14));
    }
  } else if (role == "peripheral") {
    p.setBrush(soft);
    p.drawEllipse(r.center(), s * 0.2, s * 0.2);
    for (int i = 0; i < 8; ++i) {
      const qreal a = i * kPi / 4;
      p.drawLine(QPointF(r.center().x() + s * 0.3 * std::cos(a),
                         r.center().y() + s * 0.3 * std::sin(a)),
                 QPointF(r.center().x() + s * 0.42 * std::cos(a),
                         r.center().y() + s * 0.42 * std::sin(a)));
    }
  } else if (role == "dma") {
    p.setBrush(soft);
    p.drawRect(
        QRectF(r.left() + s * 0.06, r.top() + s * 0.3, s * 0.26, s * 0.4));
    p.drawRect(
        QRectF(r.right() - s * 0.32, r.top() + s * 0.3, s * 0.26, s * 0.4));
    p.drawLine(QPointF(r.left() + s * 0.36, r.center().y()),
               QPointF(r.right() - s * 0.38, r.center().y()));
    p.drawPolyline(
        QPolygonF({QPointF(r.right() - s * 0.46, r.center().y() - s * 0.08),
                   QPointF(r.right() - s * 0.38, r.center().y()),
                   QPointF(r.right() - s * 0.46, r.center().y() + s * 0.08)}));
  } else if (role == "accelerator") {
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) {
        p.setBrush((i + j) % 2 ? soft : QBrush(color));
        p.drawRect(QRectF(r.left() + s * (0.12 + 0.27 * i),
                          r.top() + s * (0.12 + 0.27 * j), s * 0.2, s * 0.2));
      }
  } else {
    p.setBrush(soft);
    p.drawEllipse(r.center(), s * 0.32, s * 0.32);
  }
  p.restore();
}

void drawGlyph(QPainter &p, const QRectF &rect, Glyph g, const QColor &color) {
  const QRectF r = square(rect, 0.1);
  const qreal s = r.width();
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  QPen pen(color, std::max(1.4, s * 0.12), Qt::SolidLine, Qt::RoundCap,
           Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  const qreal bar = s * 0.13;
  auto fillPoly = [&](const QPolygonF &poly) {
    p.save();
    p.setPen(QPen(color, s * 0.06, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(color);
    p.drawPolygon(poly);
    p.restore();
  };
  auto fillBar = [&](qreal x) {
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(x, r.top() + s * 0.12, bar, s * 0.76), bar / 3,
                      bar / 3);
    p.restore();
  };
  const QRectF tri(r.left(), r.top() + s * 0.14, s * 0.42, s * 0.72);
  switch (g) {
  case Glyph::First:
    fillBar(r.left() + s * 0.06);
    fillPoly(triangle(tri.translated(s * 0.2, 0), false));
    fillPoly(triangle(tri.translated(s * 0.54, 0), false));
    break;
  case Glyph::Previous:
    fillBar(r.left() + s * 0.16);
    fillPoly(triangle(tri.translated(s * 0.36, 0), false));
    break;
  case Glyph::Play:
    fillPoly(QPolygonF({QPointF(r.left() + s * 0.24, r.top() + s * 0.1),
                        QPointF(r.right() - s * 0.12, r.center().y()),
                        QPointF(r.left() + s * 0.24, r.bottom() - s * 0.1)}));
    break;
  case Glyph::Pause:
    fillBar(r.left() + s * 0.24);
    fillBar(r.right() - s * 0.24 - bar);
    break;
  case Glyph::Next:
    fillPoly(triangle(tri.translated(s * 0.22, 0), true));
    fillBar(r.right() - s * 0.16 - bar);
    break;
  case Glyph::Last:
    fillPoly(triangle(tri.translated(s * 0.04, 0), true));
    fillPoly(triangle(tri.translated(s * 0.38, 0), true));
    fillBar(r.right() - s * 0.06 - bar);
    break;
  case Glyph::Check:
    p.drawPolyline(
        QPolygonF({QPointF(r.left() + s * 0.16, r.top() + s * 0.54),
                   QPointF(r.left() + s * 0.4, r.top() + s * 0.76),
                   QPointF(r.right() - s * 0.12, r.top() + s * 0.24)}));
    break;
  case Glyph::Cross:
    p.drawLine(QPointF(r.left() + s * 0.22, r.top() + s * 0.22),
               QPointF(r.right() - s * 0.22, r.bottom() - s * 0.22));
    p.drawLine(QPointF(r.right() - s * 0.22, r.top() + s * 0.22),
               QPointF(r.left() + s * 0.22, r.bottom() - s * 0.22));
    break;
  case Glyph::Lock: {
    const QRectF body(r.left() + s * 0.16, r.top() + s * 0.44, s * 0.68,
                      s * 0.48);
    p.drawArc(QRectF(r.left() + s * 0.3, r.top() + s * 0.08, s * 0.4, s * 0.62),
              0, 180 * 16);
    p.drawLine(QPointF(r.left() + s * 0.3, r.top() + s * 0.39),
               QPointF(r.left() + s * 0.3, body.top()));
    p.drawLine(QPointF(r.right() - s * 0.3, r.top() + s * 0.39),
               QPointF(r.right() - s * 0.3, body.top()));
    p.setBrush(color);
    p.drawRoundedRect(body, s * 0.08, s * 0.08);
    break;
  }
  case Glyph::Trophy: {
    QPainterPath cup;
    cup.moveTo(r.left() + s * 0.24, r.top() + s * 0.1);
    cup.lineTo(r.right() - s * 0.24, r.top() + s * 0.1);
    cup.cubicTo(r.right() - s * 0.24, r.top() + s * 0.5,
                r.center().x() + s * 0.1, r.top() + s * 0.6, r.center().x(),
                r.top() + s * 0.6);
    cup.cubicTo(r.center().x() - s * 0.1, r.top() + s * 0.6,
                r.left() + s * 0.24, r.top() + s * 0.5, r.left() + s * 0.24,
                r.top() + s * 0.1);
    p.setBrush(color);
    p.drawPath(cup);
    p.setBrush(Qt::NoBrush);
    p.drawArc(
        QRectF(r.left() + s * 0.06, r.top() + s * 0.14, s * 0.26, s * 0.26),
        90 * 16, 180 * 16);
    p.drawArc(
        QRectF(r.right() - s * 0.32, r.top() + s * 0.14, s * 0.26, s * 0.26),
        -90 * 16, 180 * 16);
    p.drawLine(QPointF(r.center().x(), r.top() + s * 0.6),
               QPointF(r.center().x(), r.top() + s * 0.8));
    p.drawLine(QPointF(r.left() + s * 0.3, r.top() + s * 0.86),
               QPointF(r.right() - s * 0.3, r.top() + s * 0.86));
    break;
  }
  case Glyph::Load:
    p.drawLine(QPointF(r.center().x(), r.top() + s * 0.08),
               QPointF(r.center().x(), r.top() + s * 0.6));
    p.drawPolyline(
        QPolygonF({QPointF(r.center().x() - s * 0.2, r.top() + s * 0.4),
                   QPointF(r.center().x(), r.top() + s * 0.62),
                   QPointF(r.center().x() + s * 0.2, r.top() + s * 0.4)}));
    p.drawPolyline(
        QPolygonF({QPointF(r.left() + s * 0.1, r.top() + s * 0.66),
                   QPointF(r.left() + s * 0.1, r.bottom() - s * 0.08),
                   QPointF(r.right() - s * 0.1, r.bottom() - s * 0.08),
                   QPointF(r.right() - s * 0.1, r.top() + s * 0.66)}));
    break;
  case Glyph::Chip:
    drawRoleIcon(p, r, "chip", color);
    break;
  case Glyph::Explore:
    p.drawEllipse(
        QRectF(r.left() + s * 0.1, r.top() + s * 0.1, s * 0.56, s * 0.56));
    p.drawLine(QPointF(r.left() + s * 0.6, r.top() + s * 0.6),
               QPointF(r.right() - s * 0.1, r.bottom() - s * 0.1));
    break;
  case Glyph::Hint:
    p.drawEllipse(
        QRectF(r.left() + s * 0.24, r.top() + s * 0.06, s * 0.52, s * 0.52));
    p.drawLine(QPointF(r.left() + s * 0.38, r.top() + s * 0.74),
               QPointF(r.right() - s * 0.38, r.top() + s * 0.74));
    p.drawLine(QPointF(r.left() + s * 0.42, r.top() + s * 0.9),
               QPointF(r.right() - s * 0.42, r.top() + s * 0.9));
    break;
  case Glyph::Bolt:
    fillPoly(QPolygonF({QPointF(r.left() + s * 0.58, r.top() + s * 0.04),
                        QPointF(r.left() + s * 0.2, r.top() + s * 0.56),
                        QPointF(r.left() + s * 0.48, r.top() + s * 0.56),
                        QPointF(r.left() + s * 0.4, r.bottom() - s * 0.04),
                        QPointF(r.right() - s * 0.2, r.top() + s * 0.42),
                        QPointF(r.left() + s * 0.52, r.top() + s * 0.42)}));
    break;
  case Glyph::Star: {
    QPainterPath path;
    for (int i = 0; i < 10; ++i) {
      const qreal rr = (i % 2 == 0) ? s * 0.48 : s * 0.22;
      const qreal a = -kPi / 2 + i * kPi / 5;
      const QPointF pt(r.center().x() + rr * std::cos(a),
                       r.center().y() + s * 0.04 + rr * std::sin(a));
      if (i == 0)
        path.moveTo(pt);
      else
        path.lineTo(pt);
    }
    path.closeSubpath();
    p.setPen(QPen(color, s * 0.05, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(color);
    p.drawPath(path);
    break;
  }
  case Glyph::Follow:
    p.drawEllipse(r.center(), s * 0.36, s * 0.36);
    p.setBrush(color);
    p.drawEllipse(r.center(), s * 0.14, s * 0.14);
    break;
  }
  p.restore();
}

QIcon glyphIcon(Glyph g, const QColor &color, int size) {
  QPixmap pm(size * 2, size * 2);
  pm.setDevicePixelRatio(2);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  drawGlyph(p, QRectF(0, 0, size, size), g, color);
  p.end();
  return QIcon(pm);
}

QFont overlineFont(const QFont &base, qreal scale) {
  // Chinese characters need more size than upper-case Latin labels.
  if (QLocale().language() == QLocale::Chinese)
    scale = std::min(1.0, scale * 1.3);
  QFont f = base;
  if (base.pointSizeF() > 0)
    f.setPointSizeF(base.pointSizeF() * scale);
  else if (base.pixelSize() > 0)
    f.setPixelSize(std::max(8, int(base.pixelSize() * scale)));
  f.setBold(true);
  f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
  f.setCapitalization(QFont::AllUppercase);
  return f;
}

} // namespace Theme
} // namespace Ripes
