#include "pipelinediagramwidget.h"
#include "ui_pipelinediagramwidget.h"

#include <QClipboard>
#include <QHeaderView>

#include "pipelinediagrammodel.h"
#include "processorhandler.h"
#include "ripessettings.h"
#include "tabheader.h"

#include <QLabel>

namespace Ripes {

PipelineDiagramWidget::PipelineDiagramWidget(PipelineDiagramModel *model,
                                             QWidget *parent)
    : QDialog(parent), m_ui(new Ui::PipelineDiagramWidget) {
  m_ui->setupUi(this);

  m_stageModel = model;
  m_ui->pipelineDiagramView->setModel(m_stageModel);

  m_ui->pipelineDiagramView->resizeColumnsToContents();
  m_ui->copy->setIcon(QIcon(":/icons/documents.svg"));
  m_ui->copy->setStatusTip(m_ui->copy->toolTip());

  // Header with explanation + help, and a legend of the stage colours.
  m_ui->verticalLayout->insertWidget(
      0, createTabHeader(
             tr("Pipeline diagram"),
             tr("One row per instruction of the program, one column per "
                "clock cycle. A cell names the pipeline stage the instruction "
                "occupies in that cycle; \"-\" means it stayed in the same "
                "stage (stall). Read a row left to right to follow one "
                "instruction through the pipeline."),
             "06-processors-pipelines", this));
  std::vector<LegendEntry> legend;
  int count = 0;
  for (auto idx : ProcessorHandler::getProcessor()->structure().stageIt()) {
    Q_UNUSED(idx);
    ++count;
  }
  int ordinal = 0;
  for (auto idx : ProcessorHandler::getProcessor()->structure().stageIt()) {
    const QString name = ProcessorHandler::getProcessor()->stageName(idx);
    legend.push_back(
        {[ordinal, count] {
           return PipelineDiagramModel::stageColor(ordinal, count);
         },
         name, tr("Instruction is in the %1 stage").arg(name)});
    ++ordinal;
  }
  legend.push_back({&PipelineDiagramModel::stallColor, tr("- (stall)"),
                    tr("Instruction stayed in the same stage as in the "
                       "previous cycle")});
  m_ui->verticalLayout->insertWidget(1, createLegend(legend, this));
  auto *notes = new QLabel(
      tr("Notes: \"-\" = stalled (same stage as in the previous cycle). An "
         "entry such as \"IF/WB\" means the same instruction address is in "
         "two stages in that cycle (e.g. a loop body fetched again). "
         "Instructions that are flushed (e.g. after a taken branch) simply "
         "disappear from their row."),
      this);
  notes->setWordWrap(true);
  notes->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  m_ui->verticalLayout->insertWidget(2, notes);
  const QString settingTip =
      tr("The number of recorded cycles is set in Edit > Settings > "
         "Environment > \"Max. pipeline diagram cycles\". Cycles beyond "
         "this limit are not recorded.");
  m_ui->infoLabel->setToolTip(settingTip);
  m_ui->infoLabel->setText(
      "<i>" +
      tr("# of recorded cycles: Edit > Settings > Environment > \"Max. "
         "pipeline diagram cycles\"") +
      "</i>");
  m_ui->pipelineDiagramView->setToolTip(
      tr("Hover over a cell to see the cycle, instruction and stage"));
  if (m_stageModel->columnCount() <= 1) {
    m_ui->infoLabel->setText(
        "<i>" +
        tr("No cycles recorded yet - step or run the processor, then reopen "
           "this diagram. The number of recorded cycles can be changed in "
           "the settings.") +
        "</i>");
  }

  m_stageModel->prepareForView();
}

PipelineDiagramWidget::~PipelineDiagramWidget() { delete m_ui; }

void PipelineDiagramWidget::on_copy_clicked() {
  // Copy entire table to clipboard, including headers
  Q_ASSERT(m_stageModel != nullptr);
  QString textualRepr;

  // Copy headers
  textualRepr.append('\t');
  for (int j = 0; j < m_stageModel->columnCount(); j++) {
    textualRepr.append(m_stageModel->headerData(j, Qt::Horizontal).toString());
    textualRepr.append('\t');
  }
  textualRepr.append('\n');
  // Copy data
  for (int i = 0; i < m_stageModel->rowCount(); ++i) {
    textualRepr.append(m_stageModel->headerData(i, Qt::Vertical).toString());
    textualRepr.append('\t');
    for (int j = 0; j < m_stageModel->columnCount(); j++) {
      textualRepr.append(
          m_stageModel->data(m_stageModel->index(i, j)).toString());
      textualRepr.append('\t');
    }
    textualRepr.append('\n');
  }
  QApplication::clipboard()->setText(textualRepr);
}
} // namespace Ripes
