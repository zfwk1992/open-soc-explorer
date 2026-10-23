// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#include "webfonts.h"

#ifdef OSX_EMBED_CJK_FONT
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

// Q_INIT_RESOURCE must be used outside of any namespace.
static void initWebFontResources() { Q_INIT_RESOURCE(webfonts); }
#endif

namespace Ripes {

QString installWebFonts() {
#ifdef OSX_EMBED_CJK_FONT
  static QString family;
  if (!family.isEmpty())
    return family;
  initWebFontResources();
  const int id = QFontDatabase::addApplicationFont(
      ":/webfonts/NotoSansSC/NotoSansSC-OSX-Subset.otf");
  const QStringList families = QFontDatabase::applicationFontFamilies(id);
  if (families.isEmpty())
    return QString();
  family = families.first();
  // Characters missing in a font are looked up in its substitutes.
  for (const char *base :
       {"DejaVu Sans", "DejaVu Sans Mono", "Inconsolata", "Sans Serif",
        "Helvetica", "Arial", "Monospace", "Courier", "Courier New"})
    QFont::insertSubstitution(base, family);
  return family;
#else
  return QString();
#endif
}

} // namespace Ripes
