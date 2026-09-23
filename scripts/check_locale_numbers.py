#!/usr/bin/env python3
"""Flag numbers read from, or written to, a file format through LC_NUMERIC.

`std::stof`, `atof`, `std::to_string(double)` and `snprintf("%g")` all go
through the C locale's decimal point. Every comma-decimal desktop — de_DE,
fr_FR, ru_RU, pt_BR and the rest — stops dead at a '.' when reading and emits
a ',' when writing, and the Linux backend calls `setlocale(LC_ALL, "")` for
XIM, so this is the default state of the framework on half of Europe.

It is not a crash. It is a *different file*:

    opacity="0.25"   read as 0      -> the shape renders invisible
    stroke-width="1.5" read as 1    -> a different drawing
    M 1.5 2          written as `M 1,5 2`  -> reads back as the point (1, 5)
    rgba(…, 0.5)     written as `rgba(…, 0,500000)` -> a 5-argument function
    GDK_SCALE=1.5    read as 1      -> the HiDPI override does nothing

which is why it survived three separate fixes (CSS, SVG, and the 0.9.42
sweep) and kept turning up somewhere else.

The rule, from AGENTS.md: numbers in file formats and wire protocols are
dot-decimal, always. Use `ParseFloatClassic` / `TryParseFloat` to read them
and `FormatFloatClassic` to write them (`UltraCanvasTextUtils.h`).

**Text a person typed or reads is the opposite case** and this check must not
push anyone to "fix" it: a German user types `1,5` into a spinner and expects
`12,5 %` on a chart label. Those sites say so where they are, with a
`// locale-ok: <why>` comment, and never reach the baseline.

`scripts/locale_numbers_baseline.txt` is the other thing: format and protocol
sites that really are wrong and are not fixed yet, recorded so CI can block
new ones while these are worked off. It should trend to empty.

What is reported:

  locale-parse
      std::stof / std::stod / atof / strtod anywhere in the scanned roots.
      Reading is always suspect: even user input is better read with
      TryParseFloat plus an explicit locale step, because stof also throws.

  locale-write
      In a file that writes a format — Storage / Writer / Export / FileIO /
      Serializer, or anything under DataFormats/ or Plugins/Vector/ — a
      std::to_string of something that looks like a float, a printf-family
      %f/%g/%e, or an ostringstream that is never imbued with
      std::locale::classic(). The unimbued stream is the one that hid
      SerializePathData's `M 1,5 2` for three releases.

      The stream rule only looks at files that mention std::locale::classic
      NOWHERE. A writer that has the idiom is trusted with it: the SVG
      converter streams every number through its own imbued `Num()`, so its
      streams are correct and flagging them would point at the reference
      implementation of the fix. This check is for code that has never heard
      of the problem, which is what VectorStorage was before 0.9.42.

A site that is deliberately locale-aware says so and is skipped:

    std::stod(text);   // locale-ok: the user typed this into the cell

Usage:
    python3 scripts/check_locale_numbers.py [--strict] [paths...]
    python3 scripts/check_locale_numbers.py --update-baseline

Exits 0 when clean. Without --strict, findings are reported and the exit code
stays 0, so the check can be introduced without blocking unrelated work.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

SEARCH_ROOTS = [
    "UltraCanvas/core",
    "UltraCanvas/include",
    "UltraCanvas/Plugins",
    "UltraCanvas/OS",
    "UltraCanvas/libspecific",
    "SmartHome",
    "UltraAI",
    "UltraCloud",
    "VirtualFS",
    "Apps",
]
SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug"}
# Vendored single-file libraries that live outside third_party/.
SKIP_NAMES = {"miniaudio.h", "qoi.h", "qoi.cpp"}
SKIP_DIR_FRAGMENTS = ("/libspecific/FFT/",)
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".mm"}

EXEMPT_RE = re.compile(r"//.*\blocale-ok\b\s*:?\s*(?P<reason>.*)")

# --- reading ---------------------------------------------------------------
PARSE_RE = re.compile(
    r"(?<![\w:.>])(?:std::)?(?P<fn>stof|stod|strtod|strtof|atof)\s*\("
)

# --- writing, in files that write a format ---------------------------------
WRITER_NAME_RE = re.compile(r"(Storage|Writer|Export|FileIO|Serializ|Converter)",
                            re.IGNORECASE)
WRITER_DIR_RE = re.compile(r"/(DataFormats|Vector)/")

PRINTF_FLOAT_RE = re.compile(
    r"\b(?:sn|s)?printf\s*\([^;]*?%[-+ #0-9.*]*[fgeFGE]\b")
# to_string of something that is syntactically a float: a decimal literal, an
# `f` suffix, or an explicit float/double cast in the argument.
TO_STRING_FLOAT_RE = re.compile(
    r"\bto_string\s*\((?P<arg>[^();]*(?:\([^()]*\)[^();]*)*)\)")
FLOATISH_RE = re.compile(r"\d*\.\d|\b\d+\.?\d*f\b|\b(?:float|double)\b")
STREAM_DECL_RE = re.compile(r"\b(?:std::)?(?:o|i)?stringstream\s+(?P<name>\w+)\s*[;(]")
IMBUE_RE = re.compile(r"\.imbue\s*\(\s*std::locale::classic\s*\(\s*\)\s*\)")
STREAM_FLOAT_RE = re.compile(r"<<\s*[^;]*\b(?:\d*\.\d|[a-zA-Z_]\w*\.(?:x|y|X|Y)\b)")


def is_writer(relative: str) -> bool:
    return bool(WRITER_NAME_RE.search(Path(relative).name)
                or WRITER_DIR_RE.search("/" + relative))


class Finding:
    def __init__(self, path: Path, line: int, kind: str, detail: str, symbol: str):
        self.path, self.line, self.kind = path, line, kind
        self.detail, self.symbol = detail, symbol

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
# File-format and protocol numbers still read or written through LC_NUMERIC,
# recorded so CI can block *new* ones while these are worked off.
# Each line is <path>::<kind>::<symbol>.
#
# These are debt, not exceptions. Every one is a number crossing into a file
# format, a document, a /proc file or a wire protocol, where the decimal point
# is a '.' by definition - so on a comma-decimal desktop each is a misread
# value or a corrupted file waiting to be reported. They are listed rather
# than fixed only because 0.9.42 swept the readers it could reach and this
# check then found the rest.
#
# Do not add to this file to silence a new finding. Read with
# TryParseFloat / ParseFloatClassic and write with FormatFloatClassic
# (UltraCanvasTextUtils.h). Remove a line when its site is fixed.
#
# A site that is genuinely locale-aware - text a person typed, or a label a
# person reads - does NOT belong here either. Say so at the site and the check
# skips it without a baseline entry:
#     std::stod(text);   // locale-ok: the user typed this into the cell
#
# Regenerate with:
#     python3 scripts/check_locale_numbers.py --update-baseline
"""


def strip_line_comment_keep(text: str) -> str:
    return text


def check_file(path: Path) -> list[Finding]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []

    relative = path.relative_to(REPO_ROOT).as_posix()
    lines = text.splitlines()
    findings: list[Finding] = []

    for number, line in enumerate(lines, start=1):
        if EXEMPT_RE.search(line):
            continue
        code = line.split("//", 1)[0]

        for m in PARSE_RE.finditer(code):
            fn = m.group("fn")
            findings.append(Finding(
                path, number, "locale-parse",
                f"{fn}() reads through LC_NUMERIC, so \"1.5\" is 1 on a "
                f"comma-decimal desktop — use TryParseFloat / ParseFloatClassic "
                f"(UltraCanvasTextUtils.h), or say why this one follows the "
                f"reader's locale with `// locale-ok: …`",
                symbol=fn))

        if not is_writer(relative):
            continue

        if PRINTF_FLOAT_RE.search(code):
            findings.append(Finding(
                path, number, "locale-write",
                "a printf-family %f/%g/%e in a format writer renders through "
                "LC_NUMERIC — use FormatFloatClassic",
                symbol="printf"))

        for m in TO_STRING_FLOAT_RE.finditer(code):
            if FLOATISH_RE.search(m.group("arg")):
                findings.append(Finding(
                    path, number, "locale-write",
                    "std::to_string of a float in a format writer renders "
                    "through LC_NUMERIC (1.5 becomes \"1,500000\") — use "
                    "FormatFloatClassic",
                    symbol="to_string"))

    # A stream in a writer that is never imbued, and does receive a number.
    # Skipped entirely for a file that knows the idiom somewhere - it either
    # imbues its streams or funnels them through a helper that does, and
    # neither is distinguishable from the outside by looking at the `<<`.
    if is_writer(relative) and "locale::classic" not in text:
        for m in STREAM_DECL_RE.finditer(text):
            name = m.group("name")
            after = text[m.end():m.end() + 4000]
            uses = re.search(rf"\b{re.escape(name)}\s*<<", after)
            imbued = re.search(rf"\b{re.escape(name)}{IMBUE_RE.pattern}", after)
            if uses and not imbued and STREAM_FLOAT_RE.search(after):
                findings.append(Finding(
                    path, text.count("\n", 0, m.start()) + 1, "locale-write",
                    f"`{name}` is never imbued with std::locale::classic(), so "
                    f"every number it writes goes through LC_NUMERIC — this is "
                    f"how `M 1.5 2` became `M 1,5 2`, which reads back as the "
                    f"point (1, 5)",
                    symbol=name))

    return findings


def iter_sources(paths: list[Path]):
    for base in paths:
        if base.is_file():
            if base.suffix in SOURCE_SUFFIXES:
                yield base
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            if SKIP_PARTS & set(path.parts) or path.name in SKIP_NAMES:
                continue
            posix = "/" + path.relative_to(REPO_ROOT).as_posix()
            if any(frag in posix for frag in SKIP_DIR_FRAGMENTS):
                continue
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero on findings not in the baseline (CI gate)")
    parser.add_argument("--baseline", type=Path,
                        default=REPO_ROOT / "scripts" / "locale_numbers_baseline.txt",
                        help="file of sites that follow the locale on purpose")
    parser.add_argument("--no-baseline", action="store_true",
                        help="report everything, including the known sites")
    parser.add_argument("--update-baseline", action="store_true",
                        help="rewrite the baseline from the current tree and exit")
    parser.add_argument("paths", nargs="*",
                        help="files or directories to scan (default: the source roots)")
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
            encoding="utf-8")
        print(f"check_locale_numbers: wrote {len(keys)} entries to "
              f"{args.baseline.relative_to(REPO_ROOT)}.")
        return 0

    baseline = set() if args.no_baseline else load_baseline(args.baseline)
    fresh = [f for f in findings if f.key not in baseline]

    for finding in fresh:
        print(finding)

    if fresh:
        print(f"\n{len(fresh)} locale-dependent number(s) in {scanned} files.\n"
              "Numbers in a file format or a wire protocol are dot-decimal "
              "(AGENTS.md): read them with TryParseFloat / ParseFloatClassic "
              "and write them with FormatFloatClassic. If this one really is "
              "text a person typed or reads, say so and it will be skipped:\n"
              "    std::stod(text);   // locale-ok: the user typed this")
        return 1 if args.strict else 0

    known = len(findings)
    if known:
        print(f"check_locale_numbers: no new findings ({scanned} files; "
              f"{known} baselined site{'' if known == 1 else 's'} still to fix "
              f"- see {args.baseline.name}).")
    else:
        print(f"check_locale_numbers: clean ({scanned} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
