#!/usr/bin/env python3
"""Rewrite the English source catalogues from the Tr() sites in the sources.

Docs/UltraCanvas/UltraCanvasLocalisationProposal.md §8. Every call of
Tr("..."), TrC("ctx", "..."), TrN("one", "many", n) and TrF("...", ...)
with a literal is a key:

- the framework's (UltraCanvas/{include,core,libspecific,OS,Plugins}), and
  those of the shared dialogs a module offers every application
  (UltraCloud/ui), go into media/locale/en/Standard.json, which is
  maintained as well as generated: a key the sources use is added, nothing
  is ever removed, so the curated vocabulary (the proposal's Appendix A)
  stays for the applications;
- an application's (Apps/<App>/) go into media/locale/en/<Name>.json,
  <Name> being the name it gives Initialize(); the file is rewritten from
  the sources, keeping its metadata ("@bases", "@source-language", ...),
  and a key the standard set or one of its base sets already has is listed
  under "@inherited" rather than repeated.

The base sets, media/locale/en/base/<Base>.json, are curated by hand and
never touched here.

    python3 scripts/extract_ui_strings.py            # rewrite the en catalogues
    python3 scripts/extract_ui_strings.py --check    # exit 1 when a rewrite would change one (CI)

A catalogue two branches both changed is never merged by hand: merge, and
run this. A catalogue the merge left conflicted (<<<<<<< markers, not JSON)
is read from the two sides git keeps for it (the index's stages 2 and 3) -
its hand-set metadata ("@bases", ...) from them, and for Standard.json,
which never loses a key, the keys of both - then written whole from the
sources; `git add` it after. Metadata the two sides set differently is not
guessed: that is reported, and the catalogue left as it is.

A key is the literal as written, escapes decoded; a TrC key is
"context|text" with the text alone as its English value; a TrN key is the
singular, its entry the two forms; a TrF key keeps its {0} placeholders. Adjacent literals ("a" "b") are one.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCALE = ROOT / "media" / "locale"
FRAMEWORK_DIRS = ["UltraCanvas/include", "UltraCanvas/core", "UltraCanvas/libspecific", "UltraCanvas/OS",
                  "UltraCanvas/Plugins",
                  # UltraCloud's add-account and picker dialogs: one dialog for
                  # every application that links UltraCloudUI, so its texts are
                  # every application's - the standard set's.
                  "UltraCloud/ui"]
SKIP_APPS = {"CSSLayoutTests"}
LIT = r'"((?:[^"\\\n]|\\.)*)"'
SITE = re.compile(r"(?<![A-Za-z0-9_:])(Tr|TrC|TrN|TrF)\s*\(")
SOURCE_SUFFIXES = (".cpp", ".h", ".mm", ".hpp")


ESCAPE = re.compile(r"\\(u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|x[0-9a-fA-F]{1,2}|[0-7]{1,3}|.)", re.S)
SIMPLE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "a": "\a", "b": "\b", "f": "\f", "v": "\v",
                  '"': '"', "'": "'", "\\": "\\", "?": "?"}


def unescape(s: str) -> str:
    """The text a C++ string literal's body stands for: \\n, \\", \\\\, \\xHH, octal,
    \\uXXXX and \\UXXXXXXXX (a character outside Latin-1, such as \\u2022, as
    the UTF-8 text the compiler emits).

    \\xHH and octal escapes are bytes, as in the compiled narrow literal: a
    source that spells a character as its UTF-8 bytes ("\\xC2\\xB7" for the
    middle dot) means that character, so the bytes are gathered and decoded
    as UTF-8 together. Only when they are not valid UTF-8 is each one read
    as a character of its own, as before."""
    if "\\" not in s:
        return s

    data = bytearray()
    pos = 0
    for m in ESCAPE.finditer(s):
        data += s[pos:m.start()].encode("utf-8")
        e = m.group(1)
        if e[0] == "x":
            data.append(int(e[1:], 16))
        elif e[0] in "01234567":
            data.append(int(e, 8) & 0xFF)
        elif e[0] in "uU":
            data += chr(int(e[1:], 16)).encode("utf-8")
        else:
            data += SIMPLE_ESCAPES.get(e, e).encode("utf-8")
        pos = m.end()
    data += s[pos:].encode("utf-8")
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        pass

    def one(m):
        e = m.group(1)
        if e[0] in "uUx":
            return chr(int(e[1:], 16))
        if e[0] in "01234567":
            return chr(int(e, 8))
        return SIMPLE_ESCAPES.get(e, e)

    return ESCAPE.sub(one, s)


def strip_comments(text: str) -> str:
    """The source without its // and /* */ comments. String and character
    literals are walked over, so a "/*" or "//" inside one (a comment
    opener in a list of markers, a URL) does not swallow the code after it;
    the newlines of a block comment stay, so line numbers hold."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            end = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, end))
            i = end
        else:
            out.append(c)
            i += 1
    return "".join(out)


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
    text = strip_comments(text)
    found = []
    for m in SITE.finditer(text):
        name = m.group(1)
        args = literal_args(text, m.end() - 1)
        if name == "Tr" and len(args) >= 1:
            found.append(("entry", args[0], None))
        elif name == "TrF" and len(args) >= 1:
            found.append(("entry", args[0], None))
        elif name == "TrC" and len(args) >= 2:
            found.append(("context", args[0] + "|" + args[1], args[1]))
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
                elif kind == "context":
                    entries.setdefault(key, plural)        # "button|Open": "Open" - the text without its context
                else:
                    plurals.setdefault(key, {"one": key, "other": plural})
    return entries, plurals


CONFLICT_MARKER = re.compile(r"^(<{7}|={7}|>{7})( |$)", re.M)


class MetadataConflict(Exception):
    """A catalogue the merge left conflicted whose two sides set its metadata differently."""


def merge_side(path: Path, stage: int) -> dict:
    """The catalogue as one side of an unfinished merge had it: stage 2 is
    the branch merged into, 3 the one merged in. {} when that side has none."""
    rel = path.relative_to(ROOT).as_posix()
    shown = subprocess.run(["git", "show", ":%d:%s" % (stage, rel)], cwd=ROOT, capture_output=True,
                           text=True, encoding="utf-8")
    if shown.returncode != 0:
        return {}
    try:
        return json.loads(shown.stdout)
    except ValueError:
        return {}


def read_conflicted(path: Path) -> dict:
    """A catalogue the merge left with conflict markers, read from both
    sides: their metadata (the same on both, or one side's alone), and for
    the standard set every key either side has."""
    ours, theirs = merge_side(path, 2), merge_side(path, 3)
    if not ours and not theirs:
        raise MetadataConflict("conflict markers, and git has no sides for it (not in a merge?)")
    merged = {}
    for key in sorted({k for k in ours if k.startswith("@")} | {k for k in theirs if k.startswith("@")}):
        if key == "@inherited":
            continue                        # worked out again from the sources
        if key in ours and key in theirs and ours[key] != theirs[key]:
            raise MetadataConflict("%s differs between the two sides: %s / %s"
                                   % (key, json.dumps(ours[key]), json.dumps(theirs[key])))
        merged[key] = ours[key] if key in ours else theirs[key]
    for side in (theirs, ours):             # ours last: its wording wins a key both have
        merged.update({k: v for k, v in side.items() if not k.startswith("@")})
    print("%s: merge conflict - rebuilt from both sides and the sources; git add it"
          % path.relative_to(ROOT), file=sys.stderr)
    return merged


def read_catalogue(path: Path):
    if not path.exists():
        return {}
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as err:
        print("%s: not read: %s" % (path.relative_to(ROOT), err), file=sys.stderr)
        return {}
    if CONFLICT_MARKER.search(text):
        return read_conflicted(path)
    try:
        return json.loads(text)
    except ValueError as err:
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
        # Initialize("Name"), or a helper that passes it on as its last
        # argument - DemoApp's InitializeSystem(g_app, "UltraCanvasDemo").
        m = re.search(r'Initialize\w*\((?:[^()"]*,\s*)?"([^"]+)"\)', path.read_text(encoding="utf-8", errors="ignore"))
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
    unresolved = []                         # conflicted catalogues whose metadata the sides disagree on

    # The standard set: add, never remove
    standard_path = LOCALE / "en" / "Standard.json"
    try:
        standard = read_catalogue(standard_path)
    except MetadataConflict as err:
        print("%s: %s - merge its metadata by hand, then run this again"
              % (standard_path.relative_to(ROOT), err), file=sys.stderr)
        return 1
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
        try:
            existing = read_catalogue(path)
        except MetadataConflict as err:
            unresolved.append("%s: %s" % (path.relative_to(ROOT), err))
            continue
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
    if unresolved:
        for line in unresolved:
            print(line + " - merge its metadata by hand, then run this again", file=sys.stderr)
        return 1
    if args.check and changed:
        print("extract_ui_strings: %d catalogue(s) out of date - run python3 scripts/extract_ui_strings.py" % len(changed))
        return 1
    print("extract_ui_strings: %s" % ("in sync" if args.check else "%d catalogue(s) rewritten" % len(changed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
