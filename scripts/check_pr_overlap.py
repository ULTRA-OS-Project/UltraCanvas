#!/usr/bin/env python3
"""Will this branch collide with the other open pull requests?

A conflict found on GitHub costs a merge, a regenerate and a full CI matrix
(two hours here) - and on a busy day it is found again an hour later, because
another pull request merged in the meantime. Most of those collisions were
predictable before the push: two branches editing the same lines, two branches
choosing the same version for one application. This predicts them.

    python3 scripts/check_pr_overlap.py                       # open PRs via gh, else recent branches
    python3 scripts/check_pr_overlap.py --branches claude/a claude/b
    python3 scripts/check_pr_overlap.py --days 7              # fallback window

For every other branch it simulates the order GitHub will see: that branch
merged into the base first, then this branch merged on top (git merge-tree, so
nothing in the working tree is touched). It reports

  * CONFLICT - a file that would really conflict. Decide the merge order with
    the other author, or reshape the change: keep edits additive, put a
    change's own text in a file of its own (the changelog.d pattern), and do
    not reformat lines other branches are editing.
  * generated - a generated file (llms.txt, llms-full.txt) that conflicts.
    Not a real conflict, and never resolved by hand: those two are written on
    main after the merge (llms-regenerate.yml), so a branch carrying them
    drops them.
  * version collision - an application changelog both branches give the same
    top version (`#### date *x.y.z*`). Git may merge that with no conflict at
    all and ship two releases under one number; renumber one of them.
  * shared files - touched by both but merging cleanly; listed so a reviewer
    knows the branches meet there.

Exit code 1 for a real conflict or a version collision, 0 otherwise (shared
files and regenerations alone are 0), so it can gate a push.

Which branches are "the other open pull requests"? GitHub knows; git does
not. With `gh` installed and authenticated the open pull requests' heads are
asked for. An assistant without `gh` lists them with the GitHub tools
(`mcp__github__list_pull_requests`, state open) and passes their head
branches with --branches. Failing both, unmerged remote branches active in
the last --days days stand in (default 3) - a superset, since a branch whose
pull request was squash-merged or closed still looks unmerged to git.

It fetches the base and the compared branches first; --no-fetch skips that.
Standard library only.
"""

import argparse
import datetime
import json
import re
import shutil
import subprocess
import sys

# Whole files written by a generator: a conflict in one is never resolved by
# hand. llms.txt / llms-full.txt are written on main after the merge
# (llms-regenerate.yml), so a branch that still carries them drops them.
GENERATED = {"llms.txt", "llms-full.txt"}

CHANGELOG_TOP = re.compile(r"^####\s+\d{4}-\d{2}-\d{2}\s+\*([0-9][0-9A-Za-z.\-]*)\*")


def git(*args, check=True, stdin=None):
    r = subprocess.run(["git", *args], capture_output=True, text=True, input=stdin)
    if check and r.returncode not in (0, 1):
        raise RuntimeError(f"git {' '.join(args)} failed: {r.stderr.strip()}")
    return r


def ref_exists(ref):
    return git("rev-parse", "--verify", "--quiet", ref + "^{commit}", check=False).returncode == 0


def open_pr_heads(base):
    """Head branches of the open pull requests against `base`, or None."""
    if not shutil.which("gh"):
        return None
    r = subprocess.run(["gh", "pr", "list", "--state", "open", "--base", base,
                        "--limit", "200", "--json", "number,headRefName"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None
    try:
        return [(item["headRefName"], item["number"]) for item in json.loads(r.stdout)]
    except (ValueError, KeyError):
        return None


def recent_branches(remote, base_ref, days, current):
    cutoff = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=days)
    out = git("for-each-ref", f"refs/remotes/{remote}", "--no-merged", base_ref,
              "--format=%(committerdate:iso-strict) %(refname:short)").stdout
    branches = []
    for line in out.splitlines():
        stamp, _, ref = line.partition(" ")
        name = ref.split("/", 1)[1] if "/" in ref else ref
        if name in ("HEAD", current) or ref == base_ref:
            continue
        try:
            when = datetime.datetime.fromisoformat(stamp)
        except ValueError:
            continue
        if when >= cutoff:
            branches.append((name, None))
    return branches


def changed_files(a, b):
    out = git("diff", "--name-only", git("merge-base", a, b).stdout.strip(), b).stdout
    return set(out.split())


def merge_conflicts(ours, theirs):
    """(clean, conflicted files, tree) for merging `theirs` into `ours`."""
    r = git("merge-tree", "--write-tree", "--name-only", "--no-messages", ours, theirs)
    lines = r.stdout.splitlines()
    tree = lines[0] if lines else ""
    files = sorted({l for l in lines[1:] if l.strip()})
    return r.returncode == 0, files, tree


def changelog_top(ref, path):
    r = git("show", f"{ref}:{path}", check=False)
    if r.returncode != 0:
        return None
    first = r.stdout.split("\n", 1)[0]
    m = CHANGELOG_TOP.match(first)
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--base", default="main")
    ap.add_argument("--remote", default="origin")
    ap.add_argument("--branches", nargs="*", help="other branches to compare (open PR heads)")
    ap.add_argument("--days", type=int, default=3,
                    help="fallback: unmerged remote branches active in the last N days")
    ap.add_argument("--no-fetch", action="store_true")
    args = ap.parse_args()

    current = git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip()
    base_ref = f"{args.remote}/{args.base}"

    if args.branches:
        others = [(b, None) for b in args.branches if b != current]
        source = "the branches given"
    else:
        heads = open_pr_heads(args.base)
        if heads is not None:
            others = [(b, n) for b, n in heads if b != current]
            source = "the open pull requests (gh)"
        else:
            others = None
            source = f"unmerged remote branches active in the last {args.days} days"

    if not args.no_fetch:
        refspecs = [f"+refs/heads/{args.base}:refs/remotes/{args.remote}/{args.base}"]
        if others is None:
            refspecs = [f"+refs/heads/*:refs/remotes/{args.remote}/*"]
        else:
            refspecs += [f"+refs/heads/{b}:refs/remotes/{args.remote}/{b}" for b, _ in others]
        r = git("fetch", "--quiet", args.remote, *refspecs, check=False)
        if r.returncode != 0:
            print(f"check_pr_overlap: fetch failed, comparing possibly stale refs ({r.stderr.strip()})")

    if others is None:
        others = recent_branches(args.remote, base_ref, args.days, current)
    if not ref_exists(base_ref):
        print(f"check_pr_overlap: {base_ref} not found")
        return 2

    mine = changed_files(base_ref, "HEAD")
    print(f"check_pr_overlap: {current} ({len(mine)} files) against {source}: {len(others)} branch(es)")
    if not others:
        print("  nothing to compare - no other open work")
        return 0

    failing = False
    for name, number in others:
        ref = f"{args.remote}/{name}"
        label = f"{name}" + (f" (PR #{number})" if number else "")
        if not ref_exists(ref):
            print(f"  {label}: not found on {args.remote}")
            continue
        theirs = changed_files(base_ref, ref)
        shared = sorted(mine & theirs)
        if not shared:
            continue

        # The order GitHub will see: the other branch lands first, then this
        # one on top. If the other branch does not merge into the base
        # cleanly itself, compare the two branches directly instead.
        clean, _, tree = merge_conflicts(base_ref, ref)
        if clean and tree:
            landed = git("commit-tree", tree, "-p", base_ref, "-p", ref,
                         stdin="check_pr_overlap simulation\n").stdout.strip()
            _, conflicted, _ = merge_conflicts(landed, "HEAD")
            note = ""
        else:
            _, conflicted, _ = merge_conflicts(ref, "HEAD")
            note = " (it does not merge into the base cleanly itself - likely stale)"

        # Only files both branches change can conflict between them. The
        # direct comparison of the fallback also sees what the other branch
        # conflicts with main over; that is its problem, not this branch's.
        conflicted = [f for f in conflicted if f in shared]
        real = [f for f in conflicted if f not in GENERATED]
        regen = [f for f in conflicted if f in GENERATED]

        collisions = []
        for path in shared:
            if not path.endswith("CHANGELOG.md"):
                continue
            ours_top, theirs_top = changelog_top("HEAD", path), changelog_top(ref, path)
            base_top = changelog_top(base_ref, path)
            if ours_top and ours_top == theirs_top and ours_top != base_top:
                collisions.append(f"{path} *{ours_top}*")

        if real:
            failing = True
            print(f"  {label}: CONFLICT in {', '.join(real)}{note}")
        if collisions:
            failing = True
            print(f"  {label}: version collision - both top {'; '.join(collisions)}")
        if regen:
            print(f"  {label}: generated, not a real conflict - {', '.join(regen)}; main "
                  f"regenerates them, drop them from the branch: git checkout {base_ref} -- "
                  f"{' '.join(regen)}")
        quiet = [f for f in shared if f not in conflicted]
        if quiet and not real:
            shown = ", ".join(quiet[:6]) + (f" and {len(quiet) - 6} more" if len(quiet) > 6 else "")
            print(f"  {label}: shares {len(quiet)} file(s), merging cleanly - {shown}")

    if failing:
        print("\nAgree the merge order with the other branch's author, or reshape this change "
              "(additive edits, a change's text in a file of its own) before pushing.")
        return 1
    print("  no real conflicts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
