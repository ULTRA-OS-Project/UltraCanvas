#!/usr/bin/env python3
"""Flag a label built from one string that reads as an identifier.

`UltraCanvasLabel` has four constructors, and the one that takes a single
string takes the label's TEXT:

    UltraCanvasLabel(identifier, x, y, w, h, text = "");
    UltraCanvasLabel(identifier, w, h, text = "");
    UltraCanvasLabel(identifier, text);
    explicit UltraCanvasLabel(text = "");          // one string = the text

`UltraCanvasContainer`'s single-string constructor is the other way round -
it takes the identifier - so `make_shared<UltraCanvasContainer>("Row")` names
a container, and the same line with a label

    countLabel = std::make_shared<UltraCanvasLabel>("CountLabel");

makes an unnamed label that says "CountLabel". Seven places had written
that by 2026-11, the framework's own dialog icon among them. None showed,
because each set the real text on the next line; but the label had no
identifier, so nothing could find it by name (`FindChildById`, a test, a
layout dump), and each was one deleted `SetText` away from showing its own
variable name to the user. `UltraCanvasCheckbox` has the same pair of
constructors.

What this reports:

  label-identifier
      A label or checkbox - `make_shared<UltraCanvasLabel>(...)`,
      `make_unique`, `new`, a declared `UltraCanvasLabel name(...)`,
      `CreateLabel(...)`, and the same for `UltraCanvasCheckbox` /
      `CreateCheckbox` - built from exactly one string literal that reads as
      a name rather than as words: one token with a capital inside it
      ("IconLabel", "countLabel"), a digit ("InfoText1"), or parts joined by
      "-", "_" or "." ("uai-hf-status").

Write the identifier and the text:

    countLabel = std::make_shared<UltraCanvasLabel>("CountLabel", "");
    title      = std::make_shared<UltraCanvasLabel>("InfoTitle", Tr("About"));

A label that needs no name takes its text alone, and text a person reads
goes through `Tr`, so it is not a bare literal and is never reported:

    auto hint = std::make_shared<UltraCanvasLabel>(Tr("Select a repository"));

A single plain word ("Ready", "OK") is taken for text. A one-string label
whose text really is shaped like a name - a product's ("UltraCanvas"), a
file's ("config.json") - says so on its line or the line above:

    // label-text-ok: <why this one string is the label's text>

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

EXEMPT_RE = re.compile(r"//\s*label-text-ok\s*:\s*\S")

# The elements whose single-string constructor takes the text.
ELEMENTS = r"(?:UltraCanvas::)?UltraCanvas(?:Label|Checkbox)"
# One string literal and nothing else between the parentheses (or braces).
ONE_LITERAL = r'\s*"(?P<text>(?:[^"\\\n]|\\.)*)"\s*'
CONSTRUCTION_RES = [
    # std::make_shared<UltraCanvasLabel>("X") / make_unique
    re.compile(r"\bmake_(?:shared|unique)\s*<\s*" + ELEMENTS + r"\s*>\s*\(" + ONE_LITERAL + r"\)"),
    # new UltraCanvasLabel("X")
    re.compile(r"\bnew\s+" + ELEMENTS + r"\s*[({]" + ONE_LITERAL + r"[)}]"),
    # UltraCanvasLabel name("X") / name{"X"}
    re.compile(r"\b" + ELEMENTS + r"\s+\w+\s*[({]" + ONE_LITERAL + r"[)}]"),
    # CreateLabel("X") / CreateCheckbox("X")
    re.compile(r"\bCreate(?:Label|Checkbox)\s*\(" + ONE_LITERAL + r"\)"),
]

# One token: letters and digits, in parts joined by "-", "_" or ".".
TOKEN_RE = re.compile(r"[A-Za-z][A-Za-z0-9]*(?:[-_.][A-Za-z0-9]+)*")


def reads_as_identifier(text: str) -> bool:
    """Whether a label's one string looks like a name rather than words."""
    if not TOKEN_RE.fullmatch(text):
        return False        # spaces, punctuation, another script: words
    if re.search(r"[-_.]", text):
        return True         # uai-hf-status, count_label, page.title
    if re.search(r"\d", text):
        return True         # InfoText1
    # A capital after a lower-case letter: IconLabel, countLabel. "OK",
    # "Ready" and "PDF" have none.
    return re.search(r"[a-z][A-Z]", text) is not None


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


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    text: str

    def __str__(self) -> str:
        try:
            shown = self.path.relative_to(REPO_ROOT)
        except ValueError:
            shown = self.path
        return (f'{shown}:{self.line}: label-identifier  "{self.text}" is this '
                f'element\'s text, not its identifier - write ("{self.text}", <text>)')


def check_source(src: str, path: Path) -> list[Finding]:
    lines = src.splitlines()
    code = strip_comments(src)
    findings: dict[int, Finding] = {}
    for construction in CONSTRUCTION_RES:
        for match in construction.finditer(code):
            text = match.group("text")
            if not reads_as_identifier(text):
                continue
            line = code.count("\n", 0, match.start("text")) + 1
            first = code.count("\n", 0, match.start()) + 1
            # The marker sits on a line of the construction, or alone on the
            # line above it (a marker that trails the statement before
            # belongs to that statement).
            marked = any(EXEMPT_RE.search(lines[n - 1])
                         for n in range(first, line + 1) if n <= len(lines))
            if not marked and first >= 2 and lines[first - 2].lstrip().startswith("//"):
                marked = EXEMPT_RE.search(lines[first - 2]) is not None
            if not marked:
                findings.setdefault(match.start("text"), Finding(path, line, text))
    return [findings[k] for k in sorted(findings)]


def check_file(path: Path) -> list[Finding]:
    try:
        src = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    if "UltraCanvasLabel" not in src and "UltraCanvasCheckbox" not in src and \
            "CreateLabel" not in src and "CreateCheckbox" not in src:
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
            f"\n{len(findings)} label(s) built from one string that reads as an "
            f"identifier, in {scanned} files.\nA label's single-string constructor "
            "takes its TEXT (a container's takes the identifier), so these are "
            "unnamed labels that say their own variable name. Write the identifier "
            'and the text - make_shared<UltraCanvasLabel>("CountLabel", "") - or, '
            "where the one string really is the text, say so on its line:\n"
            "    // label-text-ok: <why this one string is the label's text>"
        )
        return 1 if args.strict else 0

    print(f"check_label_identifiers: clean ({scanned} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
