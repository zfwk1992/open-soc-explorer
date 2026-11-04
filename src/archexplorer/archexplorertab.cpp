#include "archexplorertab.h"
#include "ui_archexplorertab.h"

#include "archscene.h"
#include "archview.h"
#include "help.h"
#include "ripessettings.h"

#include <QAction>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace Ripes {

namespace {

constexpr int DefaultPlayIntervalMs = 1500;
constexpr int StepSummaryLength = 70;

// QSettings keys for the splitter layout. Plain QSettings (organization and
// application names are set by RipesSettings) so that no new entry in the
// shared RipesSettings defaults table is required.
const char *const kOpenedFilesKey = "archexplorer/opened_files";
const char *const kLastTopologyKey = "archexplorer/last_topology";

/// Display order of the builtin topologies: real machines first, the
/// template last; anything unknown keeps file-name order in between.
int builtinRank(const QString &id) {
  static const QStringList order = {"ps3", "neogeo", "gpu_system",
                                    "npu_system"};
  const int i = order.indexOf(id);
  if (i >= 0)
    return i;
  return id == "example_minimal" ? 1000 : 100;
}

const char *const kSplitterKeys[] = {
    "archexplorer/splitter_main", "archexplorer/splitter_left",
    "archexplorer/splitter_center", "archexplorer/splitter_right"};

QString plainToHtml(const QString &text) {
  return text.toHtmlEscaped().replace(QStringLiteral("\n"),
                                      QStringLiteral("<br/>"));
}

/// First sentence of @p text, shortened to about StepSummaryLength characters.
QString summarize(const QString &text) {
  QString s = text.simplified();
  int cut = -1;
  bool sentenceEnd = false;
  for (const QString &sep : {QStringLiteral("。"), QStringLiteral("；"),
                             QStringLiteral(". "), QStringLiteral("; "),
                             QStringLiteral("：")}) {
    const int i = s.indexOf(sep);
    if (i > 0 && (cut < 0 || i < cut)) {
      cut = i;
      sentenceEnd = sep.startsWith(QChar(0x3002)) || sep.startsWith('.');
    }
  }
  if (cut > 0 && cut + 1 < s.size()) {
    // Keep a full stop; a clause cut at ";" / "：" continues with "…".
    s = sentenceEnd ? s.left(cut + 1)
                    : s.left(cut).trimmed() + QChar(0x2026);
  }
  if (s.size() > StepSummaryLength)
    s = s.left(StepSummaryLength - 1).trimmed() + QChar(0x2026);
  return s;
}

QLabel *sectionLabel(const QString &text, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  QFont f = label->font();
  f.setBold(true);
  label->setFont(f);
  return label;
}

QPixmap blockKindSwatch(ArchBlockKind kind, bool dark, const QPalette &pal) {
  QPixmap pm(28, 16);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(pal.color(QPalette::Text), 1.0));
  p.setBrush(archBlockKindColor(kind, dark));
  p.drawRoundedRect(QRectF(1.5, 1.5, 25, 13), 3, 3);
  return pm;
}

QPixmap busKindSwatch(ArchBusKind kind, bool dark) {
  QPixmap pm(28, 16);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const QPen pen = archBusKindPen(kind, dark);
  p.setPen(pen);
  p.drawLine(QPointF(2, 8), QPointF(21, 8));
  QPolygonF head;
  head << QPointF(27, 8) << QPointF(20, 4.5) << QPointF(20, 11.5);
  p.setPen(QPen(pen.color(), 1.0));
  p.setBrush(pen.color());
  p.drawPolygon(head);
  return pm;
}

QPixmap highlightSwatch(const QPalette &pal, bool dimmed, bool dark) {
  QPixmap pm(28, 16);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const QColor hl = pal.color(QPalette::Highlight);
  if (dimmed) {
    p.setOpacity(0.25);
    p.setPen(QPen(pal.color(QPalette::Text), 1.0));
    p.setBrush(archBlockKindColor(ArchBlockKind::Misc, dark));
    p.drawRoundedRect(QRectF(3, 3, 22, 10), 3, 3);
  } else {
    QColor glow = hl;
    glow.setAlpha(110);
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawRoundedRect(QRectF(0.5, 0.5, 27, 15), 5, 5);
    p.setPen(QPen(hl, 2.0));
    p.setBrush(archBlockKindColor(ArchBlockKind::Misc, dark));
    p.drawRoundedRect(QRectF(3.5, 3.5, 21, 9), 3, 3);
  }
  return pm;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

ArchExplorerTab::ArchExplorerTab(QToolBar *toolbar, QWidget *parent)
    : RipesTab(toolbar, parent), m_ui(new Ui::ArchExplorerTab) {
  m_ui->setupUi(this);

  m_scene = new ArchScene(this);
  // The legend is shown in the side panel; the in-scene copy is only used
  // for exported images.
  m_scene->setLegendVisible(false);
  buildUi();
  m_view->setScene(m_scene);

  m_playTimer = new QTimer(this);
  m_playTimer->setInterval(DefaultPlayIntervalMs);
  connect(m_playTimer, &QTimer::timeout, this, [this] {
    const ArchFlow *flow = currentFlow();
    if (flow != nullptr && m_stepIndex + 1 < flow->steps.size())
      nextStep();
    else
      setPlaying(false);
  });

  setupToolbar();
  setupShortcuts();

  connect(m_archCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    setCurrentArchitecture(index < 0 ? -1
                                     : m_archCombo->itemData(index).toInt());
  });
  connect(m_flowCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    setCurrentFlow(index < 0 ? -1 : m_flowCombo->itemData(index).toInt());
  });
  connect(m_stepList, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0)
      setStep(row - 1); // row 0 is the overview
  });
  connect(m_speedCombo, &QComboBox::currentIndexChanged, this,
          [this] { updatePlayInterval(); });
  connect(m_scene, &ArchScene::blockSelectionChanged, this,
          [this](const QString &id) {
            if (id.isEmpty())
              clearBlockDetails();
            else
              showBlockDetails(id);
          });
  connect(m_view, &ArchView::zoomChanged, this, [this](qreal scale) {
    m_zoom = scale;
    updateStatus();
  });
  connect(m_connTable, &QTableWidget::cellDoubleClicked, this,
          [this](int row, int) {
            const auto *item = m_connTable->item(row, 2);
            if (item != nullptr)
              m_scene->selectBlock(item->data(Qt::UserRole).toString());
          });
  connect(ThemeManager::get(), &ThemeManager::themeChanged, this,
          &ArchExplorerTab::onThemeChanged);

  restoreSplitterState();
  for (auto *splitter :
       {m_mainSplitter, m_leftSplitter, m_centerSplitter, m_rightSplitter}) {
    connect(splitter, &QSplitter::splitterMoved, this,
            [this] { saveSplitterState(); });
  }

  clearBlockDetails();
  populateArchitectures();
}

ArchExplorerTab::~ArchExplorerTab() { delete m_ui; }

void ArchExplorerTab::buildUi() {
  auto *root = m_ui->rootLayout;

  // ----- header: title, one-line description, help button
  auto *header = new QFrame(this);
  header->setFrameShape(QFrame::StyledPanel);
  header->setAutoFillBackground(true);
  header->setBackgroundRole(QPalette::AlternateBase);
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(10, 6, 6, 6);
  auto *titles = new QVBoxLayout();
  titles->setSpacing(1);
  m_titleLabel = new QLabel(header);
  QFont titleFont = m_titleLabel->font();
  titleFont.setBold(true);
  titleFont.setPointSizeF(titleFont.pointSizeF() + 3);
  m_titleLabel->setFont(titleFont);
  m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_subtitleLabel = new QLabel(header);
  m_subtitleLabel->setWordWrap(true);
  auto *hint = new QLabel(
      tr("Choose a topology and a data flow on the left, step with ← / → or "
         "press Space to play. Click a block for its details; scroll to zoom, "
         "drag to pan."),
      header);
  hint->setWordWrap(true);
  hint->setForegroundRole(QPalette::PlaceholderText);
  titles->addWidget(m_titleLabel);
  titles->addWidget(m_subtitleLabel);
  titles->addWidget(hint);
  headerLayout->addLayout(titles, 1);
  m_helpButton = new QToolButton(header);
  m_helpButton->setObjectName("helpButton");
  m_helpButton->setText(QStringLiteral("?"));
  QFont helpFont = m_helpButton->font();
  helpFont.setBold(true);
  helpFont.setPointSizeF(helpFont.pointSizeF() + 2);
  m_helpButton->setFont(helpFont);
  m_helpButton->setMinimumSize(32, 32);
  m_helpButton->setToolTip(
      tr("Open the user guide chapter on the Architecture Explorer"));
  m_helpButton->setStatusTip(m_helpButton->toolTip());
  connect(m_helpButton, &QToolButton::clicked, this,
          [] { showHelp(QStringLiteral("11-arch-explorer")); });
  headerLayout->addWidget(m_helpButton, 0, Qt::AlignTop);
  root->addWidget(header);

  // ----- main splitter
  m_mainSplitter = new QSplitter(Qt::Horizontal, this);
  m_mainSplitter->setChildrenCollapsible(false);
  root->addWidget(m_mainSplitter, 1);

  // ===== left: topology + data flow
  m_leftSplitter = new QSplitter(Qt::Vertical, m_mainSplitter);
  m_leftSplitter->setChildrenCollapsible(false);

  auto *topoBox = new QGroupBox(tr("Topology"), m_leftSplitter);
  auto *topoLayout = new QVBoxLayout(topoBox);
  topoLayout->setContentsMargins(6, 4, 6, 6);
  m_archCombo = new QComboBox(topoBox);
  m_archCombo->setObjectName("archCombo");
  m_archCombo->setToolTip(
      tr("Architecture (topology) to display. Hover an entry for a short "
         "description; “Open architecture JSON…” in the toolbar adds your "
         "own files."));
  m_archCombo->setStatusTip(tr("Select the architecture diagram to display"));
  m_archCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_archCombo->setMinimumContentsLength(16);
  topoLayout->addWidget(m_archCombo);
  m_archDescription = new QTextBrowser(topoBox);
  m_archDescription->setOpenExternalLinks(false);
  m_archDescription->setToolTip(tr("Overview of the selected topology"));
  m_archDescription->setStatusTip(m_archDescription->toolTip());
  m_archDescription->setMinimumHeight(60);
  topoLayout->addWidget(m_archDescription, 1);

  auto *flowBox = new QGroupBox(tr("Data flow"), m_leftSplitter);
  auto *flowLayout = new QVBoxLayout(flowBox);
  flowLayout->setContentsMargins(6, 4, 6, 6);
  m_flowCombo = new QComboBox(flowBox);
  m_flowCombo->setObjectName("flowCombo");
  m_flowCombo->setToolTip(
      tr("Data flow to animate: a sequence of steps that highlights the "
         "blocks and buses taking part"));
  m_flowCombo->setStatusTip(tr("Select the data flow to step through"));
  m_flowCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_flowCombo->setMinimumContentsLength(16);
  flowLayout->addWidget(m_flowCombo);
  m_flowDescription = new QLabel(flowBox);
  m_flowDescription->setWordWrap(true);
  m_flowDescription->setForegroundRole(QPalette::PlaceholderText);
  flowLayout->addWidget(m_flowDescription);
  m_stepList = new QListWidget(flowBox);
  m_stepList->setObjectName("stepList");
  m_stepList->setToolTip(
      tr("Steps of the flow. The current step is marked with ▶ and "
         "highlighted; click a step to jump to it."));
  m_stepList->setStatusTip(tr("Click a step to jump to it"));
  m_stepList->setWordWrap(true);
  m_stepList->setTextElideMode(Qt::ElideRight);
  m_stepList->setMinimumHeight(80);
  flowLayout->addWidget(m_stepList, 1);

  auto *controls = new QHBoxLayout();
  controls->setSpacing(2);
  m_resetButton = new QToolButton(flowBox);
  m_prevButton = new QToolButton(flowBox);
  m_playButton = new QToolButton(flowBox);
  m_nextButton = new QToolButton(flowBox);
  for (auto *b : {m_resetButton, m_prevButton, m_playButton, m_nextButton}) {
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setAutoRaise(false);
    controls->addWidget(b);
  }
  controls->addStretch(1);
  flowLayout->addLayout(controls);

  auto *speedRow = new QHBoxLayout();
  auto *speedLabel = new QLabel(tr("Speed:"), flowBox);
  m_speedCombo = new QComboBox(flowBox);
  m_speedCombo->setObjectName("speedCombo");
  m_speedCombo->addItem(tr("0.5× (3 s per step)"), 3000);
  m_speedCombo->addItem(tr("1× (1.5 s per step)"), 1500);
  m_speedCombo->addItem(tr("2× (0.75 s per step)"), 750);
  m_speedCombo->addItem(tr("4× (0.4 s per step)"), 400);
  m_speedCombo->setCurrentIndex(1);
  m_speedCombo->setToolTip(
      tr("Playback speed: how long each step stays on screen while playing"));
  m_speedCombo->setStatusTip(m_speedCombo->toolTip());
  speedLabel->setBuddy(m_speedCombo);
  speedLabel->setToolTip(m_speedCombo->toolTip());
  speedRow->addWidget(speedLabel);
  speedRow->addWidget(m_speedCombo, 1);
  flowLayout->addLayout(speedRow);

  // ===== center: diagram + current step
  m_centerSplitter = new QSplitter(Qt::Vertical, m_mainSplitter);
  m_centerSplitter->setChildrenCollapsible(false);
  m_view = new ArchView(m_centerSplitter);
  m_view->setMinimumSize(300, 200);
  m_view->setToolTip(QString());
  m_view->setStatusTip(
      tr("Diagram: click a block for details, scroll to zoom, drag to pan"));
  m_view->setPlaceholderText(
      tr("No topology loaded.\nUse “Open architecture JSON…” in the toolbar "
         "to load one."));

  auto *stepFrame = new QFrame(m_centerSplitter);
  stepFrame->setFrameShape(QFrame::StyledPanel);
  auto *stepLayout = new QVBoxLayout(stepFrame);
  stepLayout->setContentsMargins(8, 4, 8, 4);
  stepLayout->setSpacing(2);
  m_stepHeader = new QLabel(stepFrame);
  QFont stepHeaderFont = m_stepHeader->font();
  stepHeaderFont.setBold(true);
  m_stepHeader->setFont(stepHeaderFont);
  m_stepHeader->setWordWrap(true);
  stepLayout->addWidget(m_stepHeader);
  m_stepText = new QTextBrowser(stepFrame);
  m_stepText->setObjectName("stepText");
  m_stepText->setOpenExternalLinks(false);
  m_stepText->setFrameShape(QFrame::NoFrame);
  QFont stepFont = m_stepText->font();
  stepFont.setPointSizeF(stepFont.pointSizeF() + 1);
  m_stepText->setFont(stepFont);
  m_stepText->setToolTip(tr("Explanation of the current flow step"));
  m_stepText->setStatusTip(m_stepText->toolTip());
  m_stepText->setMinimumHeight(48);
  stepLayout->addWidget(m_stepText, 1);

  // ===== right: block details + legend
  m_rightSplitter = new QSplitter(Qt::Vertical, m_mainSplitter);
  m_rightSplitter->setChildrenCollapsible(false);

  auto *detailsBox = new QGroupBox(tr("Block details"), m_rightSplitter);
  auto *detailsLayout = new QVBoxLayout(detailsBox);
  detailsLayout->setContentsMargins(6, 4, 6, 6);
  detailsLayout->setSpacing(3);
  m_blockTitle = new QLabel(detailsBox);
  QFont blockTitleFont = m_blockTitle->font();
  blockTitleFont.setBold(true);
  blockTitleFont.setPointSizeF(blockTitleFont.pointSizeF() + 1);
  m_blockTitle->setFont(blockTitleFont);
  m_blockTitle->setWordWrap(true);
  m_blockTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_blockKind = new QLabel(detailsBox);
  m_blockKind->setWordWrap(true);
  m_blockKind->setTextFormat(Qt::RichText);
  m_blockKind->setToolTip(tr("Block kind (fill color in the diagram)"));
  m_blockSubtitle = new QLabel(detailsBox);
  m_blockSubtitle->setWordWrap(true);
  m_blockSubtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
  detailsLayout->addWidget(m_blockTitle);
  detailsLayout->addWidget(m_blockKind);
  detailsLayout->addWidget(m_blockSubtitle);

  m_propsLabel = sectionLabel(tr("Properties"), detailsBox);
  m_propsTable = new QTableWidget(detailsBox);
  m_propsTable->setObjectName("propsTable");
  m_propsTable->setColumnCount(2);
  m_propsTable->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
  m_propsTable->setToolTip(tr("Key facts about the selected block"));
  m_propsTable->setStatusTip(m_propsTable->toolTip());
  m_connLabel = sectionLabel(tr("Connections"), detailsBox);
  m_connTable = new QTableWidget(detailsBox);
  m_connTable->setObjectName("connTable");
  m_connTable->setColumnCount(3);
  m_connTable->setHorizontalHeaderLabels(
      {tr("Bus"), tr("Kind"), tr("Other end")});
  m_connTable->setToolTip(
      tr("Buses attached to the selected block: → outgoing, ← incoming, "
         "↔ bidirectional. Double-click a row to select the other block."));
  m_connTable->setStatusTip(
      tr("Double-click a connection to select the block at its other end"));
  for (auto *table : {m_propsTable, m_connTable}) {
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setWordWrap(true);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    table->setMinimumHeight(60);
  }
  m_connTable->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  m_detailsLabel = sectionLabel(tr("Description"), detailsBox);
  m_detailsBrowser = new QTextBrowser(detailsBox);
  m_detailsBrowser->setOpenExternalLinks(false);
  m_detailsBrowser->setToolTip(tr("Detailed explanation of the selected block"));
  m_detailsBrowser->setStatusTip(m_detailsBrowser->toolTip());
  m_detailsBrowser->setMinimumHeight(60);
  detailsLayout->addWidget(m_propsLabel);
  detailsLayout->addWidget(m_propsTable, 2);
  detailsLayout->addWidget(m_connLabel);
  detailsLayout->addWidget(m_connTable, 2);
  detailsLayout->addWidget(m_detailsLabel);
  detailsLayout->addWidget(m_detailsBrowser, 3);
  detailsLayout->addStretch(0);

  auto *legendBox = new QGroupBox(tr("Legend"), m_rightSplitter);
  auto *legendLayout = new QVBoxLayout(legendBox);
  legendLayout->setContentsMargins(6, 4, 6, 6);
  m_legendList = new QListWidget(legendBox);
  m_legendList->setObjectName("legendList");
  m_legendList->setSelectionMode(QAbstractItemView::NoSelection);
  m_legendList->setFocusPolicy(Qt::NoFocus);
  m_legendList->setIconSize(QSize(28, 16));
  m_legendList->setWordWrap(true);
  m_legendList->setToolTip(
      tr("What the colors and line styles in the diagram mean"));
  m_legendList->setStatusTip(m_legendList->toolTip());
  legendLayout->addWidget(m_legendList);

  // ----- bottom status line
  auto *statusRow = new QHBoxLayout();
  statusRow->setContentsMargins(4, 0, 4, 0);
  m_statusLabel = new QLabel(this);
  m_statusLabel->setToolTip(tr("Current topology, flow, step and playback state"));
  m_zoomLabel = new QLabel(this);
  m_zoomLabel->setToolTip(tr("Diagram zoom (100 % = one scene unit per pixel)"));
  statusRow->addWidget(m_statusLabel, 1);
  statusRow->addWidget(m_zoomLabel);
  root->addLayout(statusRow);

  // ----- default proportions (usable at 1280×800)
  m_mainSplitter->setStretchFactor(0, 0);
  m_mainSplitter->setStretchFactor(1, 10);
  m_mainSplitter->setStretchFactor(2, 0);
  m_mainSplitter->setSizes({260, 680, 270});
  m_leftSplitter->setSizes({150, 450});
  m_centerSplitter->setStretchFactor(0, 10);
  m_centerSplitter->setStretchFactor(1, 0);
  m_centerSplitter->setSizes({480, 130});
  m_rightSplitter->setSizes({430, 180});
}

// ============================================================================
// Setup
// ============================================================================

void ArchExplorerTab::setupToolbar() {
  const auto makeAction = [this](const QString &icon, const QString &text,
                                 const QString &tip) {
    auto *action = new QAction(QIcon(icon), text, this);
    action->setToolTip(tip);
    action->setStatusTip(tip);
    return action;
  };

  m_openAction =
      makeAction(":/icons/loadfile.svg", tr("Open architecture JSON..."),
                 tr("Load a topology description (*.json) from disk and add "
                    "it to the topology list"));
  connect(m_openAction, &QAction::triggered, this,
          &ArchExplorerTab::openArchitectureFile);
  m_toolbar->addAction(m_openAction);

  m_reloadAction =
      makeAction(":/icons/reset.svg", tr("Reload topology file"),
                 tr("Re-read the selected topology from its JSON file after "
                    "editing it (Ctrl+R)"));
  m_reloadAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
  connect(m_reloadAction, &QAction::triggered, this,
          [this] { reloadCurrent(); });
  m_toolbar->addAction(m_reloadAction);

  m_exportAction = makeAction(":/icons/saveas.svg", tr("Export PNG..."),
                              tr("Save the diagram (with the current "
                                 "highlighting) as a PNG image"));
  connect(m_exportAction, &QAction::triggered, this,
          &ArchExplorerTab::exportDiagramInteractive);
  m_toolbar->addAction(m_exportAction);
  m_toolbar->addSeparator();

  m_fitAction = makeAction(":/icons/expand.svg", tr("Fit to view"),
                           tr("Zoom so that the whole diagram fits (F)"));
  m_fitAction->setShortcut(QKeySequence(Qt::Key_F));
  connect(m_fitAction, &QAction::triggered, m_view, &ArchView::fitDiagram);
  m_toolbar->addAction(m_fitAction);

  m_resetZoomAction =
      makeAction(":/icons/crosshair.svg", tr("Reset zoom (100 %)"),
                 tr("Show the diagram at 100 % (Ctrl+0)"));
  m_resetZoomAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
  connect(m_resetZoomAction, &QAction::triggered, m_view,
          &ArchView::resetZoom);
  m_toolbar->addAction(m_resetZoomAction);

  m_zoomInAction = makeAction(":/icons/zoom-in.svg", tr("Zoom in"),
                              tr("Zoom in (Ctrl++ or mouse wheel)"));
  m_zoomInAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Plus),
                                QKeySequence(Qt::CTRL | Qt::Key_Equal)});
  connect(m_zoomInAction, &QAction::triggered, m_view, &ArchView::zoomIn);
  m_toolbar->addAction(m_zoomInAction);

  m_zoomOutAction = makeAction(":/icons/zoom-out.svg", tr("Zoom out"),
                               tr("Zoom out (Ctrl+- or mouse wheel)"));
  m_zoomOutAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
  connect(m_zoomOutAction, &QAction::triggered, m_view, &ArchView::zoomOut);
  m_toolbar->addAction(m_zoomOutAction);

  // Flow controls live in the data-flow panel.
  m_resetAction = makeAction(":/icons/reset.svg", tr("Reset"),
                             tr("Back to the flow overview, nothing "
                                "highlighted (Home)"));
  m_resetAction->setShortcut(QKeySequence(Qt::Key_Home));
  connect(m_resetAction, &QAction::triggered, this,
          &ArchExplorerTab::resetFlow);

  m_prevAction = makeAction(":/icons/reverse.svg", tr("Previous"),
                            tr("Previous step (Left arrow)"));
  m_prevAction->setShortcut(QKeySequence(Qt::Key_Left));
  connect(m_prevAction, &QAction::triggered, this,
          &ArchExplorerTab::previousStep);

  m_nextAction = makeAction(":/icons/step.svg", tr("Next"),
                            tr("Next step (Right arrow)"));
  m_nextAction->setShortcut(QKeySequence(Qt::Key_Right));
  connect(m_nextAction, &QAction::triggered, this, &ArchExplorerTab::nextStep);

  m_playAction = makeAction(":/icons/run.svg", tr("Play"),
                            tr("Play: advance automatically through the "
                               "steps (Space)"));
  m_playAction->setCheckable(true);
  m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
  connect(m_playAction, &QAction::toggled, this, &ArchExplorerTab::setPlaying);

  m_resetButton->setDefaultAction(m_resetAction);
  m_prevButton->setDefaultAction(m_prevAction);
  m_playButton->setDefaultAction(m_playAction);
  m_nextButton->setDefaultAction(m_nextAction);
}

void ArchExplorerTab::setupShortcuts() {
  // The shortcuts are active whenever focus is anywhere inside this tab; the
  // toolbar is outside of the tab's widget hierarchy, so the actions are also
  // added to the tab itself.
  for (auto *action : {m_resetAction, m_prevAction, m_nextAction, m_playAction,
                       m_fitAction, m_resetZoomAction, m_zoomInAction,
                       m_zoomOutAction, m_reloadAction}) {
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(action);
  }
  setFocusPolicy(Qt::StrongFocus);
}

void ArchExplorerTab::populateArchitectures() {
  QStringList errors;
  m_models = ArchModel::loadBuiltin(&errors);
  for (const auto &error : std::as_const(errors))
    qWarning() << "ArchExplorer: failed to load builtin architecture:" << error;
  std::stable_sort(m_models.begin(), m_models.end(),
                   [](const ArchModel &a, const ArchModel &b) {
                     return builtinRank(a.id) < builtinRank(b.id);
                   });
  m_modelPaths = QVector<QString>(m_models.size());

  // Files opened in earlier sessions (missing / broken files are dropped).
  QSettings settings;
  const QStringList opened = settings.value(kOpenedFilesKey).toStringList();
  for (const auto &path : opened) {
    auto model = ArchModel::fromFile(path);
    if (!model)
      continue;
    int existing = -1;
    for (int i = 0; i < m_models.size(); ++i) {
      if (m_models[i].id == model->id)
        existing = i;
    }
    if (existing >= 0) {
      m_models[existing] = *model;
      m_modelPaths[existing] = path;
    } else {
      m_models.push_back(*model);
      m_modelPaths.push_back(path);
    }
  }
  rememberOpenedFiles();

  int initial = m_models.isEmpty() ? -1 : 0;
  const QString last = settings.value(kLastTopologyKey).toString();
  {
    const QSignalBlocker blocker(m_archCombo);
    m_archCombo->clear();
    for (int i = 0; i < m_models.size(); ++i) {
      m_archCombo->addItem(comboText(i), i);
      m_archCombo->setItemData(i,
                               !m_modelPaths[i].isEmpty() ? m_modelPaths[i]
                               : m_models[i].subtitle.isEmpty()
                                   ? m_models[i].title
                                   : m_models[i].subtitle,
                               Qt::ToolTipRole);
      if (!last.isEmpty() && m_models[i].id == last)
        initial = i;
    }
  }
  if (initial >= 0)
    m_archCombo->setCurrentIndex(initial);
  setCurrentArchitecture(initial);
}

QString ArchExplorerTab::comboText(int modelIndex) const {
  const ArchModel &m = m_models[modelIndex];
  if (m_modelPaths[modelIndex].isEmpty())
    return m.title;
  return m.title + " (" + QFileInfo(m_modelPaths[modelIndex]).fileName() + ")";
}

void ArchExplorerTab::rememberOpenedFiles() const {
  QStringList paths;
  for (const auto &path : m_modelPaths) {
    if (!path.isEmpty())
      paths << path;
  }
  QSettings settings;
  settings.setValue(kOpenedFilesKey, paths);
}

bool ArchExplorerTab::reloadCurrent() {
  if (m_modelIndex < 0 || m_modelPaths[m_modelIndex].isEmpty())
    return false;
  return loadArchitectureFile(m_modelPaths[m_modelIndex]);
}

// ============================================================================
// Architecture / flow selection
// ============================================================================

const ArchModel *ArchExplorerTab::currentModel() const {
  if (m_modelIndex < 0 || m_modelIndex >= m_models.size())
    return nullptr;
  return &m_models[m_modelIndex];
}

const ArchFlow *ArchExplorerTab::currentFlow() const {
  const ArchModel *model = currentModel();
  if (model == nullptr || m_flowIndex < 0 || m_flowIndex >= model->flows.size())
    return nullptr;
  return &model->flows[m_flowIndex];
}

void ArchExplorerTab::setCurrentTopology(int index) {
  if (index >= 0 && index < m_archCombo->count())
    m_archCombo->setCurrentIndex(index);
}

void ArchExplorerTab::selectFlow(int index) {
  const int row = m_flowCombo->findData(index);
  if (row >= 0)
    m_flowCombo->setCurrentIndex(row);
}

void ArchExplorerTab::setCurrentArchitecture(int modelIndex) {
  setPlaying(false);
  m_modelIndex = (modelIndex >= 0 && modelIndex < m_models.size()) ? modelIndex
                                                                   : -1;
  m_flowIndex = -1;
  m_stepIndex = -1;

  const ArchModel *model = currentModel();
  if (model != nullptr) {
    QSettings settings;
    settings.setValue(kLastTopologyKey, model->id);
  }
  m_scene->setModel(model != nullptr ? *model : ArchModel());
  m_view->viewport()->update();

  QString html;
  if (model == nullptr) {
    html = "<p>" +
           tr("No topology loaded. Use <i>Open architecture JSON…</i> in the "
              "toolbar.") +
           "</p>";
  } else {
    if (!model->subtitle.isEmpty())
      html += "<p><i>" + model->subtitle.toHtmlEscaped() + "</i></p>";
    if (!model->description.isEmpty())
      html += "<p>" + plainToHtml(model->description) + "</p>";
    html += "<p><small>" +
            tr("%1 blocks, %2 buses, %3 data flows")
                .arg(model->blocks.size())
                .arg(model->buses.size())
                .arg(model->flows.size()) +
            "</small></p>";
  }
  m_archDescription->setHtml(html);

  updateHeader();
  updateLegend();
  clearBlockDetails();
  populateFlows(); // selects the first flow, which applies the step

  // Fit after the event loop has applied the widget geometry.
  QTimer::singleShot(0, this, [this] { m_view->fitDiagram(); });
}

void ArchExplorerTab::populateFlows() {
  const ArchModel *model = currentModel();
  {
    const QSignalBlocker blocker(m_flowCombo);
    m_flowCombo->clear();
    if (model != nullptr) {
      for (int i = 0; i < model->flows.size(); ++i) {
        const auto &flow = model->flows[i];
        m_flowCombo->addItem(
            tr("%1 (%2 steps)").arg(flow.title).arg(flow.steps.size()),
            i);
        m_flowCombo->setItemData(m_flowCombo->count() - 1,
                                 flow.description.isEmpty() ? flow.title
                                                            : flow.description,
                                 Qt::ToolTipRole);
      }
    }
    if (m_flowCombo->count() == 0) {
      m_flowCombo->addItem(model == nullptr
                               ? tr("(no topology loaded)")
                               : tr("(this topology has no data flows)"),
                           -1);
    }
    m_flowCombo->setEnabled(model != nullptr && !model->flows.isEmpty());
    m_flowCombo->setCurrentIndex(0);
  }
  setCurrentFlow(m_flowCombo->itemData(0).toInt());
}

void ArchExplorerTab::setCurrentFlow(int flowIndex) {
  setPlaying(false);
  const ArchModel *model = currentModel();
  if (model == nullptr || flowIndex < 0 || flowIndex >= model->flows.size())
    m_flowIndex = -1;
  else
    m_flowIndex = flowIndex;
  m_stepIndex = -1;

  const ArchFlow *flow = currentFlow();
  if (flow != nullptr) {
    m_flowDescription->setText(flow->description);
    m_flowDescription->setVisible(!flow->description.isEmpty());
  } else {
    m_flowDescription->setText(
        model == nullptr ? QString()
                         : tr("This topology has no data flows. Click the "
                              "blocks to explore it."));
    m_flowDescription->setVisible(model != nullptr);
  }
  populateSteps();
  applyStep();
}

void ArchExplorerTab::populateSteps() {
  const QSignalBlocker blocker(m_stepList);
  m_stepList->clear();
  const ArchFlow *flow = currentFlow();
  if (flow == nullptr) {
    auto *item = new QListWidgetItem(tr("No flow selected."), m_stepList);
    item->setFlags(Qt::NoItemFlags);
    return;
  }
  auto *overview = new QListWidgetItem(m_stepList);
  overview->setToolTip(
      tr("Overview: nothing highlighted. Press → or Space to start."));
  for (int i = 0; i < flow->steps.size(); ++i) {
    auto *item = new QListWidgetItem(m_stepList);
    item->setToolTip("<p>" + plainToHtml(flow->steps[i].text) + "</p>");
  }
}

// ============================================================================
// Stepping
// ============================================================================

void ArchExplorerTab::nextStep() {
  const ArchFlow *flow = currentFlow();
  if (flow == nullptr) {
    const ArchModel *model = currentModel();
    if (model != nullptr && !model->flows.isEmpty())
      selectFlow(0);
    return;
  }
  if (m_stepIndex + 1 < flow->steps.size()) {
    ++m_stepIndex;
    applyStep();
  }
}

void ArchExplorerTab::previousStep() {
  const ArchFlow *flow = currentFlow();
  if (flow == nullptr || m_stepIndex < 0)
    return;
  --m_stepIndex;
  applyStep();
}

void ArchExplorerTab::resetFlow() {
  setPlaying(false);
  m_stepIndex = -1;
  applyStep();
}

void ArchExplorerTab::setStep(int step) {
  const ArchFlow *flow = currentFlow();
  if (flow == nullptr)
    return;
  step = std::clamp(step, -1, static_cast<int>(flow->steps.size()) - 1);
  if (step == m_stepIndex)
    return;
  m_stepIndex = step;
  applyStep();
}

bool ArchExplorerTab::isPlaying() const {
  return m_playTimer != nullptr && m_playTimer->isActive();
}

void ArchExplorerTab::togglePlaying() { setPlaying(!isPlaying()); }

void ArchExplorerTab::updatePlayInterval() {
  const int ms = m_speedCombo->currentData().toInt();
  m_playTimer->setInterval(ms > 0 ? ms : DefaultPlayIntervalMs);
  updateStatus();
}

void ArchExplorerTab::setPlaying(bool playing) {
  if (playing) {
    const ArchFlow *flow = currentFlow();
    if (flow == nullptr) {
      playing = false;
    } else {
      // Restart from the beginning when play is pressed at the end.
      if (m_stepIndex + 1 >= flow->steps.size())
        m_stepIndex = -1;
      updatePlayInterval();
      m_playTimer->start();
      nextStep();
    }
  }
  if (!playing)
    m_playTimer->stop();

  if (m_playAction != nullptr) {
    if (m_playAction->isChecked() != playing) {
      const QSignalBlocker blocker(m_playAction);
      m_playAction->setChecked(playing);
    }
    m_playAction->setText(playing ? tr("Pause") : tr("Play"));
    m_playAction->setIcon(QIcon(playing ? ":/icons/stop-clock.svg"
                                        : ":/icons/run.svg"));
    const QString tip =
        playing ? tr("Pause automatic playback (Space)")
                : tr("Play: advance automatically through the steps (Space)");
    m_playAction->setToolTip(tip);
    m_playAction->setStatusTip(tip);
  }
  updateActions();
  updateStatus();
}

void ArchExplorerTab::applyStep() {
  const ArchModel *model = currentModel();
  const ArchFlow *flow = currentFlow();

  if (flow != nullptr && m_stepIndex >= 0 && m_stepIndex < flow->steps.size()) {
    const ArchFlowStep &step = flow->steps[m_stepIndex];
    m_scene->highlight(step.highlight, step.buses);
    m_stepHeader->setText(tr("Step %1 of %2 – %3")
                              .arg(m_stepIndex + 1)
                              .arg(flow->steps.size())
                              .arg(flow->title));
    QString html = "<p>" + plainToHtml(step.text) + "</p>";
    QStringList involved;
    for (const auto &id : step.highlight) {
      if (const ArchBlock *b = model->findBlock(id))
        involved << b->label.toHtmlEscaped();
    }
    QStringList buses;
    for (const auto &id : step.buses) {
      if (const ArchBus *b = model->findBus(id))
        buses << (b->label.isEmpty() ? b->id : b->label).toHtmlEscaped();
    }
    if (!involved.isEmpty() || !buses.isEmpty()) {
      html += "<p><small><i>";
      if (!involved.isEmpty())
        html += tr("Highlighted blocks: %1").arg(involved.join(", "));
      if (!involved.isEmpty() && !buses.isEmpty())
        html += "<br/>";
      if (!buses.isEmpty())
        html += tr("Highlighted buses: %1").arg(buses.join(", "));
      html += "</i></small></p>";
    }
    // Show the details of the first highlighted block and say so.
    if (!step.highlight.isEmpty()) {
      if (const ArchBlock *b = model->findBlock(step.highlight.first())) {
        html += "<p><small>" +
                tr("Details panel (right): %1").arg(b->label.toHtmlEscaped()) +
                "</small></p>";
      }
    }
    m_stepText->setHtml(html);
    if (!step.highlight.isEmpty())
      m_scene->selectBlock(step.highlight.first());
  } else {
    m_scene->clearHighlight();
    if (flow != nullptr) {
      m_stepHeader->setText(tr("Overview – %1").arg(flow->title));
      QString html;
      if (!flow->description.isEmpty())
        html += "<p>" + plainToHtml(flow->description) + "</p>";
      html += "<p><i>" +
              tr("%1 steps. Press → (Right arrow) or Next to go step by "
                 "step, or Space / Play to play the flow.")
                  .arg(flow->steps.size()) +
              "</i></p>";
      m_stepText->setHtml(html);
    } else if (model != nullptr) {
      m_stepHeader->setText(tr("No data flow"));
      m_stepText->setHtml(
          "<p><i>" +
          tr("This topology has no data flows. Click a block to see its "
             "details and connections.") +
          "</i></p>");
    } else {
      m_stepHeader->setText(tr("No topology loaded"));
      m_stepText->setHtml(
          "<p><i>" +
          tr("Use “Open architecture JSON…” in the toolbar to load a "
             "topology description.") +
          "</i></p>");
    }
  }
  updateStepList();
  updateActions();
  updateStatus();
}

void ArchExplorerTab::updateStepList() {
  const ArchFlow *flow = currentFlow();
  if (flow == nullptr || m_stepList->count() != flow->steps.size() + 1)
    return;
  const QSignalBlocker blocker(m_stepList);
  for (int row = 0; row < m_stepList->count(); ++row) {
    auto *item = m_stepList->item(row);
    const bool current = (row - 1) == m_stepIndex;
    const QString marker = current ? QStringLiteral("▶ ") : QString();
    QString text;
    if (row == 0)
      text = tr("Overview (nothing highlighted)");
    else
      text = tr("%1. %2").arg(row).arg(summarize(flow->steps[row - 1].text));
    item->setText(marker + text);
    QFont f = m_stepList->font();
    f.setBold(current);
    item->setFont(f);
  }
  m_stepList->setCurrentRow(m_stepIndex + 1);
  m_stepList->scrollToItem(m_stepList->item(m_stepIndex + 1));
}

void ArchExplorerTab::updateActions() {
  const ArchModel *model = currentModel();
  const ArchFlow *flow = currentFlow();
  const int steps = flow != nullptr ? flow->steps.size() : 0;

  m_prevAction->setEnabled(flow != nullptr && m_stepIndex >= 0);
  m_nextAction->setEnabled(flow != nullptr && m_stepIndex + 1 < steps);
  m_resetAction->setEnabled(flow != nullptr && m_stepIndex >= 0);
  m_playAction->setEnabled(flow != nullptr);
  m_speedCombo->setEnabled(flow != nullptr);
  m_stepList->setEnabled(flow != nullptr);
  for (auto *action :
       {m_fitAction, m_resetZoomAction, m_zoomInAction, m_zoomOutAction,
        m_exportAction}) {
    action->setEnabled(model != nullptr);
  }
  m_reloadAction->setEnabled(model != nullptr &&
                             !m_modelPaths[m_modelIndex].isEmpty());
}

void ArchExplorerTab::updateHeader() {
  const ArchModel *model = currentModel();
  if (model == nullptr) {
    m_titleLabel->setText(tr("Architecture Explorer"));
    m_subtitleLabel->setText(
        tr("Interactive block diagrams of computer architectures with "
           "step-by-step data flows."));
  } else {
    m_titleLabel->setText(tr("Architecture Explorer: %1").arg(model->title));
    m_subtitleLabel->setText(
        model->subtitle.isEmpty()
            ? tr("Interactive block diagram with step-by-step data flows.")
            : model->subtitle);
  }
}

QString ArchExplorerTab::statusText() const {
  const ArchModel *model = currentModel();
  const ArchFlow *flow = currentFlow();
  if (model == nullptr)
    return tr("No topology loaded");
  QString text;
  if (flow == nullptr) {
    text = tr("No data flow selected");
  } else if (m_stepIndex < 0) {
    text = tr("Overview (step 0 of %1) – press → or Space to start")
               .arg(flow->steps.size());
  } else {
    text = tr("Step %1 of %2").arg(m_stepIndex + 1).arg(flow->steps.size());
    if (m_stepIndex + 1 == flow->steps.size())
      text += " " + tr("(last step)");
  }
  if (flow != nullptr) {
    text += QStringLiteral("  ·  ") +
            (isPlaying() ? tr("▶ Playing at %1").arg(m_speedCombo->currentText())
                         : tr("Paused"));
  }
  return text;
}

void ArchExplorerTab::updateStatus() {
  if (m_statusLabel == nullptr)
    return;
  m_statusLabel->setText(statusText());
  m_zoomLabel->setText(
      tr("Zoom: %1 %").arg(qRound(m_zoom * 100.0)));
}

void ArchExplorerTab::updateLegend() {
  m_legendList->clear();
  const ArchModel *model = currentModel();
  if (model == nullptr) {
    auto *item = new QListWidgetItem(tr("No topology loaded."), m_legendList);
    item->setFlags(Qt::NoItemFlags);
    return;
  }
  const bool dark = archPaletteIsDark();
  const QPalette pal = palette();
  const auto addHeader = [this](const QString &text) {
    auto *item = new QListWidgetItem(text, m_legendList);
    QFont f = m_legendList->font();
    f.setBold(true);
    item->setFont(f);
    item->setFlags(Qt::ItemIsEnabled);
  };
  const auto addEntry = [this](const QPixmap &icon, const QString &name,
                               const QString &description) {
    auto *item = new QListWidgetItem(QIcon(icon), name + " – " + description,
                                     m_legendList);
    item->setToolTip(description);
    item->setFlags(Qt::ItemIsEnabled);
  };

  addHeader(tr("Block kinds (fill color)"));
  for (const auto kind : allArchBlockKinds()) {
    const bool used = std::any_of(
        model->blocks.begin(), model->blocks.end(),
        [kind](const ArchBlock &b) { return b.kind == kind; });
    if (used)
      addEntry(blockKindSwatch(kind, dark, pal),
               archBlockKindDisplayName(kind), archBlockKindDescription(kind));
  }
  if (!model->buses.isEmpty()) {
    addHeader(tr("Connection kinds (line style)"));
    for (const auto kind : allArchBusKinds()) {
      const bool used =
          std::any_of(model->buses.begin(), model->buses.end(),
                      [kind](const ArchBus &b) { return b.kind == kind; });
      if (used)
        addEntry(busKindSwatch(kind, dark), archBusKindDisplayName(kind),
                 archBusKindDescription(kind));
    }
  }
  if (!model->flows.isEmpty()) {
    addHeader(tr("Flow highlighting"));
    addEntry(highlightSwatch(pal, false, dark), tr("Highlighted"),
             tr("takes part in the current step (glow + thick outline)"));
    addEntry(highlightSwatch(pal, true, dark), tr("Faded"),
             tr("not involved in the current step"));
  }
}

// ============================================================================
// Block details
// ============================================================================

void ArchExplorerTab::showBlockDetails(const QString &id) {
  const ArchModel *model = currentModel();
  const ArchBlock *block = model != nullptr ? model->findBlock(id) : nullptr;
  if (block == nullptr) {
    clearBlockDetails();
    return;
  }
  m_detailsBlock = id;
  m_blockTitle->setText(block->label);
  const QColor fill = archBlockKindColor(block->kind, archPaletteIsDark());
  m_blockKind->setText(
      QString("<span style=\"background-color:%1;\">&nbsp;&nbsp;&nbsp;&nbsp;"
              "&nbsp;</span>&nbsp; <b>%2</b> – %3")
          .arg(fill.name(), archBlockKindDisplayName(block->kind).toHtmlEscaped(),
               archBlockKindDescription(block->kind).toHtmlEscaped()));
  m_blockKind->setVisible(true);
  m_blockSubtitle->setText(block->sublabel);
  m_blockSubtitle->setVisible(!block->sublabel.isEmpty());

  // Properties
  m_propsTable->setRowCount(block->props.size());
  for (int row = 0; row < block->props.size(); ++row) {
    const auto &prop = block->props[row];
    auto *keyItem = new QTableWidgetItem(prop.first);
    auto *valueItem = new QTableWidgetItem(prop.second);
    keyItem->setToolTip(prop.first);
    valueItem->setToolTip(prop.second);
    m_propsTable->setItem(row, 0, keyItem);
    m_propsTable->setItem(row, 1, valueItem);
  }
  m_propsTable->resizeRowsToContents();
  m_propsLabel->setText(tr("Properties (%1)").arg(block->props.size()));
  m_propsLabel->setVisible(!block->props.isEmpty());
  m_propsTable->setVisible(!block->props.isEmpty());

  // Connections
  QVector<const ArchBus *> attached;
  for (const auto &bus : model->buses) {
    if (bus.from == id || bus.to == id)
      attached.push_back(&bus);
  }
  m_connTable->setRowCount(attached.size());
  for (int row = 0; row < attached.size(); ++row) {
    const ArchBus *bus = attached[row];
    const bool outgoing = bus->from == id;
    const QString otherId = outgoing ? bus->to : bus->from;
    const ArchBlock *other = model->findBlock(otherId);
    const QString arrow = bus->bidirectional ? QStringLiteral("↔ ")
                          : outgoing         ? QStringLiteral("→ ")
                                             : QStringLiteral("← ");
    auto *nameItem =
        new QTableWidgetItem(bus->label.isEmpty() ? bus->id : bus->label);
    auto *kindItem = new QTableWidgetItem(
        QIcon(busKindSwatch(bus->kind, archPaletteIsDark())),
        archBusKindDisplayName(bus->kind));
    auto *otherItem = new QTableWidgetItem(
        arrow + (other != nullptr ? other->label : otherId));
    otherItem->setData(Qt::UserRole, otherId);
    QString tip = nameItem->text() + "\n" + archBusKindDescription(bus->kind);
    for (const auto &p : bus->props)
      tip += "\n" + p.first + ": " + p.second;
    for (auto *item : {nameItem, kindItem, otherItem}) {
      item->setToolTip(tip);
      m_connTable->setItem(row, item == nameItem   ? 0
                                : item == kindItem ? 1
                                                   : 2,
                           item);
    }
  }
  m_connTable->resizeRowsToContents();
  m_connLabel->setText(tr("Connections (%1)").arg(attached.size()));
  m_connLabel->setVisible(!attached.isEmpty());
  m_connTable->setVisible(!attached.isEmpty());

  m_detailsBrowser->setHtml(
      block->details.isEmpty()
          ? "<p><i>" + tr("No further description for this block.") +
                "</i></p>"
          : "<p>" + plainToHtml(block->details) + "</p>");
  m_detailsLabel->setVisible(true);
  m_detailsBrowser->setVisible(true);
}

void ArchExplorerTab::clearBlockDetails() {
  m_detailsBlock.clear();
  m_blockTitle->setText(tr("No block selected"));
  m_blockKind->clear();
  m_blockKind->setVisible(false);
  m_blockSubtitle->setText(
      tr("Click a block in the diagram to see its kind, properties, "
         "connections and description. Flow steps select the first "
         "highlighted block automatically."));
  m_blockSubtitle->setVisible(true);
  m_propsTable->setRowCount(0);
  m_connTable->setRowCount(0);
  m_detailsBrowser->clear();
  for (QWidget *w :
       std::initializer_list<QWidget *>{m_propsLabel, m_propsTable, m_connLabel,
                                        m_connTable, m_detailsLabel,
                                        m_detailsBrowser}) {
    w->setVisible(false);
  }
}

// ============================================================================
// Loading / export
// ============================================================================

void ArchExplorerTab::openArchitectureFile() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open architecture JSON"), QString(),
      tr("Architecture JSON (*.json);;All files (*)"));
  if (path.isEmpty())
    return;
  loadArchitectureFile(path);
}

bool ArchExplorerTab::loadArchitectureFile(const QString &path, bool quiet) {
  QString error;
  auto model = ArchModel::fromFile(path, &error);
  if (!model) {
    if (!quiet) {
      QMessageBox::warning(this, tr("Architecture Explorer"),
                           tr("Failed to load '%1':\n%2").arg(path, error));
    }
    return false;
  }
  const QString absPath = QFileInfo(path).absoluteFilePath();
  // Re-opening (or reloading) a file replaces the entry with the same path
  // or the same topology id instead of adding a duplicate.
  int index = -1;
  for (int i = 0; i < m_models.size(); ++i) {
    if (m_modelPaths[i] == absPath || m_models[i].id == model->id) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    m_models.push_back(*model);
    m_modelPaths.push_back(absPath);
    index = m_models.size() - 1;
    m_archCombo->addItem(comboText(index), index);
  } else {
    m_models[index] = *model;
    m_modelPaths[index] = absPath;
  }
  const int row = m_archCombo->findData(index);
  m_archCombo->setItemText(row, comboText(index));
  m_archCombo->setItemData(row, absPath, Qt::ToolTipRole);
  rememberOpenedFiles();
  const int previousStep = m_modelIndex == index ? m_stepIndex : -1;
  const int previousFlow = m_modelIndex == index ? m_flowIndex : -1;
  if (m_archCombo->currentIndex() == row) {
    // Reload of the shown topology: rebuild and try to keep the position.
    setCurrentArchitecture(index);
    if (previousFlow >= 0 && previousFlow < model->flows.size()) {
      selectFlow(previousFlow);
      setStep(previousStep);
    }
  } else {
    // Triggers setCurrentArchitecture(index) through currentIndexChanged.
    m_archCombo->setCurrentIndex(row);
  }
  return true;
}

bool ArchExplorerTab::exportDiagram(const QString &path) {
  m_scene->setLegendVisible(true);
  const QImage image = m_view->renderDiagram(2.0);
  m_scene->setLegendVisible(false);
  return !image.isNull() && image.save(path, "PNG");
}

void ArchExplorerTab::exportDiagramInteractive() {
  const ArchModel *model = currentModel();
  if (model == nullptr)
    return;
  QString suggestion = model->id;
  if (const ArchFlow *flow = currentFlow(); flow != nullptr && m_stepIndex >= 0)
    suggestion += "_" + flow->id + "_step" + QString::number(m_stepIndex + 1);
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export diagram as PNG"),
      QDir::home().filePath(suggestion + ".png"), tr("PNG image (*.png)"));
  if (path.isEmpty())
    return;
  if (!exportDiagram(path)) {
    QMessageBox::warning(this, tr("Architecture Explorer"),
                         tr("Could not write '%1'.").arg(path));
  }
}

// ============================================================================
// Layout persistence / theme
// ============================================================================

void ArchExplorerTab::saveSplitterState() const {
  QSettings settings;
  const QSplitter *splitters[] = {m_mainSplitter, m_leftSplitter,
                                  m_centerSplitter, m_rightSplitter};
  for (int i = 0; i < 4; ++i)
    settings.setValue(kSplitterKeys[i], splitters[i]->saveState());
}

void ArchExplorerTab::restoreSplitterState() {
  QSettings settings;
  QSplitter *splitters[] = {m_mainSplitter, m_leftSplitter, m_centerSplitter,
                            m_rightSplitter};
  for (int i = 0; i < 4; ++i) {
    const QByteArray state = settings.value(kSplitterKeys[i]).toByteArray();
    if (!state.isEmpty())
      splitters[i]->restoreState(state);
  }
}

void ArchExplorerTab::onThemeChanged() {
  updateLegend();
  if (!m_detailsBlock.isEmpty())
    showBlockDetails(m_detailsBlock);
  m_scene->update();
}

void ArchExplorerTab::tabVisibilityChanged(bool visible) {
  // The view has no valid size before the tab is first shown; fit once the
  // layout has settled (same approach as CacheTab).
  if (visible && !m_initialFitDone) {
    m_initialFitDone = true;
    QTimer::singleShot(50, this, [this] { m_view->fitDiagram(); });
  }
  if (!visible)
    setPlaying(false);
}

} // namespace Ripes
