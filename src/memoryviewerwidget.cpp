#include "memoryviewerwidget.h"
#include "ui_memoryviewerwidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTableView>

#include "flowlayout.h"
#include "gotocombobox.h"
#include "memorymodel.h"
#include "processorhandler.h"
#include "radixselectorwidget.h"
#include "tabheader.h"

namespace Ripes {

MemoryViewerWidget::MemoryViewerWidget(QWidget *parent)
    : QWidget(parent), m_ui(new Ui::MemoryViewerWidget) {
  m_ui->setupUi(this);

  setupNavigationWidgets();
  connect(ProcessorHandler::get(), &ProcessorHandler::processorChanged, this,
          &MemoryViewerWidget::updateModel);
  updateModel();

  // During processor running, it should not be possible to interact with the
  // memory viewer.
  connect(ProcessorHandler::get(), &ProcessorHandler::runStarted, this,
          [this] { setEnabled(false); });
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          [this] { setEnabled(true); });
  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, [this] { this->updateView(); });
  connect(ProcessorHandler::get(), &ProcessorHandler::memoryFocusAddressChanged,
          this, &MemoryViewerWidget::setCentralAddress);
}

MemoryViewerWidget::~MemoryViewerWidget() { delete m_ui; }

void MemoryViewerWidget::setupNavigationWidgets() {
  // Navigation widgets are placed in a flow layout to allow minimzation of the
  // memory widget

  auto *flowLayout = new FlowLayout(m_ui->flowParentLayout);

  m_radixSelector = new RadixSelectorWidget(m_ui->flowParentLayout);
  m_goToSection = new GoToSectionComboBox(m_ui->flowParentLayout);
  m_goToRegister = new GoToRegisterComboBox(m_ui->flowParentLayout);

  flowLayout->addWidget(m_radixSelector);

  QHBoxLayout *layout = new QHBoxLayout();
  auto *regLabel = new QLabel(tr("Go to register:"), m_ui->flowParentLayout);
  const QString regTip =
      tr("Center the view on the address currently stored in a register "
         "(e.g. sp for the stack, gp for global data).");
  setTips(regLabel, regTip);
  setTips(m_goToRegister, regTip);
  layout->addWidget(regLabel);
  layout->addWidget(m_goToRegister);
  flowLayout->addItem(layout);

  layout = new QHBoxLayout();
  auto *secLabel = new QLabel(tr("Go to section:"), m_ui->flowParentLayout);
  const QString secTip =
      tr("Center the view on the start of a program section (.text = code, "
         ".data = global variables) or on an address you type in "
         "(\"Address...\").");
  setTips(secLabel, secTip);
  setTips(m_goToSection, secTip);
  layout->addWidget(secLabel);
  layout->addWidget(m_goToSection);
  flowLayout->addItem(layout);

  // Legend for the placeholder symbols used in the table.
  auto *legend = new QLabel(
      tr("X = never written (no value yet), - = outside the address space; "
         "greyed rows are uninitialized."),
      m_ui->flowParentLayout);
  legend->setWordWrap(true);
  flowLayout->addWidget(legend);
}

void MemoryViewerWidget::setCentralAddress(AInt address) {
  m_memoryModel->setCentralAddress(address);
}

void MemoryViewerWidget::updateView() { m_memoryModel->processorWasClocked(); }

void MemoryViewerWidget::updateModel() {
  auto *oldModel = m_memoryModel;

  auto *newModel = new MemoryModel(this);
  m_memoryModel = newModel;
  m_ui->memoryView->setModel(m_memoryModel);
  m_radixSelector->setRadix(m_memoryModel->getRadix());
  connect(m_radixSelector, &RadixSelectorWidget::radixChanged, m_memoryModel,
          &MemoryModel::setRadix);

  // Connect scroll events on the view to modify the center address of the model
  connect(m_ui->memoryView, &MemoryView::scrolled, newModel,
          [newModel](bool dir) {
            if (dir) {
              newModel->offsetCentralAddress(1);
            } else {
              newModel->offsetCentralAddress(-1);
            }
          });

  m_memoryModel->setRowsVisible(1);
  connect(m_ui->memoryView, &MemoryView::resized, newModel, [=, this] {
    int rowHeight = m_ui->memoryView->rowHeight(0);
    if (rowHeight != 0) {
      const auto rows = m_ui->memoryView->height() / rowHeight;
      newModel->setRowsVisible(rows);
    }
  });
  connect(m_goToSection, &GoToComboBox::jumpToAddress, m_memoryModel,
          &MemoryModel::setCentralAddress);
  connect(m_goToRegister, &GoToComboBox::jumpToAddress, m_memoryModel,
          &MemoryModel::setCentralAddress);

  m_ui->memoryView->horizontalHeader()->setSectionResizeMode(
      MemoryModel::Address, QHeaderView::Stretch);
  setTips(m_ui->memoryView,
          tr("Memory contents, one word per row (addresses increase "
             "upwards). Scroll with the mouse wheel; hover over a column "
             "header for its meaning."));

  if (oldModel) {
    delete oldModel;
  }
}
} // namespace Ripes
