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

`.github/workflows/web-pages.yml` builds the WebAssembly version, assembles
the site with `web/prepare_site.py` (Qt's loader page as `index.html`, a
phone viewport, and `coi-serviceworker` for cross-origin isolation), runs
`web/smoke_test.py` (headless Chromium at desktop and phone-landscape size:
the page must be cross-origin isolated, report no uncaught error and draw the
UI) and publishes the result to the `gh-pages` branch. To serve it, enable
GitHub Pages for the repository with source *Deploy from a branch*,
`gh-pages`, `/ (root)`; the site is then at
`https://<owner>.github.io/<repository>/`.

Local test of a build directory:

```bash
python3 web/prepare_site.py <wasm build dir> site
pip install playwright pillow && python -m playwright install chromium
python3 web/smoke_test.py site smoke.png
```
