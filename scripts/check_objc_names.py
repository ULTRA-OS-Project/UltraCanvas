#!/usr/bin/env python3
"""Flag identifiers that the Objective-C runtime takes in a .mm file.

The macOS and iOS platform glue is Objective-C++ (.mm). Whatever C++ header
such a file includes is parsed with <objc/objc.h> and Foundation in effect,
which bring two kinds of name:

  macros   YES  NO  nil  Nil            `#define YES __objc_yes`, `#define nil
                                        ((id)0)` - the preprocessor rewrites
                                        every later token of that name, so an
                                        enumerator or member called NO or nil
                                        compiles everywhere except in a .mm.

  types    id  Class  SEL  IMP  BOOL    typedefs at global scope (`typedef
           Protocol                     struct objc_object *id;`), and `id`,
                                        `Class`, `SEL` and `Protocol` are
                                        known to the parser itself. A
                                        declaration of the same name at GLOBAL
                                        scope is "redefinition of 'id' as a
                                        different kind of symbol". Inside a
                                        namespace, a class, an enum or a
                                        function the names are free - clang
                                        accepts `struct S { int id; }` and
                                        `namespace N { struct Class {}; }`,
                                        and the framework lives in namespaces.

(Checked with `clang++ -x objective-c++ -fsyntax-only`; the X11 check,
scripts/check_x11_names.py, is the same idea for the Linux window headers.)

What is reported:

  objc-macro
      An enumerator, function, method, member or variable named YES, NO, nil
      or Nil, in any scope. Pick a name that says what the value means.

  objc-global
      A declaration at file scope - outside every namespace, class and
      function - named id, Class, SEL, IMP, BOOL or Protocol: a variable, a
      function, a `struct`/`class`/`enum`, a `typedef` or a `using`. Put it
      in a namespace, or rename it.

  objc-undef
      `#undef` of one of these names: the guard is the symptom.

A site that is correct as it stands says so where it is and is skipped:

    int NO = 0;   // objc-name-ok: <why>

Sites that predate the check are listed in scripts/objc_names_baseline.txt
(empty when the check was introduced) and only shrink; a new one fails CI
(`objc-names.yml`, --strict). --self-test runs the rules against built-in
examples first.

Usage:
    python3 scripts/check_objc_names.py                 # report
    python3 scripts/check_objc_names.py --strict        # CI gate
    python3 scripts/check_objc_names.py --self-test
    python3 scripts/check_objc_names.py --update-baseline
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_x11_names import (  # noqa: E402  (shared machinery)
    DECL_RE, ENUM_RE, ENUMERATOR_RE, NOT_A_TYPE, REPO_ROOT, SEARCH_ROOTS,
    iter_sources, load_lines, strip_comments_and_strings)

MACRO_NAMES = {"YES", "NO", "nil", "Nil"}
GLOBAL_NAMES = {"id", "Class", "SEL", "IMP", "BOOL", "Protocol"}
ALL_NAMES = MACRO_NAMES | GLOBAL_NAMES

EXEMPT_RE = re.compile(r"//.*\bobjc-name-ok\b")
UNDEF_RE = re.compile(r"^\s*#\s*undef\s+([A-Za-z_]\w*)", re.MULTILINE)
# A type, typedef or alias introduced at file scope.
TYPE_DECL_RE = re.compile(r"(?:^|[^\w:])(?:struct|class|union|enum(?:\s+class|\s+struct)?|typedef\s+[^;{]*?|using)\s+"
                          r"(?P<name>[A-Za-z_]\w*)\s*(?P<after>\{|;|:|=)")

BASELINE_HEADER = """\
# Identifiers of ours that the Objective-C runtime takes in a .mm file -
# enumerators or members named YES / NO / nil / Nil, and file-scope
# declarations named id / Class / SEL / IMP / BOOL / Protocol - recorded so CI
# can block *new* ones while these are worked off. Each line is
# <path>::<kind>::<name>.
#
# Do not add to this file to silence a new finding. A site that is correct as
# it stands says so where it is:
#     int NO = 0;   // objc-name-ok: <why>
#
# Regenerate with:
#     python3 scripts/check_objc_names.py --update-baseline
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
        return f"{self.rel}::{self.kind}::{self.name}"

    def __str__(self) -> str:
        what = {
            "objc-macro": "is an Objective-C macro - rename it (the .mm files see it rewritten)",
            "objc-global": "is an Objective-C runtime type - declare it in a namespace, or rename it",
            "objc-undef": "guards an Objective-C name - rename the identifier it protects instead",
        }[self.kind]
        return f"{self.rel}:{self.line}: {self.kind}: {self.name} {what}"


def at_file_scope(code: str, pos: int) -> bool:
    """True at `pos` of `code` (comments and strings already blanked) when
    outside every namespace, class and function - brace depth 0 - and outside
    a parameter list, where a parameter named `id` is fine."""
    return (code.count("{", 0, pos) == code.count("}", 0, pos)
            and code.count("(", 0, pos) == code.count(")", 0, pos))


def check_text(text: str, path: Path) -> list[Finding]:
    findings: list[Finding] = []
    exempt = {i + 1 for i, line in enumerate(text.splitlines()) if EXEMPT_RE.search(line)}
    for m in UNDEF_RE.finditer(text):
        if m.group(1) in ALL_NAMES:
            findings.append(Finding(path, text.count("\n", 0, m.start()) + 1, "objc-undef", m.group(1)))
    code = strip_comments_and_strings(text)

    def line_of(pos: int) -> int:
        return code.count("\n", 0, pos) + 1

    enum_spans: list[tuple[int, int]] = []
    for m in ENUM_RE.finditer(code):
        i, depth = m.end(), 1
        while i < len(code) and depth:
            depth += (code[i] == "{") - (code[i] == "}")
            i += 1
        enum_spans.append((m.end(), i - 1))
        for e in ENUMERATOR_RE.finditer(code, m.end(), i - 1):
            if e.group(1) in MACRO_NAMES and line_of(e.start(1)) not in exempt:
                findings.append(Finding(path, line_of(e.start(1)), "objc-macro", e.group(1)))
    for m in DECL_RE.finditer(code):
        name = m.group("name")
        if name not in ALL_NAMES or any(a <= m.start("name") < b for a, b in enum_spans):
            continue
        type_word = re.search(r"([A-Za-z_]\w*)$", m.group("type").rstrip("*& "))
        if not type_word or type_word.group(1) in NOT_A_TYPE:
            continue
        line = line_of(m.start("name"))
        if line in exempt:
            continue
        if name in MACRO_NAMES:
            findings.append(Finding(path, line, "objc-macro", name))
        elif at_file_scope(code, m.start("name")):
            findings.append(Finding(path, line, "objc-global", name))
    for m in TYPE_DECL_RE.finditer(code):
        name = m.group("name")
        if name not in GLOBAL_NAMES:
            continue
        line = line_of(m.start("name"))
        if line not in exempt and at_file_scope(code, m.start("name")):
            findings.append(Finding(path, line, "objc-global", name))
    return findings


def check_file(path: Path) -> list[Finding]:
    try:
        return check_text(path.read_text(encoding="utf-8", errors="replace"), path)
    except OSError:
        return []


SELF_TEST = """\
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
"""
SELF_TEST_EXPECT = {("objc-undef", "nil", 2), ("objc-macro", "YES", 3), ("objc-macro", "NO", 3),
                    ("objc-macro", "Nil", 5), ("objc-global", "id", 9), ("objc-global", "BOOL", 10),
                    ("objc-global", "Class", 11), ("objc-global", "SEL", 12), ("objc-macro", "nil", 17)}


def self_test() -> int:
    got = {(f.kind, f.name, f.line) for f in check_text(SELF_TEST, Path("self-test.h"))}
    if got == SELF_TEST_EXPECT:
        print("check_objc_names: self-test ok.")
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
                        default=REPO_ROOT / "scripts" / "objc_names_baseline.txt",
                        help="file of known sites still to fix")
    parser.add_argument("--no-baseline", action="store_true",
                        help="report everything, including the known sites")
    parser.add_argument("--update-baseline", action="store_true",
                        help="rewrite the baseline from the current tree and exit")
    parser.add_argument("--self-test", action="store_true",
                        help="run the rules against built-in examples and exit")
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
        args.baseline.write_text(BASELINE_HEADER + "\n".join(keys) + ("\n" if keys else ""),
                                 encoding="utf-8")
        print(f"check_objc_names: wrote {len(keys)} entries to {args.baseline.relative_to(REPO_ROOT)}.")
        return 0

    baseline = set() if args.no_baseline else load_lines(args.baseline)
    fresh = [f for f in findings if f.key not in baseline]
    for finding in fresh:
        print(finding)
    if fresh:
        print(f"\n{len(fresh)} identifier(s) the Objective-C runtime takes, in {scanned} files.\n"
              "In a .mm file YES / NO / nil / Nil are macros and id / Class / SEL / IMP /\n"
              "BOOL / Protocol are global typedefs (AGENTS.md). Rename the enumerator or\n"
              "member; put the file-scope declaration in a namespace. If this site is\n"
              "correct as it stands, say why and it will be skipped:\n"
              "    int NO = 0;   // objc-name-ok: <why>")
        return 1 if args.strict else 0

    known = len(findings)
    if known:
        print(f"check_objc_names: no new findings ({scanned} files; {known} baselined "
              f"site{'' if known == 1 else 's'} still to fix - see {args.baseline.name}).")
    else:
        print(f"check_objc_names: clean ({scanned} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
