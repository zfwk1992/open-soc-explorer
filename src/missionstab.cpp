// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "missionstab.h"

#include <QApplication>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

#include "explorer/tracerecorder.h"
#include "i18n/language.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "theme/painting.h"
#include "theme/theme.h"

namespace Ripes {

using namespace Explorer;
using namespace Theme;

namespace {
QLabel *richLabel(QWidget *parent) {
  auto *l = new QLabel(parent);
  l->setWordWrap(true);
  l->setTextFormat(Qt::RichText);
  l->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return l;
}

QString okColor() { return tokens().ok.name(); }
QString badColor() { return tokens().bad.name(); }
QString goldColor() {
  return (tokens().dark ? tokens().gold : tokens().gold.darker(140)).name();
}

void repolish(QWidget *w) {
  w->style()->unpolish(w);
  w->style()->polish(w);
  w->update();
}

QFont scaledFont(const QFont &f, qreal factor, bool bold) {
  QFont s = f;
  if (f.pointSizeF() > 0)
    s.setPointSizeF(f.pointSizeF() * factor);
  s.setBold(bold);
  return s;
}

QString processorName(const QString &id) {
  bool ok = false;
  const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
      id.toUtf8().constData(), &ok);
  return ok ? ProcessorRegistry::getDescription(static_cast<ProcessorID>(v))
                  .name
            : QString();
}

/// Colors of a mission node by status.
QColor statusColor(MissionStatus s) {
  const auto &t = tokens();
  switch (s) {
  case MissionStatus::Solved:
    return t.gold;
  case MissionStatus::InProgress:
    return t.accent;
  case MissionStatus::Available:
    return t.accent2;
  case MissionStatus::Locked:
    break;
  }
  return t.muted;
}

/// Hexagonal mission/level node.
void drawNode(QPainter &p, const QPointF &c, qreal r, MissionStatus status,
              const QString &label, const QFont &font, bool next) {
  const auto &t = tokens();
  const QColor col = statusColor(status);
  if (next && isGame()) {
    for (int i = 3; i >= 1; --i)
      drawHexagon(p, c, r + i * 2.5, Qt::transparent,
                  withAlpha(t.accent, 50 / i + 10), 2);
  }
  QColor fill;
  QColor fg;
  switch (status) {
  case MissionStatus::Solved:
  case MissionStatus::InProgress:
    fill = col;
    fg = textOn(col);
    break;
  case MissionStatus::Available:
    fill = mix(t.card, col, 0.18);
    fg = t.dark ? col.lighter(125) : col.darker(110);
    break;
  case MissionStatus::Locked:
    fill = t.panel;
    fg = t.muted;
    break;
  }
  drawHexagon(p, c, r, fill,
              status == MissionStatus::Solved ? t.gold.darker(140) : col,
              status == MissionStatus::Locked ? 1.4 : 2);
  if (status == MissionStatus::Locked) {
    drawGlyph(p, QRectF(c.x() - r * 0.45, c.y() - r * 0.45, r * 0.9, r * 0.9),
              Glyph::Lock, fg);
    return;
  }
  p.save();
  p.setFont(font);
  p.setPen(fg);
  p.drawText(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), Qt::AlignCenter,
             label);
  p.restore();
  if (status == MissionStatus::Solved) {
    const QRectF badge(c.x() + r * 0.35, c.y() - r * 1.0, r * 0.8, r * 0.8);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(t.card, 1.5));
    p.setBrush(t.ok);
    p.drawEllipse(badge);
    p.restore();
    drawGlyph(p, badge.adjusted(2, 2, -2, -2), Glyph::Check, textOn(t.ok));
  }
}

constexpr int kCardRole = Qt::UserRole + 1;

/// Paints a mission of the map: a node on the path and its card.
class MissionCardDelegate : public QStyledItemDelegate {
  Q_DECLARE_TR_FUNCTIONS(Ripes::MissionCard)

public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem &,
                 const QModelIndex &) const override {
    return QSize(320, 128);
  }

  void paint(QPainter *p, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    const auto &t = tokens();
    const QVariantMap m = index.data(kCardRole).toMap();
    const QRectF r = option.rect;
    const auto status = static_cast<MissionStatus>(m.value("status").toInt());
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const bool first = index.row() == 0;
    const bool last = index.row() == index.model()->rowCount() - 1;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    // Path of the map.
    const qreal nx = r.left() + 32;
    const qreal ny = r.top() + 42;
    const bool walked =
        status == MissionStatus::Solved || status == MissionStatus::InProgress;
    QPen path(walked ? withAlpha(t.accent, 200) : withAlpha(t.muted, 120),
              walked ? 3 : 2,
              status == MissionStatus::Locked ? Qt::DashLine : Qt::SolidLine,
              Qt::RoundCap);
    p->setPen(path);
    if (!first)
      p->drawLine(QPointF(nx, r.top()), QPointF(nx, ny));
    if (!last) {
      const bool nextWalked = m.value("nextWalked").toBool();
      QPen down = path;
      down.setColor(nextWalked ? withAlpha(t.accent, 200)
                               : withAlpha(t.muted, 120));
      down.setStyle(m.value("nextLocked").toBool() ? Qt::DashLine
                                                   : Qt::SolidLine);
      p->setPen(down);
      p->drawLine(QPointF(nx, ny), QPointF(nx, r.bottom() + 1));
    }
    drawNode(*p, QPointF(nx, ny), 21, status, m.value("number").toString(),
             scaledFont(option.font, 1.3, true), m.value("next").toBool());

    // Card.
    const QRectF card(r.left() + 64, r.top() + 6, r.width() - 72,
                      r.height() - 12);
    if (selected)
      drawGlow(*p, card, 12, t.accent, 6);
    drawCard(*p, card, 12,
             selected ? mix(t.card, t.accent, t.dark ? 0.1 : 0.05) : t.card,
             selected ? t.accent : (hover ? t.borderStrong : t.border),
             selected ? 2 : 1, true);
    const qreal x = card.left() + 12;
    const qreal w = card.width() - 24;
    const QFont over = overlineFont(option.font, 0.72);
    p->setFont(over);
    p->setPen(statusColor(status));
    p->drawText(QRectF(x, card.top() + 8, w, 14),
                Qt::AlignLeft | Qt::AlignVCenter,
                tr("Mission %1").arg(m.value("number").toInt()));
    QString statusText = missionStatusName(status);
    if (m.value("next").toBool() && status != MissionStatus::InProgress)
      statusText = tr("Next");
    const QSizeF ps = pillSize(statusText, over);
    drawPill(*p, QPointF(card.right() - 10 - ps.width(), card.top() + 6),
             statusText, statusColor(status), over,
             status == MissionStatus::Solved ||
                 status == MissionStatus::InProgress);

    const QFont titleFont = scaledFont(option.font, 1.08, true);
    p->setFont(titleFont);
    p->setPen(status == MissionStatus::Locked ? t.muted : t.text);
    p->drawText(QRectF(x, card.top() + 24, w, 20),
                Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetricsF(titleFont).elidedText(m.value("title").toString(),
                                                    Qt::ElideRight, w));

    // Processor and concepts.
    const QFont chip = scaledFont(option.font, 0.8, false);
    qreal cx = x;
    const qreal cy = card.top() + 48;
    cx = drawPill(*p, QPointF(cx, cy), m.value("processor").toString(), t.info,
                  chip)
             .right() +
         5;
    for (const auto &topic : m.value("learn").toStringList()) {
      const QSizeF s = pillSize(topic, chip);
      if (cx + s.width() > card.right() - 10)
        break;
      cx = drawPill(*p, QPointF(cx, cy), topic, t.accent2, chip).right() + 5;
    }

    // Steps verified and stars.
    const int steps = m.value("steps").toInt();
    const int verified = m.value("verified").toInt();
    const qreal by = card.bottom() - 22;
    const QFont small = scaledFont(option.font, 0.82, false);
    p->setFont(small);
    p->setPen(t.muted);
    const QString stepText = tr("%1/%2 steps").arg(verified).arg(steps);
    const qreal stw = QFontMetricsF(small).horizontalAdvance(stepText);
    const qreal starsW = 3 * 15 + 2 * 3;
    const qreal barW = std::max(30.0, w - stw - starsW - 24);
    drawProgressBar(*p, QRectF(x, by + 5, barW, 7),
                    steps ? double(verified) / steps : 0.0, t.ok);
    p->setFont(small);
    p->setPen(t.muted);
    p->drawText(QRectF(x + barW + 8, by, stw + 4, 17),
                Qt::AlignLeft | Qt::AlignVCenter, stepText);
    drawStars(*p, card.right() - 10 - starsW, by + 8.5, 15,
              m.value("stars").toInt(), kStarsPerMission);
    p->restore();
  }
};

} // namespace

// ============================================================================
// MissionHud
// ============================================================================

/// Player progress header: level, experience points, stars, solved missions
/// and verified steps, all from MissionProgress::totals().
class MissionHud : public QWidget {
  Q_DECLARE_TR_FUNCTIONS(Ripes::MissionHud)

public:
  explicit MissionHud(QWidget *parent) : QWidget(parent) {
    setObjectName("missionHud");
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_reset = new QPushButton(this);
    retranslate();
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(300);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &v) {
              m_shownXp = v.toReal();
              update();
            });
  }
  QPushButton *resetButton() const { return m_reset; }

  void retranslate() {
    m_reset->setText(tr("Reset progress"));
    m_reset->setToolTip(tr("Forget all verified steps and solved challenges"));
    resizeEvent(nullptr);
    update();
  }

  void setTotals(const MissionProgress::Totals &totals, bool animate) {
    if (m_initialized && totals.xp == m_totals.xp &&
        totals.stars == m_totals.stars && totals.solved == m_totals.solved &&
        totals.verifiedSteps == m_totals.verifiedSteps &&
        totals.missions == m_totals.missions) {
      update();
      return;
    }
    const int from = m_totals.xp;
    m_gain = totals.xp - from;
    m_totals = totals;
    m_anim->stop();
    if (animate && m_initialized && m_gain > 0 && animationsEnabled()) {
      m_anim->setStartValue(qreal(from));
      m_anim->setEndValue(qreal(totals.xp));
      m_anim->start();
    } else {
      m_shownXp = totals.xp;
      m_gain = 0;
    }
    m_initialized = true;
    update();
  }

  QSize sizeHint() const override { return QSize(900, 92); }
  QSize minimumSizeHint() const override { return QSize(500, 92); }

protected:
  void resizeEvent(QResizeEvent *) override {
    const QSize s = m_reset->sizeHint();
    m_reset->setGeometry(width() - s.width() - 18,
                         (height() - 4 - s.height()) / 2, s.width(),
                         s.height());
  }

  void paintEvent(QPaintEvent *) override {
    const auto &t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF panel = QRectF(rect()).adjusted(1, 1, -1, -5);
    drawCard(p, panel, 14, t.panel, t.border, 1, true);
    if (isGame()) {
      QLinearGradient g(panel.topLeft(), panel.topRight());
      g.setColorAt(0, withAlpha(t.gold, 0));
      g.setColorAt(0.25, t.gold);
      g.setColorAt(0.6, t.accent);
      g.setColorAt(1, withAlpha(t.accent, 0));
      p.setPen(QPen(QBrush(g), 2));
      p.drawLine(QPointF(panel.left() + 14, panel.top() + 1),
                 QPointF(panel.right() - 14, panel.top() + 1));
    }
    const auto level = playerLevel(int(std::lround(m_shownXp)));
    const QPointF hc(panel.left() + 46, panel.center().y());
    QColor hexFill = t.accent2;
    drawHexagon(p, hc, 31, hexFill, t.accent2.darker(140), 2);
    if (isGame())
      drawHexagon(p, hc, 25, withAlpha(QColor(255, 255, 255), 30),
                  Qt::transparent, 0);
    p.setFont(scaledFont(font(), 1.7, true));
    p.setPen(textOn(hexFill));
    p.drawText(QRectF(hc.x() - 30, hc.y() - 30, 60, 60), Qt::AlignCenter,
               QString::number(level.level));

    const qreal x = panel.left() + 90;
    p.setFont(overlineFont(font(), 0.74));
    p.setPen(t.accent2);
    p.drawText(QRectF(x, panel.top() + 10, 300, 14),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("Level %1").arg(level.level));
    p.setFont(scaledFont(font(), 1.35, true));
    p.setPen(t.text);
    p.drawText(QRectF(x, panel.top() + 24, 300, 24),
               Qt::AlignLeft | Qt::AlignVCenter, level.title);
    const qreal barW = 280;
    const QRectF bar(x, panel.bottom() - 24, barW, 10);
    drawProgressBar(p, bar, double(level.xpIntoLevel) / kXpPerLevel, t.gold);
    p.setFont(scaledFont(font(), 0.84, false));
    p.setPen(t.muted);
    p.drawText(QRectF(bar.right() + 10, bar.top() - 4, 200, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("%1 / %2 XP → %3")
                   .arg(level.xpIntoLevel)
                   .arg(kXpPerLevel)
                   .arg(levelTitle(level.level + 1)));
    if (m_anim->state() == QAbstractAnimation::Running && m_gain > 0) {
      const qreal e =
          m_anim->currentValue().toReal() - m_anim->startValue().toReal();
      const qreal f = std::clamp(e / std::max(1, m_gain), 0.0, 1.0);
      p.setOpacity(1.0 - f * 0.6);
      p.setFont(scaledFont(font(), 1.0, true));
      p.setPen(t.dark ? t.gold : t.gold.darker(140));
      p.drawText(QRectF(x + 150, panel.top() + 24 - f * 8, 120, 22),
                 Qt::AlignLeft | Qt::AlignVCenter,
                 QString("+%1 XP").arg(m_gain));
      p.setOpacity(1);
    }

    // Stat blocks.
    struct Block {
      Glyph glyph;
      QColor color;
      QString value;
      QString label;
    };
    const std::vector<Block> blocks = {
        {Glyph::Star, t.gold,
         QString("%1 / %2").arg(m_totals.stars).arg(m_totals.maxStars),
         tr("Stars")},
        {Glyph::Trophy, t.gold,
         QString("%1 / %2").arg(m_totals.solved).arg(m_totals.missions),
         tr("Challenges solved")},
        {Glyph::Check, t.ok,
         QString("%1 / %2").arg(m_totals.verifiedSteps).arg(m_totals.steps),
         tr("Steps verified")},
        {Glyph::Bolt, t.accent, QString::number(m_totals.xp), tr("Total XP")}};
    qreal bx = x + barW + 180;
    const qreal limit = m_reset->geometry().left() - 12;
    for (const auto &b : blocks) {
      if (bx + 150 > limit)
        break;
      const QRectF box(bx, panel.top() + 14, 150, panel.height() - 28);
      drawCard(p, box, 10, mix(t.card, b.color, t.dark ? 0.08 : 0.05),
               withAlpha(b.color, 90), 1, false);
      const QRectF icon(box.left() + 10, box.center().y() - 13, 26, 26);
      drawGlyph(p, icon, b.glyph, b.color);
      p.setFont(scaledFont(font(), 1.25, true));
      p.setPen(t.text);
      p.drawText(QRectF(icon.right() + 8, box.top() + 4, box.width() - 50, 24),
                 Qt::AlignLeft | Qt::AlignVCenter, b.value);
      p.setFont(overlineFont(font(), 0.62));
      p.setPen(t.muted);
      p.drawText(QRectF(icon.right() + 8, box.top() + 28, box.width() - 46, 14),
                 Qt::AlignLeft | Qt::AlignVCenter, b.label);
      bx += 162;
    }
  }

private:
  MissionProgress::Totals m_totals;
  qreal m_shownXp = 0;
  int m_gain = 0;
  bool m_initialized = false;
  QVariantAnimation *m_anim = nullptr;
  QPushButton *m_reset = nullptr;
};

// ============================================================================
// MissionBanner
// ============================================================================

/// Title card of the selected mission.
class MissionBanner : public QWidget {
  Q_DECLARE_TR_FUNCTIONS(Ripes::MissionBanner)

public:
  struct Info {
    int number = 0;
    QString title;
    QString processor;
    QString processorName;
    QStringList learn;
    MissionRecord record;
    MissionStatus status = MissionStatus::Available;
  };
  explicit MissionBanner(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  }
  void setInfo(const Info &info) {
    m_info = info;
    update();
  }
  QSize sizeHint() const override { return QSize(700, 136); }
  QSize minimumSizeHint() const override { return QSize(400, 136); }

protected:
  void paintEvent(QPaintEvent *) override {
    const auto &t = tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -7);
    const QColor sc = statusColor(m_info.status);
    if (isGame()) {
      // Drop shadow + gradient card.
      drawCard(p, r, 16, t.card, Qt::transparent, 0, true);
      QLinearGradient g(r.topLeft(), r.bottomRight());
      g.setColorAt(0, mix(t.card, t.accent2, t.dark ? 0.3 : 0.14));
      g.setColorAt(0.6, mix(t.card, t.accent, t.dark ? 0.12 : 0.06));
      g.setColorAt(1, mix(t.card, sc, t.dark ? 0.16 : 0.08));
      p.setPen(QPen(withAlpha(t.accent, 130), 1.2));
      p.setBrush(g);
      p.drawRoundedRect(r, 16, 16);
      // Ornament: circuit traces along the right edge.
      p.save();
      QPainterPath clip;
      clip.addRoundedRect(r, 16, 16);
      p.setClipPath(clip);
      p.setPen(QPen(withAlpha(t.accent, 34), 2));
      p.setBrush(withAlpha(t.accent, 50));
      for (int i = 0; i < 5; ++i) {
        const qreal y = r.top() + 18 + i * 22;
        const qreal x0 = r.right() - 30 - (i % 3) * 46;
        p.drawPolyline(QPolygonF({QPointF(r.right() + 4, y),
                                  QPointF(x0 + 20, y), QPointF(x0, y + 12)}));
        p.drawEllipse(QPointF(x0, y + 12), 3, 3);
      }
      p.restore();
    } else {
      drawCard(p, r, 8, t.card, t.border, 1, false);
    }

    drawNode(p, QPointF(r.left() + 58, r.center().y()), 38, m_info.status,
             QString::number(m_info.number), scaledFont(font(), 2.0, true),
             false);

    const qreal x = r.left() + 112;
    const qreal right = r.right() - 170;
    p.setFont(overlineFont(font(), 0.8));
    p.setPen(t.dark ? sc.lighter(110) : sc.darker(120));
    p.drawText(QRectF(x, r.top() + 12, right - x, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("Mission %1 · %2")
                   .arg(m_info.number)
                   .arg(missionStatusName(m_info.status)));
    const QFont tf = scaledFont(font(), 1.65, true);
    p.setFont(tf);
    p.setPen(t.text);
    p.drawText(
        QRectF(x, r.top() + 30, right - x, 34),
        Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetricsF(tf).elidedText(m_info.title, Qt::ElideRight, right - x));
    const QFont chip = scaledFont(font(), 0.86, false);
    qreal cx = x;
    const qreal cy = r.top() + 72;
    cx = drawPill(p, QPointF(cx, cy),
                  m_info.processorName.isEmpty()
                      ? m_info.processor
                      : m_info.processor + " · " + m_info.processorName,
                  t.info, chip, true)
             .right() +
         6;
    for (const auto &topic : m_info.learn) {
      const QSizeF s = pillSize(topic, chip);
      if (cx + s.width() > right)
        break;
      cx = drawPill(p, QPointF(cx, cy), topic, t.accent2, chip).right() + 6;
    }
    p.setFont(scaledFont(font(), 0.84, false));
    p.setPen(t.muted);
    p.drawText(QRectF(x, r.bottom() - 26, right - x, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               (m_info.record.solved
                    ? tr("%1 of %2 guided steps verified · challenge solved")
                    : tr("%1 of %2 guided steps verified · challenge open"))
                   .arg(m_info.record.verifiedSteps)
                   .arg(m_info.record.steps));

    // Stars and experience points of this mission.
    const qreal starSize = 26;
    const qreal starsW = 3 * starSize + 2 * starSize * 0.2;
    drawStars(p, r.right() - 22 - starsW, r.top() + 34, starSize,
              missionStars(m_info.record), kStarsPerMission);
    const MissionRecord full{m_info.record.steps, m_info.record.steps, true};
    p.setFont(scaledFont(font(), 0.9, true));
    p.setPen(t.dark ? t.gold : t.gold.darker(140));
    p.drawText(QRectF(r.right() - 170, r.top() + 56, 148, 20),
               Qt::AlignRight | Qt::AlignVCenter,
               QString("%1 / %2 XP")
                   .arg(missionXp(m_info.record))
                   .arg(missionXp(full)));
  }

private:
  Info m_info;
};

// ============================================================================
// MissionsTab
// ============================================================================

MissionsTab::MissionsTab(QToolBar *toolbar, TraceRecorder *recorder,
                         std::function<QString()> sourceProvider,
                         QWidget *parent)
    : RipesTab(toolbar, parent), m_recorder(recorder),
      m_sourceProvider(std::move(sourceProvider)) {
  // This view translates itself (missions in the active language).
  setProperty("osxOwnTranslation", true);
  QString error;
  m_missions = loadBuiltinMissions(&error, I18n::missionLanguage());
  loadProgress();

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(8, 6, 8, 6);
  root->setSpacing(6);
  m_hud = new MissionHud(this);
  connect(m_hud->resetButton(), &QPushButton::clicked, this,
          &MissionsTab::resetProgress);
  root->addWidget(m_hud);

  auto *splitter = new QSplitter(Qt::Horizontal, this);
  root->addWidget(splitter, 1);

  auto *mapPane = new QWidget(splitter);
  auto *mapLayout = new QVBoxLayout(mapPane);
  mapLayout->setContentsMargins(0, 4, 4, 0);
  auto *mapTitle = m_mapTitle = new QLabel(mapPane);
  mapTitle->setObjectName("missionOverline");
  mapTitle->setFont(overlineFont(font(), 0.86));
  mapLayout->addWidget(mapTitle);
  m_list = new QListWidget(mapPane);
  m_list->setObjectName("missionList");
  m_list->setMinimumWidth(340);
  m_list->setWordWrap(true);
  m_list->setMouseTracking(true);
  m_list->setItemDelegate(new MissionCardDelegate(m_list));
  m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_list->setFrameShape(QFrame::NoFrame);
  connect(m_list, &QListWidget::currentRowChanged, this,
          &MissionsTab::showMission);
  mapLayout->addWidget(m_list, 1);

  auto *scroll = new QScrollArea(splitter);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *page = new QWidget(scroll);
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(4, 4, 8, 8);
  layout->setSpacing(8);
  scroll->setWidget(page);

  m_banner = new MissionBanner(page);
  layout->addWidget(m_banner);
  m_lockNote = richLabel(page);
  m_lockNote->setObjectName("missionLockNote");
  m_lockNote->setVisible(false);
  layout->addWidget(m_lockNote);

  auto *objectiveCard = new QFrame(page);
  objectiveCard->setObjectName("missionCard");
  auto *objectiveLayout = new QVBoxLayout(objectiveCard);
  objectiveLayout->setContentsMargins(14, 10, 14, 10);
  m_objective = richLabel(objectiveCard);
  objectiveLayout->addWidget(m_objective);
  layout->addWidget(objectiveCard);

  auto *conceptGroup = m_conceptGroup = new QGroupBox(page);
  auto *conceptLayout = new QVBoxLayout(conceptGroup);
  m_explanation = richLabel(conceptGroup);
  conceptLayout->addWidget(m_explanation);
  layout->addWidget(conceptGroup);

  auto *buttons = new QHBoxLayout();
  auto *loadButton = m_loadButton = new QPushButton(page);
  loadButton->setProperty("osxRole", "primary");
  connect(loadButton, &QPushButton::clicked, this, [this] {
    if (m_current >= 0)
      emit loadProgramRequested(m_missions[m_current].example);
  });
  auto *procButton = m_processorButton = new QPushButton(page);
  connect(procButton, &QPushButton::clicked, this, [this] {
    if (m_current < 0)
      return;
    bool ok = false;
    const int v = QMetaEnum::fromType<ProcessorID>().keyToValue(
        m_missions[m_current].processor.toUtf8().constData(), &ok);
    if (ok)
      emit selectProcessorRequested(static_cast<ProcessorID>(v));
  });
  auto *explorerButton = m_explorerButton = new QPushButton(page);
  connect(explorerButton, &QPushButton::clicked, this,
          &MissionsTab::showExplorerRequested);
  buttons->addWidget(loadButton);
  buttons->addWidget(procButton);
  buttons->addWidget(explorerButton);
  buttons->addStretch();
  layout->addLayout(buttons);
  m_iconButtons = {{loadButton, int(Glyph::Load)},
                   {procButton, int(Glyph::Chip)},
                   {explorerButton, int(Glyph::Explore)}};
  m_processorLabel = richLabel(page);
  layout->addWidget(m_processorLabel);

  m_stepsGroup = new QGroupBox(page);
  m_stepsLayout = new QVBoxLayout(m_stepsGroup);
  m_stepsLayout->setSpacing(6);
  layout->addWidget(m_stepsGroup);

  auto *challengeGroup = m_challengeGroup = new QGroupBox(page);
  auto *challengeLayout = new QVBoxLayout(challengeGroup);
  m_challenge = richLabel(challengeGroup);
  challengeLayout->addWidget(m_challenge);
  auto *challengeButtons = new QHBoxLayout();
  m_hintButton = new QPushButton(challengeGroup);
  m_hintButton->setCheckable(true);
  connect(m_hintButton, &QPushButton::toggled, this,
          [this](bool on) { m_hint->setVisible(on); });
  auto *checkButton = m_checkButton = new QPushButton(challengeGroup);
  checkButton->setProperty("osxRole", "gold");
  connect(checkButton, &QPushButton::clicked, this,
          &MissionsTab::checkChallenge);
  m_iconButtons.push_back({m_hintButton, int(Glyph::Hint)});
  m_iconButtons.push_back({checkButton, int(Glyph::Trophy)});
  challengeButtons->addWidget(m_hintButton);
  challengeButtons->addWidget(checkButton);
  challengeButtons->addStretch();
  challengeLayout->addLayout(challengeButtons);
  m_hint = richLabel(challengeGroup);
  m_hint->setVisible(false);
  challengeLayout->addWidget(m_hint);
  m_result = richLabel(challengeGroup);
  m_result->setObjectName("missionResult");
  challengeLayout->addWidget(m_result);
  m_completion = richLabel(challengeGroup);
  challengeLayout->addWidget(m_completion);
  layout->addWidget(challengeGroup);
  layout->addStretch();

  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({430, 1200});

  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, &MissionsTab::evaluateSteps);
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          &MissionsTab::evaluateSteps);
  connect(m_recorder, &TraceRecorder::runStarted, this,
          &MissionsTab::evaluateSteps, Qt::QueuedConnection);
  // Progress changed elsewhere (another window, a reset): reload it.
  for (const char *key :
       {RIPES_SETTING_OSX_MISSION_STEPS, RIPES_SETTING_OSX_MISSIONS_COMPLETED})
    connect(RipesSettings::getObserver(key), &SettingObserver::modified, this,
            [this] {
              if (m_saving)
                return;
              loadProgress();
              refreshMissionList();
              if (m_current >= 0)
                showMission(m_current);
            });
  connect(Notifier::get(), &Notifier::changed, this, &MissionsTab::applyTheme);

  retranslateUi();
  refreshMissionList();
  if (!m_missions.empty())
    m_list->setCurrentRow(0);
  else
    m_objective->setText(
        tr("No missions available: %1").arg(error.toHtmlEscaped()));
  applyTheme();
}

void MissionsTab::retranslateUi() {
  m_mapTitle->setText(tr("MISSION MAP"));
  m_conceptGroup->setTitle(tr("Briefing: concepts"));
  m_loadButton->setText(tr("Load example"));
  m_loadButton->setToolTip(
      tr("Replace the editor contents with the example program"));
  m_processorButton->setText(tr("Select processor"));
  m_processorButton->setToolTip(
      tr("Select the processor model this mission is designed for"));
  m_explorerButton->setText(tr("Open Explorer"));
  m_stepsGroup->setTitle(tr("Guided steps (verified live)"));
  m_challengeGroup->setTitle(tr("Challenge"));
  m_hintButton->setText(tr("Show hint"));
  m_checkButton->setText(tr("Check challenge"));
  m_checkButton->setToolTip(tr("Verify your solution against the simulator "
                               "state and the recorded trace"));
  m_hud->retranslate();
}

void MissionsTab::changeEvent(QEvent *event) {
  RipesTab::changeEvent(event);
  if (event->type() != QEvent::LanguageChange)
    return;
  // Mission texts of the new language (same ids, steps and checks).
  QString error;
  auto missions = loadBuiltinMissions(&error, I18n::missionLanguage());
  if (missions.size() == m_missions.size())
    m_missions = std::move(missions);
  retranslateUi();
  refreshMissionList();
  if (m_current >= 0)
    showMission(m_current);
  m_banner->update();
}

void MissionsTab::applyTheme() {
  const auto &t = tokens();
  for (auto [button, glyph] : m_iconButtons) {
    const QString role = button->property("osxRole").toString();
    QColor color = t.text;
    if (isGame() && role == "primary")
      color = t.accentText;
    else if (isGame() && role == "gold")
      color = QColor("#2a1d00");
    button->setIcon(glyphIcon(static_cast<Glyph>(glyph), color, 16));
  }
  refreshMissionList();
  if (m_current >= 0)
    showMission(m_current);
  m_hud->update();
}

void MissionsTab::tabVisibilityChanged(bool visible) {
  m_visible = visible;
  if (visible)
    evaluateSteps();
}

void MissionsTab::loadProgress() {
  m_progress = MissionProgress(
      RipesSettings::value(RIPES_SETTING_OSX_MISSION_STEPS).toStringList(),
      RipesSettings::value(RIPES_SETTING_OSX_MISSIONS_COMPLETED)
          .toStringList());
}

void MissionsTab::saveProgress() {
  m_saving = true;
  if (RipesSettings::value(RIPES_SETTING_OSX_MISSION_STEPS).toStringList() !=
      m_progress.verifiedSteps())
    RipesSettings::setValue(RIPES_SETTING_OSX_MISSION_STEPS,
                            m_progress.verifiedSteps());
  if (RipesSettings::value(RIPES_SETTING_OSX_MISSIONS_COMPLETED)
          .toStringList() != m_progress.solved())
    RipesSettings::setValue(RIPES_SETTING_OSX_MISSIONS_COMPLETED,
                            m_progress.solved());
  m_saving = false;
}

void MissionsTab::resetProgress() {
  if (QMessageBox::question(
          this, tr("Reset mission progress"),
          tr("Forget all verified steps and solved challenges? Your stars, "
             "experience points and level start again from zero.")) !=
      QMessageBox::Yes)
    return;
  m_progress = MissionProgress();
  saveProgress();
  refreshMissionList();
  if (m_current >= 0)
    showMission(m_current);
}

void MissionsTab::refreshProgressViews() {
  m_hud->setTotals(m_progress.totals(m_missions), true);
  refreshMissionList();
  if (m_current >= 0) {
    const auto &m = m_missions[m_current];
    MissionBanner::Info info;
    info.number = m.number;
    info.title = m.title;
    info.processor = m.processor;
    info.processorName = processorName(m.processor);
    info.learn = m.learn;
    info.record = m_progress.record(m);
    info.status = missionStatus(m_progress.records(m_missions), m_current);
    m_banner->setInfo(info);
  }
}

void MissionsTab::refreshMissionList() {
  const int row = m_list->currentRow();
  const auto records = m_progress.records(m_missions);
  // The recommended next mission: the first unlocked one not solved yet.
  int next = -1;
  for (size_t i = 0; i < records.size() && next < 0; ++i) {
    const auto s = missionStatus(records, i);
    if (s == MissionStatus::Available || s == MissionStatus::InProgress)
      next = static_cast<int>(i);
  }
  m_list->blockSignals(true);
  m_list->clear();
  for (size_t i = 0; i < m_missions.size(); ++i) {
    const auto &m = m_missions[i];
    const auto status = missionStatus(records, i);
    auto *item = new QListWidgetItem(tr("%1 Mission %2: %3")
                                         .arg(records[i].solved ? "✓" : "○")
                                         .arg(m.number)
                                         .arg(m.title));
    QVariantMap card;
    card["number"] = m.number;
    card["title"] = m.title;
    card["processor"] = m.processor;
    card["learn"] = m.learn;
    card["steps"] = records[i].steps;
    card["verified"] = records[i].verifiedSteps;
    card["stars"] = missionStars(records[i]);
    card["status"] = static_cast<int>(status);
    card["next"] = static_cast<int>(i) == next;
    if (i + 1 < m_missions.size()) {
      const auto ns = missionStatus(records, i + 1);
      card["nextWalked"] =
          ns == MissionStatus::Solved || ns == MissionStatus::InProgress;
      card["nextLocked"] = ns == MissionStatus::Locked;
    }
    item->setData(kCardRole, card);
    item->setToolTip(tr("%1 · %2 of %3 steps verified · %4 star(s) · %5 XP")
                         .arg(missionStatusName(status))
                         .arg(records[i].verifiedSteps)
                         .arg(records[i].steps)
                         .arg(missionStars(records[i]))
                         .arg(missionXp(records[i])));
    m_list->addItem(item);
  }
  m_list->setCurrentRow(row);
  m_list->blockSignals(false);
}

void MissionsTab::showMission(int index) {
  if (index < 0 || static_cast<size_t>(index) >= m_missions.size())
    return;
  m_current = index;
  const auto &m = m_missions[index];
  const auto records = m_progress.records(m_missions);
  const auto status = missionStatus(records, index);

  MissionBanner::Info info;
  info.number = m.number;
  info.title = m.title;
  info.processor = m.processor;
  info.processorName = processorName(m.processor);
  info.learn = m.learn;
  info.record = records[index];
  info.status = status;
  m_banner->setInfo(info);

  if (status == MissionStatus::Locked && index > 0) {
    m_lockNote->setText(
        tr("<b>Locked</b> – recommended after Mission %1: verify its "
           "guided steps (or solve its challenge) to unlock this "
           "mission on the map. You can still read and play it now.")
            .arg(m_missions[index - 1].number));
    m_lockNote->setVisible(true);
  } else {
    m_lockNote->setVisible(false);
  }

  m_objective->setText(
      QString("<span style='color:%1; font-weight:700'>%2</span><br>"
              "%3<br><span style='color:%4'>%5</span>")
          .arg(tokens().accent.name(), tr("OBJECTIVE"),
               m.objective.toHtmlEscaped(), tokens().muted.name(),
               tr("You will learn: %1")
                   .arg(m.learn.join(" · ").toHtmlEscaped())));
  m_explanation->setText(m.explanation);
  m_processorLabel->setText(
      tr("<b>Processor:</b> %1 (%2). <b>Example program:</b><pre>%3</pre>")
          .arg(processorName(m.processor).toHtmlEscaped(),
               m.processor.toHtmlEscaped(), m.example.toHtmlEscaped()));

  // Rebuild the step widgets.
  while (auto *item = m_stepsLayout->takeAt(0)) {
    if (auto *w = item->widget())
      w->deleteLater();
    delete item;
  }
  m_stepWidgets.clear();
  int n = 1;
  for (const auto &step : m.steps) {
    auto *box = new QFrame(m_stepsGroup);
    box->setObjectName("missionStep");
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(10, 8, 10, 8);
    row->setSpacing(10);
    auto *status = new QLabel(box);
    status->setObjectName("missionStepStatus");
    status->setMinimumWidth(28);
    status->setAlignment(Qt::AlignCenter);
    QFont f = status->font();
    f.setPointSizeF(f.pointSizeF() * 1.3);
    f.setBold(true);
    status->setFont(f);
    auto *col = new QVBoxLayout();
    auto *text = richLabel(box);
    text->setText(tr("<b>Step %1: %2</b><br>%3")
                      .arg(n++)
                      .arg(step.title.toHtmlEscaped(), step.instruction));
    auto *checks = richLabel(box);
    col->addWidget(text);
    col->addWidget(checks);
    row->addWidget(status, 0, Qt::AlignTop);
    row->addLayout(col, 1);
    m_stepsLayout->addWidget(box);
    m_stepWidgets.push_back({box, status, checks});
  }

  m_challenge->setText(m.challenge);
  m_hint->setText("<i>" + tr("Hint:") + "</i> " + m.hint.toHtmlEscaped());
  m_hintButton->setChecked(false);
  m_result->clear();
  m_completion->setText(
      m_progress.isSolved(m.id)
          ? QString("<span style='color:%1'>✓ %2</span> %3")
                .arg(okColor(), tr("Completed."), m.completion.toHtmlEscaped())
          : QString());
  m_hud->setTotals(m_progress.totals(m_missions), false);
  evaluateSteps();
}

MissionState MissionsTab::currentState() const {
  return captureMissionState(m_recorder->trace(),
                             m_sourceProvider ? m_sourceProvider() : QString());
}

QString MissionsTab::criteriaHtml(const CheckReport &report) const {
  QString html;
  for (const auto &r : report.results) {
    html += QString("<span style='color:%1'>%2</span> %3")
                .arg(r.passed ? okColor() : badColor(), r.passed ? "✓" : "✗",
                     r.description.toHtmlEscaped());
    if (!r.detail.isEmpty())
      html += " <span style='color:" + tokens().muted.name() + "'>(" +
              r.detail.toHtmlEscaped() + ")</span>";
    html += "<br>";
  }
  return html;
}

void MissionsTab::evaluateSteps() {
  if (!m_visible || m_current < 0 || ProcessorHandler::isRunning())
    return;
  const auto &m = m_missions[m_current];
  const auto state = currentState();
  bool previousDone = true;
  bool newlyVerified = false;
  for (size_t i = 0; i < m.steps.size() && i < m_stepWidgets.size(); ++i) {
    const auto report = evaluateChecks(m.steps[i].checks, state, m);
    if (report.passed)
      newlyVerified |= m_progress.recordStep(m.id, i);
    const bool completed = m_progress.stepVerified(m.id, i);
    auto &w = m_stepWidgets[i];
    const char *stateName = completed      ? "done"
                            : previousDone ? "current"
                                           : "todo";
    w.status->setText(completed ? "✓" : (previousDone ? "▶" : "○"));
    w.status->setProperty("state", stateName);
    w.box->setProperty("state", stateName);
    w.status->setStyleSheet(completed && !isGame()
                                ? QString("color:%1").arg(okColor())
                                : QString());
    repolish(w.status);
    repolish(w.box);
    QString html = criteriaHtml(report);
    if (completed && !report.passed)
      html =
          "<i>" + tr("Completed earlier. Live state now:") + "</i><br>" + html;
    w.checks->setText(html);
    previousDone = completed;
  }
  if (newlyVerified) {
    saveProgress();
    refreshProgressViews();
  }
}

void MissionsTab::checkChallenge() {
  if (m_current < 0)
    return;
  const auto &m = m_missions[m_current];
  const auto report = evaluateChecks(m.challengeChecks, currentState(), m);
  QString html = criteriaHtml(report);
  if (report.passed) {
    const bool first = m_progress.recordSolved(m.id);
    html =
        QString("<p style='color:%1'><b>%2</b> %3%4</p>")
            .arg(okColor(), tr("Challenge solved!"),
                 tr("Verified against the simulator state and the recorded "
                    "trace."),
                 first ? QString(" <b style='color:%1'>%2</b>")
                             .arg(goldColor(), tr("+%1 XP · ★ challenge star")
                                                   .arg(kXpPerChallenge))
                       : " " + tr("(already counted)")) +
        html;
    m_completion->setText(
        QString("<span style='color:%1'>✓ %2</span> %3")
            .arg(okColor(), tr("Completed."), m.completion.toHtmlEscaped()));
    if (first) {
      saveProgress();
      refreshProgressViews();
    }
  } else {
    html = QString("<p style='color:%1'><b>%2</b> %3</p>")
               .arg(badColor(), tr("Not solved yet."),
                    tr("The failing conditions are marked below.")) +
           html;
  }
  m_result->setText(html);
}

} // namespace Ripes
