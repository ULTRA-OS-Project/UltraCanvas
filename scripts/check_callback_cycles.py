#!/usr/bin/env python3
"""Flag callbacks that own the widget holding them.

A widget owns its callbacks: `button->onClick = ...` stores the lambda inside
the button. So a capture that holds a `std::shared_ptr` back to that button —
or to any container above it — closes a cycle neither end ever escapes. The
refcount never reaches zero and the whole subtree, its images and its render
buffers stay allocated for the life of the process.

    auto button = CreateButton(...);
    container->AddChild(button);

    button->onClick = [button, status]() { ... };   // leaks the subtree
    button->onClick = [button = button.get(), status]() { ... };   // fine

The raw back-reference is valid for exactly as long as the callback can run,
because the thing holding the callback is the thing being pointed at. Captures
that point the other way are ownership, not a cycle, and must stay
`shared_ptr`: a popup the lambda keeps alive, a sibling label it updates, the
`make_shared` state a toggle button counts in.

53 callbacks across 25 DemoApp files had this, and the demo is the framework's
worked example, so the pattern was being copied outwards.

What this reports:

  self-capture
      A callback captures the widget it is stored on.

  ancestor-capture
      A callback captures a container that (transitively) owns that widget,
      via AddChild / AddDialogElement / AddRadioButton.

Both are reported ONLY when the capture is demonstrably a `shared_ptr` in
that scope. This matters: the first version of this check matched capture
names alone and reported three "leaks" in Texter and UltraFiler that were
already `auto* editorPtr = editor.get()` and a `T* target` parameter — raw
pointers, no ownership, nothing to fix. A name is not a type.

A file that is legitimately an exception opts out with a marker comment
naming the reason:

    // callback-cycle-exempt: <why this callback may own its owner>

Usage:
    python3 scripts/check_callback_cycles.py [--strict] [paths...]

Exits 0 when clean. Without --strict, findings are reported and the exit code
stays 0, so the check can be introduced without blocking unrelated work.
"""

from __future__ import annotations

import argparse
import functools
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Where widget-building code lives. Vendored code and build output are never
# scanned. Mirrors check_ui_reuse.py, plus SmartHome, which builds its own UI.
SEARCH_ROOTS = [
    "UltraCanvas/core",
    "UltraCanvas/include",
    "UltraCanvas/dialogs",
    "Apps",
    "SmartHome",
]
SKIP_PARTS = {"third_party", "3rdparty", "build", "cmake-build-debug"}
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp", ".mm"}

EXEMPT_RE = re.compile(r"//\s*callback-cycle-exempt\s*:\s*(?P<reason>.+)")

# `parent->AddChild(child)` and the other adders that take ownership of a
# shared_ptr child (UltraCanvasContainer.h, UltraCanvasModalDialog.h,
# UltraCanvasRadio.h).
ADDER_RE = re.compile(
    r"\b(\w+)\s*->\s*(?:AddChild|AddDialogElement|AddRadioButton)\s*\(\s*(\w+)\s*\)"
)

# `widget->onSomething = [captures]`. The framework spells its callback members
# onX (onClick, onThemeChanged, onNewTabRequest), canX (canCloseEmptyWindow) or
# somethingProvider / somethingCallback / somethingHandler.
CALLBACK_RE = re.compile(
    r"(?P<obj>\w+)\s*->\s*"
    r"(?P<member>(?:on|can)[A-Z]\w*|\w*(?:Provider|Callback|Handler))\s*=\s*"
    r"\[(?P<captures>[^\]]*)\]"
)

# Declarations that settle a name's type inside a scope.
RAW_AUTO_RE = r"\bauto\s*\*\s*{name}\s*="            # auto* x = y.get()
RAW_PARAM_RE = r"[\w>]\s*\*\s*&?\s*{name}\b\s*[,)]"  # T* x / T*& x parameter
AUTO_INIT_RE = r"\bauto\s+{name}\s*=\s*(?P<init>[^;]{{0,120}})"
SHARED_DECL_RE = r"std::shared_ptr\s*<[^>]+>\s*&?\s*{name}\b"
# A factory whose declared return type is a shared_ptr (CreateButton, ...).
FACTORY_RE = r"std::shared_ptr\s*<[^>]+>\s*{name}\s*\("
# The same, harvested from the headers, so `auto btn = CreateButton(...)` is
# recognised in a file that does not itself declare the factory. Without this
# the check only saw make_shared and went quiet on most of the framework.
FACTORY_DECL_RE = re.compile(r"std::shared_ptr\s*<[^>]+>\s*(?:\w+::)?(\w+)\s*\(")
FACTORY_HEADER_ROOTS = ["UltraCanvas/include", "Apps", "SmartHome"]


def strip_code(src: str) -> str:
    """Blank out comments and string/char literals, keeping offsets and lines.

    Brace matching and capture parsing both need this: a `{` in a comment or a
    string would otherwise shift every function boundary after it.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        ch = src[i]
        if ch == "/" and i + 1 < n:
            if src[i + 1] == "/":
                j = src.find("\n", i)
                j = n if j < 0 else j
                out.append(" " * (j - i))
                i = j
                continue
            if src[i + 1] == "*":
                j = src.find("*/", i + 2)
                j = n - 2 if j < 0 else j
                seg = src[i:j + 2]
                out.append("".join(c if c == "\n" else " " for c in seg))
                i = j + 2
                continue
        if ch in "\"'":
            quote, j = ch, i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == quote:
                    j += 1
                    break
                if src[j] == "\n":
                    break
                j += 1
            seg = src[i:j]
            out.append("".join(c if c == "\n" else " " for c in seg))
            i = j
            continue
        out.append(ch)
        i += 1
    return "".join(out)


@functools.lru_cache(maxsize=1)
def header_factories() -> frozenset[str]:
    """Names of functions declared to return a shared_ptr, across the tree."""
    names: set[str] = set()
    for rel in FACTORY_HEADER_ROOTS:
        root = REPO_ROOT / rel
        if not root.exists():
            continue
        for path in root.rglob("*.h"):
            if SKIP_PARTS & set(path.parts):
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            names.update(FACTORY_DECL_RE.findall(text))
    return frozenset(names)


# A `{` that opens a function body: a signature's `)`, possibly followed by
# trailing qualifiers (`const`, `noexcept`, `override`, a ref-qualifier) or a
# constructor's member-initialiser list.
BODY_OPEN_RE = re.compile(
    r"\)\s*(?:(?:const|noexcept|override|final|mutable|&{1,2})\s*)*"
    r"(?::[^;{}]*)?$"
)


def function_bodies(code: str) -> list[tuple[int, int]]:
    """Outermost brace blocks that are a function body, as (start, end).

    A body is a `{` preceded by a signature — not `namespace X {` or a braced
    initializer. Outermost only, so a lambda inside a builder is read as part
    of that builder rather than as a scope of its own: the widget it captures
    was declared in the enclosing function.
    """
    bodies: list[tuple[int, int]] = []
    stack: list[int] = []
    depth_of_body = None
    for i, ch in enumerate(code):
        if ch == "{":
            stack.append(i)
            if depth_of_body is None:
                before = code[max(0, i - 400):i].rstrip()
                if BODY_OPEN_RE.search(before):
                    depth_of_body = len(stack)
        elif ch == "}":
            if not stack:
                continue
            start = stack.pop()
            if depth_of_body is not None and len(stack) + 1 == depth_of_body:
                bodies.append((start, i))
                depth_of_body = None
    return bodies


def is_shared_ptr(name: str, body: str, whole: str) -> bool:
    """True only when `name` is demonstrably a shared_ptr in this scope.

    Deliberately conservative — an unknown type is not reported. A missed leak
    costs memory; a false positive costs someone a `.get()` on a raw pointer
    that will not compile, and trust in the check.
    """
    if re.search(RAW_AUTO_RE.format(name=re.escape(name)), body):
        return False
    if re.search(RAW_PARAM_RE.format(name=re.escape(name)), body):
        return False

    match = re.search(AUTO_INIT_RE.format(name=re.escape(name)), body)
    if match:
        init = match.group("init")
        if "make_shared" in init or "shared_ptr" in init:
            return True
        factory = re.match(r"\s*(?:\w+::)?(\w+)\s*\(", init)
        if factory:
            name = factory.group(1)
            if re.search(FACTORY_RE.format(name=re.escape(name)), whole):
                return True
            if name in header_factories():
                return True
        return False

    return bool(re.search(SHARED_DECL_RE.format(name=re.escape(name)), body))


def ancestors_of(name: str, parent: dict[str, str]) -> set[str]:
    seen: set[str] = set()
    cur = parent.get(name)
    while cur and cur not in seen:
        seen.add(cur)
        cur = parent.get(cur)
    return seen


class Finding:
    def __init__(self, path: Path, line: int, kind: str, owner: str,
                 capture: str, member: str):
        self.path = path
        self.line = line
        self.kind = kind
        self.owner = owner
        self.capture = capture
        self.member = member

    @property
    def rel(self) -> str:
        try:
            return self.path.relative_to(REPO_ROOT).as_posix()
        except ValueError:
            return self.path.as_posix()

    @property
    def key(self) -> str:
        """Identity that survives edits elsewhere in the file (no line number)."""
        return f"{self.rel}::{self.kind}::{self.owner}->{self.member}::{self.capture}"

    def __str__(self) -> str:
        if self.kind == "self-capture":
            what = (f"{self.owner}->{self.member} captures a shared_ptr to "
                    f"{self.owner} itself")
        else:
            what = (f"{self.owner}->{self.member} captures a shared_ptr to "
                    f"{self.capture}, which owns {self.owner}")
        return (f"{self.rel}:{self.line}: {self.kind}: {what} — capture it raw "
                f"(`{self.capture} = {self.capture}.get()`)")


def check_file(path: Path) -> list[Finding]:
    try:
        raw = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []

    if EXEMPT_RE.search(raw):
        return []
    if "->" not in raw:
        return []

    code = strip_code(raw)
    findings: list[Finding] = []

    for start, end in function_bodies(code):
        body = code[start:end]
        parent = {m.group(2): m.group(1) for m in ADDER_RE.finditer(body)}

        for match in CALLBACK_RE.finditer(body):
            owner = match.group("obj")
            owning = {owner} | ancestors_of(owner, parent)

            for entry in match.group("captures").split(","):
                entry = entry.strip()
                if not entry or entry in ("this", "=", "&"):
                    continue
                # An init-capture (`x = x.get()`, `w = weak_ptr<T>(x)`) has
                # already chosen what it holds; only a plain by-value capture
                # of the owner or an ancestor is a cycle.
                if "=" in entry:
                    continue
                entry = entry.lstrip("&")
                if entry not in owning:
                    continue
                if not is_shared_ptr(entry, body, code):
                    continue
                findings.append(
                    Finding(
                        path,
                        code.count("\n", 0, start + match.start()) + 1,
                        "self-capture" if entry == owner else "ancestor-capture",
                        owner,
                        entry,
                        match.group("member"),
                    )
                )

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
            if SKIP_PARTS & set(path.parts):
                continue
            yield path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="exit non-zero on findings (CI gate)",
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

    for finding in findings:
        print(finding)

    if findings:
        print(
            f"\n{len(findings)} callback(s) own the widget holding them, in "
            f"{scanned} files.\nCapture the back-reference raw — it is valid "
            "for as long as the callback can run, because the thing holding "
            "the callback is the thing being pointed at. Captures pointing the "
            "other way (a popup the lambda keeps alive, a sibling it updates, "
            "make_shared state) are ownership and stay shared_ptr. If this one "
            "really is an exception, add a marker naming the reason:\n"
            "    // callback-cycle-exempt: <why this callback may own its owner>"
        )
        return 1 if args.strict else 0

    print(f"check_callback_cycles: clean ({scanned} files).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
