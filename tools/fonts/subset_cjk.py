#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""Builds the Simplified Chinese font of the WebAssembly build.

    subset_cjk.py <NotoSansSC-Regular.otf> [output.otf]

Qt for WebAssembly only ships DejaVu fonts, and phones offer no access to
their local fonts, so Chinese text would be drawn as boxes. The web build
therefore embeds Noto Sans SC (SIL Open Font License 1.1, see OFL.txt next
to the output), reduced to the characters that the application can show:
every non-ASCII character of the translation catalogue, the mission files,
the sources and the web pages, the 3755 most common characters (GB 2312
level 1), CJK punctuation and full-width forms. Run it again after adding
Chinese texts; the font is loaded by src/platform/webfonts.cpp.

Source font: https://github.com/notofonts/noto-cjk (Sans/SubsetOTF/SC).
Requires: pip install fonttools
"""

import glob
import os
import sys

from fontTools import subset

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
DEFAULT_OUT = os.path.join(ROOT, "resources", "fonts", "NotoSansSC",
                           "NotoSansSC-OSX-Subset.otf")
SOURCES = [
    "translations/*.ts",
    "src/explorer/missions/*.json",
    "src/**/*.cpp",
    "src/**/*.h",
    "web/pages/*.html",
]


def characters():
    chars = set()
    for pattern in SOURCES:
        for path in glob.glob(os.path.join(ROOT, pattern), recursive=True):
            with open(path, encoding="utf-8", errors="ignore") as f:
                chars.update(c for c in f.read() if ord(c) > 0x2000)
    # CJK punctuation, full-width ASCII forms and the digits/letters they
    # are mixed with, so that edited texts keep a consistent look.
    chars.update(chr(c) for c in range(0x3000, 0x3040))
    chars.update(chr(c) for c in range(0xFF01, 0xFF5F))
    chars.update(chr(c) for c in range(0x20, 0x7F))
    # The 3755 most common characters (GB 2312 level 1), for texts that are
    # not in the sources (e.g. trace texts and comments in programs).
    for hi in range(0xB0, 0xD8):
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                pass
    return sorted(chars)


def main(argv):
    if len(argv) not in (2, 3):
        print(__doc__)
        return 2
    out = argv[2] if len(argv) == 3 else DEFAULT_OUT
    chars = characters()
    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.notdef_outline = True
    options.hinting = False
    options.desubroutinize = False
    font = subset.load_font(argv[1], options)
    subsetter = subset.Subsetter(options)
    subsetter.populate(text="".join(chars))
    subsetter.subset(font)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    subset.save_font(font, out, options)
    print(f"{len(chars)} characters -> {out} "
          f"({os.path.getsize(out) // 1024} KiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
