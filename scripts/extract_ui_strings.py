#!/usr/bin/env python3
"""Rewrite the English source catalogues from the Tr() sites in the sources.

Docs/UltraCanvas/UltraCanvasLocalisationProposal.md §8. Every call of
Tr("..."), TrC("ctx", "..."), TrN("one", "many", n) and TrF("...", ...)
with a literal is a key:

- the framework's (UltraCanvas/{include,core,libspecific,OS,Plugins}) go
  into media/locale/en/Standard.json, which is maintained as well as
  generated: a key the sources use is added, nothing is ever removed, so
  the curated vocabulary (the proposal's Appendix A) stays for the
  applications;
- an application's (Apps/<App>/) go into media/locale/en/<Name>.json,
  <Name> being the name it gives Initialize(); the file is rewritten from
  the sources, keeping its metadata ("@bases", "@source-language", ...),
  and a key the standard set or one of its base sets already has is listed
  under "@inherited" rather than repeated.

The base sets, media/locale/en/base/<Base>.json, are curated by hand and
never touched here.

    python3 scripts/extract_ui_strings.py            # rewrite the en catalogues
    python3 scripts/extract_ui_strings.py --check    # exit 1 when a rewrite would change one (CI)

A key is the literal as written, escapes decoded; a TrC key is
"context|text"; a TrN key is the singular, its entry the two forms; a
TrF key keeps its {0} placeholders. Adjacent literals ("a" "b") are one.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCALE = ROOT / "media" / "locale"
FRAMEWORK_DIRS = ["UltraCanvas/include", "UltraCanvas/core", "UltraCanvas/libspecific", "UltraCanvas/OS",
                  "UltraCanvas/Plugins"]
SKIP_APPS = {"DemoApp", "CSSLayoutTests"}
LIT = r'"((?:[^"\\\n]|\\.)*)"'
SITE = re.compile(r"(?<![A-Za-z0-9_:])(Tr|TrC|TrN|TrF)\s*\(")
SOURCE_SUFFIXES = (".cpp", ".h", ".mm", ".hpp")


def unescape(s: str) -> str:
    return bytes(s, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8") if "\\" in s else s


def literal_args(text: str, open_paren: int):
    """The leading literal arguments of the call opening at open_paren:
    a list of decoded strings, stopping at the first non-literal argument."""
    i = open_paren + 1
    args = []
    current = None
    while i < len(text):
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if c == '"':
            m = re.compile(LIT).match(text, i)
            if not m:
                break
            current = (current or "") + unescape(m.group(1))
            i = m.end()
            continue
        if c == ",":
            if current is None:
                break
            args.append(current)
            current = None
            i += 1
            continue
        if c == ")":
            if current is not None:
                args.append(current)
            break
        break   # anything else: a variable, an expression
    return args


def sites_in(path: Path):
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    found = []
    for m in SITE.finditer(text):
        name = m.group(1)
        args = literal_args(text, m.end() - 1)
        if name == "Tr" and len(args) >= 1:
            found.append(("entry", args[0], None))
        elif name == "TrF" and len(args) >= 1:
            found.append(("entry", args[0], None))
        elif name == "TrC" and len(args) >= 2:
            found.append(("entry", args[0] + "|" + args[1], None))
        elif name == "TrN" and len(args) >= 2:
            found.append(("plural", args[0], args[1]))
    return found


def walk(folder: Path):
    for path in sorted(folder.rglob("*")):
        if path.suffix in SOURCE_SUFFIXES and path.is_file():
            yield path


def collect(folders):
    entries, plurals = {}, {}
    for folder in folders:
        for path in walk(folder):
            for kind, key, plural in sites_in(path):
                if kind == "entry":
                    entries.setdefault(key, key)
                else:
                    plurals.setdefault(key, {"one": key, "other": plural})
    return entries, plurals


def read_catalogue(path: Path):
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as err:
        print("%s: not read: %s" % (path.relative_to(ROOT), err), file=sys.stderr)
        return {}


def serialize(meta: dict, entries: dict, plurals: dict) -> str:
    out = {}
    for key in ("@language", "@name", "@source-language", "@translated-by", "@bases", "@inherited"):
        if key in meta and meta[key] not in ("", [], None):
            out[key] = meta[key]
    for key in sorted(set(entries) | set(plurals), key=lambda k: (k.lower(), k)):
        out[key] = plurals[key] if key in plurals else entries[key]
    return json.dumps(out, ensure_ascii=False, indent=2) + "\n"


def app_name(folder: Path) -> str:
    for path in walk(folder):
        m = re.search(r'Initialize\("([^"]+)"\)', path.read_text(encoding="utf-8", errors="ignore"))
        if m:
            return m.group(1)
    return folder.name


def keys_of(catalogue: dict):
    return {k for k in catalogue if not k.startswith("@")}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--check", action="store_true", help="exit 1 when a catalogue would change; write nothing")
    args = parser.parse_args()
    changed = []

    # The standard set: add, never remove
    standard_path = LOCALE / "en" / "Standard.json"
    standard = read_catalogue(standard_path)
    meta = {k: v for k, v in standard.items() if k.startswith("@")}
    meta.setdefault("@language", "en")
    meta.setdefault("@name", "English")
    entries = {k: v for k, v in standard.items() if not k.startswith("@") and isinstance(v, str)}
    plurals = {k: v for k, v in standard.items() if not k.startswith("@") and isinstance(v, dict)}
    found_entries, found_plurals = collect([ROOT / d for d in FRAMEWORK_DIRS])
    for key, value in found_entries.items():
        if key not in entries and key not in plurals:
            entries[key] = value
    for key, forms in found_plurals.items():
        if key not in plurals:
            entries.pop(key, None)
            plurals[key] = forms
    text = serialize(meta, entries, plurals)
    if not standard_path.exists() or standard_path.read_text(encoding="utf-8") != text:
        changed.append(standard_path)
        if not args.check:
            standard_path.parent.mkdir(parents=True, exist_ok=True)
            standard_path.write_text(text, encoding="utf-8")
    standard_keys = set(entries) | set(plurals)

    # The base sets, for what an application inherits
    base_keys = {}
    for path in sorted((LOCALE / "en" / "base").glob("*.json")) if (LOCALE / "en" / "base").exists() else []:
        base_keys[path.stem] = keys_of(read_catalogue(path))

    # Each application: rewrite from the sources
    for folder in sorted((ROOT / "Apps").iterdir()):
        if not folder.is_dir() or folder.name in SKIP_APPS:
            continue
        found_entries, found_plurals = collect([folder])
        name = app_name(folder)
        path = LOCALE / "en" / (name + ".json")
        if not found_entries and not found_plurals and not path.exists():
            continue
        existing = read_catalogue(path)
        meta = {k: v for k, v in existing.items() if k.startswith("@")}
        meta.setdefault("@language", "en")
        inherited_from = set(standard_keys)
        for base in meta.get("@bases", []):
            inherited_from |= base_keys.get(base, set())
        entries = {k: v for k, v in found_entries.items() if k not in inherited_from}
        plurals = {k: v for k, v in found_plurals.items() if k not in inherited_from}
        inherited = sorted((set(found_entries) | set(found_plurals)) & inherited_from, key=lambda k: (k.lower(), k))
        meta["@inherited"] = inherited
        text = serialize(meta, entries, plurals)
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            changed.append(path)
            if not args.check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")

    for path in changed:
        print(("would rewrite " if args.check else "rewrote ") + str(path.relative_to(ROOT)))
    if args.check and changed:
        print("extract_ui_strings: %d catalogue(s) out of date - run python3 scripts/extract_ui_strings.py" % len(changed))
        return 1
    print("extract_ui_strings: %s" % ("in sync" if args.check else "%d catalogue(s) rewritten" % len(changed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
