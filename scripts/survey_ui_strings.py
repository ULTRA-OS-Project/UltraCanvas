#!/usr/bin/env python3
"""Which UI strings the applications share - the measure behind the
standard set of Docs/UltraCanvas/UltraCanvasLocalisationProposal.md.

Every string literal of Apps/<App>/**/*.cpp,*.h that looks like interface
text (starts with a capital, letters, spaces and light punctuation, 2-60
characters, no identifier shapes) is counted once per application; the
same over UltraCanvas/core/**/*.cpp for the framework's own. DemoApp is
left out (its pages name every element's demo, which no application
shares).

    python3 scripts/survey_ui_strings.py            # strings in 3+ applications
    python3 scripts/survey_ui_strings.py --min 2    # in 2+
    python3 scripts/survey_ui_strings.py --core     # the framework's own, in 2+ core files
    python3 scripts/survey_ui_strings.py --json out.json

A survey, not a check: it over-counts (a log line can look like a label)
and under-counts (a string built from pieces), and its answer is a list to
read, not a gate. The extraction script the proposal describes will read
Tr() sites instead, once they exist.
"""
import argparse
import collections
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
UI_TEXT = re.compile(r"^[A-Z][A-Za-z0-9 ,.'?!:;&/()+\-…]*(\.\.\.)?$")
NOISE = re.compile(r'^(Ctrl|Shift|Alt|Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec|Mon|Tue|Wed|Thu|Fri|Sat|Sun|'
                   r'SELECT|CREATE|INSERT|UPDATE|DELETE FROM|Content-|Usage|Options:|Examples:|Unhandled|'
                   r'Warning: X11|Failed to (initialize|create)|FATAL|Fatal|Unknown (argument|exception)|'
                   r'Use --help|UltraCanvas Framework|Application Support)')
SKIP_APPS = {"DemoApp", "CSSLayoutTests", "UltraMessageCli"}


def looks_like_ui_text(s):
    if not 2 <= len(s) <= 60 or not UI_TEXT.match(s):
        return False
    if "_" in s or "/" in s:
        return False
    if re.fullmatch(r"[A-Z][a-z]*(?:[A-Z][a-z]*)+", s):          # PascalCase identifier
        return False
    if re.fullmatch(r"[A-Z0-9-]{3,}", s) and s not in ("PDF", "CSV", "URL"):  # a CONSTANT
        return False
    if s.endswith(" ") or NOISE.match(s):
        return False
    return True


def literals(path):
    try:
        src = open(path, encoding="utf-8", errors="ignore").read()
    except OSError:
        return []
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"#include[^\n]*", "", src)
    return [s for s in LITERAL.findall(src) if looks_like_ui_text(s)]


def walk(folder, suffixes):
    for dirpath, _, files in os.walk(folder):
        for name in files:
            if name.endswith(suffixes):
                yield os.path.join(dirpath, name)


def survey():
    by_app = collections.defaultdict(set)
    for app in sorted(os.listdir(os.path.join(ROOT, "Apps"))):
        folder = os.path.join(ROOT, "Apps", app)
        if app in SKIP_APPS or not os.path.isdir(folder):
            continue
        for path in walk(folder, (".cpp", ".h")):
            for s in literals(path):
                by_app[s].add(app)
    by_core = collections.defaultdict(set)
    for path in walk(os.path.join(ROOT, "UltraCanvas", "core"), (".cpp",)):
        for s in literals(path):
            by_core[s].add(os.path.basename(path))
    return by_app, by_core


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--min", type=int, default=3, help="list strings in at least this many applications")
    parser.add_argument("--core", action="store_true", help="list the framework's own strings instead")
    parser.add_argument("--json", help="write the whole survey to this file")
    args = parser.parse_args()
    by_app, by_core = survey()
    if args.json:
        with open(args.json, "w", encoding="utf-8") as out:
            json.dump({"apps": {k: sorted(v) for k, v in by_app.items()},
                       "core": {k: sorted(v) for k, v in by_core.items()}},
                      out, ensure_ascii=False, indent=1)
    rows = by_core if args.core else by_app
    minimum = 2 if args.core else args.min
    listed = sorted(((k, v) for k, v in rows.items() if len(v) >= minimum), key=lambda kv: (-len(kv[1]), kv[0]))
    for text, where in listed:
        also = "" if args.core or text not in by_core else "  [framework: %d]" % len(by_core[text])
        print("%3d  %s%s" % (len(where), text, also))
    print("%d string(s) in %d+ %s" % (len(listed), minimum, "core files" if args.core else "applications"),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
