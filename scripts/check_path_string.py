#!/usr/bin/env python3
"""Flag std::filesystem::path <-> std::string conversions that use the Windows
code page instead of UTF-8.

UltraCanvas strings are UTF-8. std::filesystem disagrees on Windows: with
libc++ (the MSYS2 CLANG64 / CLANGARM64 toolchain the Windows builds use)

    path.string()             converts to the process's ANSI code page and
                              THROWS - "filesystem error: in __wide_to_char:
                              Illegal byte sequence" - as soon as one
                              character of the name has no equivalent there;
    fs::path(std::string)     decodes the bytes AS that code page, so a UTF-8
                              name silently becomes a different file.

The first one quit UltraFiler on a Thai Windows 10 machine the moment it
opened a folder holding an emoji, CJK or accented file name (0.9.71). The
UTF-8 activeCodePage in the application manifests would hide both, but
Windows before 10 version 1903 ignores it, so nothing may rely on it.

The rule, from AGENTS.md: every conversion between a path and a std::string
goes through PathToUtf8 / PathFromUtf8 (`UltraCanvasPathUtf8.h`, header-only,
C++17, no link dependency), and a UTF-8 name is opened with OpenFileUtf8
rather than std::fopen.

What is reported:

  path-string
      `.string()` or `.generic_string()` - on this codebase that is always a
      std::filesystem::path (std::string has no such member). Use
      PathToUtf8(p).

  path-from-string
      `fs::path(x)` / `std::filesystem::path(x)` with an argument. Use
      PathFromUtf8(x). A construction from a wide string (`wchar_t*`,
      `std::wstring`) or a `std::u8string` is correct as it stands and says so
      at the site:

          return std::filesystem::path(wpath);   // path-string-ok: wide

What is NOT reported, and still wrong: a declaration `fs::path p(str);` and an
implicit conversion `fs::exists(str)`. Neither is distinguishable from a
correct use without the types, so review has to catch those.

A site that is deliberately left alone says why and is skipped:

    return p.string();   // path-string-ok: <why>

`scripts/path_string_baseline.txt` lists sites that are wrong and not fixed
yet, so CI blocks new ones while these are worked off. It should be empty.

Usage:
    python3 scripts/check_path_string.py [--strict] [paths...]
    python3 scripts/check_path_string.py --update-baseline

Exits 0 when clean. Without --strict, findings are reported and the exit code
stays 0.
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
    "UltraCanvas/dialogs",
    "SmartHome",
    "UltraAI",
    "UltraCloud",
    "VirtualFS",
    "Apps",
]
# Tests/ is not scanned: the framework test suite builds on Linux only
# (BUILD_TESTS in build.yml), where a path's native string is the UTF-8 bytes
# and `.string()` is exact. A test that starts running on Windows should be
# added here and converted.
SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug"}
# The implementation of the rule itself.
SKIP_NAMES = {"UltraCanvasPathUtf8.h"}
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".mm"}

EXEMPT_RE = re.compile(r"//.*\bpath-string-ok\b\s*:?\s*(?P<reason>.*)")

TO_STRING_RE = re.compile(r"\.(?P<fn>string|generic_string)\s*\(\s*\)")
FROM_STRING_RE = re.compile(
    r"(?<![\w:])(?P<fn>(?:fs|std::filesystem|filesystem)::path)\s*\(\s*(?!\))")


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
# std::filesystem::path <-> std::string conversions still going through the
# Windows code page, recorded so CI can block *new* ones while these are
# worked off. Each line is <path>::<kind>::<symbol>.
#
# These are debt, not exceptions: on a Windows whose code page lacks a
# character of the file name, `.string()` throws and `fs::path(std::string)`
# names a different file. Convert with PathToUtf8 / PathFromUtf8
# (UltraCanvasPathUtf8.h) and remove the line.
#
# Do not add to this file to silence a new finding. A site that is correct as
# it stands - a path built from a wide string, say - says so where it is:
#     return std::filesystem::path(wpath);   // path-string-ok: wide
#
# Regenerate with:
#     python3 scripts/check_path_string.py --update-baseline
"""


def strip_strings(code: str) -> str:
    """Blank out string and character literals so their text never matches."""
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', code)


def check_file(path: Path) -> list[Finding]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []

    findings: list[Finding] = []
    in_block_comment = False
    for number, line in enumerate(text.splitlines(), start=1):
        if EXEMPT_RE.search(line):
            continue
        code = line
        if in_block_comment:
            end = code.find("*/")
            if end < 0:
                continue
            code = code[end + 2:]
            in_block_comment = False
        code = strip_strings(code)
        code = re.sub(r"/\*.*?\*/", "", code)
        if "/*" in code:
            code = code[:code.index("/*")]
            in_block_comment = True
        code = code.split("//", 1)[0]

        for m in TO_STRING_RE.finditer(code):
            fn = m.group("fn")
            findings.append(Finding(
                path, number, "path-string",
                f".{fn}() converts through the Windows code page and throws "
                f"on a name it cannot hold (\"__wide_to_char: Illegal byte "
                f"sequence\") - use PathToUtf8(p) (UltraCanvasPathUtf8.h)",
                symbol=fn))

        for m in FROM_STRING_RE.finditer(code):
            findings.append(Finding(
                path, number, "path-from-string",
                f"{m.group('fn')}(...) reads a narrow string in the Windows "
                f"code page, not UTF-8 - use PathFromUtf8(s); if the argument "
                f"is wide or char8_t, say so with `// path-string-ok: wide`",
                symbol="path"))

    return findings


def iter_sources(paths: list[Path]):
    for base in paths:
        if base.is_file():
            if base.suffix in SOURCE_SUFFIXES and base.name not in SKIP_NAMES:
                yield base
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            if SKIP_PARTS & set(path.parts) or path.name in SKIP_NAMES:
                continue
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero on findings not in the baseline (CI gate)")
    parser.add_argument("--baseline", type=Path,
                        default=REPO_ROOT / "scripts" / "path_string_baseline.txt",
                        help="file of known sites still to fix")
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
        print(f"check_path_string: wrote {len(keys)} entries to "
              f"{args.baseline.relative_to(REPO_ROOT)}.")
        return 0

    baseline = set() if args.no_baseline else load_baseline(args.baseline)
    fresh = [f for f in findings if f.key not in baseline]

    for finding in fresh:
        print(finding)

    if fresh:
        print(f"\n{len(fresh)} code-page path conversion(s) in {scanned} files.\n"
              "Convert between std::filesystem::path and std::string with "
              "PathToUtf8 / PathFromUtf8 (UltraCanvasPathUtf8.h; AGENTS.md). "
              "If this one is correct as it stands, say why and it will be "
              "skipped:\n"
              "    return std::filesystem::path(wpath);   // path-string-ok: wide")
        return 1 if args.strict else 0

    known = len(findings)
    if known:
        print(f"check_path_string: no new findings ({scanned} files; "
              f"{known} baselined site{'' if known == 1 else 's'} still to fix "
              f"- see {args.baseline.name}).")
    else:
        print(f"check_path_string: clean ({scanned} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
