# UI translations / 界面翻译

One Qt `.ts` file per module, language `zh_CN` (source strings are English):

| File | Covers (see `tools/i18n/modules.json`) |
|---|---|
| `core_zh_CN.ts` | main window, menus, settings, editor, dialogs |
| `tabs_zh_CN.ts` | memory / cache / I/O / pipeline / register widgets, built-in peripherals |
| `archexplorer_zh_CN.ts` | Architecture Explorer tab |
| `iogpu_zh_CN.ts` | SIMT GPU peripheral widget |
| `ionpu_zh_CN.ts` | Tensor Unit (NPU) peripheral widget |

Workflow:

1. Wrap user-visible strings in `tr("...")` (inside `Q_OBJECT` classes) or
   `QObject::tr("...")` / `QCoreApplication::translate("Context", "...")`.
   Strings in `.ui` files are translatable by default.
2. `python tools/i18n/update_ts.py <module>` regenerates the `.ts` (merging
   existing translations).
3. Fill in the `<translation>` elements and remove `type="unfinished"`.
4. CMake runs `lrelease` and embeds the `.qm` under `:/i18n/`; `src/i18n.cpp`
   installs every `*_zh_CN.qm` at startup when the language is Chinese.

Language is chosen in Settings → Environment → Language (`system` / English /
简体中文) and applied on restart.
