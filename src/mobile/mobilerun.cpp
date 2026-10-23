// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "mobilerun.h"

#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QVBoxLayout>

#include "assembler/program.h"
#include "io/iomanager.h"
#include "processorhandler.h"
#include "processors/ripesvsrtlprocessor.h"
#include "ripessettings.h"
#include "theme/theme.h"
#include "touch.h"

namespace Ripes {
namespace Mobile {

using namespace Theme;

namespace {

vsrtl::core::Design *reversibleDesign() {
  return dynamic_cast<vsrtl::core::Design *>(
      ProcessorHandler::getProcessorNonConst());
}

} // namespace

RunPage::RunPage(QWidget *parent) : QWidget(parent) {
  setObjectName("mobileRunPage");
  auto *v = new QVBoxLayout(this);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  m_page = new ScrollPage(this);
  m_page->setObjectName("mobileRunScroll");
  v->addWidget(m_page, 1);
  QWidget *c = m_page->content();
  auto *l = m_page->contentLayout();

  // Mission context.
  m_missionCard = new Card(c);
  m_missionCard->setAccent(tokens().accent);
  m_missionTitle = makeLabel(m_missionCard);
  m_missionTitle->setObjectName("mobileTitle");
  m_missionCard->body()->addWidget(m_missionTitle);
  auto *mrow = new QHBoxLayout();
  mrow->setSpacing(8);
  m_missionLoad = makeButton(QString(), m_missionCard, "primary");
  m_missionLoad->setObjectName("mobileRunLoadExample");
  m_missionProcessor = makeButton(QString(), m_missionCard);
  m_missionProcessor->setObjectName("mobileRunSelectProcessor");
  mrow->addWidget(m_missionLoad);
  mrow->addWidget(m_missionProcessor);
  m_missionCard->body()->addLayout(mrow);
  connect(m_missionLoad, &QAbstractButton::clicked, this,
          &RunPage::loadMissionExampleRequested);
  connect(m_missionProcessor, &QAbstractButton::clicked, this,
          &RunPage::selectMissionProcessorRequested);
  l->addWidget(m_missionCard);
  m_missionCard->setVisible(false);

  // Live status and the trace's engine / model / fidelity.
  m_statusCard = new Card(c);
  m_status = makeLabel(m_statusCard);
  m_status->setObjectName("mobileRunStatus");
  m_statusCard->body()->addWidget(m_status);
  m_banner = new HudBanner(m_statusCard);
  m_banner->setObjectName("mobileHudBanner");
  m_statusCard->body()->addWidget(m_banner);
  l->addWidget(m_statusCard);

  // Processor.
  m_processorCard = new Card(c);
  m_processor = new QComboBox(m_processorCard);
  m_processor->setObjectName("mobileProcessorChoice");
  m_processor->setMinimumHeight(kTouchTarget + 4);
  m_processor->setFocusPolicy(Qt::NoFocus);
  // Long entries must not widen the page beyond the screen.
  m_processor->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_processor->setMinimumContentsLength(8);
  m_processor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  for (const auto &[id, desc] : ProcessorRegistry::getAvailableProcessors())
    m_processor->addItem(desc->name + " (" + enumToString(id) + ")",
                         static_cast<int>(id));
  m_processorCard->body()->addWidget(m_processor);
  m_processorInfo = makeLabel(m_processorCard);
  m_processorInfo->setObjectName("mobileMuted");
  m_processorCard->body()->addWidget(m_processorInfo);
  connect(m_processor, qOverload<int>(&QComboBox::activated), this,
          [this](int index) {
            const auto id =
                static_cast<ProcessorID>(m_processor->itemData(index).toInt());
            if (id != ProcessorHandler::getID())
              selectProcessor(id);
          });
  l->addWidget(m_processorCard);

  // Program.
  m_programCard = new Card(c);
  m_examples = new QComboBox(m_programCard);
  m_examples->setObjectName("mobileExampleChoice");
  m_examples->setMinimumHeight(kTouchTarget + 4);
  m_examples->setFocusPolicy(Qt::NoFocus);
  // Long entries must not widen the page beyond the screen.
  m_examples->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_examples->setMinimumContentsLength(8);
  m_examples->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_programCard->body()->addWidget(m_examples);
  connect(m_examples, qOverload<int>(&QComboBox::activated), this,
          [this](int index) {
            const QString file = m_examples->itemData(index).toString();
            m_examples->setCurrentIndex(0);
            if (file.isEmpty())
              return;
            QFile f(file);
            if (f.open(QIODevice::ReadOnly))
              setSource(QString::fromUtf8(f.readAll()));
          });
  m_editor = new QPlainTextEdit(m_programCard);
  m_editor->setObjectName("mobileProgramEditor");
  m_editor->setFont(
      monoFont(font().pointSizeF() > 0 ? font().pointSizeF() : 11));
  m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_editor->setTabChangesFocus(false);
  m_editor->setInputMethodHints(Qt::ImhNoAutoUppercase |
                                Qt::ImhNoPredictiveText);
  m_programCard->body()->addWidget(m_editor);
  m_assemblerStatus = makeLabel(m_programCard);
  m_assemblerStatus->setObjectName("mobileAssemblerStatus");
  m_programCard->body()->addWidget(m_assemblerStatus);
  l->addWidget(m_programCard);
  m_page->finish();

  // Simulator controls (fixed above the navigation bar).
  auto *controls = new QWidget(this);
  controls->setObjectName("mobileControlBar");
  auto *cl = new QHBoxLayout(controls);
  cl->setContentsMargins(8, 6, 8, 6);
  cl->setSpacing(6);
  m_reset = makeButton(QString(), controls);
  m_reset->setObjectName("mobileReset");
  m_back = makeButton(QString(), controls);
  m_back->setObjectName("mobileStepBack");
  m_clock = makeButton(QString(), controls, "primary");
  m_clock->setObjectName("mobileClock");
  m_run = makeButton(QString(), controls, "gold");
  m_run->setObjectName("mobileRun");
  for (auto *b : {m_reset, m_back, m_clock, m_run})
    cl->addWidget(b);
  v->addWidget(controls);
  connect(m_reset, &QAbstractButton::clicked, this, [] {
    RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
  });
  connect(m_back, &QAbstractButton::clicked, this, &RunPage::reverse);
  connect(m_clock, &QAbstractButton::clicked, this, [] {
    if (!ProcessorHandler::isRunning())
      ProcessorHandler::clock();
  });
  connect(m_run, &QAbstractButton::clicked, this, &RunPage::toggleRun);

  auto *handler = ProcessorHandler::get();
  connect(handler, &ProcessorHandler::procStateChangedNonRun, this,
          &RunPage::updateStatus);
  connect(handler, &ProcessorHandler::runStarted, this, [this] {
    m_statusTimer.start();
    updateControls();
  });
  connect(handler, &ProcessorHandler::runFinished, this, [this] {
    m_statusTimer.stop();
    updateStatus();
  });
  connect(handler, &ProcessorHandler::processorChanged, this, [this] {
    syncProcessorChoice();
    // The assembler depends on the ISA: assemble the program again.
    assemble();
  });
  m_statusTimer.setInterval(250);
  connect(&m_statusTimer, &QTimer::timeout, this, &RunPage::updateStatus);

  m_assembleTimer.setSingleShot(true);
  m_assembleTimer.setInterval(600);
  connect(&m_assembleTimer, &QTimer::timeout, this, &RunPage::assemble);
  connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
    fitEditor();
    if (!m_settingSource)
      m_assembleTimer.start();
  });

  retranslate();
  syncProcessorChoice();
  // The program of the previous session (shared with the desktop editor).
  setSource(RipesSettings::value(RIPES_SETTING_SOURCECODE).toString());
}

void RunPage::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    retranslate();
}

void RunPage::retranslate() {
  m_missionLoad->setText(tr("Load example"));
  m_missionProcessor->setText(tr("Select processor"));
  m_statusCard->setTitle(tr("Simulator"));
  m_processorCard->setTitle(tr("Processor model"));
  m_programCard->setTitle(tr("Program (RISC-V assembly)"));
  m_editor->setPlaceholderText(
      tr("Type RISC-V assembly here, or load an example."));
  m_reset->setText("⟲ " + tr("Reset"));
  m_back->setText("◀ " + tr("Back"));
  m_clock->setText(tr("Clock") + " ▶");
  m_examples->clear();
  m_examples->addItem(tr("Load an example program…"), QString());
  for (const auto &file : QDir(":/examples/assembly/").entryList(QDir::Files))
    m_examples->addItem(file, ":/examples/assembly/" + file);
  setMission(m_missionNumber, m_missionTitleText);
  syncProcessorChoice();
  updateStatus();
  // Re-render the assembler status in the new language.
  if (m_errors.isEmpty() && !m_editor->toPlainText().trimmed().isEmpty())
    m_assemblerStatus->setText(
        QString("<span style='color:%1'>✓</span> ").arg(tokens().ok.name()) +
        tr("Assembled and loaded: %1 bytes of code.").arg(m_programBytes));
}

QString RunPage::source() const { return m_editor->toPlainText(); }

void RunPage::setSource(const QString &source) {
  m_settingSource = true;
  m_editor->setPlainText(source);
  m_settingSource = false;
  fitEditor();
  assembleNow();
}

void RunPage::assembleNow() {
  m_assembleTimer.stop();
  assemble();
}

QString RunPage::assemblerStatus() const { return m_assemblerStatus->text(); }

void RunPage::assemble() {
  if (ProcessorHandler::isRunning()) {
    m_assembleTimer.start();
    return;
  }
  const QString source = m_editor->toPlainText();
  RipesSettings::setValue(RIPES_SETTING_SOURCECODE, source);
  auto res = ProcessorHandler::getAssembler()->assembleRaw(
      source, &IOManager::get().assemblerSymbols());
  m_errors.clear();
  if (res.errors.empty()) {
    ProcessorHandler::loadProgram(std::make_shared<Program>(res.program));
    m_programBytes = ProcessorHandler::getCurrentProgramSize();
    m_assemblerStatus->setText(
        source.trimmed().isEmpty()
            ? QString()
            : QString("<span style='color:%1'>✓</span> ")
                      .arg(tokens().ok.name()) +
                  tr("Assembled and loaded: %1 bytes of code.")
                      .arg(m_programBytes));
  } else {
    for (const auto &e : res.errors)
      m_errors << tr("line %1: %2")
                      .arg(e.isKnownSourceLine() ? e.sourceLine() + 1 : 0)
                      .arg(e.errorMessage());
    QStringList shown;
    for (const auto &e : m_errors.mid(0, 5))
      shown << e.toHtmlEscaped();
    if (m_errors.size() > 5)
      shown << tr("… and %1 more").arg(m_errors.size() - 5);
    m_assemblerStatus->setText(
        QString("<span style='color:%1'><b>%2</b></span><br>%3")
            .arg(tokens().bad.name(),
                 tr("Not assembled – the previous program stays loaded:"),
                 shown.join("<br>")));
  }
  updateStatus();
}

void RunPage::fitEditor() {
  // The editor grows with its text, so that only the page scrolls.
  const QFontMetrics fm(m_editor->font());
  int lines = 0;
  for (QTextBlock b = m_editor->document()->begin(); b.isValid(); b = b.next())
    lines += std::max(1, b.layout() ? b.layout()->lineCount() : 1);
  lines = std::max(lines, 8);
  m_editor->setFixedHeight(lines * fm.lineSpacing() + 2 * 8 +
                           int(m_editor->document()->documentMargin() * 2));
}

void RunPage::setMission(int number, const QString &title) {
  m_missionNumber = number;
  m_missionTitleText = title;
  m_missionCard->setVisible(number >= 0);
  if (number >= 0) {
    m_missionCard->setTitle(tr("Mission %1").arg(number));
    m_missionTitle->setText(title.toHtmlEscaped());
  }
}

void RunPage::setBadges(const std::vector<HudBanner::Badge> &badges,
                        const QString &note) {
  m_banner->setBadges(badges, note);
}

void RunPage::selectProcessor(ProcessorID id) {
  if (ProcessorHandler::isRunning())
    ProcessorHandler::stopRun();
  const auto &desc = ProcessorRegistry::getDescription(id);
  ProcessorHandler::selectProcessor(id, desc.isaInfo().defaultExtensions,
                                    desc.defaultRegisterVals);
}

void RunPage::syncProcessorChoice() {
  const int index =
      m_processor->findData(static_cast<int>(ProcessorHandler::getID()));
  if (index >= 0)
    m_processor->setCurrentIndex(index);
  const auto &desc =
      ProcessorRegistry::getDescription(ProcessorHandler::getID());
  m_processorInfo->setText(desc.description.toHtmlEscaped());
}

void RunPage::reverse() {
  if (ProcessorHandler::isRunning())
    return;
  if (auto *design = reversibleDesign(); design && design->canReverse())
    design->reverse();
  updateControls();
}

void RunPage::toggleRun() {
  if (ProcessorHandler::isRunning())
    ProcessorHandler::stopRun();
  else if (!ProcessorHandler::getProcessor()->finished())
    ProcessorHandler::run();
  updateControls();
}

void RunPage::updateControls() {
  const bool running = ProcessorHandler::isRunning();
  const bool finished = ProcessorHandler::getProcessor()->finished();
  auto *design = reversibleDesign();
  m_run->setText(running ? "■ " + tr("Stop") : tr("Run") + " ▶▶");
  m_clock->setEnabled(!running && !finished);
  m_run->setEnabled(running || !finished);
  m_reset->setEnabled(!running);
  m_back->setEnabled(!running && design && design->canReverse());
  m_processor->setEnabled(!running);
  m_editor->setReadOnly(running);
  m_missionLoad->setEnabled(!running);
  m_missionProcessor->setEnabled(!running);
}

void RunPage::updateStatus() {
  const auto *proc = ProcessorHandler::getProcessor();
  const auto cycles = proc->getCycleCount();
  const auto retired = proc->getInstructionsRetired();
  QString state;
  if (ProcessorHandler::isRunning())
    state = tr("running…");
  else if (proc->finished())
    state = tr("finished");
  else if (cycles == 0)
    state = tr("ready (reset)");
  else
    state = tr("paused");
  const QString cpi =
      cycles && retired
          ? QString::number(double(cycles) / double(retired), 'f', 2)
          : QString("–");
  m_status->setText(
      tr("<b>%1</b> · cycle %2 · %3 instructions retired · CPI %4")
          .arg(state)
          .arg(cycles)
          .arg(retired)
          .arg(cpi));
  updateControls();
}

} // namespace Mobile
} // namespace Ripes
