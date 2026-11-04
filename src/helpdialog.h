#pragma once

#include <QDialog>
#include <QUrl>

QT_FORWARD_DECLARE_CLASS(QTextBrowser)
QT_FORWARD_DECLARE_CLASS(QListWidget)
QT_FORWARD_DECLARE_CLASS(QLineEdit)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QToolButton)
QT_FORWARD_DECLARE_CLASS(QTimer)

namespace Ripes {

/**
 * In-app user guide viewer.
 *
 * Displays the Markdown chapters of docs/guide (embedded under the resource
 * prefix ":/guide" by the top-level CMakeLists.txt; falls back to the
 * docs/guide folder on disk, see userGuideDir()). Layout of the guide:
 *   <root>/README.md            bilingual index
 *   <root>/zh/NN-name.md        Chinese chapters
 *   <root>/en/NN-name.md        English chapters
 *   <root>/images/<f>           screenshots referenced as ../images/x.png
 *
 * The dialog is non-modal and shared: use HelpDialog::open() (or
 * Ripes::showHelp()) rather than constructing it directly.
 */
class HelpDialog : public QDialog {
  Q_OBJECT

public:
  /// Shows the shared help window at @p chapter (file stem such as
  /// "03-gui-tour"; empty = index). Creates the window on first use.
  static HelpDialog *open(const QString &chapter = QString());

  /// Displays @p chapter in the currently selected guide language. Falls back
  /// to the index (README.md) when the chapter does not exist.
  void showChapter(const QString &chapter);

  /// Guide language currently displayed: "zh" or "en".
  QString language() const { return m_lang; }
  void setLanguage(const QString &lang);

  /// Root URL of the guide (qrc:/guide or a file:// URL), or an empty URL if
  /// no guide is available.
  static QUrl guideRoot();

  /// Chapter ids (file stems) available for @p lang, sorted.
  static QStringList availableChapters(const QString &lang);

protected:
  explicit HelpDialog(QWidget *parent = nullptr);
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void populateChapterList();
  void anchorClicked(const QUrl &url);
  void sourceChanged(const QUrl &url);
  /// Scales screenshots wider than the view down to the view's width
  /// (keeping the aspect ratio) and restores them when the view grows.
  /// Called after a page is loaded and, debounced, when the view is resized.
  void fitImages();
  void scheduleFitImages();
  void findNext(bool backward = false);
  void updateLanguageButton();
  QUrl chapterUrl(const QString &lang, const QString &chapter) const;
  QString chapterOf(const QUrl &url) const;
  static QString chapterTitle(const QUrl &url, const QString &fallback);

  QString m_lang;
  QUrl m_root;
  QTextBrowser *m_browser = nullptr;
  QListWidget *m_chapters = nullptr;
  QLineEdit *m_search = nullptr;
  QLabel *m_status = nullptr;
  QToolButton *m_langButton = nullptr;
  bool m_updatingList = false;
  QTimer *m_fitTimer = nullptr;
  /// Width the images of the current page were fitted to (-1: not fitted).
  int m_fittedWidth = -1;
};

} // namespace Ripes
