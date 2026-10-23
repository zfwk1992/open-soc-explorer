#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""Assemble the static web site of the WebAssembly build.

    prepare_site.py <wasm build dir> <output dir>

Copies the files produced by the Qt for WebAssembly build and writes

* index.html: a small router (web/pages/index.html) that sends phones to
  mobile.html?ui=mobile and everything else to desktop.html?ui=desktop
  before any of the simulator's files are loaded;
* desktop.html and mobile.html: Qt's generated loader page (Ripes.html) with
  - coi-serviceworker.min.js as the first script: the multithreaded build
    needs SharedArrayBuffer, i.e. a cross-origin isolated page, and static
    hosts such as GitHub Pages cannot send the COOP/COEP headers themselves
    (the service worker reloads the page once on the first visit; the URL,
    query included, is kept);
  - web/pages/<page>.head.html inserted at the start of <head> and the
    optional web/pages/<page>.body.html at the start of <body> (Qt's own
    viewport meta tag is removed: the fragments set the viewport).
  Both pages share Ripes.wasm, Ripes.js and qtloader.js. The application
  reads the user interface mode from the ui query parameter.

Placeholders replaced in the fragments: @OSX_WASM_BYTES@ (size of
Ripes.wasm in bytes, for the download progress), @OSX_WASM_MB@ (rounded
size in MB) and @OSX_UI@ (desktop or mobile).

Qt's own loader code in the pages is kept unchanged, so the pages follow the
qtloader.js API of the Qt version used for the build.
"""

import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PAGES_DIR = os.path.join(HERE, "pages")
BUILD_FILES = ["Ripes.wasm", "Ripes.js", "qtloader.js", "qtlogo.svg"]
OPTIONAL_FILES = ["Ripes.worker.js"]
UI_PAGES = ["desktop", "mobile"]
COI_SCRIPT = '<script src="coi-serviceworker.min.js"></script>'

# Used when a page has no head fragment (yet).
DEFAULT_HEAD = """
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#0b1220">
<style>
  html, body { margin: 0; width: 100%; height: 100%; overflow: hidden;
               background: #0b1220; }
  #screen { position: fixed !important; inset: 0; width: 100vw; height: 100vh; }
</style>
"""


class SiteError(Exception):
    pass


def read_fragment(name):
    path = os.path.join(PAGES_DIR, name)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return f.read()


def fill(text, values):
    for key, value in values.items():
        text = text.replace(f"@{key}@", value)
    return text


def build_page(template, head, body, values):
    """Returns Qt's loader page @p template with our additions."""
    page = template
    # The fragments set the viewport.
    page = re.sub(r"<meta\s+name=[\"']viewport[\"'][^>]*>\s*", "", page,
                  flags=re.IGNORECASE)
    # The service worker goes first, so that it is registered before
    # anything else runs.
    page, n = re.subn(r"<head>", lambda m: m.group(0) + "\n" + COI_SCRIPT +
                      "\n" + fill(head, values), page, count=1,
                      flags=re.IGNORECASE)
    if n != 1:
        raise SiteError("no <head> in Ripes.html")
    page = re.sub(r"<title>.*?</title>", "<title>Open SoC Explorer</title>",
                  page, count=1, flags=re.IGNORECASE | re.DOTALL)
    if body:
        page, n = re.subn(r"<body[^>]*>",
                          lambda m: m.group(0) + "\n" + fill(body, values),
                          page, count=1, flags=re.IGNORECASE)
        if n != 1:
            raise SiteError("no <body> in Ripes.html")
    first_script = re.search(r"<script\b[^>]*>", page, flags=re.IGNORECASE)
    if not first_script or "coi-serviceworker" not in first_script.group(0):
        raise SiteError("coi-serviceworker.min.js is not the first script")
    return page


def build_site(build, out):
    os.makedirs(out, exist_ok=True)
    for name in BUILD_FILES + OPTIONAL_FILES:
        src = os.path.join(build, name)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(out, name))
        elif name in BUILD_FILES:
            raise SiteError(f"{src} not found")
    for name in ["coi-serviceworker.min.js", "LICENSE.coi-serviceworker"]:
        shutil.copy2(os.path.join(HERE, name), os.path.join(out, name))
    shutil.copy2(os.path.join(PAGES_DIR, "index.html"),
                 os.path.join(out, "index.html"))

    with open(os.path.join(build, "Ripes.html"), encoding="utf-8") as f:
        template = f.read()
    wasm_bytes = os.path.getsize(os.path.join(build, "Ripes.wasm"))
    for ui in UI_PAGES:
        values = {"OSX_WASM_BYTES": str(wasm_bytes),
                  "OSX_WASM_MB": str(max(1, round(wasm_bytes / 1048576))),
                  "OSX_UI": ui}
        head = read_fragment(f"{ui}.head.html")
        if head is None:
            print(f"note: web/pages/{ui}.head.html not found, using a "
                  "default viewport")
            head = DEFAULT_HEAD
        page = build_page(template, head, read_fragment(f"{ui}.body.html"),
                          values)
        with open(os.path.join(out, f"{ui}.html"), "w",
                  encoding="utf-8") as f:
            f.write(page)
    # GitHub Pages: serve files as they are (no Jekyll processing).
    open(os.path.join(out, ".nojekyll"), "w").close()


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    try:
        build_site(argv[1], argv[2])
    except SiteError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"site written to {argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
