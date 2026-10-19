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

"Stored on" is either form the framework uses: an assignment to a callback
member (`x->onClick = [...]`, `x->fooCallback = [...]`) or a lambda handed
straight to a setter on the object (`x->SetOnClick([...])`,
`x->SetValueFormatter([...])`, `x->AddListener([...])`). "Captures" is any
capture that copies the shared_ptr: a plain `[x]`, an init-capture of the
name itself (`[p = x]`, `[p = std::move(x)]`), or a `[=]` default whose body
uses `x`. A by-reference capture (`[&x]`, `[&]`) copies nothing and is not a
cycle; neither is `[x = x.get()]` or `[w = std::weak_ptr<T>(x)]`.

Both are reported ONLY when the capture is demonstrably a `shared_ptr` in
that scope: `make_shared`, a declared `shared_ptr` local, a factory whose
declared return type is one — or a parameter of the enclosing function
declared `shared_ptr<T>`, `const shared_ptr<T>&` or `shared_ptr<T>&&`. The
parameter case is how `UltraCanvasRadioGroup::AddRadioButton` leaked every
radio it was given (`button->onChecked = [this, button]`, with `button` the
by-value parameter) while this check passed: it only looked inside the body,
and a parameter is declared before the `{`.

Being demonstrable matters: the first version of this check matched capture
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

# Where widget-building code lives: the framework (plugins, OS back ends and
# render back ends included - a flow-chart palette button that owned itself
# went unseen while Plugins was left out), the applications, SmartHome and
# the tests. Vendored code and build output are never scanned.
SEARCH_ROOTS = [
    "UltraCanvas/core",
    "UltraCanvas/include",
    "UltraCanvas/dialogs",
    "UltraCanvas/Plugins",
    "UltraCanvas/OS",
    "UltraCanvas/libspecific",
    "Apps",
    "SmartHome",
    "Tests",
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

# `widget->SetOnClick([captures]...)`: the lambda is the setter's first
# argument and the setter stores it on the widget, exactly as the assignment
# above does. SetOnClick, SetEventCallback, SetValueFormatter, SetXProvider,
# AddListener / AddXCallback. The framework wires most clicks this way, and
# the first version of this check never looked at them.
SETTER_RE = re.compile(
    r"(?P<obj>\w+)\s*->\s*"
    r"(?P<member>Set[A-Z]\w*|Add\w*(?:Listener|Callback|Handler))\s*\(\s*"
    r"\[(?P<captures>[^\]]*)\]"
)

# What may sit between a lambda's `]` and its body's `{`: a parameter list
# (one level of nested parentheses, for a `std::function<void(int)>`
# parameter), `mutable`, `noexcept`, a trailing return type.
LAMBDA_HEAD_RE = re.compile(
    r"\s*(?:\((?P<params>[^()]*(?:\([^()]*\)[^()]*)*)\))?[^;{}()]*\{"
)

# `[p = x]` / `[p = std::move(x)]`: an init-capture that copies (or moves)
# the shared_ptr itself. Anything else on the right — `x.get()`,
# `std::weak_ptr<T>(x)`, `x.get()->child` — holds something other than x.
INIT_CAPTURE_RE = re.compile(
    r"^(?P<alias>\w+)\s*=\s*(?:std::move\s*\(\s*(?P<moved>\w+)\s*\)|(?P<plain>\w+))$"
)

# A parameter that is a shared_ptr held by value or by reference — not a
# pointer to one, not a weak_ptr, not a container of them. The template
# argument is matched greedily so `shared_ptr<Foo<Bar>>` still parses.
SHARED_PARAM_RE = re.compile(
    r"^(?:const\s+)?(?:std::)?shared_ptr\s*<.+>\s*(?:const\s*)?&{0,2}\s*"
    r"(?P<name>\w+)$"
)

# Declarations that settle a name's type inside a scope.
RAW_AUTO_RE = r"\bauto\s*\*\s*{name}\s*="            # auto* x = y.get()
RAW_PARAM_RE = r"[\w>]\s*\*\s*&?\s*{name}\b\s*[,)]"  # T* x / T*& x parameter
AUTO_INIT_RE = r"\bauto\s+{name}\s*=\s*(?P<init>[^;]{{0,120}})"
SHARED_DECL_RE = r"std::shared_ptr\s*<[^>]+>\s*&?\s*{name}\b"
# `T name = ...` / `auto* name;` / `Foo name{...}` — a declaration that would
# shadow the enclosing `name` inside a lambda body. `return name;` and the
# like are uses, not declarations.
LOCAL_DECL_RE = (
    r"(?:\b(?!(?:return|co_return|throw|delete|else|case|new|goto)\b)\w+|>)"
    r"\s*[*&]*\s+[*&]*{name}\s*(?:=|;|\{{|\()"
)
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


def split_top_level(text: str, sep: str = ",") -> list[str]:
    """Split on `sep` outside (), <>, [] and {} — a parameter list's commas."""
    parts, depth, cur = [], 0, []
    for k, ch in enumerate(text):
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}" and not (ch == ">" and k and text[k - 1] == "-"):
            depth = max(0, depth - 1)
        if ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
            continue
        cur.append(ch)
    parts.append("".join(cur))
    return parts


def shared_ptr_params(code: str, body_start: int) -> frozenset[str]:
    """Parameters of the function whose body opens at `body_start` that are
    declared `shared_ptr<T>`, `const shared_ptr<T>&` or `shared_ptr<T>&&`.

    The body is everything from its `{`, so a parameter is invisible to a
    search of it — which is how a by-value `shared_ptr` parameter captured
    into a callback on itself went unreported.
    """
    window = max(0, body_start - 400)
    before = code[window:body_start].rstrip()
    match = BODY_OPEN_RE.search(before)
    if not match:
        return frozenset()
    close = window + match.start()        # the parameter list's `)`
    depth, i = 0, close
    while i >= window:
        if code[i] == ")":
            depth += 1
        elif code[i] == "(":
            depth -= 1
            if depth == 0:
                break
        i -= 1
    else:
        return frozenset()

    names = set()
    for param in split_top_level(code[i + 1:close]):
        # Drop a default argument; `=` cannot appear in the declarator itself.
        param = " ".join(param.split("=", 1)[0].split())
        found = SHARED_PARAM_RE.match(param)
        if found:
            names.add(found.group("name"))
    return frozenset(names)


def lambda_after(code: str, close_bracket: int) -> tuple[str, str] | None:
    """(parameter list, body) of the lambda whose capture list ends at
    `close_bracket` (the index of its `]`), or None if it cannot be read."""
    head = LAMBDA_HEAD_RE.match(code, close_bracket + 1)
    if not head:
        return None
    open_brace = head.end() - 1
    depth = 0
    for j in range(open_brace, len(code)):
        if code[j] == "{":
            depth += 1
        elif code[j] == "}":
            depth -= 1
            if depth == 0:
                return head.group("params") or "", code[open_brace + 1:j]
    return None


def uses_free_name(name: str, params: str, lambda_body: str) -> bool:
    """True when a `[=]` lambda's body names `name` as the enclosing scope's
    variable, so the default captures it by value.

    Conservative: a lambda parameter of that name, any declaration of it in
    the body, or a use only as a member (`obj.name`, `p->name`, `X::name`)
    means it is not captured.
    """
    word = re.escape(name)
    if re.search(rf"\b{word}\b", params):
        return False
    if re.search(LOCAL_DECL_RE.format(name=word), lambda_body):
        return False
    if re.search(RAW_PARAM_RE.format(name=word), lambda_body):
        return False
    return bool(re.search(rf"(?<![\w.])(?<!->)(?<!::){word}\b", lambda_body))


def is_shared_ptr(name: str, body: str, whole: str,
                  params: frozenset[str] = frozenset()) -> bool:
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

    if re.search(SHARED_DECL_RE.format(name=re.escape(name)), body):
        return True
    # Declared in the signature: `AddRadioButton(std::shared_ptr<Radio> button)`.
    return name in params


def ancestors_of(name: str, parent: dict[str, str]) -> set[str]:
    seen: set[str] = set()
    cur = parent.get(name)
    while cur and cur not in seen:
        seen.add(cur)
        cur = parent.get(cur)
    return seen


class Finding:
    def __init__(self, path: Path, line: int, kind: str, owner: str,
                 capture: str, member: str, via: str = ""):
        self.path = path
        self.line = line
        self.kind = kind
        self.owner = owner
        self.capture = capture
        self.member = member
        # How the shared_ptr got in: "" for a plain `[x]`, "[=]" for a
        # default capture, or the init-capture's own name (`[p = x]` -> "p").
        self.via = via

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
        how = ""
        if self.via == "[=]":
            how = " (implicitly, through [=])"
        elif self.via:
            how = f" (as `{self.via}`)"
        if self.kind == "self-capture":
            what = (f"{self.owner}->{self.member} captures{how} a shared_ptr "
                    f"to {self.owner} itself")
        else:
            what = (f"{self.owner}->{self.member} captures{how} a shared_ptr "
                    f"to {self.capture}, which owns {self.owner}")
        alias = self.via if self.via and self.via != "[=]" else self.capture
        return (f"{self.rel}:{self.line}: {self.kind}: {what} — capture it raw "
                f"(`{alias} = {self.capture}.get()`)")


def captured_by_value(captures: str, owning: set[str], code: str,
                      close_bracket: int) -> list[tuple[str, str]]:
    """The names in `owning` that this capture list copies, as (name, via).

    A plain `[x]`, an init-capture `[p = x]` / `[p = std::move(x)]`, or —
    under a `[=]` default — a use of `x` in the lambda's body. By-reference
    captures (`[&x]`, `[&]`) copy nothing, and an init-capture of anything
    but the name itself (`x.get()`, `std::weak_ptr<T>(x)`) has already chosen
    to hold something else.
    """
    found: list[tuple[str, str]] = []
    explicit: set[str] = set()
    copy_default = False
    for entry in split_top_level(captures):
        entry = entry.strip()
        if not entry or entry in ("this", "*this", "&"):
            continue
        if entry == "=":
            copy_default = True
            continue
        if entry.startswith("&"):
            explicit.add(entry.lstrip("&").split("=", 1)[0].strip())
            continue
        init = INIT_CAPTURE_RE.match(" ".join(entry.split()))
        if init:
            alias = init.group("alias")
            explicit.add(alias)
            name = init.group("moved") or init.group("plain")
            if name in owning:
                found.append((name, alias if alias != name else ""))
            continue
        if "=" in entry:
            explicit.add(entry.split("=", 1)[0].strip())
            continue
        explicit.add(entry)
        if entry in owning:
            found.append((entry, ""))

    if copy_default:
        lam = lambda_after(code, close_bracket)
        if lam:
            params, lambda_body = lam
            for name in sorted(owning - explicit):
                if uses_free_name(name, params, lambda_body):
                    found.append((name, "[=]"))
    return found


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
        params = shared_ptr_params(code, start)

        stored = [(m, m.group("member")) for m in CALLBACK_RE.finditer(body)]
        stored += [(m, m.group("member") + "(...)")
                   for m in SETTER_RE.finditer(body)]
        for match, member in sorted(stored, key=lambda s: s[0].start()):
            owner = match.group("obj")
            owning = {owner} | ancestors_of(owner, parent)

            for entry, via in captured_by_value(match.group("captures"), owning,
                                                body, match.end() - 1):
                if not is_shared_ptr(entry, body, code, params):
                    continue
                findings.append(
                    Finding(
                        path,
                        code.count("\n", 0, start + match.start()) + 1,
                        "self-capture" if entry == owner else "ancestor-capture",
                        owner,
                        entry,
                        member,
                        via,
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
