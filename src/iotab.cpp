#include "iotab.h"
#include "ui_iotab.h"

#include <QDockWidget>

#include <algorithm>
#include <QGraphicsItem>
#include <QHeaderView>
#include <QLabel>
#include <QMdiSubWindow>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QToolBar>

#include "fonts.h"
#include "io/memorymapmodel.h"
#include "ioperipheraltab.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "tabheader.h"

#include "cereal/archives/json.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"

namespace Ripes {

IOTab::IOTab(QToolBar *toolbar, QWidget *parent)
    : RipesTab(toolbar, parent), m_ui(new Ui::IOTab) {
  m_ui->setupUi(this);

  m_ui->gridLayout_2->addWidget(
      createTabHeader(
          tr("I/O peripherals"),
          tr("Add memory-mapped devices (LEDs, switches, a screen, "
             "accelerators...) that your program controls by reading and "
             "writing their registers. Pick a device on the left and click "
             "\"Add selected device\" (or double-click it); its parameters, register map and "
             "exported symbols appear in the panel on the right. Base "
             "addresses are assigned in the order devices are added and are "
             "reassigned when a parameter changes a device's size - always "
             "use the exported symbols instead of hard-coded addresses."),
          "08-mmio-peripherals", this),
      0, 0);
  m_ui->gridLayout_2->setRowStretch(1, 1);

  m_ui->mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_ui->mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  // Device windows added while the tab was hidden are sized once the area
  // has its real size.
  new ResizeHook(m_ui->mdiArea->viewport(), [this] {
    if (!m_pendingFit.empty())
      QTimer::singleShot(0, this, [this] { fitPendingSubWindows(); });
  });

  // QMdiArea captures its backdrop brush (QPalette::Dark) once at construction
  // and does not refresh it when the application palette changes. Set it
  // explicitly now and whenever the theme changes, so the I/O view backdrop
  // follows light/dark mode live instead of only after a processor reset.
  const auto updateMdiBackground = [this] {
    m_ui->mdiArea->setBackground(palette().brush(QPalette::Dark));
  };
  updateMdiBackground();
  connect(ThemeManager::get(), &ThemeManager::themeChanged, this,
          updateMdiBackground);

  m_ui->peripheralsTab->clear();

  // Setup peripheral selection window: one row per device type with its
  // title and a one-sentence description.
  m_ui->peripheralsTable->setRowCount(IOTypeTitles.size());
  m_ui->peripheralsTable->setColumnCount(2);
  m_ui->peripheralsTable->setHorizontalHeaderLabels(
      {tr("Device"), tr("What it does")});
  m_ui->peripheralsTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  m_ui->peripheralsTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Stretch);
  m_ui->peripheralsTable->verticalHeader()->hide();
  m_ui->peripheralsTable->setWordWrap(true);
  m_ui->peripheralsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_ui->peripheralsTable->setSelectionMode(
      QAbstractItemView::SingleSelection);
  m_ui->peripheralsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  int row = 0;
  for (const auto &it : IOTypeTitles) {
    const QString title = ioTypeTitle(it.first);
    const QString desc = ioTypeDescription(it.first);
    const QString tip = tr("%1: %2\nDouble-click (or select and click Add) "
                           "to add this device to the system.")
                            .arg(title, desc);
    QTableWidgetItem *periphItem = new QTableWidgetItem(title);
    periphItem->setData(Qt::UserRole, QVariant::fromValue(it.first));
    periphItem->setToolTip(tip);
    periphItem->setStatusTip(tip);
    QFont f = periphItem->font();
    f.setBold(true);
    periphItem->setFont(f);
    periphItem->setFlags(periphItem->flags() & ~Qt::ItemIsEditable);
    m_ui->peripheralsTable->setItem(row, 0, periphItem);

    QTableWidgetItem *descItem = new QTableWidgetItem(desc);
    descItem->setData(Qt::UserRole, QVariant::fromValue(it.first));
    descItem->setToolTip(tip);
    descItem->setStatusTip(tip);
    descItem->setFlags(descItem->flags() & ~Qt::ItemIsEditable);
    m_ui->peripheralsTable->setItem(row, 1, descItem);
    row++;
  }
  m_ui->peripheralsTable->resizeRowsToContents();
  new ResizeHook(m_ui->peripheralsTable->viewport(), [this] {
    QTimer::singleShot(0, m_ui->peripheralsTable,
                       [this] { m_ui->peripheralsTable->resizeRowsToContents(); });
  });
  m_ui->peripheralsTable->selectRow(0);

  connect(m_ui->peripheralsTable, &QTableWidget::itemDoubleClicked, this,
          [this](QTableWidgetItem *item) {
            this->createPeripheral(item->data(Qt::UserRole).value<IOType>());
          });

  // Explicit "Add" button, so that adding a device does not rely on knowing
  // about double-clicking.
  m_addButton = new QPushButton(QIcon(":/icons/plus.svg"),
                                tr("Add selected device"), this);
  setTips(m_addButton,
          tr("Create an instance of the selected device. It is placed at the "
             "next free address after the previous peripherals, so base "
             "addresses depend on the order in which devices are added and "
             "are reassigned when a parameter changes a device's size. Its "
             "base address and register symbols are listed under I/O "
             "exports."));
  m_addButton->setObjectName("addPeripheralButton");
  m_ui->gridLayout->addWidget(m_addButton, 1, 0);
  connect(m_addButton, &QPushButton::clicked, this, [this] {
    const auto items = m_ui->peripheralsTable->selectedItems();
    if (!items.isEmpty()) {
      createPeripheral(items.first()->data(Qt::UserRole).value<IOType>());
    }
  });
  connect(m_ui->peripheralsTable, &QTableWidget::itemSelectionChanged, this,
          [this] {
            m_addButton->setEnabled(
                !m_ui->peripheralsTable->selectedItems().isEmpty());
          });

  // Empty-state hint shown in the device area while no peripheral exists.
  m_emptyHint = new QLabel(
      tr("No peripherals yet.\nSelect a device in the list on the left and "
         "click \"Add selected device\" (or double-click it)."),
      this);
  m_emptyHint->setAlignment(Qt::AlignCenter);
  m_emptyHint->setWordWrap(true);
  m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_emptyHint->setStyleSheet("QLabel { font-style: italic; padding: 12px; }");
  m_ui->dockArea->addWidget(m_emptyHint, 0, 0, Qt::AlignCenter);

  // Setup MDI area
  connect(m_ui->mdiArea, &QMdiArea::subWindowActivated, this,
          [this](QMdiSubWindow *w) {
            if (w == nullptr) {
              setPeripheralTabActive(nullptr);
            } else {
              // MDI window -> QMainwindow -> QDockWidget -> IOBase widget...
              // Whew!
              auto *w1 = w->widget();
              auto *w2 = w1->findChildren<QDockWidget *>().at(0);
              auto *w3 = w2->widget();
              auto *peripheral = dynamic_cast<IOBase *>(w3);
              Q_ASSERT(peripheral != nullptr);
              this->setPeripheralTabActive(peripheral);
            }
          });

  connect(&IOManager::get(), &IOManager::peripheralRemoved, QPointer(this),
          &IOTab::removePeripheral);
  connect(m_ui->peripheralsTab, &QTabWidget::currentChanged, this,
          &IOTab::setPeripheralMDIWindowActive);
  connect(&IOManager::get(), &IOManager::memoryMapChanged, this,
          &IOTab::updateIOSymbolFilePreview);

  // Disable instantiating new peripherals during processor running
  connect(ProcessorHandler::get(), &ProcessorHandler::runStarted, this,
          [this] {
            m_ui->peripheralsTable->setEnabled(false);
            m_addButton->setEnabled(false);
          });
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          [this] {
            m_ui->peripheralsTable->setEnabled(true);
            m_addButton->setEnabled(
                !m_ui->peripheralsTable->selectedItems().isEmpty());
          });

  // Store peripheral state before exiting the program
  connect(RipesSettings::getObserver(RIPES_GLOBALSIGNAL_QUIT),
          &SettingObserver::modified, this, &IOTab::storePeripheralState);

  // Reload peripheral state from the last graceful exit of the program
  loadPeripheralState();

  m_ui->ioSymbols->setFont(QFont(Fonts::monospace, 11));
  setTips(m_ui->ioSymbols,
          tr("Exported symbols for all I/O devices.\nThese symbols may be "
             "referenced in Assembly\nor C-language programs (C: #include "
             "\"ripes_system.h\")."));
  setTips(m_ui->peripheralsTab,
          tr("Details of each added device: description, editable "
             "parameters, register map (offset, access, meaning) and the "
             "symbols exported for your program."));
  setTips(m_ui->mdiArea,
          tr("Live view of every added device. Close a device window to "
             "remove the device."));
  updateEmptyHint();

  // The default sizing of the splitter is fairly unuseable due to the MDIArea
  // minimizing the left-hand side layout. Size it once the tab has its real
  // width: the device list on the left and the details panel on the right
  // (register map, exports; it scrolls when narrow) get about a fifth each,
  // and the device area in the middle, where the large accelerator widgets
  // live, gets the rest. Extra width from resizing goes to the middle.
  m_ui->splitter->setStretchFactor(0, 0);
  m_ui->splitter->setStretchFactor(1, 1);
  m_ui->splitter->setStretchFactor(2, 0);
  new ResizeHook(m_ui->splitter, [this] { sizeSplitterOnce(); });
}

void IOTab::updateIOSymbolFilePreview() {
  const auto &headerPath = IOManager::get().cSymbolsHeaderpath();
  auto headerFile = QFile(headerPath);
  if (headerFile.open(QIODevice::ReadOnly)) {
    m_ui->ioSymbols->document()->setPlainText(
        QString::fromLatin1(headerFile.readAll()));
  }
}

IOBase *IOTab::createPeripheral(IOType type, int forcedID) {
  auto *peripheral = IOManager::get().createPeripheral(type, forcedID);

  // Create tab for peripheral
  auto *peripheralTab = new IOPeripheralTab(this, peripheral);
  m_ui->peripheralsTab->addTab(peripheralTab, peripheral->name());
  m_periphToTab[peripheral] = peripheralTab;

  // It seems excessive to create a QMainWindow for each peripheral but it seems
  // like the only way to mix MDI behaviour + dockable widgets that are able to
  // pop out to a separate window
  auto *mw = new QMainWindow(this);
  m_ui->dockArea->addWidget(
      mw); // Shouldn't be needed, but MDI windows aren't created without this?
  auto *dw = new QDockWidget();
  dw->setFeatures(dw->features() & ~QDockWidget::DockWidgetClosable);
  dw->setWidget(peripheral);
  dw->setAllowedAreas(Qt::AllDockWidgetAreas);
  mw->addDockWidget(Qt::TopDockWidgetArea, dw);
  auto *mdiw = m_ui->mdiArea->addSubWindow(mw);
  mdiw->setWindowTitle(peripheral->name());
  peripheral->setFocus();

  /* The following ensures that the MDI window which a peripheral is contained
   * within is resized when the widget itself is resized. It seems a bit
   * cumbersome, but this was the only way i wound to trigger both the
   * QMainWindow and the outer MDIWindow to register that its child widget has
   * changed in size, and adjust itself accordingly.
   */
  connect(peripheral, &IOBase::paramsChanged, this, [this, mw, mdiw] {
    // Ensure that a peripheral resize event, as a result of a parameter being
    // changed, as been processed, so that its new size has taken effect.
    QApplication::processEvents();
    mw->resize(mw->minimumSizeHint());
    mdiw->resize(mdiw->minimumSizeHint());
  });

  mdiw->setWindowFlags(Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                       Qt::WindowCloseButtonHint);

  // Disable MDI subwindow close button when running the processor
  connect(ProcessorHandler::get(), &ProcessorHandler::runStarted, mdiw,
          [this, mdiw] { mdiw->setWindowFlags(Qt::WindowTitleHint); });
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, mdiw,
          [this, mdiw] {
            mdiw->setWindowFlags(Qt::WindowTitleHint |
                                 Qt::WindowSystemMenuHint |
                                 Qt::WindowCloseButtonHint);
          });

  m_subWindows[peripheralTab] = mdiw;
  updateEmptyHint();
  m_pendingFit.push_back(mdiw);
  QTimer::singleShot(0, this, [this] { fitPendingSubWindows(); });
  return peripheral;
}

void IOTab::tabVisibilityChanged(bool visible) {
  if (visible)
    sizeSplitterOnce();
}

void IOTab::sizeSplitterOnce() {
  const int w = m_ui->splitter->width();
  // Hidden tab pages are laid out too; wait for the real window size.
  if (m_splitterSized || !m_ui->splitter->window()->isVisible() || w < 600)
    return;
  m_splitterSized = true;
  const int left = std::max(
      m_ui->verticalLayoutWidget->minimumSizeHint().width(), w * 22 / 100);
  const int right =
      std::max(m_ui->peripheralsTab->minimumWidth(), w * 25 / 100);
  m_ui->splitter->setSizes({left, std::max(0, w - left - right), right});
}

void IOTab::fitPendingSubWindows() {
  const QSize avail = m_ui->mdiArea->viewport()->size();
  // Not laid out yet (tab never shown): try again on the next resize.
  if (!m_ui->mdiArea->isVisible() || avail.width() < 100 ||
      avail.height() < 100)
    return;
  for (const auto &w : m_pendingFit) {
    if (!w)
      continue;
    const QSize minSize = w->minimumSizeHint();
    const QSize size =
        w->sizeHint().boundedTo(avail).expandedTo(minSize);
    w->resize(size);
    // Keep the window's top-left corner visible; anything beyond the area is
    // reached with the device area's scroll bars.
    const int x = std::clamp(w->x(), 0, std::max(0, avail.width() - size.width()));
    const int y =
        std::clamp(w->y(), 0, std::max(0, avail.height() - size.height()));
    w->move(x, y);
  }
  m_pendingFit.clear();
}

void IOTab::updateEmptyHint() {
  if (m_emptyHint) {
    m_emptyHint->setVisible(m_periphToTab.empty());
    m_emptyHint->raise();
  }
}

void IOTab::loadPeripheralState() {
  auto settings = RipesSettings::value(RIPES_SETTING_PERIPHERAL_SETTINGS)
                      .toString()
                      .toStdString();
  if (settings.empty()) {
    return;
  }

  std::istringstream in(settings);
  try {
    cereal::JSONInputArchive archive(in);

    // Following the serialization order of storePeripheralState(), we first
    // load the peripheral types and unique IDs, instantiate peripherals, and
    // then load the peripheral settings.
    PeriphIDs ids;
    try {
      archive(cereal::make_nvp("periphIDs", ids));
      for (const auto &id : ids) {
        if (id.typeId < NPERIPHERALS) {
          createPeripheral(static_cast<IOType>(id.typeId), id.uniqueId);
        }
      }

      for (const auto &periph : m_periphToTab) {
        IOBase *periphPtr = dynamic_cast<IOBase *>(periph.first);
        archive(cereal::make_nvp(periphPtr->serializedUniqueID(), *periphPtr));
      }
    } catch (const cereal::Exception &e) {
      /// @todo: build an error report
    }
  } catch (...) {
    // Could not load peripherals from settings...
    return;
  }
}

void IOTab::storePeripheralState() {
  std::ostringstream out;
  {
    cereal::JSONOutputArchive archive(out);

    // First, serialize the type of each peripheral, and then the peripheral
    // itself
    PeriphIDs ids;
    for (const auto &periph : m_periphToTab) {
      IOBase *periphPtr = dynamic_cast<IOBase *>(periph.first);
      assert(periphPtr);
      ids.push_back({periphPtr->iotype(), periphPtr->id()});
    }

    try {
      archive(cereal::make_nvp("periphIDs", ids));
      for (const auto &periph : m_periphToTab) {
        IOBase *periphPtr = dynamic_cast<IOBase *>(periph.first);
        archive(cereal::make_nvp(periphPtr->serializedUniqueID(), *periphPtr));
      }
    } catch (const cereal::Exception &e) {
      /// @todo: build an error report
    }
  }
  RipesSettings::setValue(RIPES_SETTING_PERIPHERAL_SETTINGS,
                          QString::fromStdString(out.str()));
}

void IOTab::setPeripheralTabActive(IOBase *peripheral) {
  if (peripheral == nullptr) {
    return;
  }
  m_ui->peripheralsTab->setCurrentWidget(m_periphToTab.at(peripheral));
}

void IOTab::setPeripheralMDIWindowActive(int tabIndex) {
  if (tabIndex == -1) {
    // No more peripherals
    return;
  }
  auto it = m_subWindows.find(m_ui->peripheralsTab->widget(tabIndex));
  if (it == m_subWindows.end()) {
    // We are currently creating the peripheral
    return;
  }

  m_ui->mdiArea->setActiveSubWindow(it->second);
}

void IOTab::removePeripheral(QObject *peripheral) {
  auto *tab = m_periphToTab.at(peripheral);
  Q_ASSERT(m_subWindows.count(tab) != 0);
  m_subWindows.erase(tab);
  m_periphToTab.at(peripheral)->deleteLater();
  m_periphToTab.erase(peripheral);
  updateEmptyHint();
}

IOTab::~IOTab() {
  /* Because of the way that IOBase objects signal to the IOTab that they have
   * been removed, we need to delete all IOBase objects before deleting the
   * IOTab itself. The default deletion mechanism is incorrect for this, given
   * that IOTab is first deleted, and then the underlying QObject is deleted
   * (which deletes its children, being the IOBase objects). We delete from the
   * subwindows because they are the top-level parent of the IOBase objects.
   */

  // Copy subwindows collection, so we can safely iterate through it
  // (m_subWindows is modified when deleting a subwindow).
  auto subWindowsCopy = m_subWindows;
  for (const auto &subwindow : subWindowsCopy) {
    delete subwindow.second;
  }
  delete m_ui;
}
} // namespace Ripes
