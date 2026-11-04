#include "iotensorunitwidget.h"

#include <QApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QScrollArea>
#include <QResizeEvent>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "help.h"
#include "processorhandler.h"
#include "ripessettings.h"

namespace Ripes {

// ===========================================================================
// Colours and small helpers
// ===========================================================================

namespace {

QColor mix(const QColor &a, const QColor &b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  return QColor::fromRgbF(
      float(a.redF() * (1 - t) + b.redF() * t),
      float(a.greenF() * (1 - t) + b.greenF() * t),
      float(a.blueF() * (1 - t) + b.blueF() * t));
}

QString hex32(uint32_t v) {
  return QStringLiteral("0x%1").arg(v, 8, 16, QLatin1Char('0'));
}

/// Sets tooltip and status tip in one go (§1.2: every control has both).
void setTips(QWidget *w, const QString &text) {
  w->setToolTip(text);
  w->setStatusTip(text);
}

/// Updates a table cell, creating it on first use and only touching it when
/// the content changed (keeps refreshes cheap).
void setCell(QTableWidget *t, int row, int col, const QString &text,
             const QString &tip = QString(), const QColor &bg = QColor(),
             const QColor &fg = QColor(), bool bold = false) {
  auto *item = t->item(row, col);
  if (!item) {
    item = new QTableWidgetItem();
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    t->setItem(row, col, item);
  }
  if (item->text() != text)
    item->setText(text);
  if (!tip.isNull() && item->toolTip() != tip) {
    item->setToolTip(tip);
    item->setStatusTip(tip);
  }
  item->setBackground(bg.isValid() ? QBrush(bg) : QBrush());
  item->setForeground(fg.isValid() ? QBrush(fg) : QBrush());
  if (item->font().bold() != bold) {
    QFont f = item->font();
    f.setBold(bold);
    item->setFont(f);
  }
}

QTableWidget *makeTable(const QStringList &headers, QWidget *parent) {
  auto *t = new QTableWidget(0, headers.size(), parent);
  t->setHorizontalHeaderLabels(headers);
  t->verticalHeader()->hide();
  t->setSelectionMode(QAbstractItemView::NoSelection);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setWordWrap(false);
  t->horizontalHeader()->setStretchLastSection(true);
  t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  t->verticalHeader()->setDefaultSectionSize(
      QFontMetrics(t->font()).height() + 6);
  return t;
}

void drawArrowHead(QPainter &p, QPointF tip, QPointF from, double size = 6) {
  const QPointF d = tip - from;
  const double len = std::hypot(d.x(), d.y());
  if (len < 1e-6)
    return;
  const QPointF u = d / len;
  const QPointF n(-u.y(), u.x());
  QPolygonF head;
  head << tip << tip - u * size + n * size * 0.5
       << tip - u * size - n * size * 0.5;
  p.save();
  p.setBrush(p.pen().color());
  p.drawPolygon(head);
  p.restore();
}

constexpr npu::State kStates[] = {
    npu::State::IDLE,    npu::State::FETCH_DESC, npu::State::DMA_IN,
    npu::State::COMPUTE, npu::State::DMA_OUT,    npu::State::COMPLETE};
constexpr int kNumStates = 6;

} // namespace

NpuColors NpuColors::fromPalette(const QPalette &pal) {
  NpuColors c;
  const bool dark = pal.color(QPalette::Window).lightnessF() < 0.5;
  c.base = pal.color(QPalette::Base);
  c.text = pal.color(QPalette::Text);
  c.dim = mix(c.text, c.base, 0.45);
  c.grid = mix(c.text, c.base, 0.65);
  c.highlight = pal.color(QPalette::Highlight);
  c.highlightText = pal.color(QPalette::HighlightedText);
  c.activeLow = mix(c.base, c.highlight, 0.30);
  c.activeHigh = mix(c.base, c.highlight, 0.95);
  c.weight = mix(c.base, pal.color(QPalette::Mid), dark ? 0.35 : 0.25);
  c.empty = c.base;
  c.ok = dark ? QColor(120, 210, 130) : QColor(30, 125, 50);
  c.warn = dark ? QColor(240, 180, 90) : QColor(175, 100, 0);
  c.error = dark ? QColor(255, 120, 120) : QColor(185, 30, 30);
  c.info = c.highlight;
  c.dmaIn = c.highlight;
  c.dmaOut = mix(c.highlight, c.warn, 0.75);
  return c;
}

QColor npuContrastText(const QColor &bg) {
  // Relative luminance (sRGB approximation).
  const double l =
      0.2126 * bg.redF() + 0.7152 * bg.greenF() + 0.0722 * bg.blueF();
  return l > 0.55 ? QColor(20, 20, 20) : QColor(245, 245, 245);
}

// ===========================================================================
// State machine view
// ===========================================================================

namespace {
constexpr int kSmMargin = 8;
constexpr int kSmTopArc = 16;   // y of the "error" arc above the boxes
constexpr int kSmBoxTop = 34;   // top of the state boxes
constexpr int kSmBoxH = 38;
constexpr int kSmGap = 18;      // horizontal gap between boxes
constexpr int kSmLevel = 17;    // spacing of the loop arcs below the boxes
constexpr int kSmMinBoxW = 78;
} // namespace

NpuStateMachineView::NpuStateMachineView(QWidget *parent) : QWidget(parent) {
  setMouseTracking(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_colors = NpuColors::fromPalette(palette());
}

QSize NpuStateMachineView::minimumSizeHint() const {
  const int lineH = QFontMetrics(font()).height();
  const int h = kSmBoxTop + kSmBoxH + 3 * kSmLevel + 6 + 2 * (lineH + 2) +
                kSmMargin;
  return QSize(2 * kSmMargin + kNumStates * kSmMinBoxW +
                   (kNumStates - 1) * kSmGap,
               h);
}

QSize NpuStateMachineView::sizeHint() const {
  return QSize(std::max(760, minimumSizeHint().width()),
               minimumSizeHint().height());
}

QRect NpuStateMachineView::boxRect(int i) const {
  const int avail = width() - 2 * kSmMargin - (kNumStates - 1) * kSmGap;
  const int boxW = std::max(kSmMinBoxW, avail / kNumStates);
  return QRect(kSmMargin + i * (boxW + kSmGap), kSmBoxTop, boxW, kSmBoxH);
}

void NpuStateMachineView::setData(const npu::Snapshot &snap,
                                  const NpuColors &colors) {
  m_snap = snap;
  m_colors = colors;
  update();
}

QString NpuStateMachineView::stateDescription(npu::State s) {
  switch (s) {
  case npu::State::IDLE:
    return tr("IDLE: waiting for the doorbell (HEAD == TAIL).");
  case npu::State::FETCH_DESC:
    return tr("FETCH_DESC: reading the 32-byte descriptor at QUEUE_BASE + "
              "HEAD*32 by DMA, then validating it.");
  case npu::State::DMA_IN:
    return tr("DMA_IN: copying the input tile (A and B, or a vector chunk) "
              "from host memory into the on-chip buffers.");
  case npu::State::COMPUTE:
    return tr("COMPUTE: shifting the weights in, then streaming the "
              "activations through the systolic array.");
  case npu::State::DMA_OUT:
    return tr("DMA_OUT: writing the int32 result tile back to host memory.");
  case npu::State::COMPLETE:
    return tr("COMPLETE: HEAD advances by one and the IRQ_STATUS bits are "
              "set (DESC_DONE / QUEUE_EMPTY / ERROR).");
  }
  return QString();
}

QString NpuStateMachineView::currentActivity(const npu::Snapshot &s) {
  const bool matmul =
      static_cast<npu::Opcode>(s.descriptor.opcode) == npu::Opcode::MATMUL;
  QString tile;
  if (matmul && s.tile.mTiles > 0) {
    tile = tr(", tile (m,n,k) = (%1,%2,%3) of (%4,%5,%6)")
               .arg(s.tile.mTile)
               .arg(s.tile.nTile)
               .arg(s.tile.kTile)
               .arg(s.tile.mTiles)
               .arg(s.tile.nTiles)
               .arg(s.tile.kTiles);
  }
  switch (s.state) {
  case npu::State::IDLE:
    if (s.queueSize != 0 && s.head != s.tail)
      return tr("doorbell seen: fetching starts next cycle");
    return tr("waiting for work");
  case npu::State::FETCH_DESC:
    return tr("reading descriptor in slot %1").arg(s.head);
  case npu::State::DMA_IN:
  case npu::State::DMA_OUT:
    return tr("%1 of %2 bytes transferred")
               .arg(s.dma.bytesDone)
               .arg(s.dma.bytesTotal) +
           tile;
  case npu::State::COMPUTE:
    if (!matmul)
      return tr("SIMD pass over elements %1..%2 of %3")
          .arg(s.vecOffset)
          .arg(s.vecOffset + s.vecChunk)
          .arg(s.vecTotal);
    if (s.weightLoading)
      return tr("weight load: %1 of %2 rows shifted in (B rows enter at the "
                "top, last row k=%3 first, and move down one row per cycle)")
                 .arg(s.weightLoadCycle)
                 .arg(s.arrayN)
                 .arg(s.arrayN - 1) +
             tile;
    return tr("systolic pass: %1 of %2 array cycles done (tileM + 2N - 2)")
               .arg(s.computeCycle)
               .arg(s.computeCyclesTotal) +
           tile;
  case npu::State::COMPLETE:
    return s.status & npu::Status::ERROR
               ? tr("finishing a rejected descriptor (ERROR)")
               : tr("finishing descriptor: HEAD++");
  }
  return QString();
}

bool NpuStateMachineView::event(QEvent *e) {
  if (e->type() == QEvent::ToolTip) {
    auto *he = static_cast<QHelpEvent *>(e);
    for (int i = 0; i < kNumStates; ++i) {
      if (boxRect(i).contains(he->pos())) {
        QToolTip::showText(he->globalPos(), stateDescription(kStates[i]),
                           this, boxRect(i));
        return true;
      }
    }
    QToolTip::showText(he->globalPos(), toolTip(), this);
    return true;
  }
  return QWidget::event(e);
}

void NpuStateMachineView::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto &c = m_colors;
  QFont small = font();
  small.setPointSizeF(std::max(6.5, font().pointSizeF() * 0.85));
  QFont bold = font();
  bold.setBold(true);
  const QFontMetrics fm(font());

  const bool error = (m_snap.status & npu::Status::ERROR) != 0;
  const auto cx = [this](int i) { return boxRect(i).center().x(); };
  const int boxBottom = kSmBoxTop + kSmBoxH;

  // Forward arrows between consecutive boxes.
  p.setPen(QPen(c.dim, 1.4));
  for (int i = 0; i + 1 < kNumStates; ++i) {
    const QRect a = boxRect(i), b = boxRect(i + 1);
    const QPointF from(a.right() + 1, a.center().y());
    const QPointF to(b.left() - 1, b.center().y());
    p.drawLine(from, to);
    drawArrowHead(p, to, from);
  }

  // Loop arcs. Each is a polyline with an arrow head at its end.
  const auto arc = [&](int fromX, int toX, int y, bool above,
                       const QString &label, const QRect &labelRect,
                       const QColor &col) {
    const int edge = above ? kSmBoxTop : boxBottom;
    p.setPen(QPen(col, 1.2, Qt::DashLine));
    QPainterPath path;
    path.moveTo(fromX, edge);
    path.lineTo(fromX, y);
    path.lineTo(toX, y);
    path.lineTo(toX, edge);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(QPen(col, 1.2));
    drawArrowHead(p, QPointF(toX, edge), QPointF(toX, y));
    p.setFont(small);
    p.setPen(col);
    p.drawText(labelRect, Qt::AlignCenter, label);
  };

  const QFontMetrics sfm(small);
  const int sH = sfm.height();
  // Above: FETCH_DESC -> COMPLETE (descriptor rejected / DMA fault).
  arc(cx(1) + 6, cx(5) - 6, kSmTopArc, true,
      tr("invalid descriptor or DMA fault"),
      QRect(cx(2), kSmTopArc + 1, cx(4) - cx(2), kSmBoxTop - kSmTopArc - 1),
      error ? c.error : c.dim);
  // Below: three loops.
  const int y1 = boxBottom + kSmLevel;
  const int y2 = y1 + kSmLevel;
  const int y3 = y2 + kSmLevel;
  arc(cx(3) - 8, cx(2) + 8, y1, false, tr("next K-tile"),
      QRect(cx(2) + 10, boxBottom + 1, cx(3) - cx(2) - 20, kSmLevel - 2),
      c.dim);
  arc(cx(4), cx(2) - 8, y2, false, tr("next output tile"),
      QRect(cx(3) - 4, y1 + 1, cx(4) - cx(3), kSmLevel - 2), c.dim);
  arc(cx(5), cx(0), y3, false,
      tr("HEAD++, set IRQ_STATUS; fetch the next descriptor if HEAD != TAIL"),
      QRect(cx(0) + 4, y2 + 1, cx(4) - cx(0), kSmLevel - 2), c.dim);
  (void)sH;

  // Boxes.
  for (int i = 0; i < kNumStates; ++i) {
    const QRect r = boxRect(i);
    const bool current = m_snap.state == kStates[i];
    const bool errBox = error && kStates[i] == npu::State::COMPLETE;
    const QColor fill = current ? c.highlight : c.base;
    const QColor border =
        errBox ? c.error : (current ? c.highlight.darker(130) : c.grid);
    p.setPen(QPen(border, current || errBox ? 2.5 : 1.2));
    p.setBrush(fill);
    p.drawRoundedRect(r, 6, 6);
    p.setFont(current ? bold : font());
    p.setPen(current ? c.highlightText : c.text);
    QString name = QString::fromLatin1(npu::stateName(kStates[i]));
    if (current)
      name = QStringLiteral("▶ ") + name; // text marker, not only colour
    p.drawText(r.adjusted(2, 2, -2, -r.height() / 2),
               Qt::AlignHCenter | Qt::AlignBottom,
               fm.elidedText(name, Qt::ElideRight, r.width() - 4));
    p.setFont(small);
    p.drawText(r.adjusted(2, r.height() / 2, -2, -2),
               Qt::AlignHCenter | Qt::AlignTop,
               current ? tr("current") : QString::number(i));
  }

  // Caption: current state in words.
  const int capY = y3 + 6;
  const int lineH = fm.height() + 2;
  p.setFont(bold);
  p.setPen(c.text);
  const QString head =
      tr("Current state: %1 (STATUS[7:4] = %2)")
          .arg(QString::fromLatin1(npu::stateName(m_snap.state)))
          .arg(static_cast<unsigned>(m_snap.state));
  p.drawText(QRect(kSmMargin, capY, width() - 2 * kSmMargin, lineH),
             Qt::AlignLeft | Qt::AlignVCenter, head);
  p.setFont(font());
  p.setPen(c.dim);
  p.drawText(QRect(kSmMargin, capY + lineH, width() - 2 * kSmMargin, lineH),
             Qt::AlignLeft | Qt::AlignVCenter,
             fm.elidedText(currentActivity(m_snap), Qt::ElideRight,
                           width() - 2 * kSmMargin));
}

// ===========================================================================
// Systolic array view
// ===========================================================================

namespace {
constexpr int kArMargin = 8;
constexpr int kArGutter = 44; // row / column label gutter
constexpr int kArLegendRowH = 20;
constexpr int kArLegendRows = 5;
} // namespace

NpuArrayView::NpuArrayView(QWidget *parent) : QWidget(parent) {
  setMouseTracking(true);
  m_snap.arrayN = 4; // default array size until the first refresh
  m_snap.pes.assign(16, npu::PEState());
  m_colors = NpuColors::fromPalette(palette());
}

void NpuArrayView::setData(const npu::Snapshot &snap, unsigned cellSize,
                           const NpuColors &colors) {
  const bool resized =
      snap.arrayN != m_snap.arrayN || cellSize != m_cellSize;
  m_snap = snap;
  m_cellSize = cellSize;
  m_colors = colors;
  if (resized) {
    setMinimumSize(minimumSizeHint());
    updateGeometry();
  }
  update();
}

int NpuArrayView::cellSize() const {
  return static_cast<int>(std::max(16u, m_cellSize));
}

QPoint NpuArrayView::gridOrigin() const {
  const int lineH = QFontMetrics(font()).height() + 4;
  return QPoint(kArMargin + kArGutter, kArMargin + lineH + kArGutter);
}

int NpuArrayView::legendTop() const {
  const int n = static_cast<int>(std::max(1u, m_snap.arrayN));
  return gridOrigin().y() + n * cellSize() + kArGutter;
}

QSize NpuArrayView::minimumSizeHint() const {
  const int n = static_cast<int>(std::max(1u, m_snap.arrayN));
  const int gridW = gridOrigin().x() + n * cellSize() + kArGutter + kArMargin;
  const int w = std::max(gridW, 380);
  const int h = legendTop() + kArLegendRows * kArLegendRowH + kArMargin;
  return QSize(w, h);
}

QSize NpuArrayView::sizeHint() const { return minimumSizeHint(); }

bool NpuArrayView::isActive(const npu::PEState &pe) const {
  const bool matmul =
      static_cast<npu::Opcode>(m_snap.descriptor.opcode) ==
      npu::Opcode::MATMUL;
  return pe.aValid && (pe.wValid || !matmul);
}

bool NpuArrayView::event(QEvent *e) {
  if (e->type() == QEvent::ToolTip) {
    auto *he = static_cast<QHelpEvent *>(e);
    const QPoint o = gridOrigin();
    const int cs = cellSize();
    const int n = static_cast<int>(m_snap.arrayN);
    const QPoint rel = he->pos() - o;
    if (rel.x() >= 0 && rel.y() >= 0 && rel.x() < n * cs && rel.y() < n * cs &&
        static_cast<int>(m_snap.pes.size()) == n * n) {
      const int col = rel.x() / cs, row = rel.y() / cs;
      const auto &pe = m_snap.pes[static_cast<size_t>(row) * n + col];
      QString t =
          tr("PE(k=%1, n=%2)\na = %3 (activation from the left%4)\n"
             "w = %5 (stationary weight B[k][n]%6)\nacc = %7 (psum_in + a*w, "
             "passed down)\n%8")
              .arg(row)
              .arg(col)
              .arg(int(pe.a))
              .arg(pe.aValid ? QString() : tr(", none"))
              .arg(int(pe.w))
              .arg(pe.wValid ? QString() : tr(", padding"))
              .arg(pe.acc)
              .arg(isActive(pe) ? tr("MAC performed this cycle")
                                : tr("no MAC this cycle"));
      QToolTip::showText(he->globalPos(), t, this,
                         QRect(o.x() + col * cs, o.y() + row * cs, cs, cs));
      return true;
    }
    QToolTip::showText(he->globalPos(), toolTip(), this);
    return true;
  }
  return QWidget::event(e);
}

void NpuArrayView::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), m_colors.base);
  const auto &c = m_colors;
  const int n = static_cast<int>(m_snap.arrayN);
  const int cs = cellSize();
  const QPoint o = gridOrigin();
  const QFontMetrics fm(font());
  QFont small = font();
  small.setPointSizeF(std::max(6.0, font().pointSizeF() * 0.8));
  QFont bold = font();
  bold.setBold(true);

  if (n <= 0 || static_cast<int>(m_snap.pes.size()) != n * n)
    return;

  // Summary line.
  int active = 0;
  int32_t maxAbs = 1;
  for (const auto &pe : m_snap.pes) {
    if (isActive(pe)) {
      active++;
      maxAbs = std::max<int32_t>(maxAbs, std::abs(pe.acc));
    }
  }
  p.setFont(bold);
  p.setPen(c.text);
  QString summary;
  if (m_snap.state != npu::State::COMPUTE)
    summary = tr("Systolic array %1 x %1 (active only in COMPUTE)").arg(n);
  else if (m_snap.weightLoading)
    summary = tr("Systolic array %1 x %1: loading weights - B rows are "
                 "pushed in bottom row first and shift down")
                  .arg(n);
  else
    summary = tr("Systolic array %1 x %1: %2 of %3 PEs did a MAC this cycle")
                  .arg(n)
                  .arg(active)
                  .arg(n * n);
  p.drawText(QRect(kArMargin, kArMargin, width() - 2 * kArMargin,
                   fm.height() + 2),
             Qt::AlignLeft | Qt::AlignVCenter, summary);

  // Edge labels and flow arrows.
  p.setFont(small);
  p.setPen(c.dim);
  for (int col = 0; col < n; ++col)
    p.drawText(QRect(o.x() + col * cs, o.y() - kArGutter + 2, cs,
                     kArGutter / 2),
               Qt::AlignCenter, QStringLiteral("n%1").arg(col));
  for (int row = 0; row < n; ++row)
    p.drawText(QRect(kArMargin + 18, o.y() + row * cs, kArGutter - 20, cs),
               Qt::AlignCenter, QStringLiteral("k%1").arg(row));
  p.drawText(QRect(o.x(), o.y() - kArGutter / 2, n * cs, kArGutter / 2),
             Qt::AlignLeft | Qt::AlignVCenter,
             tr("↓ weights B (stationary), partial sums flow down"));
  p.drawText(QRect(o.x(), o.y() + n * cs + 2, n * cs, kArGutter - 4),
             Qt::AlignLeft | Qt::AlignVCenter,
             tr("↓ results C leave the bottom row"));
  {
    // "A ->" arrow on the left edge.
    p.save();
    p.translate(kArMargin, o.y() + n * cs / 2);
    p.rotate(-90);
    p.drawText(QRect(-n * cs / 2, 0, n * cs, 16), Qt::AlignCenter,
               tr("activations A enter here →"));
    p.restore();
  }

  // Cells.
  const bool showText = cs >= 40;
  for (int row = 0; row < n; ++row) {
    for (int col = 0; col < n; ++col) {
      const auto &pe = m_snap.pes[static_cast<size_t>(row) * n + col];
      const QRect r(o.x() + col * cs + 1, o.y() + row * cs + 1, cs - 3,
                    cs - 3);
      QColor fill = c.empty;
      if (isActive(pe)) {
        const double t = double(std::abs(pe.acc)) / double(maxAbs);
        fill = mix(c.activeLow, c.activeHigh, t);
      } else if (pe.wValid) {
        fill = c.weight;
      }
      p.setBrush(fill);
      p.setPen(QPen(pe.aValid ? c.text : c.grid,
                    pe.aValid ? 2.0 : 1.0,
                    pe.wValid || pe.aValid ? Qt::SolidLine : Qt::DotLine));
      p.drawRoundedRect(r, 4, 4);
      if (!showText)
        continue;
      const QColor tc = npuContrastText(fill);
      p.setPen(tc);
      p.setFont(small);
      const int half = r.width() / 2;
      const int rowH = r.height() / 3;
      p.drawText(QRect(r.x() + 3, r.y() + 1, half, rowH),
                 Qt::AlignLeft | Qt::AlignVCenter,
                 QStringLiteral("a %1").arg(int(pe.a)));
      p.drawText(QRect(r.x() + half, r.y() + 1, half - 4, rowH),
                 Qt::AlignRight | Qt::AlignVCenter,
                 QStringLiteral("w %1").arg(int(pe.w)));
      p.setFont(bold);
      p.drawText(QRect(r.x() + 2, r.y() + rowH, r.width() - 4,
                       r.height() - rowH - 2),
                 Qt::AlignCenter, QString::number(pe.acc));
    }
  }

  // Empty state.
  if (!m_snap.everStarted) {
    const QRect gr(o.x(), o.y(), n * cs, n * cs);
    p.setPen(Qt::NoPen);
    QColor veil = c.base;
    veil.setAlphaF(0.85f);
    p.setBrush(veil);
    p.drawRect(gr);
    p.setPen(c.text);
    p.setFont(font());
    p.drawText(gr.adjusted(6, 6, -6, -6),
               Qt::AlignCenter | Qt::TextWordWrap,
               tr("No work submitted yet.\nFill a descriptor in memory and "
                  "write TAIL (the doorbell) to start the unit."));
  }

  // Legend.
  int y = legendTop();
  const int sw = 14;
  const auto legendItem = [&](const QColor &fill, Qt::PenStyle style,
                              bool thick, const QString &text) {
    const QRect s(kArMargin, y + 3, sw, sw);
    p.setBrush(fill);
    p.setPen(QPen(thick ? c.text : c.grid, thick ? 2.0 : 1.0, style));
    p.drawRect(s);
    p.setPen(c.text);
    p.setFont(small);
    p.drawText(QRect(s.right() + 6, y, width() - s.right() - 12,
                     kArLegendRowH),
               Qt::AlignLeft | Qt::AlignVCenter, text);
    y += kArLegendRowH;
  };
  p.setFont(bold);
  p.setPen(c.text);
  p.drawText(QRect(kArMargin, y, width(), kArLegendRowH),
             Qt::AlignLeft | Qt::AlignVCenter, tr("Legend"));
  y += kArLegendRowH;
  {
    // Heat-map gradient for active cells.
    const QRect g(kArMargin, y + 3, 5 * sw, sw);
    QLinearGradient grad(g.topLeft(), g.topRight());
    grad.setColorAt(0, c.activeLow);
    grad.setColorAt(1, c.activeHigh);
    p.setBrush(grad);
    p.setPen(QPen(c.text, 2.0));
    p.drawRect(g);
    p.setPen(c.text);
    p.setFont(small);
    p.drawText(QRect(g.right() + 6, y, width() - g.right() - 12,
                     kArLegendRowH),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("MAC this cycle; darker = larger |acc| (heat map)"));
    y += kArLegendRowH;
  }
  legendItem(c.weight, Qt::SolidLine, false,
             tr("weight loaded, no activation this cycle"));
  legendItem(c.empty, Qt::DotLine, false,
             tr("empty / padding (outside the current tile)"));
  p.setPen(c.dim);
  p.setFont(small);
  p.drawText(QRect(kArMargin, y, width() - 2 * kArMargin, kArLegendRowH),
             Qt::AlignLeft | Qt::AlignVCenter,
             tr("Thick border = activation present. Cell text: a, w (top), "
                "acc (bottom). Hover a PE for details."));
}

// ===========================================================================
// IOTensorUnitView
// ===========================================================================

namespace {
enum RingCol {
  RC_SLOT,
  RC_ADDR,
  RC_ROLE,
  RC_OPCODE,
  RC_SRCA,
  RC_SRCB,
  RC_DST,
  RC_M,
  RC_N,
  RC_K,
  RC_FLAGS,
  RC_COUNT
};
constexpr unsigned kMaxRingRows = 64;

enum DmaRow {
  DR_IN,
  DR_OUT,
  DR_TOTAL,
  DR_TRANSFERS,
  DR_BANDWIDTH,
  DR_CYCLES,
  DR_BUSY,
  DR_FETCH,
  DR_DMAIN,
  DR_COMPUTE,
  DR_DMAOUT,
  DR_COMPLETE,
  DR_MACS,
  DR_UTIL,
  DR_DESCS,
  DR_COUNT
};
} // namespace

IOTensorUnitView::IOTensorUnitView(IOTensorUnit *unit)
    : QWidget(unit), m_unit(unit) {
  m_colors = NpuColors::fromPalette(palette());

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(6, 6, 6, 6);
  root->setSpacing(4);

  buildHeader();
  auto *headerRow = new QHBoxLayout();
  auto *titleCol = new QVBoxLayout();
  titleCol->setSpacing(1);
  titleCol->addWidget(m_title);
  titleCol->addWidget(m_subtitle);
  headerRow->addLayout(titleCol, 1);
  headerRow->addWidget(m_stateBadge, 0, Qt::AlignTop);
  headerRow->addWidget(m_helpButton, 0, Qt::AlignTop);
  root->addLayout(headerRow);
  root->addWidget(m_statusLine);
  root->addWidget(m_notice);

  m_stateView = new NpuStateMachineView(this);
  setTips(m_stateView,
          tr("Descriptor state machine. The current state is filled and "
             "marked with ▶; dashed arrows are the loops between tiles "
             "and descriptors. Hover a state for an explanation."));

  m_arrayView = new NpuArrayView();
  setTips(m_arrayView,
          tr("The N x N systolic array. Each PE shows a (activation), w "
             "(stationary weight) and acc (partial sum). Hover a PE for "
             "details."));
  m_arrayScroll = new QScrollArea(this);
  m_arrayScroll->setWidget(m_arrayView);
  m_arrayScroll->setWidgetResizable(false);
  m_arrayScroll->setFrameShape(QFrame::NoFrame);
  setTips(m_arrayScroll, m_arrayView->toolTip());

  m_tabs = new QTabWidget(this);
  setTips(m_tabs, tr("Details: descriptor ring, DMA and cycle statistics, "
                     "interrupt bits and the register table."));
  buildRingTab();
  buildDmaTab();
  buildIrqTab();
  buildRegisterTab();

  auto *lower = new QSplitter(Qt::Horizontal, this);
  m_lowerSplit = lower;
  lower->addWidget(m_arrayScroll);
  lower->addWidget(m_tabs);
  lower->setStretchFactor(0, 2);
  lower->setStretchFactor(1, 3);
  // Give the array view its natural width initially (the tabs scroll).
  lower->setSizes({m_arrayView->sizeHint().width() + 24, 600});
  lower->setChildrenCollapsible(false);
  setTips(lower, tr("Drag the handle to resize the array view and the "
                    "detail tabs."));

  auto *vsplit = new QSplitter(Qt::Vertical, this);
  auto *smScroll = new QScrollArea(this);
  smScroll->setWidget(m_stateView);
  smScroll->setWidgetResizable(true);
  smScroll->setFrameShape(QFrame::NoFrame);
  smScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  smScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  smScroll->setMinimumHeight(m_stateView->minimumSizeHint().height() + 4);
  vsplit->addWidget(smScroll);
  vsplit->addWidget(lower);
  vsplit->setStretchFactor(0, 0);
  vsplit->setStretchFactor(1, 1);
  vsplit->setChildrenCollapsible(false);
  root->addWidget(vsplit, 1);

  connect(ThemeManager::get(), &ThemeManager::themeChanged, this,
          &IOTensorUnitView::applyTheme);
  applyTheme();
}

// Small enough for the I/O tab at 1280x800: the header wraps and the lower
// panels stack when narrow (see resizeEvent).
QSize IOTensorUnitView::minimumSizeHint() const { return QSize(400, 480); }

void IOTensorUnitView::resizeEvent(QResizeEvent *e) {
  QWidget::resizeEvent(e);
  if (m_lowerSplit) {
    const int sideBySide = m_arrayView->sizeHint().width() + 24 + 360;
    const auto o =
        width() >= std::max(720, sideBySide) ? Qt::Horizontal : Qt::Vertical;
    if (m_lowerSplit->orientation() != o)
      m_lowerSplit->setOrientation(o);
  }
}
QSize IOTensorUnitView::sizeHint() const { return QSize(1000, 720); }

void IOTensorUnitView::buildHeader() {
  m_title = new QLabel(tr("Tensor Unit (NPU) — systolic-array "
                          "accelerator"),
                       this);
  QFont tf = m_title->font();
  tf.setBold(true);
  tf.setPointSizeF(tf.pointSizeF() * 1.2);
  m_title->setFont(tf);
  m_title->setWordWrap(true);
  setTips(m_title, tr("A teaching model of a TPU-like accelerator attached "
                      "to the CPU through memory-mapped registers."));

  m_subtitle = new QLabel(
      tr("The CPU writes descriptors into a ring in memory and rings the TAIL "
         "doorbell; the unit fetches each one, moves data by DMA, multiplies "
         "on the N x N array and advances HEAD."),
      this);
  m_subtitle->setWordWrap(true);
  setTips(m_subtitle, tr("What this peripheral does and what to look at."));

  m_stateBadge = new QLabel(this);
  m_stateBadge->setAlignment(Qt::AlignCenter);
  m_stateBadge->setMinimumWidth(130);
  m_stateBadge->setMargin(4);
  setTips(m_stateBadge,
          tr("Current state of the unit (also in STATUS[7:4]) and whether it "
             "is busy or reported an error. Note: after the doorbell and "
             "between two descriptors the unit spends one cycle in IDLE, so a "
             "loop polling STATUS.BUSY can exit too early - poll HEAD (or "
             "IRQ_STATUS) instead."));

  m_helpButton = new QToolButton(this);
  m_helpButton->setText(QStringLiteral("?"));
  QFont hf = m_helpButton->font();
  hf.setBold(true);
  m_helpButton->setFont(hf);
  setTips(m_helpButton,
          tr("Open the user guide chapter on the NPU (descriptor ring, "
             "doorbell, DMA, matmul walkthrough)."));
  connect(m_helpButton, &QToolButton::clicked, this,
          [] { Ripes::showHelp(QStringLiteral("10-npu")); });

  m_statusLine = new QLabel(this);
  m_statusLine->setWordWrap(true);
  setTips(m_statusLine,
          tr("Cycle counters (in CPU cycles) and how many cycles of NPU "
             "state can be undone with reverse execution."));

  m_notice = new QLabel(this);
  m_notice->setWordWrap(true);
  m_notice->setMargin(6);
  m_notice->setTextFormat(Qt::PlainText);
  setTips(m_notice, tr("Hints, errors and reverse-execution notices."));
}

void IOTensorUnitView::buildRingTab() {
  auto *page = new QWidget();
  auto *l = new QVBoxLayout(page);
  l->setContentsMargins(4, 4, 4, 4);
  m_ringSummary = new QLabel(page);
  m_ringSummary->setWordWrap(true);
  setTips(m_ringSummary,
          tr("QUEUE_BASE is the ring address, QUEUE_SIZE the number of "
             "32-byte slots; the unit executes slots HEAD..TAIL-1."));
  l->addWidget(m_ringSummary);

  m_ringTable = makeTable(
      {tr("Slot"), tr("Address"), tr("Pointer / status"), tr("Opcode"),
       tr("srcA"), tr("srcB"), tr("dst"), tr("M"), tr("N"), tr("K"),
       tr("Flags")},
      page);
  setTips(m_ringTable,
          tr("Every slot of the descriptor ring, decoded field by field. The "
             "slot in flight is highlighted. Slot contents are re-read from "
             "memory whenever the simulation is paused."));
  const QStringList headerTips = {
      tr("Slot index in the ring (0 .. QUEUE_SIZE-1)"),
      tr("Byte address of the slot: QUEUE_BASE + slot*32"),
      tr("HEAD = next slot the unit executes, TAIL = next free slot for the "
         "CPU"),
      tr("+0 opcode: 0 NOP, 1 MATMUL, 2 RELU, 3 ADD"),
      tr("+4 srcA: address of A (int8 MxK) or of the first int32 input"),
      tr("+8 srcB: address of B (int8 KxN) or of the second int32 input"),
      tr("+12 dst: address of the int32 result"),
      tr("+16 M: rows of A and C"),
      tr("+20 N: columns of B and C"),
      tr("+24 K: columns of A = rows of B (MATMUL only)"),
      tr("+28 flags: bit0 RELU_FUSED, bit1 NO_IRQ")};
  for (int i = 0; i < headerTips.size(); ++i) {
    m_ringTable->horizontalHeaderItem(i)->setToolTip(headerTips[i]);
    m_ringTable->horizontalHeaderItem(i)->setStatusTip(headerTips[i]);
  }
  l->addWidget(m_ringTable, 1);
  m_tabs->addTab(page, tr("Descriptor ring"));
  m_tabs->setTabToolTip(0, m_ringTable->toolTip());
}

void IOTensorUnitView::buildDmaTab() {
  auto *page = new QWidget();
  auto *l = new QVBoxLayout(page);
  l->setContentsMargins(4, 4, 4, 4);
  m_dmaLabel = new QLabel(page);
  setTips(m_dmaLabel, tr("Direction and progress of the DMA transfer in "
                         "flight."));
  l->addWidget(m_dmaLabel);
  m_dmaBar = new QProgressBar(page);
  m_dmaBar->setTextVisible(true);
  m_dmaBar->setRange(0, 1);
  m_dmaBar->setValue(0);
  setTips(m_dmaBar, tr("Bytes of the current DMA transfer moved so far "
                       "(bytes). The DMA moves 'DMA words/cycle' 32-bit "
                       "words per cycle."));
  l->addWidget(m_dmaBar);

  m_dmaTable = makeTable({tr("Quantity"), tr("Value"), tr("Meaning")}, page);
  m_dmaTable->setRowCount(DR_COUNT);
  setTips(m_dmaTable, tr("DMA traffic and cycle counters since the last "
                         "reset or CTRL.CLEAR_COUNTERS. All cycles are CPU "
                         "cycles."));
  l->addWidget(m_dmaTable, 1);
  m_tabs->addTab(page, tr("DMA && cycles"));
  m_tabs->setTabToolTip(1, m_dmaTable->toolTip());
}

void IOTensorUnitView::buildIrqTab() {
  auto *page = new QWidget();
  auto *l = new QVBoxLayout(page);
  l->setContentsMargins(4, 4, 4, 4);
  m_irqLine = new QLabel(page);
  m_irqLine->setMargin(4);
  setTips(m_irqLine, tr("The interrupt line is asserted while IRQ_STATUS & "
                        "IRQ_EN is non-zero."));
  l->addWidget(m_irqLine);

  m_irqTable = makeTable({tr("Bit"), tr("Name"), tr("IRQ_EN"),
                          tr("IRQ_STATUS"), tr("Meaning")},
                         page);
  m_irqTable->setRowCount(3);
  setTips(m_irqTable, tr("IRQ_EN (offset 0x14) enables a source; IRQ_STATUS "
                         "(offset 0x18) latches events until software "
                         "clears them."));
  l->addWidget(m_irqTable);

  auto *explain = new QLabel(
      tr("IRQ_STATUS is write-1-to-clear (W1C): writing 1 to a bit clears "
         "it, writing 0 leaves it unchanged, so an interrupt handler clears "
         "exactly the events it handled. The bits are sticky: they stay set "
         "until software clears them.\n\nSTATUS.ERROR is different: it "
         "describes only the most recent descriptor and clears itself when "
         "the next descriptor is fetched. Use IRQ_STATUS.ERROR to find out "
         "whether any descriptor failed since you last looked.\n\nNote: the "
         "Ripes RISC-V processors have no external interrupt input, so the "
         "CPU is never actually interrupted. The bits are observable only: "
         "poll IRQ_STATUS (or HEAD) to emulate an interrupt handler."),
      page);
  explain->setWordWrap(true);
  setTips(explain, tr("How the interrupt bits work and why the CPU must poll "
                      "them in Ripes."));
  l->addWidget(explain);
  l->addStretch(1);
  m_tabs->addTab(page, tr("Interrupts"));
  m_tabs->setTabToolTip(2, m_irqTable->toolTip());
}

void IOTensorUnitView::buildRegisterTab() {
  m_regTable = makeTable({tr("Name"), tr("Offset"), tr("Address"),
                          tr("Access"), tr("Value"), tr("Meaning")},
                         this);
  m_regTable->setRowCount(IOTensorUnit::NREGISTERS);
  setTips(m_regTable,
          tr("Memory-mapped registers: name, byte offset from the "
             "peripheral base, absolute address, access (R/W/RW), current "
             "value and its decoded meaning."));
  m_tabs->addTab(m_regTable, tr("Registers"));
  m_tabs->setTabToolTip(3, m_regTable->toolTip());
}

void IOTensorUnitView::applyTheme() {
  m_colors = NpuColors::fromPalette(palette());
  if (m_haveData)
    refresh(m_last);
}

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

QString IOTensorUnitView::opcodeName(uint32_t opcode) const {
  switch (static_cast<npu::Opcode>(opcode)) {
  case npu::Opcode::NOP:
    return QStringLiteral("NOP (0)");
  case npu::Opcode::MATMUL:
    return QStringLiteral("MATMUL (1)");
  case npu::Opcode::RELU:
    return QStringLiteral("RELU (2)");
  case npu::Opcode::ADD:
    return QStringLiteral("ADD (3)");
  }
  return tr("invalid (%1)").arg(opcode);
}

QString IOTensorUnitView::flagsText(uint32_t flags) const {
  QStringList parts;
  if (flags & npu::DescFlags::RELU_FUSED)
    parts << QStringLiteral("RELU_FUSED");
  if (flags & npu::DescFlags::NO_IRQ)
    parts << QStringLiteral("NO_IRQ");
  if (flags & ~(npu::DescFlags::RELU_FUSED | npu::DescFlags::NO_IRQ))
    parts << tr("unknown bits");
  return parts.isEmpty() ? QStringLiteral("-") : parts.join(QLatin1Char('|'));
}

QString IOTensorUnitView::irqBitsText(uint32_t bits) const {
  QStringList parts;
  if (bits & npu::Irq::DESC_DONE)
    parts << QStringLiteral("DESC_DONE");
  if (bits & npu::Irq::QUEUE_EMPTY)
    parts << QStringLiteral("QUEUE_EMPTY");
  if (bits & npu::Irq::ERROR)
    parts << QStringLiteral("ERROR");
  return parts.isEmpty() ? tr("none") : parts.join(QLatin1Char('|'));
}

QString IOTensorUnitView::statusText(uint32_t st) const {
  QStringList parts;
  parts << ((st & npu::Status::BUSY) ? QStringLiteral("BUSY")
                                     : tr("not busy"));
  const auto state = static_cast<npu::State>(
      (st & npu::Status::STATE_MASK) >> npu::Status::STATE_SHIFT);
  parts << tr("state=%1").arg(QString::fromLatin1(npu::stateName(state)));
  if (st & npu::Status::QUEUE_EMPTY)
    parts << QStringLiteral("QUEUE_EMPTY");
  if (st & npu::Status::ERROR)
    parts << QStringLiteral("ERROR");
  if (st & npu::Status::WEIGHT_LOAD)
    parts << QStringLiteral("WEIGHT_LOAD");
  if (st & npu::Status::DMA_FAULT)
    parts << QStringLiteral("DMA_FAULT");
  return parts.join(QStringLiteral(", "));
}

QString IOTensorUnitView::errorText(const npu::Snapshot &s) const {
  switch (s.errorCause) {
  case npu::ErrorCause::NONE:
    return QString();
  case npu::ErrorCause::BAD_OPCODE:
    return tr("Error: the last descriptor had an invalid opcode (%1). It "
              "was skipped; HEAD still advanced.")
        .arg(s.descriptor.opcode);
  case npu::ErrorCause::BAD_DIMENSION:
    return tr("Error: the last descriptor had M, N or K outside 1..%1 "
              "(M=%2, N=%3, K=%4). It was skipped; HEAD still advanced.")
        .arg(npu::TensorUnit::kMaxDim)
        .arg(s.descriptor.M)
        .arg(s.descriptor.N)
        .arg(s.descriptor.K);
  case npu::ErrorCause::BUS_FAULT:
    return tr("Error: DMA fault at address %1. The unit cannot access its "
              "own registers (or an unmapped region) by DMA - check "
              "QUEUE_BASE and the descriptor addresses. The descriptor was "
              "aborted; HEAD still advanced.")
        .arg(hex32(s.faultAddr));
  }
  return QString();
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void IOTensorUnitView::refresh(const IOTensorUnit::ViewData &data) {
  m_last = data;
  m_haveData = true;
  updateHeader(data);
  updateNotice(data);
  m_stateView->setData(data.snap, m_colors);
  m_arrayView->setData(data.snap, data.cellSize, m_colors);
  updateRing(data.snap);
  updateDma(data.snap);
  updateIrq(data.snap);
  updateRegisters(data.snap);
}

void IOTensorUnitView::updateHeader(const IOTensorUnit::ViewData &d) {
  const auto &s = d.snap;
  const bool error = (s.status & npu::Status::ERROR) != 0;
  const bool busy = s.state != npu::State::IDLE;
  QString badge = QString::fromLatin1(npu::stateName(s.state));
  QColor bg;
  if (busy) {
    badge += QStringLiteral("\n") + tr("BUSY");
    bg = m_colors.highlight;
  } else if (error) {
    badge += QStringLiteral("\n") + tr("ERROR");
    bg = m_colors.error;
  } else {
    badge += QStringLiteral("\n") +
             (s.everStarted ? tr("done, idle") : tr("idle, no work yet"));
    bg = mix(m_colors.base, m_colors.ok, 0.25);
  }
  if (busy && error)
    badge += QStringLiteral(" / ") + tr("ERROR");
  const QColor fg = npuContrastText(bg);
  m_stateBadge->setText(badge);
  m_stateBadge->setStyleSheet(
      QStringLiteral("QLabel { background-color: %1; color: %2; "
                     "border-radius: 4px; font-weight: bold; }")
          .arg(bg.name(), fg.name()));

  const double cells = double(s.arrayN) * double(s.arrayN);
  const double util =
      (s.counters.busyCycles > 0 && cells > 0)
          ? 100.0 * double(s.counters.macs) /
                (double(s.counters.busyCycles) * cells)
          : 0.0;
  m_statusLine->setText(
      tr("Cycles: %1  |  Busy cycles: %2  |  MACs: %3  |  Array "
         "utilization: %4 %  |  Reverse history: %5 cycles (%6 KiB)")
          .arg(s.counters.cycles)
          .arg(s.counters.busyCycles)
          .arg(s.counters.macs)
          .arg(util, 0, 'f', 1)
          .arg(d.historyFrames)
          .arg(double(d.historyBytes) / 1024.0, 0, 'f', 1));
}

void IOTensorUnitView::updateNotice(const IOTensorUnit::ViewData &d) {
  const auto &s = d.snap;
  QString text;
  QColor col;
  if (d.reverseNotice == IOTensorUnit::ReverseNotice::Exhausted) {
    text = tr("Reverse execution went further back than the NPU history "
              "(limit: the 'rewind stack size' setting, or the unit was "
              "created/reset later). The NPU state was NOT rewound for this "
              "step; reset the processor to start over cleanly.");
    col = m_colors.warn;
  } else if (s.status & npu::Status::ERROR) {
    text = errorText(s) + QStringLiteral(" ") +
           tr("(STATUS.ERROR clears itself when the next descriptor is "
              "fetched; IRQ_STATUS.ERROR stays set until software writes 1 "
              "to it.)");
    col = m_colors.error;
  } else if (d.reverseNotice == IOTensorUnit::ReverseNotice::Rewound) {
    text = tr("Reversed one cycle: the NPU state and the memory written by "
              "its DMA in that cycle were rewound together with the CPU.");
    col = m_colors.info;
  } else if (!s.everStarted) {
    if (s.queueSize == 0)
      text = tr("No work submitted yet. Set QUEUE_BASE and QUEUE_SIZE, "
                "write a 32-byte descriptor into slot TAIL, then write "
                "TAIL+1 to TAIL (the doorbell). Try the example "
                "npu_matmul.s.");
    else
      text = tr("Ring configured (%1 slots at %2). Write a descriptor into "
                "slot %3 and then write %4 to TAIL to ring the doorbell.")
                 .arg(s.queueSize)
                 .arg(hex32(s.queueBase))
                 .arg(s.tail)
                 .arg((s.tail + 1) % s.queueSize);
    col = m_colors.info;
  }
  m_notice->setVisible(!text.isEmpty());
  if (text.isEmpty())
    return;
  if (m_notice->text() != text)
    m_notice->setText(text);
  const QColor bg = mix(m_colors.base, col, 0.18);
  m_notice->setStyleSheet(
      QStringLiteral("QLabel { background-color: %1; color: %2; border: 1px "
                     "solid %3; border-radius: 4px; }")
          .arg(bg.name(), m_colors.text.name(), col.name()));
}

void IOTensorUnitView::updateRing(const npu::Snapshot &s) {
  const unsigned size = s.queueSize;
  const unsigned rows = std::min(size, kMaxRingRows);
  const unsigned pending = size ? (s.tail + size - s.head) % size : 0;
  QString summary =
      tr("QUEUE_BASE = %1   QUEUE_SIZE = %2 slots   HEAD = %3 (next to "
         "execute)   TAIL = %4 (next free)   pending = %5")
          .arg(hex32(s.queueBase))
          .arg(size)
          .arg(s.head)
          .arg(s.tail)
          .arg(pending);
  if (size > kMaxRingRows)
    summary += QStringLiteral("\n") +
               tr("(showing the first %1 slots)").arg(kMaxRingRows);
  QString warning;
  if (size == 0)
    warning = tr("Warning: QUEUE_SIZE is 0 - descriptor fetch is disabled "
                 "and the doorbell is ignored.");
  else if (size == 1)
    warning = tr("Warning: QUEUE_SIZE is 1 - one slot always stays empty "
                 "(HEAD == TAIL means empty), so nothing can be submitted. "
                 "Use at least 2 slots.");
  else if (pending == size - 1)
    warning = tr("Ring full: %1 of %2 slots pending. Do not submit more until "
                 "HEAD advances - writing TAIL onto HEAD would make the ring "
                 "look empty and the queued work would be lost.")
                  .arg(pending)
                  .arg(size);
  if (!warning.isEmpty())
    summary += QStringLiteral("\n") + warning;
  if (m_ringSummary->text() != summary)
    m_ringSummary->setText(summary);
  m_ringSummary->setStyleSheet(
      warning.isEmpty()
          ? QString()
          : QStringLiteral("QLabel { color: %1; font-weight: bold; }")
                .arg(m_colors.warn.name()));

  // Re-read slot contents from memory only while the processor is paused;
  // during a run the memory belongs to the processor thread.
  if (m_ringCache.size() != rows || m_ringCacheBase != s.queueBase) {
    m_ringCache.assign(rows, RingSlot());
    m_ringCacheBase = s.queueBase;
  }
  if (!ProcessorHandler::isRunning() && ProcessorHandler::get()) {
    auto &mem = ProcessorHandler::getMemory();
    for (unsigned i = 0; i < rows; ++i) {
      const uint32_t addr = s.queueBase + i * npu::kDescriptorBytes;
      RingSlot &slot = m_ringCache[i];
      slot.mmio = mem.regionType(addr) ==
                  vsrtl::core::AddressSpace::RegionType::IO;
      if (slot.mmio) {
        slot.desc.reset();
        continue;
      }
      npu::Descriptor d;
      for (unsigned w = 0; w < npu::kDescriptorWords; ++w)
        d.setWord(w, static_cast<uint32_t>(mem.readMemConst(addr + w * 4, 4)));
      slot.desc = d;
    }
  }

  if (m_ringTable->rowCount() != static_cast<int>(rows))
    m_ringTable->setRowCount(static_cast<int>(rows));
  const bool inFlight = s.state != npu::State::IDLE && s.hasDescriptor;
  for (unsigned i = 0; i < rows; ++i) {
    const int r = static_cast<int>(i);
    const bool isHead = i == s.head;
    const bool isTail = i == s.tail;
    const bool isPending =
        size && ((i + size - s.head) % size) < pending;
    const bool flight = inFlight && isHead;

    QStringList role;
    if (isHead)
      role << QStringLiteral("HEAD");
    if (isTail)
      role << QStringLiteral("TAIL");
    QString status;
    if (flight)
      status = tr("▶ in flight");
    else if (isPending)
      status = tr("pending");
    else
      status = tr("free / done");
    const QString roleText =
        role.isEmpty() ? status : role.join(QLatin1Char('+')) + " - " + status;

    QColor bg, fg;
    if (flight) {
      bg = m_colors.highlight;
      fg = m_colors.highlightText;
    } else if (isPending) {
      bg = mix(m_colors.base, m_colors.highlight, 0.18);
    }
    const uint32_t addr = s.queueBase + i * npu::kDescriptorBytes;
    setCell(m_ringTable, r, RC_SLOT, QString::number(i), QString(), bg, fg,
            flight);
    setCell(m_ringTable, r, RC_ADDR, hex32(addr), QString(), bg, fg);
    setCell(m_ringTable, r, RC_ROLE, roleText,
            tr("HEAD: next slot the unit executes. TAIL: next free slot (the "
               "CPU writes here, then rings the doorbell)."),
            bg, fg, isHead || isTail);

    // Contents: the descriptor in flight comes from the unit itself (exactly
    // what it fetched); the others from memory.
    std::optional<npu::Descriptor> d;
    if (flight)
      d = s.descriptor;
    else if (i < m_ringCache.size())
      d = m_ringCache[i].desc;
    if (!d) {
      const QString why = (i < m_ringCache.size() && m_ringCache[i].mmio)
                              ? tr("MMIO region - not a valid ring address")
                              : tr("(read when paused)");
      setCell(m_ringTable, r, RC_OPCODE, why, QString(), bg,
              (i < m_ringCache.size() && m_ringCache[i].mmio)
                  ? m_colors.error
                  : fg);
      for (int c = RC_SRCA; c < RC_COUNT; ++c)
        setCell(m_ringTable, r, c, QString(), QString(), bg, fg);
      continue;
    }
    const bool validOp = d->opcode <= 3;
    setCell(m_ringTable, r, RC_OPCODE, opcodeName(d->opcode), QString(), bg,
            validOp ? fg : m_colors.error);
    setCell(m_ringTable, r, RC_SRCA, hex32(d->srcA), QString(), bg, fg);
    setCell(m_ringTable, r, RC_SRCB, hex32(d->srcB), QString(), bg, fg);
    setCell(m_ringTable, r, RC_DST, hex32(d->dst), QString(), bg, fg);
    setCell(m_ringTable, r, RC_M, QString::number(d->M), QString(), bg, fg);
    setCell(m_ringTable, r, RC_N, QString::number(d->N), QString(), bg, fg);
    setCell(m_ringTable, r, RC_K, QString::number(d->K), QString(), bg, fg);
    setCell(m_ringTable, r, RC_FLAGS, flagsText(d->flags), QString(), bg, fg);
  }
}

void IOTensorUnitView::updateDma(const npu::Snapshot &s) {
  const bool active =
      s.state == npu::State::DMA_IN || s.state == npu::State::DMA_OUT ||
      s.state == npu::State::FETCH_DESC;
  QString label;
  if (s.state == npu::State::FETCH_DESC)
    label = tr("DMA: fetching a 32-byte descriptor (host → NPU)");
  else if (s.state == npu::State::DMA_IN)
    label = tr("DMA_IN: host memory → NPU buffers, %1 of %2 bytes")
                .arg(s.dma.bytesDone)
                .arg(s.dma.bytesTotal);
  else if (s.state == npu::State::DMA_OUT)
    label = tr("DMA_OUT: NPU results → host memory, %1 of %2 bytes")
                .arg(s.dma.bytesDone)
                .arg(s.dma.bytesTotal);
  else
    label = tr("DMA idle (last transfer: %1 bytes %2)")
                .arg(s.dma.bytesTotal)
                .arg(s.dma.writing ? tr("out") : tr("in"));
  if (m_dmaLabel->text() != label)
    m_dmaLabel->setText(label);

  const int total = static_cast<int>(std::max<uint32_t>(1, s.dma.bytesTotal));
  m_dmaBar->setRange(0, total);
  m_dmaBar->setValue(static_cast<int>(std::min<uint32_t>(
      s.dma.bytesDone, static_cast<uint32_t>(total))));
  m_dmaBar->setFormat(tr("%v / %m bytes"));
  QPalette bp = m_dmaBar->palette();
  bp.setColor(QPalette::Highlight,
              s.dma.writing ? m_colors.dmaOut : m_colors.dmaIn);
  m_dmaBar->setPalette(bp);
  m_dmaBar->setEnabled(active || s.dma.bytesDone > 0);

  const auto &c = s.counters;
  const double cells = double(s.arrayN) * double(s.arrayN);
  const double util = (c.busyCycles > 0 && cells > 0)
                          ? 100.0 * double(c.macs) /
                                (double(c.busyCycles) * cells)
                          : 0.0;
  const double busyPct =
      c.cycles ? 100.0 * double(c.busyCycles) / double(c.cycles) : 0.0;
  const auto cyc = [&](npu::State st) {
    return tr("%1 cycles").arg(c.stateCycles[static_cast<unsigned>(st)]);
  };
  struct Row {
    QString name, value, meaning;
  };
  const Row rows[DR_COUNT] = {
      {tr("DMA bytes in"), tr("%1 bytes").arg(c.dmaBytesIn),
       tr("host memory → NPU: descriptors, A/B tiles, vector inputs")},
      {tr("DMA bytes out"), tr("%1 bytes").arg(c.dmaBytesOut),
       tr("NPU → host memory: int32 results")},
      {tr("DMA_BYTES (total)"), tr("%1 bytes").arg(c.dmaBytes),
       tr("register 0x20: in + out")},
      {tr("DMA transfers"), QString::number(c.dmaTransfers),
       tr("jobs started: descriptor fetches, tile loads, write-backs")},
      {tr("DMA bandwidth"),
       tr("%1 words/cycle = %2 bytes/cycle")
           .arg(s.dmaWordsPerCycle)
           .arg(s.dmaWordsPerCycle * 4),
       tr("parameter 'DMA words/cycle'")},
      {tr("CYCLES"), tr("%1 cycles").arg(c.cycles),
       tr("register 0x1C: every CPU cycle since reset")},
      {tr("BUSY_CYCLES"),
       tr("%1 cycles (%2 %)").arg(c.busyCycles).arg(busyPct, 0, 'f', 1),
       tr("register 0x2C: cycles not in IDLE")},
      {tr("  in FETCH_DESC"), cyc(npu::State::FETCH_DESC),
       tr("reading descriptors")},
      {tr("  in DMA_IN"), cyc(npu::State::DMA_IN), tr("loading inputs")},
      {tr("  in COMPUTE"), cyc(npu::State::COMPUTE),
       tr("weight load + systolic passes (or SIMD for RELU/ADD)")},
      {tr("  in DMA_OUT"), cyc(npu::State::DMA_OUT), tr("writing results")},
      {tr("  in COMPLETE"), cyc(npu::State::COMPLETE),
       tr("HEAD++ and interrupt bits")},
      {tr("MACS"), tr("%1 MACs").arg(c.macs),
       tr("register 0x24: useful int8 multiply-accumulates")},
      {tr("Array utilization"), tr("%1 %").arg(util, 0, 'f', 1),
       tr("MACS / (BUSY_CYCLES x N²); low = memory-bound")},
      {tr("Descriptors completed"),
       tr("%1 (%2 with error)").arg(c.descriptorsDone).arg(c.errors),
       tr("including rejected / aborted ones")},
  };
  for (int r = 0; r < DR_COUNT; ++r) {
    setCell(m_dmaTable, r, 0, rows[r].name, rows[r].meaning);
    setCell(m_dmaTable, r, 1, rows[r].value, rows[r].meaning);
    setCell(m_dmaTable, r, 2, rows[r].meaning, rows[r].meaning);
  }
  if (m_dmaTable->columnWidth(0) < 60)
    m_dmaTable->resizeColumnsToContents();
}

void IOTensorUnitView::updateIrq(const npu::Snapshot &s) {
  const bool asserted = (s.irqEnable & s.irqStatus) != 0;
  const QString line =
      asserted ? tr("Interrupt line: ASSERTED (IRQ_STATUS & IRQ_EN = %1)")
                     .arg(irqBitsText(s.irqEnable & s.irqStatus))
               : tr("Interrupt line: not asserted (IRQ_STATUS & IRQ_EN = 0)");
  if (m_irqLine->text() != line)
    m_irqLine->setText(line);
  const QColor bg = asserted ? m_colors.warn : mix(m_colors.base, m_colors.ok,
                                                   0.2);
  m_irqLine->setStyleSheet(
      QStringLiteral("QLabel { background-color: %1; color: %2; "
                     "border-radius: 4px; font-weight: bold; }")
          .arg(bg.name(), npuContrastText(bg).name()));

  struct Bit {
    uint32_t mask;
    const char *name;
    QString meaning;
  };
  const Bit bits[3] = {
      {npu::Irq::DESC_DONE, "DESC_DONE",
       tr("a descriptor completed (not set when flags.NO_IRQ)")},
      {npu::Irq::QUEUE_EMPTY, "QUEUE_EMPTY",
       tr("the last pending descriptor completed (HEAD == TAIL)")},
      {npu::Irq::ERROR, "ERROR",
       tr("a descriptor was rejected or aborted by a DMA fault")}};
  for (int r = 0; r < 3; ++r) {
    const bool en = s.irqEnable & bits[r].mask;
    const bool st = s.irqStatus & bits[r].mask;
    const QColor rowBg =
        (en && st) ? mix(m_colors.base, m_colors.warn, 0.35) : QColor();
    setCell(m_irqTable, r, 0, QString::number(r), bits[r].meaning, rowBg);
    setCell(m_irqTable, r, 1, QString::fromLatin1(bits[r].name),
            bits[r].meaning, rowBg, QColor(), true);
    setCell(m_irqTable, r, 2, en ? tr("1 enabled") : tr("0 disabled"),
            tr("IRQ_EN bit %1: write 1 to enable this source").arg(r), rowBg);
    setCell(m_irqTable, r, 3, st ? tr("1 pending") : tr("0 clear"),
            tr("IRQ_STATUS bit %1: set by the unit; write 1 to clear (W1C)")
                .arg(r),
            st ? mix(m_colors.base, m_colors.warn, 0.35) : rowBg);
    setCell(m_irqTable, r, 4, bits[r].meaning, bits[r].meaning, rowBg);
  }
  if (m_irqTable->columnWidth(1) < 80)
    m_irqTable->resizeColumnsToContents();
}

void IOTensorUnitView::updateRegisters(const npu::Snapshot &s) {
  const AInt base = m_unit->baseAddress();
  const unsigned size = s.queueSize;
  const unsigned pending = size ? (s.tail + size - s.head) % size : 0;
  const double busyPct =
      s.counters.cycles
          ? 100.0 * double(s.counters.busyCycles) / double(s.counters.cycles)
          : 0.0;
  struct Reg {
    const char *name;
    const char *access;
    QString value;
    QString meaning;
  };
  const Reg regs[IOTensorUnit::NREGISTERS] = {
      {"QUEUE_BASE", "RW", hex32(s.queueBase),
       tr("address of the descriptor ring in memory")},
      {"QUEUE_SIZE", "RW", QString::number(size),
       tr("%1 slots x 32 bytes (0 = ring disabled)").arg(size)},
      {"HEAD", "R", QString::number(s.head),
       tr("next slot the unit executes; advances after each descriptor")},
      {"TAIL", "RW", QString::number(s.tail),
       tr("next free slot; writing it is the doorbell (%1 pending)")
           .arg(pending)},
      {"STATUS", "R", hex32(s.status),
       statusText(s.status) +
           tr(" (ERROR clears on the next fetch; BUSY is 0 for one cycle "
              "after the doorbell and between descriptors - poll HEAD)")},
      {"IRQ_EN", "RW", hex32(s.irqEnable),
       tr("enabled sources: %1").arg(irqBitsText(s.irqEnable))},
      {"IRQ_STATUS", "RW (W1C)", hex32(s.irqStatus),
       tr("latched events: %1; sticky, write 1 to clear; observable only "
          "(the CPU is never interrupted)")
           .arg(irqBitsText(s.irqStatus))},
      {"CYCLES", "R", QString::number(s.counters.cycles & 0xFFFFFFFFu),
       tr("cycles since reset (free-running CPU cycle counter)")},
      {"DMA_BYTES", "R", QString::number(s.counters.dmaBytes & 0xFFFFFFFFu),
       tr("bytes moved by DMA (in + out)")},
      {"MACS", "R", QString::number(s.counters.macs & 0xFFFFFFFFu),
       tr("useful int8 multiply-accumulates")},
      {"ARRAY_N", "R", QString::number(s.arrayN),
       tr("array is %1 x %1 = %2 MACs/cycle peak")
           .arg(s.arrayN)
           .arg(s.arrayN * s.arrayN)},
      {"BUSY_CYCLES", "R",
       QString::number(s.counters.busyCycles & 0xFFFFFFFFu),
       tr("cycles not in IDLE (%1 % of all cycles)")
           .arg(busyPct, 0, 'f', 1)},
      {"CTRL", "W", QStringLiteral("-"),
       tr("write-only: bit0 SOFT_RESET, bit1 CLEAR_COUNTERS")},
      {"FAULT_ADDR", "R", hex32(s.faultAddr),
       s.errorCause == npu::ErrorCause::BUS_FAULT
           ? tr("address of the refused DMA access")
           : tr("no DMA fault since the last descriptor fetch")}};
  for (int r = 0; r < IOTensorUnit::NREGISTERS; ++r) {
    const uint32_t off = static_cast<uint32_t>(r) * 4;
    setCell(m_regTable, r, 0, QString::fromLatin1(regs[r].name),
            regs[r].meaning, QColor(), QColor(), true);
    setCell(m_regTable, r, 1,
            QStringLiteral("0x%1").arg(off, 2, 16, QLatin1Char('0')));
    setCell(m_regTable, r, 2,
            base ? hex32(static_cast<uint32_t>(base + off)) : tr("unmapped"));
    setCell(m_regTable, r, 3, QString::fromLatin1(regs[r].access));
    setCell(m_regTable, r, 4, regs[r].value, regs[r].meaning);
    setCell(m_regTable, r, 5, regs[r].meaning, regs[r].meaning);
  }
  if (m_regTable->columnWidth(0) < 70)
    m_regTable->resizeColumnsToContents();
}

} // namespace Ripes
