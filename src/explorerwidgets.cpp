// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "explorerwidgets.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>
#include <map>

#include "theme/theme.h"

namespace Ripes {

using Explorer::TraceReplay;
using namespace Theme;

namespace {
constexpr qreal kBadgeH = 40;
constexpr qreal kBadgeGap = 8;
constexpr qreal kPad = 10;
constexpr qreal kTileHeader = 22;

QRectF lerp(const QRectF &a, const QRectF &b, qreal t) {
  return QRectF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t,
                a.width() + (b.width() - a.width()) * t,
                a.height() + (b.height() - a.height()) * t);
}

QFont scaled(const QFont &f, qreal factor, bool bold = false) {
  QFont s = f;
  if (f.pointSizeF() > 0)
    s.setPointSizeF(f.pointSizeF() * factor);
  s.setBold(bold);
  return s;
}
} // namespace

// ============================================================================
// HudBanner
// ============================================================================

HudBanner::HudBanner(QWidget *parent) : QLabel(parent) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  setMouseTracking(true);
}

void HudBanner::setBadges(const std::vector<Badge> &badges,
                          const QString &note) {
  m_badges = badges;
  m_note = note;
  updateGeometry();
  update();
}

std::vector<QRectF> HudBanner::layoutBadges(int width, qreal *height) const {
  const QFontMetricsF labelFm(overlineFont(font(), 0.72));
  const QFontMetricsF valueFm(scaled(font(), 1.0, true));
  std::vector<QRectF> rects;
  const qreal maxW = std::max(80.0, width - 2 * kPad);
  qreal x = kPad, y = kPad;
  for (const auto &b : m_badges) {
    qreal w = std::max(labelFm.horizontalAdvance(b.label),
                       valueFm.horizontalAdvance(b.value)) +
              26;
    w = std::min(w, maxW);
    if (x > kPad && x + w > width - kPad) {
      x = kPad;
      y += kBadgeH + kBadgeGap;
    }
    rects.emplace_back(x, y, w, kBadgeH);
    x += w + kBadgeGap;
  }
  qreal h = (rects.empty() ? kPad : rects.back().bottom()) + kPad;
  if (!m_note.isEmpty())
    h += QFontMetricsF(font()).height() + 2;
  if (height)
    *height = h + 4;
  return rects;
}

int HudBanner::heightForWidth(int width) const {
  qreal h = 0;
  layoutBadges(width, &h);
  return int(std::ceil(h));
}

QSize HudBanner::sizeHint() const {
  const int w = std::max(width(), 400);
  return QSize(w, heightForWidth(w));
}

QSize HudBanner::minimumSizeHint() const {
  return QSize(200, heightForWidth(std::max(width(), 200)));
}

void HudBanner::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  qreal h = 0;
  const auto rects = layoutBadges(width(), &h);
  const QRectF panel = QRectF(rect()).adjusted(1, 1, -1, -4);
  drawCard(p, panel, 12, t.panel, t.border, 1, true);
  if (isGame()) {
    // HUD accent line along the top edge.
    QLinearGradient g(panel.topLeft(), panel.topRight());
    g.setColorAt(0, withAlpha(t.accent2, 0));
    g.setColorAt(0.2, t.accent2);
    g.setColorAt(0.8, t.accent);
    g.setColorAt(1, withAlpha(t.accent, 0));
    p.setPen(QPen(QBrush(g), 2));
    p.drawLine(QPointF(panel.left() + 12, panel.top() + 1),
               QPointF(panel.right() - 12, panel.top() + 1));
  }
  const QFont labelFont = overlineFont(font(), 0.72);
  const QFont valueFont = scaled(font(), 1.0, true);
  for (size_t i = 0; i < m_badges.size() && i < rects.size(); ++i) {
    const auto &b = m_badges[i];
    const QRectF r = rects[i];
    drawCard(p, r, 9, mix(t.card, b.color, t.dark ? 0.12 : 0.07),
             withAlpha(b.color, t.dark ? 140 : 120), 1, false);
    p.setPen(Qt::NoPen);
    p.setBrush(b.color);
    p.drawRoundedRect(QRectF(r.left() + 4, r.top() + 7, 4, r.height() - 14), 2,
                      2);
    p.setFont(labelFont);
    p.setPen(t.dark ? b.color.lighter(108) : b.color.darker(115));
    p.drawText(QRectF(r.left() + 14, r.top() + 4, r.width() - 18, 14),
               Qt::AlignLeft | Qt::AlignVCenter, b.label);
    p.setFont(valueFont);
    p.setPen(t.text);
    const QFontMetricsF fm(valueFont);
    p.drawText(QRectF(r.left() + 14, r.top() + 18, r.width() - 18, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(b.value, Qt::ElideMiddle, r.width() - 18));
  }
  if (!m_note.isEmpty()) {
    QFont nf = font();
    nf.setItalic(true);
    p.setFont(nf);
    p.setPen(t.muted);
    const qreal y = (rects.empty() ? kPad : rects.back().bottom()) + 4;
    const QRectF nr(kPad + 2, y, width() - 2 * kPad - 4,
                    QFontMetricsF(nf).height() + 2);
    p.drawText(
        nr, Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetricsF(nf).elidedText(m_note, Qt::ElideRight, nr.width()));
  }
}

bool HudBanner::event(QEvent *e) {
  if (e->type() == QEvent::ToolTip) {
    auto *he = static_cast<QHelpEvent *>(e);
    const auto rects = layoutBadges(width(), nullptr);
    for (size_t i = 0; i < rects.size() && i < m_badges.size(); ++i)
      if (rects[i].contains(he->pos()) && !m_badges[i].tooltip.isEmpty()) {
        QToolTip::showText(he->globalPos(), m_badges[i].tooltip, this,
                           rects[i].toRect());
        return true;
      }
  }
  return QLabel::event(e);
}

// ============================================================================
// StageTile
// ============================================================================

StageTile::StageTile(QWidget *parent) : QToolButton(parent) {
  setToolButtonStyle(Qt::ToolButtonTextOnly);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setCursor(Qt::PointingHandCursor);
  setAttribute(Qt::WA_Hover);
}

QSize StageTile::sizeHint() const { return QSize(150, 104); }
QSize StageTile::minimumSizeHint() const { return QSize(96, 104); }

void StageTile::setVisual(const SlotVisual &v) {
  m_visual = v;
  update();
}

void StageTile::setBodyHidden(bool hidden) {
  if (hidden != m_bodyHidden) {
    m_bodyHidden = hidden;
    update();
  }
}

QRectF StageTile::frameRect() const {
  return QRectF(rect()).adjusted(4, 4, -4, -6);
}

QRectF StageTile::bodyRect() const {
  return frameRect().adjusted(6, kTileHeader + 5, -6, -6);
}

void StageTile::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF f = frameRect();
  const QColor sc = stageColor(m_visual.colorIndex);
  const bool hover =
      underMouse() && m_visual.kind == TraceReplay::SlotKind::Instruction;
  if (m_visual.selected)
    drawGlow(p, f, 12, t.accent, 6);
  drawCard(p, f, 12, t.card,
           m_visual.selected ? t.accent : (hover ? t.borderStrong : t.border),
           m_visual.selected ? 2 : 1, true);

  // Header band in the stage color.
  p.save();
  QPainterPath clip;
  clip.addRoundedRect(f.adjusted(1, 1, -1, -1), 11, 11);
  p.setClipPath(clip);
  const QRectF header(f.left(), f.top(), f.width(), kTileHeader);
  if (isGame()) {
    QLinearGradient g(header.topLeft(), header.topRight());
    g.setColorAt(0, sc.darker(t.dark ? 125 : 105));
    g.setColorAt(1, mix(sc, t.card, 0.35));
    p.fillRect(header, g);
  } else {
    p.fillRect(header, withAlpha(sc, 70));
  }
  p.restore();
  QFont hf = overlineFont(font(), 0.86);
  p.setFont(hf);
  p.setPen(isGame() ? textOn(sc.darker(t.dark ? 125 : 105)) : t.text);
  p.drawText(header.adjusted(10, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter,
             m_visual.stage);

  if (!m_bodyHidden)
    paintBody(p, bodyRect(), m_visual, font());
}

void StageTile::paintBody(QPainter &p, const QRectF &r, const SlotVisual &v,
                          const QFont &font) {
  const auto &t = tokens();
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  const QFont pillFont = overlineFont(font, 0.68);
  switch (v.kind) {
  case TraceReplay::SlotKind::Instruction: {
    const QColor col = v.wrongPath ? t.bad
                       : v.held    ? t.warn
                                   : stageColor(v.colorIndex);
    drawCard(p, r, 8, mix(t.base, col, t.dark ? 0.24 : 0.12),
             withAlpha(col, 180), 1.2, false);
    if (v.held)
      drawStripes(p, r, 8, withAlpha(t.warn, t.dark ? 38 : 48), 9);
    const QString mnemonic = v.assembly.section(' ', 0, 0);
    const QString operands = v.assembly.section(' ', 1).trimmed();
    const QFont mf = scaled(font, 1.12, true);
    const QFontMetricsF mfm(mf);
    const QString id = Trace::instanceName(v.instance);
    const QFont idf = scaled(font, 0.8);
    const qreal idW = QFontMetricsF(idf).horizontalAdvance(id) + 4;
    p.setFont(mf);
    p.setPen(t.text);
    const QRectF line1(r.left() + 8, r.top() + 3, r.width() - 16, mfm.height());
    p.drawText(line1, Qt::AlignLeft | Qt::AlignVCenter,
               mfm.elidedText(mnemonic, Qt::ElideRight, line1.width() - idW));
    p.setFont(idf);
    p.setPen(t.muted);
    p.drawText(line1, Qt::AlignRight | Qt::AlignVCenter, id);
    const QFont of =
        monoFont(font.pointSizeF() > 0 ? font.pointSizeF() * 1.08 : 10);
    const QFontMetricsF ofm(of);
    p.setFont(of);
    p.setPen(mix(t.text, t.muted, 0.25));
    const QRectF line2(r.left() + 8, line1.bottom(), r.width() - 16,
                       ofm.height());
    p.drawText(line2, Qt::AlignLeft | Qt::AlignVCenter,
               ofm.elidedText(operands, Qt::ElideRight, line2.width()));
    // Status pills along the bottom edge.
    const qreal pillH = pillSize("X", pillFont).height();
    qreal x = r.left() + 6;
    const qreal y = r.bottom() - pillH - 4;
    if (v.held)
      x = drawPill(p, QPointF(x, y), "held", t.warn, pillFont, true).right() +
          4;
    if (v.wrongPath)
      x = drawPill(p, QPointF(x, y), "✕ wrong path", t.bad, pillFont, true)
              .right() +
          4;
    if (v.occupants > 1) {
      const QString more = QString("+%1 more").arg(v.occupants - 1);
      const qreal w = pillSize(more, pillFont).width();
      drawPill(p, QPointF(r.right() - 6 - w, y), more, t.accent2, pillFont);
    }
    break;
  }
  case TraceReplay::SlotKind::StallBubble:
  case TraceReplay::SlotKind::FlushBubble: {
    const bool stall = v.kind == TraceReplay::SlotKind::StallBubble;
    const QColor col = stall ? t.warn : t.bad;
    p.setBrush(withAlpha(col, t.dark ? 26 : 18));
    QPen pen(withAlpha(col, 200), 1.4, Qt::DashLine);
    p.setPen(pen);
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 8, 8);
    const QRectF icon(r.left() + 10, r.center().y() - 8, 16, 16);
    if (stall) {
      p.setPen(QPen(col, 1.6));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(icon.adjusted(1, 1, -1, -1));
      p.drawEllipse(QRectF(icon.left() + 5, icon.top() + 4, 4, 4));
    } else {
      drawGlyph(p, icon, Glyph::Cross, col);
    }
    p.setFont(scaled(font, 1.0, true));
    p.setPen(t.dark ? col.lighter(110) : col.darker(110));
    p.drawText(r.adjusted(32, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter,
               stall ? "stall bubble" : "flushed (bubble)");
    break;
  }
  case TraceReplay::SlotKind::Empty: {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(withAlpha(t.muted, 90), 1, Qt::DotLine));
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 8, 8);
    p.setFont(font);
    p.setPen(withAlpha(t.muted, 200));
    p.drawText(r, Qt::AlignCenter, "— empty —");
    break;
  }
  }
  p.restore();
}

// ============================================================================
// PipelineStrip
// ============================================================================

class StripOverlay : public QWidget {
public:
  explicit StripOverlay(PipelineStrip *strip) : QWidget(strip), m_strip(strip) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    m_strip->paintOverlay(p);
  }

private:
  PipelineStrip *m_strip;
};

PipelineStrip::PipelineStrip(QWidget *parent) : QWidget(parent) {
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(2, 0, 2, 0);
  m_layout->setSpacing(4);
  m_overlay = new StripOverlay(this);
  m_overlay->hide();
  m_anim = new QVariantAnimation(this);
  m_anim->setStartValue(0.0);
  m_anim->setEndValue(1.0);
  m_anim->setDuration(kTransitionMs);
  m_anim->setEasingCurve(QEasingCurve::OutCubic);
  connect(m_anim, &QVariantAnimation::valueChanged, this,
          [this](const QVariant &v) {
            m_progress = v.toReal();
            m_overlay->update();
          });
  connect(m_anim, &QVariantAnimation::finished, this,
          [this] { stopAnimation(); });
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

bool PipelineStrip::setStages(
    const std::vector<std::pair<unsigned, QString>> &stages) {
  if (stages == m_stages && !m_tiles.empty())
    return false;
  stopAnimation();
  m_stages = stages;
  // Hide the old tiles and show the new ones right away: deleteLater() and
  // the layout's own show are both deferred to the event loop.
  for (auto *tile : m_tiles) {
    tile->hide();
    tile->deleteLater();
  }
  m_tiles.clear();
  m_lanes.clear();
  while (auto *item = m_layout->takeAt(0))
    delete item;
  std::map<unsigned, QHBoxLayout *> lanes;
  for (const auto &[lane, name] : stages) {
    if (!lanes.count(lane)) {
      lanes[lane] = new QHBoxLayout();
      lanes[lane]->setSpacing(20);
      m_layout->addLayout(lanes[lane]);
    }
    auto *tile = new StageTile(this);
    connect(tile, &QToolButton::clicked, this, [this, tile] {
      const qint64 id = tile->property("instance").toLongLong();
      if (id >= 0)
        emit instanceClicked(id);
    });
    lanes[lane]->addWidget(tile);
    tile->setVisible(true);
    m_tiles.push_back(tile);
    m_lanes.push_back(lane);
  }
  // Models with one or two stages keep normal-sized tiles.
  for (auto &[lane, layout] : lanes)
    if (layout->count() < 3)
      layout->addStretch(1);
  m_overlay->raise();
  updateGeometry();
  update();
  return true;
}

void PipelineStrip::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  m_overlay->setGeometry(rect());
}

QRectF PipelineStrip::tileBody(int index) const {
  if (index < 0 || static_cast<size_t>(index) >= m_tiles.size())
    return QRectF();
  const auto *tile = m_tiles[index];
  return tile->bodyRect().translated(tile->pos());
}

void PipelineStrip::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  // Flow arrows between consecutive stages of a lane.
  const QColor arrow = isGame() ? withAlpha(t.accent, 170) : t.borderStrong;
  p.setPen(QPen(arrow, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  for (size_t i = 1; i < m_tiles.size(); ++i) {
    if (m_lanes[i] != m_lanes[i - 1])
      continue;
    const QRect a = m_tiles[i - 1]->geometry();
    const QRect b = m_tiles[i]->geometry();
    const qreal cx = (a.right() + b.left()) / 2.0 + 1;
    const qreal cy = a.top() + 4 + (a.height() - 10) / 2.0 + kTileHeader / 2;
    p.drawPolyline(QPolygonF({QPointF(cx - 4, cy - 6), QPointF(cx + 3, cy),
                              QPointF(cx - 4, cy + 6)}));
  }
}

void PipelineStrip::animate(std::vector<Move> moves) {
  stopAnimation();
  if (moves.empty())
    return;
  m_moves = std::move(moves);
  for (const auto &m : m_moves)
    if (m.kind != Move::Exit && m.to >= 0 &&
        static_cast<size_t>(m.to) < m_tiles.size())
      m_tiles[m.to]->setBodyHidden(true);
  m_progress = 0;
  m_overlay->setGeometry(rect());
  m_overlay->show();
  m_overlay->raise();
  m_anim->start();
}

bool PipelineStrip::isAnimating() const {
  return m_anim->state() == QAbstractAnimation::Running;
}

void PipelineStrip::stopAnimation() {
  if (m_anim->state() == QAbstractAnimation::Running)
    m_anim->stop();
  m_moves.clear();
  m_progress = 1.0;
  for (auto *tile : m_tiles)
    tile->setBodyHidden(false);
  m_overlay->hide();
}

void PipelineStrip::paintOverlay(QPainter &p) {
  const auto &t = tokens();
  p.setRenderHint(QPainter::Antialiasing);
  const qreal e = m_progress;
  for (const auto &m : m_moves) {
    switch (m.kind) {
    case Move::Slide: {
      const QRectF from = tileBody(m.from), to = tileBody(m.to);
      // Motion trail: two faint copies behind the card.
      for (qreal lag : {0.24, 0.12}) {
        p.setOpacity(0.18 * (1 - e));
        StageTile::paintBody(p, lerp(from, to, std::max(0.0, e - lag)),
                             m.visual, font());
      }
      p.setOpacity(1);
      StageTile::paintBody(p, lerp(from, to, e), m.visual, font());
      break;
    }
    case Move::Enter: {
      p.setOpacity(e);
      StageTile::paintBody(p, tileBody(m.to).translated(-(1 - e) * 26, 0),
                           m.visual, font());
      p.setOpacity(1);
      break;
    }
    case Move::Exit: {
      QRectF r = tileBody(m.from);
      const QColor col = m.retired ? t.ok : t.bad;
      if (m.retired) {
        r.translate(e * 40, 0);
      } else {
        const qreal s = 0.12 * e;
        r.adjust(r.width() * s, r.height() * s, -r.width() * s,
                 -r.height() * s);
      }
      p.setOpacity(1 - e);
      StageTile::paintBody(p, r, m.visual, font());
      const QRectF badge(r.right() - 22, r.top() - 6, 20, 20);
      p.setPen(Qt::NoPen);
      p.setBrush(col);
      p.drawEllipse(badge);
      drawGlyph(p, badge.adjusted(2, 2, -2, -2),
                m.retired ? Glyph::Check : Glyph::Cross, textOn(col));
      p.setOpacity(1);
      break;
    }
    }
  }
}

// ============================================================================
// StripLegend
// ============================================================================

StripLegend::StripLegend(QWidget *parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
}

void StripLegend::setStages(
    const std::vector<std::pair<int, QString>> &colorAndName) {
  m_stages = colorAndName;
  updateGeometry();
  update();
}

std::vector<StripLegend::Entry> StripLegend::entries() const {
  const auto &t = tokens();
  std::vector<Entry> e;
  for (const auto &[ci, name] : m_stages)
    e.push_back({name, stageColor(ci), 0});
  e.push_back({"held (stall)", t.warn, 1});
  e.push_back({"stall bubble", t.warn, 2});
  e.push_back({"flushed / wrong path", t.bad, 3});
  e.push_back({"retired", t.ok, 4});
  e.push_back({"squashed", t.bad, 5});
  return e;
}

std::vector<QRectF> StripLegend::layout(int width, qreal *height) const {
  QFont f = font();
  if (f.pointSizeF() > 0)
    f.setPointSizeF(f.pointSizeF() * 0.86);
  const QFontMetricsF fm(f);
  std::vector<QRectF> rects;
  qreal x = 2, y = 2;
  const qreal h = fm.height() + 6;
  for (const auto &e : entries()) {
    const qreal w = fm.horizontalAdvance(e.text) + (e.style >= 4 ? 30 : 18);
    if (x > 2 && x + w > width - 2) {
      x = 2;
      y += h + 4;
    }
    rects.emplace_back(x, y, w, h);
    x += w + 6;
  }
  if (height)
    *height = (rects.empty() ? 0 : rects.back().bottom()) + 2;
  return rects;
}

int StripLegend::heightForWidth(int width) const {
  qreal h = 0;
  layout(width, &h);
  return int(std::ceil(h));
}

QSize StripLegend::sizeHint() const {
  const int w = std::max(width(), 300);
  return QSize(w, heightForWidth(w));
}

QSize StripLegend::minimumSizeHint() const {
  return QSize(100, heightForWidth(std::max(width(), 100)));
}

void StripLegend::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  QFont f = font();
  if (f.pointSizeF() > 0)
    f.setPointSizeF(f.pointSizeF() * 0.86);
  p.setFont(f);
  const auto list = entries();
  const auto rects = layout(width(), nullptr);
  for (size_t i = 0; i < list.size(); ++i) {
    const auto &e = list[i];
    const QRectF r = rects[i];
    const QRectF chip = r.adjusted(0, 1, 0, -1);
    const QColor fill = mix(t.base, e.color, t.dark ? 0.34 : 0.2);
    switch (e.style) {
    case 0:
    case 1:
    case 3:
      p.setPen(QPen(withAlpha(e.color, 190), 1));
      p.setBrush(fill);
      p.drawRoundedRect(chip, 5, 5);
      if (e.style == 1)
        drawStripes(p, chip, 5, withAlpha(t.warn, 60), 6);
      break;
    case 2:
      p.setPen(QPen(withAlpha(e.color, 200), 1.2, Qt::DashLine));
      p.setBrush(withAlpha(e.color, 24));
      p.drawRoundedRect(chip, 5, 5);
      break;
    default: {
      const qreal d = chip.height() - 4;
      const QRectF c(chip.left() + 2, chip.top() + 2, d, d);
      p.setPen(Qt::NoPen);
      p.setBrush(e.color);
      p.drawEllipse(c);
      drawGlyph(p, c.adjusted(1, 1, -1, -1),
                e.style == 4 ? Glyph::Check : Glyph::Cross, textOn(e.color));
      break;
    }
    }
    p.setPen(e.style == 0 && !t.dark ? e.color.darker(150) : t.text);
    p.drawText(e.style >= 4 ? chip.adjusted(chip.height() - 2, 0, 0, 0) : chip,
               Qt::AlignCenter, e.text);
  }
}

// ============================================================================
// TimelineDelegate
// ============================================================================

void TimelineDelegate::paint(QPainter *painter,
                             const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
  const auto &t = tokens();
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  const QRectF r = option.rect;
  if (option.state & QStyle::State_Selected)
    painter->fillRect(r, withAlpha(t.accent, t.dark ? 30 : 24));
  if (index.data(TimelineRole::CurrentCycle).toBool()) {
    painter->fillRect(r, withAlpha(t.accent, t.dark ? 34 : 26));
    painter->setPen(QPen(withAlpha(t.accent, 150), 1));
    painter->drawLine(r.topLeft(), r.bottomLeft());
    painter->drawLine(r.topRight(), r.bottomRight());
  }
  const int stage = index.data(TimelineRole::Stage).toInt();
  const int end = index.data(TimelineRole::End).toInt();
  if (stage >= 0) {
    const int ci = index.data(TimelineRole::ColorIndex).toInt();
    const bool held = index.data(TimelineRole::Held).toBool();
    const bool wrong = index.data(TimelineRole::WrongPath).toBool();
    const QColor col = wrong ? t.bad : held ? t.warn : stageColor(ci);
    const QColor fill = wrong  ? mix(t.base, t.bad, t.dark ? 0.32 : 0.16)
                        : held ? mix(t.base, t.warn, t.dark ? 0.32 : 0.2)
                               : stageFill(ci);
    const QRectF cell = r.adjusted(2, 3, -2, -3);
    painter->setPen(QPen(withAlpha(col, t.dark ? 200 : 170), 1));
    painter->setBrush(fill);
    painter->drawRoundedRect(cell, 5, 5);
    if (held)
      drawStripes(*painter, cell, 5, withAlpha(t.warn, t.dark ? 55 : 60), 6);
    if (isGame()) {
      QPainterPath clip;
      clip.addRoundedRect(cell, 5, 5);
      painter->save();
      painter->setClipPath(clip);
      painter->fillRect(
          QRectF(cell.left(), cell.top(), cell.width(), cell.height() * 0.42),
          QColor(255, 255, 255, t.dark ? 16 : 70));
      painter->restore();
    }
    QFont f = option.font;
    f.setBold(true);
    if (f.pointSizeF() > 0)
      f.setPointSizeF(f.pointSizeF() * 0.9);
    painter->setFont(f);
    painter->setPen(t.dark ? t.text : col.darker(150));
    painter->drawText(cell, Qt::AlignCenter, index.data().toString());
  } else if (end == 1 || end == 2) {
    const QColor col = end == 1 ? t.ok : t.bad;
    const qreal d = std::min(r.height() - 6, 17.0);
    const QRectF c(r.center().x() - d / 2, r.center().y() - d / 2, d, d);
    painter->setPen(Qt::NoPen);
    painter->setBrush(col);
    painter->drawEllipse(c);
    drawGlyph(*painter, c.adjusted(1.5, 1.5, -1.5, -1.5),
              end == 1 ? Glyph::Check : Glyph::Cross, textOn(col));
  }
  painter->restore();
}

// ============================================================================
// ChipDelegate
// ============================================================================

void ChipDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const {
  const QString text = index.data().toString();
  if (text.isEmpty()) {
    QStyledItemDelegate::paint(painter, option, index);
    return;
  }
  painter->save();
  if (option.state & QStyle::State_Selected)
    painter->fillRect(option.rect, option.palette.highlight());
  QFont f = option.font;
  if (f.pointSizeF() > 0)
    f.setPointSizeF(f.pointSizeF() * 0.86);
  f.setBold(index.data(Qt::FontRole).value<QFont>().bold());
  const QSizeF s = pillSize(text, f);
  drawPill(*painter,
           QPointF(option.rect.left() + 6,
                   option.rect.center().y() - s.height() / 2 + 0.5),
           text, observabilityColor(text), f);
  painter->restore();
}

QSize ChipDelegate::sizeHint(const QStyleOptionViewItem &option,
                             const QModelIndex &index) const {
  QFont f = option.font;
  if (f.pointSizeF() > 0)
    f.setPointSizeF(f.pointSizeF() * 0.86);
  const QSizeF s = pillSize(index.data().toString(), f);
  return QSize(int(s.width()) + 14, int(s.height()) + 8);
}

} // namespace Ripes
