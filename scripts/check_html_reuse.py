#!/usr/bin/env python3
"""Flag HTML, CSS or entity handling written again outside the HTMLReader module.

The framework reads HTML and CSS in one place, UltraCanvas/{include,core}/
HTMLReader/ (see "Core conventions" in AGENTS.md and
Docs/UltraCanvas/UltraCanvasHTMLReader.md): a parser and DOM, a CSS parser
with selector matching for any tree, the cascade, an element builder, an
importer into UCRichDocument, and the two helpers everybody needs -
HTML::DecodeEntities and HTML::ExtractPlainText. By 2026-10 seven places had
written their own instead - the Filer's file preview, UltraMail's plain-text
view and its threat scan, EmailCleaner, the rich document's paste path and
two SVG readers - each with a different handful of entities and a tag
stripper that breaks on a different input (a `>` inside an attribute value, a
<style> body shown as text, `&#x2F;` left as written).

Two shapes are reported, in code outside the module:

  html-reuse-definition
      A function or method is defined (a body follows) with one of the names
      such code takes: DecodeEntities, DecodeHtmlEntities, UnescapeHtml,
      StripTags, StripHtml, HtmlToText, HtmlToPlainText, TokenizeHtml,
      ParseHtml, ApplyCss, ParseCss, ParseStyleAttribute, ParseInlineStyle,
      ParseFromStyle, and a CSS selector matcher (SelectorMatches,
      CompoundMatches, MatchSelector): a tree other than the DOM supplies a
      Traits type to HTML::MatchingRules instead. Calls and bare declarations
      are not reported: calling HTML::DecodeEntities is the point.

  html-reuse-entity-table
      A file holds a literal table of HTML entities: "nbsp" (or "&nbsp;") as
      a string literal together with at least two of amp, lt, gt, quot and
      apos, outside a test. nbsp is the tell: a writer that *escapes* text
      for XML or Pango markup names amp, lt, gt, quot and apos and never
      nbsp, and a decoder of HTML always has it.

Both are heuristics for the one review question: is this file reading HTML
or CSS on its own? A site that must stay says why, on the line or the one
above it, and is skipped:

    std::string DecodeEntities(std::string s) {   // html-reuse-exempt: WebDAV XML, five XML entities only

`scripts/html_reuse_baseline.txt` lists the sites that predate the check, so
CI blocks new ones while these are replaced; the baseline only shrinks.

Usage:
    python3 scripts/check_html_reuse.py [--strict] [paths...]
    python3 scripts/check_html_reuse.py --update-baseline

Exits 0 when clean. Without --strict, findings are reported and the exit code
stays 0.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BASELINE = REPO_ROOT / "scripts" / "html_reuse_baseline.txt"

SEARCH_ROOTS = [
    "UltraCanvas/core",
    "UltraCanvas/include",
    "UltraCanvas/Plugins",
    "UltraCanvas/libspecific",
    "UltraCanvas/OS",
    "UltraCanvas/dialogs",
    "Apps",
    "UltraAI",
    "UltraCloud",
    "VirtualFS",
    "SmartHome",
    "UltraWeb",
]
# The module itself, vendored code, generated output and the tests (a test
# may well hold an entity table to check the real decoder against).
SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug", "HTMLReader", "Tests"}
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".mm", ".cc"}

EXEMPT_RE = re.compile(r"//\s*html-reuse-exempt\s*:\s*(?P<reason>.+)")

# `<return type> Name(` at the start of a declaration or definition: a word
# or `>`/`*`/`&` before the name, then `(`, and no `;` right after the `)`
# would be nicer but costs a parser; a declaration in a header counts as the
# same intent, so it is reported too.
DEFINITION_NAMES = (
    "DecodeEntities", "DecodeHtmlEntities", "DecodeHTMLEntities", "UnescapeHtml",
    "UnescapeHTML", "StripTags", "StripHtml", "StripHTML", "HtmlToText", "HTMLToText",
    "HtmlToPlainText", "HtmlToPlain", "TokenizeHtml", "TokenizeHTML", "ParseHtml",
    "ParseHTML", "ApplyCss", "ApplyCSS", "ParseCss", "ParseCSS", "ParseStyleAttribute",
    "ParseInlineStyle", "ParseFromStyle",
    "SelectorMatches", "CompoundMatches", "MatchSelector", "MatchesSelector",
)
# A return type is required, so `for (x : TokenizeHtml(s)) {` and
# `return StripHtml(s)` - calls - are not definitions.
DEFINITION_RE = re.compile(
    r"(?:^|[\s>*&])(?!return\b)[A-Za-z_][A-Za-z0-9_:<>]*\s+[*&]?\s*(?:[A-Za-z_][A-Za-z0-9_]*::)?"
    r"(?P<name>" + "|".join(DEFINITION_NAMES) + r")\s*\([^;]*\)\s*(?:const\s*)?(?:\{|$)",
    re.MULTILINE,
)

ENTITY_NAMES = ("amp", "lt", "gt", "quot", "apos", "nbsp")
# "amp" or "&amp;" as a string literal (a comparison or a table entry).
ENTITY_RE = re.compile(r'"&?(?P<name>' + "|".join(ENTITY_NAMES) + r');?"')


class Finding:
    def __init__(self, path: Path, line: int, kind: str, detail: str) -> None:
        self.path = path
        self.line = line
        self.kind = kind
        self.detail = detail

    @property
    def rel(self) -> str:
        try:
            return self.path.resolve().relative_to(REPO_ROOT).as_posix()
        except ValueError:
            return self.path.as_posix()

    # The baseline is keyed without the line number, so a file that merely
    # moves its copy around stays known while a new copy in a new file is
    # reported.
    @property
    def key(self) -> str:
        return f"{self.rel}\t{self.kind}\t{self.detail}"

    def __str__(self) -> str:
        return f"{self.rel}:{self.line}: {self.kind}: {self.detail}"


def load_baseline(path: Path) -> set[str]:
    if not path.exists():
        return set()
    entries = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            entries.add(line)
    return entries


def iter_sources(paths: list[Path]):
    for base in paths:
        if base.is_file():
            if base.suffix in SOURCE_SUFFIXES:
                yield base
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            if any(part in SKIP_PARTS for part in path.parts):
                continue
            yield path


def strip_comments_and_strings_keep_lines(text: str) -> str:
    """Blank out comments so a mention in prose is not a definition; keep
    string literals (the entity check needs them) and line structure."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            if end == -1:
                end = n
            out.append(" " * (end - i))
            i = end
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            end = n if end == -1 else end + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:end]))
            i = end
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                if text[j] == "\\":
                    j += 1
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def exempt_at(lines: list[str], line_no: int) -> bool:
    for candidate in (line_no, line_no - 1):
        if 1 <= candidate <= len(lines) and EXEMPT_RE.search(lines[candidate - 1]):
            return True
    return False


def check_file(path: Path) -> list[Finding]:
    try:
        raw = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    lines = raw.splitlines()
    code = strip_comments_and_strings_keep_lines(raw)
    findings: list[Finding] = []

    for match in DEFINITION_RE.finditer(code):
        line_no = line_of(code, match.start("name"))
        if exempt_at(lines, line_no):
            continue
        findings.append(Finding(path, line_no, "html-reuse-definition",
                                f"{match.group('name')}() defined here"))

    names_seen: dict[str, int] = {}
    for match in ENTITY_RE.finditer(code):
        line_no = line_of(code, match.start())
        if exempt_at(lines, line_no):
            continue
        names_seen.setdefault(match.group("name"), line_no)
    if "nbsp" in names_seen and len(names_seen) >= 3:
        first = min(names_seen.values())
        findings.append(Finding(path, first, "html-reuse-entity-table",
                                "entity table (" + ", ".join(sorted(names_seen)) + ")"))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero on findings that are not in the baseline (CI gate)")
    parser.add_argument("--baseline", type=Path, default=BASELINE,
                        help="file of already-known findings (default: scripts/html_reuse_baseline.txt)")
    parser.add_argument("--no-baseline", action="store_true",
                        help="report every finding, known or not")
    parser.add_argument("--update-baseline", action="store_true",
                        help="rewrite the baseline from the current tree and exit")
    parser.add_argument("paths", nargs="*", type=Path,
                        help="files or directories to check (default: the usual roots)")
    args = parser.parse_args()

    roots = args.paths or [REPO_ROOT / r for r in SEARCH_ROOTS if (REPO_ROOT / r).exists()]
    findings: list[Finding] = []
    for path in iter_sources(roots):
        findings.extend(check_file(path))
    findings.sort(key=lambda f: (f.rel, f.line))

    if args.update_baseline:
        header = (
            "# Sites that read HTML, CSS or entities on their own, from before\n"
            "# scripts/check_html_reuse.py existed. Each is a replacement waiting to\n"
            "# happen (HTML::ExtractPlainText, HTML::DecodeEntities,\n"
            "# ImportHTMLToRichDocument, HTML::StyleSheet) - see\n"
            "# Docs/UltraCanvas/UltraCanvasHTMLReader.md. Only shrink this file.\n"
            "# Regenerate with: python3 scripts/check_html_reuse.py --update-baseline\n"
        )
        args.baseline.write_text(header + "".join(f.key + "\n" for f in findings), encoding="utf-8")
        print(f"wrote {len(findings)} entr{'y' if len(findings) == 1 else 'ies'} to "
              f"{args.baseline.relative_to(REPO_ROOT)}")
        return 0

    baseline = set() if args.no_baseline else load_baseline(args.baseline)
    fresh = [f for f in findings if f.key not in baseline]
    known = [f for f in findings if f.key in baseline]

    for f in fresh:
        print(f)
    if fresh:
        print(
            f"\n{len(fresh)} new site(s) read HTML, CSS or entities outside the HTMLReader "
            "module. Use HTML::ExtractPlainText / HTML::DecodeEntities / HTML::Parser /\n"
            "HTML::StyleSheet (Docs/UltraCanvas/UltraCanvasHTMLReader.md), add what is "
            "missing to the module, or say why this one must stay:\n"
            "    // html-reuse-exempt: <why>"
        )
    if known:
        print(f"\n{len(known)} known site(s) still on the baseline ({args.baseline.name}); "
              "replacing one with the module is always welcome.")

    stale = [] if args.paths else sorted(baseline - {f.key for f in findings})
    if stale:
        print(f"\n{len(stale)} baseline entr{'y is' if len(stale) == 1 else 'ies are'} "
              "no longer found - drop them with --update-baseline:")
        for key in stale:
            print("    " + key.replace("\t", "  "))

    if not fresh and not stale:
        print("check_html_reuse: clean")
    if args.strict and (fresh or stale):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
