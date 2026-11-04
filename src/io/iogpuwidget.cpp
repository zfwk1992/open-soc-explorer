#include "iogpuwidget.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QResizeEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <map>

#include "help.h"
#include "tabheader.h" // Ripes::setTips

#include "ripessettings.h"

namespace Ripes {

namespace {
constexpr int kMargin = 6;
constexpr int kLane = 14;
constexpr int kLaneGap = 3;
constexpr int kSmGap = 8;

QFont monoFont() {
  QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  f.setPointSize(9);
  return f;
}

QString hex32(uint32_t v) {
  return QStringLiteral("0x%1").arg(v, 8, 16, QChar('0'));
}

QColor mix(const QColor &a, const QColor &b, double t) {
  return QColor::fromRgbF(
      static_cast<float>(a.redF() * (1 - t) + b.redF() * t),
      static_cast<float>(a.greenF() * (1 - t) + b.greenF() * t),
      static_cast<float>(a.blueF() * (1 - t) + b.blueF() * t));
}
} // namespace

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

GpuColors GpuColors::fromPalette(const QPalette &pal) {
  GpuColors c;
  const QColor base = pal.color(QPalette::Base);
  const bool dark = pal.color(QPalette::Window).lightness() < 128;
  c.text = pal.color(QPalette::WindowText);
  c.mutedText = mix(c.text, pal.color(QPalette::Window), 0.4);
  c.frame = pal.color(QPalette::Mid);
  // Semantic hues, toned for the current background so that both themes
  // keep enough contrast. Shapes (hatching, X, pause bars) carry the same
  // information for colour-blind users.
  c.active = dark ? QColor(102, 187, 106) : QColor(46, 125, 50);
  c.masked = dark ? QColor(160, 160, 160) : QColor(97, 97, 97);
  c.maskedFill = mix(base, c.masked, 0.35);
  c.parked = dark ? QColor(255, 183, 77) : QColor(230, 81, 0);
  c.exited = dark ? QColor(239, 83, 80) : QColor(198, 40, 40);
  c.executed = pal.color(QPalette::Highlight);
  if (c.executed.lightness() < 60 && dark)
    c.executed = c.executed.lighter(170);
  c.divergedRow = mix(base, c.parked, dark ? 0.22 : 0.16);
  c.issueRow = mix(base, pal.color(QPalette::Highlight), dark ? 0.18 : 0.10);
  c.pcRow = mix(base, pal.color(QPalette::Highlight), dark ? 0.35 : 0.22);
  c.idle = c.masked;
  c.busy = dark ? QColor(66, 165, 245) : QColor(21, 101, 192);
  c.done = c.active;
  c.error = c.exited;
  c.badgeText = dark ? QColor(20, 20, 20) : QColor(255, 255, 255);
  return c;
}

// ---------------------------------------------------------------------------
// GpuWarpView
// ---------------------------------------------------------------------------

GpuWarpView::GpuWarpView(QWidget *parent) : QWidget(parent) {
  setFont(monoFont());
  setMouseTracking(true);
  setToolTip(tr("Warp view: one row per warp slot. Hover a lane square to see "
                "its thread id, state and registers."));
  setStatusTip(tr("SM / warp / lane view of the running kernel"));
}

void GpuWarpView::setView(std::shared_ptr<const IOGpu::View> view,
                          const GpuColors &c) {
  m_view = std::move(view);
  m_colors = c;
  relayout();
  update();
}

void GpuWarpView::relayout() {
  const QFontMetrics fm(font());
  m_labelW = fm.horizontalAdvance(QStringLiteral("W00 B0000.00 "));
  m_instrW = fm.horizontalAdvance(QStringLiteral("pc 0000 ")) +
             fm.horizontalAdvance(QStringLiteral("@!P3 SETP.GEU P3, R15, -512 "));
  m_statusW = fm.horizontalAdvance(
      tr("waiting at BAR") + QStringLiteral(" · ") + tr("stall %1").arg(99) +
      QStringLiteral(" · ") + tr("diverged: %1 paths").arg(9) +
      QStringLiteral("  "));
  int w = 2 * kMargin + 400, h = 2 * kMargin + 40;
  if (m_view) {
    const unsigned ws = m_view->config.warpSize;
    const int rowH = std::max(fm.height(), kLane) + 6;
    w = 2 * kMargin + 12 + m_labelW + m_instrW +
        static_cast<int>(ws) * (kLane + kLaneGap) + m_statusW;
    w = std::max(w, 2 * kMargin + fm.horizontalAdvance(
                                      tr("SM %1 · %2 resident block(s) · "
                                         "issued %3 · idle %4 · issue "
                                         "utilization %5%")
                                          .arg(9)
                                          .arg(9)
                                          .arg(99999)
                                          .arg(99999)
                                          .arg(100.0, 0, 'f', 1)) +
                        20);
    h = kMargin;
    for (const auto &sm : m_view->sms)
      h += fm.height() + 8 + static_cast<int>(sm.warps.size()) * rowH +
           kSmGap;
    h += kMargin;
  }
  m_contentSize = QSize(w, h);
  setMinimumSize(m_contentSize);
  updateGeometry();
}

QSize GpuWarpView::sizeHint() const { return m_contentSize; }

void GpuWarpView::paintLane(QPainter &p, const QRect &r, LaneState s,
                            bool executed, const GpuColors &c) {
  p.save();
  switch (s) {
  case LaneState::NoThread: {
    QPen pen(c.frame, 1, Qt::DotLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    break;
  }
  case LaneState::Active:
    p.setPen(Qt::NoPen);
    p.setBrush(c.active);
    p.drawRect(r);
    break;
  case LaneState::Masked:
    p.setPen(Qt::NoPen);
    p.setBrush(c.maskedFill);
    p.drawRect(r);
    p.setPen(QPen(c.masked, 1));
    p.setBrush(QBrush(c.masked, Qt::BDiagPattern));
    p.drawRect(r.adjusted(0, 0, -1, -1));
    break;
  case LaneState::Parked: {
    p.setPen(Qt::NoPen);
    p.setBrush(c.parked);
    p.drawRect(r);
    // "pause" bars
    p.setBrush(c.badgeText);
    const int bw = std::max(2, r.width() / 6);
    const int bh = r.height() / 2;
    const int y = r.top() + (r.height() - bh) / 2;
    p.drawRect(r.left() + r.width() / 2 - bw - 1, y, bw, bh);
    p.drawRect(r.left() + r.width() / 2 + 1, y, bw, bh);
    break;
  }
  case LaneState::Exited: {
    p.setPen(Qt::NoPen);
    p.setBrush(c.exited);
    p.drawRect(r);
    p.setPen(QPen(c.badgeText, 1.6));
    const QRect in = r.adjusted(3, 3, -3, -3);
    p.drawLine(in.topLeft(), in.bottomRight());
    p.drawLine(in.topRight(), in.bottomLeft());
    break;
  }
  }
  if (executed) {
    p.setPen(QPen(c.executed, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(-2, -2, 1, 1));
  }
  p.restore();
}

QString GpuWarpView::instrAt(uint32_t pc) const {
  if (!m_view || !m_view->kernelWords)
    return QString();
  const auto &words = *m_view->kernelWords;
  if (pc >= words.size())
    return QStringLiteral("?");
  return QString::fromStdString(gpu::disassemble(words[pc]));
}

void GpuWarpView::paintEvent(QPaintEvent *) {
  m_hits.clear();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, false);
  if (!m_view)
    return;
  const auto &v = *m_view;
  const GpuColors &c = m_colors;
  const QFontMetrics fm(font());
  const int rowH = std::max(fm.height(), kLane) + 6;
  const unsigned ws = v.config.warpSize;
  const int boxW = width() - 2 * kMargin;

  int y = kMargin;
  for (unsigned smIdx = 0; smIdx < v.sms.size(); ++smIdx) {
    const auto &sm = v.sms[smIdx];
    const int titleH = fm.height() + 8;
    const int boxH = titleH + static_cast<int>(sm.warps.size()) * rowH + 4;
    p.setPen(QPen(c.frame, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRect(kMargin, y, boxW, boxH), 4, 4);

    const uint64_t issueSlots = sm.issued + sm.idleSlots;
    const double util = issueSlots ? 100.0 * double(sm.issued) / double(issueSlots) : 0;
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(c.text);
    p.drawText(QRect(kMargin + 6, y + 2, boxW - 12, titleH - 4),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("SM %1 · %2 resident block(s) · issued %3 · idle %4 · "
                  "issue utilization %5%")
                   .arg(smIdx)
                   .arg(sm.residentBlocks)
                   .arg(sm.issued)
                   .arg(sm.idleSlots)
                   .arg(util, 0, 'f', 1));
    p.setFont(font());

    int ry = y + titleH;
    for (unsigned wi = 0; wi < sm.warps.size(); ++wi) {
      const auto &w = sm.warps[wi];
      const QRect row(kMargin + 2, ry, boxW - 4, rowH);
      const bool diverged = w.valid && w.paths.size() > 1;
      const bool issuedNow = w.valid && sm.issuedWarp == static_cast<int>(wi);
      if (diverged)
        p.fillRect(row, c.divergedRow);
      if (issuedNow) {
        if (!diverged)
          p.fillRect(row, c.issueRow);
        p.fillRect(QRect(row.left(), row.top() + 1, 4, row.height() - 2),
                   c.executed);
      }
      int x = kMargin + 6;

      // Identity
      p.setPen(w.valid ? c.text : c.mutedText);
      const QString label = w.valid ? QStringLiteral("W%1 B%2.%3")
                                          .arg(wi)
                                          .arg(w.blockId)
                                          .arg(w.warpInBlock)
                                    : tr("W%1 (free)").arg(wi);
      const QRect labelRect(x, ry, m_labelW, rowH);
      p.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, label);
      m_hits.push_back({labelRect, smIdx, wi, -1});
      x += m_labelW;

      // Next pc + instruction
      if (w.valid) {
        QString instr = QStringLiteral("pc %1 ").arg(w.pc, -4);
        instr += (w.activeMask == 0 && w.parkedMask != 0) ? tr("(waiting)")
                                                           : instrAt(w.pc);
        p.setPen(c.text);
        p.drawText(QRect(x, ry, m_instrW, rowH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(instr, Qt::ElideRight, m_instrW - 4));
      }
      x += m_instrW;

      // Lanes
      const int ly = ry + (rowH - kLane) / 2;
      for (unsigned lane = 0; lane < ws; ++lane) {
        const uint32_t bit = 1u << lane;
        LaneState st;
        if (!w.valid || !(w.validMask & bit))
          st = LaneState::NoThread;
        else if (w.exitedMask & bit)
          st = LaneState::Exited;
        else if (w.parkedMask & bit)
          st = LaneState::Parked;
        else if (w.activeMask & bit)
          st = LaneState::Active;
        else
          st = LaneState::Masked;
        const QRect r(x, ly, kLane, kLane);
        paintLane(p, r, st, issuedNow && (w.lastExecMask & bit), c);
        m_hits.push_back({r.adjusted(-1, -1, 1, 1), smIdx, wi,
                          static_cast<int>(lane)});
        x += kLane + kLaneGap;
      }
      x += 8;

      // Status text
      if (w.valid) {
        QStringList parts;
        if (issuedNow)
          parts << tr("issued this cycle");
        if (w.atBarrier || (w.parkedMask && !w.activeMask))
          parts << tr("waiting at BAR");
        if (w.stallCycles > 0)
          parts << tr("stall %1").arg(w.stallCycles);
        if (diverged)
          parts << tr("diverged: %1 paths").arg(w.paths.size());
        p.setPen(diverged || issuedNow ? c.text : c.mutedText);
        p.drawText(QRect(x, ry, std::max(10, width() - x - kMargin), rowH),
                   Qt::AlignLeft | Qt::AlignVCenter, parts.join(" · "));
      }
      ry += rowH;
    }
    y += boxH + kSmGap;
  }
}

QString GpuWarpView::laneToolTip(unsigned smIdx, unsigned wi, int lane) const {
  const auto &w = m_view->sms[smIdx].warps[wi];
  QStringList lines;
  if (!w.valid)
    return tr("SM %1, warp slot %2: free (no warp resident)")
        .arg(smIdx)
        .arg(wi);
  lines << tr("SM %1, warp slot %2 - block %3, warp %4 of the block")
               .arg(smIdx)
               .arg(wi)
               .arg(w.blockId)
               .arg(w.warpInBlock);
  if (lane < 0) {
    lines << tr("Next pc: %1").arg(w.pc);
    if (w.hasLastInstr)
      lines << tr("Last issued: pc %1  %2 (exec mask 0x%3)")
                   .arg(w.lastPc)
                   .arg(QString::fromStdString(gpu::disassemble(w.lastInstr)))
                   .arg(w.lastExecMask, 0, 16);
    QStringList paths;
    for (const auto &path : w.paths)
      paths << tr("pc %1 mask 0x%2%3")
                   .arg(path.pc)
                   .arg(path.mask, 0, 16)
                   .arg(path.parked ? tr(" (at barrier)") : QString());
    lines << tr("Control-flow paths: %1").arg(paths.join(", "));
    return lines.join('\n');
  }
  const unsigned ws = m_view->config.warpSize;
  const uint32_t bit = 1u << lane;
  lines << tr("Lane %1, threadIdx.x = %2")
               .arg(lane)
               .arg(w.warpInBlock * ws + static_cast<unsigned>(lane));
  QString state;
  if (!(w.validMask & bit))
    state = tr("no thread (block smaller than the warp)");
  else if (w.exitedMask & bit)
    state = tr("exited");
  else if (w.parkedMask & bit)
    state = tr("waiting at a barrier");
  else if (w.activeMask & bit)
    state = tr("active (follows the current path)");
  else
    state = tr("masked off (waits for another path to reconverge)");
  lines << tr("State: %1").arg(state);
  lines << ((w.lastExecMask & bit) ? tr("Executed the last instruction: yes")
                                   : tr("Executed the last instruction: no"));
  if (static_cast<size_t>(lane) < w.lanes.size()) {
    const auto &lr = w.lanes[static_cast<size_t>(lane)];
    QStringList regs;
    for (unsigned r = 1; r < 16; ++r) {
      regs << QStringLiteral("R%1=%2").arg(r).arg(
          static_cast<int32_t>(lr.r[r]));
      if (r % 5 == 0) {
        lines << regs.join("  ");
        regs.clear();
      }
    }
    QStringList preds;
    for (unsigned pi = 0; pi < 4; ++pi)
      preds << QStringLiteral("P%1=%2").arg(pi).arg((lr.preds >> pi) & 1u);
    lines << preds.join("  ");
  }
  return lines.join('\n');
}

bool GpuWarpView::event(QEvent *e) {
  if (e->type() == QEvent::ToolTip && m_view) {
    auto *he = static_cast<QHelpEvent *>(e);
    for (const auto &h : m_hits) {
      if (h.rect.contains(he->pos()) && h.sm < m_view->sms.size() &&
          h.warp < m_view->sms[h.sm].warps.size()) {
        QToolTip::showText(he->globalPos(), laneToolTip(h.sm, h.warp, h.lane),
                           this, h.rect);
        return true;
      }
    }
  }
  return QWidget::event(e);
}

// ---------------------------------------------------------------------------
// GpuLegend
// ---------------------------------------------------------------------------

GpuLegend::GpuLegend(QWidget *parent) : QWidget(parent) {
  QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
  sp.setHeightForWidth(true);
  setSizePolicy(sp);
  setToolTip(tr("Legend: what the lane squares and row colours mean"));
  setStatusTip(tr("Legend of the warp view"));
}

void GpuLegend::setColors(const GpuColors &c) {
  m_colors = c;
  updateGeometry();
  update();
}

std::vector<GpuLegend::Item> GpuLegend::items() const {
  return {{1, tr("active lane")},
          {2, tr("masked off: thread waits for the other path")},
          {3, tr("waiting at barrier")},
          {4, tr("exited")},
          {0, tr("no thread (block smaller than warp)")},
          {5, tr("issued this cycle (row bar + lanes that executed)")},
          {6, tr("diverged warp (row)")}};
}

int GpuLegend::layoutItems(int width, std::vector<QRect> *rects) const {
  const QFontMetrics fm(font());
  const int h = std::max(fm.height(), kLane) + 4;
  int x = 4, y = 2;
  for (const auto &it : items()) {
    const int w = kLane + 6 + fm.horizontalAdvance(it.text) + 14;
    if (x + w > width && x > 4) {
      x = 4;
      y += h;
    }
    if (rects)
      rects->push_back(QRect(x, y, w, h));
    x += w;
  }
  return y + h + 2;
}

int GpuLegend::heightForWidth(int w) const { return layoutItems(w, nullptr); }
QSize GpuLegend::sizeHint() const {
  return QSize(600, layoutItems(600, nullptr));
}
QSize GpuLegend::minimumSizeHint() const {
  return QSize(200, layoutItems(std::max(200, width()), nullptr));
}

void GpuLegend::paintEvent(QPaintEvent *) {
  QPainter p(this);
  std::vector<QRect> rects;
  layoutItems(width(), &rects);
  const auto its = items();
  const QFontMetrics fm(font());
  for (size_t i = 0; i < its.size() && i < rects.size(); ++i) {
    const QRect &r = rects[i];
    const QRect sw(r.left(), r.top() + (r.height() - kLane) / 2, kLane, kLane);
    switch (its[i].kind) {
    case 5:
      p.fillRect(sw.adjusted(-6, -1, -sw.width() - 3, 1), m_colors.executed);
      GpuWarpView::paintLane(p, sw, GpuWarpView::LaneState::Active, true,
                             m_colors);
      break;
    case 6:
      p.fillRect(sw.adjusted(-2, -1, 2, 1), m_colors.divergedRow);
      p.setPen(QPen(m_colors.frame, 1));
      p.drawRect(sw.adjusted(-2, -1, 1, 0));
      break;
    default:
      GpuWarpView::paintLane(
          p, sw, static_cast<GpuWarpView::LaneState>(its[i].kind), false,
          m_colors);
      break;
    }
    p.setPen(m_colors.text);
    p.drawText(QRect(sw.right() + 6, r.top(), r.width() - kLane - 6,
                     r.height()),
               Qt::AlignLeft | Qt::AlignVCenter, its[i].text);
  }
}

// ---------------------------------------------------------------------------
// IOGpuWidget
// ---------------------------------------------------------------------------

IOGpuWidget::IOGpuWidget(IOGpu *gpu, QWidget *parent)
    : QWidget(parent), m_gpu(gpu) {
  m_colors = GpuColors::fromPalette(palette());
  buildUi();
  applyTheme();

  connect(ThemeManager::get(), &ThemeManager::themeChanged, this,
          &IOGpuWidget::applyTheme);

  // Throttled refresh at the same rate as the rest of the UI
  // (RIPES_SETTING_UIUPDATEPS); only repaints when the GPU state changed.
  m_timer = new QTimer(this);
  connect(m_timer, &QTimer::timeout, this, [this] { refresh(false); });
  updateTimerInterval();
  if (RipesSettings::hasSetting(RIPES_SETTING_UIUPDATEPS))
    connect(RipesSettings::getObserver(RIPES_SETTING_UIUPDATEPS),
            &SettingObserver::modified, this,
            [this] { updateTimerInterval(); });
  m_timer->start();
  refresh(true);
}

void IOGpuWidget::openGpuHelp() { Ripes::showHelp(QStringLiteral("09-gpu")); }

void IOGpuWidget::updateTimerInterval() {
  int ups = 25;
  if (RipesSettings::hasSetting(RIPES_SETTING_UIUPDATEPS))
    ups = std::max(1, RipesSettings::value(RIPES_SETTING_UIUPDATEPS).toInt());
  m_timer->setInterval(1000 / ups);
}

void IOGpuWidget::resizeEvent(QResizeEvent *e) {
  QWidget::resizeEvent(e);
  // Side by side needs room for two readable tables; below that, stack them.
  if (m_detailSplit) {
    const auto o = width() >= 700 ? Qt::Horizontal : Qt::Vertical;
    if (m_detailSplit->orientation() != o)
      m_detailSplit->setOrientation(o);
  }
}

void IOGpuWidget::buildUi() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(6, 6, 6, 6);
  root->setSpacing(4);

  // Header: title + one-line explanation + help.
  auto *header = new QHBoxLayout();
  auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("SIMT GPU")));
  setTips(title, tr("A teaching model of a GPU: SMs run warps of lanes in "
                    "lockstep (SIMT)"));
  auto *subtitle = new QLabel(
      tr("Watch warps of threads execute a kernel in lockstep: active masks, "
         "branch divergence, memory coalescing and latency hiding."));
  subtitle->setWordWrap(true);
  setTips(subtitle, tr("What this view shows"));
  m_helpButton = new QToolButton();
  m_helpButton->setText(QStringLiteral("?"));
  setTips(m_helpButton,
          tr("Open the GPU tutorial (user guide chapter 09-gpu)"));
  connect(m_helpButton, &QToolButton::clicked, this,
          [this] { openGpuHelp(); });
  header->addWidget(title);
  header->addWidget(subtitle, 1);
  header->addWidget(m_helpButton, 0, Qt::AlignTop);
  root->addLayout(header);

  // Status strip.
  auto *status = new QHBoxLayout();
  m_badge = new QLabel();
  m_badge->setAlignment(Qt::AlignCenter);
  m_badge->setMinimumWidth(90);
  setTips(m_badge, tr("GPU state from the STATUS register: IDLE (no kernel "
                      "or DONE acknowledged), BUSY (kernel running), DONE "
                      "(finished, not yet acknowledged), ABORTED (stopped by "
                      "CTRL.ABORT), ERROR (kernel failed)"));
  m_cycles = new QLabel();
  setTips(m_cycles, tr("GPU cycles spent by the current or last kernel (the "
                       "CYCLES register)"));
  m_launchInfo = new QLabel();
  setTips(m_launchInfo,
          tr("Launch geometry of the current/last kernel: blocks in the grid "
             "x threads per block, and blocks not yet dispatched to an SM"));
  m_configInfo = new QLabel();
  setTips(m_configInfo, tr("Hardware configuration (change it in the "
                           "Parameters table of the peripheral)"));
  // Long labels wrap instead of forcing a minimum width on the window.
  for (auto *l : {m_launchInfo, m_configInfo})
    l->setWordWrap(true);
  m_configInfo->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  status->addWidget(m_badge);
  status->addWidget(m_cycles);
  status->addWidget(m_launchInfo, 2);
  status->addWidget(m_configInfo, 2);
  root->addLayout(status);

  m_errorLabel = new QLabel();
  m_errorLabel->setWordWrap(true);
  m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  setTips(m_errorLabel, tr("Why the kernel stopped with an error"));
  root->addWidget(m_errorLabel);
  m_noticeLabel = new QLabel();
  m_noticeLabel->setWordWrap(true);
  setTips(m_noticeLabel, tr("Recent event that changed the GPU state"));
  root->addWidget(m_noticeLabel);

  // Main area.
  auto *vsplit = new QSplitter(Qt::Vertical);
  vsplit->setChildrenCollapsible(false);

  auto *warpPanel = new QWidget();
  auto *warpLayout = new QVBoxLayout(warpPanel);
  warpLayout->setContentsMargins(0, 0, 0, 0);
  warpLayout->setSpacing(2);
  m_warpStack = new QStackedWidget();
  auto *empty = new QLabel(
      tr("No kernel has been launched yet.\n\nTo start one, the host program "
         "writes the kernel address to KERNEL_PC, the number of blocks to "
         "GRID_X, the threads per block to BLOCK_X, the arguments to "
         "ARG0-ARG3, and then writes 1 to CTRL.START.\nTry the example "
         "gpu_vecadd.s (File > Load Example), then press Run or step with "
         "the clock button."));
  empty->setWordWrap(true);
  empty->setAlignment(Qt::AlignCenter);
  empty->setMargin(16);
  setTips(empty, tr("Shown until the first kernel launch"));
  m_warpView = new GpuWarpView();
  m_warpScroll = new QScrollArea();
  m_warpScroll->setWidget(m_warpView);
  m_warpScroll->setWidgetResizable(true);
  setTips(m_warpScroll, tr("SMs, warp slots and lanes. Each square is one "
                           "thread; hover it for details"));
  m_warpStack->addWidget(empty);
  m_warpStack->addWidget(m_warpScroll);
  m_legend = new GpuLegend();
  warpLayout->addWidget(m_warpStack, 1);
  warpLayout->addWidget(m_legend);
  vsplit->addWidget(warpPanel);

  auto *hsplit = new QSplitter(Qt::Horizontal);
  hsplit->setChildrenCollapsible(false);
  m_detailSplit = hsplit;

  auto *kernelPanel = new QWidget();
  auto *kernelLayout = new QVBoxLayout(kernelPanel);
  kernelLayout->setContentsMargins(0, 0, 0, 0);
  kernelLayout->setSpacing(2);
  m_kernelHeader = new QLabel();
  m_kernelHeader->setWordWrap(true);
  setTips(m_kernelHeader, tr("The kernel code is read from KERNEL_PC when "
                             "CTRL.START is written"));
  m_kernelTable = new QTableWidget(0, 5);
  m_kernelTable->setHorizontalHeaderLabels(
      {tr("#"), tr("Address"), tr("Machine code"), tr("Instruction"),
       tr("Warps at this PC")});
  setTips(m_kernelTable,
          tr("Kernel disassembly. Highlighted rows are the next instruction "
             "of at least one warp; the last column lists those warps as "
             "SM/warp slot"));
  kernelLayout->addWidget(m_kernelHeader);
  kernelLayout->addWidget(m_kernelTable, 1);
  hsplit->addWidget(kernelPanel);

  auto *tabs = new QTabWidget();
  m_statsTable = new QTableWidget(0, 3);
  m_statsTable->setHorizontalHeaderLabels(
      {tr("Metric"), tr("Value"), tr("Unit / meaning")});
  setTips(m_statsTable, tr("Performance counters of the current/last kernel"));
  m_regTable = new QTableWidget(0, 4);
  m_regTable->setHorizontalHeaderLabels(
      {tr("Name"), tr("Offset"), tr("Value"), tr("Meaning")});
  setTips(m_regTable, tr("Memory-mapped registers: name, byte offset from "
                         "GPU_0_BASE, current value (decoded) and meaning"));
  tabs->addTab(m_statsTable, tr("Statistics"));
  tabs->addTab(m_regTable, tr("Registers"));
  tabs->setTabToolTip(0, tr("Performance counters"));
  tabs->setTabToolTip(1, tr("Register table"));
  hsplit->addWidget(tabs);
  hsplit->setStretchFactor(0, 1);
  hsplit->setStretchFactor(1, 1);
  vsplit->addWidget(hsplit);
  vsplit->setStretchFactor(0, 3);
  vsplit->setStretchFactor(1, 2);
  root->addWidget(vsplit, 1);

  for (auto *t : {m_kernelTable, m_statsTable, m_regTable}) {
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(
        QFontMetrics(t->font()).height() + 4);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setAlternatingRowColors(true);
    t->setWordWrap(false);
  }
  m_kernelTable->setFont(monoFont());

  // Static rows of the register table.
  m_regTable->setRowCount(IOGpu::NREGISTERS);
  for (unsigned r = 0; r < IOGpu::NREGISTERS; ++r) {
    auto *name = new QTableWidgetItem(IOGpu::registerName(r));
    auto *off = new QTableWidgetItem(
        QStringLiteral("0x%1").arg(r * 4, 2, 16, QChar('0')));
    auto *val = new QTableWidgetItem();
    auto *meaning = new QTableWidgetItem(IOGpu::registerMeaning(r));
    for (auto *it : {name, off, val, meaning})
      it->setToolTip(IOGpu::registerMeaning(r));
    m_regTable->setItem(static_cast<int>(r), 0, name);
    m_regTable->setItem(static_cast<int>(r), 1, off);
    m_regTable->setItem(static_cast<int>(r), 2, val);
    m_regTable->setItem(static_cast<int>(r), 3, meaning);
  }
}

void IOGpuWidget::applyTheme() {
  m_colors = GpuColors::fromPalette(palette());
  m_legend->setColors(m_colors);
  m_errorLabel->setStyleSheet(
      QStringLiteral("color: %1;").arg(m_colors.error.name()));
  m_shownListing.reset(); // re-colour the kernel rows
  refresh(true);
}

void IOGpuWidget::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  refresh(true);
}

void IOGpuWidget::refresh(bool force) {
  if (!force && !isVisible())
    return;
  const uint64_t ver = m_gpu->version();
  if (!force && ver == m_lastVersion)
    return;
  m_lastVersion = ver;
  auto v = std::make_shared<IOGpu::View>(m_gpu->view());
  updateStatus(*v);
  m_warpStack->setCurrentIndex(v->launched ? 1 : 0);
  updateKernel(*v);
  updateStats(*v);
  updateRegisters(*v);
  m_warpView->setView(std::move(v), m_colors);
}

void IOGpuWidget::updateStatus(const IOGpu::View &v) {
  QString text;
  QColor bg;
  if (v.error) {
    text = tr("ERROR");
    bg = m_colors.error;
  } else if (v.busy) {
    text = tr("BUSY");
    bg = m_colors.busy;
  } else if (v.done && v.aborted) {
    text = tr("ABORTED");
    bg = m_colors.parked;
  } else if (v.done) {
    text = tr("DONE");
    bg = m_colors.done;
  } else {
    text = tr("IDLE");
    bg = m_colors.idle;
  }
  m_badge->setText(text);
  m_badge->setStyleSheet(
      QStringLiteral("QLabel { background: %1; color: %2; font-weight: bold; "
                     "padding: 2px 8px; border-radius: 4px; }")
          .arg(bg.name(), m_colors.badgeText.name()));

  m_cycles->setText(tr("GPU cycles: %1").arg(v.stats.cycles));
  if (v.launched)
    m_launchInfo->setText(tr("Grid: %1 block(s) x %2 thread(s) · pending "
                             "blocks: %3")
                              .arg(v.gridX)
                              .arg(v.blockX)
                              .arg(v.pendingBlocks));
  else
    m_launchInfo->setText(tr("No kernel launched"));
  m_configInfo->setText(tr("%1 SM x %2 warps x %3 lanes · %4 GPU cycle(s) "
                           "per CPU cycle")
                            .arg(v.config.numSMs)
                            .arg(v.config.warpsPerSM)
                            .arg(v.config.warpSize)
                            .arg(v.stepsPerCycle));

  if (!v.errorMessage.isEmpty()) {
    m_errorLabel->setText(
        (v.error ? tr("Error: %1") : tr("Last error (acknowledged): %1"))
            .arg(v.errorMessage));
    m_errorLabel->show();
  } else {
    m_errorLabel->hide();
  }

  QString notice;
  switch (v.notice) {
  case IOGpu::Notice::None:
    break;
  case IOGpu::Notice::LaunchIgnored:
    notice = tr("CTRL.START was written while the GPU was BUSY: that launch "
                "was ignored (STATUS.LAUNCH_IGNORED, %1 time(s) so far). The "
                "running kernel is not affected.")
                 .arg(v.ignoredLaunches);
    break;
  case IOGpu::Notice::ReverseRestored:
    notice = tr("Reverse execution: the GPU state was restored to the "
                "previous CPU cycle (%1 cycle(s) of GPU history left).")
                 .arg(v.historyFrames > 0 ? v.historyFrames - 1 : 0);
    break;
  case IOGpu::Notice::ReverseReset:
    notice = tr("Reverse execution went past the recorded GPU history: the "
                "accelerator was reset to its pre-launch state. Memory the "
                "kernel already wrote is not undone; reset the processor and "
                "run again to relaunch the kernel.");
    break;
  case IOGpu::Notice::ConfigChangedWhileBusy:
    notice = tr("The GPU configuration was changed while a kernel was "
                "running: the kernel was aborted (STATUS.DONE | ERROR). Clear "
                "the flags and launch it again on the new configuration.");
    break;
  }
  if (notice.isEmpty() && v.launchIgnored)
    notice = tr("STATUS.LAUNCH_IGNORED is set: a launch was ignored because "
                "the GPU was BUSY.");
  m_noticeLabel->setText(notice.isEmpty() ? QString()
                                          : QStringLiteral("ⓘ ") + notice);
  m_noticeLabel->setVisible(!notice.isEmpty());
}

void IOGpuWidget::updateKernel(const IOGpu::View &v) {
  const auto &words = v.kernelWords;
  if (words != m_shownListing || v.kernelBase != m_shownListingBase) {
    m_shownListing = words;
    m_shownListingBase = v.kernelBase;
    const int n = words ? static_cast<int>(words->size()) : 0;
    m_kernelTable->setRowCount(n);
    for (int i = 0; i < n; ++i) {
      const uint32_t w = (*words)[static_cast<size_t>(i)];
      const QStringList cols = {
          QString::number(i), hex32(v.kernelBase + static_cast<uint32_t>(i) * 4),
          hex32(w), QString::fromStdString(gpu::disassemble(w)), QString()};
      for (int c = 0; c < cols.size(); ++c) {
        auto *it = new QTableWidgetItem(cols[c]);
        m_kernelTable->setItem(i, c, it);
      }
    }
    if (n == 0)
      m_kernelHeader->setText(
          tr("Kernel: none captured yet (read from KERNEL_PC on CTRL.START)."));
    else
      m_kernelHeader->setText(tr("Kernel at %1 - %2 instruction(s). "
                                 "Highlighted: next instruction of a warp.")
                                  .arg(hex32(v.kernelBase))
                                  .arg(n));
  }

  // Current PCs.
  std::map<uint32_t, QStringList> at;
  if (v.busy) {
    for (unsigned s = 0; s < v.sms.size(); ++s)
      for (unsigned w = 0; w < v.sms[s].warps.size(); ++w) {
        const auto &wv = v.sms[s].warps[w];
        if (!wv.valid)
          continue;
        for (const auto &p : wv.paths)
          at[p.pc] << QStringLiteral("S%1W%2%3")
                          .arg(s)
                          .arg(w)
                          .arg(p.parked ? QStringLiteral("(BAR)")
                                        : QString());
      }
  }
  QFont normal = m_kernelTable->font();
  QFont bold = normal;
  bold.setBold(true);
  int firstHit = -1;
  for (int i = 0; i < m_kernelTable->rowCount(); ++i) {
    auto it = at.find(static_cast<uint32_t>(i));
    const bool hit = it != at.end();
    if (hit && firstHit < 0)
      firstHit = i;
    for (int c = 0; c < m_kernelTable->columnCount(); ++c) {
      auto *item = m_kernelTable->item(i, c);
      if (!item)
        continue;
      item->setBackground(hit ? QBrush(m_colors.pcRow) : QBrush());
      item->setFont(hit ? bold : normal);
    }
    if (auto *item = m_kernelTable->item(i, 4))
      item->setText(hit ? it->second.join(' ') : QString());
  }
  // Size the columns after highlighting (bold rows are wider).
  m_kernelTable->resizeColumnsToContents();
  if (firstHit >= 0 && v.busy)
    m_kernelTable->scrollToItem(m_kernelTable->item(firstHit, 0),
                                QAbstractItemView::EnsureVisible);
}

void IOGpuWidget::updateStats(const IOGpu::View &v) {
  const auto &st = v.stats;
  const double ipc = st.cycles ? double(st.issued) / double(st.cycles) : 0.0;
  const double simd =
      st.issued ? 100.0 * double(st.threadInstrs) /
                      double(st.issued * std::max(1u, v.config.warpSize))
                : 0.0;
  const uint64_t issueSlots = st.issued + st.idleSlots;
  const double util = issueSlots ? 100.0 * double(st.issued) / double(issueSlots) : 0.0;
  struct Row {
    QString metric, value, unit;
    const char *reg = nullptr; ///< register exposing it, nullptr = widget only
  };
  const std::vector<Row> rows = {
      {tr("GPU cycles"), QString::number(st.cycles),
       tr("cycles since the kernel was launched"), "CYCLES"},
      {tr("Warp instructions issued"), QString::number(st.issued),
       tr("warp-instructions (one per warp per issue)"), "STATS_INSTRS"},
      {tr("Thread instructions"), QString::number(st.threadInstrs),
       tr("lane-instructions actually executed")},
      {tr("IPC"), QString::number(ipc, 'f', 2),
       tr("warp-instructions per GPU cycle (max = number of SMs)")},
      {tr("SIMD efficiency"), QStringLiteral("%1 %").arg(simd, 0, 'f', 1),
       tr("thread instructions / (warp instructions x warp size)")},
      {tr("Issue-slot utilization"),
       QStringLiteral("%1 %").arg(util, 0, 'f', 1),
       tr("issued / (issued + idle slots)")},
      {tr("Idle issue slots"), QString::number(st.idleSlots),
       tr("SM-cycles in which no warp was ready (latency not hidden)"),
       "STATS_IDLE"},
      {tr("Divergent branches"), QString::number(st.divergences),
       tr("events: a BRA split a warp into two paths"), "STATS_DIVERGE"},
      {tr("Memory instructions"), QString::number(st.memInstrs),
       tr("LDG/STG warp-instructions")},
      {tr("Memory transactions"), QString::number(st.memTransactions),
       tr("32-byte segments moved"), "STATS_MEMTXN"},
      {tr("Coalesced accesses"),
       tr("%1 instr / %2 txn").arg(st.coalescedInstrs).arg(st.coalescedTxns),
       tr("used the minimum number of 32-byte segments")},
      {tr("Uncoalesced accesses"),
       tr("%1 instr / %2 txn")
           .arg(st.uncoalescedInstrs)
           .arg(st.uncoalescedTxns),
       tr("needed extra segments (strided or scattered addresses)")},
      {tr("Shared-memory bank conflicts"),
       tr("%1 instr / %2 cycles")
           .arg(st.bankConflicts)
           .arg(st.bankConflictCycles),
       tr("LDS/STS with conflicts / extra serialisation cycles")},
      {tr("Barrier arrivals"), QString::number(st.barriers),
       tr("warps that reached BAR (counted once per warp)")},
      {tr("Blocks completed"),
       tr("%1 of %2").arg(st.blocksCompleted).arg(v.launched ? v.gridX : 0),
       tr("thread blocks finished / blocks in the grid")},
      {tr("Ignored launches"), QString::number(v.ignoredLaunches),
       tr("CTRL.START writes while BUSY")},
      {tr("Reverse history"),
       tr("%1 cycle(s), %2 KiB").arg(v.historyFrames).arg(v.historyBytes / 1024),
       tr("saved GPU states for Reverse; %1 bytes per changed cycle")
           .arg(v.lastFrameBytes)},
  };
  if (m_statsTable->rowCount() != static_cast<int>(rows.size()))
    m_statsTable->setRowCount(static_cast<int>(rows.size()));
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const auto &r = rows[static_cast<size_t>(i)];
    const QString where =
        r.reg ? tr("register %1").arg(QString::fromLatin1(r.reg))
              : tr("widget only");
    const QString cells[3] = {r.metric, r.value,
                              r.unit + QStringLiteral("  [") + where +
                                  QStringLiteral("]")};
    for (int c = 0; c < 3; ++c) {
      auto *it = m_statsTable->item(i, c);
      if (!it) {
        it = new QTableWidgetItem();
        m_statsTable->setItem(i, c, it);
      }
      if (it->text() != cells[c])
        it->setText(cells[c]);
      it->setToolTip(r.metric + QStringLiteral(": ") + r.unit);
      if (c == 1)
        it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
  }
  m_statsTable->resizeColumnToContents(0);
  m_statsTable->resizeColumnToContents(1);
}

void IOGpuWidget::updateRegisters(const IOGpu::View &v) {
  for (unsigned r = 0; r < IOGpu::NREGISTERS; ++r) {
    const uint32_t val = v.regs[r];
    QString text = hex32(val);
    const QString dec = IOGpu::decodeRegister(r, val);
    if (!dec.isEmpty() && dec != QString::number(val))
      text += QStringLiteral("  (%1)").arg(dec);
    else if (val > 9)
      text += QStringLiteral("  (%1)").arg(val);
    if (auto *it = m_regTable->item(static_cast<int>(r), 2))
      if (it->text() != text)
        it->setText(text);
  }
  for (int c = 0; c < 3; ++c)
    m_regTable->resizeColumnToContents(c);
}

} // namespace Ripes
