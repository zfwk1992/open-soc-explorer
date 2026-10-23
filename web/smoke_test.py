#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""Browser test of the web site produced by prepare_site.py (desktop part).

    smoke_test.py <site dir> <screenshot.png>

Serves the site locally twice: once with the cross-origin isolation headers
and once without them, like GitHub Pages, where coi-serviceworker provides
them (and reloads the page once). In headless Chromium (Playwright) it checks

* the router (index.html): desktop browsers go to desktop.html?ui=desktop,
  phones (touch, coarse pointer, small screen) to mobile.html?ui=mobile,
  tablets to the desktop version; ?ui= overrides and is remembered, ?ui=auto
  forgets it, ?choose shows the choice, other query parameters are kept;
* desktop.html at 1680x1000 (without headers: the service worker reload
  keeps the query) and at 1280x720 with a device pixel ratio of 2: the page
  is cross-origin isolated, no uncaught error is reported, the loading screen
  goes away, Qt's canvas fills the window at full resolution, the
  application drew a picture, the page does not scroll, and the application
  follows a window resize;
* the example gem5 trace opens with its shortcut (Ctrl+Shift+G), the mouse
  wheel scrolls the Explorer's instruction timeline, typing reaches the
  editor, and settings persist in the browser's storage across a reload;
* Load Program (Ctrl+O) reads a file chosen in the browser and Save File
  (Ctrl+S) hands it back, through the File System Access API (Chrome,
  pickers stubbed) and through a file input and a download (as in Firefox
  and Safari).

Small JPEG thumbnails of the screenshots are printed to the log
(log_thumbnail). The phone version is tested by smoke_mobile.py.

Requires: pip install playwright pillow && python -m playwright install chromium
(set OSX_CHROMIUM to use another Chromium executable).
"""

import functools
import http.server
import io
import os
import socketserver
import sys
import threading
from urllib.parse import parse_qs, urlparse

from playwright.sync_api import sync_playwright

# Positions in the application window (CSS pixels, from the top left), as
# laid out by the native build at the same window size (tst_explorergui
# prints them: tst_webLayout).
EDITOR_TAB = (38, 117)         # first entry of the tab bar on the left
EDITOR_TEXT = (200, 200)       # assembly editor
TIMELINE = (0.30, 0.60)        # Explorer timeline, fraction of the window
TIMELINE_BOX = (0.07, 0.45, 0.60, 0.72)


def make_handler(isolation_headers):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def end_headers(self):
            if isolation_headers:
                self.send_header("Cross-Origin-Opener-Policy", "same-origin")
                self.send_header("Cross-Origin-Embedder-Policy",
                                 "require-corp")
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def log_message(self, *args):
            pass
    return Handler


def serve(site, isolation_headers):
    socketserver.TCPServer.allow_reuse_address = True
    server = socketserver.ThreadingTCPServer(
        ("127.0.0.1", 0),
        functools.partial(make_handler(isolation_headers), directory=site))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def image(png):
    from PIL import Image
    return Image.open(io.BytesIO(png)).convert("L")


def picture_spread(png):
    """Standard deviation of the grey values of a PNG screenshot."""
    from PIL import ImageStat
    return ImageStat.Stat(image(png)).stddev[0]


def picture_change(before, after, box=None):
    """Mean absolute grey difference of two screenshots (optionally of the
    region box = (left, top, right, bottom) in pixels)."""
    from PIL import ImageChops, ImageStat
    a, b = image(before), image(after)
    if a.size != b.size:
        return 255.0
    if box:
        a, b = a.crop(box), b.crop(box)
    return ImageStat.Stat(ImageChops.difference(a, b)).mean[0]


def crop_png(png, box):
    """The region box = (left, top, right, bottom) of a screenshot, as PNG."""
    from PIL import Image
    out = io.BytesIO()
    Image.open(io.BytesIO(png)).crop(box).save(out, "PNG")
    return out.getvalue()


def log_thumbnail(png, label, width=480):
    """Prints a small JPEG of the screenshot as base64 between markers, so the
    picture can be recovered from the CI log (artifacts may be unreachable)."""
    import base64
    from PIL import Image
    picture = Image.open(io.BytesIO(png)).convert("RGB")
    picture.thumbnail((width, width * 4))
    out = io.BytesIO()
    picture.save(out, "JPEG", quality=70)
    print(f"OSX-THUMBNAIL-BEGIN {label}")
    print(base64.b64encode(out.getvalue()).decode())
    print(f"OSX-THUMBNAIL-END {label}")


# Largest canvas (Qt's window), also inside shadow roots, with its geometry.
FIND_CANVAS = """() => {
    let best = null;
    const walk = (root) => {
      for (const el of root.querySelectorAll('*')) {
        if (el.tagName === 'CANVAS' && el.width > 100 &&
            (!best || el.width * el.height > best.width * best.height))
          best = el;
        if (el.shadowRoot) walk(el.shadowRoot);
      }
    };
    walk(document);
    if (!best) return null;
    const r = best.getBoundingClientRect();
    return {width: best.width, height: best.height, x: r.x, y: r.y,
            cssWidth: r.width, cssHeight: r.height,
            dpr: window.devicePixelRatio};
}"""

PAGE_STATE = """() => ({
    isolated: self.crossOriginIsolated,
    search: location.search,
    loader: !!document.getElementById('osx-loader'),
    phase: window.osxLoader ? window.osxLoader.state.phase : null,
    scroll: [window.scrollX, window.scrollY,
             document.documentElement.scrollTop, document.body.scrollTop],
    viewport: [innerWidth, innerHeight],
})"""


PROGRAM = "# loaded in the browser\nli a0, 7\nli a1, 35\nadd a2, a0, a1\n"

# Chrome and Edge have the File System Access API, which Qt's file dialogs
# use (showOpenFilePicker / showSaveFilePicker); headless Chromium cannot
# show these pickers, so they are replaced by stubs that hand over a file
# and record what is written.
PICKER_STUBS = """
window.osxSaved = null;
window.showOpenFilePicker = async (options) => {
  window.osxPickerOptions = options;
  const file = new File([%s], 'browser.s', {type: 'text/plain'});
  return [{kind: 'file', name: file.name, getFile: async () => file}];
};
window.showSaveFilePicker = async (options) => {
  window.osxSaveOptions = options;
  const chunks = [];
  return {kind: 'file', name: (options && options.suggestedName) || 'file',
          createWritable: async () => ({
            write: async (data) => { chunks.push(data && data.data !== undefined
                                                 ? data.data : data); },
            close: async () => { window.osxSaved = await new Blob(chunks).text(); },
          })};
};
""" % repr(PROGRAM)
# Firefox and Safari have no such API: Qt uses a file input and a download.
NO_PICKERS = """
for (const name of ['showOpenFilePicker', 'showSaveFilePicker'])
  Object.defineProperty(window, name, {value: undefined, configurable: true});
"""


class Run:
    def __init__(self, shot):
        self.shot = shot
        self.failures = []

    def check(self, ok, message):
        print(("ok: " if ok else "FAIL: ") + message)
        if not ok:
            self.failures.append(message)

    def screenshot(self, page, label):
        png = page.screenshot(path=self.shot.replace(".png", f"-{label}.png"))
        log_thumbnail(png, label)
        return png


def route_target(url):
    parsed = urlparse(url)
    return os.path.basename(parsed.path), parse_qs(parsed.query)


def test_router(run, browser, base):
    desktop = dict(viewport={"width": 1680, "height": 1000})
    phone = dict(viewport={"width": 390, "height": 844},
                 screen={"width": 390, "height": 844},
                 device_scale_factor=3, is_mobile=True, has_touch=True)
    phone_landscape = dict(phone, viewport={"width": 844, "height": 390},
                           screen={"width": 844, "height": 390})
    tablet = dict(viewport={"width": 820, "height": 1180},
                  screen={"width": 820, "height": 1180},
                  device_scale_factor=2, is_mobile=True, has_touch=True)

    def new_context(options):
        context = browser.new_context(**options)
        # Only the routing is tested here: do not load the simulator.
        context.route("**/Ripes.*", lambda route: route.abort())
        context.route("**/qtloader.js", lambda route: route.abort())
        return context

    def expect(page, path, expected_page, expected_ui, label, extra=None):
        page.goto(base + path)
        try:
            page.wait_for_url(lambda u: route_target(u)[0] == expected_page,
                              timeout=10000)
        except Exception:
            pass
        name, query = route_target(page.url)
        ok = name == expected_page and query.get("ui") == [expected_ui]
        for key, value in (extra or {}).items():
            ok = ok and query.get(key) == [value]
        run.check(ok, f"router {label}: {path} -> {page.url[len(base):]}")

    for label, options, expected in [
            ("desktop", desktop, "desktop"),
            ("phone portrait", phone, "mobile"),
            ("phone landscape", phone_landscape, "mobile"),
            ("tablet", tablet, "desktop")]:
        context = new_context(options)
        page = context.new_page()
        expect(page, "index.html", f"{expected}.html", expected, label)
        context.close()

    # Overrides, remembered choice, other parameters.
    context = new_context(phone)
    page = context.new_page()
    expect(page, "index.html?ui=desktop", "desktop.html", "desktop",
           "phone ?ui=desktop")
    expect(page, "index.html", "desktop.html", "desktop",
           "phone, remembered desktop")
    expect(page, "index.html?ui=auto", "mobile.html", "mobile",
           "phone ?ui=auto")
    context.close()
    context = new_context(desktop)
    page = context.new_page()
    expect(page, "index.html?ui=mobile", "mobile.html", "mobile",
           "desktop ?ui=mobile")
    expect(page, "index.html", "mobile.html", "mobile",
           "desktop, remembered mobile")
    expect(page, "index.html?ui=auto&lesson=3", "desktop.html", "desktop",
           "desktop ?ui=auto, other parameters kept", {"lesson": "3"})
    # ?choose: no redirect, both versions offered.
    page.goto(base + "index.html?choose")
    page.wait_for_timeout(500)
    route = page.evaluate("window.osxRoute")
    links = page.evaluate(
        "[...document.querySelectorAll('a.choice')].map(a => a.textContent)")
    run.check(route_target(page.url)[0] == "index.html" and
              route["redirect"] is False and len(links) == 2,
              f"router ?choose stays and offers {links}")
    run.screenshot(page, "router-choose")
    context.close()


def wait_for_app(page, label, timeout_s=180):
    """Waits until Qt's canvas is shown (the page may reload once for the
    service worker); returns its geometry or None."""
    for _ in range(timeout_s // 2):
        try:
            canvas = page.evaluate(FIND_CANVAS)
            state = page.evaluate(PAGE_STATE)
            if canvas and not state["loader"]:
                return canvas
        except Exception:
            pass  # navigation (service worker reload) in progress
        page.wait_for_timeout(2000)
    print(f"[{label}] no application window after {timeout_s} s")
    return None


def check_window(run, page, label, viewport):
    canvas = page.evaluate(FIND_CANVAS)
    state = page.evaluate(PAGE_STATE)
    print(f"[{label}] canvas {canvas} state {state}")
    if not canvas:
        run.check(False, f"{label}: no canvas")
        return
    run.check(abs(canvas["cssWidth"] - viewport[0]) <= 2 and
              abs(canvas["cssHeight"] - viewport[1]) <= 2 and
              abs(canvas["x"]) <= 1 and abs(canvas["y"]) <= 1,
              f"{label}: window fills the page "
              f"({canvas['cssWidth']:.0f}x{canvas['cssHeight']:.0f} at "
              f"{canvas['x']:.0f},{canvas['y']:.0f})")
    run.check(abs(canvas["width"] - canvas["cssWidth"] * canvas["dpr"]) <= 2,
              f"{label}: canvas at device resolution ({canvas['width']} px "
              f"for {canvas['cssWidth']:.0f} CSS px, ratio {canvas['dpr']})")
    run.check(not any(state["scroll"]), f"{label}: page not scrolled "
              f"{state['scroll']}")


def file_round_trip(run, page, label, size, pickers):
    """Load Program (Ctrl+O) reads a file chosen in the browser, Save File
    (Ctrl+S) hands the program back (File System Access API or download)."""
    page.mouse.click(size[0] // 2, size[1] - 8)  # keyboard focus
    page.wait_for_timeout(500)
    before = page.screenshot()
    try:
        if pickers:
            page.keyboard.press("Control+O")
        else:
            with page.expect_file_chooser(timeout=10000) as chooser:
                page.keyboard.press("Control+O")
            chooser.value.set_files(files=[{"name": "browser.s",
                                            "mimeType": "text/plain",
                                            "buffer": PROGRAM.encode()}])
        page.wait_for_timeout(3000)
        loaded = run.screenshot(page, f"{label}-load-program")
        log_thumbnail(crop_png(loaded, (0, 0, 480, 240)),
                      f"{label}-load-program-detail")
        change = picture_change(before, loaded)
        run.check(change > 0.3, f"{label}: Load Program read the file chosen "
                  f"in the browser (change {change:.2f})")
    except Exception as e:
        run.check(False, f"{label}: Load Program: {e}")
    try:
        if pickers:
            page.keyboard.press("Control+S")
            page.wait_for_timeout(3000)
            saved = page.evaluate("window.osxSaved")
            name = page.evaluate("(window.osxSaveOptions || {}).suggestedName")
        else:
            with page.expect_download(timeout=10000) as download:
                page.keyboard.press("Control+S")
            with open(download.value.path(), encoding="utf-8") as f:
                saved = f.read()
            name = download.value.suggested_filename
        print(f"[{label}] saved {name}: {saved!r}")
        run.check(bool(saved) and "loaded in the browser" in saved,
                  f"{label}: Save File handed the program to the browser "
                  f"({name})")
    except Exception as e:
        run.check(False, f"{label}: Save File: {e}")


def test_desktop(run, browser, base, label, size, scale, interactions,
                 pickers):
    context = browser.new_context(
        viewport={"width": size[0], "height": size[1]},
        device_scale_factor=scale)
    context.add_init_script(PICKER_STUBS if pickers else NO_PICKERS)
    page = context.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.on("console", lambda m: print(f"[{label}] {m.type}: {m.text}"))
    page.goto(base + "desktop.html?ui=desktop&osxtest=1")
    page.wait_for_timeout(1500)
    try:
        run.screenshot(page, f"{label}-loading")
        print(f"[{label}] while loading: {page.evaluate(PAGE_STATE)}")
    except Exception as e:
        print(f"[{label}] loading screenshot skipped: {e}")
    canvas = wait_for_app(page, label)
    run.check(canvas is not None, f"{label}: application window shown")
    if canvas is None:
        run.screenshot(page, f"{label}-failed")
        context.close()
        return
    page.wait_for_timeout(8000)
    state = page.evaluate(PAGE_STATE)
    run.check(state["isolated"], f"{label}: cross-origin isolated")
    query = parse_qs(state["search"].lstrip("?"))
    run.check(query.get("ui") == ["desktop"] and
              query.get("osxtest") == ["1"],
              f"{label}: query kept ({state['search']})")
    check_window(run, page, label, size)
    png = run.screenshot(page, label)
    spread = picture_spread(png)
    run.check(spread >= 8, f"{label}: picture drawn (stddev {spread:.1f})")

    if interactions:
        desktop_interactions(run, page, label, size)
    file_round_trip(run, page, label, size, pickers)

    for e in errors:
        run.check(False, f"{label}: uncaught error: {e}")
    context.close()


def desktop_interactions(run, page, label, size):
    w, h = size
    # The example gem5 trace, with its keyboard shortcut. Click into the
    # status bar first so that the application has the keyboard focus.
    page.mouse.click(w // 2, h - 8)
    page.wait_for_timeout(500)
    before = page.screenshot()
    page.keyboard.press("Control+Shift+G")
    page.wait_for_timeout(5000)
    gem5 = run.screenshot(page, f"{label}-gem5-example")
    change = picture_change(before, gem5)
    run.check(change > 3, f"{label}: Ctrl+Shift+G opened the example gem5 "
              f"trace (picture change {change:.1f})")

    # Mouse wheel over the instruction timeline (317 rows).
    x, y = int(w * TIMELINE[0]), int(h * TIMELINE[1])
    page.mouse.move(x, y)
    page.wait_for_timeout(500)
    before = page.screenshot()
    for _ in range(4):
        page.mouse.wheel(0, 240)
        page.wait_for_timeout(250)
    page.wait_for_timeout(1500)
    after = run.screenshot(page, f"{label}-wheel")
    box = (int(w * TIMELINE_BOX[0]), int(h * TIMELINE_BOX[1]),
           int(w * TIMELINE_BOX[2]), int(h * TIMELINE_BOX[3]))
    change = picture_change(before, after, box)
    run.check(change > 2, f"{label}: mouse wheel scrolled the timeline "
              f"(change {change:.1f})")
    state = page.evaluate(PAGE_STATE)
    run.check(not any(state["scroll"]),
              f"{label}: page not scrolled by the wheel {state['scroll']}")

    # Typing in the editor.
    page.mouse.click(*EDITOR_TAB)
    page.wait_for_timeout(1500)
    page.mouse.click(*EDITOR_TEXT)
    page.wait_for_timeout(500)
    before = page.screenshot()
    page.keyboard.type("addi a0, a0, 42\n", delay=30)
    page.wait_for_timeout(1500)
    typed = run.screenshot(page, f"{label}-editor-typed")
    log_thumbnail(crop_png(typed, (0, 0, 480, 240)),
                  f"{label}-editor-typed-detail")
    change = picture_change(before, typed)
    run.check(change > 0.3, f"{label}: typing changed the editor "
              f"(change {change:.2f})")

    # The window follows a resize of the browser window.
    small = (1280, 720)
    page.set_viewport_size({"width": small[0], "height": small[1]})
    page.wait_for_timeout(3000)
    check_window(run, page, f"{label}-resized", small)
    png = run.screenshot(page, f"{label}-resized-{small[0]}x{small[1]}")
    run.check(picture_spread(png) >= 8, f"{label}: drawn after the resize")
    page.set_viewport_size({"width": w, "height": h})
    page.wait_for_timeout(2000)
    check_window(run, page, f"{label}-resized-back", size)

    # Settings are kept in the browser's storage (QSettings uses
    # localStorage on WebAssembly): switch the language there and reload.
    keys = page.evaluate("Object.keys(localStorage)")
    print(f"[{label}] localStorage keys: {sorted(keys)[:40]}")
    language = [k for k in keys if k.endswith("osx_language")]
    run.check(bool(language), f"{label}: settings stored in localStorage "
              f"({len(keys)} keys)")
    if language:
        page.evaluate(f"localStorage.setItem({language[0]!r}, 'zh_CN')")
        page.reload()
        canvas = wait_for_app(page, f"{label}-reload")
        run.check(canvas is not None, f"{label}: application shown again "
                  "after a reload")
        page.wait_for_timeout(8000)
        zh = run.screenshot(page, f"{label}-reloaded-zh")
        # Full resolution detail: the Chinese menu and tab texts.
        log_thumbnail(crop_png(zh, (0, 0, 480, 360)),
                      f"{label}-reloaded-zh-detail")
        stored = page.evaluate(f"localStorage.getItem({language[0]!r})")
        run.check(stored == "zh_CN" and picture_spread(zh) >= 8,
                  f"{label}: language setting kept after the reload "
                  f"({stored!r})")


def run_tests(site, shot):
    run = Run(shot)
    plain, plain_base = serve(site, isolation_headers=False)
    isolated, isolated_base = serve(site, isolation_headers=True)
    with sync_playwright() as p:
        browser = p.chromium.launch(
            executable_path=os.environ.get("OSX_CHROMIUM") or None)
        test_router(run, browser, plain_base)
        # As on GitHub Pages: isolation by the service worker.
        test_desktop(run, browser, plain_base, "desktop-1680x1000",
                     (1680, 1000), 1, interactions=True, pickers=True)
        # High-DPI screen, isolation by the server's headers.
        # Without the File System Access API (as in Firefox and Safari).
        test_desktop(run, browser, isolated_base, "desktop-1280x720-hidpi",
                     (1280, 720), 2, interactions=False, pickers=False)
        browser.close()
    plain.shutdown()
    isolated.shutdown()
    print("web smoke test:", "FAILED" if run.failures else "PASSED")
    for f in run.failures:
        print("FAIL:", f)
    return 1 if run.failures else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(run_tests(sys.argv[1], sys.argv[2]))
