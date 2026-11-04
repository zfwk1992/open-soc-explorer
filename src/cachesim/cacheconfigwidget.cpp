#include "cacheconfigwidget.h"
#include "ui_cacheconfigwidget.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QtCharts/QChartView>

#include "cacheplotwidget.h"
#include "enumcombobox.h"
#include "ripessettings.h"
#include "tabheader.h"

namespace Ripes {

CacheConfigWidget::CacheConfigWidget(QWidget *parent)
    : QWidget(parent), m_ui(new Ui::CacheConfigWidget) {
  m_ui->setupUi(this);

  // Gather a list of all items in this widget which will trigger a modification
  // to the current configuration
  m_configItems = {
      m_ui->presets,           m_ui->ways,   m_ui->lines, m_ui->blocks,
      m_ui->replacementPolicy, m_ui->wrMiss, m_ui->wrHit};
}

void CacheConfigWidget::setCache(const std::shared_ptr<CacheSim> &cache) {
  m_cache = cache;

  setupEnumCombobox(m_ui->replacementPolicy, s_cacheReplPolicyStrings);
  setupEnumCombobox(m_ui->wrHit, s_cacheWritePolicyStrings);
  setupEnumCombobox(m_ui->wrMiss, s_cacheWriteAllocateStrings);
  // The enum name maps hold untranslated (QT_TRANSLATE_NOOP) source strings;
  // translate the visible item texts. Item data (the enum values) is
  // unchanged, so presets and settings are unaffected.
  for (auto *cb : {m_ui->replacementPolicy, m_ui->wrHit, m_ui->wrMiss}) {
    for (int i = 0; i < cb->count(); ++i) {
      cb->setItemText(i, QCoreApplication::translate(
                             "Ripes::CacheConfigWidget",
                             cb->itemText(i).toUtf8().constData()));
    }
  }
  setupToolTips();

  m_ui->ways->setValue(m_cache->getWays());
  m_ui->lines->setValue(m_cache->getLineBits());
  m_ui->blocks->setValue(m_cache->getBlockBits());

  connect(m_ui->ways, QOverload<int>::of(&QSpinBox::valueChanged),
          m_cache.get(), &CacheSim::setWays);
  connect(m_ui->blocks, QOverload<int>::of(&QSpinBox::valueChanged),
          m_cache.get(), &CacheSim::setBlocks);
  connect(m_ui->lines, QOverload<int>::of(&QSpinBox::valueChanged),
          m_cache.get(), &CacheSim::setLines);

  connect(m_ui->replacementPolicy,
          QOverload<int>::of(&QComboBox::currentIndexChanged), cache.get(),
          [this](int index) {
            m_cache->setReplacementPolicy(qvariant_cast<ReplPolicy>(
                m_ui->replacementPolicy->itemData(index)));
          });
  connect(m_ui->wrHit, QOverload<int>::of(&QComboBox::currentIndexChanged),
          cache.get(), [this](int index) {
            m_cache->setWritePolicy(
                qvariant_cast<WritePolicy>(m_ui->wrHit->itemData(index)));
          });
  connect(m_ui->wrMiss, QOverload<int>::of(&QComboBox::currentIndexChanged),
          cache.get(), [this](int index) {
            m_cache->setWriteAllocatePolicy(
                qvariant_cast<WriteAllocPolicy>(m_ui->wrMiss->itemData(index)));
          });
  connect(m_ui->savePresetButton, &QPushButton::clicked, this,
          &CacheConfigWidget::storePreset);
  m_ui->savePresetButton->setIcon(QIcon(":/icons/save.svg"));
  setTips(m_ui->savePresetButton,
          tr("Save the current cache configuration as a named preset"));
  connect(m_ui->removePresetButton, &QPushButton::clicked, this,
          &CacheConfigWidget::removePreset);
  m_ui->removePresetButton->setIcon(QIcon(":/icons/delete.svg"));
  setTips(m_ui->removePresetButton, tr("Delete the selected cache preset"));

  connect(m_cache.get(), &CacheSim::configurationChanged, this,
          &CacheConfigWidget::handleConfigurationChanged);
  connect(m_cache.get(), &CacheSim::configurationChanged, this,
          [this] { emit configurationChanged(); });

  setupPresets();
  handleConfigurationChanged();
}

void CacheConfigWidget::storePreset() {
  bool ok;
  QString text = QInputDialog::getText(
      this, tr("Store cache preset"), tr("Preset name:"), QLineEdit::Normal,
      "", &ok);
  if (ok && !text.isEmpty()) {
    CachePreset preset;
    preset.name = text;
    preset.ways = m_ui->ways->value();
    preset.lines = m_ui->lines->value();
    preset.blocks = m_ui->blocks->value();
    preset.wrPolicy = getEnumValue<WritePolicy>(m_ui->wrHit);
    preset.wrAllocPolicy = getEnumValue<WriteAllocPolicy>(m_ui->wrMiss);
    preset.replPolicy = getEnumValue<ReplPolicy>(m_ui->replacementPolicy);

    auto presets = RipesSettings::value(RIPES_SETTING_CACHE_PRESETS)
                       .value<QList<Ripes::CachePreset>>();
    presets.push_back(preset);
    RipesSettings::setValue(RIPES_SETTING_CACHE_PRESETS,
                            QVariant::fromValue(presets));

    m_ui->presets->addItem(preset.name,
                           QVariant::fromValue<CachePreset>(preset));
    m_ui->presets->setCurrentIndex(m_ui->presets->count() - 1);
  }
}

void CacheConfigWidget::removePreset() {
  auto presetData = m_ui->presets->currentData();
  if (presetData.isNull()) {
    return;
  }

  auto preset = presetData.value<CachePreset>();
  const QString prompt =
      tr("Are you sure you want to delete preset '%1'?").arg(preset.name);
  auto button =
      QMessageBox::information(this, tr("Delete cache preset"), prompt,
                                         QMessageBox::No | QMessageBox::Yes);
  if (button != QMessageBox::Yes) {
    return;
  }

  auto presets = RipesSettings::value(RIPES_SETTING_CACHE_PRESETS)
                     .value<QList<Ripes::CachePreset>>();
  presets.removeOne(preset);
  RipesSettings::setValue(RIPES_SETTING_CACHE_PRESETS,
                          QVariant::fromValue(presets));
  // delete preset, but keep preset data by setting an invalid index (avoids
  // loading the next preset in the combobox)
  const auto idxToDelete = m_ui->presets->currentIndex();
  m_ui->presets->setCurrentIndex(-1);
  m_ui->presets->removeItem(idxToDelete);
}

void CacheConfigWidget::setupPresets() {
  for (const auto &preset : RipesSettings::value(RIPES_SETTING_CACHE_PRESETS)
                                .value<QList<CachePreset>>()) {
    m_ui->presets->addItem(preset.name,
                           QVariant::fromValue<CachePreset>(preset));
  }

  connect(m_ui->presets, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (index == -1) {
              return;
            }
            const CachePreset _preset =
                qvariant_cast<CachePreset>(m_ui->presets->itemData(index));
            m_justSetPreset = true;
            m_cache->setPreset(_preset);
          });
}

void CacheConfigWidget::handleConfigurationChanged() {
  std::for_each(m_configItems.begin(), m_configItems.end(),
                [](QObject *o) { o->blockSignals(true); });

  m_ui->ways->setValue(m_cache->getWays());
  m_ui->lines->setValue(m_cache->getLineBits());
  m_ui->blocks->setValue(m_cache->getBlockBits());
  setEnumIndex(m_ui->wrHit, m_cache->getWritePolicy());
  setEnumIndex(m_ui->wrMiss, m_cache->getWriteAllocPolicy());
  setEnumIndex(m_ui->replacementPolicy, m_cache->getReplacementPolicy());

  if (!m_justSetPreset) {
    m_ui->presets->setCurrentIndex(-1);
  }
  m_justSetPreset = false;

  std::for_each(m_configItems.begin(), m_configItems.end(),
                [](QObject *o) { o->blockSignals(false); });
  updateSummary();
}

void CacheConfigWidget::updateSummary() {
  const unsigned sets = m_cache->getLines();
  const unsigned ways = m_cache->getWays();
  const unsigned words = m_cache->getBlocks();
  constexpr unsigned wordBytes = 4;
  const unsigned bytes = sets * ways * words * wordBytes;
  QString kind;
  if (ways == 1) {
    kind = tr("direct-mapped");
  } else if (sets == 1) {
    kind = tr("fully associative");
  } else {
    kind = tr("%1-way set associative").arg(ways);
  }
  m_ui->summary->setText(
      tr("Capacity: %1 sets x %2 ways x %3 words x %4 bytes = %5 bytes of "
         "data (%6). Address split: tag %7 bits | index %8 bits | word %9 "
         "bits | byte offset 2 bits.")
          .arg(sets)
          .arg(ways)
          .arg(words)
          .arg(wordBytes)
          .arg(bytes)
          .arg(kind)
          .arg(m_cache->getTagBits())
          .arg(m_cache->getLineBits())
          .arg(m_cache->getBlockBits()));
}

void CacheConfigWidget::setupToolTips() {
  setTips(m_ui->presets,
          tr("Saved cache configurations. Selecting a preset sets all "
             "parameters below at once. Use the buttons to save the current "
             "configuration or delete the selected preset."));
  const QString setsTip =
      tr("Number of sets (cache lines selected by the index bits), given as "
         "the exponent N: the cache has 2^N sets. Example: N = 3 gives 8 "
         "sets. Each additional bit doubles the cache size.");
  setTips(m_ui->lines, setsTip);
  setTips(m_ui->label_4, setsTip);
  const QString waysTip =
      tr("Associativity: number of ways (blocks) in every set, as a plain "
         "count. 1 = direct-mapped: each address can live in exactly one "
         "place. More ways reduce conflict misses but need a replacement "
         "policy to pick the victim.");
  setTips(m_ui->ways, waysTip);
  setTips(m_ui->label_2, waysTip);
  const QString blocksTip =
      tr("Line (block) size, given as the exponent N: each line holds 2^N "
         "words of 4 bytes. Larger lines exploit spatial locality "
         "(neighbouring addresses are loaded together) but cost more per "
         "miss.");
  setTips(m_ui->blocks, blocksTip);
  setTips(m_ui->label_3, blocksTip);
  const QString replTip =
      tr("Replacement policy: which way of a full set is evicted on a miss. "
         "LRU evicts the least recently used way (the LRU column shows the "
         "age, 0 = most recent); Random picks any way.");
  setTips(m_ui->replacementPolicy, replTip);
  setTips(m_ui->label_8, replTip);
  const QString wrHitTip =
      tr("Write-hit policy. Write-through: every store is also written to "
         "memory immediately. Write-back: the store only updates the cache "
         "and marks the line dirty (D column); memory is updated when the "
         "line is evicted.");
  setTips(m_ui->wrHit, wrHitTip);
  setTips(m_ui->label_9, wrHitTip);
  const QString wrMissTip =
      tr("Write-miss policy. Write allocate: the missing line is loaded into "
         "the cache, then written. No write allocate: the store goes directly "
         "to memory and the cache is not changed.");
  setTips(m_ui->wrMiss, wrMissTip);
  setTips(m_ui->label_10, wrMissTip);
  setTips(m_ui->summary,
          tr("Derived from the parameters above: total data capacity and how "
             "a 32-bit address is split into tag, index and word offset."));
}

CacheConfigWidget::~CacheConfigWidget() { delete m_ui; }

} // namespace Ripes
