#!/usr/bin/env python3
"""Fold pending changelog entries into the changelog under the next version.

Line 1 of `Docs/UltraCanvas/CHANGELOG.md` IS the framework's version:
`cmake/UltraCanvasVersion.cmake` reads it and every `project(VERSION ...)`,
compile definition and packaging script follows. Every branch therefore wanted
to write that one line, and two open at once always collided — on 2026-09-23 a
single branch was renumbered five times in one morning (0.9.23 -> 0.9.27 ->
0.9.28 -> 0.9.29 -> 0.9.31), each renumber throwing away a six-platform CI
matrix, and 0.9.29 was consumed and lost in the churn.

Branches now drop their bullets in `Docs/UltraCanvas/changelog.d/<name>.md`
with no header and no number — two files never conflict — and this script
assigns the number ONCE, on main, after the merge:
`.github/workflows/changelog-fold.yml` runs it on every push to main.

    python3 scripts/fold_changelog.py            # fold, print the new version
    python3 scripts/fold_changelog.py --check    # report, change nothing
    python3 scripts/fold_changelog.py --version 0.10.0   # override the bump

Exit code is 0 whether or not anything was pending — a push with no entries is
the normal case, not an error. It is 1 only when something is wrong: an entry
that carries its own header, an unparsable line 1, a version that would not go
forwards.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CHANGELOG = REPO / "Docs" / "UltraCanvas" / "CHANGELOG.md"
ENTRIES = REPO / "Docs" / "UltraCanvas" / "changelog.d"

# `#### YYYY-MM-DD *x.y.z*` — the format cmake/UltraCanvasVersion.cmake parses.
HEADER = re.compile(r"^#### (\d{4}-\d{2}-\d{2}) \*(\d+(?:\.\d+)*)\*\s*$")


def pending(directory: Path) -> list[Path]:
    """Entry files, in a deterministic order. README.md documents, not entries."""
    if not directory.is_dir():
        return []
    return sorted(p for p in directory.glob("*.md") if p.name != "README.md")


def versions(text: str) -> list[tuple[int, ...]]:
    return [tuple(int(p) for p in m.group(2).split("."))
            for m in (HEADER.match(line) for line in text.splitlines()) if m]


def show(version: tuple[int, ...]) -> str:
    return ".".join(str(p) for p in version)


def next_version(text: str) -> tuple[int, ...]:
    """One patch above the highest version in the file.

    The highest, not line 1: the file carries sixteen duplicated numbers from
    before any of this was checked, and a malformed line 1 must not be able to
    hand out a number that goes backwards.
    """
    found = versions(text)
    if not found:
        raise ValueError(f"{CHANGELOG.name} has no `#### YYYY-MM-DD *x.y.z*` entry")
    top = max(found)
    if len(top) < 3:
        raise ValueError(f"version {show(top)} is not x.y.z")
    return (*top[:-1], top[-1] + 1)


def read_entry(path: Path) -> str:
    """One entry's bullets, with the header rule enforced."""
    text = path.read_text(encoding="utf-8").strip("\n")
    for number, line in enumerate(text.splitlines(), start=1):
        if HEADER.match(line) or line.startswith("#### "):
            raise ValueError(
                f"{path.relative_to(REPO)}:{number}: a pending entry must not carry its "
                f"own version header — the number is assigned here, on main, which is "
                f"the whole point of the directory")
    if not text.strip():
        raise ValueError(f"{path.relative_to(REPO)}: empty")
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="report what would be folded and change nothing")
    ap.add_argument("--version", metavar="X.Y.Z",
                    help="use this version instead of bumping the patch")
    ap.add_argument("--date", metavar="YYYY-MM-DD",
                    help="date for the entry (default: today, UTC)")
    args = ap.parse_args()

    entries = pending(ENTRIES)
    if not entries:
        print("fold_changelog: nothing pending.")
        return 0

    text = CHANGELOG.read_text(encoding="utf-8")
    try:
        bullets = [read_entry(p) for p in entries]
        if args.version:
            version = tuple(int(p) for p in args.version.split("."))
            highest = max(versions(text), default=())
            if version <= highest:
                raise ValueError(
                    f"--version {args.version} is not above {show(highest)}, the highest "
                    f"version in the file; the product's version would go backwards")
        else:
            version = next_version(text)
    except ValueError as exc:
        print(f"fold_changelog: {exc}")
        return 1

    date = args.date or dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d")
    entry = f"#### {date} *{show(version)}*\n" + "\n".join(bullets) + "\n"

    names = ", ".join(p.name for p in entries)
    if args.check:
        print(f"fold_changelog: would fold {len(entries)} entr"
              f"{'y' if len(entries) == 1 else 'ies'} ({names}) as {show(version)}.")
        return 0

    CHANGELOG.write_text(entry + "\n" + text, encoding="utf-8")
    for path in entries:
        path.unlink()

    print(f"fold_changelog: folded {len(entries)} entr"
          f"{'y' if len(entries) == 1 else 'ies'} ({names}) as {show(version)}.")
    # The workflow reads this to write the commit message and skip a no-op push.
    print(f"::folded::{show(version)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
