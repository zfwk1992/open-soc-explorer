// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "touch.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QVBoxLayout>

#include <cmath>

#include "theme/painting.h"
#include "theme/theme.h"

namespace Ripes {
namespace Mobile {

using namespace Theme;

constexpr const char *kKineticProperty = "osxKineticScrolling";

void enableKineticScrolling(QAbstractScrollArea *area, bool horizontal) {
  if (!area)
    return;
  QWidget *viewport = area->viewport();
  QScroller::grabGesture(viewport, QScroller::LeftMouseButtonGesture);
  QScroller *scroller = QScroller::scroller(viewport);
  QScrollerProperties p = scroller->scrollerProperties();
  // Start scrolling after a short drag (3 mm) and do not bounce beyond the
  // ends: pages stay put when they fit the screen.
  p.setScrollMetric(QScrollerProperties::DragStartDistance, 0.003);
  p.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                    QScrollerProperties::OvershootAlwaysOff);
  p.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                    QScrollerProperties::OvershootAlwaysOff);
  p.setScrollMetric(QScrollerProperties::FrameRate, QScrollerProperties::Fps60);
  // Locks a mostly vertical drag to the vertical axis.
  p.setScrollMetric(QScrollerProperties::AxisLockThreshold, 0.6);
  scroller->setScrollerProperties(p);
  if (!horizontal)
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  if (auto *view = qobject_cast<QAbstractItemView *>(area)) {
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  }
  area->setProperty(kKineticProperty, true);
}

bool hasKineticScrolling(const QAbstractScrollArea *area) {
  return area && area->property(kKineticProperty).toBool() &&
         QScroller::hasScroller(area->viewport());
}

// ============================================================================
// ScrollPage
// ============================================================================

namespace {

/// A scroll area whose content always has exactly the viewport's width (no
/// horizontal overflow) and the height its layout needs for that width.
class FitScrollArea : public QScrollArea {
public:
  explicit FitScrollArea(QWidget *parent) : QScrollArea(parent) {
    setWidgetResizable(false);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }
  void setContent(QWidget *w) {
    setWidget(w);
    w->installEventFilter(this);
  }
  void fit() {
    QWidget *w = widget();
    if (!w || !w->layout() || m_fitting)
      return;
    m_fitting = true;
    const int width = viewport()->width();
    QLayout *l = w->layout();
    int height = l->hasHeightForWidth() ? l->totalHeightForWidth(width)
                                        : l->totalSizeHint().height();
    height = std::max(height, viewport()->height());
    if (w->size() != QSize(width, height))
      w->resize(width, height);
    m_fitting = false;
  }

protected:
  void resizeEvent(QResizeEvent *e) override {
    QScrollArea::resizeEvent(e);
    fit();
  }
  bool eventFilter(QObject *o, QEvent *e) override {
    if (o == widget() && e->type() == QEvent::LayoutRequest)
      fit();
    return QScrollArea::eventFilter(o, e);
  }

private:
  bool m_fitting = false;
};

} // namespace

ScrollPage::ScrollPage(QWidget *parent) : QWidget(parent) {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  auto *area = new FitScrollArea(this);
  m_area = area;
  m_content = new QWidget();
  m_content->setObjectName("mobilePageContent");
  m_layout = new QVBoxLayout(m_content);
  m_layout->setContentsMargins(12, 12, 12, 16);
  m_layout->setSpacing(12);
  area->setContent(m_content);
  outer->addWidget(area);
  enableKineticScrolling(area);
}

void ScrollPage::finish() { m_layout->addStretch(1); }

void ScrollPage::scrollToTop() {
  QScroller::scroller(m_area->viewport())->stop();
  m_area->verticalScrollBar()->setValue(0);
}

// ============================================================================
// Card
// ============================================================================

Card::Card(QWidget *parent) : QWidget(parent) {
  setObjectName("mobileCard");
  m_body = new QVBoxLayout(this);
  m_body->setContentsMargins(14, 12, 14, 14);
  m_body->setSpacing(8);
  m_title = new QLabel(this);
  m_title->setObjectName("mobileOverline");
  m_title->setVisible(false);
  m_body->addWidget(m_title);
}

void Card::setTitle(const QString &title) {
  m_title->setText(title.toUpper());
  m_title->setVisible(!title.isEmpty());
}

void Card::setAccent(const QColor &color) {
  m_accent = color;
  update();
}

void Card::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -2.5);
  drawCard(p, r, 14, t.card, m_accent.isValid() ? m_accent : t.border,
           m_accent.isValid() ? 1.5 : 1, true);
  if (m_accent.isValid()) {
    p.setPen(Qt::NoPen);
    p.setBrush(m_accent);
    p.drawRoundedRect(QRectF(r.left() + 1, r.top() + 12, 4, r.height() - 24), 2,
                      2);
  }
}

// ============================================================================
// NavBar
// ============================================================================

NavBar::NavBar(QWidget *parent) : QWidget(parent) {
  setObjectName("mobileNavBar");
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

int NavBar::addTab(NavIcon icon, const QString &text) {
  m_tabs.push_back({icon, text});
  if (m_current < 0)
    m_current = 0;
  updateGeometry();
  update();
  return count() - 1;
}

void NavBar::setTabText(int index, const QString &text) {
  if (index >= 0 && index < count()) {
    m_tabs[index].text = text;
    update();
  }
}

QString NavBar::tabText(int index) const {
  return index >= 0 && index < count() ? m_tabs[index].text : QString();
}

void NavBar::setCurrentIndex(int index) {
  if (index < 0 || index >= count() || index == m_current)
    return;
  m_current = index;
  update();
  emit currentChanged(index);
}

QSize NavBar::sizeHint() const { return QSize(360, 60); }
QSize NavBar::minimumSizeHint() const { return QSize(6 * kTouchTarget, 60); }

QRect NavBar::tabRect(int index) const {
  if (count() == 0)
    return QRect();
  const qreal w = qreal(width()) / count();
  return QRect(int(std::round(index * w)), 0,
               int(std::round((index + 1) * w)) - int(std::round(index * w)),
               height());
}

int NavBar::tabAt(const QPoint &pos) const {
  for (int i = 0; i < count(); ++i)
    if (tabRect(i).contains(pos))
      return i;
  return -1;
}

void NavBar::mousePressEvent(QMouseEvent *e) {
  m_pressed = tabAt(e->position().toPoint());
  update();
}

void NavBar::mouseReleaseEvent(QMouseEvent *e) {
  const int tab = tabAt(e->position().toPoint());
  if (tab >= 0 && tab == m_pressed)
    setCurrentIndex(tab);
  m_pressed = -1;
  update();
}

void NavBar::paintEvent(QPaintEvent *) {
  const auto &t = tokens();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), t.panel);
  p.setPen(QPen(t.border, 1));
  p.drawLine(QPointF(0, 0.5), QPointF(width(), 0.5));
  QFont f = font();
  if (f.pointSizeF() > 0)
    f.setPointSizeF(f.pointSizeF() * 0.78);
  f.setBold(true);
  p.setFont(f);
  for (int i = 0; i < count(); ++i) {
    const QRectF r = tabRect(i);
    const bool active = i == m_current;
    const QColor c = active ? t.accent : t.muted;
    if (active) {
      const QRectF pill(r.center().x() - 24, r.top() + 6, 48, 28);
      p.setPen(Qt::NoPen);
      p.setBrush(withAlpha(t.accent, t.dark ? 46 : 30));
      p.drawRoundedRect(pill, 14, 14);
    } else if (i == m_pressed) {
      p.setPen(Qt::NoPen);
      p.setBrush(withAlpha(t.muted, 30));
      p.drawRoundedRect(r.adjusted(4, 4, -4, -4), 10, 10);
    }
    drawNavIcon(p, QRectF(r.center().x() - 11, r.top() + 9, 22, 22),
                m_tabs[i].icon, c);
    p.setPen(active ? (t.dark ? t.accent.lighter(115) : t.accent) : t.muted);
    // Shrink long labels a little before eliding them.
    QFont lf = f;
    for (int step = 0;
         step < 3 && lf.pointSizeF() > 0 &&
         QFontMetricsF(lf).horizontalAdvance(m_tabs[i].text) > r.width() - 4;
         ++step)
      lf.setPointSizeF(lf.pointSizeF() * 0.92);
    p.setFont(lf);
    const QFontMetricsF fm(lf);
    p.drawText(QRectF(r.left() + 1, r.top() + 36, r.width() - 2, 20),
               Qt::AlignHCenter | Qt::AlignVCenter,
               fm.elidedText(m_tabs[i].text, Qt::ElideRight, r.width() - 2));
    p.setFont(f);
  }
}

void drawNavIcon(QPainter &p, const QRectF &r, NavIcon icon,
                 const QColor &color) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  QPen pen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  const qreal x = r.left(), y = r.top(), w = r.width(), h = r.height();
  switch (icon) {
  case NavIcon::Missions:
    drawGlyph(p, r, Glyph::Trophy, color);
    break;
  case NavIcon::Run:
    p.drawEllipse(r.adjusted(1, 1, -1, -1));
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(QPolygonF({QPointF(x + w * 0.4, y + h * 0.3),
                             QPointF(x + w * 0.72, y + h * 0.5),
                             QPointF(x + w * 0.4, y + h * 0.7)}));
    break;
  case NavIcon::Pipeline:
    // Stacked stage cards.
    for (int i = 0; i < 3; ++i) {
      const QRectF bar(x + 2, y + 2 + i * h * 0.32, w - 4, h * 0.22);
      p.drawRoundedRect(bar, 2, 2);
    }
    break;
  case NavIcon::Explain:
    // Speech bubble with an "i".
    {
      QPainterPath path;
      path.addRoundedRect(QRectF(x + 1, y + 1, w - 2, h * 0.72), 5, 5);
      p.drawPath(path);
      p.drawLine(QPointF(x + w * 0.3, y + h * 0.73),
                 QPointF(x + w * 0.22, y + h - 1));
      p.drawLine(QPointF(x + w * 0.22, y + h - 1),
                 QPointF(x + w * 0.5, y + h * 0.73));
      p.drawLine(QPointF(x + w * 0.5, y + h * 0.32),
                 QPointF(x + w * 0.5, y + h * 0.56));
      p.setPen(Qt::NoPen);
      p.setBrush(color);
      p.drawEllipse(QPointF(x + w * 0.5, y + h * 0.2), 1.4, 1.4);
    }
    break;
  case NavIcon::Registers:
    // A register file: grid of cells.
    for (int row = 0; row < 3; ++row)
      for (int col = 0; col < 2; ++col)
        p.drawRoundedRect(QRectF(x + 1 + col * w * 0.52, y + 1 + row * h * 0.34,
                                 w * 0.44, h * 0.26),
                          1.5, 1.5);
    break;
  case NavIcon::Settings: {
    // Gear.
    const QPointF c = r.center();
    const qreal ro = w * 0.46, ri = w * 0.32;
    QPolygonF gear;
    for (int i = 0; i < 16; ++i) {
      const qreal a0 = (i / 16.0) * 2 * M_PI;
      const qreal rad = (i % 2 == 0) ? ro : ri;
      gear << QPointF(c.x() + rad * std::cos(a0 - 0.12),
                      c.y() + rad * std::sin(a0 - 0.12))
           << QPointF(c.x() + rad * std::cos(a0 + 0.12),
                      c.y() + rad * std::sin(a0 + 0.12));
    }
    p.drawPolygon(gear);
    p.drawEllipse(c, w * 0.13, w * 0.13);
    break;
  }
  }
  p.restore();
}

QAbstractButton *makeButton(const QString &text, QWidget *parent,
                            const QString &role) {
  auto *b = new QPushButton(text, parent);
  b->setMinimumHeight(kTouchTarget + 4);
  b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  if (!role.isEmpty())
    b->setProperty("osxRole", role);
  b->setFocusPolicy(Qt::NoFocus);
  return b;
}

QLabel *makeLabel(QWidget *parent, bool rich) {
  auto *l = new QLabel(parent);
  l->setWordWrap(true);
  l->setTextFormat(rich ? Qt::RichText : Qt::PlainText);
  l->setTextInteractionFlags(Qt::NoTextInteraction);
  return l;
}

} // namespace Mobile
} // namespace Ripes
