#include "help.h"
#include "helpdialog.h"

#include <QCoreApplication>
#include <QDir>

namespace Ripes {

QString userGuideDir() {
  // Candidates: next to the executable (installed/packaged layout), and the
  // source tree layout for developer builds (<repo>/build/Ripes.exe).
  const QString appDir = QCoreApplication::applicationDirPath();
  for (const char *rel : {"docs/guide", "../docs/guide", "../../docs/guide",
                             "../share/ripes/docs/guide"}) {
    QDir dir(appDir + "/" + rel);
    if (dir.exists("zh") && dir.exists("en"))
      return dir.absolutePath();
  }
  return QString();
}

void showHelp(const QString &chapter) {
  // The guide is embedded under ":/guide" (see the top-level CMakeLists.txt)
  // and shown in the shared, non-modal in-app help window.
  HelpDialog::open(chapter);
}

} // namespace Ripes
