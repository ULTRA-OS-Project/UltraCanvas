#!/usr/bin/env python3
"""Flag a label built from one string that is its text.

An element built from one string takes it as its IDENTIFIER:

    std::make_shared<UltraCanvasContainer>("ContentSection");
    std::make_shared<UltraCanvasLabel>("CountLabel");          // named, no text yet

and the text, where the element has one, comes second or from SetText:

    std::make_shared<UltraCanvasLabel>("InfoTitle", Tr("About"));
    std::make_shared<UltraCanvasLabel>("", Tr("Select a repository"));   // no name

`UltraCanvasLabel` was the exception until 2026-11: its one string was the
label's text. That read like every other element and meant the opposite, and
eleven callers in this repository had handed it a name - the dialog's
severity icon, Texter's status bar, three labels of the HTML builder, the
framework's own LabelBuilder - against one that handed it text. The
constructor now takes the identifier, which made those eleven right; what
it cannot do is refuse the old use, because both are one string. So

    auto hint = std::make_shared<UltraCanvasLabel>(Tr("Select a repository"));

still compiles, and makes a label that shows nothing and is named after a
sentence. This check is what refuses it.

What this reports:

  one-string-text
      A label - `make_shared<UltraCanvasLabel>(...)`, `make_unique`, `new`,
      a declared `UltraCanvasLabel name(...)`, a temporary, a base-class
      initialiser, `CreateLabel(...)` - built from exactly one argument
      that reads as text: a `Tr` / `TrF` / `TrN` / `TrC` call, or a string
      literal that is not a name (a space in it, a colon after it, a
      sentence).

A one-string literal that is a name - "CountLabel", "uai-hf-status",
"Ready" - is the identifier it says it is and is not reported. Neither is a
variable: the check cannot see what it holds. For those the framework says
so at run time, on debugOutput (`WarnIfIdentifierReadsAsText`).

An identifier that really does read as text says so on its line, or alone
on the line above:

    // label-identifier-ok: <why this one string is the identifier>

Usage:
    python3 scripts/check_label_identifiers.py [--strict] [paths...]

Exits 0 when clean. Without --strict, findings are reported and the exit code
stays 0.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Where elements are built: the framework, the applications, the modules
# with a user interface and the tests. Vendored code and build output are
# never scanned.
SEARCH_ROOTS = [
    "UltraCanvas/core",
    "UltraCanvas/include",
    "UltraCanvas/dialogs",
    "UltraCanvas/Plugins",
    "UltraCanvas/OS",
    "UltraCanvas/libspecific",
    "Apps",
    "SmartHome",
    "UltraAI",
    "UltraCloud",
    "UltraWeb",
    "Tests",
]
SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug"}
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".mm"}

EXEMPT_RE = re.compile(r"//\s*label-identifier-ok\s*:\s*\S")

# The elements that have a text, and whose one string is nevertheless the
# identifier - the ones a text is handed to by mistake.
ELEMENTS = ("Label",)
_ELEMENT = r"(?:UltraCanvas::)?UltraCanvas(?:" + "|".join(ELEMENTS) + r")"
_FACTORY = r"Create(?:" + "|".join(ELEMENTS) + r")"

# Everything that constructs one, up to and including the opening bracket.
CONSTRUCTION_RE = re.compile(
    r"(?:"
    r"\bmake_(?:shared|unique)\s*<\s*" + _ELEMENT + r"\s*>"     # make_shared<UltraCanvasLabel>(
    r"|\bnew\s+" + _ELEMENT +                                   # new UltraCanvasLabel(
    r"|\b" + _ELEMENT + r"\s+\w+"                               # UltraCanvasLabel name(
    r"|\b" + _ELEMENT +                                         # UltraCanvasLabel( - a temporary, a base initialiser
    r"|\b" + _FACTORY +                                         # CreateLabel(
    r")\s*(?P<open>[({])"
)

# A name: letters and digits, in parts joined by the marks identifiers are
# written with.
NAME_RE = re.compile(r"[A-Za-z0-9]+(?:[-_.:/#][A-Za-z0-9]+)*")
# One string literal, or several written side by side.
LITERAL_RE = re.compile(r'(?:"(?:[^"\\\n]|\\.)*"\s*)+')
# A call that returns text for a person: Tr("Save"), UltraCanvas::TrF(...).
TRANSLATED_RE = re.compile(r"(?:UltraCanvas::)?Tr[FNC]?\s*\(")


def reads_as_text(literal: str) -> bool:
    """Whether a string's content is words rather than a name."""
    return literal != "" and NAME_RE.fullmatch(literal) is None


def strip_comments(src: str) -> str:
    """Blank out comments, keeping string literals and every line break."""
    out: list[str] = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            # A literal, kept as it is. (A raw string is rare enough in
            # element construction to be read as an ordinary one.)
            j = i + 1
            while j < n and src[j] != c:
                if src[j] == "\\":
                    j += 1
                if j < n and src[j] == "\n":
                    break       # not a literal after all: an apostrophe
                j += 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in src[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def arguments(code: str, open_at: int) -> list[tuple[int, str]] | None:
    """The arguments of the call whose bracket opens at `open_at`, each with
    where it starts; None when the bracket never closes."""
    depth = 0
    parts: list[tuple[int, str]] = []
    start = open_at + 1
    i, n = open_at, len(code)
    while i < n:
        c = code[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and code[j] != c:
                if code[j] == "\\":
                    j += 1
                j += 1
            i = j
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                parts.append((start, code[start:i]))
                break
        elif c == "," and depth == 1:
            parts.append((start, code[start:i]))
            start = i + 1
        i += 1
    else:
        return None
    out = []
    for at, text in parts:
        lead = len(text) - len(text.lstrip())
        if text.strip():
            out.append((at + lead, text.strip()))
    return out


def literal_content(argument: str) -> str | None:
    """What a string-literal argument says; None when it is not one."""
    if not LITERAL_RE.fullmatch(argument):
        return None
    pieces = re.findall(r'"((?:[^"\\\n]|\\.)*)"', argument)
    return "".join(pieces).replace('\\"', '"').replace("\\n", "\n").replace("\\t", "\t")


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    shown: str

    def __str__(self) -> str:
        try:
            where = self.path.relative_to(REPO_ROOT)
        except ValueError:
            where = self.path
        return (f"{where}:{self.line}: one-string-text  {self.shown} is this label's "
                f'identifier, not its text - write ("", <text>) or (<identifier>, <text>)')


def check_source(src: str, path: Path) -> list[Finding]:
    lines = src.splitlines()
    code = strip_comments(src)
    findings: dict[int, Finding] = {}
    for match in CONSTRUCTION_RE.finditer(code):
        args = arguments(code, match.start("open"))
        if args is None or len(args) != 1:
            continue
        at, argument = args[0]
        content = literal_content(argument)
        if content is not None:
            if not reads_as_text(content):
                continue
            shown = '"' + (content if len(content) <= 40 else content[:37] + "...") + '"'
        elif TRANSLATED_RE.match(argument):
            shown = argument if len(argument) <= 42 else argument[:39] + "..."
            shown = " ".join(shown.split())
        else:
            continue        # a variable, a call: not something this can read
        first = code.count("\n", 0, match.start()) + 1
        line = code.count("\n", 0, at) + 1
        # The marker sits on a line of the construction, or alone on the
        # line above it (a marker that trails the statement before belongs
        # to that statement).
        marked = any(EXEMPT_RE.search(lines[n - 1])
                     for n in range(first, line + 1) if n <= len(lines))
        if not marked and first >= 2 and lines[first - 2].lstrip().startswith("//"):
            marked = EXEMPT_RE.search(lines[first - 2]) is not None
        if not marked:
            findings.setdefault(at, Finding(path, line, shown))
    return [findings[k] for k in sorted(findings)]


def check_file(path: Path) -> list[Finding]:
    try:
        src = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    if not any(("UltraCanvas" + e) in src or ("Create" + e) in src for e in ELEMENTS):
        return []
    return check_source(src, path)


def iter_sources(paths: list[Path]):
    for base in paths:
        if base.is_file():
            if base.suffix in SOURCE_SUFFIXES:
                yield base
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            if SKIP_PARTS & set(path.parts):
                continue
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero on findings (CI gate)")
    parser.add_argument("paths", nargs="*",
                        help="files or directories to scan (default: the UI source roots)")
    args = parser.parse_args()

    if args.paths:
        roots = [Path(p).resolve() for p in args.paths]
    else:
        roots = [REPO_ROOT / r for r in SEARCH_ROOTS]
        roots = [r for r in roots if r.exists()]

    findings: list[Finding] = []
    scanned = 0
    for path in iter_sources(roots):
        scanned += 1
        findings.extend(check_file(path))

    for finding in findings:
        print(finding)

    if findings:
        print(
            f"\n{len(findings)} element(s) built from one string that is text, in "
            f"{scanned} files.\nOne string is an element's IDENTIFIER, so these show "
            "nothing and are named after their words. Put the text second - "
            'make_shared<UltraCanvasLabel>("", Tr("...")), or give the element a name: '
            '("InfoTitle", Tr("...")). If the one string really is an identifier, say '
            "so on its line:\n"
            "    // label-identifier-ok: <why this one string is the identifier>"
        )
        return 1 if args.strict else 0

    print(f"check_label_identifiers: clean ({scanned} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
