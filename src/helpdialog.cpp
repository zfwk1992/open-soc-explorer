#include "helpdialog.h"

#include "help.h"
#include "i18n.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPointer>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTimer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace Ripes {

namespace {

constexpr const char kResourceRoot[] = ":/guide";

QString guideLangFor(const QString &uiLang) {
  return uiLang.startsWith("zh") ? QStringLiteral("zh") : QStringLiteral("en");
}

/// Child of the guide root, e.g. child(root, "zh/03-gui-tour.md").
QUrl child(const QUrl &root, const QString &rel) {
  if (root.scheme() == "qrc")
    return QUrl("qrc" + QString(kResourceRoot) + "/" + rel);
  return QUrl::fromLocalFile(root.toLocalFile() + "/" + rel);
}

/// Path usable with QFile/QDir for a qrc: or file: URL.
QString localPath(const QUrl &url) {
  if (url.scheme() == "qrc")
    return ":" + url.path();
  if (url.isLocalFile())
    return url.toLocalFile();
  return QString();
}

bool urlExists(const QUrl &url) {
  const QString path = localPath(url.adjusted(QUrl::RemoveFragment));
  return !path.isEmpty() && QFileInfo(path).isFile();
}

} // namespace

QUrl HelpDialog::guideRoot() {
  if (QFile::exists(QString(kResourceRoot) + "/README.md"))
    return QUrl("qrc" + QString(kResourceRoot));
  const QString dir = userGuideDir();
  if (!dir.isEmpty())
    return QUrl::fromLocalFile(dir);
  return QUrl();
}

QStringList HelpDialog::availableChapters(const QString &lang) {
  const QUrl root = guideRoot();
  if (root.isEmpty())
    return {};
  QDir dir(localPath(child(root, lang)));
  QStringList chapters;
  for (const auto &file : dir.entryList({"*.md"}, QDir::Files, QDir::Name))
    chapters << QFileInfo(file).completeBaseName();
  return chapters;
}

QString HelpDialog::chapterTitle(const QUrl &url, const QString &fallback) {
  QFile file(localPath(url));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return fallback;
  // Use the first level-1 heading of the chapter.
  for (int i = 0; i < 40 && !file.atEnd(); ++i) {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    if (line.startsWith("# "))
      return line.mid(2).trimmed();
  }
  return fallback;
}

HelpDialog *HelpDialog::open(const QString &chapter) {
  static QPointer<HelpDialog> s_dialog;
  if (!s_dialog) {
    // Parent the help window to the main window (as a separate top-level
    // window) so that it is destroyed together with it and never keeps the
    // application alive on its own.
    QWidget *parent = nullptr;
    for (auto *w : QApplication::topLevelWidgets()) {
      if (qobject_cast<QMainWindow *>(w)) {
        parent = w;
        break;
      }
    }
    s_dialog = new HelpDialog(parent);
    if (!parent)
      s_dialog->setAttribute(Qt::WA_DeleteOnClose);
  }
  s_dialog->showChapter(chapter);
  s_dialog->show();
  s_dialog->raise();
  s_dialog->activateWindow();
  return s_dialog;
}

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent, Qt::Window), m_lang(guideLangFor(uiLanguage())),
      m_root(guideRoot()) {
  setWindowTitle(tr("User Guide"));
  setAttribute(Qt::WA_QuitOnClose, false);
  setModal(false);
  resize(1100, 780);

  // ---- Tool row: navigation, language, search
  auto *backButton = new QToolButton(this);
  backButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
  backButton->setToolTip(tr("Back (Alt+Left)"));
  backButton->setShortcut(QKeySequence::Back);
  backButton->setEnabled(false);

  auto *forwardButton = new QToolButton(this);
  forwardButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
  forwardButton->setToolTip(tr("Forward (Alt+Right)"));
  forwardButton->setShortcut(QKeySequence::Forward);
  forwardButton->setEnabled(false);

  auto *homeButton = new QToolButton(this);
  homeButton->setIcon(style()->standardIcon(QStyle::SP_DirHomeIcon));
  homeButton->setToolTip(tr("Guide index (table of contents)"));

  m_langButton = new QToolButton(this);
  m_langButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

  m_search = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Find in page (Enter = next)"));
  m_search->setClearButtonEnabled(true);
  m_search->setToolTip(tr("Search the current page. Enter finds the next "
                          "match, Shift+Enter the previous one."));
  m_search->setMaximumWidth(280);

  m_status = new QLabel(this);
  m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

  auto *toolRow = new QHBoxLayout();
  toolRow->addWidget(backButton);
  toolRow->addWidget(forwardButton);
  toolRow->addWidget(homeButton);
  toolRow->addSpacing(8);
  toolRow->addWidget(m_langButton);
  toolRow->addSpacing(8);
  toolRow->addWidget(m_status, 1);
  toolRow->addWidget(m_search);

  // ---- Chapter list and page view
  m_chapters = new QListWidget(this);
  m_chapters->setToolTip(tr("Chapters of the user guide"));
  m_chapters->setMinimumWidth(180);

  m_browser = new QTextBrowser(this);
  m_browser->setOpenLinks(false); // handled in anchorClicked()
  m_browser->setOpenExternalLinks(false);
  m_browser->document()->setDocumentMargin(16);
  // Re-fit screenshots to the view width when the window is resized.
  m_fitTimer = new QTimer(this);
  m_fitTimer->setSingleShot(true);
  m_fitTimer->setInterval(60);
  connect(m_fitTimer, &QTimer::timeout, this, &HelpDialog::fitImages);
  m_browser->viewport()->installEventFilter(this);

  auto *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->addWidget(m_chapters);
  splitter->addWidget(m_browser);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({240, 860});

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(toolRow);
  layout->addWidget(splitter, 1);

  // ---- Connections
  connect(backButton, &QToolButton::clicked, m_browser,
          &QTextBrowser::backward);
  connect(forwardButton, &QToolButton::clicked, m_browser,
          &QTextBrowser::forward);
  connect(m_browser, &QTextBrowser::backwardAvailable, backButton,
          &QToolButton::setEnabled);
  connect(m_browser, &QTextBrowser::forwardAvailable, forwardButton,
          &QToolButton::setEnabled);
  connect(homeButton, &QToolButton::clicked, this,
          [this] { showChapter(QString()); });
  connect(m_langButton, &QToolButton::clicked, this,
          [this] { setLanguage(m_lang == "zh" ? "en" : "zh"); });
  connect(m_browser, &QTextBrowser::anchorClicked, this,
          &HelpDialog::anchorClicked);
  connect(m_browser, &QTextBrowser::sourceChanged, this,
          &HelpDialog::sourceChanged);
  connect(m_browser, &QTextBrowser::highlighted, this, [this](const QUrl &u) {
    m_status->setText(u.isEmpty() ? QString() : u.toString());
  });
  connect(m_chapters, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *current) {
            if (m_updatingList || !current)
              return;
            showChapter(current->data(Qt::UserRole).toString());
          });
  connect(m_search, &QLineEdit::returnPressed, this, [this] {
    findNext(QApplication::keyboardModifiers() & Qt::ShiftModifier);
  });
  connect(m_search, &QLineEdit::textChanged, this, [this] {
    // Restart searching from the top whenever the search text changes.
    QTextCursor c = m_browser->textCursor();
    c.movePosition(QTextCursor::Start);
    m_browser->setTextCursor(c);
    if (!m_search->text().isEmpty())
      findNext();
    else
      m_status->clear();
  });
  auto *findShortcut = new QShortcut(QKeySequence::Find, this);
  connect(findShortcut, &QShortcut::activated, this, [this] {
    m_search->setFocus();
    m_search->selectAll();
  });

  updateLanguageButton();
  populateChapterList();
}

void HelpDialog::updateLanguageButton() {
  // The button shows the language one can switch *to*.
  if (m_lang == "zh") {
    m_langButton->setText(QStringLiteral("English"));
    m_langButton->setToolTip(tr("Show this page in English"));
  } else {
    m_langButton->setText(QStringLiteral("中文"));
    m_langButton->setToolTip(tr("Show this page in Chinese (中文)"));
  }
}

void HelpDialog::setLanguage(const QString &lang) {
  const QString newLang = guideLangFor(lang);
  if (newLang == m_lang)
    return;
  const QString chapter = chapterOf(m_browser->source());
  m_lang = newLang;
  updateLanguageButton();
  populateChapterList();
  showChapter(chapter);
}

QUrl HelpDialog::chapterUrl(const QString &lang,
                            const QString &chapter) const {
  if (chapter.isEmpty())
    return child(m_root, "README.md");
  return child(m_root, lang + "/" + chapter + ".md");
}

QString HelpDialog::chapterOf(const QUrl &url) const {
  const QFileInfo info(url.path());
  const QString dirName = info.dir().dirName();
  if (dirName == "zh" || dirName == "en")
    return info.completeBaseName();
  return QString();
}

void HelpDialog::populateChapterList() {
  m_updatingList = true;
  m_chapters->clear();
  auto *index = new QListWidgetItem(tr("Index / 目录"));
  index->setData(Qt::UserRole, QString());
  m_chapters->addItem(index);
  for (const auto &chapter : availableChapters(m_lang)) {
    auto *item = new QListWidgetItem(
        chapterTitle(chapterUrl(m_lang, chapter), chapter));
    item->setData(Qt::UserRole, chapter);
    item->setToolTip(chapter);
    m_chapters->addItem(item);
  }
  m_updatingList = false;
}

void HelpDialog::showChapter(const QString &chapter) {
  if (m_root.isEmpty()) {
    m_browser->setHtml(
        tr("<h2>User guide not found</h2><p>The user guide was not built into "
           "this executable and could not be found next to it.</p><p>It is "
           "part of the source repository, in the folder "
           "<code>docs/guide</code> (<code>README.md</code> is the index).</p>"));
    return;
  }

  QUrl url = chapterUrl(m_lang, chapter);
  if (!urlExists(url)) {
    url = chapterUrl(m_lang, QString());
    m_status->setText(
        tr("Chapter \"%1\" is not available yet; showing the index.")
            .arg(chapter));
  }
  if (m_browser->source() == url) {
    m_browser->reload();
    m_fittedWidth = -1;
    scheduleFitImages();
  } else
    m_browser->setSource(url, QTextDocument::MarkdownResource);
}

void HelpDialog::sourceChanged(const QUrl &url) {
  const QString chapter = chapterOf(url);

  // If a link took us into the other language folder, follow it.
  const QString dirName = QFileInfo(url.path()).dir().dirName();
  if ((dirName == "zh" || dirName == "en") && dirName != m_lang) {
    m_lang = dirName;
    updateLanguageButton();
    populateChapterList();
  }

  m_updatingList = true;
  for (int i = 0; i < m_chapters->count(); ++i) {
    if (m_chapters->item(i)->data(Qt::UserRole).toString() == chapter) {
      m_chapters->setCurrentRow(i);
      break;
    }
  }
  m_updatingList = false;

  const QString title = chapterTitle(url, QString());
  setWindowTitle(title.isEmpty() ? tr("User Guide")
                                 : tr("User Guide - %1").arg(title));
  // A new document: fit its images now (no flicker) and again once the
  // layout has settled.
  m_fittedWidth = -1;
  fitImages();
  scheduleFitImages();
}

void HelpDialog::anchorClicked(const QUrl &link) {
  const QString scheme = link.scheme();
  if (scheme == "http" || scheme == "https" || scheme == "mailto") {
    QDesktopServices::openUrl(link);
    return;
  }

  const QUrl current = m_browser->source();
  const QUrl target = current.resolved(link);
  const QUrl targetPage = target.adjusted(QUrl::RemoveFragment);

  if (targetPage == current.adjusted(QUrl::RemoveFragment)) {
    if (target.hasFragment())
      m_browser->scrollToAnchor(target.fragment());
    return;
  }

  if (urlExists(targetPage)) {
    if (targetPage.path().endsWith(".md", Qt::CaseInsensitive)) {
      m_browser->setSource(targetPage, QTextDocument::MarkdownResource);
      if (target.hasFragment())
        m_browser->scrollToAnchor(target.fragment());
    } else if (targetPage.isLocalFile()) {
      QDesktopServices::openUrl(targetPage);
    } else {
      m_status->setText(tr("Cannot open %1 from the built-in guide.")
                            .arg(targetPage.fileName()));
    }
    return;
  }

  // Not part of the built-in guide (e.g. a link to a source file of the
  // repository). Try the guide folder on disk, otherwise just say so.
  const QString diskDir = userGuideDir();
  if (!diskDir.isEmpty() && current.scheme() == "qrc" && link.isRelative()) {
    // ":/guide/zh/x.md" -> "<diskDir>/zh/x.md"; resolve the link against it.
    const QString currentRel = current.path().mid(QString("/guide/").size());
    const QFileInfo currentOnDisk(QDir(diskDir).filePath(currentRel));
    const QFileInfo onDisk(currentOnDisk.dir(), link.path());
    if (onDisk.exists()) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(onDisk.absoluteFilePath()));
      return;
    }
  }
  m_status->setText(
      tr("Not available in the built-in guide: %1").arg(link.toString()));
}

bool HelpDialog::eventFilter(QObject *watched, QEvent *event) {
  if (m_browser && watched == m_browser->viewport() &&
      event->type() == QEvent::Resize)
    scheduleFitImages();
  return QDialog::eventFilter(watched, event);
}

void HelpDialog::scheduleFitImages() {
  if (m_fitTimer)
    m_fitTimer->start();
}

namespace {
/// Format property holding an image's original resource name once it has
/// been replaced by a scaled copy.
constexpr int kOriginalImageName = QTextFormat::UserProperty + 1;
} // namespace

void HelpDialog::fitImages() {
  // QTextDocument shows images at their natural size; scale large screenshots
  // down to the width of the view so that pages stay readable without
  // horizontal scrolling. The scaled copy is made with smooth filtering at the
  // screen's device pixel ratio and added to the document as a resource, so
  // it stays crisp (QTextDocument itself scales without filtering).
  QTextDocument *doc = m_browser->document();
  // Use the width with a vertical scroll bar, so that the scroll bar
  // appearing or disappearing does not cause another fit.
  const auto *vbar = m_browser->verticalScrollBar();
  const int viewportWidth =
      m_browser->viewport()->width() -
      (vbar->isVisible() ? 0 : vbar->sizeHint().width());
  const int maxWidth = std::max(
      120, viewportWidth - 2 * static_cast<int>(doc->documentMargin()) - 8);
  if (maxWidth == m_fittedWidth)
    return;
  m_fittedWidth = maxWidth;

  const QUrl base = m_browser->source();
  const qreal dpr = std::max<qreal>(1.0, m_browser->devicePixelRatioF());

  // Collect first: changing a format while iterating over fragments
  // invalidates the iteration.
  struct ImageRef {
    int position;
    int length;
    QTextImageFormat format;
  };
  std::vector<ImageRef> images;
  for (QTextBlock block = doc->begin(); block.isValid();
       block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const QTextFragment fragment = it.fragment();
      if (fragment.isValid() && fragment.charFormat().isImageFormat())
        images.push_back({fragment.position(), fragment.length(),
                          fragment.charFormat().toImageFormat()});
    }
  }
  if (images.empty())
    return;

  QTextCursor cursor(doc);
  cursor.beginEditBlock();
  for (auto &ref : images) {
    QTextImageFormat format = ref.format;
    QString original = format.stringProperty(kOriginalImageName);
    if (original.isEmpty()) {
      // Respect an explicit size given by the page itself.
      if (format.width() > 0)
        continue;
      original = format.name();
    }
    const QUrl url = base.resolved(QUrl(original));
    QImage image = doc->resource(QTextDocument::ImageResource, url)
                       .value<QImage>();
    if (image.isNull())
      image = QImageReader(localPath(url)).read();
    if (image.isNull())
      continue;

    if (image.width() <= maxWidth) {
      // Fits (again): show the original at its natural size.
      if (format.name() == original)
        continue;
      format.setName(original);
      format.clearProperty(QTextFormat::ImageWidth);
      format.clearProperty(QTextFormat::ImageHeight);
      format.clearProperty(kOriginalImageName);
    } else {
      const int height = qRound(static_cast<qreal>(image.height()) * maxWidth /
                                image.width());
      const QUrl scaledName(QStringLiteral("fitted-image:%1/%2")
                                .arg(maxWidth)
                                .arg(url.toString()));
      if (!doc->resource(QTextDocument::ImageResource, scaledName).isValid()) {
        QImage scaled = image.scaled(qRound(maxWidth * dpr),
                                     qRound(height * dpr), Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        doc->addResource(QTextDocument::ImageResource, scaledName, scaled);
      }
      format.setProperty(kOriginalImageName, original);
      format.setName(scaledName.toString());
      format.setWidth(maxWidth);
      format.setHeight(height);
    }
    cursor.setPosition(ref.position);
    cursor.setPosition(ref.position + ref.length, QTextCursor::KeepAnchor);
    cursor.setCharFormat(format);
  }
  cursor.endEditBlock();
}

void HelpDialog::findNext(bool backward) {
  const QString text = m_search->text();
  if (text.isEmpty())
    return;
  const QTextDocument::FindFlags flags =
      backward ? QTextDocument::FindBackward : QTextDocument::FindFlags();
  if (m_browser->find(text, flags)) {
    m_status->clear();
    return;
  }
  // Wrap around.
  QTextCursor c = m_browser->textCursor();
  c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
  m_browser->setTextCursor(c);
  if (m_browser->find(text, flags))
    m_status->setText(tr("Search wrapped around"));
  else
    m_status->setText(tr("\"%1\" not found on this page").arg(text));
}

} // namespace Ripes
