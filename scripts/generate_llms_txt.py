#!/usr/bin/env python3
"""Generate llms.txt and llms-full.txt from the UltraCanvas docs corpus.

llms.txt      - index per the llms.txt convention (https://llmstxt.org):
                title, summary, and sections of markdown links with one-line
                descriptions extracted from each document.
llms-full.txt - concatenation of the full corpus for direct LLM ingestion.

Run from anywhere; paths are resolved relative to the repository root.
Deterministic output (stable ordering) so CI can diff against the committed
files. Standard library only.
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
RAW_BASE = "https://raw.githubusercontent.com/ULTRA-OS-Project/UltraCanvas/main"

# Files excluded from both outputs: changelogs are long and low-value for
# code-writing agents; proposals/plans describe unbuilt work.
EXCLUDE_NAMES = {"CHANGELOG.md"}
EXCLUDE_PATTERNS = ("Proposal", "Plan", "DesignVariants")

# Per-application doc directories under Docs/ to index. Only Docs/*.md,
# Docs/UltraCanvas/ and Docs/Modules/ are walked otherwise, so an app's docs
# are invisible to the corpus until its directory is listed here.
#
# This is an allowlist rather than a glob over Docs/*/ on purpose: several
# sibling directories (Research, Video, VideoScripts) hold material that is
# not developer documentation, and sweeping them in would dilute the corpus.
# Add a directory here when its contents are meant for the LLM-facing docs.
APP_DOC_DIRS = ("Ladybird", "UltraAuthenticator")

SUMMARY = (
    "UltraCanvas is a modular cross-platform C++20 UI and rendering framework "
    "(Windows, Linux, macOS, WebAssembly, ULTRA OS) with sibling modules for "
    "AI capabilities (UltraAI), networking (UltraNet), databases "
    "(UltraDatabase), file loading (FileLoader) and a plugin system. "
    "Identifiers use PascalCase; widgets are std::shared_ptr-managed and "
    "created via factory helpers; consult the per-component document before "
    "using any widget."
)

METADATA_RE = re.compile(r"^\*\*(Version|Last Modified|Author|Status|Date)\b")


def is_excluded(path: Path) -> bool:
    if path.name in EXCLUDE_NAMES:
        return True
    return any(p in path.name for p in EXCLUDE_PATTERNS)


def extract_description(text: str, limit: int = 220) -> str:
    """First real prose paragraph, preferring the '## Overview' section."""
    lines = text.splitlines()
    start = 0
    for i, line in enumerate(lines):
        if re.match(r"^##\s+Overview\b", line):
            start = i + 1
            break
    para: list[str] = []
    in_metadata = False
    for line in lines[start:]:
        s = line.strip()
        if not s:
            in_metadata = False
            if para:
                break
            continue
        if METADATA_RE.match(s):
            # A "**Field:** value" header may wrap onto the following lines.
            # Skip the whole run up to the next blank line, so a continuation
            # is not mistaken for the opening prose paragraph.
            in_metadata = True
            if para:
                break
            continue
        if in_metadata:
            continue
        if s.startswith(("#", "```", "|", "!", "---", ">")):
            if para:
                break
            continue
        para.append(s)
    desc = " ".join(para)
    desc = re.sub(r"\*\*?", "", desc)               # strip bold/italic markers
    desc = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", desc)  # strip links
    desc = re.sub(r"\s+", " ", desc).strip()
    if len(desc) > limit:
        desc = desc[: limit - 1].rsplit(" ", 1)[0] + "…"
    return desc


def title_of(path: Path, text: str) -> str:
    m = re.search(r"^#\s+(.+)$", text, re.MULTILINE)
    return m.group(1).strip() if m else path.stem


def collect() -> dict[str, list[Path]]:
    sections: dict[str, list[Path]] = {
        "Framework overview": [],
        "Components and subsystems": [],
        "Modules": [],
        "Applications": [],
    }
    for name in ("README.md", "Masterfile_modules.md"):
        p = REPO_ROOT / name
        if p.exists():
            sections["Framework overview"].append(p)
    for p in sorted((REPO_ROOT / "Docs").glob("*.md")):
        if not is_excluded(p):
            sections["Framework overview"].append(p)
    for p in sorted((REPO_ROOT / "Docs" / "UltraCanvas").glob("*.md")):
        if not is_excluded(p):
            sections["Components and subsystems"].append(p)
    for p in sorted((REPO_ROOT / "Docs" / "Modules").rglob("*.md")):
        if not is_excluded(p):
            sections["Modules"].append(p)
    for name in APP_DOC_DIRS:
        for p in sorted((REPO_ROOT / "Docs" / name).rglob("*.md")):
            if not is_excluded(p):
                sections["Applications"].append(p)
    return sections


def main() -> int:
    sections = collect()

    index = ["# UltraCanvas", "", f"> {SUMMARY}", ""]
    full = [
        "# UltraCanvas — full documentation corpus",
        "",
        f"> {SUMMARY}",
        "",
        "Generated by scripts/generate_llms_txt.py — do not edit by hand.",
        "",
    ]
    count = 0
    for section, paths in sections.items():
        index += [f"## {section}", ""]
        for p in paths:
            text = p.read_text(encoding="utf-8", errors="replace")
            rel = p.relative_to(REPO_ROOT).as_posix()
            desc = extract_description(text) or title_of(p, text)
            index.append(f"- [{title_of(p, text)}]({RAW_BASE}/{rel}): {desc}")
            full += [
                "=" * 78,
                f"FILE: {rel}",
                "=" * 78,
                "",
                text.rstrip(),
                "",
            ]
            count += 1
        index.append("")

    (REPO_ROOT / "llms.txt").write_text("\n".join(index).rstrip() + "\n", encoding="utf-8")
    (REPO_ROOT / "llms-full.txt").write_text("\n".join(full).rstrip() + "\n", encoding="utf-8")
    size = (REPO_ROOT / "llms-full.txt").stat().st_size
    print(f"llms.txt: {count} documents indexed; llms-full.txt: {size/1024:.0f} KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
