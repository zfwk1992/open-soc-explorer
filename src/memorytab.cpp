#include "memorytab.h"
#include "ui_memorytab.h"

#include <QGraphicsItem>
#include <QPushButton>
#include <QToolBar>

#include "cachesim/cachesim.h"
#include "io/iomanager.h"
#include "io/memorymapmodel.h"
#include "processorhandler.h"
#include "tabheader.h"

namespace Ripes {

MemoryTab::MemoryTab(QToolBar *toolbar, QWidget *parent)
    : RipesTab(toolbar, parent), m_ui(new Ui::MemoryTab) {
  m_ui->setupUi(this);

  m_ui->gridLayout_3->addWidget(
      createTabHeader(
          tr("Memory"),
          tr("Shows the simulated memory around a chosen address: each row "
             "is one word, with its address, the whole word and its "
             "individual bytes (byte 0 = lowest address, little-endian). "
             "Scroll with the mouse wheel or jump with \"Go to\". The memory "
             "map on the right lists where the program sections and "
             "peripherals live."),
          "02-quickstart", this),
      0, 0);
  m_ui->gridLayout_3->setRowStretch(1, 1);

  m_ui->memoryViewerWidget->updateModel();
  m_ui->memoryViewerWidget->updateView();

  m_ui->memoryMapView->setModel(new MemoryMapModel(&IOManager::get(), this));
  m_ui->memoryMapView->horizontalHeader()->setSectionResizeMode(
      MemoryMapModel::Name, QHeaderView::ResizeToContents);
  m_ui->memoryMapView->horizontalHeader()->setSectionResizeMode(
      MemoryMapModel::AddressRange, QHeaderView::ResizeToContents);
  m_ui->memoryMapView->horizontalHeader()->setSectionResizeMode(
      MemoryMapModel::Description, QHeaderView::Stretch);
  m_ui->memoryMapView->verticalHeader()->hide();
  setTips(m_ui->memoryMapView,
          tr("Memory map: the address ranges used by the program sections "
             "(.text, .data, ...) and by the I/O peripherals. Empty until a "
             "program is loaded or a peripheral is added."));
  m_ui->memoryMapView->horizontalHeader()->setSectionResizeMode(
      MemoryMapModel::Size, QHeaderView::ResizeToContents);
  m_ui->splitter->setStretchFactor(0, 2);
  m_ui->splitter->setStretchFactor(1, 1);
}

MemoryTab::~MemoryTab() { delete m_ui; }

void MemoryTab::setCentralAddress(unsigned int address) {
  m_ui->memoryViewerWidget->setCentralAddress(address);
}

} // namespace Ripes
