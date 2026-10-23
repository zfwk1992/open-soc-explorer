#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""Tests of prepare_site.py on a loader page shaped like Qt's Ripes.html.

    python3 -m unittest discover -s web
"""

import os
import re
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prepare_site  # noqa: E402

# The structure of Qt 6.9's wasm_shell.html.
QT_PAGE = """<!doctype html>
<html lang="en-us">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, height=device-height, user-scalable=0"/>
    <title>Ripes</title>
    <style>
      html, body { padding: 0; margin: 0; overflow: hidden; height: 100% }
      #screen { width: 100%; height: 100%; }
    </style>
  </head>
  <body onload="init()">
    <figure style="overflow:visible;" id="qtspinner"><div id="qtstatus"></div></figure>
    <div id="screen"></div>
    <script type="text/javascript">
        async function init() {
            const instance = await qtLoad({ qt: {
                onLoaded: () => {}, entryFunction: window.Ripes_entry,
                containerElements: [document.querySelector('#screen')] } });
        }
    </script>
    <script src="Ripes.js"></script>
    <script type="text/javascript" src="qtloader.js"></script>
  </body>
</html>
"""


class PrepareSiteTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.build = os.path.join(self.tmp.name, "build")
        self.out = os.path.join(self.tmp.name, "site")
        os.makedirs(self.build)
        for name in prepare_site.BUILD_FILES:
            with open(os.path.join(self.build, name), "wb") as f:
                f.write(b"\0" * (3 * 1048576 if name == "Ripes.wasm" else 10))
        with open(os.path.join(self.build, "Ripes.html"), "w") as f:
            f.write(QT_PAGE)

    def tearDown(self):
        self.tmp.cleanup()

    def read(self, name):
        with open(os.path.join(self.out, name), encoding="utf-8") as f:
            return f.read()

    def test_site_layout(self):
        prepare_site.build_site(self.build, self.out)
        for name in prepare_site.BUILD_FILES + [
                "index.html", "desktop.html", "mobile.html",
                "coi-serviceworker.min.js", ".nojekyll"]:
            self.assertTrue(os.path.exists(os.path.join(self.out, name)), name)
        # The router does not load the simulator nor the service worker.
        index = self.read("index.html")
        self.assertNotIn("Ripes.js", index)
        self.assertNotIn("qtloader.js", index)
        self.assertIn("desktop.html?ui=desktop", index)
        self.assertIn("mobile.html?ui=mobile", index)
        self.assertIn("(pointer: coarse)", index)

    def test_ui_pages(self):
        prepare_site.build_site(self.build, self.out)
        for ui in prepare_site.UI_PAGES:
            page = self.read(f"{ui}.html")
            first = re.search(r"<script\b[^>]*>", page).group(0)
            self.assertIn("coi-serviceworker.min.js", first, ui)
            viewports = re.findall(r"<meta\s+name=\"viewport\"[^>]*>", page)
            self.assertEqual(len(viewports), 1, ui)
            self.assertIn("width=device-width", viewports[0])
            self.assertNotIn("height=device-height", page)
            self.assertIn("<title>Open SoC Explorer</title>", page)
            # Qt's loader is kept, and shared by both pages.
            self.assertIn('<script src="Ripes.js"></script>', page)
            self.assertIn("qtloader.js", page)
            self.assertIn("qtLoad(", page)
            self.assertNotIn("@OSX_", page)

    def test_desktop_page(self):
        prepare_site.build_site(self.build, self.out)
        page = self.read("desktop.html")
        self.assertIn('id="osx-loader"', page)
        # The loading screen comes first in the body.
        self.assertLess(page.index('id="osx-loader"'),
                        page.index('id="screen"'))
        self.assertIn('Number("3145728")', page)
        self.assertIn("about 3 MB", page)

    def test_page_without_fragments(self):
        page = prepare_site.build_page(QT_PAGE, prepare_site.DEFAULT_HEAD,
                                       None, {})
        self.assertIn("width=device-width, initial-scale=1", page)
        self.assertTrue(page.index("coi-serviceworker") <
                        page.index("<style>"))

    def test_fragment_must_not_include_the_service_worker(self):
        # It would be loaded twice ("Identifier 'coepCredentialless' has
        # already been declared" in the browser).
        head = '<script src="coi-serviceworker.min.js"></script>'
        with self.assertRaises(prepare_site.SiteError):
            prepare_site.build_page(QT_PAGE, head, None, {})
        # Mentioning it in a comment is fine.
        page = prepare_site.build_page(
            QT_PAGE, "<!-- after coi-serviceworker.min.js -->", None, {})
        self.assertIn("coi-serviceworker", page)

    def test_missing_build_file(self):
        os.remove(os.path.join(self.build, "Ripes.wasm"))
        with self.assertRaises(prepare_site.SiteError):
            prepare_site.build_site(self.build, self.out)


if __name__ == "__main__":
    unittest.main()
