#!/usr/bin/env python3
"""Checks the changelogs that cmake/UltraCanvasVersion.cmake turns into version numbers.

Why this exists
---------------
The first line of each changelog IS the product's version: CMake reads it with
`file(STRINGS ... LIMIT_COUNT 1)` and every `project(VERSION ...)`, compile
definition and packaging script follows from it. That makes line 1 of a shared
file the single most contended line in the repository - every pull request wants
to add its entry there, and two that are open at once always collide.

The collision has two shapes, and this script refuses both:

  Stale.     A pull request picks the next number when it is opened, main
             releases further versions while it waits for review, and the
             number is behind by the time it merges. The entry lands on top of
             the file carrying a number lower than versions already released
             below it - so the whole product's version goes BACKWARDS, and the
             build reports and packages a release that already shipped.
             (0.8.53 did exactly this, over a main that had reached 0.8.60.)

  Shared.    Two pull requests both write `#### <date> *0.8.62*` as line 1.
             Git sees an identical context line, merges the two bullet lists
             under the one header without a conflict, and two unrelated
             releases end up sharing a number - while the version never
             increments at all. This is also why such a pull request's
             changelog diff never goes away no matter how often main is merged
             into it: its bullets are not in main's copy of that entry, so they
             are still an addition, and the two pull requests keep rewriting
             the same lines and invalidating each other.

Both are caught by one rule, applied per changelog:

    the version on line 1 must be unique in the file, and strictly greater
    than every other version in it.

History below line 1 is not policed. The file carries sixteen duplicate
version numbers from before this check existed, some of them months old and
long since released; renumbering a published release would be a lie, so they
are left as they are and only new top entries have to be well-formed.

With --base <ref>, the stricter pull-request rule also applies: a changelog
that this branch modified (its copy differs from the merge base's - not from
<ref>'s head, which also differs whenever <ref> released something the branch
has not merged yet) must claim a version strictly above every version <ref>'s
copy of the file carries. That catches both shapes before the merge:
the same number as <ref> (bullets appended to a released entry, or two
branches choosing one number), and a lower one - the branch picked the next
number, main released past it, and the branch has not merged main since, so
the per-file rule above cannot see the newer releases. Line 1 of <ref>'s copy
is what it is compared with, so <ref> has to be current: fetch it first. The
merge base needs history: in a clone too shallow to find one, every file
that differs from <ref>'s copy is taken as modified, which can only add a
report, never miss one.

Usage:
    python3 scripts/check_changelog.py                 # check the working tree
    python3 scripts/check_changelog.py --base origin/main
Exit code is 0 when clean, 1 when a changelog is malformed or collides.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VERSION_CMAKE = REPO / "cmake" / "UltraCanvasVersion.cmake"

# `#### YYYY-MM-DD *x.y.z*` - the format cmake/UltraCanvasVersion.cmake parses.
HEADER = re.compile(r"^#### (\d{4}-\d{2}-\d{2}) \*(\d+(?:\.\d+)*)\*\s*$")
DECLARE = re.compile(r'^\s*_ultracanvas_declare_product\(\s*(\S+)\s+"([^"]+)"')


def changelogs():
    """The changelog of every product cmake declares, so the two cannot drift."""
    found = []
    for line in VERSION_CMAKE.read_text(encoding="utf-8").splitlines():
        m = DECLARE.match(line)
        if m:
            found.append((m.group(1), m.group(2)))
    if not found:
        sys.exit(f"check_changelog: no products declared in {VERSION_CMAKE}")
    return found


def parse(text):
    """Every version header in the file, as (line number, date, version tuple)."""
    entries = []
    for number, line in enumerate(text.splitlines(), start=1):
        m = HEADER.match(line)
        if m:
            entries.append((number, m.group(1), tuple(int(p) for p in m.group(2).split("."))))
    return entries


def show(version):
    return ".".join(str(p) for p in version)


def git(*args):
    done = subprocess.run(["git", "-C", str(REPO), *args],
                          capture_output=True, text=True)
    return done.stdout if done.returncode == 0 else None


def branch_edited(relative, base, text):
    """Whether this branch changed the file.

    Compared against the merge base with `base`, not against base's head: a
    file that base released on since the branch forked also differs from
    base's head, and it is not this branch's edit. Without a merge base (a
    shallow clone) base's head is the best available and errs towards
    reporting.
    """
    ancestor = git("merge-base", base, "HEAD")
    ref = ancestor.strip() if ancestor else base
    before = git("show", f"{ref}:{relative}")
    # A file the merge base does not have is new on this branch.
    return before is None or before != text


def check(prefix, relative, base):
    path = REPO / relative
    problems = []
    if not path.exists():
        # cmake treats a missing changelog as fatal; say so here rather than
        # letting configure be the one to find out.
        return [f"{relative}: declared by {prefix} in UltraCanvasVersion.cmake but missing"]

    text = path.read_text(encoding="utf-8")
    entries = parse(text)
    if not entries:
        return [f"{relative}: no `#### YYYY-MM-DD *x.y.z*` entry found"]

    first_line, _, top = entries[0]
    if first_line != 1:
        problems.append(
            f"{relative}:1: the version header must be the FIRST line - cmake reads "
            f"only line 1 - but the first one is on line {first_line}")

    rest = entries[1:]
    duplicates = [ln for ln, _, v in rest if v == top]
    if duplicates:
        problems.append(
            f"{relative}:1: version {show(top)} is already used on line "
            f"{', '.join(str(d) for d in duplicates)}. Two releases cannot share a "
            f"number - give this entry the next free one.")

    higher = [(ln, v) for ln, _, v in rest if v > top]
    if higher:
        ln, v = max(higher, key=lambda p: p[1])
        problems.append(
            f"{relative}:1: version {show(top)} is lower than {show(v)} on line {ln}, so "
            f"{prefix}_VERSION would go BACKWARDS. main released further versions while "
            f"this was open - renumber this entry above {show(v)}.")

    if base:
        # Compare the WORKING TREE against base, not HEAD against base: the
        # point of running this before pushing is to catch the collision while
        # the edit is still uncommitted, and `git diff base...HEAD` cannot see
        # that. Reading base's blob and comparing content covers both.
        before = git("show", f"{base}:{relative}")
        if before is not None and branch_edited(relative, base, text):
            was = parse(before)
            # Line 1 of base's copy is its version; the max covers a base that
            # is itself malformed, so the comparison never trusts a bad line 1.
            base_top = max((v for _, _, v in was), default=None)
            if base_top is not None and top == base_top:
                problems.append(
                    f"{relative}:1: this branch edits the changelog but still claims "
                    f"version {show(top)}, the same version {base} is on. Bullets added "
                    f"under a released entry ship unversioned and collide with every "
                    f"other branch doing the same - add a NEW entry above it instead.")
            elif base_top is not None and top < base_top:
                problems.append(
                    f"{relative}:1: version {show(top)} is behind {show(base_top)}, the "
                    f"version {base} is on. main released further versions while this "
                    f"was open and they are not in this branch's copy of the file yet - "
                    f"renumber this entry above {show(base_top)} (and merge {base} so the "
                    f"file carries what was released).")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", metavar="REF",
                    help="also require that a changelog this branch modified does not "
                         "still claim REF's version (e.g. --base origin/main)")
    args = ap.parse_args()

    problems = []
    products = changelogs()
    for prefix, relative in products:
        problems.extend(check(prefix, relative, args.base))

    if problems:
        for p in problems:
            print(f"check_changelog: {p}")
        print(f"\ncheck_changelog: {len(problems)} problem(s) in {len(products)} changelog(s).")
        return 1

    print(f"check_changelog: clean ({len(products)} changelogs).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
