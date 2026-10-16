#!/usr/bin/env python3
"""Keep a "## Contents" inventory at the top of every demo-app document.

Every markdown file the demo app opens from its "Documentation" button (the
"Docs/....md" paths in Apps/DemoApp/*.cpp) gets a table of contents just
before its first section heading: one link per "##" and "###" heading, each
jumping to that section. The anchors are GitHub's (lower-case, spaces to '-',
punctuation dropped, "-1", "-2" for repeats), which the demo's markdown viewer
resolves the same way (UltraCanvasTextArea::MarkdownHeadingSlug), so a link
jumps on github.com and in the demo app alike.

The block is regenerated in place: from its "## Contents" heading up to the
next heading. Write prose above the first section or inside a section, never
inside the inventory.

    python3 scripts/generate_doc_toc.py           # rewrite the inventories
    python3 scripts/generate_doc_toc.py --check   # fail when one is stale

A mirrored README (MIRRORED_READMES in generate_llms_txt.py) is updated in its
source file; rerun scripts/generate_llms_txt.py afterwards to refresh the
mirror. Standard library only.
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))
from generate_llms_txt import MIRRORED_READMES  # noqa: E402

TOC_HEADING = "## Contents"
TOC_LEVELS = (2, 3)
# Too few sections for an inventory to help.
MIN_ENTRIES = 2
# The changelog is a dated log, not a guide; its versions are not sections.
EXCLUDED = {"Docs/UltraCanvas/CHANGELOG.md", "Docs/CHANGELOG.md"}

DOC_REF_RE = re.compile(r'"(Docs/[^"]+\.md)"')
HEADING_RE = re.compile(r"^(#{1,6}) +(.*?)\s*$")
FENCE_RE = re.compile(r"^\s*(```|~~~)")
LINK_TARGET_RE = re.compile(r"\]\([^)]*\)")


def demo_docs():
    """The documents the demo app's pages open, in a stable order."""
    docs = set()
    for src in sorted((REPO_ROOT / "Apps" / "DemoApp").glob("*.cpp")):
        docs.update(DOC_REF_RE.findall(src.read_text(encoding="utf-8", errors="replace")))
    mirror_sources = {v: k for k, v in MIRRORED_READMES.items()}
    out = []
    for doc in sorted(docs - EXCLUDED):
        doc = mirror_sources.get(doc, doc)
        if (REPO_ROOT / doc).is_file() and doc not in out:
            out.append(doc)
    return out


def slug(text):
    """GitHub's heading anchor for `text` (the heading after its #s)."""
    text = text.rstrip("# ")
    text = LINK_TARGET_RE.sub("]", text)
    out = []
    for ch in text:
        if ord(ch) >= 0x80:
            out.append(ch)
        elif ch.isalnum():
            out.append(ch.lower())
        elif ch == " ":
            out.append("-")
        elif ch in "-_":
            out.append(ch)
    return "".join(out)


def link_text(text):
    """A heading as link text: inner links flattened, brackets escaped."""
    text = text.rstrip("# ")
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    return text.replace("[", "\\[").replace("]", "\\]")


def headings(lines):
    """(index, level, text) of every ATX heading outside code fences."""
    fence = None
    for i, line in enumerate(lines):
        m = FENCE_RE.match(line)
        if m:
            if fence is None:
                fence = m.group(1)
            elif m.group(1) == fence:
                fence = None
            continue
        if fence is not None:
            continue
        h = HEADING_RE.match(line)
        if h:
            yield i, len(h.group(1)), h.group(2)


def build_toc(lines, toc_range):
    """The inventory block for `lines`, skipping the existing block."""
    seen = {}
    entries = []
    for i, level, text in headings(lines):
        in_toc = toc_range and toc_range[0] <= i < toc_range[1]
        explicit = re.search(r"\s*\{#([^}]+)\}$", text)
        if explicit:
            anchor = explicit.group(1)
            text = text[:explicit.start()]
        else:
            anchor = slug(text)
            n = seen.get(anchor, 0)
            seen[anchor] = n + 1
            if n:
                anchor = f"{anchor}-{n}"
        if level in TOC_LEVELS and anchor and not in_toc:
            indent = "  " * (level - TOC_LEVELS[0])
            entries.append(f"{indent}- [{link_text(text)}](#{anchor})")
    return entries


def find_toc(lines):
    """[start, end) of the existing inventory, or None."""
    for i, level, text in headings(lines):
        if f"{'#' * level} {text}" == TOC_HEADING:
            for j, _, _ in headings(lines):
                if j > i:
                    return (i, j)
            return (i, len(lines))
    return None


def update(lines):
    toc_range = find_toc(lines)
    entries = build_toc(lines, toc_range)
    if toc_range:
        start, end = toc_range
        lines = lines[:start] + lines[end:]
    else:
        start = None
    if len(entries) < MIN_ENTRIES:
        return lines
    if start is None:
        start = next((i for i, level, _ in headings(lines) if level >= 2), None)
        if start is None:
            return lines
    block = [TOC_HEADING, ""] + entries + [""]
    if start > 0 and lines[start - 1].strip():
        block = [""] + block
    return lines[:start] + block + lines[start:]


def main():
    check = "--check" in sys.argv[1:]
    stale = []
    for doc in demo_docs():
        path = REPO_ROOT / doc
        original = path.read_text(encoding="utf-8")
        lines = original.split("\n")
        result = "\n".join(update(lines))
        if result != original:
            stale.append(doc)
            if not check:
                path.write_text(result, encoding="utf-8")
    if check and stale:
        for doc in stale:
            print(f"stale inventory: {doc}")
        print("Run: python3 scripts/generate_doc_toc.py  and commit the result.")
        return 1
    if not check:
        print(f"{len(stale)} document(s) updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
