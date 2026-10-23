# Ripes on WebAssembly

## Developer notes

## Building Ripes for WebAssembly

Some pointers:
- [WASM CI pipeline](/home/mpetersen/Work/Ripes/docs/webassembly.md)
- [Qt for WASM](https://doc.qt.io/qt-6/wasm.html)

### Debugging

To debug the webassembly build, you can use the following workflow:
1. Checkout https://github.com/mortbopet/mortbopet.github.io (Site where Ripes is hosted from)
2. Copy the `Ripes.*` files to the `mortbopet.github.io` folder. The `Ripes.*` files are generated in the build folder.
3. Use `emrun` to serve the site:
```bash
cd mortbopet.github.io
emrun index.html --no_browser
```
## Open SoC Explorer web version

`.github/workflows/web-pages.yml` builds the WebAssembly version
(`wasm-release.yml`, Qt 6.9.3 `wasm_multithread`), assembles the site with
`web/prepare_site.py`, tests it in headless Chromium and publishes it to the
`gh-pages` branch (only from `feature/web` and `main`; `feature/web-*`
branches are built and tested only). To serve it, enable GitHub Pages for
the repository with source *Deploy from a branch*, `gh-pages`, `/ (root)`;
the site is then at `https://<owner>.github.io/<repository>/`.

### Site layout

| File | Content |
|---|---|
| `index.html` | Router (`web/pages/index.html`), no simulator files |
| `desktop.html` | Qt's `Ripes.html` + `web/pages/desktop.head.html` / `desktop.body.html` |
| `mobile.html` | Qt's `Ripes.html` + `web/pages/mobile.head.html` / `mobile.body.html` |
| `Ripes.wasm`, `Ripes.js`, `qtloader.js` | Shared by both pages |
| `coi-serviceworker.min.js` | Cross-origin isolation (first script of both pages) |

The multithreaded build needs `SharedArrayBuffer`, i.e. a cross-origin
isolated page. GitHub Pages cannot send the COOP/COEP headers, so
`coi-serviceworker` adds them: on the first visit it registers and reloads
the page once (`location.reload()`, the query is kept). The desktop page
does not start Qt before the page is isolated.

`prepare_site.py` inserts, for each page, the service worker script and the
`<page>.head.html` fragment at the start of `<head>` (Qt's own viewport tag
is removed) and the optional `<page>.body.html` at the start of `<body>`;
Qt's loader script stays unchanged. Placeholders in the fragments:
`@OSX_WASM_BYTES@`, `@OSX_WASM_MB@` (size of `Ripes.wasm`) and `@OSX_UI@`.
A page without a head fragment gets a plain full-window viewport.

### Router

`index.html` decides before any of the simulator's files load:

1. `?ui=desktop` or `?ui=mobile` wins and is remembered (`localStorage`
   key `osx-ui`); `?ui=auto` forgets the remembered choice;
2. otherwise the remembered choice;
3. otherwise phones — `matchMedia('(pointer: coarse)').matches &&
   Math.min(screen.width, screen.height) < 768` — get the mobile version,
   everything else (computers, tablets, touch laptops) the desktop version.

It then replaces itself with `mobile.html?ui=mobile` or
`desktop.html?ui=desktop` (other query parameters and the hash are kept).
The pages never redirect back, so there is no loop. `index.html?choose`
shows both links ("电脑版 / Desktop", "手机版 / Mobile") without
redirecting; choosing one remembers it. The application reads the user
interface mode from the `ui` query parameter (default: desktop).

### Desktop version

* The Qt window fills the browser window and follows its size (Qt observes
  the size of its container, which is fixed to the window); the page never
  scrolls, also not when Qt focuses its hidden text input. Ctrl+wheel and
  trackpad pinch go to the application (zoom of the processor view) instead
  of zooming the page. Qt renders at the device pixel ratio.
* Loading screen in English and Chinese: download progress of `Ripes.wasm`
  (counted from the response stream against the file size written by
  `prepare_site.py`, since servers may compress), compiling, starting, and
  an error with a *Reload* button if the page cannot be isolated or Qt
  exits. It disappears when Qt has drawn its window.
* Files: *File → Load Program* (assembly source or ELF) and the Explorer's
  *Open a trace file* use `QFileDialog::getOpenFileContent` (the browser's
  file chooser); *Save File* and *Export trace* download the file
  (`QFileDialog::saveFileContent`). C compilation and the compiler settings
  stay unavailable in the browser.
* *File → Load Example → gem5 trace (O3CPU, loop_sum) in the Explorer*
  (Ctrl+Shift+G, also in the Explorer tool bar) replays the recorded gem5
  run in `tools/gem5/testdata/loop_sum_o3_l1l2`, converted with
  `gem5_to_osx.py` at build time (needs Python 3 on the build machine) and
  built in as the resource `:/osx-examples/gem5/loop_sum_o3_l1l2.jsonl`.
* Settings, the language and mission progress are `QSettings`, which Qt
  stores in the browser's `localStorage` on WebAssembly: they survive
  reloads (per browser and site). The editor's program is not kept.
* Every view fits a 1280×720 window (`tst_explorergui::tst_webLayout`).

### Tests

* `python3 -m unittest discover -s web`: `prepare_site.py` on a page shaped
  like Qt's loader page.
* `web/smoke_test.py <site> <screenshot.png>`: serves the site with and
  without the COOP/COEP headers and checks in headless Chromium the router
  decisions (desktop, phone portrait/landscape, tablet, `?ui=` overrides,
  remembered choice, `?choose`), then the desktop page at 1680×1000
  (isolated by the service worker; the query survives its reload) and at
  1280×720 with a device pixel ratio of 2: isolation, no uncaught errors,
  loading screen gone, canvas filling the window at device resolution,
  picture drawn, no page scrolling, resize to 1280×720 and back, the gem5
  example (Ctrl+Shift+G), mouse-wheel scrolling of the Explorer timeline,
  typing in the editor, and the language setting across a reload. Small
  JPEG thumbnails of the screenshots are printed to the log between
  `OSX-THUMBNAIL-BEGIN`/`END` markers.
* `web/smoke_mobile.py`: the phone version (run by the workflow if
  present).

Local test of a build directory:

```bash
python3 web/prepare_site.py <wasm build dir> site
pip install playwright pillow && python -m playwright install chromium
python3 web/smoke_test.py site smoke.png   # OSX_CHROMIUM=<path> for another Chromium
```
