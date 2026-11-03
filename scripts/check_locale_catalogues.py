#!/usr/bin/env python3
"""Check the translation catalogues under media/locale/ (CI gate).

Docs/UltraCanvas/UltraCanvasLocalisationProposal.md §8. For every language
folder but en, every <Set>.json and base/<Base>.json:

- it parses, and is a JSON object;
- every key is in the matching English source catalogue (a key the
  source no longer has is stale: a changed English string wants a new
  translation, not an old one under a key nobody looks up);
- a placeholder ({0}, {1}, {n}) in the source is in the translation;
- a plural entry has every category the language's plural rule needs
  (the table mirrors UltraLocalisation::PluralCategory);
- it repeats no entry of a set below it - an application's set against
  its bases and the standard set, a base set against the standard set -
  with the same translation: that is the maintenance the layers exist to
  end;
- "@bases" names base sets that exist.

And the English catalogues are in sync with the sources
(extract_ui_strings.py --check). A collision between two of an
application's base sets - one key, two translations - is reported, not
failed: the application's order decides, and the report is how the two
converge.

Findings that predate the check are in scripts/locale_catalogues_baseline.txt;
--strict fails on a finding not in it; --update-baseline rewrites it from
the tree (the file only shrinks).

    python3 scripts/check_locale_catalogues.py --strict
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCALE = ROOT / "media" / "locale"
BASELINE = ROOT / "scripts" / "locale_catalogues_baseline.txt"
PLACEHOLDER = re.compile(r"\{(\d+|n)\}")

# The categories each language's integer plural rule can produce, by base
# language; everything else is one/other.
CATEGORIES = {
    "ja": ["other"], "zh": ["other"], "yue": ["other"], "ko": ["other"], "th": ["other"], "vi": ["other"],
    "id": ["other"], "ms": ["other"], "my": ["other"], "km": ["other"], "lo": ["other"], "jv": ["other"],
    "ig": ["other"], "yo": ["other"], "bo": ["other"], "dz": ["other"], "ii": ["other"],
    "ru": ["one", "few", "many"], "uk": ["one", "few", "many"], "be": ["one", "few", "many"],
    "pl": ["one", "few", "many"], "cs": ["one", "few", "other"], "sk": ["one", "few", "other"],
    "ro": ["one", "few", "other"], "lt": ["one", "few", "other"], "lv": ["zero", "one", "other"],
    "hr": ["one", "few", "other"], "sr": ["one", "few", "other"], "bs": ["one", "few", "other"],
    "sl": ["one", "two", "few", "other"], "ga": ["one", "two", "few", "many", "other"],
    "ar": ["zero", "one", "two", "few", "many", "other"], "he": ["one", "two", "many", "other"],
    "cy": ["zero", "one", "two", "few", "many", "other"], "mt": ["one", "few", "many", "other"],
    "gd": ["one", "two", "few", "other"], "br": ["one", "two", "few", "many", "other"],
    "shi": ["one", "few", "other"], "gv": ["one", "two", "few", "other"],
    "kw": ["zero", "one", "two", "few", "many", "other"], "ksh": ["zero", "one", "other"], "lag": ["zero", "one", "other"],
    "iu": ["one", "two", "other"], "se": ["one", "two", "other"], "naq": ["one", "two", "other"],
}


class Finding:
    def __init__(self, path: Path, message: str):
        self.path = path
        self.message = message

    @property
    def key(self) -> str:
        return "%s::%s" % (self.path.relative_to(ROOT).as_posix(), self.message)


def read(path: Path, findings):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as err:
        findings.append(Finding(path, "does not parse: %s" % err))
        return None
    if not isinstance(data, dict):
        findings.append(Finding(path, "is not a JSON object"))
        return None
    return data


def entries_of(data: dict) -> dict:
    return {k: v for k, v in data.items() if not k.startswith("@")}


def base_language(tag: str) -> str:
    return tag.split("-")[0].lower()


def check_file(path: Path, lang: str, source: dict, below: list, findings, notes):
    data = read(path, findings)
    if data is None:
        return
    entries = entries_of(data)
    for key, value in entries.items():
        if key not in source:
            findings.append(Finding(path, "stale key: %r is not in the English source catalogue" % key))
            continue
        src = source[key]
        src_text = src if isinstance(src, str) else " ".join(src.values())
        wanted = set(PLACEHOLDER.findall(src_text))
        if isinstance(value, str):
            if isinstance(src, dict):
                if base_language(lang) not in CATEGORIES or CATEGORIES[base_language(lang)] != ["other"]:
                    findings.append(Finding(path, "plural entry %r is a plain string" % key))
            have = set(PLACEHOLDER.findall(value))
            for p in sorted(wanted - have):
                findings.append(Finding(path, "placeholder {%s} of %r is missing from the translation" % (p, key)))
        elif isinstance(value, dict):
            if not isinstance(src, dict):
                findings.append(Finding(path, "%r is a plural object but a plain entry in the source" % key))
            needed = CATEGORIES.get(base_language(lang), ["one", "other"])
            for category in needed:
                if category not in value:
                    findings.append(Finding(path, "plural entry %r lacks the %r form the language needs" % (key, category)))
            for category, form in value.items():
                if isinstance(form, str) and "{n}" in src_text and "{n}" not in form:
                    findings.append(Finding(path, "placeholder {n} of %r is missing from its %r form" % (key, category)))
        else:
            findings.append(Finding(path, "%r is neither a string nor a plural object" % key))
        for name, lower in below:
            if key in lower and lower[key] == value:
                findings.append(Finding(path, "%r repeats the %s set's translation" % (key, name)))
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    parser.add_argument("--strict", action="store_true", help="exit non-zero on findings not in the baseline")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--update-baseline", action="store_true", help="rewrite the baseline from the tree and exit")
    parser.add_argument("--no-sync", action="store_true", help="skip the extraction sync check")
    args = parser.parse_args()

    findings, notes = [], []
    en = LOCALE / "en"
    sources = {}
    base_sources = {}
    if en.exists():
        for path in sorted(en.glob("*.json")):
            data = read(path, findings)
            if data is not None:
                sources[path.stem] = entries_of(data)
        for path in sorted((en / "base").glob("*.json")) if (en / "base").exists() else []:
            data = read(path, findings)
            if data is not None:
                base_sources[path.stem] = entries_of(data)
        # An application's en catalogue naming a base that does not exist
        for path in sorted(en.glob("*.json")):
            data = read(path, [])
            for base in (data or {}).get("@bases", []):
                if base not in base_sources:
                    findings.append(Finding(path, "@bases names %r, which has no base set" % base))

    for folder in sorted(LOCALE.iterdir()) if LOCALE.exists() else []:
        if not folder.is_dir() or folder.name == "en":
            continue
        lang = folder.name
        standard = read(folder / "Standard.json", []) if (folder / "Standard.json").exists() else {}
        standard_entries = entries_of(standard or {})
        bases = {}
        if (folder / "base").exists():
            for path in sorted((folder / "base").glob("*.json")):
                data = check_file(path, lang, base_sources.get(path.stem, {}), [("standard", standard_entries)], findings, notes)
                if data is not None:
                    bases[path.stem] = entries_of(data)
        for path in sorted(folder.glob("*.json")):
            name = path.stem
            if name == "Standard":
                check_file(path, lang, sources.get("Standard", {}), [], findings, notes)
                continue
            en_data = read(en / (name + ".json"), []) if (en / (name + ".json")).exists() else {}
            app_bases = (en_data or {}).get("@bases", [])
            below = [("base " + b, bases.get(b, {})) for b in app_bases] + [("standard", standard_entries)]
            check_file(path, lang, sources.get(name, {}), below, findings, notes)
            # collisions between the application's bases
            seen = {}
            for b in app_bases:
                for key, value in bases.get(b, {}).items():
                    if key in seen and seen[key][1] != value:
                        notes.append("%s: %r differs between base sets %s and %s (the first named wins)"
                                     % (path.relative_to(ROOT).as_posix(), key, seen[key][0], b))
                    seen.setdefault(key, (b, value))

    if not args.no_sync and not args.update_baseline:
        sync = subprocess.run([sys.executable, str(ROOT / "scripts" / "extract_ui_strings.py"), "--check"],
                              capture_output=True, text=True)
        if sync.returncode != 0:
            for line in sync.stdout.splitlines():
                if line.startswith("would rewrite "):
                    findings.append(Finding(ROOT / line[len("would rewrite "):], "out of date: run python3 scripts/extract_ui_strings.py"))

    if args.update_baseline:
        keys = sorted(f.key for f in findings)
        header = ("# Findings of scripts/check_locale_catalogues.py that predate the CI check,\n"
                  "# so it can block new ones. One per line: <file>::<message>. Only shrinks.\n"
                  "# Regenerate with: python3 scripts/check_locale_catalogues.py --update-baseline\n")
        args.baseline.write_text(header + "".join(k + "\n" for k in keys), encoding="utf-8")
        print("check_locale_catalogues: wrote %d entries to %s" % (len(keys), args.baseline.relative_to(ROOT)))
        return 0

    baseline = set()
    if args.baseline.exists():
        baseline = {l.strip() for l in args.baseline.read_text(encoding="utf-8").splitlines() if l.strip() and not l.startswith("#")}
    fresh = [f for f in findings if f.key not in baseline]
    for note in notes:
        print("note: " + note)
    for f in (fresh if args.strict else findings):
        print("%s: %s" % (f.path.relative_to(ROOT).as_posix(), f.message))
    if args.strict:
        if fresh:
            print("check_locale_catalogues: %d new finding(s)" % len(fresh))
            return 1
        print("check_locale_catalogues: no new findings (%d baselined)" % (len(findings)))
        return 0
    print("check_locale_catalogues: %d finding(s), %d note(s)" % (len(findings), len(notes)))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
