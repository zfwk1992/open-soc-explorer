#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""Assemble the static web site of the WebAssembly build.

    prepare_site.py <wasm build dir> <output dir>

Copies the files produced by the Qt for WebAssembly build and turns Qt's
generated loader page (Ripes.html) into index.html:

* coi-serviceworker.min.js is loaded first: the multithreaded build needs
  SharedArrayBuffer, i.e. a cross-origin isolated page, and static hosts such
  as GitHub Pages cannot send the COOP/COEP headers themselves;
* a viewport for phones and tablets (the desktop layout is shown at 1280 CSS
  pixels and can be zoomed; landscape works best);
* a title and a short loading note in English and Chinese.

It also writes mobile.html, the phone version: Qt's loader page with the head
fragment web/pages/mobile.head.html (coi-serviceworker first, a device-width
viewport without zoom, a page that does not scroll itself) and the optional
body fragment web/pages/mobile.body.html. The application shows its phone
user interface for the URL parameter ?ui=mobile (src/platform/uimode.cpp).

Qt's own loader code in the page is kept unchanged, so the page follows the
qtloader.js API of the Qt version used for the build.
"""

import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD_FILES = ["Ripes.wasm", "Ripes.js", "qtloader.js", "qtlogo.svg"]
OPTIONAL_FILES = ["Ripes.worker.js"]

HEAD = """
<meta name="viewport" content="width=1280, initial-scale=0.3, user-scalable=yes">
<meta name="theme-color" content="#0b1220">
<script src="coi-serviceworker.min.js"></script>
<style>
  html, body { background: #0b1220; color: #dbe4f3; }
  #osx-note { font: 15px/1.4 system-ui, sans-serif; text-align: center;
              padding: 6px; background: #111a2e; }
</style>
"""

NOTE = """
<div id="osx-note">Open SoC Explorer &middot; based on Ripes &middot;
loading the simulator, the first start can take a while; landscape works
best on phones &middot; 正在加载模拟器，首次打开较慢；手机横屏体验更佳</div>
"""


def mobile_page(page):
    """mobile.html from Qt's loader page (see the module documentation)."""
    with open(os.path.join(HERE, "pages", "mobile.head.html"),
              encoding="utf-8") as f:
        head = f.read()
    body_path = os.path.join(HERE, "pages", "mobile.body.html")
    body = ""
    if os.path.exists(body_path):
        with open(body_path, encoding="utf-8") as f:
            body = f.read()
    # Our viewport replaces Qt's; the charset is declared by the fragment.
    page = re.sub(r"<meta[^>]*name=[\"']viewport[\"'][^>]*>", "", page,
                  flags=re.IGNORECASE)
    page = re.sub(r"<meta[^>]*charset[^>]*>", "", page, flags=re.IGNORECASE)
    page, n = re.subn(r"<head>", lambda m: "<head>\n" + head, page, count=1,
                      flags=re.IGNORECASE)
    if n != 1:
        return None
    page = re.sub(r"<title>.*?</title>",
                  "<title>Open SoC Explorer &middot; phone</title>", page,
                  count=1, flags=re.IGNORECASE | re.DOTALL)
    page, n = re.subn(r"(<body[^>]*>)", lambda m: m.group(1) + body, page,
                      count=1, flags=re.IGNORECASE)
    if n != 1:
        return None
    return page


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    build, out = argv[1], argv[2]
    os.makedirs(out, exist_ok=True)
    for name in BUILD_FILES + OPTIONAL_FILES:
        src = os.path.join(build, name)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(out, name))
        elif name in BUILD_FILES:
            print(f"error: {src} not found", file=sys.stderr)
            return 1
    for name in ["coi-serviceworker.min.js", "LICENSE.coi-serviceworker"]:
        shutil.copy2(os.path.join(HERE, name), os.path.join(out, name))

    with open(os.path.join(build, "Ripes.html"), encoding="utf-8") as f:
        page = f.read()
    mobile = mobile_page(page)
    if mobile is None:
        print("error: no <head> or <body> in Ripes.html", file=sys.stderr)
        return 1
    with open(os.path.join(out, "mobile.html"), "w", encoding="utf-8") as f:
        f.write(mobile)
    # Our head additions go first so that the service worker is registered
    # before anything else runs.
    page, n = re.subn(r"<head>", "<head>" + HEAD, page, count=1,
                      flags=re.IGNORECASE)
    if n != 1:
        print("error: no <head> in Ripes.html", file=sys.stderr)
        return 1
    page = re.sub(r"<title>.*?</title>", "<title>Open SoC Explorer</title>",
                  page, count=1, flags=re.IGNORECASE | re.DOTALL)
    page, n = re.subn(r"(<body[^>]*>)", r"\1" + NOTE, page, count=1,
                      flags=re.IGNORECASE)
    if n != 1:
        print("error: no <body> in Ripes.html", file=sys.stderr)
        return 1
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)
    # GitHub Pages: serve files as they are (no Jekyll processing).
    open(os.path.join(out, ".nojekyll"), "w").close()
    print(f"site written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
