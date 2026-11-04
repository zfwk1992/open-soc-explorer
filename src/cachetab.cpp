#include "cachetab.h"
#include "ui_cachetab.h"

#include "cachesim/cachegraphic.h"
#include "cachesim/cacheview.h"
#include "cachesim/cachewidget.h"

#include "processorhandler.h"
#include "tabheader.h"

#include <QScrollArea>
#include <QScrollBar>

namespace Ripes {

namespace {
/// Vertical-only scroll area for the cache configuration / statistics / plot
/// column: it keeps the column's full width but lets it scroll vertically,
/// so that the column (and the hit-rate plot's minimum height) does not
/// force the main window to be taller than a 1280x800 screen.
class VerticalScrollArea : public QScrollArea {
public:
  using QScrollArea::QScrollArea;
  QSize minimumSizeHint() const override {
    const int w = widget() ? widget()->minimumSizeHint().width() : 0;
    return {w + verticalScrollBar()->sizeHint().width() + 2 * frameWidth(),
            120};
  }
  QSize sizeHint() const override {
    if (!widget())
      return QScrollArea::sizeHint();
    const QSize s = widget()->sizeHint().expandedTo(
        widget()->minimumSizeHint());
    return {s.width() + verticalScrollBar()->sizeHint().width() +
                2 * frameWidth(),
            s.height() + 2 * frameWidth()};
  }
};
} // namespace

CacheTab::CacheTab(QToolBar *toolbar, QWidget *parent)
    : RipesTab(toolbar, parent), m_ui(new Ui::CacheTab) {
  m_ui->setupUi(this);

  // Let the left column scroll vertically instead of setting the window's
  // minimum height.
  auto *scroll = new VerticalScrollArea(m_ui->verticalLayoutWidget);
  scroll->setObjectName("cacheColumnScroll");
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_ui->verticalLayout->replaceWidget(m_ui->cacheTabWidget, scroll);
  scroll->setWidget(m_ui->cacheTabWidget);
  scroll->viewport()->setAutoFillBackground(false);
  m_ui->cacheTabWidget->setAutoFillBackground(false);

  // Explanatory header + colour legend for the cache diagram.
  m_ui->gridLayout->addWidget(
      createTabHeader(
          tr("Cache simulator"),
          tr("Configure the L1 data and instruction caches on the left and "
             "watch every memory access in the diagram on the right: the "
             "address is split into tag / index / word, the indexed set is "
             "highlighted and the accessed word is marked as a hit or a "
             "miss. Statistics and the hit-rate plot update as the program "
             "runs."),
          "07-cache", this),
      0, 0);
  m_ui->gridLayout->addWidget(
      createLegend(
          {{CacheColors::indexingColor, tr("Indexed set / word"),
            tr("The set (row) selected by the index bits and the word "
               "(column) selected by the word-offset bits of the last access")},
           {CacheColors::hitColor, tr("Hit"),
            tr("The accessed word was found in the cache")},
           {CacheColors::missColor, tr("Miss"),
            tr("The accessed word was not in the cache and was loaded from "
               "the next level")},
           {CacheColors::dirtyColor, tr("Dirty word"),
            tr("Written in the cache but not yet in memory (write-back "
               "policy)")}},
          this),
      1, 0);
  m_ui->gridLayout->setContentsMargins(4, 4, 4, 0);
  // Header and legend keep their natural height; the splitter gets the rest.
  m_ui->gridLayout->setRowStretch(2, 1);
  setTips(m_ui->cacheView,
          tr("Cache diagram: one row per way of each set (Index column). V = "
             "valid bit, D = dirty bit, LRU = age for replacement, Tag = upper "
             "address bits stored with the line, Word N = cached data. Click a "
             "word to show its address in the Memory tab. Ctrl + mouse wheel "
             "zooms; middle-drag or Ctrl + drag pans."));

  // During processor running, it should not be possible to interact with the
  // cache tab
  connect(ProcessorHandler::get(), &ProcessorHandler::runStarted, this,
          [this] { setEnabled(false); });
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          [this] { setEnabled(true); });

  connect(m_ui->cacheView, &CacheView::cacheAddressSelected, this,
          &CacheTab::focusAddressChanged);

  connect(m_ui->cacheTabWidget, &CacheTabWidget::cacheFocusChanged, this,
          [this](CacheWidget *widget) {
            m_ui->cacheView->setScene(widget->getScene());
            m_ui->cacheView->fitScene();
          });

  // CacheTabWidget has a tendency to expand, but we'd like to minimize its
  // size. Stretch the CacheView.
  m_ui->splitter->setStretchFactor(0, 0);
  m_ui->splitter->setStretchFactor(1, 10);
  m_ui->splitter->setSizes({1, 10000});
}

void CacheTab::tabVisibilityChanged(bool visible) {
  /**
   * A hack to ensure that the cache view is resized to screen size _after_ Qt
   * has calculated widget sizes when the cache tab is first shown.
   */
  if (!m_initialized && visible) {
    QTimer::singleShot(100, m_ui->cacheTabWidget,
                       [&] { m_ui->cacheTabWidget->flipTabs(); });
    m_initialized = visible;
  }
}

CacheTab::~CacheTab() { delete m_ui; }

} // namespace Ripes
