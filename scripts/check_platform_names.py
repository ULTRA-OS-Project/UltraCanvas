#!/usr/bin/env python3
"""Flag identifiers of ours that a platform header takes for itself.

Three platforms, one trap: a system header #defines (or typedefs at global
scope) an ordinary-looking name, and whatever we declare under that name
compiles in the translation units that never see the header and fails in
the one that includes it first.

  win32   <windows.h> #defines a few thousand function names to their A or
          W variant (`#define CreateFile __MINGW_NAME_AW(CreateFile)`), so a
          method of ours called CreateFile is CreateFile in the .cpp that
          defines it and CreateFileW in a .cpp that sees windows.h, and the
          Windows link fails with an undefined symbol no other platform
          shows (UltraCanvasPdfSurface::CreateFile broke both Windows
          builds that way).

  x11     <X11/Xlib.h>, which the Linux and BSD window headers reach,
          #defines None, Success, Status, Bool, Always, NoValue, Above,
          Below, True, False and a few hundred more object-like macros, so an
          enumerator, method or member of that name is rewritten ("expected
          identifier") in any file that includes a window header first. For
          years each module hid that with `#undef None`; in 0.9.363 every such
          name was renamed and every guard removed.

  objc    In a .mm file (the macOS and iOS glue) <objc/objc.h> is in effect:
          YES, NO, nil and Nil are macros that rewrite any identifier of that
          name, and id, Class, SEL, IMP, BOOL and Protocol are global typedefs
          that a FILE-SCOPE declaration of the same name redefines. Inside a
          namespace, a class, an enum or a parameter list those six are free
          (clang accepts `struct S { int id; }` and `void f(int id)`), and the
          framework lives in namespaces. Foundation also brings
          <MacTypes.h>, which puts Rect, Point, Boolean, Byte, Size, Handle,
          Ptr, Fixed, Style, OSErr, UInt8, Str255 and a hundred more
          typedefs at global scope, the Style constants normal, bold,
          italic, underline, ... as global enumerators, and kInvalidID as a
          macro; scripts/mactypes_global_names.txt is generated from that
          header.

What is reported, by kind (in code, never in comments or string literals):

  win32-name-qualified     `X::DrawText(` - a qualified definition or call
  win32-name-member        `.DrawText(` / `->DrawText(` - calling a member
  win32-name-declaration   `bool DrawText(` - declaring or defining one
      Not reported: an unqualified call (`h = CreateFile(...)` is the real
      Win32 function); anything under UltraCanvas/OS/MSWindows, where every
      file includes windows.h and the macro applies consistently; a name
      the Windows platform headers #undef right after windows.h
      (`#undef DrawText` in UltraCanvasWindowsApplication.h and friends -
      adding one there is how a name is made safe for the whole framework);
      a name the file itself #undefs. The names come from
      scripts/win32_aw_macros.txt, generated from the MinGW-w64 headers.

  x11-undef                `#undef Name` / `#pragma push_macro("Name")` for
                           a name on the list: the guard is the symptom
  x11-enumerator           an enumerator of that name, in any enum
  x11-declaration          a function, method, member or variable of that
                           name (`std::string Status() const`, `uint8_t Bool`)
      The names come from scripts/x11_macro_names.txt, generated from the
      X11 headers. The Linux platform glue is skipped: it uses the macros
      as values and declares nothing by their names.

  objc-macro               an enumerator, function, member or variable named
                           YES, NO, nil or Nil, in any scope
  objc-global              a file-scope declaration (variable, function,
                           struct/class/enum, typedef, using) named id, Class,
                           SEL, IMP, BOOL or Protocol, or like a MacTypes.h
                           typedef or constant (Rect, Boolean, bold, noErr)
  objc-undef               `#undef` of one of those names

A site that is correct as it stands says so where it is and is skipped:

    hr = item->GetDisplayName(&name);   // win32-name-ok: COM method, windows.h included
    uint8_t Status = 0;                 // x11-name-ok: <why>
    int NO = 0;                         // objc-name-ok: <why>

scripts/platform_names_baseline.txt lists the sites that predate the checks
(Win32 names still being worked off; the X11 and Objective-C sets are empty
and stay so), so CI blocks *new* ones (`platform-names.yml`, --strict). Each
platform's rules have a --self-test against built-in examples, run first in
CI, so a rule that stopped matching cannot pass by finding nothing.

Usage:
    python3 scripts/check_platform_names.py [--strict] [paths...]
    python3 scripts/check_platform_names.py --platform x11 [--platform ...]
    python3 scripts/check_platform_names.py --self-test
    python3 scripts/check_platform_names.py --update-baseline
    python3 scripts/check_platform_names.py --platform win32 --generate-names <mingw-w64 include dir>
    python3 scripts/check_platform_names.py --platform x11 --generate-names /usr/include/X11
    python3 scripts/check_platform_names.py --platform objc --generate-names <SDK>/usr/include/MacTypes.h

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
SCRIPTS = REPO_ROOT / "scripts"
BASELINE_FILE = SCRIPTS / "platform_names_baseline.txt"

SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug"}
SKIP_NAMES = {"miniaudio.h"}          # vendored single-file libraries, not ours to rename

# Words that may stand before an identifier without making it a declaration.
NOT_A_TYPE = {"return", "co_return", "co_yield", "throw", "else", "case", "new",
              "delete", "do", "sizeof", "decltype", "and", "or", "not", "goto",
              "define", "if", "while", "for", "switch", "in"}

BASELINE_HEADER = """\
# Identifiers of ours that a platform header takes for itself, recorded so CI
# can block *new* ones while these are worked off. Each line is
# <path>::<kind>::<name>; the kind says which platform (win32-*, x11-*,
# objc-*) - see scripts/check_platform_names.py.
#
# These are debt, not exceptions. win32-*: in a translation unit that includes
# <windows.h> the name becomes NameW and the Windows link fails as soon as the
# declaring and the calling file disagree. x11-* / objc-*: in a file that
# includes the platform header first the name no longer compiles. Rename the
# identifier and remove the line.
#
# Do not add to this file to silence a new finding. A site that is correct as
# it stands says so where it is:
#     hr = item->GetDisplayName(&name);   // win32-name-ok: <why>
#     uint8_t Status = 0;                 // x11-name-ok: <why>
#     int NO = 0;                         // objc-name-ok: <why>
#
# Regenerate with:
#     python3 scripts/check_platform_names.py --update-baseline
"""

MESSAGES = {
    "win32-name-qualified": "is a <windows.h> macro ({name}W in a file that includes it) - rename the function",
    "win32-name-member": "is a <windows.h> macro ({name}W in a file that includes it) - rename the function",
    "win32-name-declaration": "is a <windows.h> macro ({name}W in a file that includes it) - rename the function",
    "x11-undef": "guards an X11 macro - rename the identifier it protects instead",
    "x11-enumerator": "is an X11 macro - rename the enumerator (NoBorder, Ok, AboveBaseline, ...)",
    "x11-declaration": "is an X11 macro - rename the function or member",
    "objc-macro": "is an Objective-C macro - rename it (the .mm files see it rewritten)",
    "objc-global": "is an Objective-C runtime type - declare it in a namespace, or rename it",
    "objc-undef": "guards an Objective-C name - rename the identifier it protects instead",
}


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
        return f"{self.rel}:{self.line}: {self.kind}: {self.name} {MESSAGES[self.kind].format(name=self.name)}"


# ---------------------------------------------------------------------------
# Shared machinery
# ---------------------------------------------------------------------------

def load_lines(path: Path) -> set[str]:
    if not path.is_file():
        return set()
    out = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            out.add(line)
    return out


def iter_sources(paths: list[Path], suffixes: set[str], skip_dirs: list[Path]):
    resolved_skips = [(REPO_ROOT / d).resolve() for d in skip_dirs]
    for base in paths:
        candidates = [base] if base.is_file() else sorted(base.rglob("*"))
        for path in candidates:
            if path.suffix not in suffixes or not path.is_file():
                continue
            if SKIP_PARTS & set(path.parts) or path.name in SKIP_NAMES:
                continue
            resolved = path.resolve()
            if any(d == resolved or d in resolved.parents for d in resolved_skips):
                continue
            yield path


def strip_strings(code: str) -> str:
    """Blank out string and character literals so their text never matches."""
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', code)


def strip_comments_and_strings(code: str) -> str:
    """Blank out comments and literals, keeping line numbers (newlines stay)."""
    def blank(m: re.Match) -> str:
        return re.sub(r"[^\n]", " ", m.group(0))
    code = re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', blank, code)
    code = re.sub(r"/\*.*?\*/", blank, code, flags=re.S)
    code = re.sub(r"//[^\n]*", blank, code)
    return code


ENUM_RE = re.compile(r"\benum\s+(?:class\s+|struct\s+)?(?:[A-Za-z_]\w*\s*)?(?::\s*[\w:]+\s*)?\{")
ENUMERATOR_RE = re.compile(r"(?:^|[{,\s])([A-Za-z_]\w*)\s*(?==|,|\}|$)", re.MULTILINE)
DECL_RE = re.compile(r"(?:^|[^\w:.>])(?P<type>[A-Za-z_][\w:<>]*(?:\s*[*&]+)?)\s+"
                     r"(?P<name>[A-Za-z_]\w*)\s*(?P<after>\(|=|;|\{|,|\[)")
UNDEF_RE = re.compile(r"^\s*#\s*undef\s+([A-Za-z_]\w*)", re.MULTILINE)
PUSH_MACRO_RE = re.compile(r'^\s*#\s*pragma\s+push_macro\s*\(\s*"([A-Za-z_]\w*)"\s*\)', re.MULTILINE)


def enum_bodies(code: str) -> list[tuple[int, int]]:
    """(start, end) of every enum body in `code` (comments and strings blanked)."""
    spans = []
    for m in ENUM_RE.finditer(code):
        i, depth = m.end(), 1
        while i < len(code) and depth:
            depth += (code[i] == "{") - (code[i] == "}")
            i += 1
        spans.append((m.end(), i - 1))
    return spans


def declared_type_word(m: re.Match) -> str | None:
    """The word before the declared name in a DECL_RE match, or None when it
    is a keyword that makes this an expression (`return None;`)."""
    word = re.search(r"([A-Za-z_]\w*)$", m.group("type").rstrip("*& "))
    if not word or word.group(1) in NOT_A_TYPE:
        return None
    return word.group(1)


def line_of(code: str, pos: int) -> int:
    return code.count("\n", 0, pos) + 1


# ---------------------------------------------------------------------------
# win32: function names <windows.h> turns into NameA / NameW
# ---------------------------------------------------------------------------

WIN32_NAMES_FILE = SCRIPTS / "win32_aw_macros.txt"
WIN32_ROOTS = ["UltraCanvas/core", "UltraCanvas/include", "UltraCanvas/Plugins", "UltraCanvas/OS",
               "UltraCanvas/libspecific", "UltraCanvas/dialogs",
               "SmartHome", "UltraAI", "UltraCloud", "VirtualFS", "Apps", "Tests"]
# Every file in MSWindows includes <windows.h>: the macro applies everywhere,
# and a `->GetObject(` is the Win32/COM method it names. The other platform
# directories never compile on Windows, so windows.h never reaches them.
WIN32_SKIP_DIRS = [Path("UltraCanvas/OS") / d for d in ("MSWindows", "Linux", "MacOS", "BSD", "WASM", "Android")]
WIN32_SUFFIXES = {".cpp", ".h", ".hpp", ".mm", ".c"}
# Headers whose `#undef Name` lines make a name safe everywhere.
WIN32_UNDEF_HEADERS_DIR = Path("UltraCanvas/OS/MSWindows")
WIN32_EXEMPT_RE = re.compile(r"//.*\bwin32-name-ok\b")
WIN32_CALL_RE = re.compile(r"(?P<before>(?:\w+\s*::\s*|::\s*|\.\s*|->\s*)?)"
                           r"(?<![\w])(?P<name>[A-Za-z_]\w*)\s*\(")

WIN32_NAMES_HEADER = """\
# Function-like names that <windows.h> (and the headers it pulls in) #define
# to their A or W variant: `#define CreateFile __MINGW_NAME_AW(CreateFile)`
# expands to CreateFileW in a UNICODE build. Read by
# scripts/check_platform_names.py (win32).
#
# Generated from the MinGW-w64 headers (the toolchain behind the MSYS2
# CLANG64 / CLANGARM64 Windows builds); driver-kit-only (ddk/) names left out.
# Regenerate with:
#     python3 scripts/check_platform_names.py --platform win32 --generate-names <mingw-w64 include dir>
"""


def win32_generate_names(include_dir: Path) -> list[str]:
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


def win32_undone_names() -> set[str]:
    """Names the Windows platform headers #undef after including windows.h."""
    out: set[str] = set()
    for header in sorted((REPO_ROOT / WIN32_UNDEF_HEADERS_DIR).glob("*.h")):
        out |= set(UNDEF_RE.findall(header.read_text(encoding="utf-8", errors="replace")))
    return out


def win32_classify(code: str, m: re.Match) -> str | None:
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


def win32_check_text(text: str, names: set[str], path: Path) -> list[Finding]:
    names = names - set(UNDEF_RE.findall(text))
    findings: list[Finding] = []
    in_block_comment = False
    for number, line in enumerate(text.splitlines(), start=1):
        if WIN32_EXEMPT_RE.search(line):
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
        for m in WIN32_CALL_RE.finditer(code):
            if m.group("name") not in names:
                continue
            kind = win32_classify(code, m)
            if kind:
                findings.append(Finding(path, number, kind, m.group("name")))
    return findings


WIN32_SELF_TEST = """\
class PdfSurface {
public:
    bool CreateFile(const std::string& name);          // declaration
    static std::unique_ptr<PdfSurface> LoadImage(int);  // template return type
    void Draw() { DrawTextRun(); }
};
bool PdfSurface::CreateFile(const std::string&) { return true; }   // qualified
void Use(PdfSurface* s) {
    s->LoadImage(1);                       // member
    HANDLE h = CreateFile(L"x", 0);        // the real Win32 function: not reported
    h = ::CreateFile(L"y", 0);             // likewise
    hr = item->GetObject(&o);              // win32-name-ok: COM method, windows.h included
    if (a && SendMessage(b)) {}            // a call, not a declaration
}
"""
WIN32_SELF_TEST_EXPECT = {("win32-name-declaration", "CreateFile", 3), ("win32-name-declaration", "LoadImage", 4),
                          ("win32-name-qualified", "CreateFile", 7), ("win32-name-member", "LoadImage", 9)}


# ---------------------------------------------------------------------------
# x11: object-like macros of <X11/Xlib.h>
# ---------------------------------------------------------------------------

X11_NAMES_FILE = SCRIPTS / "x11_macro_names.txt"
X11_ROOTS = ["UltraCanvas", "SmartHome", "UltraAI", "UltraCloud", "VirtualFS", "UltraNet", "VideoFX", "Apps", "Tests"]
# The Linux platform glue is the one place the X11 names are meant: it uses
# the macros as values and declares nothing by their names.
X11_SKIP_DIRS = [Path("UltraCanvas/OS/Linux")]
X11_SUFFIXES = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx", ".mm", ".inl"}
X11_SKIP_PARTS_EXTRA = {"libspecific"}
X11_EXEMPT_RE = re.compile(r"//.*\bx11-name-ok\b")
X11_HEADERS = ["X.h", "Xlib.h", "Xutil.h"]
X11_MACRO_RE = re.compile(r"^\s*#\s*define\s+([A-Z][A-Za-z]+)\s+\S", re.MULTILINE)

X11_NAMES_HEADER = """\
# Object-like macros that <X11/X.h>, <X11/Xlib.h> and <X11/Xutil.h> #define
# with ordinary-looking names (None, Success, Status, Bool, Always, NoValue,
# Above, Below, True, False, KeyPress, ...). Read by
# scripts/check_platform_names.py (x11): no enumerator, function, method or
# member of ours may carry one of them, and no header may #undef one.
# Regenerate with:
#     python3 scripts/check_platform_names.py --platform x11 --generate-names /usr/include/X11
"""


def x11_generate_names(include_dir: Path) -> list[str]:
    names: set[str] = set()
    for header in X11_HEADERS:
        text = (include_dir / header).read_text(encoding="utf-8", errors="replace")
        names.update(X11_MACRO_RE.findall(text))
    return sorted(names)


def x11_check_text(text: str, names: set[str], path: Path) -> list[Finding]:
    findings: list[Finding] = []
    exempt = {i + 1 for i, line in enumerate(text.splitlines()) if X11_EXEMPT_RE.search(line)}
    for regex in (UNDEF_RE, PUSH_MACRO_RE):
        for m in regex.finditer(text):
            if m.group(1) in names:
                findings.append(Finding(path, line_of(text, m.start()), "x11-undef", m.group(1)))
    code = strip_comments_and_strings(text)
    spans = enum_bodies(code)
    for a, b in spans:
        for e in ENUMERATOR_RE.finditer(code, a, b):
            if e.group(1) in names and line_of(code, e.start(1)) not in exempt:
                findings.append(Finding(path, line_of(code, e.start(1)), "x11-enumerator", e.group(1)))
    for m in DECL_RE.finditer(code):
        name = m.group("name")
        if name not in names or any(a <= m.start("name") < b for a, b in spans):
            continue
        if declared_type_word(m) is None:
            continue
        line = line_of(code, m.start("name"))
        if line not in exempt:
            findings.append(Finding(path, line, "x11-declaration", name))
    findings.sort(key=lambda f: (f.line, f.kind))
    return findings


X11_SELF_TEST = """\
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
X11_SELF_TEST_EXPECT = {("x11-undef", "Success", 3), ("x11-undef", "None", 5),
                        ("x11-enumerator", "None", 6), ("x11-enumerator", "Status", 7),
                        ("x11-declaration", "Status", 9), ("x11-declaration", "Success", 10),
                        ("x11-declaration", "Bool", 11)}


# ---------------------------------------------------------------------------
# objc: what <objc/objc.h> takes in a .mm file
# ---------------------------------------------------------------------------

OBJC_MACRO_NAMES = {"YES", "NO", "nil", "Nil"}
OBJC_GLOBAL_NAMES = {"id", "Class", "SEL", "IMP", "BOOL", "Protocol"}
# What <MacTypes.h> adds, as `macro Name` / `global Name` lines.
OBJC_NAMES_FILE = SCRIPTS / "mactypes_global_names.txt"
OBJC_NAMES_HEADER = """\
# Names <MacTypes.h> (CoreServices, reached by every Foundation import) puts at
# global scope, so a .mm file sees them in whatever C++ header it includes.
# `global Name` is a typedef, a struct, a Str255-style array type, an inline
# function or an anonymous-enum constant (Rect, Point, Boolean, Byte, Size,
# Handle, Ptr, Fixed, Style, OSErr, UInt8, noErr, bold, italic, ...): a
# FILE-SCOPE declaration of ours with that name redefines it - inside a
# namespace, a class or an enum class it is free. `macro Name` is an
# object-like macro (kInvalidID) that rewrites the name in every scope.
# Read by scripts/check_platform_names.py (objc), together with the
# Objective-C runtime's own id / Class / SEL / IMP / BOOL / Protocol and
# YES / NO / nil / Nil. Generated from the macOS SDK's usr/include/MacTypes.h
# (byte-identical in the 10.15 and 11.3 SDKs; `Length`, `Debugger68k` and
# `StrLength` as a macro are left out because a C++ translation unit never
# sees them). Regenerate with:
#     python3 scripts/check_platform_names.py --platform objc --generate-names <SDK>/usr/include/MacTypes.h
"""
OBJC_EXEMPT_RE = re.compile(r"//.*\bobjc-name-ok\b")
# Only the macOS glue is Objective-C++; the other platform folders never meet
# <objc/objc.h> or <MacTypes.h> (and the Windows one #undefs Rect on purpose).
OBJC_SKIP_DIRS = [Path("UltraCanvas/OS") / d for d in ("MSWindows", "Linux", "BSD", "WASM", "Android")]
# A type, typedef or alias introduced at file scope.
OBJC_TYPE_DECL_RE = re.compile(r"(?:^|[^\w:])(?:struct|class|union|enum(?:\s+class|\s+struct)?|typedef\s+[^;{]*?|using)\s+"
                               r"(?P<name>[A-Za-z_]\w*)\s*(?P<after>\{|;|:|=)")


def objc_generate_names(header: Path) -> list[str]:
    """`macro` / `global` lines from MacTypes.h (a file, or a directory holding it)."""
    if header.is_dir():
        header = header / "MacTypes.h"
    code = strip_comments_and_strings(header.read_text(encoding="utf-8", errors="replace"))
    # Object-like macros, outside the #if blocks a C++ translation unit never
    # enters (OLDROUTINENAMES, TARGET_CPU_68K); NULL and nil are the runtime's.
    macros: set[str] = set()
    off = 0
    for line in code.splitlines():
        s = line.strip()
        if re.match(r"#\s*if\b.*\b(OLDROUTINENAMES|TARGET_CPU_68K)\b", s):
            off += 1
        elif off and re.match(r"#\s*(if|ifdef|ifndef)\b", s):
            off += 1
        elif off and re.match(r"#\s*endif\b", s):
            off -= 1
        elif not off:
            m = re.match(r"#\s*define\s+([A-Za-z_]\w*)\s+\S", s)
            if m and m.group(1) not in ("NULL", "nil") and not m.group(1).startswith(("ALLOW_", "__")):
                macros.add(m.group(1))
    names: set[str] = set()
    for m in re.finditer(r"^typedef\b[^;{]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*;", code, re.M):
        names.add(m.group(1))
    for m in re.finditer(r"^\}\s*([A-Za-z_][\w ,*]*);", code, re.M):             # `} Point, *PointPtr;`
        names.update(n.strip(" *") for n in m.group(1).split(","))
    for m in re.finditer(r"^(?:struct|union)\s+([A-Za-z_]\w*)\s*\{", code, re.M):
        names.add(m.group(1))
    for m in re.finditer(r"^(?:static\s+)?inline\s+[\w\s*]+?\b([A-Za-z_]\w*)\s*\(", code, re.M):
        names.add(m.group(1))
    for m in re.finditer(r"^enum\s*\{(.*?)\};", code, re.M | re.S):                   # anonymous enum constants
        names.update(re.findall(r"(?:^|[{,\s])([A-Za-z_]\w*)\s*=", m.group(1)))
    names.discard("")
    return [f"macro {n}" for n in sorted(macros)] + [f"global {n}" for n in sorted(names)]


def at_file_scope(code: str, pos: int) -> bool:
    """True at `pos` of `code` (comments and strings already blanked) when
    outside every namespace, class and function - brace depth 0 - and outside
    a parameter list, where a parameter named `id` is fine."""
    return (code.count("{", 0, pos) == code.count("}", 0, pos)
            and code.count("(", 0, pos) == code.count(")", 0, pos))


def objc_check_text(text: str, names: set[str], path: Path) -> list[Finding]:
    macro_names = OBJC_MACRO_NAMES | {n.split()[1] for n in names if n.startswith("macro ")}
    global_names = OBJC_GLOBAL_NAMES | {n.split()[1] for n in names if n.startswith("global ")}
    all_names = macro_names | global_names
    findings: list[Finding] = []
    exempt = {i + 1 for i, line in enumerate(text.splitlines()) if OBJC_EXEMPT_RE.search(line)}
    for m in UNDEF_RE.finditer(text):
        if m.group(1) in all_names:
            findings.append(Finding(path, line_of(text, m.start()), "objc-undef", m.group(1)))
    code = strip_comments_and_strings(text)
    spans = enum_bodies(code)
    for a, b in spans:
        for e in ENUMERATOR_RE.finditer(code, a, b):
            if e.group(1) in macro_names and line_of(code, e.start(1)) not in exempt:
                findings.append(Finding(path, line_of(code, e.start(1)), "objc-macro", e.group(1)))
    for m in DECL_RE.finditer(code):
        name = m.group("name")
        if name not in all_names or any(a <= m.start("name") < b for a, b in spans):
            continue
        if declared_type_word(m) is None:
            continue
        line = line_of(code, m.start("name"))
        if line in exempt:
            continue
        if name in macro_names:
            findings.append(Finding(path, line, "objc-macro", name))
        elif at_file_scope(code, m.start("name")):
            findings.append(Finding(path, line, "objc-global", name))
    for m in OBJC_TYPE_DECL_RE.finditer(code):
        name = m.group("name")
        if name not in global_names:
            continue
        line = line_of(code, m.start("name"))
        if line not in exempt and at_file_scope(code, m.start("name")):
            findings.append(Finding(path, line, "objc-global", name))
    findings.sort(key=lambda f: (f.line, f.kind))
    return findings


OBJC_SELF_TEST = """\
#pragma once
#undef nil
enum class Answer { YES, NO, Maybe };
struct Wire {
    uint8_t Nil = 0;
    int id = 0;             // a member: fine in a .mm
    bool NO = false;        // objc-name-ok: mirrors the wire field
};
int id = 0;
typedef int BOOL;
struct Class {};
using SEL = int;
namespace N {
    int id = 0;             // in a namespace: fine
    struct Class {};
    enum class Kind { id, Protocol };
    bool nil = false;
}
void f(int id) { int Class = id; (void)Class; }
int g(const char* id, int Class = 0);
struct Rect { int x, y; };          // MacTypes.h typedef, file scope
int bold = 1;                       // MacTypes.h Style constant
enum class Weight { bold, italic }; // enum class scope: fine
namespace N { struct Point {}; }    // fine
int kInvalidID = 0;                 // MacTypes.h macro: any scope
"""
OBJC_SELF_TEST_EXPECT = {("objc-undef", "nil", 2), ("objc-macro", "YES", 3), ("objc-macro", "NO", 3),
                         ("objc-macro", "Nil", 5), ("objc-global", "id", 9), ("objc-global", "BOOL", 10),
                         ("objc-global", "Class", 11), ("objc-global", "SEL", 12), ("objc-macro", "nil", 17),
                         ("objc-global", "Rect", 21), ("objc-global", "bold", 22), ("objc-macro", "kInvalidID", 25)}


# ---------------------------------------------------------------------------
# The platforms
# ---------------------------------------------------------------------------

class Platform:
    def __init__(self, key, roots, skip_dirs, suffixes, check_text, self_test, expect,
                 names_file=None, names_header="", generate=None, extra_skip_parts=frozenset()):
        self.key, self.roots, self.skip_dirs, self.suffixes = key, roots, skip_dirs, suffixes
        self.check_text, self.self_test, self.expect = check_text, self_test, expect
        self.names_file, self.names_header, self.generate = names_file, names_header, generate
        self.extra_skip_parts = extra_skip_parts

    def names(self) -> set[str]:
        if self.names_file is None:
            return set()
        names = load_lines(self.names_file)
        if not names:
            raise SystemExit(f"check_platform_names: {self.names_file} is missing or empty.")
        if self.key == "win32":
            names -= win32_undone_names()
        return names

    def sources(self, paths: list[Path] | None):
        roots = [Path(p).resolve() for p in paths] if paths else \
                [r for r in (REPO_ROOT / r for r in self.roots) if r.exists()]
        for path in iter_sources(roots, self.suffixes, self.skip_dirs):
            if self.extra_skip_parts & set(path.parts):
                continue
            yield path

    def run_self_test(self) -> list[str]:
        got = {(f.kind, f.name, f.line) for f in self.check_text(self.self_test, self.names(), Path("self-test.h"))}
        problems = [f"{self.key}: missing {item}" for item in sorted(self.expect - got)]
        problems += [f"{self.key}: unexpected {item}" for item in sorted(got - self.expect)]
        return problems


PLATFORMS = {
    "win32": Platform("win32", WIN32_ROOTS, WIN32_SKIP_DIRS, WIN32_SUFFIXES, win32_check_text,
                      WIN32_SELF_TEST, WIN32_SELF_TEST_EXPECT, WIN32_NAMES_FILE, WIN32_NAMES_HEADER,
                      win32_generate_names),
    "x11": Platform("x11", X11_ROOTS, X11_SKIP_DIRS, X11_SUFFIXES, x11_check_text,
                    X11_SELF_TEST, X11_SELF_TEST_EXPECT, X11_NAMES_FILE, X11_NAMES_HEADER,
                    x11_generate_names, X11_SKIP_PARTS_EXTRA),
    "objc": Platform("objc", X11_ROOTS, OBJC_SKIP_DIRS, X11_SUFFIXES, objc_check_text,
                     OBJC_SELF_TEST, OBJC_SELF_TEST_EXPECT, OBJC_NAMES_FILE, OBJC_NAMES_HEADER,
                     objc_generate_names, X11_SKIP_PARTS_EXTRA),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", action="append", choices=sorted(PLATFORMS),
                        help="which platform's names to check (repeatable; default: all)")
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero on findings not in the baseline (CI gate)")
    parser.add_argument("--baseline", type=Path, default=BASELINE_FILE,
                        help="file of known sites still to fix")
    parser.add_argument("--no-baseline", action="store_true",
                        help="report everything, including the known sites")
    parser.add_argument("--update-baseline", action="store_true",
                        help="rewrite the baseline from the current tree and exit")
    parser.add_argument("--generate-names", type=Path, metavar="INCLUDE_DIR",
                        help="rewrite the chosen platform's name list from its headers (win32: a "
                             "MinGW-w64 include dir; x11: an X11 include dir; objc: MacTypes.h)")
    parser.add_argument("--self-test", action="store_true",
                        help="run every platform's rules against built-in examples and exit")
    parser.add_argument("paths", nargs="*",
                        help="files or directories to scan (default: the platform's source roots)")
    args = parser.parse_args()
    platforms = [PLATFORMS[k] for k in (args.platform or sorted(PLATFORMS))]

    if args.generate_names:
        if len(platforms) != 1 or platforms[0].generate is None:
            print("check_platform_names: --generate-names needs exactly one --platform.")
            return 2
        p = platforms[0]
        names = p.generate(args.generate_names)
        p.names_file.write_text(p.names_header + "\n".join(names) + "\n", encoding="utf-8")
        print(f"check_platform_names: wrote {len(names)} {p.key} names to {p.names_file.relative_to(REPO_ROOT)}.")
        return 0

    if args.self_test:
        problems = [line for p in platforms for line in p.run_self_test()]
        for line in problems:
            print(f"self-test: {line}")
        if not problems:
            print(f"check_platform_names: self-test ok ({', '.join(p.key for p in platforms)}).")
        return 1 if problems else 0

    findings: list[Finding] = []
    scanned: set[Path] = set()
    for p in platforms:
        names = p.names()
        for path in p.sources(args.paths):
            scanned.add(path)
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            findings.extend(p.check_text(text, names, path))

    if args.update_baseline:
        if args.platform:
            print("check_platform_names: --update-baseline rewrites every platform's entries; drop --platform.")
            return 2
        keys = sorted({f.key for f in findings})
        args.baseline.write_text(BASELINE_HEADER + "\n".join(keys) + ("\n" if keys else ""), encoding="utf-8")
        print(f"check_platform_names: wrote {len(keys)} entries to {args.baseline.relative_to(REPO_ROOT)}.")
        return 0

    baseline = set() if args.no_baseline else load_lines(args.baseline)
    fresh = [f for f in findings if f.key not in baseline]
    for finding in fresh:
        print(finding)
    if fresh:
        print(f"\n{len(fresh)} identifier(s) a platform header takes, in {len(scanned)} files.\n"
              "win32-*: in a file that includes <windows.h> the name gets an A/W suffix and\n"
              "the Windows link fails when the declaring and the calling file disagree.\n"
              "x11-* / objc-*: in a file that includes the platform header first the name is\n"
              "rewritten and no longer compiles (AGENTS.md). Rename it - an #undef guard only\n"
              "moves the failure to the next file. If this site is correct as it stands, say\n"
              "why and it will be skipped:\n"
              "    x->GetObject(...);    // win32-name-ok: <why>\n"
              "    uint8_t Status = 0;   // x11-name-ok: <why>\n"
              "    int NO = 0;           // objc-name-ok: <why>")
        return 1 if args.strict else 0

    known = len(findings)
    which = ", ".join(p.key for p in platforms)
    if known:
        print(f"check_platform_names: no new findings ({which}; {len(scanned)} files; {known} baselined "
              f"site{'' if known == 1 else 's'} still to fix - see {args.baseline.name}).")
    else:
        print(f"check_platform_names: clean ({which}; {len(scanned)} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
