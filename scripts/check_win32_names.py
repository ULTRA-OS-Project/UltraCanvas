#!/usr/bin/env python3
"""Flag functions of our own whose name <windows.h> turns into another name.

<windows.h> #defines a few thousand Win32 names to their A or W variant:

    #define CreateFile __MINGW_NAME_AW(CreateFile)     ->  CreateFileW

A macro does not know about scopes, so it renames *our* functions of that name
too - but only in the translation units that include <windows.h>. A method
declared as CreateFile in a header is CreateFile in the .cpp that defines it
and CreateFileW in a .cpp that happens to see windows.h, and the Windows link
fails with an undefined symbol that no other platform reproduces:

    ld.lld: error: undefined symbol: UltraCanvasPdfSurface::CreateFileW(...)

That is how UltraCanvasPdfSurface::CreateFile broke both Windows builds while
Linux and macOS were green. The rule: a function, method or member of ours is
never named like a Win32 A/W macro. Pick another name (CreateForFile,
DrawTextRun, LoadImageFile, ...).

What is reported (in code, not in comments or string literals):

  win32-name-qualified
      `Name::Func(` - a qualified definition or call, `X::DrawText(`.
  win32-name-member
      `.Func(` or `->Func(` - calling a member of that name.
  win32-name-declaration
      `<type> Func(` - declaring or defining a function of that name, e.g.
      `bool DrawText(` or `static std::unique_ptr<X> CreateFile(`.

What is not reported:

  - an unqualified call such as `h = CreateFile(...)` - that is the real Win32
    function;
  - anything under UltraCanvas/OS/MSWindows, where every file includes
    <windows.h>, so the macro applies consistently and calls to Win32 and COM
    methods are meant;
  - a name the Windows platform headers undo right after including
    <windows.h> (`#undef DrawText` in UltraCanvasWindowsApplication.h,
    UltraCanvasWindowsWindow.h, ...). That is how IRenderContext::DrawText
    survives; the list is read from those headers, so adding an #undef there
    is how a name is made safe for the whole framework;
  - a name the file itself #undefs (WebDavPlugin.cpp does CreateDirectory).

A site that is deliberately left alone says why and is skipped:

    hr = dialog->GetResults(&item);   // win32-name-ok: COM method, windows.h included

`scripts/win32_names_baseline.txt` lists sites that predate the check, so CI
blocks new ones while these are worked off. The names come from
`scripts/win32_aw_macros.txt`, generated from the MinGW-w64 headers.

Usage:
    python3 scripts/check_win32_names.py [--strict] [paths...]
    python3 scripts/check_win32_names.py --update-baseline
    python3 scripts/check_win32_names.py --generate-names <mingw-w64 include dir>

Exits 0 when clean. Without --strict, findings are reported and the exit code
stays 0.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
NAMES_FILE = REPO_ROOT / "scripts" / "win32_aw_macros.txt"

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
    "Tests",
]
SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug"}
# Every file in MSWindows includes <windows.h>: the macro applies everywhere,
# and a `->GetObject(` is the Win32/COM method it names. The other platform
# directories never compile on Windows, so windows.h never reaches them.
SKIP_DIRS = [Path("UltraCanvas/OS") / d for d in
             ("MSWindows", "Linux", "MacOS", "BSD", "WASM", "Android")]
# Vendored single-file libraries, not ours to rename.
SKIP_NAMES = {"miniaudio.h"}
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".mm", ".c"}

# Headers whose `#undef Name` lines make a name safe everywhere.
UNDEF_HEADERS_DIR = Path("UltraCanvas/OS/MSWindows")
UNDEF_RE = re.compile(r"^\s*#\s*undef\s+([A-Za-z_]\w*)", re.MULTILINE)

EXEMPT_RE = re.compile(r"//.*\bwin32-name-ok\b\s*:?\s*(?P<reason>.*)")

# Words that may stand before a call without making it a declaration.
NOT_A_TYPE = {"return", "co_return", "co_yield", "throw", "else", "case", "new",
              "delete", "do", "sizeof", "decltype", "and", "or", "not"}

NAMES_HEADER = """\
# Function-like names that <windows.h> (and the headers it pulls in) #define
# to their A or W variant: `#define CreateFile __MINGW_NAME_AW(CreateFile)`
# expands to CreateFileW in a UNICODE build. Read by
# scripts/check_win32_names.py.
#
# Generated from the MinGW-w64 headers (the toolchain behind the MSYS2
# CLANG64 / CLANGARM64 Windows builds); driver-kit-only (ddk/) names left out.
# Regenerate with:
#     python3 scripts/check_win32_names.py --generate-names <mingw-w64 include dir>
"""

BASELINE_HEADER = """\
# Functions of ours named like a Win32 A/W macro (CreateFile, DrawText, ...),
# recorded so CI can block *new* ones while these are worked off. Each line is
# <path>::<kind>::<name>.
#
# These are debt, not exceptions: in a translation unit that includes
# <windows.h> the name becomes NameW, and the Windows link fails as soon as the
# declaring and the calling file disagree. Rename the function and remove the
# line.
#
# Do not add to this file to silence a new finding. A site that is correct as
# it stands says so where it is:
#     hr = item->GetDisplayName(&name);   // win32-name-ok: <why>
#
# Regenerate with:
#     python3 scripts/check_win32_names.py --update-baseline
"""


class Finding:
    def __init__(self, path: Path, line: int, kind: str, name: str):
        self.path, self.line, self.kind, self.name = path, line, kind, name

    @property
    def rel(self) -> str:
        try:
            return self.path.relative_to(REPO_ROOT).as_posix()
        except ValueError:
            return self.path.as_posix()

    @property
    def key(self) -> str:
        """Identity that survives edits elsewhere in the file (no line number)."""
        return f"{self.rel}::{self.kind}::{self.name}"

    def __str__(self) -> str:
        return (f"{self.rel}:{self.line}: {self.kind}: {self.name} is a "
                f"<windows.h> macro ({self.name}W in a file that includes it) "
                f"- rename the function")


def load_lines(path: Path) -> set[str]:
    if not path.exists():
        return set()
    out = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            out.add(line)
    return out


def generate_names(include_dir: Path) -> list[str]:
    aw = re.compile(r"^#\s*define\s+([A-Za-z_]\w*)\s+__MINGW_NAME_U?AW\(\s*\1\s*\)")
    alias = re.compile(r"^#\s*define\s+([A-Za-z_]\w*)\s+\1W\s*$")
    found: dict[str, set[str]] = {}
    for root, _, files in os.walk(include_dir):
        for name in files:
            if not name.endswith(".h"):
                continue
            path = Path(root) / name
            rel = path.relative_to(include_dir).as_posix()
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                m = aw.match(line) or alias.match(line)
                if m:
                    found.setdefault(m.group(1), set()).add(rel)
    return sorted(n for n, headers in found.items()
                  if not all(h.startswith("ddk/") for h in headers))


def undone_names() -> set[str]:
    """Names the Windows platform headers #undef after including windows.h."""
    out: set[str] = set()
    for header in sorted((REPO_ROOT / UNDEF_HEADERS_DIR).glob("*.h")):
        out |= set(UNDEF_RE.findall(header.read_text(encoding="utf-8", errors="replace")))
    return out


def strip_strings(code: str) -> str:
    """Blank out string and character literals so their text never matches."""
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', code)


CALL_RE = re.compile(r"(?P<before>(?:\w+\s*::\s*|::\s*|\.\s*|->\s*)?)"
                     r"(?<![\w])(?P<name>[A-Za-z_]\w*)\s*\(")


def classify(code: str, m: re.Match) -> str | None:
    before = m.group("before")
    if before:
        stripped = before.replace(" ", "")
        if stripped == "::":
            return None          # ::CreateFile( - the global Win32 function
        if stripped.endswith("::"):
            return "win32-name-qualified"
        return "win32-name-member"
    prefix = code[:m.start("name")].rstrip()
    if not prefix:
        return None
    last = prefix[-1]
    if last == ">":
        # `std::unique_ptr<X> Name(` closes a template; `a > Name(` and
        # `p->Name(` (handled above) do not.
        if prefix.endswith("->") or prefix.count("<") < prefix.count(">"):
            return None
        return "win32-name-declaration"
    if last in "*&":
        # `X* Name(` / `const X& Name(`; not `a && Name(` or `x = &Name(`.
        if prefix.endswith(("&&", "||")) or re.search(r"[=!<>(,]\s*[*&]$", prefix):
            return None
        return "win32-name-declaration"
    word = re.search(r"(\w+)$", prefix)
    if word and word.group(1) not in NOT_A_TYPE and not word.group(1).isdigit():
        return "win32-name-declaration"
    return None


def check_file(path: Path, names: set[str]) -> list[Finding]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []

    names = names - set(UNDEF_RE.findall(text))
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
        if code.lstrip().startswith("#"):
            continue             # a preprocessor line, e.g. `#define DrawText ...`

        for m in CALL_RE.finditer(code):
            if m.group("name") not in names:
                continue
            kind = classify(code, m)
            if kind:
                findings.append(Finding(path, number, kind, m.group("name")))
    return findings


def iter_sources(paths: list[Path]):
    skip_dirs = [(REPO_ROOT / d).resolve() for d in SKIP_DIRS]
    for base in paths:
        candidates = [base] if base.is_file() else sorted(base.rglob("*"))
        for path in candidates:
            if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
                continue
            if SKIP_PARTS & set(path.parts) or path.name in SKIP_NAMES:
                continue
            resolved = path.resolve()
            if any(d == resolved or d in resolved.parents for d in skip_dirs):
                continue
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero on findings not in the baseline (CI gate)")
    parser.add_argument("--baseline", type=Path,
                        default=REPO_ROOT / "scripts" / "win32_names_baseline.txt",
                        help="file of known sites still to fix")
    parser.add_argument("--no-baseline", action="store_true",
                        help="report everything, including the known sites")
    parser.add_argument("--update-baseline", action="store_true",
                        help="rewrite the baseline from the current tree and exit")
    parser.add_argument("--generate-names", type=Path, metavar="INCLUDE_DIR",
                        help="rewrite win32_aw_macros.txt from a MinGW-w64 include dir")
    parser.add_argument("paths", nargs="*",
                        help="files or directories to scan (default: the source roots)")
    args = parser.parse_args()

    if args.generate_names:
        names = generate_names(args.generate_names)
        NAMES_FILE.write_text(NAMES_HEADER + "\n".join(names) + "\n", encoding="utf-8")
        print(f"check_win32_names: wrote {len(names)} names to "
              f"{NAMES_FILE.relative_to(REPO_ROOT)}.")
        return 0

    names = load_lines(NAMES_FILE)
    if not names:
        print(f"check_win32_names: {NAMES_FILE} is missing or empty.")
        return 1
    names -= undone_names()

    if args.paths:
        roots = [Path(p).resolve() for p in args.paths]
    else:
        roots = [REPO_ROOT / r for r in SEARCH_ROOTS]
        roots = [r for r in roots if r.exists()]

    findings: list[Finding] = []
    scanned = 0
    for path in iter_sources(roots):
        scanned += 1
        findings.extend(check_file(path, names))

    if args.update_baseline:
        keys = sorted({f.key for f in findings})
        args.baseline.write_text(
            BASELINE_HEADER + "\n".join(keys) + ("\n" if keys else ""),
            encoding="utf-8")
        print(f"check_win32_names: wrote {len(keys)} entries to "
              f"{args.baseline.relative_to(REPO_ROOT)}.")
        return 0

    baseline = set() if args.no_baseline else load_lines(args.baseline)
    fresh = [f for f in findings if f.key not in baseline]

    for finding in fresh:
        print(finding)

    if fresh:
        print(f"\n{len(fresh)} function name(s) that <windows.h> renames, in "
              f"{scanned} files.\nIn a file that includes windows.h the name "
              "gets an A/W suffix, and the Windows link fails when the "
              "declaring and the calling file disagree (AGENTS.md). Rename the "
              "function; if this site is correct as it stands, say why and it "
              "will be skipped:\n"
              "    x->GetObject(...);   // win32-name-ok: <why>")
        return 1 if args.strict else 0

    known = len(findings)
    if known:
        print(f"check_win32_names: no new findings ({scanned} files; "
              f"{known} baselined site{'' if known == 1 else 's'} still to fix "
              f"- see {args.baseline.name}).")
    else:
        print(f"check_win32_names: clean ({scanned} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
