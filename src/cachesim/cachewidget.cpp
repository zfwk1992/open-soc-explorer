#include "cachewidget.h"
#include "ui_cachewidget.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>

#include "cachegraphic.h"
#include "processorhandler.h"
#include "radix.h"
#include "cacheview.h"
#include "ripessettings.h"

namespace Ripes {

CacheWidget::CacheWidget(QWidget *parent)
    : QWidget(parent), m_ui(new Ui::CacheWidget) {
  m_ui->setupUi(this);

  m_scene = std::make_unique<QGraphicsScene>(this);
  m_cacheSim = std::make_shared<CacheSim>(this);
  m_ui->cacheConfig->setCache(m_cacheSim);
  m_ui->cachePlot->setCache(m_cacheSim);

  auto *cacheGraphic = new CacheGraphic(*m_cacheSim);
  m_scene->addItem(cacheGraphic);
  connect(m_cacheSim.get(), &CacheSim::configurationChanged, this, [this] {
    RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();

    auto cacheViews = m_scene->views();
    if (cacheViews.size() > 0) {
      static_cast<CacheView *>(cacheViews.at(0))->fitScene();
    }
  });

  // Status line describing the most recent access in words, so that hit/miss
  // is not conveyed by colour alone.
  m_lastAccess = new QLabel(this);
  m_lastAccess->setWordWrap(true);
  m_lastAccess->setTextFormat(Qt::RichText);
  const QString tip =
      tr("Describes the most recent access to this cache: the address, "
         "whether it was a read or a write, whether the data was found in the "
         "cache (hit) or had to be fetched from the next level (miss), and "
         "where it is stored (set/line index, way and word).");
  m_lastAccess->setToolTip(tip);
  m_lastAccess->setStatusTip(tip);
  m_ui->verticalLayout->insertWidget(1, m_lastAccess);
  connect(m_cacheSim.get(), &CacheSim::dataChanged, this,
          &CacheWidget::updateLastAccess, Qt::QueuedConnection);
  connect(m_cacheSim.get(), &CacheSim::cacheInvalidated, this,
          &CacheWidget::showIdleHint);
  connect(ThemeManager::get(), &ThemeManager::themeChanged, this,
          &CacheWidget::showIdleHint);
  showIdleHint();
}

void CacheWidget::showIdleHint() {
  if (m_cacheSim->getHits() + m_cacheSim->getMisses() == 0) {
    m_lastAccess->setText(
        "<i>" +
        tr("No cache accesses yet. Assemble a program and step the processor "
           "(or run it) to see each access marked as a hit or a miss.") +
        "</i>");
  } else {
    m_lastAccess->setText(
        "<i>" +
        tr("%1 hits, %2 misses so far. Step the processor to see individual "
           "accesses highlighted in the diagram.")
            .arg(m_cacheSim->getHits())
            .arg(m_cacheSim->getMisses()) +
        "</i>");
  }
}

void CacheWidget::updateLastAccess(
    const CacheSim::CacheTransaction &transaction) {
  if (transaction.type == MemoryAccess::None ||
      transaction.index.line == CacheSim::s_invalidIndex) {
    showIdleHint();
    return;
  }
  const QColor color = transaction.isHit ? CacheColors::hitColor()
                                         : CacheColors::missColor();
  const QString result =
      transaction.isHit ? tr("HIT") : tr("MISS");
  const QString kind =
      transaction.type == MemoryAccess::Write ? tr("Write") : tr("Read");
  QString text =
      tr("Last access: %1 %2 &rarr; %3 (set %4, way %5, word %6)")
          .arg(kind)
          .arg(encodeRadixValue(transaction.address, Radix::Hex,
                                ProcessorHandler::currentISA()->bytes()))
          .arg("<span style=\"background-color:" + color.name() +
               "; color:black; font-weight:bold;\">&nbsp;" + result +
               "&nbsp;</span>")
          .arg(transaction.index.line)
          .arg(transaction.index.way)
          .arg(transaction.index.block);
  if (transaction.isWriteback) {
    text += " &mdash; " + tr("a dirty line was written back to memory");
  }
  m_lastAccess->setText(text);
}

void CacheWidget::setNextLevelCache(const std::shared_ptr<CacheSim> &cache) {
  m_cacheSim.get()->setNextLevelCache(cache);
}

CacheWidget::~CacheWidget() { delete m_ui; }

} // namespace Ripes
