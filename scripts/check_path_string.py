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

  path-from-string (declarations)
      `fs::path p(str);` / `fs::path p = str;` where `str` is a std::string.

  implicit-path-from-string
      A std::filesystem function given a std::string, which converts it
      implicitly through the code page: `fs::exists(str)`,
      `fs::create_directories(dir)`, `fs::directory_iterator(root)`,
      `fs::rename(a, b)`, ... Pass PathFromUtf8(str).

  stream-from-string
      `std::ifstream in(path)`, `std::ofstream`, `std::fstream`, or
      `.open(path)` with a std::string path - the narrow-string constructor
      opens it in the code page too. Pass PathFromUtf8(path).

The last three need a type the script cannot see, so they are decided by
the argument's nearest declaration: an identifier (or an expression starting
with one, `dir + "/x"`, `name.c_str()`) whose latest declaration above the
use - in the function, the file, or for a `member_` the matching header - is
a `std::string`. An argument whose type is not written down (`auto`, a
function's result) is not reported; review still has to catch those.

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

# `fs::path p(arg)` / `fs::path p{arg}` / `fs::path p = arg` - a declaration.
PATH_DECL_RE = re.compile(
    r"(?<![\w:])(?:fs|std::filesystem|filesystem)::path\s+(?:const\s+)?"
    r"(?P<name>\w+)\s*(?P<open>[({=])")

# std::filesystem functions that take a path; every argument is checked.
FS_FUNCTIONS = (
    "exists|is_directory|is_regular_file|is_symlink|is_empty|is_other|"
    "create_directory|create_directories|remove|remove_all|rename|copy|"
    "copy_file|file_size|last_write_time|resize_file|status|symlink_status|"
    "weakly_canonical|canonical|absolute|relative|proximate|equivalent|space|"
    "directory_iterator|recursive_directory_iterator|permissions|"
    "create_symlink|create_directory_symlink|create_hard_link|read_symlink")
FS_CALL_RE = re.compile(
    r"(?<![\w:])(?:fs|std::filesystem|filesystem)::(?P<fn>" + FS_FUNCTIONS +
    r")\s*\(")

# std::ifstream in(arg) / std::ofstream{arg} / std::fstream f(arg), and
# stream.open(arg).
STREAM_CTOR_RE = re.compile(
    r"(?<![\w:])(?:std::)?(?P<fn>[io]?fstream)\s+\w+\s*[({]")
STREAM_OPEN_RE = re.compile(r"\.\s*open\s*\(")

# A declaration of a name, with the type written before it.
DECL_TYPES = {
    "string": r"std::string|string",
    "path": r"(?:std::filesystem|fs|filesystem)::path",
    "other": r"auto|std::wstring|std::u8string|const\s+char\s*\*|char\s*\*|"
             r"const\s+wchar_t\s*\*|wchar_t\s*\*|std::string_view",
}


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
# Most entries are the kinds added in 2026-10 - implicit-path-from-string
# (`fs::exists(str)`, `fs::create_directories(dir)`, ...), stream-from-string
# (`std::ifstream in(path)`) and the `fs::path p(str);` declarations - which
# the check could not see before, so they had accumulated across the tree.
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


def split_args(text: str, start: int) -> list[str]:
    """The top-level arguments of the call whose '(' / '{' is at text[start-1]."""
    depth, args, current = 0, [], []
    i = start
    while i < len(text):
        c = text[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                args.append("".join(current).strip())
                return args
            depth -= 1
        elif c == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
            i += 1
            continue
        elif c == ";" and depth == 0:
            break
        current.append(c)
        i += 1
    args.append("".join(current).strip())
    return args


LEADING_ID_RE = re.compile(r"^\s*(?:\*\s*)?(?P<id>[A-Za-z_]\w*)(?P<rest>.*)$", re.S)
# Wrapping the argument in one of these makes it a correct path already.
CONVERTERS = re.compile(r"\b(?:PathFromUtf8|OpenFileUtf8|u8path|std::filesystem::path|"
                        r"fs::path|filesystem::path)\b")


DECL_RES = [
    (kind, re.compile(r"(?<![\w:])(?:const\s+)?(?:" + pattern +
                      r")\s*(?:const\s*)?[&*]?\s*(?P<name>[A-Za-z_]\w*)\s*"
                      r"(?=[;,)=({\[]|$)"))
    for kind, pattern in DECL_TYPES.items()
]
# A line can only declare one of those if it names one of the types.
DECL_HINT = re.compile(r"string|path|auto|char|wchar_t")


class TypeIndex:
    """Where each name is declared, and as what, in a file (and its header)."""

    def __init__(self, code_lines: list[str], header_lines: list[str]):
        self.local = self._index(code_lines)
        self.header = self._index(header_lines)

    @staticmethod
    def _index(lines: list[str]) -> dict[str, list[tuple[int, str]]]:
        index: dict[str, list[tuple[int, str]]] = {}
        for number, line in enumerate(lines, start=1):
            if not DECL_HINT.search(line):
                continue
            for kind, rx in DECL_RES:
                for m in rx.finditer(line):
                    index.setdefault(m.group("name"), []).append((number, kind))
        for decls in index.values():
            decls.sort()
        return index

    def kind_of(self, name: str, line: int) -> str | None:
        """The type kind of `name`'s latest declaration at or above `line`."""
        best = None
        for number, kind in self.local.get(name, []):
            if number <= line:
                best = kind
        if best is None and name.endswith("_"):
            decls = self.header.get(name, [])
            if decls:
                best = decls[-1][1]
        return best


def string_argument(arg: str, line: int, types: TypeIndex) -> str | None:
    """The std::string name an argument is built on, or None."""
    if not arg or CONVERTERS.search(arg):
        return None
    m = LEADING_ID_RE.match(arg)
    if not m:
        return None
    name = m.group("id")
    if name in {"std", "fs", "filesystem", "this", "nullptr", "true", "false"}:
        return None
    rest = m.group("rest").strip()
    # `name`, `name.c_str()`, `name + "..."` - a member access other than
    # c_str()/data() reaches some other object, whose type is unknown.
    if rest and not (rest.startswith("+") or re.match(r"^\.\s*(c_str|data)\s*\(\s*\)", rest)):
        return None
    return name if types.kind_of(name, line) == "string" else None


def clean_lines(text: str) -> list[str]:
    """Each line with literals blanked and comments removed (block comments too)."""
    out, in_block = [], False
    for line in text.splitlines():
        code = line
        if in_block:
            end = code.find("*/")
            if end < 0:
                out.append("")
                continue
            code = code[end + 2:]
            in_block = False
        code = strip_strings(code)
        code = re.sub(r"/\*.*?\*/", "", code)
        if "/*" in code:
            code = code[:code.index("/*")]
            in_block = True
        out.append(code.split("//", 1)[0])
    return out


def paired_header(path: Path) -> Path | None:
    if path.suffix not in {".cpp", ".mm"}:
        return None
    for suffix in (".h", ".hpp"):
        candidate = path.with_suffix(suffix)
        if candidate.exists():
            return candidate
    # include/ and core/ are split in UltraCanvas: try the include directory.
    parts = list(path.parts)
    if "core" in parts:
        i = parts.index("core")
        alt = Path(*parts[:i], "include", *parts[i + 1:]).with_suffix(".h")
        if alt.exists():
            return alt
    return None


def check_file(path: Path) -> list[Finding]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []

    raw_lines = text.splitlines()
    code_lines = clean_lines(text)
    header = paired_header(path)
    header_lines: list[str] = []
    if header is not None:
        try:
            header_lines = clean_lines(header.read_text(encoding="utf-8", errors="replace"))
        except OSError:
            header_lines = []
    types = TypeIndex(code_lines, header_lines)
    # The whole file as one string, for arguments that span lines.
    joined = "\n".join(code_lines)
    line_starts = [0]
    for line in code_lines:
        line_starts.append(line_starts[-1] + len(line) + 1)

    findings: list[Finding] = []
    for number, line in enumerate(raw_lines, start=1):
        code = code_lines[number - 1]
        if not code.strip() or EXEMPT_RE.search(line):
            continue

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

        # The typed checks read their arguments from the whole file, so a
        # call that wraps onto the next line is seen.
        cleaned = code
        base = line_starts[number - 1]

        for m in PATH_DECL_RE.finditer(cleaned):
            start = base + m.end()
            if m.group("open") == "=":
                end = joined.find(";", start)
                args = [joined[start:end if end >= 0 else len(joined)].strip()]
            else:
                args = split_args(joined, start)
            if len(args) != 1:
                continue
            name = string_argument(args[0], number, types)
            if name:
                findings.append(Finding(
                    path, number, "path-from-string",
                    f"fs::path {m.group('name')} from std::string '{name}' reads "
                    f"it in the Windows code page - use PathFromUtf8({name})",
                    symbol=f"decl:{name}"))

        for m in FS_CALL_RE.finditer(cleaned):
            for arg in split_args(joined, base + m.end()):
                name = string_argument(arg, number, types)
                if name:
                    findings.append(Finding(
                        path, number, "implicit-path-from-string",
                        f"fs::{m.group('fn')}({name}) converts std::string "
                        f"'{name}' to a path through the Windows code page - "
                        f"pass PathFromUtf8({name})",
                        symbol=f"{m.group('fn')}:{name}"))

        for rx, label in ((STREAM_CTOR_RE, None), (STREAM_OPEN_RE, "open")):
            for m in rx.finditer(cleaned):
                args = split_args(joined, base + m.end())
                if not args:
                    continue
                name = string_argument(args[0], number, types)
                if name:
                    fn = label or m.group("fn")
                    findings.append(Finding(
                        path, number, "stream-from-string",
                        f"{fn}({name}) opens std::string '{name}' in the Windows "
                        f"code page - pass PathFromUtf8({name})",
                        symbol=f"{fn}:{name}"))

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
