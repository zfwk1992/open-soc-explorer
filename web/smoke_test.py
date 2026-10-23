#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""Browser smoke test of the web site produced by prepare_site.py.

    smoke_test.py <site dir> <screenshot.png>

Serves the site locally (with the cross-origin isolation headers, as the
service worker provides them on GitHub Pages), opens it in headless Chromium
(Playwright) at a desktop and at a phone-landscape size, and checks that

* the page is cross-origin isolated (SharedArrayBuffer available),
* no uncaught exception or WebAssembly abort is reported,
* Qt created its canvas and the application drew a non-uniform picture.

Requires: pip install playwright && python -m playwright install chromium
"""

import functools
import http.server
import socketserver
import sys
import threading

from playwright.sync_api import sync_playwright


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *args):
        pass


def picture_spread(png):
    """Standard deviation of the grey values of a PNG screenshot."""
    import io
    from PIL import Image, ImageStat
    image = Image.open(io.BytesIO(png)).convert("L")
    return ImageStat.Stat(image).stddev[0]


def log_thumbnail(png, label, width=480):
    """Prints a small JPEG of the screenshot as base64 between markers, so the
    picture can be recovered from the CI log (artifacts may be unreachable)."""
    import base64
    import io
    from PIL import Image
    image = Image.open(io.BytesIO(png)).convert("RGB")
    image.thumbnail((width, width * 4))
    out = io.BytesIO()
    image.save(out, "JPEG", quality=70)
    print(f"OSX-THUMBNAIL-BEGIN {label}")
    print(base64.b64encode(out.getvalue()).decode())
    print(f"OSX-THUMBNAIL-END {label}")


def run(site, shot):
    socketserver.TCPServer.allow_reuse_address = True
    server = socketserver.ThreadingTCPServer(
        ("127.0.0.1", 0), functools.partial(Handler, directory=site))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/index.html"

    failures = []
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for label, viewport, mobile in [("desktop", (1680, 1000), False),
                                        ("phone-landscape", (844, 390), True)]:
            context = browser.new_context(
                viewport={"width": viewport[0], "height": viewport[1]},
                is_mobile=mobile, has_touch=mobile)
            page = context.new_page()
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.on("console", lambda m: print(f"[{label}] {m.type}: {m.text}"))
            page.goto(url)
            isolated = page.evaluate("self.crossOriginIsolated")
            # Qt draws into canvases that may live inside shadow roots; look
            # for one recursively. Give the application time to start.
            find_canvas = """() => {
                const walk = (root) => {
                  for (const el of root.querySelectorAll('*')) {
                    if (el.tagName === 'CANVAS' && el.width > 100) return true;
                    if (el.shadowRoot && walk(el.shadowRoot)) return true;
                  }
                  return false;
                };
                return walk(document);
            }"""
            found = False
            for _ in range(36):  # up to 3 minutes
                if page.evaluate(find_canvas):
                    found = True
                    break
                page.wait_for_timeout(5000)
            page.wait_for_timeout(15000)
            print(f"[{label}] canvas found: {found}; body: "
                  f"{page.evaluate('document.body.innerText')[:300]!r}")
            if not found:
                failures.append(f"{label}: no Qt canvas after 3 minutes")
            png = page.screenshot(path=shot.replace(".png", f"-{label}.png"))
            spread = picture_spread(png)
            log_thumbnail(png, label)
            print(f"[{label}] crossOriginIsolated={isolated} "
                  f"picture stddev={spread:.1f} errors={len(errors)}")
            if not isolated:
                failures.append(f"{label}: page is not cross-origin isolated")
            for e in errors:
                failures.append(f"{label}: uncaught error: {e}")
            if spread < 8:
                failures.append(f"{label}: nothing drawn (stddev {spread:.1f})")
            context.close()
        browser.close()
    server.shutdown()
    for f in failures:
        print("FAIL:", f)
    print("web smoke test:", "FAILED" if failures else "PASSED")
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(run(sys.argv[1], sys.argv[2]))
