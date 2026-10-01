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

  path-implicit
      A UTF-8 std::string handed straight to something that takes a path, so
      the string is converted implicitly - through the code page:
          fs::exists(str)  fs::remove(str, ec)  fs::directory_iterator(str)
          std::ifstream f(str)  f.open(str)  fs::path p = str;
      Use PathFromUtf8(str). The argument counts as a string when it is
      `.c_str()`, a `+` concatenation with a literal or a string, or a name
      the file declares as std::string - the check has no types, so it reads
      the declarations of the same file. (PathFromUtf8 also takes a path, a C
      string and a string_view, so wrapping is never wrong.)

  fopen-narrow
      `fopen(name, mode)` with anything but a literal: the narrow fopen reads
      the name in the Windows code page. Use OpenFileUtf8(name, mode).

What is still NOT reported: a string whose type the file does not spell out
(an `auto`, a getter's return value) handed to a path parameter, and the
declaration form `fs::path p(str);` with such a string. Review catches those.
The two implicit kinds skip the Linux, macOS, Android, WASM and ULTRA OS
platform folders, where a path's native string is the UTF-8 bytes.

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
    "UltraNet",
    "VideoFX",
    "Apps",
]
# Tests/ is not scanned: the framework test suite builds on Linux only
# (BUILD_TESTS in build.yml), where a path's native string is the UTF-8 bytes
# and `.string()` is exact. A test that starts running on Windows should be
# added here and converted.
SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug"}
# The implementation of the rule itself.
SKIP_NAMES = {"UltraCanvasPathUtf8.h",
              # Vendored single-header libraries.
              "miniaudio.h", "qoi.h"}
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".mm"}

EXEMPT_RE = re.compile(r"//.*\bpath-string-ok\b\s*:?\s*(?P<reason>.*)")

TO_STRING_RE = re.compile(r"\.(?P<fn>string|generic_string)\s*\(\s*\)")
FROM_STRING_RE = re.compile(
    r"(?<![\w:])(?P<fn>(?:fs|std::filesystem|filesystem)::path)\s*\(\s*(?!\))")

# ---- path-implicit / fopen-narrow -------------------------------------------
# Platform folders whose path is the UTF-8 bytes: implicit conversions there
# are exact, so the two implicit kinds leave them alone.
UTF8_NATIVE_OS = {"Linux", "MacOS", "Android", "WASM", "Wasm", "UltraOS"}
FS_PATH_FUNCS = set("""exists is_directory is_regular_file is_symlink is_empty
    file_size last_write_time remove remove_all create_directory
    create_directories rename copy copy_file directory_iterator
    recursive_directory_iterator status symlink_status canonical
    weakly_canonical absolute space permissions resize_file equivalent
    read_symlink hard_link_count create_symlink create_directory_symlink
    create_hard_link relative proximate is_other is_fifo is_socket
    is_block_file is_character_file""".split())
FS_TWO_PATHS = {"copy", "copy_file", "rename", "create_symlink",
                "create_directory_symlink", "create_hard_link", "equivalent",
                "relative", "proximate"}
STRING_DECL_RE = re.compile(
    r"\b(?:const\s+)?(?:std::)?string\s*[&*]?\s*(\w+)\s*(?=[;=,\)\{\(\[])")
STREAM_DECL_RE = re.compile(r"\bstd::(?:i|o)?fstream\s+(\w+)")
FS_CALL_RE = re.compile(
    r"(?<![\w:])(?:std::filesystem|fs|filesystem)::(\w+)\s*(\w+\s*)?([\({])")
STREAM_CTOR_RE = re.compile(r"\bstd::(?:i|o)?fstream\s*(\w+\s*)?([\({])")
PATH_ASSIGN_RE = re.compile(
    r"(?<![\w:])(?:std::filesystem|fs|filesystem)::path\s+\w+\s*=\s*([^;]+);")
FOPEN_RE = re.compile(r"(?<![\w.>])(?:std::|::)?fopen\s*\(")
ID_CHAIN_RE = re.compile(r"^[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*$")


def _close_of(code: str, i: int) -> int:
    """Index of the bracket closing the one at code[i], or -1."""
    depth = 0
    for j in range(i, len(code)):
        if code[j] in "([{":
            depth += 1
        elif code[j] in ")]}":
            depth -= 1
            if depth == 0:
                return j
    return -1


def _split_args(text: str) -> list[str]:
    args, depth, cur = [], 0, ""
    for c in text:
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            args.append(cur)
            cur = ""
        else:
            cur += c
    args.append(cur)
    return args


def _top_level(text: str, ch: str) -> bool:
    depth = 0
    for c in text:
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == ch and depth == 0:
            return True
    return False


def _is_utf8_string(arg: str, strings: set[str], raw: str) -> bool:
    """Whether `arg` (string literals blanked) is a narrow UTF-8 string.
    `raw` is the same text with the literals kept, to tell L"" from ""."""
    a = arg.strip()
    if not a or "PathFromUtf8" in a or raw.strip().startswith(("L\"", "u8\"", "u\"", "U\"")):
        return False
    if a.endswith(".c_str()"):
        return True
    if ID_CHAIN_RE.match(a):
        return re.split(r"\.|->", a)[-1] in strings
    if a.startswith(("std::string(", "PathToUtf8(")):
        return True
    if _top_level(a, "+") and not _top_level(a, "/"):
        for op in (o.strip() for o in a.split("+")):
            if op.startswith('"'):
                return True
            if ID_CHAIN_RE.match(op) and re.split(r"\.|->", op)[-1] in strings:
                return True
    return False


def implicit_findings(path: Path, number: int, code: str, raw: str,
                      strings: set[str], streams: set[str]) -> list[Finding]:
    found: list[Finding] = []

    def check_args(open_at: int, which, what: str):
        close = _close_of(code, open_at)
        if close < 0:
            return
        args = _split_args(code[open_at + 1:close])
        raws = _split_args(raw[open_at + 1:close]) if len(raw) == len(code) else args
        for k in which(len(args)):
            if _is_utf8_string(args[k], strings, raws[k] if k < len(raws) else args[k]):
                found.append(Finding(
                    path, number, "path-implicit",
                    f"{what} takes a path, and a UTF-8 std::string converts to "
                    f"one through the Windows code page - wrap it: "
                    f"PathFromUtf8({args[k].strip()})",
                    symbol=what))

    for m in FS_CALL_RE.finditer(code):
        fn, var = m.group(1), m.group(2)
        if fn in FS_PATH_FUNCS and not var:
            n = 2 if fn in FS_TWO_PATHS else 1
            check_args(m.end() - 1, lambda c, n=n: range(min(c, n)), f"fs::{fn}")
        elif fn in ("path", "directory_iterator", "recursive_directory_iterator") and var:
            check_args(m.end() - 1, lambda c: range(min(c, 1)), f"fs::{fn}")
    for m in STREAM_CTOR_RE.finditer(code):
        check_args(m.end() - 1, lambda c: range(min(c, 1)), "fstream")
    for name in streams:
        for m in re.finditer(r"\b" + re.escape(name) + r"\s*(?:\.|->)\s*open\s*\(", code):
            check_args(m.end() - 1, lambda c: range(min(c, 1)), "fstream::open")
    m = PATH_ASSIGN_RE.search(code)
    if m and _is_utf8_string(m.group(1), strings, m.group(1)):
        found.append(Finding(
            path, number, "path-implicit",
            "fs::path = <UTF-8 std::string> converts through the Windows code "
            "page - use PathFromUtf8", symbol="path="))
    for m in FOPEN_RE.finditer(code):
        close = _close_of(code, m.end() - 1)
        if close < 0:
            continue
        args = _split_args(code[m.end():close])
        if len(args) == 2 and not args[0].strip().startswith('"'):
            found.append(Finding(
                path, number, "fopen-narrow",
                "fopen reads the name in the Windows code page - use "
                "OpenFileUtf8(name, mode) (UltraCanvasPathUtf8.h)",
                symbol="fopen"))
    return found


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
    implicit = not (UTF8_NATIVE_OS & set(path.parts))
    strings = set(STRING_DECL_RE.findall(text)) if implicit else set()
    streams = set(STREAM_DECL_RE.findall(text)) if implicit else set()
    in_block_comment = False
    for number, line in enumerate(text.splitlines(), start=1):
        if EXEMPT_RE.search(line):
            continue
        code = line
        raw_line = line
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

        if implicit:
            raw = raw_line.split("//", 1)[0]
            findings.extend(implicit_findings(path, number, code, raw
                                              if len(raw) == len(code) else code,
                                              strings, streams))

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
