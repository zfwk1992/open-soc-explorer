#!/usr/bin/env python3
"""Regenerate resources/i18n/<module>_zh_CN.ts from the sources listed in
tools/i18n/modules.json by running Qt lupdate.

Usage:
  python tools/i18n/update_ts.py <module> [<module> ...]   # e.g. core tabs archexplorer
  python tools/i18n/update_ts.py --all
  python tools/i18n/update_ts.py --stats core               # untranslated count only

lupdate is looked up in (1) $LUPDATE, (2) PATH, (3) the user-space Qt
toolchain under P:/workspace/soc/toolchain. Existing translations in the .ts
file are preserved (lupdate merges); obsolete entries are dropped.
"""
import glob
import json
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MODULES = os.path.join(ROOT, "tools", "i18n", "modules.json")
TS_DIR = os.path.join(ROOT, "resources", "i18n")
LANG = "zh_CN"


def find_lupdate():
    cand = os.environ.get("LUPDATE") or shutil.which("lupdate")
    if cand:
        return cand
    for pat in ["P:/workspace/soc/toolchain/Qt/*/mingw_64/bin/lupdate.exe",
                "P:/workspace/soc/toolchain/Qt/*/*/bin/lupdate"]:
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[-1]
    sys.exit("lupdate not found; set LUPDATE=<path to lupdate>")


def sources_for(module, mapping):
    files = []
    for pat in mapping[module]:
        hits = sorted(glob.glob(os.path.join(ROOT, pat)))
        files.extend(h for h in hits if os.path.isfile(h))
    return files


def stats(ts_path):
    if not os.path.exists(ts_path):
        return (0, 0)
    tree = ET.parse(ts_path)
    total = unfinished = 0
    for msg in tree.iter("message"):
        total += 1
        tr = msg.find("translation")
        if tr is None or tr.get("type") in ("unfinished", "vanished", "obsolete"):
            unfinished += 1
            continue
        # Plural messages keep their text in <numerusform> children.
        forms = tr.findall("numerusform")
        texts = [f.text for f in forms] if forms else [tr.text]
        if not all((t or "").strip() for t in texts):
            unfinished += 1
    return (total, unfinished)


def main(argv):
    with open(MODULES, encoding="utf-8") as f:
        mapping = {k: v for k, v in json.load(f).items() if not k.startswith("_")}
    only_stats = "--stats" in argv
    argv = [a for a in argv if a != "--stats"]
    modules = list(mapping) if "--all" in argv else argv
    if not modules:
        sys.exit(__doc__)
    os.makedirs(TS_DIR, exist_ok=True)
    lupdate = None if only_stats else find_lupdate()
    for module in modules:
        if module not in mapping:
            sys.exit(f"unknown module '{module}'; known: {', '.join(mapping)}")
        ts = os.path.join(TS_DIR, f"{module}_{LANG}.ts")
        if not only_stats:
            files = sources_for(module, mapping)
            if not files:
                print(f"[{module}] no source files matched; skipping")
                continue
            cmd = [lupdate, "-no-obsolete", "-locations", "relative",
                   "-source-language", "en_US", "-target-language", LANG,
                   *files, "-ts", ts]
            subprocess.run(cmd, check=True, cwd=ROOT)
        total, unfinished = stats(ts)
        print(f"[{module}] {os.path.relpath(ts, ROOT)}: {total} strings, {unfinished} untranslated")


if __name__ == "__main__":
    main(sys.argv[1:])
