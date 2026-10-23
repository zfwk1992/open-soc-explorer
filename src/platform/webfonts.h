// SPDX-License-Identifier: MIT
// Part of Open SoC Explorer.
#pragma once

#include <QString>

namespace Ripes {

/// Registers the embedded Chinese font (Noto Sans SC subset, built with
/// OSX_EMBED_CJK_FONT, the default for WebAssembly, where Qt only has
/// DejaVu fonts) and makes it the fallback of the default fonts, so that
/// Chinese texts are not drawn as boxes. Returns the font's family, or an
/// empty string if no font is embedded in this build.
QString installWebFonts();

} // namespace Ripes
