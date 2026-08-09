#!/usr/bin/env python3
"""Flag UI controls painted by hand instead of built from UltraCanvas elements.

The framework ships ~60 UI elements (see "Build UI out of UltraCanvas elements"
in AGENTS.md). Reinventing one with raw drawing calls plus a private buffer or
hover flag loses everything the real element already handles: caret and
selection, clipboard, undo, multi-byte input, keyboard focus, theming, DPI
scaling, tooltips. The Filer's compress dialog shipped exactly that and the
name field could not be edited in the middle and went deaf whenever another
element took the focus.

Two shapes are reported, both chosen because they are cheap to spot and hard
to produce by accident:

  hand-rolled-text-field
      A type keeps its own edit buffer *and* its own caret/focus flag, and
      consumes printable characters out of a KeyDown handler, while never
      mentioning UltraCanvasTextInput / UltraCanvasTextArea.

  hand-rolled-button
      A painter of the shape Draw<Something>Button(IRenderContext*, ...)
      taking a hovered/pressed flag — a button drawn instead of instantiated.

Both are heuristics. A file that is legitimately an exception (the element
itself, or a self-rendered view whose *content* is drawn directly) opts out
with a marker comment naming the reason:

    // ui-reuse-exempt: UltraCanvasTextInput is the text input.

Usage:
    python3 scripts/check_ui_reuse.py [--strict] [paths...]

Exits 0 when clean. Without --strict, findings are reported and the exit code
stays 0, so the check can be introduced without blocking unrelated work.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Where UI code lives. Vendored code and generated output are never scanned.
SEARCH_ROOTS = [
    "UltraCanvas/core",
    "UltraCanvas/include",
    "UltraCanvas/dialogs",
    "Apps",
]
SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug"}
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".mm"}

EXEMPT_RE = re.compile(r"//\s*ui-reuse-exempt\s*:\s*(?P<reason>.+)")

# --- hand-rolled-text-field -------------------------------------------------
# An edit buffer of one's own ... (trailing `_` is the member convention in
# some files, so it must not break the match)
BUFFER_RE = re.compile(
    r"\b(?:edit|input|name|text|entry|field)Buffer_?\b"
    r"|\bbuffer(?:Text|Value)_?\b",
    re.IGNORECASE,
)
# ... plus a caret / text cursor / focus flag of one's own ...
CARET_RE = re.compile(
    r"\b\w*[Cc]aret(?:Pos|Position|Index)?_?\b"
    r"|\b\w*(?:[Cc]ursorPos|[Cc]ursorPosition|[Cc]ursorIndex)_?\b"
    r"|\b\w*(?:Focused|HasFocus)_?\b\s*(?:=|;)",
)
# ... plus printable characters harvested from a key handler.
KEYDOWN_RE = re.compile(r"UCEventType::KeyDown")
PRINTABLE_RE = re.compile(
    r"event\.character\s*>=?\s*32"
    r"|event\.text\b"
    r"|UCKeys::Backspace",
)
# The elements that make the pattern legitimate when present.
TEXT_ELEMENT_RE = re.compile(
    r"UltraCanvas(?:TextInput|TextArea|AutoComplete|TagInput|Spinner)\b"
)

# --- hand-rolled-button -----------------------------------------------------
BUTTON_PAINTER_RE = re.compile(
    r"\b(?:void|bool)\s+(?:\w+::)?(Draw\w*Button)\s*\([^;{]*IRenderContext[^;{]*"
    r"\b(?:hover|hovered|isHovered|pressed|isPressed)\b",
    re.DOTALL,
)


class Finding:
    def __init__(self, path: Path, line: int, kind: str, detail: str,
                 symbol: str = ""):
        self.path = path
        self.line = line
        self.kind = kind
        self.detail = detail
        self.symbol = symbol

    @property
    def rel(self) -> str:
        try:
            return self.path.relative_to(REPO_ROOT).as_posix()
        except ValueError:
            return self.path.as_posix()

    @property
    def key(self) -> str:
        """Identity that survives edits elsewhere in the file (no line number)."""
        return f"{self.rel}::{self.kind}::{self.symbol}"

    def __str__(self) -> str:
        return f"{self.rel}:{self.line}: {self.kind}: {self.detail}"


def load_baseline(path: Path) -> set[str]:
    if not path.exists():
        return set()
    keys = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            keys.add(line)
    return keys


BASELINE_HEADER = """\
# Known hand-rolled UI controls, recorded so CI can block *new* ones while
# these are worked off. Each line is <path>::<kind>::<symbol>.
#
# Do not add to this file to silence a new finding — build the UI out of an
# UltraCanvas element instead (AGENTS.md: "Build UI out of UltraCanvas
# elements"). Remove a line when its control is ported to a real element;
# regenerate with:
#     python3 scripts/check_ui_reuse.py --update-baseline
"""


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


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def check_file(path: Path) -> list[Finding]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []

    if EXEMPT_RE.search(text):
        return []

    findings: list[Finding] = []

    buffer_hit = BUFFER_RE.search(text)
    if (
        buffer_hit
        and CARET_RE.search(text)
        and KEYDOWN_RE.search(text)
        and PRINTABLE_RE.search(text)
        and not TEXT_ELEMENT_RE.search(text)
    ):
        findings.append(
            Finding(
                path,
                line_of(text, buffer_hit.start()),
                "hand-rolled-text-field",
                "private edit buffer + caret/focus flag + printable characters "
                "read from KeyDown, with no UltraCanvasTextInput in sight — use "
                "the element (AGENTS.md: Build UI out of UltraCanvas elements)",
            )
        )

    seen_buttons = set()
    for match in BUTTON_PAINTER_RE.finditer(text):
        name = match.group(1)
        if name in seen_buttons:
            continue          # declaration + definition in the same file
        seen_buttons.add(name)
        findings.append(
            Finding(
                path,
                line_of(text, match.start()),
                "hand-rolled-button",
                f"{name}() paints a button with its own hover/pressed "
                "state — use UltraCanvasButton",
                symbol=name,
            )
        )

    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="exit non-zero on findings that are not in the baseline (CI gate)",
    )
    parser.add_argument(
        "--baseline",
        type=Path,
        default=REPO_ROOT / "scripts" / "ui_reuse_baseline.txt",
        help="file of already-known findings (default: scripts/ui_reuse_baseline.txt)",
    )
    parser.add_argument(
        "--no-baseline",
        action="store_true",
        help="report everything, including the known findings",
    )
    parser.add_argument(
        "--update-baseline",
        action="store_true",
        help="rewrite the baseline from the current tree and exit",
    )
    parser.add_argument(
        "paths",
        nargs="*",
        help="files or directories to scan (default: the UI source roots)",
    )
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

    if args.update_baseline:
        keys = sorted({f.key for f in findings})
        args.baseline.write_text(
            BASELINE_HEADER + "\n".join(keys) + ("\n" if keys else ""),
            encoding="utf-8",
        )
        rel = args.baseline.relative_to(REPO_ROOT)
        print(f"check_ui_reuse: wrote {len(keys)} entries to {rel}.")
        return 0

    baseline = set() if args.no_baseline else load_baseline(args.baseline)
    fresh = [f for f in findings if f.key not in baseline]
    known = [f for f in findings if f.key in baseline]

    for finding in fresh:
        print(finding)

    if fresh:
        print(
            f"\n{len(fresh)} new hand-rolled UI control(s) in {scanned} files.\n"
            "Build it from an UltraCanvas element (AGENTS.md: \"Build UI out of "
            "UltraCanvas elements\"), or — if this really is an exception (the "
            "element itself, or a self-rendered view drawing its own content) — "
            "add a marker naming the reason:\n"
            "    // ui-reuse-exempt: <why this one paints directly>"
        )

    if known:
        print(
            f"\n{len(known)} known hand-rolled control(s) still on the baseline "
            f"({args.baseline.name}); porting one to a real element is always "
            "welcome:"
        )
        for finding in known:
            print(f"  {finding.rel}: {finding.kind}")

    # A partial scan cannot tell "fixed" from "not looked at", so stale entries
    # are only reported for a full-tree run.
    stale = [] if args.paths else sorted(baseline - {f.key for f in findings})
    if stale:
        print(
            f"\n{len(stale)} baseline entr{'y is' if len(stale) == 1 else 'ies are'} "
            "no longer found — drop them with --update-baseline:"
        )
        for key in stale:
            print(f"  {key}")

    if fresh:
        return 1 if args.strict else 0

    if not findings:
        print(f"check_ui_reuse: clean ({scanned} files).")
    else:
        print(f"\ncheck_ui_reuse: no new findings ({scanned} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
