#include "ioperipheraltab.h"
#include "ui_ioperipheraltab.h"

#include "editor/csyntaxhighlighter.h"
#include "fonts.h"
#include "io/iobase.h"
#include "io/iomanager.h"
#include "io/periphparammodel.h"
#include "io/registermapmodel.h"
#include "processorhandler.h"
#include "tabheader.h"

namespace Ripes {

IOPeripheralTab::IOPeripheralTab(QWidget *parent, IOBase *peripheral)
    : QWidget(parent), m_ui(new Ui::IOPeripheralTab), m_peripheral(peripheral) {
  m_ui->setupUi(this);
  m_ui->exports->setFont(QFont(Fonts::monospace, 11));

  m_ui->description->setText(m_peripheral->description());
  m_ui->registerMapView->setModel(new RegisterMapModel(peripheral, this));
  m_ui->registerMapView->horizontalHeader()->setStretchLastSection(true);
  m_ui->registerMapView->setWordWrap(true);
  m_ui->registerMapView->resizeColumnsToContents();
  setTips(m_ui->registerMapView,
          tr("Registers of this device. Address = base address of the device "
             "+ offset. Hover over a column header for its meaning."));
  setTips(m_ui->description,
          tr("How this device works and how a program talks to it"));
  setTips(m_ui->exports,
          tr("Symbols generated for this device. Use them in assembly (e.g. "
             "li a0, NAME) or in C after #include \"ripes_system.h\". They "
             "are updated automatically when parameters change."));

  if (peripheral->parameters().size() == 0) {
    m_ui->parameterGroupBox->hide();
  } else {
    m_ui->parameterView->setModel(new PeriphParamModel(peripheral, this));
    m_ui->parameterView->horizontalHeader()->setStretchLastSection(true);
    m_ui->parameterView->resizeColumnsToContents();
    m_ui->parameterView->setItemDelegateForColumn(
        PeriphParamModel::Value, new PeriphParamDelegate(this));
    setTips(m_ui->parameterView,
            tr("Configuration of this device. Double-click a value to "
               "change it (not possible while the processor is running)."));
  }

  // Disable parameter modification during processor running
  connect(ProcessorHandler::get(), &ProcessorHandler::runStarted, this,
          [this] { m_ui->parameterView->setEnabled(false); });
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          [this] { m_ui->parameterView->setEnabled(true); });

  // Explain how the exported symbol names are formed from the instance name
  // (e.g. "D-Pad 0" -> D_PAD_0_..., "RTC 0" -> RTC_0_...).
  m_ui->exports->setToolTip(
      m_ui->exports->toolTip() + "\n\n" +
      tr("Symbol prefix of this device: %1_ (the instance name \"%2\" in "
         "upper case, with spaces and '-' replaced by '_'). Example: "
         "%1_BASE is the base address.")
          .arg(cName(m_peripheral->name()), m_peripheral->name()));
  m_ui->description->setText(
      m_peripheral->description() + "\n\n" +
      tr("Exported symbols use the prefix %1_ (e.g. %1_BASE).")
          .arg(cName(m_peripheral->name())));

  updateExportsInfo();

  connect(peripheral, &IOBase::paramsChanged, this,
          &IOPeripheralTab::updateExportsInfo);
}

void IOPeripheralTab::updateExportsInfo() {
  m_ui->exports->clear();
  auto symbols = IOManager::get().assemblerSymbolsForPeriph(m_peripheral);
  for (const auto &symbol : symbols) {
    m_ui->exports->appendPlainText("#define " + symbol.first.v + " " + "(0x" +
                                   QString::number(symbol.second, 16) + ")");
  }
  CSyntaxHighlighter(m_ui->exports->document()).rehighlight();
}

IOPeripheralTab::~IOPeripheralTab() { delete m_ui; }

} // namespace Ripes
