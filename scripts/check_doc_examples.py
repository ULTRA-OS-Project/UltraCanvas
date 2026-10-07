#!/usr/bin/env python3
"""Check the C++ in the component docs against the headers.

Each ```cpp block of a doc (by default Docs/UltraCanvas/*Examples*.md) is
compiled with clang++ -fsyntax-only against every public header, and its
errors are reported at the doc's own line numbers. Four kinds of C++ are
told apart:

* examples - statements and definitions, compiled as written. A snippet
  that uses a name it never declares (`container`, `window`, `label`) gets
  a declaration whose type is taken from the doc (`auto label =
  std::make_shared<UltraCanvasLabel>(...)` elsewhere in it) or guessed from
  the name; a name that can't be typed is listed as context, not as an
  error.
* API listings - declarations copied from a class
  (`void SetText(const std::string& text);`). Each must be a member of a
  class the doc uses, or a free function (in the namespace the listing is
  written in, `namespace QRCodeUtils { ... }`), with that exact signature.
  A block of only fields (`std::function<void()> onClick;`) is a listing
  when most of its names are members of the doc's classes, and local
  variables of an example otherwise.
* copies of a type - `struct X { ... };`, `enum class X { ... };` or a
  class outline, where X is a type of the headers: its fields and
  enumerators must exist in the real X, and its functions with their
  signatures.
* prose - a `Name(` in backticks must be a function of some header.

A doc can declare what its snippets assume and the checker can't guess, in
an HTML comment (not rendered):

    <!-- doc-check: void CreateFolder(); std::shared_ptr<UltraCanvasTreeView> tree; -->

A function is declared for every block; a variable is given to the blocks
that use it without declaring it. A snippet's `#include <...>` is honoured
where this machine has the header.

Missing `#include` targets are reported too.

Needs clang++ and pkg-config (cairo, pango, glib); Linux. The first run
builds a precompiled header of all public headers in --work (about 1 min).

    python3 scripts/check_doc_examples.py                 # all Examples docs
    python3 scripts/check_doc_examples.py Docs/UltraCanvas/UltraCanvasButtonExamples.md
    python3 scripts/check_doc_examples.py --show-context  # also untyped names

Exit status 1 when a doc has findings.
"""
# Version: 1.0.0
# Last Modified: 2026-10-07
# Author: UltraCanvas Framework

import argparse
import concurrent.futures
import hashlib
import os
import re
import shutil
import subprocess
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "UltraCanvas" / "include"

# Headers that don't compile on their own, or clash with another header
# (UltraCanvasConnectionRenderer's struct ConnectionStyle and the block
# diagram's enum class ConnectionStyle): left out of the shared header, and
# the block diagram gets a second one without the chart side.
STANDALONE_EXCLUDED = {
    "Plugins/LaTeX/UltraCanvasLaTeXBackend.h",
    "UltraCanvasCairoDebugExtension.h",
}
CLASH_A = {"Plugins/Diagrams/UltraCanvasBlockDiagram.h"}
CLASH_B = {
    "Plugins/Charts/UltraCanvasConnectionRenderer.h",
    "Plugins/Charts/UltraCanvasCircularInfoGraphic.h",
    "Plugins/Charts/UltraCanvasChordChart.h",
}

SYSTEM_HEADERS = [
    "iostream", "sstream", "fstream", "iomanip", "random", "map", "set",
    "unordered_map", "thread", "chrono", "numeric", "cmath", "algorithm",
    "functional", "memory", "string", "vector", "array", "optional",
    "filesystem", "atomic", "mutex", "cstdio", "cstdlib", "cstring",
    "type_traits",
]

DEFINES = [
    "ULTRACANVAS_PLATFORM_Linux=1", "ULTRACANVAS_ENABLE_AUDIO=1",
    "ULTRACANVAS_ENABLE_GL=1", "ULTRACANVAS_HAS_DATABASE=1",
    "ULTRACANVAS_HAS_NET=1", "ULTRACANVAS_HAS_ULTRAWIN=1",
    "ULTRACANVAS_HAS_VIRTUALFS", "ULTRACANVAS_PLUGIN_LATEX=1",
    "HAS_LIBRSVG=1", "HAS_LIBVIPS=1",
    "ULTRACANVASEPSPLUGIN_AVAILABLE=1", "ULTRACANVASXARPLUGIN_AVAILABLE=1",
    "ULTRACANVASVECTORPLUGIN_AVAILABLE=1", "ULTRACANVAS_HAS_EPS_PLUGIN=1",
    "ULTRACANVAS_HAS_XAR_PLUGIN=1", "ULTRACANVAS_HAS_VECTOR_PLUGIN=1",
]

INCLUDE_DIRS = [
    INCLUDE, ROOT / "UltraCanvas", ROOT / "UltraCanvas" / "Plugins",
    ROOT / "UltraCanvas" / "Plugins" / "Vector",
    ROOT / "UltraCanvas" / "Plugins" / "Vector" / "XAR",
    ROOT / "UltraCanvas" / "Plugins" / "Vector" / "EPS",
    ROOT / "UltraCanvas" / "third_party", ROOT / "VirtualFS" / "include",
    ROOT / "Apps" / "DemoApp",
]

KEYWORDS = {
    "return", "else", "if", "for", "while", "switch", "do", "try", "catch",
    "new", "delete", "throw", "case", "co_return", "co_await", "sizeof",
    "typeid", "decltype", "static_assert", "default", "goto", "using",
    "public", "private", "protected", "operator", "template", "typename",
}

GEN = "__gen__"          # #line file name of generated lines


# --------------------------------------------------------------------------
# Compiler setup

def pkg_cflags():
    try:
        out = subprocess.run(["pkg-config", "--cflags", "cairo", "pango", "glib-2.0",
                              "freetype2", "harfbuzz"], capture_output=True, text=True, check=True).stdout
        return out.split()
    except (OSError, subprocess.CalledProcessError):
        return []


def base_flags():
    flags = ["-std=gnu++20", "-w", "-ferror-limit=0", "-fno-caret-diagnostics",
             "-fno-diagnostics-fixit-info"]
    flags += ["-D" + d for d in DEFINES]
    flags += ["-I" + str(d) for d in INCLUDE_DIRS]
    return flags + pkg_cflags()


def public_headers():
    return sorted(str(p.relative_to(INCLUDE)).replace(os.sep, "/") for p in INCLUDE.rglob("*.h"))


def build_pch(work, name, excluded, flags, clang):
    headers = [h for h in public_headers() if h not in STANDALONE_EXCLUDED and h not in excluded]
    text = "".join('#include "%s"\n' % (INCLUDE / h) for h in headers)
    text += "".join("#include <%s>\n" % h for h in SYSTEM_HEADERS)
    text += "using namespace UltraCanvas;\n"
    digest = hashlib.sha1(text.encode()).hexdigest()[:12]
    header = work / ("%s.h" % name)
    pch = work / ("%s.h.pch" % name)
    stamp = work / ("%s.stamp" % name)
    newest = max(p.stat().st_mtime for p in INCLUDE.rglob("*.h"))
    if pch.exists() and stamp.exists() and stamp.read_text() == digest and pch.stat().st_mtime > newest:
        return header
    header.write_text(text)
    proc = subprocess.run([clang] + flags + ["-x", "c++-header", str(header), "-o", str(pch)],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit("cannot build the shared header %s:\n%s" % (name, proc.stderr[-4000:]))
    stamp.write_text(digest)
    return header


# --------------------------------------------------------------------------
# C++ text

def clean_lines(lines):
    """The lines with comments removed and string and char literals blanked,
    so that braces and keywords in them don't count."""
    out = []
    in_block = False
    for line in lines:
        res = []
        i = 0
        n = len(line)
        while i < n:
            c = line[i]
            if in_block:
                if line.startswith("*/", i):
                    in_block = False
                    i += 2
                else:
                    i += 1
                continue
            if line.startswith("//", i):
                break
            if line.startswith("/*", i):
                in_block = True
                i += 2
                continue
            if c == '"' or (c == "'" and not (i > 0 and line[i - 1].isalnum())):
                quote = c
                res.append(c)
                i += 1
                while i < n and line[i] != quote:
                    i += 2 if line[i] == "\\" else 1
                res.append(quote)
                i += 1
                continue
            res.append(c)
            i += 1
        out.append("".join(res))
    return out


def split_top(text, sep=","):
    """Splits at `sep` outside (), <>, [], {}."""
    parts, depth, cur = [], 0, []
    for c in text:
        if c in "([{<":
            depth += 1
        elif c in ")]}>":
            depth -= 1
        if c == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    parts.append("".join(cur))
    return [p.strip() for p in parts if p.strip()]


def statements(text):
    """Top-level statements of cleaned text: split at ';' outside brackets,
    and after a '}' that closes a body not followed by ';'."""
    out, depth, cur = [], 0, []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        cur.append(c)
        if c in "({[":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                rest = text[i + 1:].lstrip()
                if not rest.startswith((";", ",", ")", "else", "while", "catch")) and \
                        not re.match(r"^\w+\s*[;,=]", rest):
                    out.append("".join(cur).strip())
                    cur = []
        elif c == ";" and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        i += 1
    rest = "".join(cur).strip()
    if rest:
        out.append(rest)
    return [s for s in out if s and s != ";"]


def flatten_templates(text):
    """`std::function<void(int)>` -> `std::function<>`, for pattern tests."""
    prev = None
    while prev != text:
        prev = text
        text = re.sub(r"<[^<>]*>", "\x01", text)
    return text.replace("\x01", "<>")


# --------------------------------------------------------------------------
# What the headers declare

class HeaderIndex:
    TYPE_DEF = re.compile(
        r"\bnamespace\s+([\w:]+)\s*\{"
        r"|\b(?:class|struct|union)\s+(?:\[\[[^\]]*\]\]\s*)?(?:alignas\s*\([^)]*\)\s*)?"
        r"(?:[A-Z][A-Z0-9_]*_(?:API|EXPORT)\s+)?(\w+)\s*(?:final\s*)?(?::[^;{}()]*)?\{"
        r"|\benum\s+(?:class\s+|struct\s+)?(\w+)\s*(?::\s*[\w:\s]+)?\{"
        r"|([{}])")

    def __init__(self):
        self.text = {}
        for root in (INCLUDE, ROOT / "UltraCanvas" / "Plugins"):
            for p in root.rglob("*.h"):
                try:
                    self.text[p] = p.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    pass
        self.qualified = {}     # short type name -> {qualified names}
        self.defined_in = {}    # qualified type name -> header path
        self.namespaces = set()
        for path, text in self.text.items():
            self._scan(path, text)
        blob = "\n".join(self.text.values())
        self.functions = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", blob))
        self.types = set(self.qualified)
        self.types |= set(re.findall(r"\busing\s+([A-Za-z_]\w*)\s*=", blob))
        self.types |= set(re.findall(r"\btypedef\b[^;]*\b([A-Za-z_]\w*)\s*;", blob))
        self.factories = {}
        for m in re.finditer(r"std::shared_ptr<\s*([\w:]+)\s*>\s+(Create\w*)\s*\(", blob):
            self.factories.setdefault(m.group(2), m.group(1))

    def _scan(self, path, text):
        code = "\n".join(l if not l.lstrip().startswith("#") else "" for l in clean_lines(text.splitlines()))
        stack = []
        for m in self.TYPE_DEF.finditer(code):
            ns, cls, enum, brace = m.groups()
            scope = [n for kind, n in stack if kind in ("ns", "cls")]
            if ns:
                stack.append(("ns", ns))
                self.namespaces.add("::".join(scope + [ns]))
            elif cls or enum:
                name = cls or enum
                q = "::".join(scope + [name])
                self.qualified.setdefault(name, set()).add(q)
                self.defined_in.setdefault(q, path)
                stack.append(("cls" if cls else "blk", name))
            elif brace == "{":
                stack.append(("blk", None))
            elif brace == "}" and stack:
                stack.pop()

    def qualify(self, name, prefer_ns=None):
        qs = sorted(self.qualified.get(name, ()), key=lambda q: (q.count("::"), q))
        if prefer_ns:
            pref = [q for q in qs if q.startswith(prefer_ns + "::")]
            if pref:
                return pref
        return qs

    def is_final(self, q):
        path = self.defined_in.get(q)
        name = q.split("::")[-1]
        return bool(path) and re.search(r"\b(?:class|struct)\s+%s\s+final\b" % re.escape(name), self.text[path]) is not None

    def header_lines(self, name, owner_q=None):
        """Declarations of `name(` for the report: in the header that
        defines `owner_q` when there is one."""
        out = []
        pat = re.compile(r"^\s*(?:[\w:<>,\*&\s\[\]]+\s)?[\*&]?~?%s\s*\(" % re.escape(name))
        paths = [self.defined_in[owner_q]] if owner_q in self.defined_in else list(self.text)
        for path in paths:
            for i, line in enumerate(self.text[path].splitlines(), 1):
                stripped = line.strip()
                if pat.search(line) and not stripped.startswith(("//", "*", "return")) and "->" not in stripped:
                    out.append("%s:%d: %s" % (path.relative_to(ROOT), i, stripped))
        return out


# --------------------------------------------------------------------------
# Markdown

class Block:
    def __init__(self, first_line, lines):
        self.first = first_line          # doc line number of lines[0]
        self.lines = lines
        self.index = 0
        self.context = {}
        self.bad_context = set()
        self.uses = set()           # using __dc_bK::X; for a type an earlier block defined

    def last(self):
        return self.first + len(self.lines) - 1


def read_blocks(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    blocks = []
    i = 0
    while i < len(lines):
        m = re.match(r"^(\s*)```\s*([\w+]*)\s*$", lines[i])
        if not m:
            i += 1
            continue
        indent, lang = len(m.group(1)), m.group(2).lower()
        j = i + 1
        body = []
        while j < len(lines) and not re.match(r"^\s*```\s*$", lines[j]):
            line = lines[j]
            body.append(line[indent:] if line[:indent].strip() == "" else line)
            j += 1
        if lang in ("cpp", "c++", "cxx", "cc"):
            blocks.append(Block(i + 2, body))
        i = j + 1
    for n, b in enumerate(blocks):
        b.index = n
    return lines, blocks


# --------------------------------------------------------------------------
# Chunks of a block

TYPE_PART = r"(?:[\w:]+(?:\s*<[^;{}()]*>)?[\s\*&]+)+"
FUNC_HEAD = re.compile(r"^(?:\[\[\w+\]\]\s*)?(?:(?:static|inline|virtual|constexpr|explicit|friend|extern)\s+)*"
                       r"(?:" + TYPE_PART + r")?(?:[\w]+(?:<[^;{}()]*>)?::)*~?(?:operator\s*\S+|\w+)\s*\(")


def is_function_head(head):
    """True for `void Foo(int x) const` / `Foo::Foo(...) : a(1)`, ahead of a `{`."""
    head = head.strip()
    if not head or "=" in head.split("(")[0]:
        return False
    first = re.match(r"[\w:~\[]+", head)
    if not first or first.group(0) in KEYWORDS:
        return False
    m = FUNC_HEAD.match(head)
    if not m:
        return False
    name = re.search(r"(~?\w+)\s*\($", m.group(0))
    if name is None or name.group(1) in KEYWORDS:
        return False
    return bool(m.group(0)[:name.start(1)].strip())


def is_member_head(head):
    """`int Get() const`, `void F() override`: only a class member is written so."""
    tail = head[head.rfind(")") + 1:] if ")" in head else ""
    return bool(re.search(r"\b(const|override|final)\b", tail)) or bool(re.match(r"^\s*virtual\b", head))


def is_namespace_declaration(text):
    """A function declaration that belongs at namespace scope (`inline T F();`,
    `void F(const std::string& id);`), not a local variable (`std::string s(text);`)."""
    d = parse_decl(text)
    if not d or d["ctor"] or not d["ret"] or d["ret"] == "auto":
        return False
    if re.search(r"\b(inline|static|extern)\b", d["pre"] + " " + d["ret"]):
        return True
    named = [re.match(r"^(?:const\s+)?[\w:]+(?:<>)?(?:\s*[\*&]+\s*|\s+)(?:const\s+)?\w+(?:\s*\[\s*\w*\s*\])?$",
                      flatten_templates(p).strip()) for p in d["params"]]
    return all(named)


def chunk_block(clean):
    """Splits a block into ('pp'|'def'|'stmt', first, last) chunks by line."""
    chunks = []
    n = len(clean)
    i = 0
    while i < n:
        code = clean[i].strip()
        if not code:
            i += 1
            continue
        if code.startswith("#"):
            j = i
            while clean[j].rstrip().endswith("\\") and j + 1 < n:
                j += 1
            chunks.append(("pp", i, j))
            i = j + 1
            continue
        depth_brace = depth_paren = 0
        kind = None
        head = []
        j = i
        done = False
        while j < n and not done:
            seg = clean[j]
            for k, c in enumerate(seg):
                if c == "(":
                    depth_paren += 1
                elif c == ")":
                    depth_paren -= 1
                elif c == "{":
                    if depth_brace == 0 and depth_paren == 0 and kind is None:
                        h = (" ".join(head) + " " + seg[:k]).strip()
                        if re.match(r"^(template\s*<|class\b|struct\b|enum\b|union\b|namespace\b|extern\s+\"C\")", h):
                            kind = "def"
                        elif is_function_head(h):
                            kind = "def"
                        else:
                            kind = "stmt"
                    depth_brace += 1
                elif c == "}":
                    depth_brace -= 1
                    if depth_brace == 0 and depth_paren == 0 and kind == "def":
                        rest = seg[k + 1:].strip()
                        nxt = clean[j + 1].strip() if j + 1 < n else ""
                        if re.match(r"^\s*(template\s*<[^>]*>\s*)?(class|struct|enum|union)\b", clean[i]) \
                                and not rest.startswith(";") and nxt.startswith(";"):
                            j += 1
                        done = True
                        break
                elif c == ";" and depth_brace == 0 and depth_paren == 0:
                    if kind is None:
                        h = (" ".join(head) + " " + seg[:k]).strip()
                        kind = "def" if re.match(r"^(using\s+namespace|template\s*<|class\s+\w+\s*$|struct\s+\w+\s*$|typedef\b)", h) else "stmt"
                    done = True
                    break
            if not done:
                head.append(seg)
                j += 1
        j = min(j, n - 1)
        kind = kind or "stmt"
        text = "\n".join(clean[i:j + 1]).strip()
        if kind == "stmt" and (is_namespace_declaration(text) or re.match(r"^(using\s+\w+\s*=|typedef\b)", text)):
            kind = "def"
        chunks.append((kind, i, j))
        i = j + 1
    merged = []
    for c in chunks:
        if merged and c[0] == "stmt" and merged[-1][0] == "stmt":
            merged[-1] = ("stmt", merged[-1][1], c[2])
        else:
            merged.append(c)
    return merged


# --------------------------------------------------------------------------
# Declarations

DECL = re.compile(r"^(?P<pre>(?:\[\[\w+\]\]\s*)?(?:(?:virtual|static|inline|explicit|constexpr|friend)\s+)*)"
                  r"(?P<ret>(?:const\s+)?(?:unsigned\s+|signed\s+|long\s+)*[\w:]+(?:\s*<.*>)?"
                  r"(?:\s*(?:const\b|[\*&]))*\s+(?:const\s*[\*&]+\s*)?|(?:const\s+)?[\w:]+(?:\s*<.*>)?[\*&]+\s*)?"
                  r"(?P<name>~?\w+)\s*\((?P<params>.*)\)\s*(?P<cv>const)?\s*(?:noexcept)?\s*"
                  r"(?:override|final|\s)*(?:=\s*(?:0|default|delete))?\s*$", re.S)
FIELD = re.compile(r"^(?:static\s+|inline\s+|constexpr\s+|mutable\s+)*(?:const\s+)?(?:unsigned\s+|signed\s+|long\s+)*"
                   r"[\w:]+(?:\s*<.*>)?[\s\*&]+(?P<name>\w+)\s*(?:\[\s*\w*\s*\])?\s*(?:=.*|\{.*\})?$", re.S)


def looks_like_param(p):
    p = flatten_templates(re.sub(r"=.*$", "", p, flags=re.S)).strip()
    if p in ("", "void", "..."):
        return True
    if re.search(r"[\"']|->|^\d|\(\s*\)|^\w+\s*\(", p):
        return False
    return bool(re.match(r"^(?:const\s+)?(?:unsigned\s+|signed\s+|long\s+)*[\w:]+(?:<>)?(?:\s*(?:const\b|[\*&]))*"
                         r"(?:\s*(?:\w+|\(\s*[\*&]\s*\w*\s*\)\s*\(.*\)))?(?:\s*\[\s*\w*\s*\])?$", p))


def strip_inline_body(s):
    """`int Get() const { return x; }` -> `int Get() const`."""
    if not s.endswith("}"):
        return s
    depth = 0
    for i, c in enumerate(s):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "{" and depth == 0 and ")" in s[:i]:
            return s[:i].strip()
    return s


def parse_decl(stmt):
    s = strip_inline_body(stmt.rstrip(";").strip())
    s = re.sub(r"^\s*(?:public|private|protected)\s*:\s*", "", s)
    if not s or "{" in s or "->" in s.split("(")[0]:
        return None
    m = DECL.match(s)
    if not m:
        return None
    name = m.group("name")
    if name in KEYWORDS:
        return None
    ret = (m.group("ret") or "").strip()
    if ret in KEYWORDS or ret.split()[0:1] in (["return"], ["new"], ["delete"], ["else"]):
        return None
    params = split_top(m.group("params"))
    if not all(looks_like_param(p) for p in params):
        return None
    if not ret and not (name[0].isupper() or name.startswith("~")):
        return None
    if not ret and not params and not name.startswith("~"):
        return None
    if ret == "auto" and "=" in s.split("(")[0]:
        return None
    return {"name": name, "ret": ret, "pre": m.group("pre").strip(),
            "params": [re.sub(r"\s*=.*$", "", p, flags=re.S).strip() for p in params],
            "const": bool(m.group("cv")), "text": s, "ctor": not ret}


def parse_field(stmt):
    s = stmt.rstrip(";").strip()
    s = re.sub(r"^\s*(?:public|private|protected)\s*:\s*", "", s)
    head = s.split("=")[0]
    if "(" in flatten_templates(head) or s.startswith(("return", "using", "typedef", "auto ", "friend")):
        return None
    m = FIELD.match(s)
    if not m or m.group("name") in KEYWORDS:
        return None
    return {"name": m.group("name"), "text": s}


def listing_of(text, types=()):
    """(decls, fields) when every statement of `text` is a declaration,
    else None. Type definitions are skipped (they are copies). A
    constructor must name a type: `ShowAs(a, b);` is a call."""
    stmts = statements(text)
    if not stmts:
        return None
    decls, fields = [], []
    for s in stmts:
        s2 = re.sub(r"^\s*(?:public|private|protected)\s*:\s*", "", s).strip()
        if not s2:
            continue
        if re.match(r"^(?:template\s*<[^>]*>\s*)?(class|struct|enum|union)\b", s2):
            continue
        if re.match(r"^(using\s+\w+\s*=|typedef\b)", s2):
            continue    # an alias, as the class declares it
        if re.match(r"^(namespace|#|using\s+namespace)", s2):
            return None
        d = parse_decl(s2)
        if d and d["ctor"] and not d["name"].startswith("~") and d["name"] not in types:
            return None
        if d:
            decls.append(d)
            continue
        f = parse_field(s2)
        if f:
            fields.append(f)
            continue
        return None
    if not decls and not fields:
        return None
    return decls, fields


# --------------------------------------------------------------------------
# Context

GENERIC_NAMES = {
    "container", "parent", "root", "rootcontainer", "maincontainer", "panel", "content", "contentarea",
    "layoutcontainer", "sidebar", "form", "row", "column", "page", "host", "body", "toolbarcontainer",
    "header", "footer", "area", "pane",
}


def doc_classes(stem, text, index):
    """Short names of the classes the doc uses, its main one first."""
    used = {}
    for m in re.finditer(r"\b(UltraCanvas\w+|UC[A-Z]\w*|I[A-Z]\w+)\b", text):
        if m.group(1) in index.qualified:
            used[m.group(1)] = used.get(m.group(1), 0) + 1
    for m in re.finditer(r"\b(Create\w+)\s*\(", text):
        t = index.factories.get(m.group(1))
        if t:
            short = t.split("::")[-1]
            used[short] = used.get(short, 0) + 1
    main = None
    for suffix in ("", "Element", "Diagram", "DiagramElement", "Chart", "ChartElement", "Control", "View"):
        if stem + suffix in index.qualified:
            main = stem + suffix
            break
    ordered = sorted(used, key=lambda k: -used[k])
    if main is None and ordered:
        main = ordered[0]
    if main in ordered:
        ordered.remove(main)
    return ([main] if main else []) + ordered


def context_types(text, index):
    """name -> declared type, from the doc's own declarations."""
    types = {}
    for m in re.finditer(r"\b(?:auto|std::shared_ptr<\s*[\w:]+\s*>)\s*&?\s*(\w+)\s*=\s*std::make_shared<\s*([\w:]+)\s*>", text):
        types.setdefault(m.group(1), "std::shared_ptr<%s>" % m.group(2))
    for m in re.finditer(r"\bstd::shared_ptr<\s*([\w:]+)\s*>\s*&?\s*(\w+)\s*[;=({,)]", text):
        types.setdefault(m.group(2), "std::shared_ptr<%s>" % m.group(1))
    for m in re.finditer(r"\bauto\s+(\w+)\s*=\s*(Create\w+)\s*\(", text):
        t = index.factories.get(m.group(2))
        if t:
            types.setdefault(m.group(1), "std::shared_ptr<%s>" % t)
    for m in re.finditer(r"(?:^|[;{(,]|\n)\s*(?:const\s+)?([A-Z][\w:]*)\s*([\*&]?)\s*(\w+)\s*(?=[;={(,)])", text):
        t, ptr, name = m.group(1), m.group(2), m.group(3)
        if t.split("::")[-1] in index.qualified and name not in KEYWORDS and name[0].islower():
            types.setdefault(name, t + ("*" if ptr == "*" else ""))
    return types


def guess_type(name, declared, main, index):
    if name in declared:
        return declared[name]
    low = name.lower()
    if low in GENERIC_NAMES or low.endswith(("container", "panel")):
        return "std::shared_ptr<UltraCanvasContainer>"
    if low in ("window", "mainwindow", "win", "appwindow", "parentwindow") or low.endswith("window"):
        return "std::shared_ptr<UltraCanvasWindow>"
    if low in ("ctx", "context", "rendercontext", "rc", "g", "renderer"):
        return "IRenderContext*"
    if low in ("event", "e", "ev", "evt"):
        return "UCEvent"
    if low in ("app", "application"):
        return "UltraCanvasApplication*"
    words = re.findall(r"[A-Z]?[a-z0-9]+|[A-Z]+(?![a-z])", name)
    if words:
        last = words[-1].capitalize()
        for cand in ("UltraCanvas" + last, "UltraCanvas" + last + "Element"):
            if cand in index.qualified:
                return "std::shared_ptr<%s>" % cand
    if main:
        stem = main.replace("UltraCanvas", "").replace("Element", "").lower()
        generic = {"element", "widget", "control", "chart", "diagram", "view", "plot", "gauge", "graph",
                   "viewer", "editor", "surface", "item", "instance"}
        if stem and (low == stem or low.endswith(stem) or low in generic or low.startswith("my")):
            return "std::shared_ptr<%s>" % main
    return None


# --------------------------------------------------------------------------
# Compiling

ERR = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):(?P<col>\d+): (?P<kind>error|fatal error|note): (?P<msg>.*)$")


def run_clang(clang, flags, pch_header, source, work, tag, doc=None):
    """Errors as (file, line, message). An error inside a header that a
    doc line caused (a template instantiated from it) is put on that line."""
    path = work / ("%s.cpp" % tag)
    path.write_text(source, encoding="utf-8")
    cmd = [clang] + flags + ["-include-pch", str(pch_header) + ".pch", "-fsyntax-only", str(path)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    errors = []
    for line in proc.stderr.splitlines():
        m = ERR.match(line)
        if not m:
            continue
        f, n, msg = m.group("file"), int(m.group("line")), m.group("msg")
        if m.group("kind") != "note":
            errors.append([f, n, msg])
        elif doc and errors and errors[-1][0] not in (doc, GEN) and f == doc:
            errors[-1][0], errors[-1][1] = f, n
    return [tuple(e) for e in errors]


_header_ok = {}
_header_lock = threading.Lock()


def header_compiles(path, clang, flags, pch_header):
    """Whether a header outside the shared one compiles here (a plugin's
    header may need a library this machine lacks)."""
    key = (str(path), str(pch_header))
    with _header_lock:
        if key in _header_ok:
            return _header_ok[key]
    proc = subprocess.run([clang] + flags + ["-include-pch", str(pch_header) + ".pch", "-fsyntax-only",
                           "-x", "c++", "-include", str(path), os.devnull], capture_output=True, text=True)
    first = next((l for l in proc.stderr.splitlines() if ": error:" in l or "fatal error" in l), "")
    result = (proc.returncode == 0, first)
    with _header_lock:
        _header_ok[key] = result
    return result


class Doc:
    def __init__(self, path, index, args, flags, pch):
        self.path = path
        self.rel = str(path.relative_to(ROOT)) if ROOT in path.parents else str(path)
        self.index = index
        self.args = args
        self.flags = flags
        self.pch = pch["B"] if "BlockDiagram" in path.name else pch["A"]
        self.work = args.work / "docs"
        self.work.mkdir(parents=True, exist_ok=True)
        self.tag = re.sub(r"\W", "_", path.stem) + "_" + hashlib.sha1(str(path).encode()).hexdigest()[:6]
        self.lines, self.blocks = read_blocks(path)
        self.text = "\n".join(self.lines)
        self.classes = doc_classes(path.stem.replace("Examples", ""), self.text, index)
        self.findings = []
        self.notes = []
        self.context_left = {}
        self.gen_map = {}
        self.gen_next = 1

    def extra_includes(self):
        """Headers outside the shared one that the checks need: the doc's
        own includes, and where the types it uses or copies are defined."""
        paths = []
        for b in self.blocks:
            for kind, i, j in b.chunks:
                m = re.match(r'\s*#\s*include\s*"([^"]+)"', b.lines[i]) if kind == "pp" else None
                if m:
                    for d in INCLUDE_DIRS:
                        if (d / m.group(1)).exists():
                            paths.append((d / m.group(1)).resolve())
                            break
        qs = [q for short in self.classes[:12] for q in self.index.qualify(short)[:2]]
        qs += [q for _, cq, _, _ in self.copies for q in cq[:2]]
        qs += [q for _, _, _, owner in self.items for q in (owner or [])]
        for q in qs:
            p = self.index.defined_in.get(q)
            if p is not None:
                paths.append(p.resolve())
        out = []
        for p in paths:
            if INCLUDE in p.parents:
                continue        # in the shared header already
            line = '#include "%s"' % p
            if line in out:
                continue
            ok, why = header_compiles(p, self.args.clang, self.flags, self.pch)
            if not ok:
                self.notes.append("not checked against %s, which does not compile here: %s"
                                  % (p.relative_to(ROOT), why.split("error: ")[-1]))
                continue
            out.append(line)
        return out

    def gen(self, what):
        n = self.gen_next
        self.gen_next += 1
        self.gen_map[n] = what
        return '#line %d "%s"' % (n, GEN)

    def add(self, line, msg):
        self.findings.append((line, msg))

    # -- the parts of each block --------------------------------------------

    def split(self):
        """Per block: chunks to compile, listing declarations, type copies."""
        self.items = []         # listing entries: (doc line, decl, is_field, owner_q or None)
        self.copies = []        # (doc line of the body, qualified names, kind, body text)
        for b in self.blocks:
            clean = clean_lines(b.lines)
            b.clean = clean
            b.chunks = []
            for kind, i, j in chunk_block(clean):
                text = "\n".join(clean[i:j + 1])
                if kind == "def":
                    copy = self.type_copy(b, i, j, text)
                    if copy:
                        continue
                    m = re.match(r"^\s*namespace\s+([\w:]+)\s*\{", text)
                    if m and not self.has_bodies(text):
                        # A header outline: its types are copies, its
                        # declarations a listing.
                        inner = text[text.find("{") + 1:text.rfind("}")]
                        offset = text[:text.find("{") + 1].count("\n")
                        ns = [x for x in m.group(1).split("::") if x != "UltraCanvas"]
                        self.outline(b, i + offset, inner, ns)
                        continue
                b.chunks.append((kind, i, j))
            # A block of only declarations is a listing; a function with a
            # body is example code, even when it is all the block holds.
            code = [(k, i, j) for k, i, j in b.chunks if k != "pp"]
            defines = any(k == "def" and "{" in body and is_function_head(body.split("{")[0])
                          and not is_member_head(body.split("{")[0])
                          for k, i, j in code
                          for body in ["\n".join(clean[i:j + 1])])
            if code and not defines:
                text = "\n".join("\n".join(clean[i:j + 1]) for k, i, j in code)
                listing = listing_of(text, self.index.qualified)
                if listing and listing[0]:
                    self.listing(b, code[0][1], text, listing, None)
                    b.chunks = [c for c in b.chunks if c[0] == "pp"]
                elif listing:
                    # Only fields: a listing of a class's members, or local
                    # variables of an example. It stays code, and its names
                    # are checked as members; check_listing keeps the
                    # verdict only when most of them are members.
                    for f in listing[1]:
                        f["tentative"] = b.index
                    self.listing(b, code[0][1], text, listing, None)

    @staticmethod
    def has_bodies(text):
        for s in statements(text[text.find("{") + 1:text.rfind("}")]):
            if s.endswith("}") and is_function_head(s[:s.find("{")]):
                return True
            if re.match(r"^\s*namespace\b", s) and Doc.has_bodies(s):
                return True
        return False

    def outline(self, b, first, inner, ns=()):
        for s in statements(inner):
            m = re.match(r"^\s*namespace\s+([\w:]+)", s)
            if m:
                inner_ns = list(ns) + [x for x in m.group(1).split("::") if x != "UltraCanvas"]
                self.outline(b, first + inner[:inner.find(s)].count("\n"), s[s.find("{") + 1:s.rfind("}")], inner_ns)
                continue
            m = re.match(r"^\s*(?:template\s*<[^>]*>\s*)?(class|struct|enum\s+class|enum|union)\s+(\w+)", s)
            line = first + inner[:inner.find(s.split("\n")[0])].count("\n")
            if m:
                self.copy_of(b, line, m.group(2), m.group(1), s)
                continue
            listing = listing_of(s, self.index.qualified)
            if listing and listing[0]:
                for d in listing[0] + listing[1]:
                    d["ns"] = "::".join(["UltraCanvas"] + list(ns))
                self.listing(b, line, s, listing, None)

    def type_copy(self, b, i, j, text):
        m = re.match(r"^\s*(?:template\s*<[^>]*>\s*)?(class|struct|enum\s+class|enum|union)\s+(\w+)\b[^;{]*\{", text)
        if not m or m.group(2) not in self.index.qualified:
            return False
        self.copy_of(b, i, m.group(2), m.group(1), text)
        return True

    def copy_of(self, b, line_in_block, name, kind, text):
        qs = self.index.qualify(name)
        if not qs or "{" not in text:
            return
        body = text[text.find("{") + 1:text.rfind("}")]
        first = b.first + line_in_block + text[:text.find("{")].count("\n")
        self.copies.append((first, qs, kind, body))

    def listing(self, b, line_in_block, text, listing, owner):
        decls, fields = listing
        for d in decls + fields:
            k = text.find(d["text"].split("\n")[0].strip()[:40])
            line = b.first + line_in_block + (text[:k].count("\n") if k >= 0 else 0)
            self.items.append((line, d, d in fields, owner))

    # -- the checks -----------------------------------------------------------

    def check(self):
        self.split()
        self.includes = self.extra_includes()
        self.check_includes()
        self.check_examples()
        self.check_copies()
        self.check_listing()
        self.check_prose()
        self.findings = sorted(set(self.findings))
        return {"doc": self.rel, "blocks": len(self.blocks), "findings": self.findings, "notes": self.notes,
                "context": {k: sorted(set(v)) for k, v in self.context_left.items()}}

    def check_includes(self):
        for b in self.blocks:
            for kind, i, j in b.chunks:
                if kind != "pp":
                    continue
                m = re.match(r'\s*#\s*include\s*"([^"]+)"', b.lines[i])
                if m and not any((d / m.group(1)).exists() for d in INCLUDE_DIRS):
                    self.add(b.first + i, 'header "%s" does not exist' % m.group(1))

    def emit_examples(self, assumed):
        self.gen_map = {}
        self.gen_next = 1
        out = list(self.includes)
        for b in self.blocks:
            for kind, i, j in b.chunks:
                m = re.match(r"\s*#\s*include\s*<([^>]+)>", b.lines[i]) if kind == "pp" else None
                if m:
                    out += ["#if __has_include(<%s>)" % m.group(1), "#include <%s>" % m.group(1), "#endif"]
        for b in self.blocks:
            code = [c for c in b.chunks if c[0] != "pp"]
            if not code:
                continue
            out.append(self.gen(None))
            out.append("namespace __dc_b%d {" % b.index)
            out.append("using namespace ::UltraCanvas;")      # what an app has
            for k, name in sorted(b.uses):
                out.append("using __dc_b%d::%s;" % (k, name))
            for decl in assumed:
                out.append(self.gen(("assumed", decl)))
                out.append(decl)
            for name, t in sorted(b.context.items()):
                out.append(self.gen(("context", b.index, name)))
                out.append("extern %s %s;" % (t, name))
            for kind, i, j in code:
                if kind == "def":
                    out.append('#line %d "%s"' % (b.first + i, self.rel))
                    out.extend(b.lines[i:j + 1])
            stmts = [(i, j) for kind, i, j in code if kind == "stmt"]
            if stmts:
                out.append(self.gen(None))
                out.append("struct __Run { auto __run() {")
                for name, t in sorted(b.context.items()):
                    out.append(self.gen(("context", b.index, name)))
                    out.append("%s& %s = *static_cast<%s*>(nullptr);" % (t, name, t))
                for i, j in stmts:
                    out.append('#line %d "%s"' % (b.first + i, self.rel))
                    out.extend(b.lines[i:j + 1])
                out.append(self.gen(None))
                out.append("} };")
            out.append(self.gen(None))
            out.append("}")
        return "\n".join(out) + "\n"

    def check_examples(self):
        declared = context_types("\n".join("\n".join(b.clean) for b in self.blocks), self.index)
        main = self.classes[0] if self.classes else None
        assumed = []
        for m in re.finditer(r"<!--\s*doc-check:(.*?)-->", self.text, re.S):
            for st in statements(m.group(1)):
                f = parse_field(st) if "(" not in st else None
                if f:
                    body = st.rstrip(";").strip()
                    declared[f["name"]] = body[:body.rfind(f["name"])].strip()
                else:
                    assumed.append(st if st.endswith(";") else st + ";")
        errors = []
        for _ in range(5):
            if not any(c[0] != "pp" for b in self.blocks for c in b.chunks):
                return
            errors = run_clang(self.args.clang, self.flags, self.pch, self.emit_examples(assumed),
                               self.work, self.tag + "_ex", self.rel)
            changed = False
            for f, line, msg in errors:
                if f == GEN:
                    what = self.gen_map.get(line)
                    other = re.search(r"did you mean '__dc_b(\d+)::(\w+)'", msg)
                    if what and what[0] == "context" and other:
                        b = self.blocks[what[1]]
                        key = (int(other.group(1)), other.group(2))
                        if key[0] < b.index and key not in b.uses:
                            b.uses.add(key)
                            changed = True
                            continue
                    if what and what[0] == "context":
                        b = self.blocks[what[1]]
                        if what[2] in b.context:
                            del b.context[what[2]]
                            b.bad_context.add(what[2])
                            changed = True
                    continue
                other = re.search(r"did you mean '__dc_b(\d+)::(\w+)'", msg)
                if other and f == self.rel:
                    b = next((b for b in self.blocks if b.first <= line <= b.last()), None)
                    key = (int(other.group(1)), other.group(2))
                    if b is not None and key[0] < b.index and key not in b.uses:
                        b.uses.add(key)
                        changed = True
                    continue
                m = re.match(r"use of undeclared identifier '(\w+)'", msg)
                if not m or f != self.rel:
                    continue
                name = m.group(1)
                if not (name[0].islower() or name[0] == "_"):
                    continue
                b = next((b for b in self.blocks if b.first <= line <= b.last()), None)
                if b is None or name in b.context or name in b.bad_context:
                    continue
                t = guess_type(name, declared, main, self.index)
                if t:
                    b.context[name] = t
                    changed = True
            if not changed:
                break
        for f, line, msg in errors:
            if f == GEN:
                what = self.gen_map.get(line)
                if what and what[0] == "assumed":
                    self.add(0, "doc-check declaration `%s`: %s" % (what[1], msg))
                continue
            if f != self.rel:
                self.add(0, "%s:%d: %s" % (f, line, msg))
                continue
            m = re.match(r"use of undeclared identifier '(\w+)'", msg)
            if m and (m.group(1)[0].islower() or m.group(1)[0] == "_"):
                self.context_left.setdefault(m.group(1), []).append(line)
                continue
            if msg.startswith("redefinition of") or "redeclaration of" in msg:
                continue
            m = re.match(r"unknown type name '(\w+)'$", msg)
            if m and m.group(1) not in self.index.types and not m.group(1).startswith(("UltraCanvas", "UC")):
                self.context_left.setdefault(m.group(1), []).append(line)
                continue
            self.add(line, msg)

    def check_copies(self):
        """Fields and enumerators of a type copy must exist in the real type;
        its functions go to the listing check with the type as owner."""
        src = self.includes + ["namespace __dc_c {", "using namespace ::UltraCanvas;"]
        where = {}
        n = 0
        for first, qs, kind, body in self.copies:
            if kind.startswith("enum"):
                for e in split_top(body):
                    e = e.split("=")[0].strip()
                    if re.match(r"^\w+$", e):
                        line = first + body[:body.find(e)].count("\n")
                        n += 1
                        where[n] = (line, "%s::%s" % (qs[0], e))
                        src.append('#line %d "%s"' % (n, GEN))
                        src.append("inline void __c%d() { (void)::%s::%s; }" % (n, qs[0], e))
                continue
            for st in statements(body):
                st0 = re.sub(r"^\s*(?:public|private|protected)\s*:\s*", "", st).strip()
                if not st0 or re.match(r"^(?:template\s*<[^>]*>\s*)?(class|struct|enum|union|using|typedef|friend)\b", st0):
                    continue
                line = first + body[:body.find(st0.split("\n")[0])].count("\n")
                d = parse_decl(st0)
                if d:
                    if d["name"].startswith("~"):
                        continue
                    self.items.append((line, d, False, qs))
                    continue
                f = parse_field(st0)
                if not f:
                    continue
                more = [re.split(r"[={]", part)[0].strip() for part in split_top(st0)[1:]]
                for fname in [f["name"]] + [x for x in more if re.match(r"^[A-Za-z_]\w*$", x)]:
                    n += 1
                    where[n] = (line, "%s::%s" % (qs[0], fname))
                    src.append('#line %d "%s"' % (n, GEN))
                    src.append("inline void __c%d() { (void)sizeof(&::%s::%s); }" % (n, qs[0], fname))
        src.append("}")
        if not where:
            return
        errors = run_clang(self.args.clang, self.flags, self.pch, "\n".join(src) + "\n", self.work, self.tag + "_c")
        for f, line, msg in errors:
            if f == GEN and line in where and "protected" not in msg and "private" not in msg \
                    and "overloaded" not in msg and "non-static" not in msg:
                doc_line, what = where[line]
                self.add(doc_line, "'%s' does not exist (%s)" % (what.split("::", 1)[-1] if what.startswith("UltraCanvas::") else what, msg))

    def namespace_of(self, q):
        parts = q.split("::")
        for k in range(len(parts) - 1, 0, -1):
            cand = "::".join(parts[:k])
            if cand in self.index.namespaces:
                return cand
        return "UltraCanvas"

    def candidates(self):
        out = []
        for short in self.classes[:12]:
            out.extend(self.index.qualify(short)[:2])
        return out

    def check_listing(self):
        if not self.items:
            return
        cands = self.candidates()
        # Stage 1: whose member is it (or a free function)?
        src = self.includes + ["namespace __dc_m {", "using namespace ::UltraCanvas;"]
        where = {}
        n = 0
        for k, (line, d, is_field, owner) in enumerate(self.items):
            if d.get("ctor") and d["name"] in self.index.qualified and not owner:
                continue
            for q in (owner or cands):
                if d.get("ctor") and q.split("::")[-1] == d["name"]:
                    continue
                n += 1
                where[n] = (k, q)
                src.append('#line %d "%s"' % (n, GEN))
                src.append("inline void __m%d() { (void)sizeof(&::%s::%s); }" % (n, q, d["name"]))
            if not owner:
                n += 1
                where[n] = (k, None)
                src.append('#line %d "%s"' % (n, GEN))
                src.append("using ::%s::%s;" % (d.get("ns", "UltraCanvas"), d["name"]))
        src.append("}")
        errors = run_clang(self.args.clang, self.flags, self.pch, "\n".join(src) + "\n", self.work, self.tag + "_m")
        bad = {}
        for f, line, msg in errors:
            if f != GEN:
                continue
            if any(s in msg for s in ("overloaded function", "protected member", "unexpected type name",
                                      "non-static member", "bit-field", "unresolved overloaded")):
                continue
            bad.setdefault(line, msg)
        owners = {}
        private = {}
        for num, (k, q) in where.items():
            if num in bad:
                if "private member" in bad[num]:
                    private[k] = q
                continue
            owners.setdefault(k, []).append(q)
        sig = []
        pending = {}        # tentative block -> findings held back
        members = {}        # tentative block -> [names that are members, names in all]
        for k, (line, d, is_field, owner) in enumerate(self.items):
            group = d.get("tentative")
            if group is not None:
                counts = members.setdefault(group, [0, 0])
                counts[1] += 1
                if k in owners:
                    counts[0] += 1
                else:
                    names = ", ".join(q.split("::")[-1] for q in cands[:4]) or "the doc's classes"
                    pending.setdefault(group, []).append(
                        (line, "'%s' is not a member of %s" % (d["name"], names)))
                continue
            ctor_of = None
            if d.get("ctor"):
                for q in (owner or self.index.qualify(d["name"])):
                    if q.split("::")[-1] == d["name"]:
                        ctor_of = q
                        break
            if ctor_of:
                sig.append((k, ctor_of, "ctor"))
                continue
            if k not in owners:
                main_q = self.index.qualify(self.classes[0])[:2] if self.classes else []
                if k in private and (owner or private[k] in main_q):
                    if not owner:
                        self.add(line, "'%s' is a private member of %s" % (d["name"], private[k]))
                else:
                    names = ", ".join(q.split("::")[-1] for q in (owner or cands)[:4]) or "the doc's classes"
                    self.add(line, "'%s' is not a member of %s%s" % (d["name"], names, "" if owner else ", nor a free function"))
                continue
            if not is_field:
                for q in owners[k]:
                    sig.append((k, q, "fn"))
        for group, (hits, total) in members.items():
            if hits * 2 >= total:
                for finding in pending.get(group, []):
                    self.add(*finding)
        # Stage 2: the signature, written inside the owner (a class derived
        # from it, in its namespace), so that the doc's unqualified types
        # resolve as they do in the header.
        src = self.includes + [
            "template<class C, class F> struct __dc_ctor;",
            "template<class C, class... A> struct __dc_ctor<C, void(A...)> "
            "{ static constexpr bool value = std::is_constructible_v<C, A...>; };"]
        where = {}
        n = 0
        for k, q, what in sig:
            line, d, _, _ = self.items[k]
            if "template" in d["text"] or any("..." in p or "auto" in p.split() for p in d["params"]):
                continue
            params = ", ".join(d["params"])
            ret = d["ret"]
            cv = " const" if d["const"] else ""
            if q is None:
                checks = ["static_cast<%s (*)(%s)>(&::%s::%s)" % (ret, params, d.get("ns", "UltraCanvas"), d["name"])]
            elif what == "ctor":
                checks = ["__dc_ctor<::%s, void(%s)>::value" % (q, params)]
            else:
                checks = ["static_cast<%s (::%s::*)(%s)%s>(&::%s::%s)" % (ret, q, params, cv, q, d["name"])]
                if "static" in d["pre"] or not d["const"]:
                    checks.append("static_cast<%s (*)(%s)>(&::%s::%s)" % (ret, params, q, d["name"]))
            ns = self.namespace_of(q) if q else d.get("ns", "UltraCanvas")
            for check in checks:
                n += 1
                where[n] = (k, q)
                src.append('#line %d "%s"' % (n, GEN))
                if what == "ctor":
                    body = "static_assert(%s);" % check
                else:
                    body = "static void __f() { (void)%s; }" % check
                if q and not self.index.is_final(q):
                    # &__dc_sN::f, not &Q::f: a protected member is reachable
                    # from the derived class, and has the same type.
                    body = body.replace("(&::%s::%s)" % (q, d["name"]), "(&__dc_s%d::%s)" % (n, d["name"]))
                    src.append("namespace %s { struct __dc_s%d : ::%s { %s }; }" % (ns, n, q, body))
                elif what == "ctor":
                    src.append("namespace %s { %s }" % (ns, body))
                else:
                    src.append("namespace %s { struct __dc_s%d { %s }; }" % (ns, n, body))
        if n == 0:
            return
        errors = run_clang(self.args.clang, self.flags, self.pch, "\n".join(src) + "\n", self.work, self.tag + "_s")
        failed = {line for f, line, msg in errors if f == GEN}
        ok, tried = set(), {}
        for num, (k, q) in where.items():
            tried.setdefault(k, []).append(q)
            if num not in failed:
                ok.add(k)
        for k, qs in tried.items():
            if k in ok:
                continue
            line, d, _, _ = self.items[k]
            owner_q = next((q for q in qs if q), None)
            name = d["name"]
            decls = self.index.header_lines(name, owner_q)[:3]
            hint = (" - the header has: " + " | ".join(x.split(": ", 1)[1] for x in decls)) if decls else ""
            self.add(line, "signature differs: `%s`%s" % (" ".join(d["text"].split()), hint))

    def check_prose(self):
        """`Name(` in backticks outside code must be a function somewhere."""
        fence = False
        for i, line in enumerate(self.lines, 1):
            if re.match(r"^\s*```", line):
                fence = not fence
                continue
            if fence:
                continue
            for span in re.findall(r"`([^`]+)`", line):
                for m in re.finditer(r"(?:->|\.|::|^|\s)([A-Z]\w+)\s*\(", span):
                    name = m.group(1)
                    if name not in self.index.functions and name not in self.index.types:
                        self.add(i, "prose names `%s(`, which no header declares" % name)


def check_doc(path, args, index, flags, pch):
    try:
        return Doc(path, index, args, flags, pch).check()
    except Exception as exc:     # a checker bug must not hide the other docs
        return {"doc": str(path), "blocks": 0, "findings": [(0, "checker failed: %r" % exc)], "context": {},
                "notes": []}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("docs", nargs="*", type=Path)
    parser.add_argument("--work", type=Path, default=ROOT / "build" / "doc-examples",
                        help="where the shared header and generated sources go")
    parser.add_argument("--clang", default=shutil.which("clang++") or "clang++")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--show-context", action="store_true",
                        help="also list the names a doc's snippets use without declaring")
    args = parser.parse_args()
    docs = args.docs or sorted((ROOT / "Docs" / "UltraCanvas").glob("*Examples*.md"))
    docs = [d.resolve() for d in docs]
    args.work.mkdir(parents=True, exist_ok=True)

    flags = base_flags()
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        fa = pool.submit(build_pch, args.work, "umbrellaA", CLASH_A, flags, args.clang)
        fb = pool.submit(build_pch, args.work, "umbrellaB", CLASH_B, flags, args.clang)
        pch = {"A": fa.result(), "B": fb.result()}
    index = HeaderIndex()

    total = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(lambda d: check_doc(d, args, index, flags, pch), docs))
    for r in results:
        total += len(r["findings"])
        status = "ok" if not r["findings"] else "%d finding(s)" % len(r["findings"])
        print("%s: %d blocks, %s" % (r["doc"], r["blocks"], status))
        for line, msg in r["findings"]:
            print("  %s:%d: %s" % (r["doc"], line, msg) if line else "  %s: %s" % (r["doc"], msg))
        for note in r["notes"]:
            print("  note: " + note)
        if args.show_context and r["context"]:
            print("  context (untyped names): " + ", ".join(
                "%s@%s" % (k, ",".join(map(str, v[:3]))) for k, v in sorted(r["context"].items())))
    print("%d doc(s), %d finding(s)" % (len(results), total))
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
