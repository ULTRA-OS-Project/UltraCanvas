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

A third shape is a typo, not a collision, and the rule above lets it through:

  Runaway.   The entry is renumbered above what main released, and the new
             number is not the next free one but something far past it -
             0.9.120 landed on a file whose previous release was 0.9.32,
             from a script that took the highest patch number across every
             minor in the file. "Strictly greater" is satisfied, so nothing
             objected, and that number would have become the released
             version with eighty-odd numbers burnt behind it.

So the top entry must also be CLOSE to the release before it: at most
GAP_LIMIT patch numbers above the next-highest version in the file (and,
with --base, above the base's version). A few numbers of headroom are needed
because every open pull request reserves one and they merge in any order; a
gap wider than that is a mistake. A minor or major bump (0.9.x to 0.10.0) is
a new series, so its patch number only has to be small, not adjacent.

History below line 1 is not policed. The file carries sixteen duplicate
version numbers from before this check existed, some of them months old and
long since released; renumbering a published release would be a lie, so they
are left as they are and only new top entries have to be well-formed.

With --base <ref>, the stricter pull-request rule also applies: a changelog
that this branch modified (its copy differs from the merge base's - not from
<ref>'s head, which also differs whenever <ref> released something the branch
has not merged yet) must claim a version strictly above every version <ref>'s
copy of the file carries, and by no more than GAP_LIMIT. That catches all
three shapes before the merge:
the same number as <ref> (bullets appended to a released entry, or two
branches choosing one number), and a lower one - the branch picked the next
number, main released past it, and the branch has not merged main since, so
the per-file rule above cannot see the newer releases. Line 1 of <ref>'s copy
is what it is compared with, so <ref> has to be current: fetch it first. The
merge base needs history: in a clone too shallow to find one, every file
that differs from <ref>'s copy is taken as modified, which can only add a
report, never miss one. A file identical to <ref>'s copy is never taken as
modified, so the check can be run in the middle of an uncommitted merge of
<ref> without the files <ref> moved on being reported as this branch's.

Pending entries
---------------
Because line 1 is contended, the routine case no longer writes it: a branch
drops its bullets in `Docs/UltraCanvas/changelog.d/<name>.md` with no header
and no number, and `.github/workflows/changelog-fold.yml` assigns the number
once, on main, after the merge. Two branches adding two files never collide,
so neither shape above can happen.

This script refuses a `####` header inside those files. A number chosen on a
branch is exactly the collision the directory exists to end, and one that
slipped through would be folded into the changelog verbatim, giving the entry
two headers.

A top entry written straight into the changelog is still accepted - a hotfix
that has to carry a chosen number is a real case - and is still held to the
rules above.

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

# How far above the previous release a new top entry may sit. Each open pull
# request reserves the next free number and they merge in any order, so the
# gap is legitimately more than one; eight is the widest the framework's
# history shows. Anything past this is a typo, not a queue.
GAP_LIMIT = 10

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


def gap_problem(relative, top, previous, previous_is):
    """The Runaway shape: `top` is above `previous`, but by too much.

    `previous` is the release `top` must follow closely - the next-highest
    version in the file, or the base's version. Same series (all but the
    last component equal): the last component may advance by at most
    GAP_LIMIT. New series (a minor or major bump): the last component must
    itself be small, since a fresh series starts near zero. Returns the
    message, or None when the gap is acceptable. Callers ensure top > previous.
    """
    same_series = top[:-1] == previous[:-1]
    if same_series:
        gap = top[-1] - previous[-1]
        if gap <= GAP_LIMIT:
            return None
        return (f"{relative}:1: version {show(top)} is {gap} numbers past {show(previous)}, "
                f"{previous_is}. A release is at most {GAP_LIMIT} past the one before it "
                f"(open pull requests each hold one number, nothing holds eighty) - a gap "
                f"like this is a renumbering typo. Give this entry the next free number "
                f"above {show(previous)}.")
    if top[-1] <= GAP_LIMIT:
        return None
    return (f"{relative}:1: version {show(top)} starts a new series after {show(previous)}, "
            f"{previous_is}, but not near its beginning: a bumped minor or major begins "
            f"at .0, or within {GAP_LIMIT} of it when open pull requests hold the first "
            f"numbers. This looks like a renumbering typo.")


def git(*args):
    done = subprocess.run(["git", "-C", str(REPO), *args],
                          capture_output=True, text=True)
    return done.stdout if done.returncode == 0 else None


def branch_edited(relative, base, text, base_copy):
    """Whether this branch changed the file.

    Compared against the merge base with `base`, not against base's head: a
    file that base released on since the branch forked also differs from
    base's head, and it is not this branch's edit. Without a merge base (a
    shallow clone) base's head is the best available and errs towards
    reporting.

    A file whose working copy is byte-for-byte base's copy is not this
    branch's edit either, whatever the merge base says. That is the state of
    every file base changed while a merge of base is in progress and not yet
    committed: the merge base still predates the change, so the file
    "differs", yet the branch added nothing to it. Before this exception the
    check, run mid-merge, reported such files as edits that "still claim
    base's version" - a false alarm that went away only once the merge was
    committed, which nobody could tell from the message.
    """
    if base_copy is not None and text == base_copy:
        return False
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
    elif rest and not duplicates:
        # Above everything else in the file, as required - but not by a mile.
        ln, v = max(((ln, v) for ln, _, v in rest), key=lambda p: p[1])
        gap = gap_problem(relative, top, v, f"the next-highest version in this file (line {ln})")
        if gap:
            problems.append(gap)

    if base:
        # Compare the WORKING TREE against base, not HEAD against base: the
        # point of running this before pushing is to catch the collision while
        # the edit is still uncommitted, and `git diff base...HEAD` cannot see
        # that. Reading base's blob and comparing content covers both.
        before = git("show", f"{base}:{relative}")
        if before is not None and branch_edited(relative, base, text, before):
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
            elif base_top is not None:
                gap = gap_problem(relative, top, base_top, f"the version {base} is on")
                if gap:
                    problems.append(gap)
    return problems


def check_pending():
    """Pending entries must not carry a version - the fold assigns it."""
    problems = []
    directory = REPO / "Docs" / "UltraCanvas" / "changelog.d"
    if not directory.is_dir():
        return problems
    for path in sorted(directory.glob("*.md")):
        if path.name == "README.md":
            continue
        relative = path.relative_to(REPO).as_posix()
        text = path.read_text(encoding="utf-8")
        if not text.strip():
            problems.append(f"{relative}: empty - write the bullets, or delete the file")
            continue
        for number, line in enumerate(text.splitlines(), start=1):
            if line.startswith("#### "):
                problems.append(
                    f"{relative}:{number}: a pending entry must not carry a version "
                    f"header - changelog-fold.yml assigns the number on main, which is "
                    f"what keeps two branches from choosing the same one. Leave the "
                    f"bullets; drop the header.")
                break
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
    problems.extend(check_pending())

    if problems:
        for p in problems:
            print(f"check_changelog: {p}")
        print(f"\ncheck_changelog: {len(problems)} problem(s) in {len(products)} changelog(s).")
        return 1

    print(f"check_changelog: clean ({len(products)} changelogs).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
