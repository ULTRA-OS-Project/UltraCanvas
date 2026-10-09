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
          PathFromUtf8(dir) / accountId     p /= folder;
      Use PathFromUtf8(str). A `/` join counts when the chain holds a path:
      a PathFromUtf8(...) or fs::path(...), a name declared as a path (an
      `auto` initialised from PathFromUtf8 included), or a path accessor
      such as .parent_path() or temp_directory_path(). Each operand of that
      chain that is a UTF-8 string is reported - a name declared as a
      std::string, `.c_str()`, or a call to a function the file declares as
      returning std::string (`/ SanitizeFolder(folder)`), or a parenthesised
      sum with such a term (`/ (baseName + " (2)")`). A bare literal
      (`/ "mail"`) and a sum of literals and std::to_string are ASCII and
      fine. Assigning a string to a path declared earlier (`dir =
      currentPath;`) and a `cond ? s1 : s2` with a string branch count too.
      The argument counts as a string when it is `.c_str()`, a `+` concatenation with a literal or a string, or a name
      whose nearest declaration above the use, in the same file, is a
      std::string - the check has no types, so it reads the declarations;
      for a call, the declaration of the function: a call to a function
      declared as returning std::string is a string wherever it is handed to
      a path (`fs::exists(DeviceKeyPath(), ec)`). The declarations read are
      the file's own and those of the repository headers it includes
      directly - where a class declares its members (`std::string dir_;`)
      and its functions - with the bodies of inline functions left out, so a
      header's locals lend their types to nothing. A member access
      (`env.accountId`, `msg->folder`) is also looked up in the headers
      those headers include, transitively: the struct is usually declared a
      header or two further down. There a header is also found by its file
      name when exactly one header in the repository has it, and a member
      name the headers declare two different ways is left alone. A call that runs on over
      several lines is read whole. (PathFromUtf8 also takes a path, a C
      string and a string_view, so wrapping is never wrong.)

  fopen-narrow
      `fopen(name, mode)` with anything but a literal: the narrow fopen reads
      the name in the Windows code page. Use OpenFileUtf8(name, mode).

  env-narrow
      A narrow read of the environment where Windows answers in the ANSI code
      page: `getenv` / `secure_getenv` / `_dupenv_s` of a variable Windows
      keeps a path or the user's name in (APPDATA, LOCALAPPDATA, USERPROFILE,
      TEMP, ProgramFiles, SystemRoot, USERNAME ...), any `_dupenv_s` or
      `getenv(name)` in Windows-only code, and a call with such a name to a
      helper of the same file whose body reads narrowly
      (`EnvOrEmpty("LOCALAPPDATA")`). The value is not UTF-8 to begin with,
      so wrapping it in PathFromUtf8 afterwards is no fix - the rules above
      see a correct-looking call. Use GetEnvUtf8(name), which asks
      GetEnvironmentVariableW and converts. `_wgetenv` is wide and not
      reported.

What is still NOT reported: a string whose type neither the file nor a
header it includes directly spells out (an `auto`, a member or getter of a
class declared further away, such as `env.accountId`) handed to a path
parameter or joined onto a path, and the declaration form `fs::path p(str);`
with such a string. Nor is any other string that is not UTF-8 to begin with
- an ...A Win32 call (GetVolumeInformationA, GetModuleFileNameA) answers in
the ANSI code page; call the ...W one and convert with PathToUtf8 or
Utf16ToUtf8. Review catches those.

`--self-test` runs the header-aware rules against built-in examples; CI runs
it before the scan.
The two implicit kinds skip the Linux, macOS, Android, WASM and ULTRA OS
platform folders, where a path's native string is the UTF-8 bytes.

A site that is deliberately left alone says why and is skipped:

    return p.string();   // path-string-ok: <why>

`scripts/path_string_baseline.txt` lists sites that are wrong and not fixed
yet, so CI blocks new ones while these are worked off. It should be empty.

Usage:
    python3 scripts/check_path_string.py [--strict] [paths...]
    python3 scripts/check_path_string.py --self-test
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
    # Runs on Windows too (ULTRACANVAS_BUILD_VAULT_TESTS in build.yml).
    "Tests/UltraVaultTests.cpp",
    # Run on Windows too (ULTRACANVAS_BUILD_FILER_TESTS in build.yml, the
    # "Run Filer tests (Windows)" step - keep this list and that one equal).
    "Tests/FilerFolderPreviewTest.cpp",
    "Tests/FilerHistoryTest.cpp",
    "Tests/FilerHostIconsTest.cpp",
    "Tests/FilerNameEncodingTest.cpp",
    "Tests/FilerShortcutEntryTest.cpp",
]
# The rest of Tests/ is not scanned: the framework test suite builds on Linux
# only (BUILD_TESTS in build.yml), where a path's native string is the UTF-8
# bytes and `.string()` is exact. A test that starts running on Windows should
# be added here and converted, as UltraVaultTests.cpp was.
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
PATH_DECL_RE = re.compile(
    r"\b(?:std::filesystem|fs|filesystem)::path\s*[&*]?\s*(\w+)\s*(?=[;=,\)\{\(\[])")
# `auto p = PathFromUtf8(...)` / `auto p = fs::path(...)`: a path too
AUTO_PATH_DECL_RE = re.compile(
    r"\bauto\s*[&*]?\s*(\w+)\s*=\s*(?:UltraCanvas::)?(?:PathFromUtf8|"
    r"(?:std::filesystem|fs|filesystem)::(?:path|temp_directory_path|current_path))\s*\(")
# Declarations that make a name neither a string nor a path, so an older
# std::string of the same name further up no longer applies: a C string, a
# range-for variable (`for (const auto& part : rel)`, `for (const char* n :
# {...})`) and any other `auto`. A range-for over std::string stays a string.
OTHER_DECL_RES = (
    re.compile(r"\b(?:const\s+)?char\s*(?:const\s*)?\*\s*(?:const\s+)?(\w+)\s*(?=[;=,\)\{\(\[:])"),
    re.compile(r"\bfor\s*\(\s*(?:const\s+)?(?!(?:std::)?string\b)[\w:<>]+\s*[&*]*\s*(\w+)\s*:(?!:)"),
    re.compile(r"\bauto\s*[&*]*\s*(\w+)\s*="),
)
RANGE_FOR_STRING_RE = re.compile(
    r"\bfor\s*\(\s*(?:const\s+)?(?:std::)?string\s*[&*]?\s*(\w+)\s*:(?!:)")
STREAM_DECL_RE = re.compile(r"\bstd::(?:i|o)?fstream\s+(\w+)")
FS_CALL_RE = re.compile(
    r"(?<![\w:])(?:std::filesystem|fs|filesystem)::(\w+)\s*(\w+\s*)?([\({])")
STREAM_CTOR_RE = re.compile(r"\bstd::(?:i|o)?fstream\s*(\w+\s*)?([\({])")
PATH_ASSIGN_RE = re.compile(
    r"(?<![\w:])(?:std::filesystem|fs|filesystem)::path\s+\w+\s*=\s*([^;]+);")
FOPEN_RE = re.compile(r"(?<![\w.>])(?:std::|::)?fopen\s*\(")
ID_CHAIN_RE = re.compile(r"^[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*$")
# A call: name (qualified, or through a member chain), then one argument list
CALL_RE = re.compile(r"^(?P<name>[A-Za-z_][\w:]*(?:(?:\.|->)[A-Za-z_]\w*)*)\s*\((?P<args>.*)\)$")
# Calls whose result is a std::filesystem::path
PATH_CALLS = {"PathFromUtf8", "path", "parent_path", "filename", "stem",
              "extension", "root_path", "root_name", "root_directory",
              "relative_path", "lexically_normal", "lexically_relative",
              "replace_extension", "replace_filename", "remove_filename",
              "temp_directory_path", "current_path", "absolute", "canonical",
              "weakly_canonical", "read_symlink"}
# A `/` that is a binary operator: not `//`, `/*`, `*/` or `/=`
JOIN_RE = re.compile(r"(?<![/*])/(?![/*=])")
JOIN_ASSIGN_RE = re.compile(r"(?P<lhs>[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*)\s*/=(?P<rhs>[^;]+);")


# ---- env-narrow ------------------------------------------------------------
# Environment variables Windows keeps paths (and the user's name) in. Read
# with the narrow getenv / _dupenv_s they come back in the ANSI code page, so
# the characters it lacks are '?' - and wrapping those bytes in PathFromUtf8
# afterwards does not bring them back, which is why the path rules above
# cannot see this. GetEnvUtf8 (UltraCanvasPathUtf8.h) asks the process for
# the UTF-16 value (GetEnvironmentVariableW) and returns it as UTF-8, the
# form every consumer here takes. _wgetenv is wide and not reported; the
# codebase still prefers GetEnvUtf8, which reads the live environment rather
# than the C runtime's copy and needs no conversion at the call.
WIN_ENV_NAMES = {
    "APPDATA", "LOCALAPPDATA", "USERPROFILE", "PROGRAMDATA", "ProgramData",
    "ALLUSERSPROFILE", "PUBLIC", "HOMEDRIVE", "HOMEPATH", "TEMP", "TMP",
    "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "CommonProgramFiles",
    "SystemRoot", "SYSTEMROOT", "WINDIR", "windir", "ComSpec", "USERNAME",
    "OneDrive", "OneDriveConsumer", "OneDriveCommercial"}
NARROW_ENV_RE = re.compile(
    r"(?<![\w.>])(?:std::|::)?(?P<fn>getenv|secure_getenv|_dupenv_s)\s*\(")
# A literal argument, in the line with its literals kept
LITERAL_ARG_RE = re.compile(r'\s*(?:L|u8|u)?"(?P<name>[^"]*)"')
WIN_COND_RE = re.compile(r"\b(?:_WIN32|_WIN64|_MSC_VER|__MINGW32__|__MINGW64__|_WINDOWS)\b")
WINDOWS_ONLY_PARTS = {"MSWindows", "Windows", "Win32"}


def clean_lines(lines: list[str]) -> list[str]:
    """Each line with its literals blanked and its comments removed. One line
    at a time, comments out after the literals: run over a whole file at once,
    an apostrophe in a comment ("don't") pairs with one many lines further on
    and the code between them vanishes."""
    out, in_block = [], False
    for line in lines:
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


def windows_regions(lines: list[str], windows_file: bool) -> list[bool]:
    """For each line, whether it is compiled only on Windows: inside the
    Windows branch of an #if on _WIN32 / _WIN64 / _MSC_VER, or anywhere in a
    file of a Windows platform folder."""
    stack: list[str] = []   # per #if level: "win", "notwin" or "other"
    out = []
    for line in lines:
        t = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", t)
        if m:
            kind, cond = m.group(1), m.group(2)
            win = bool(WIN_COND_RE.search(cond))
            negated = kind == "ifndef" or re.search(r"!\s*defined\s*\(?\s*_WIN", cond)
            if kind in ("if", "ifdef", "ifndef"):
                stack.append(("notwin" if negated else "win") if win else "other")
            elif kind == "elif" and stack:
                stack[-1] = ("notwin" if negated else "win") if win else (
                    "notwin" if stack[-1] == "win" else stack[-1])
            elif kind == "else" and stack:
                stack[-1] = {"win": "notwin", "notwin": "win"}.get(stack[-1], "other")
            elif kind == "endif" and stack:
                stack.pop()
            out.append(False)
            continue
        out.append(windows_file and "notwin" not in stack or
                   ("win" in stack and "notwin" not in stack))
    return out


def narrow_env_helpers(text: str) -> set[str]:
    """Names of the functions this file defines whose body reads the
    environment narrowly: a call to one of them with a Windows variable's
    name is the same mistake one step removed."""
    helpers = set()
    clean = "\n".join(clean_lines(text.splitlines()))
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\([^;{}()]*(?:\([^()]*\)[^;{}()]*)*\)\s*"
                         r"(?:const\s*)?(?:noexcept\s*)?\{", clean):
        close = _close_of(clean, m.end() - 1)
        if close > 0 and NARROW_ENV_RE.search(clean[m.end():close]):
            helpers.add(m.group(1))
    return helpers - {"if", "for", "while", "switch", "catch", "return"}


def env_findings(path: Path, number: int, code: str, raw: str, in_windows: bool,
                 helpers: set[str]) -> list["Finding"]:
    found = []
    for m in NARROW_ENV_RE.finditer(code):
        fn = m.group("fn")
        lit = LITERAL_ARG_RE.match(raw, m.end()) if len(raw) == len(code) else None
        name = lit.group("name") if lit else None
        if name in WIN_ENV_NAMES:
            why = f"{fn}(\"{name}\") answers in the Windows ANSI code page"
        elif fn == "_dupenv_s" and in_windows:
            why = f"{fn} answers in the Windows ANSI code page"
        elif in_windows and not lit:
            why = f"{fn} in Windows code answers in the ANSI code page"
        else:
            continue
        found.append(Finding(
            path, number, "env-narrow",
            f"{why}, so a profile folder or user name outside it comes back "
            f"with '?' in it - use GetEnvUtf8({name and repr(name).replace(chr(39), chr(34)) or 'name'}) "
            f"(UltraCanvasPathUtf8.h), which asks GetEnvironmentVariableW",
            symbol=fn))
    if helpers:
        for m in re.finditer(r"(?<![\w:.>])([A-Za-z_]\w*)\s*\(", code):
            if m.group(1) not in helpers or len(raw) != len(code):
                continue
            lit = LITERAL_ARG_RE.match(raw, m.end())
            if lit and lit.group("name") in WIN_ENV_NAMES:
                found.append(Finding(
                    path, number, "env-narrow",
                    f"{m.group(1)}(\"{lit.group('name')}\") reads the environment "
                    f"through a narrow getenv - make {m.group(1)} use GetEnvUtf8 "
                    f"(UltraCanvasPathUtf8.h)", symbol=m.group(1)))
    return found


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


def _top_level_index(text: str, ch: str) -> int:
    depth = 0
    for i, c in enumerate(text):
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == ch and depth == 0 and not (ch == ":" and (
                text[i + 1:i + 2] == ":" or text[i - 1:i] == ":")):
            return i
    return -1


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
# Where an `#include <Module/Header.h>` is looked for, besides the including
# file's own folder.
INCLUDE_ROOTS = ["UltraCanvas/include", "UltraCanvas/core", "UltraCloud/include",
                 "UltraAI/include", "VirtualFS/include", "SmartHome/include", "VideoFX/include"]
_header_cache: dict[Path, str] = {}


def _strip_function_bodies(text: str) -> str:
    """`text` with the body of every function defined in it removed: a brace
    block opened right after a parameter list (`) {`, `) const {`,
    `) override {` ...). Class, struct, namespace and initialiser braces stay,
    so what remains is what the header declares, not an inline function's
    locals."""
    out, i, n = [], 0, len(text)
    qualifier = re.compile(r"\)\s*(?:const|noexcept|override|final|mutable|"
                           r"->\s*[\w:<>,\s&*]+?|\s)*$")
    while i < n:
        j = text.find("{", i)
        if j < 0:
            out.append(text[i:])
            break
        head = text[max(i, j - 200):j]
        if qualifier.search(head):
            close = _close_of(text, j)
            if close < 0:
                out.append(text[i:])
                break
            out.append(text[i:j] + ";")
            i = close + 1
        else:
            out.append(text[i:j + 1])
            i = j + 1
    return "".join(out)


def included_headers(path: Path, text: str) -> tuple[str, ...]:
    """The text of the repository headers `text` includes directly, function
    bodies removed. A header that cannot be found unambiguously is left out:
    a wrong header would lend its declarations to the wrong names."""
    found = []
    for name in INCLUDE_RE.findall(text):
        candidates = [path.parent / name] + [REPO_ROOT / root / name for root in INCLUDE_ROOTS]
        target = next((c for c in candidates if c.is_file()), None)
        if target is None or target.resolve() == path.resolve():
            continue
        target = target.resolve()
        if SKIP_PARTS & set(target.parts) or target.name in SKIP_NAMES:
            continue
        if target not in _header_cache:
            try:
                raw = target.read_text(encoding="utf-8", errors="replace")
            except OSError:
                raw = ""
            # Comments and literals out first, so neither opens a brace.
            _header_cache[target] = _strip_function_bodies(
                    "\n".join(clean_lines(raw.splitlines())))
        found.append(_header_cache[target])
    return tuple(found)


MAX_MEMBER_HEADERS = 400      # per file: a deep include chain is cut here
# The declaration rules, in DeclaredTypes' order (None = neither a string nor
# a path, True = std::string, False = path)
DECL_RULES = [(rx, None) for rx in OTHER_DECL_RES] + [
    (STRING_DECL_RE, True), (RANGE_FOR_STRING_RE, True),
    (PATH_DECL_RE, False), (AUTO_PATH_DECL_RE, False)]
_header_index: dict[str, list[Path]] | None = None
_header_includes: dict[Path, list[str]] = {}
_header_members: dict[Path, dict[str, frozenset]] = {}
_closure_cache: dict[tuple, tuple] = {}


def _unique_header(name: str) -> Path | None:
    """The one repository header called `Path(name).name` whose path ends in
    `name` - None when there is none or more than one."""
    global _header_index
    if _header_index is None:
        _header_index = {}
        for root in SEARCH_ROOTS:
            base = REPO_ROOT / root
            if not base.exists():
                continue
            for f in base.rglob("*.h*"):
                if f.suffix in (".h", ".hpp") and not (SKIP_PARTS & set(f.parts)):
                    _header_index.setdefault(f.name, []).append(f)
    want = "/" + name.replace("\\", "/").lstrip("./")
    hits = [f for f in _header_index.get(Path(name).name, ())
            if ("/" + f.as_posix()).endswith(want)]
    return hits[0].resolve() if len(hits) == 1 else None


_resolved: dict[tuple, Path | None] = {}


def _member_header(path: Path, name: str) -> Path | None:
    key = (path.parent, name, path.name)
    if key not in _resolved:
        _resolved[key] = _resolve_member_header(path, name)
    return _resolved[key]


def _resolve_member_header(path: Path, name: str) -> Path | None:
    candidates = [path.parent / name] + [REPO_ROOT / root / name for root in INCLUDE_ROOTS]
    target = next((c for c in candidates if c.is_file()), None)
    target = target.resolve() if target is not None else _unique_header(name)
    if target is None or target == path.resolve():
        return None
    if SKIP_PARTS & set(target.parts) or target.name in SKIP_NAMES:
        return None
    return target


def _members_of(target: Path) -> dict[str, frozenset]:
    """name -> the kinds `target` declares it as (read once per run)."""
    if target not in _header_members:
        kinds: dict[str, set] = {}
        for rx, kind in DECL_RULES:
            for m in rx.finditer(_header_cache[target]):
                kinds.setdefault(m.group(1), set()).add(kind)
        _header_members[target] = {n: frozenset(k) for n, k in kinds.items()}
    return _header_members[target]


def member_headers(path: Path, text: str) -> dict[str, frozenset]:
    """What the repository headers `text` includes, directly or through
    other headers, declare (name -> every kind given) - read only for member
    accesses (`env.accountId`), whose struct is often a few includes away.
    Merged once per set of includes and shared, not copied."""
    key = (path.parent, tuple(INCLUDE_RE.findall(text)))
    if key in _closure_cache:
        return _closure_cache[key]
    queue = [(path, n) for n in INCLUDE_RE.findall(text)]
    seen: set[Path] = set()
    found = []
    while queue and len(found) < MAX_MEMBER_HEADERS:
        source, name = queue.pop(0)
        target = _member_header(source, name)
        if target is None or target in seen:
            continue
        seen.add(target)
        if target not in _header_cache or target not in _header_includes:
            try:
                raw = target.read_text(encoding="utf-8", errors="replace")
            except OSError:
                raw = ""
            _header_includes[target] = INCLUDE_RE.findall(raw)
            if target not in _header_cache:
                _header_cache[target] = _strip_function_bodies(
                        "\n".join(clean_lines(raw.splitlines())))
        found.append(_members_of(target))
        queue.extend((target, n) for n in _header_includes[target])
    merged: dict[str, frozenset] = {}
    for declared in found:
        for name, kinds in declared.items():
            merged[name] = merged.get(name, frozenset()) | kinds
    _closure_cache[key] = merged
    return merged


class DeclaredTypes:
    """Which names the file declares as std::string and which as a path, by
    line. A name means whatever its nearest declaration above the use says:
    `path` can be a std::string parameter in one function and an fs::path
    member or local in the next, and only the nearer one is in scope."""

    def __init__(self, text: str, headers: tuple[str, ...] = (),
                 members: dict | None = None):
        self.decls: dict[str, list[tuple[int, bool]]] = {}
        # Member names -> every kind the headers (all the way down) give them,
        # shared between files; the file's own kinds are kept beside it.
        self.members: dict[str, frozenset] = members or {}
        self.own_kinds: dict[str, set] = {}
        starts = [0]
        for m in re.finditer("\n", text):
            starts.append(m.end())
        import bisect
        # Kinds: True = std::string, False = path, None = something else.
        # Order matters only within a line: a later kind on the same line
        # (an `auto p = PathFromUtf8(...)` is also an `auto`) wins.
        rules = [(rx, None) for rx in OTHER_DECL_RES] + [
            (STRING_DECL_RE, True), (RANGE_FOR_STRING_RE, True),
            (PATH_DECL_RE, False), (AUTO_PATH_DECL_RE, False)]
        # The repository headers the file includes declare its class's
        # members and functions: `std::string dir_;` and `std::string
        # DeviceKeyPath() const;` live in the header, and the .cpp alone gave
        # no type for `fs::exists(DeviceKeyPath())` or
        # `fs::create_directories(dir_)`. They count as declared above line 1,
        # so anything the file declares itself still wins from its line on.
        # Only what a header says at namespace or class level is read: a
        # function body in a header has locals of its own.
        for header in headers:
            for order, (rx, kind) in enumerate(rules):
                for m in rx.finditer(header):
                    self.decls.setdefault(m.group(1), []).append((0, order, kind))
        for order, (rx, kind) in enumerate(rules):
            for m in rx.finditer(text):
                line = bisect.bisect_right(starts, m.start(1))
                self.decls.setdefault(m.group(1), []).append((line, order, kind))
        for name, v in self.decls.items():
            v.sort()
            self.decls[name] = [(line, kind) for line, _, kind in v]
            self.own_kinds[name] = {k for _, k in self.decls[name]}
        self.line = 0

    def member_is_string(self, name: str) -> bool:
        """A member access `x.name`: every declaration of `name` the headers
        or the file hold is a std::string."""
        return set(self.members.get(name, ())) | self.own_kinds.get(name, set()) == {True}

    def member_is_path(self, name: str) -> bool:
        return set(self.members.get(name, ())) | self.own_kinds.get(name, set()) == {False}

    def is_path(self, name: str) -> bool:
        """Whether `name`'s nearest declaration above the use is a path."""
        best, self._seen = None, False
        for line, is_string in self.decls.get(name, ()):
            if line > self.line:
                break
            best, self._seen = is_string, True
        if not self._seen:
            kinds = {k for _, k in self.decls.get(name, ())}
            return kinds == {False}
        return best is False

    def __contains__(self, name: str) -> bool:
        best, seen = None, False
        for line, is_string in self.decls.get(name, ()):
            if line > self.line:
                break
            best, seen = is_string, True
        if not seen:   # used above any declaration (a member, say)
            kinds = {k for _, k in self.decls.get(name, ())}
            return kinds == {True}
        return best is True


def _is_utf8_string(arg: str, strings, raw: str) -> bool:
    """Whether `arg` (string literals blanked) is a narrow UTF-8 string.
    `raw` is the same text with the literals kept, to tell L"" from ""."""
    a = arg.strip()
    if not a or "PathFromUtf8" in a or raw.strip().startswith(("L\"", "u8\"", "u\"", "U\"")):
        return False
    if a.endswith(".c_str()"):
        return True
    q = _top_level_index(a, "?")
    if q >= 0:   # cond ? x : y - a string when either branch is one
        c = _top_level_index(a[q + 1:], ":")
        if c >= 0:
            return any(_is_utf8_string(b, strings, b)
                       for b in (a[q + 1:q + 1 + c], a[q + 2 + c:]))
    if ID_CHAIN_RE.match(a):
        last = re.split(r"\.|->", a)[-1]
        if last in strings:
            return True
        return ("." in a or "->" in a) and hasattr(strings, "member_is_string") \
            and strings.member_is_string(last)
    if a.startswith(("std::string(", "PathToUtf8(")):
        return True
    m = CALL_RE.match(a)
    if m and strings:
        # A call to a function the file - or a header it includes - declares
        # as returning std::string: `fs::exists(DeviceKeyPath(), ec)`.
        name = re.split(r"::|\.|->", m.group("name"))[-1]
        if name not in PATH_CALLS and name in strings:
            return True
    if _top_level(a, "+") and not _top_level(a, "/"):
        for op in (o.strip() for o in a.split("+")):
            if op.startswith('"'):
                return True
            if ID_CHAIN_RE.match(op) and re.split(r"\.|->", op)[-1] in strings:
                return True
    return False


def _operand_before(code: str, i: int) -> int:
    """Start of the operand that ends just before code[i] (a `/`)."""
    j = i - 1
    while j >= 0 and code[j] == " ":
        j -= 1
    if j < 0:
        return i
    if code[j] == ")":
        depth = 0
        while j >= 0:
            if code[j] == ")":
                depth += 1
            elif code[j] == "(":
                depth -= 1
                if depth == 0:
                    break
            j -= 1
        if j < 0:
            return i
        j -= 1
        while j >= 0 and code[j] == " ":
            j -= 1
    while j >= 0 and (code[j].isalnum() or code[j] in "_:.>-"):
        if code[j] == ">" and not (j > 0 and code[j - 1] == "-"):
            break
        if code[j] == "-" and not (j + 1 < len(code) and code[j + 1] == ">"):
            break
        j -= 1
    return j + 1


def _operand_after(code: str, i: int) -> int:
    """End (exclusive) of the operand that starts just after code[i]."""
    j = i + 1
    while j < len(code) and code[j] == " ":
        j += 1
    if j < len(code) and code[j] == "(":
        close = _close_of(code, j)
        return close + 1 if close >= 0 else j
    if j < len(code) and code[j] == '"':
        k = code.find('"', j + 1)
        return k + 1 if k > 0 else j
    while j < len(code) and (code[j].isalnum() or code[j] in "_:.") or \
            (j + 1 < len(code) and code[j:j + 2] == "->"):
        j += 2 if code[j:j + 2] == "->" else 1
    if j < len(code) and code[j] == "(":
        close = _close_of(code, j)
        return close + 1 if close >= 0 else j
    return j


def _is_path_operand(op: str, strings) -> bool:
    op = op.strip()
    if ID_CHAIN_RE.match(op):
        last = re.split(r"\.|->", op)[-1]
        return strings.is_path(last) or (("." in op or "->" in op)
                                          and strings.member_is_path(last))
    m = CALL_RE.match(op)
    if m:
        return re.split(r"::|\.|->", m.group("name"))[-1] in PATH_CALLS
    return False


def _is_string_operand(op: str, strings) -> bool:
    op = op.strip()
    if op.startswith("(") and _close_of(op, 0) == len(op) - 1:
        # A parenthesised sum is a string when one of its terms is a string
        # variable or call: `/ (baseName + " (2)")`. Literals and
        # std::to_string alone are ASCII (`/ (std::to_string(uid) + ".eml")`).
        inner = op[1:-1]
        if _top_level(inner, "+"):
            return any(_is_string_operand(t, strings) for t in _split_top(inner, "+")
                       if not t.strip().startswith(('"', "std::to_string")))
        return _is_string_operand(inner, strings)
    if not op or op.startswith('"'):
        return False   # a literal is ASCII here
    if _is_utf8_string(op, strings, op):
        return True
    m = CALL_RE.match(op)
    if m:
        name = re.split(r"::|\.|->", m.group("name"))[-1]
        return name not in PATH_CALLS and name in strings
    return False


def _split_top(text: str, ch: str) -> list[str]:
    parts, depth, cur = [], 0, ""
    for c in text:
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == ch and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += c
    parts.append(cur)
    return parts


PATH_REASSIGN_RE = re.compile(
    r"(?:^|[;{}]|\belse\b|\))\s*(?P<lhs>[A-Za-z_]\w*)\s*=(?!=)\s*(?P<rhs>[^;]+);")


def join_findings(path: Path, number: int, code: str, strings,
                  raw: str = "") -> list[Finding]:
    """`/` chains holding a path, and an operand that is a UTF-8 string.
    `raw` is the line with its literals kept, for the message."""
    found: list[Finding] = []
    shown = raw if len(raw) == len(code) else code
    slashes = [m.start() for m in JOIN_RE.finditer(code)]
    seen: set[int] = set()
    for i in slashes:
        if i in seen:
            continue
        # Walk the chain this slash belongs to, left to right.
        start = _operand_before(code, i)
        operands = [(start, i)]
        chain = [i]
        k = i
        while True:
            end = _operand_after(code, k)
            operands.append((k + 1, end))
            nxt = end
            while nxt < len(code) and code[nxt] == " ":
                nxt += 1
            if nxt < len(code) and nxt in slashes:
                chain.append(nxt)
                k = nxt
                continue
            break
        seen.update(chain)
        texts = [code[a:b] for a, b in operands]
        if not any(_is_path_operand(t, strings) for t in texts):
            continue
        for (a, b), t in zip(operands, texts):
            if _is_string_operand(t, strings):
                t = shown[a:b].strip()
                inner = t[1:-1] if t.startswith("(") and _close_of(t, 0) == len(t) - 1 else t
                found.append(Finding(
                    path, number, "path-implicit",
                    f"`/` joins {t} onto a path, and a UTF-8 std::string "
                    f"converts through the Windows code page - wrap it: "
                    f"PathFromUtf8({inner})",
                    symbol="operator/"))
    for m in PATH_REASSIGN_RE.finditer(code):
        # `p = str;` to a path declared earlier (the declaration form is
        # path-implicit's `fs::path p = str;`)
        if strings.is_path(m.group("lhs")) and _is_string_operand(m.group("rhs"), strings):
            found.append(Finding(
                path, number, "path-implicit",
                f"assigning {m.group('rhs').strip()} to the path {m.group('lhs')} converts "
                f"through the Windows code page - use PathFromUtf8(...)",
                symbol="path=assign"))
    for m in JOIN_ASSIGN_RE.finditer(code):
        if _is_path_operand(m.group("lhs"), strings) and \
                _is_string_operand(m.group("rhs"), strings):
            found.append(Finding(
                path, number, "path-implicit",
                f"`/=` appends {m.group('rhs').strip()} to a path through the "
                f"Windows code page - wrap it: PathFromUtf8(...)",
                symbol="operator/="))
    return found


def implicit_findings(path: Path, number: int, code: str, raw: str,
                      strings, streams: set[str]) -> list[Finding]:
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
    found.extend(join_findings(path, number, code, strings, raw))
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
    strings = (DeclaredTypes(text, included_headers(path, text), member_headers(path, text))
               if implicit else set())
    streams = set(STREAM_DECL_RE.findall(text)) if implicit else set()

    # Each line with its literals blanked and its comments removed, first, so
    # a call that runs on over several lines can be read whole below.
    lines = text.splitlines()
    cleaned: list[str | None] = [None if EXEMPT_RE.search(line) else code
                                 for line, code in zip(lines, clean_lines(lines))]

    windows_file = bool(WINDOWS_ONLY_PARTS & set(path.parts)) or "Windows" in path.stem
    regions = windows_regions(lines, windows_file)
    helpers = narrow_env_helpers(text)

    for number, line in enumerate(lines, start=1):
        code = cleaned[number - 1]
        if code is None:
            continue

        raw_full = line.split("//", 1)[0]
        findings.extend(env_findings(path, number, code,
                                     raw_full if len(raw_full) == len(code) else code,
                                     regions[number - 1], helpers))

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
            strings.line = number
            raw = line.split("//", 1)[0]
            raw = raw if len(raw) == len(code) else code
            # A call left open at the end of the line is read together with
            # the lines that close it - `fs::create_directories(\n
            # GetConfigDirectory(), ec);` was invisible one line at a time.
            # The finding stays on the line the call starts on; a line taken
            # in this way still gets its own pass, but holds no whole call.
            depth = code.count("(") - code.count(")")
            k = number
            while depth > 0 and k < len(lines) and k - number < 8:
                more = cleaned[k]
                if more is None:
                    break
                code += " " + more.strip()
                raw += " " + more.strip()
                depth += more.count("(") - more.count(")")
                k += 1
            findings.extend(implicit_findings(path, number, code, raw, strings, streams))

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


# --self-test: what the header-aware and env-narrow rules must and must not
# report. A line
# ending in "// expect" has to be flagged; every other line must not be. The
# header's inline function keeps a std::string local that must not leak into
# the .cpp (`fs::exists(local, ec)`), and a path member stays a path.
SELF_TEST_TYPES = """\
#pragma once
#include <filesystem>
#include <string>
struct Envelope {
    std::string accountId;
    std::filesystem::path root;
};
"""
SELF_TEST_HEADER = """\
#pragma once
#include <filesystem>
#include <string>
#include "Types.h"
class Store {
public:
    std::string ConfigPath() const;
    std::string KeyPath() const { std::string local = "x"; return local; }
    void Run();
private:
    std::string dir_;
    std::filesystem::path root_;
};
"""
SELF_TEST_SOURCE = """\
#include "Store.h"
#include <fstream>
namespace fs = std::filesystem;
void Store::Run() {
    std::error_code ec;
    fs::exists(ConfigPath(), ec);                         // expect
    fs::create_directories(dir_, ec);                     // expect
    std::ifstream in(KeyPath());                          // expect
    fs::permissions(KeyPath(),                            // expect
                    fs::perms::owner_read, fs::perm_options::replace, ec);
    fs::create_directories(
        dir_, ec);                                        
    fs::exists(root_, ec);
    fs::exists(PathFromUtf8(ConfigPath()), ec);
    fs::exists(local, ec);
    const fs::path p = PathFromUtf8(dir_) / "mail";
    std::ofstream out(PathFromUtf8(KeyPath()));
    const char* appData = std::getenv("APPDATA");         // expect
    const wchar_t* temp = _wgetenv(L"TEMP");               // wide: not this rule
    const char* home = std::getenv("HOME");
    const std::string profile = GetEnvUtf8("USERPROFILE");
    const std::string localAppData = EnvOr("LOCALAPPDATA"); // expect
    const std::string xdg = EnvOr("XDG_CONFIG_HOME");
}
std::string EnvOr(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}
#ifdef _WIN32
std::string WindowsOnly(const char* name) {
    char* value = nullptr; size_t n = 0;
    _dupenv_s(&value, &n, name);                          // expect
    return std::getenv(name) ? "" : "";                   // expect
}
#else
void Members(const Envelope& env, std::error_code& ec) {
    fs::exists(env.accountId, ec);                        // expect
    fs::exists(PathFromUtf8(env.accountId), ec);
    fs::exists(env.root, ec);
    const fs::path p = PathFromUtf8("cache") / env.accountId; // expect
}
std::string PosixOnly(const char* name) { return std::getenv(name); }
#endif
"""
# The call that opens on the `fs::create_directories(` line and closes on the
# next is reported on the line it starts on.
SELF_TEST_MULTILINE_LINE = 11


def self_test() -> int:
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "Store.h").write_text(SELF_TEST_HEADER, encoding="utf-8")
        (root / "Types.h").write_text(SELF_TEST_TYPES, encoding="utf-8")
        source = root / "Store.cpp"
        source.write_text(SELF_TEST_SOURCE, encoding="utf-8")
        got = sorted({f.line for f in check_file(source)})
    want = sorted({n for n, l in enumerate(SELF_TEST_SOURCE.splitlines(), start=1)
                   if l.rstrip().endswith("// expect")} | {SELF_TEST_MULTILINE_LINE})
    if got != want:
        print(f"check_path_string --self-test: FAILED - flagged lines {got}, "
              f"expected {want}")
        return 1
    print(f"check_path_string --self-test: ok ({len(want)} findings where expected, "
          f"none elsewhere)")
    return 0


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
    parser.add_argument("--self-test", action="store_true",
                        help="check the rules against built-in examples and exit")
    parser.add_argument("paths", nargs="*",
                        help="files or directories to scan (default: the source roots)")
    args = parser.parse_args()
    if args.self_test:
        return self_test()

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
