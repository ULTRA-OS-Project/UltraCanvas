#!/usr/bin/env python3
"""Flag identifiers named like an X11 macro, and the #undef guards that hide them.

On Linux and BSD the framework's window headers reach <X11/Xlib.h>, which
#defines a few hundred object-like macros with ordinary-looking names:

    #define None     0L        #define Success  0        #define Always   2
    #define Bool     int       #define Status   int      #define NoValue  0x0000
    #define Above    0         #define Below    1        #define True     1

The preprocessor rewrites every later token of that name, so an enumerator,
a method or a member called `None`, `Status` or `Above` compiles in the
translation units that include the header first and fails in the one that
includes a window header first ("expected identifier", "expected
unqualified-id"). For years each module hid that with `#undef None` at the
top of its header, or with include-order comments in every file that hit
it. In 0.9.363 every such enumerator was renamed (`ErrorCode::Ok`,
`BorderStyle::NoBorder`, `UltraNetAuthType::NoAuth`, ...) and every guard
removed. This check keeps it that way.

What is reported (the name list is scripts/x11_macro_names.txt, generated
from the X11 headers):

  x11-undef
      `#undef Name` or `#pragma push_macro("Name")` for a name on the list.
      The guard is the symptom: rename the identifier it protects instead.

  x11-enumerator
      An enumerator of that name in any enum. Pick a name that says what
      the value means: `NoBorder`, `NoLabels`, `Ok`, `AboveBaseline`.

  x11-declaration
      A function, method, member or variable declared with that name
      (`std::string Status() const`, `uint8_t Bool = 0`). `Status` and
      `Bool` are type macros (`int`), so such a declaration does not even
      parse after the X11 header.

A site that is correct as it stands says so where it is and is skipped:

    uint8_t Status = 0;   // x11-name-ok: <why>

Sites that predate the check are listed in scripts/x11_names_baseline.txt
(empty when the check was introduced) and only shrink; a new one fails
CI (`x11-names.yml`, --strict). --self-test runs the rules against
built-in examples first, so a rule that stopped matching cannot pass by
finding nothing.

Usage:
    python3 scripts/check_x11_names.py                 # report
    python3 scripts/check_x11_names.py --strict        # CI gate
    python3 scripts/check_x11_names.py --self-test
    python3 scripts/check_x11_names.py --generate-names /usr/include/X11
    python3 scripts/check_x11_names.py --update-baseline
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
NAMES_FILE = REPO_ROOT / "scripts" / "x11_macro_names.txt"

SEARCH_ROOTS = [
    "UltraCanvas",
    "SmartHome",
    "UltraAI",
    "UltraCloud",
    "VirtualFS",
    "UltraNet",
    "VideoFX",
    "Apps",
    "Tests",
]
SOURCE_SUFFIXES = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx", ".mm", ".inl"}
SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug", "libspecific"}
# The Linux platform glue is the one place the X11 names are meant: it uses
# the macros as values and declares nothing by their names.
SKIP_DIRS = [Path("UltraCanvas/OS/Linux")]
SKIP_NAMES = {"miniaudio.h"}

EXEMPT_RE = re.compile(r"//.*\bx11-name-ok\b")

# Words that may stand before an identifier without making it a declaration.
NOT_A_TYPE = {"return", "co_return", "co_yield", "throw", "else", "case", "new",
              "delete", "do", "sizeof", "decltype", "and", "or", "not", "goto",
              "define", "if", "while", "for", "switch", "in"}

# X11 headers whose object-like macros are collected by --generate-names.
X11_HEADERS = ["X.h", "Xlib.h", "Xutil.h"]
MACRO_RE = re.compile(r"^\s*#\s*define\s+([A-Z][A-Za-z]+)\s+\S", re.MULTILINE)

NAMES_HEADER = """\
# Object-like macros that <X11/X.h>, <X11/Xlib.h> and <X11/Xutil.h> #define
# with ordinary-looking names (None, Success, Status, Bool, Always, NoValue,
# Above, Below, True, False, KeyPress, ...). Read by scripts/check_x11_names.py:
# no enumerator, function, method or member of ours may carry one of them,
# and no header may #undef one. Regenerate with:
#     python3 scripts/check_x11_names.py --generate-names /usr/include/X11
"""

BASELINE_HEADER = """\
# Identifiers of ours named like an X11 macro, and #undef guards for such a
# name, recorded so CI can block *new* ones while these are worked off. Each
# line is <path>::<kind>::<name>.
#
# These are debt, not exceptions: in a translation unit that includes a window
# header first, the preprocessor rewrites the name and the file no longer
# compiles. Rename the identifier (and drop the guard) and remove the line.
#
# Do not add to this file to silence a new finding. A site that is correct as
# it stands says so where it is:
#     uint8_t Status = 0;   // x11-name-ok: <why>
#
# Regenerate with:
#     python3 scripts/check_x11_names.py --update-baseline
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
        what = {
            "x11-undef": "guards an X11 macro - rename the identifier it protects instead",
            "x11-enumerator": "is an X11 macro - rename the enumerator (NoBorder, Ok, AboveBaseline, ...)",
            "x11-declaration": "is an X11 macro - rename the function or member",
        }[self.kind]
        return f"{self.rel}:{self.line}: {self.kind}: {self.name} {what}"


def load_lines(path: Path) -> set[str]:
    if not path.is_file():
        return set()
    return {line.strip() for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.startswith("#")}


def generate_names(include_dir: Path) -> list[str]:
    names: set[str] = set()
    for header in X11_HEADERS:
        text = (include_dir / header).read_text(encoding="utf-8", errors="replace")
        names.update(MACRO_RE.findall(text))
    return sorted(names)


def strip_comments_and_strings(code: str) -> str:
    """Blank out comments and literals, keeping line numbers (newlines stay)."""
    def blank(m: re.Match) -> str:
        return re.sub(r"[^\n]", " ", m.group(0))
    code = re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', blank, code)
    code = re.sub(r"/\*.*?\*/", blank, code, flags=re.S)
    code = re.sub(r"//[^\n]*", blank, code)
    return code


UNDEF_RE = re.compile(r'^\s*#\s*(?:undef\s+([A-Za-z_]\w*)|pragma\s+push_macro\s*\(\s*"([A-Za-z_]\w*)"\s*\))',
                      re.MULTILINE)
ENUM_RE = re.compile(r"\benum\s+(?:class\s+|struct\s+)?(?:[A-Za-z_]\w*\s*)?(?::\s*[\w:]+\s*)?\{")
ENUMERATOR_RE = re.compile(r"(?:^|[{,\s])([A-Za-z_]\w*)\s*(?==|,|\}|$)", re.MULTILINE)
DECL_RE = re.compile(r"(?:^|[^\w:.>])(?P<type>[A-Za-z_][\w:<>]*(?:\s*[*&]+)?)\s+"
                     r"(?P<name>[A-Za-z_]\w*)\s*(?P<after>\(|=|;|\{|,|\[)")


def check_text(text: str, names: set[str], path: Path) -> list[Finding]:
    findings: list[Finding] = []
    exempt_lines = {i + 1 for i, line in enumerate(text.splitlines()) if EXEMPT_RE.search(line)}
    for m in UNDEF_RE.finditer(text):
        name = m.group(1) or m.group(2)
        if name in names:
            findings.append(Finding(path, text.count("\n", 0, m.start()) + 1, "x11-undef", name))
    code = strip_comments_and_strings(text)
    enum_spans: list[tuple[int, int]] = []
    for m in ENUM_RE.finditer(code):
        i, depth = m.end(), 1
        while i < len(code) and depth:
            depth += (code[i] == "{") - (code[i] == "}")
            i += 1
        enum_spans.append((m.end(), i - 1))
        for e in ENUMERATOR_RE.finditer(code, m.end(), i - 1):
            name = e.group(1)
            if name in names:
                line = code.count("\n", 0, e.start(1)) + 1
                if line not in exempt_lines:
                    findings.append(Finding(path, line, "x11-enumerator", name))
    for m in DECL_RE.finditer(code):
        name = m.group("name")
        if name not in names:
            continue
        if any(a <= m.start("name") < b for a, b in enum_spans):
            continue
        type_word = re.search(r"([A-Za-z_]\w*)$", m.group("type").rstrip("*& "))
        if not type_word or type_word.group(1) in NOT_A_TYPE:
            continue
        line = code.count("\n", 0, m.start("name")) + 1
        if line not in exempt_lines:
            findings.append(Finding(path, line, "x11-declaration", name))
    return findings


def check_file(path: Path, names: set[str]) -> list[Finding]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    return check_text(text, names, path)


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


SELF_TEST = """\
#pragma once
#ifdef Success
#undef Success
#endif
#pragma push_macro("None")
enum class ErrorCode { None = 0, Other };
enum Kind { Pen, Status } kind = Pen;
struct Result {
    std::string Status() const;
    static Result Success() { return {}; }
    uint8_t Bool = 0;
    uint8_t Always = 0;   // x11-name-ok: mirrors the wire field
    bool ok = true;
    ErrorCode Fine = ErrorCode::None;
};
Window w = None;
int code = Success;
void F() {
    return None;
    if (x) Status();
    switch (y) { case Above: break; }
    auto below = Below;
}
"""
SELF_TEST_EXPECT = {("x11-undef", "Success", 3), ("x11-undef", "None", 5),
                    ("x11-enumerator", "None", 6), ("x11-enumerator", "Status", 7),
                    ("x11-declaration", "Status", 9), ("x11-declaration", "Success", 10),
                    ("x11-declaration", "Bool", 11)}


def self_test(names: set[str]) -> int:
    got = {(f.kind, f.name, f.line) for f in check_text(SELF_TEST, names, Path("self-test.h"))}
    if got == SELF_TEST_EXPECT:
        print("check_x11_names: self-test ok.")
        return 0
    for item in sorted(SELF_TEST_EXPECT - got):
        print(f"self-test: missing {item}")
    for item in sorted(got - SELF_TEST_EXPECT):
        print(f"self-test: unexpected {item}")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero on findings not in the baseline (CI gate)")
    parser.add_argument("--baseline", type=Path,
                        default=REPO_ROOT / "scripts" / "x11_names_baseline.txt",
                        help="file of known sites still to fix")
    parser.add_argument("--no-baseline", action="store_true",
                        help="report everything, including the known sites")
    parser.add_argument("--update-baseline", action="store_true",
                        help="rewrite the baseline from the current tree and exit")
    parser.add_argument("--generate-names", type=Path, metavar="X11_INCLUDE_DIR",
                        help="rewrite x11_macro_names.txt from an X11 include dir")
    parser.add_argument("--self-test", action="store_true",
                        help="run the rules against built-in examples and exit")
    parser.add_argument("paths", nargs="*",
                        help="files or directories to scan (default: the source roots)")
    args = parser.parse_args()

    if args.generate_names:
        names = generate_names(args.generate_names)
        NAMES_FILE.write_text(NAMES_HEADER + "\n".join(names) + "\n", encoding="utf-8")
        print(f"check_x11_names: wrote {len(names)} names to {NAMES_FILE.relative_to(REPO_ROOT)}.")
        return 0

    names = load_lines(NAMES_FILE)
    if not names:
        print(f"check_x11_names: {NAMES_FILE} is missing or empty.")
        return 1
    if args.self_test:
        return self_test(names)

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
        args.baseline.write_text(BASELINE_HEADER + "\n".join(keys) + ("\n" if keys else ""),
                                 encoding="utf-8")
        print(f"check_x11_names: wrote {len(keys)} entries to {args.baseline.relative_to(REPO_ROOT)}.")
        return 0

    baseline = set() if args.no_baseline else load_lines(args.baseline)
    fresh = [f for f in findings if f.key not in baseline]
    for finding in fresh:
        print(finding)
    if fresh:
        print(f"\n{len(fresh)} identifier(s) named like an X11 macro, in {scanned} files.\n"
              "In a translation unit that includes a window header first, Xlib.h's\n"
              "macro rewrites the name and the file no longer compiles (AGENTS.md).\n"
              "Rename it - an #undef guard only moves the failure to the next file.\n"
              "If this site is correct as it stands, say why and it will be skipped:\n"
              "    uint8_t Status = 0;   // x11-name-ok: <why>")
        return 1 if args.strict else 0

    known = len(findings)
    if known:
        print(f"check_x11_names: no new findings ({scanned} files; {known} baselined "
              f"site{'' if known == 1 else 's'} still to fix - see {args.baseline.name}).")
    else:
        print(f"check_x11_names: clean ({scanned} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
