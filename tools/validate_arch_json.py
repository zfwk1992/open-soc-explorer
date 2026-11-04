#!/usr/bin/env python3
"""Validator for Architecture Explorer JSON files (resources/arch/*.json).

Implements the same checks as src/archexplorer/archmodel.cpp
(ArchModel::fromJson) so that content authors can validate a description
without building Ripes.

User-visible text fields (title, subtitle, description, label, sublabel,
details, flow title/description, step text, property keys/values) may be a
plain string or a bilingual object {"zh": "...", "en": "..."}. The loader
resolves bilingual objects with Ripes::localized() (UI language, then "en",
then "zh"). A bilingual object that lacks one of the two languages produces a
warning; with --bilingual, plain-string text fields are reported as well.

Usage:
    python -I tools/validate_arch_json.py resources/arch/*.json
    python -I tools/validate_arch_json.py resources/arch        # a directory
    python -I tools/validate_arch_json.py                        # defaults to resources/arch
    python -I tools/validate_arch_json.py --bilingual resources/arch

Exit code 0 if every file is valid, 1 otherwise.
"""

import json
import re
import os
import sys

BLOCK_KINDS = (
    "cpu", "gpu", "accel", "dma", "memory", "cache", "io", "bridge", "audio",
    "video", "bus", "misc",
)
BUS_KINDS = ("bus", "dma", "irq", "mailbox", "link")

# Keys the loader knows about; anything else is reported as a warning only.
KNOWN_ROOT_KEYS = {"id", "title", "subtitle", "description", "blocks", "buses",
                   "flows"}
KNOWN_BLOCK_KEYS = {"id", "label", "sublabel", "details", "kind", "x", "y",
                    "w", "h", "props"}
KNOWN_BUS_KEYS = {"id", "from", "to", "label", "kind", "bidirectional",
                  "props"}
KNOWN_FLOW_KEYS = {"id", "title", "description", "steps"}
KNOWN_STEP_KEYS = {"highlight", "buses", "text"}


class ValidationError(Exception):
    def __init__(self, path, message):
        super().__init__(f"{path}: {message}")
        self.path = path
        self.message = message


def _is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _required_string(obj, key, path, allow_empty=False):
    if key not in obj:
        raise ValidationError(f"{path}.{key}", "missing required string field")
    value = obj[key]
    if not isinstance(value, str):
        raise ValidationError(f"{path}.{key}", "must be a string")
    if not allow_empty and value.strip() == "":
        raise ValidationError(f"{path}.{key}", "must not be empty")
    return value


# Set by main() from --bilingual: also warn about plain-string text fields.
REQUIRE_BILINGUAL = False
LANGUAGES = ("zh", "en")


_CJK = re.compile("[　-鿿＀-￯]")


def _text_value(value, path, warnings, is_prop=False):
    """Validates a localizable text value and returns its resolved text
    (English first, like the loader's fallback order for an English UI)."""
    if isinstance(value, str):
        # Property keys/values are mostly language-neutral technical data
        # ("32 bit", "12 MHz"); only flag them when they contain CJK text.
        if REQUIRE_BILINGUAL and value.strip() and (
                not is_prop or _CJK.search(value)):
            warnings.append(f"{path}: plain string (not translated)")
        return value
    if not isinstance(value, dict):
        raise ValidationError(
            path, 'must be a string or a {"zh": ..., "en": ...} object')
    if not any(lang in value for lang in LANGUAGES):
        raise ValidationError(
            path, 'bilingual text object must contain "zh" and/or "en"')
    for key, item in value.items():
        if not isinstance(item, str):
            raise ValidationError(f"{path}.{key}", "must be a string")
        if key not in LANGUAGES:
            warnings.append(f"{path}.{key}: unknown language key (ignored)")
    for lang in LANGUAGES:
        if lang not in value or not value[lang].strip():
            warnings.append(f"{path}: missing \"{lang}\" text")
    for lang in ("en", "zh"):
        if value.get(lang, "").strip():
            return value[lang]
    for item in value.values():
        return item
    return ""


def _required_text(obj, key, path, warnings):
    if key not in obj:
        raise ValidationError(f"{path}.{key}", "missing required text field")
    text = _text_value(obj[key], f"{path}.{key}", warnings)
    if text.strip() == "":
        raise ValidationError(f"{path}.{key}", "must not be empty")
    return text


def _optional_text(obj, key, path, warnings):
    if key not in obj or obj[key] is None:
        return ""
    return _text_value(obj[key], f"{path}.{key}", warnings)


def _required_number(obj, key, path):
    if key not in obj:
        raise ValidationError(f"{path}.{key}", "missing required number field")
    value = obj[key]
    if not _is_number(value):
        raise ValidationError(f"{path}.{key}", "must be a number")
    return value


def _optional_bool(obj, key, path, default=False):
    if key not in obj or obj[key] is None:
        return default
    value = obj[key]
    if not isinstance(value, bool):
        raise ValidationError(f"{path}.{key}", "must be a boolean")
    return value


def _prop_value(value, path, warnings):
    if isinstance(value, (str, dict)):
        return _text_value(value, path, warnings, is_prop=True)
    if isinstance(value, bool) or _is_number(value):
        return value
    raise ValidationError(
        path, "property values must be strings, bilingual text objects, "
        "numbers or booleans")


def _optional_props(obj, path, warnings):
    if "props" not in obj or obj["props"] is None:
        return
    props = obj["props"]
    if isinstance(props, dict):
        for key, value in props.items():
            _prop_value(value, f"{path}.props.{key}", warnings)
        return
    if isinstance(props, list):
        for i, entry in enumerate(props):
            ppath = f"{path}.props[{i}]"
            if not isinstance(entry, dict):
                raise ValidationError(
                    ppath, 'must be an object {"key": ..., "value": ...}')
            for key in entry:
                if key not in ("key", "value"):
                    warnings.append(f"{ppath}.{key}: unknown field (ignored by loader)")
            if "key" not in entry:
                raise ValidationError(f"{ppath}.key", "missing required text field")
            if _text_value(entry["key"], f"{ppath}.key", warnings,
                           is_prop=True).strip() == "":
                raise ValidationError(f"{ppath}.key", "must not be empty")
            if "value" not in entry:
                raise ValidationError(f"{ppath}.value", "missing required field")
            _prop_value(entry["value"], f"{ppath}.value", warnings)
        return
    raise ValidationError(
        f"{path}.props", "must be an object of key -> value or an array of "
        '{"key", "value"} objects')


def _optional_string_list(obj, key, path):
    if key not in obj or obj[key] is None:
        return []
    value = obj[key]
    if not isinstance(value, list):
        raise ValidationError(f"{path}.{key}", "must be an array of strings")
    for i, item in enumerate(value):
        if not isinstance(item, str):
            raise ValidationError(f"{path}.{key}[{i}]", "must be a string")
    return value


def _required_array(obj, key, path, allow_empty):
    if key not in obj:
        raise ValidationError(f"{path}.{key}", "missing required array field")
    value = obj[key]
    if not isinstance(value, list):
        raise ValidationError(f"{path}.{key}", "must be an array")
    if not allow_empty and len(value) == 0:
        raise ValidationError(f"{path}.{key}", "must not be empty")
    return value


def _optional_array(obj, key, path):
    if key not in obj or obj[key] is None:
        return []
    value = obj[key]
    if not isinstance(value, list):
        raise ValidationError(f"{path}.{key}", "must be an array")
    return value


def _warn_unknown_keys(obj, known, path, warnings):
    for key in obj:
        if key not in known:
            warnings.append(f"{path}.{key}: unknown field (ignored by loader)")


def validate_model(doc, warnings):
    """Validates a parsed JSON document. Raises ValidationError."""
    if not isinstance(doc, dict):
        raise ValidationError("$", "top-level JSON value must be an object")
    path = "$"
    _warn_unknown_keys(doc, KNOWN_ROOT_KEYS, path, warnings)
    _required_string(doc, "id", path)
    _required_text(doc, "title", path, warnings)
    _optional_text(doc, "subtitle", path, warnings)
    _optional_text(doc, "description", path, warnings)

    # ----- blocks
    block_ids = set()
    blocks = _required_array(doc, "blocks", path, allow_empty=False)
    for i, block in enumerate(blocks):
        bpath = f"blocks[{i}]"
        if not isinstance(block, dict):
            raise ValidationError(bpath, "must be an object")
        _warn_unknown_keys(block, KNOWN_BLOCK_KEYS, bpath, warnings)
        bid = _required_string(block, "id", bpath)
        _required_text(block, "label", bpath, warnings)
        _optional_text(block, "sublabel", bpath, warnings)
        _optional_text(block, "details", bpath, warnings)
        kind = _required_string(block, "kind", bpath)
        if kind not in BLOCK_KINDS:
            raise ValidationError(
                f"{bpath}.kind",
                f"unknown block kind '{kind}' (expected one of: "
                f"{', '.join(BLOCK_KINDS)})")
        _required_number(block, "x", bpath)
        _required_number(block, "y", bpath)
        w = _required_number(block, "w", bpath)
        h = _required_number(block, "h", bpath)
        if w <= 0:
            raise ValidationError(f"{bpath}.w", "must be > 0")
        if h <= 0:
            raise ValidationError(f"{bpath}.h", "must be > 0")
        _optional_props(block, bpath, warnings)
        if bid in block_ids:
            raise ValidationError(f"{bpath}.id", f"duplicate block id '{bid}'")
        block_ids.add(bid)

    # ----- buses
    bus_ids = set()
    buses = _optional_array(doc, "buses", path)
    for i, bus in enumerate(buses):
        bpath = f"buses[{i}]"
        if not isinstance(bus, dict):
            raise ValidationError(bpath, "must be an object")
        _warn_unknown_keys(bus, KNOWN_BUS_KEYS, bpath, warnings)
        bid = _required_string(bus, "id", bpath)
        src = _required_string(bus, "from", bpath)
        dst = _required_string(bus, "to", bpath)
        _optional_text(bus, "label", bpath, warnings)
        kind = _required_string(bus, "kind", bpath)
        if kind not in BUS_KINDS:
            raise ValidationError(
                f"{bpath}.kind",
                f"unknown bus kind '{kind}' (expected one of: "
                f"{', '.join(BUS_KINDS)})")
        _optional_bool(bus, "bidirectional", bpath)
        _optional_props(bus, bpath, warnings)
        if bid in bus_ids:
            raise ValidationError(f"{bpath}.id", f"duplicate bus id '{bid}'")
        if src not in block_ids:
            raise ValidationError(f"{bpath}.from",
                                  f"references unknown block id '{src}'")
        if dst not in block_ids:
            raise ValidationError(f"{bpath}.to",
                                  f"references unknown block id '{dst}'")
        bus_ids.add(bid)

    # ----- flows
    flow_ids = set()
    flows = _optional_array(doc, "flows", path)
    for i, flow in enumerate(flows):
        fpath = f"flows[{i}]"
        if not isinstance(flow, dict):
            raise ValidationError(fpath, "must be an object")
        _warn_unknown_keys(flow, KNOWN_FLOW_KEYS, fpath, warnings)
        fid = _required_string(flow, "id", fpath)
        _required_text(flow, "title", fpath, warnings)
        _optional_text(flow, "description", fpath, warnings)
        steps = _required_array(flow, "steps", fpath, allow_empty=False)
        if fid in flow_ids:
            raise ValidationError(f"{fpath}.id", f"duplicate flow id '{fid}'")
        flow_ids.add(fid)
        for s, step in enumerate(steps):
            spath = f"{fpath}.steps[{s}]"
            if not isinstance(step, dict):
                raise ValidationError(spath, "must be an object")
            _warn_unknown_keys(step, KNOWN_STEP_KEYS, spath, warnings)
            _required_text(step, "text", spath, warnings)
            for hid in _optional_string_list(step, "highlight", spath):
                if hid not in block_ids:
                    raise ValidationError(
                        f"{spath}.highlight",
                        f"references unknown block id '{hid}'")
            for hid in _optional_string_list(step, "buses", spath):
                if hid not in bus_ids:
                    raise ValidationError(
                        f"{spath}.buses",
                        f"references unknown bus id '{hid}'")

    return {
        "blocks": len(blocks),
        "buses": len(buses),
        "flows": len(flows),
        "steps": sum(len(f["steps"]) for f in flows),
    }


def validate_file(filename):
    """Returns (ok, message, warnings)."""
    warnings = []
    try:
        with open(filename, "rb") as f:
            raw = f.read()
        # The loader requires UTF-8 (a BOM is tolerated by QJsonDocument).
        text = raw.decode("utf-8-sig")
        doc = json.loads(text)
    except UnicodeDecodeError as e:
        return False, f"not valid UTF-8: {e}", warnings
    except json.JSONDecodeError as e:
        return False, f"JSON parse error at line {e.lineno} col {e.colno}: {e.msg}", warnings
    except OSError as e:
        return False, f"cannot open: {e}", warnings
    try:
        stats = validate_model(doc, warnings)
    except ValidationError as e:
        return False, str(e), warnings
    summary = (f"{stats['blocks']} blocks, {stats['buses']} buses, "
               f"{stats['flows']} flows, {stats['steps']} steps")
    return True, summary, warnings


def collect_files(args):
    if not args:
        here = os.path.dirname(os.path.abspath(__file__))
        args = [os.path.join(here, "..", "resources", "arch")]
    files = []
    for arg in args:
        if os.path.isdir(arg):
            for name in sorted(os.listdir(arg)):
                if name.lower().endswith(".json"):
                    files.append(os.path.join(arg, name))
        else:
            files.append(arg)
    return files


def main(argv):
    global REQUIRE_BILINGUAL
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass
    args = argv[1:]
    if "--bilingual" in args:
        REQUIRE_BILINGUAL = True
        args = [a for a in args if a != "--bilingual"]
    files = collect_files(args)
    if not files:
        print("no JSON files given")
        return 1
    failures = 0
    for filename in files:
        ok, message, warnings = validate_file(filename)
        status = "OK  " if ok else "FAIL"
        print(f"[{status}] {filename}: {message}")
        for w in warnings:
            print(f"       warning: {w}")
        if not ok:
            failures += 1
    print(f"{len(files) - failures}/{len(files)} files valid")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
