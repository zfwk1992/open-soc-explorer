#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""Phone browser test of the web site produced by prepare_site.py.

    smoke_mobile.py <site dir> <screenshot.png>

Serves the site locally (with the cross-origin isolation headers, as the
service worker provides them on GitHub Pages) and opens mobile.html?ui=mobile
in headless Chromium (Playwright) emulating a phone, in portrait (390x844)
and landscape (844x390), device scale factor 3, with a touch screen. The
application publishes the geometry of its navigation tabs, of the main touch
targets and the scroll position of the visible page as
window.osxMobileLayout (src/mobile/mobileshell.cpp); the test uses it to
touch real widgets and checks that

* the page is cross-origin isolated and reports no uncaught error,
* the application fills exactly the screen and the page does not overflow,
* tapping navigation tabs and a mission card changes the page and the picture,
* swiping (CDP touch events) scrolls a long mission page and the register
  list, also visible in the picture,
* a tap on Clock advances the simulator (portrait).

Screenshots are printed into the log as base64 JPEG thumbnails between
OSX-THUMBNAIL-BEGIN/END markers (as web/smoke_test.py does).

Requires: pip install playwright pillow && python -m playwright install chromium
"""

import functools
import hashlib
import http.server
import io
import json
import socketserver
import sys
import threading

from playwright.sync_api import sync_playwright

CONFIGS = [("phone-portrait", (390, 844)), ("phone-landscape", (844, 390))]
DSF = 3

FIND_CANVAS = """() => {
    const walk = (root) => {
      for (const el of root.querySelectorAll('*')) {
        if (el.tagName === 'CANVAS' && el.width > 100) return true;
        if (el.shadowRoot && walk(el.shadowRoot)) return true;
      }
      return false;
    };
    return walk(document);
}"""


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *args):
        pass


def log_thumbnail(png, label, width=480):
    """Prints a small JPEG of the screenshot as base64 between markers, so the
    picture can be recovered from the CI log (artifacts may be unreachable)."""
    import base64
    from PIL import Image
    image = Image.open(io.BytesIO(png)).convert("RGB")
    image.thumbnail((width, width * 4))
    out = io.BytesIO()
    image.save(out, "JPEG", quality=70)
    print(f"OSX-THUMBNAIL-BEGIN {label}")
    print(base64.b64encode(out.getvalue()).decode())
    print(f"OSX-THUMBNAIL-END {label}")


def region_hash(png, rect=None):
    """Hash of the screenshot, or of @p rect (CSS pixels) of it."""
    from PIL import Image
    image = Image.open(io.BytesIO(png)).convert("RGB")
    if rect:
        x, y, w, h = rect
        image = image.crop((int(x * DSF), int(y * DSF), int((x + w) * DSF),
                            int((y + h) * DSF)))
    return hashlib.sha1(image.tobytes()).hexdigest()


def picture_spread(png):
    from PIL import Image, ImageStat
    image = Image.open(io.BytesIO(png)).convert("L")
    return ImageStat.Stat(image).stddev[0]


class Phone:
    """One emulated phone with the application loaded."""

    def __init__(self, context, page, label, failures):
        self.context = context
        self.page = page
        self.label = label
        self.failures = failures
        self.cdp = context.new_cdp_session(page)
        self.offset = (0, 0)
        self.step = 0

    def fail(self, message):
        print(f"[{self.label}] FAIL: {message}")
        self.failures.append(f"{self.label}: {message}")

    def layout(self):
        value = self.page.evaluate("window.osxMobileLayout || null")
        return json.loads(value) if value else None

    def wait_layout(self, predicate, timeout_ms=20000):
        waited = 0
        while waited <= timeout_ms:
            lay = self.layout()
            try:
                if lay and predicate(lay):
                    return lay
            except (KeyError, TypeError):
                pass
            self.page.wait_for_timeout(250)
            waited += 250
        return None

    def screenshot(self, name):
        self.step += 1
        png = self.page.screenshot()
        log_thumbnail(png, f"{self.label}-{self.step:02d}-{name}")
        return png

    def point(self, rect):
        return (self.offset[0] + rect["cx"], self.offset[1] + rect["cy"])

    def tap(self, rect):
        x, y = self.point(rect)
        self.page.touchscreen.tap(x, y)
        self.page.wait_for_timeout(700)

    def swipe(self, x, y0, y1, steps=14):
        """A one-finger drag with real touch events (touchStart, touchMove,
        touchEnd through the DevTools protocol)."""
        x += self.offset[0]
        y0 += self.offset[1]
        y1 += self.offset[1]
        self.cdp.send("Input.dispatchTouchEvent", {
            "type": "touchStart", "touchPoints": [{"x": x, "y": y0}]})
        for i in range(1, steps + 1):
            self.page.wait_for_timeout(16)
            y = y0 + (y1 - y0) * i / steps
            self.cdp.send("Input.dispatchTouchEvent", {
                "type": "touchMove", "touchPoints": [{"x": x, "y": y}]})
        self.page.wait_for_timeout(16)
        self.cdp.send("Input.dispatchTouchEvent", {
            "type": "touchEnd", "touchPoints": []})
        self.page.wait_for_timeout(1500)  # kinetic scrolling comes to rest

    def nav(self, name):
        lay = self.layout()
        tabs = {t["name"]: t for t in lay["nav"]}
        self.tap(tabs[name])
        lay = self.wait_layout(lambda l: l["page"] == name)
        if not lay:
            self.fail(f"tapping the '{name}' tab did not show that page")
        return lay

    def swipe_page(self, up=True, fraction=0.55):
        lay = self.layout()
        s = lay["scroll"]
        y_from = s["y"] + s["h"] * (0.5 + fraction / 2)
        y_to = s["y"] + s["h"] * (0.5 - fraction / 2)
        if not up:
            y_from, y_to = y_to, y_from
        self.swipe(s["x"] + s["w"] * 0.5, y_from, y_to)
        return self.layout()

    def target(self, name, tries=8):
        """The rectangle of touch target @p name, swiping the page upwards
        until it is on the screen."""
        for _ in range(tries):
            lay = self.layout()
            t = lay["targets"].get(name)
            if t and t["h"] >= 30:
                return t
            self.swipe_page(up=True, fraction=0.35)
        self.fail(f"touch target '{name}' not reachable by swiping")
        return None


def check_phone(phone, viewport):
    page = phone.page
    lay = phone.wait_layout(lambda l: l["width"] > 0, timeout_ms=60000)
    if not lay:
        phone.fail("the application did not publish its layout "
                   "(window.osxMobileLayout); is the phone UI running?")
        return
    page.wait_for_timeout(2000)
    lay = phone.layout()
    phone.offset = tuple(page.evaluate(
        "(() => { const s = document.getElementById('screen');"
        " const r = s.getBoundingClientRect(); return [r.left, r.top]; })()"))
    print(f"[{phone.label}] layout: page={lay['page']} "
          f"size={lay['width']}x{lay['height']} offset={phone.offset} "
          f"language={lay.get('language')}")

    # The application fills the screen; the page itself does not overflow.
    if abs(lay["width"] - viewport[0]) > 2 or abs(lay["height"] - viewport[1]) > 2:
        phone.fail(f"application is {lay['width']}x{lay['height']}, the "
                   f"screen {viewport[0]}x{viewport[1]}")
    dims = page.evaluate("[document.documentElement.scrollWidth,"
                         " document.documentElement.scrollHeight,"
                         " window.innerWidth, window.innerHeight]")
    print(f"[{phone.label}] document {dims[0]}x{dims[1]}, "
          f"window {dims[2]}x{dims[3]}")
    if dims[0] > dims[2] + 1 or dims[1] > dims[3] + 1:
        phone.fail(f"the page overflows the screen: {dims}")
    for tab in lay["nav"]:
        if tab["h"] < 44 or tab["w"] < 44:
            phone.fail(f"navigation tab {tab['name']} is only "
                       f"{tab['w']}x{tab['h']} px")

    start = phone.screenshot("start")
    if picture_spread(start) < 8:
        phone.fail("nothing drawn")

    # 1. A navigation tab changes the page and the picture.
    if phone.nav("registers"):
        regs = phone.screenshot("registers")
        if region_hash(regs) == region_hash(start):
            phone.fail("the picture did not change after tapping a tab")
        # 2. Swiping the register list scrolls it.
        before = phone.layout()["scroll"]
        rect = (before["x"] + phone.offset[0], before["y"] + phone.offset[1],
                before["w"], before["h"])
        after = phone.swipe_page(up=True)["scroll"]
        print(f"[{phone.label}] registers scrolled {before['value']} -> "
              f"{after['value']} (max {after['maximum']}, "
              f"kinetic={after['kinetic']})")
        swiped = phone.screenshot("registers-swiped")
        if after["maximum"] > 0 and after["value"] <= before["value"]:
            phone.fail("swiping did not scroll the register list")
        if after["maximum"] > 0 and region_hash(swiped, rect) == region_hash(regs, rect):
            phone.fail("the register list looks the same after swiping")

    # 3. Missions: a card opens the mission; swiping scrolls the long page.
    if phone.nav("missions"):
        lay = phone.layout()
        if lay["missionDetail"]:
            phone.tap(lay["targets"]["missions.back"])
            lay = phone.wait_layout(lambda l: not l["missionDetail"])
        card = phone.target("missions.card0")
        if card:
            phone.tap(card)
            lay = phone.wait_layout(lambda l: l["missionDetail"])
            if not lay:
                phone.fail("tapping a mission card did not open the mission")
            else:
                detail = phone.screenshot("mission-detail")
                s = lay["scroll"]
                rect = (s["x"] + phone.offset[0], s["y"] + phone.offset[1],
                        s["w"], s["h"])
                after = phone.swipe_page(up=True)["scroll"]
                print(f"[{phone.label}] mission page scrolled {s['value']} -> "
                      f"{after['value']} (max {after['maximum']})")
                scrolled = phone.screenshot("mission-swiped")
                if after["value"] <= s["value"]:
                    phone.fail("swiping did not scroll the mission page")
                if region_hash(scrolled, rect) == region_hash(detail, rect):
                    phone.fail("the mission page looks the same after swiping")
                back = phone.swipe_page(up=False)["scroll"]
                if back["value"] >= after["value"]:
                    phone.fail("swiping down did not scroll back")

    # 4. Load the mission's example, select its processor, clock once.
    if phone.label.endswith("portrait") and phone.layout()["missionDetail"]:
        for _ in range(3):
            if phone.layout()["scroll"]["value"] == 0:
                break
            phone.swipe_page(up=False)
        for name in ("missions.loadExample", "missions.selectProcessor"):
            t = phone.target(name)
            if t:
                phone.tap(t)
        if phone.nav("run"):
            cycle = phone.layout()["cycle"]
            clock = phone.target("run.clock")
            if clock:
                phone.tap(clock)
                lay = phone.wait_layout(lambda l: l["cycle"] > cycle,
                                        timeout_ms=15000)
                if not lay:
                    phone.fail("tapping Clock did not advance the simulator")
                else:
                    print(f"[{phone.label}] Clock: cycle {cycle} -> "
                          f"{lay['cycle']}")
            phone.screenshot("run-clocked")
        if phone.nav("pipeline"):
            phone.screenshot("pipeline")

    # 5. Settings: the language switch (Chinese text rendering).
    if phone.label.endswith("portrait") and phone.nav("settings"):
        t = phone.target("settings.chinese")
        if t:
            phone.tap(t)
            if phone.wait_layout(lambda l: l["language"] == "zh_CN"):
                phone.nav("missions")
                phone.screenshot("missions-zh")
                phone.nav("settings")
                t = phone.target("settings.english")
                if t:
                    phone.tap(t)
                    phone.wait_layout(lambda l: l["language"] == "en")
            else:
                phone.fail("tapping 简体中文 did not switch the language")


def run(site, shot):
    socketserver.TCPServer.allow_reuse_address = True
    server = socketserver.ThreadingTCPServer(
        ("127.0.0.1", 0), functools.partial(Handler, directory=site))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f"http://127.0.0.1:{server.server_address[1]}/mobile.html?ui=mobile"

    failures = []
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for label, viewport in CONFIGS:
            context = browser.new_context(
                viewport={"width": viewport[0], "height": viewport[1]},
                device_scale_factor=DSF, is_mobile=True, has_touch=True)
            page = context.new_page()
            errors = []
            page.on("pageerror", lambda e: errors.append(str(e)))
            page.on("console", lambda m: print(f"[{label}] {m.type}: {m.text}"))
            page.goto(url)
            isolated = page.evaluate("self.crossOriginIsolated")
            found = False
            for _ in range(36):  # up to 3 minutes
                if page.evaluate(FIND_CANVAS):
                    found = True
                    break
                page.wait_for_timeout(5000)
            print(f"[{label}] canvas found: {found}, crossOriginIsolated="
                  f"{isolated}, url={page.url}")
            phone = Phone(context, page, label, failures)
            if not isolated:
                phone.fail("page is not cross-origin isolated")
            if not found:
                phone.fail("no Qt canvas after 3 minutes")
                page.screenshot(path=shot.replace(".png", f"-{label}.png"))
            else:
                try:
                    check_phone(phone, viewport)
                except Exception as e:  # report and continue
                    phone.fail(f"test error: {e!r}")
                page.screenshot(path=shot.replace(".png", f"-{label}.png"))
            for e in errors:
                phone.fail(f"uncaught error: {e}")
            context.close()
        browser.close()
    server.shutdown()
    for f in failures:
        print("FAIL:", f)
    print("phone browser test:", "FAILED" if failures else "PASSED")
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(run(sys.argv[1], sys.argv[2]))
