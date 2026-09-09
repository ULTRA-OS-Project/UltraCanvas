#!/usr/bin/env python3
"""Is the work on this branch actually going anywhere?

Pushing is not delivering. A branch can carry days of finished work and still
be invisible to `main`: no pull request was ever opened for it, or its pull
request was merged and the commits pushed afterwards are stranded on a branch
nothing tracks. Both have happened in this repository, and neither announces
itself - the push succeeds, the session ends, and the work is simply not in
the product.

This answers the question mechanically.

    python3 scripts/check_publication.py            # the current branch
    python3 scripts/check_publication.py --all      # every remote branch
    python3 scripts/check_publication.py --branch claude/foo

Exit code 1 when a branch carries commits that are not in the base branch, so
it can gate a session's "done" as well as be read by a person.

What it cannot tell you: whether a pull request exists and is open. That needs
GitHub. The output says so, and names the check to run there - for an
assistant, the `mcp__github__list_pull_requests` tool with
`head: <owner>:<branch>`; for a person, the branch's page on GitHub.
"""

import argparse
import subprocess
import sys


def git(*args, check=True):
    r = subprocess.run(["git", *args], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout.strip()


def remote_default_branch(remote="origin"):
    """The branch a pull request would target."""
    try:
        ref = git("symbolic-ref", f"refs/remotes/{remote}/HEAD")
        return ref.rsplit("/", 1)[-1]
    except RuntimeError:
        for name in ("main", "master"):
            if git("rev-parse", "--verify", "--quiet",
                   f"refs/remotes/{remote}/{name}", check=False):
                return name
    return "main"


def shared_history(base_ref, branch_ref):
    r = subprocess.run(["git", "merge-base", base_ref, branch_ref],
                       capture_output=True, text=True)
    return r.returncode == 0 and bool(r.stdout.strip())


class Report:
    def __init__(self, branch, base_ref, remote):
        self.branch = branch
        self.base_ref = base_ref
        self.remote = remote
        self.unrelated = not shared_history(base_ref, self._ref())
        self.subjects = []
        if not self.unrelated:
            out = git("log", f"{base_ref}..{self._ref()}",
                      "--format=%h %ad %s", "--date=short", check=False)
            self.subjects = [l for l in out.splitlines() if l]

    def _ref(self):
        return f"{self.remote}/{self.branch}"

    @property
    def ahead(self):
        return len(self.subjects)

    @property
    def delivered(self):
        return not self.unrelated and self.ahead == 0


def remote_branches(remote="origin", base=None):
    out = git("for-each-ref", "--format=%(refname:short)|%(committerdate:short)",
              f"refs/remotes/{remote}")
    rows = []
    for line in out.splitlines():
        ref, _, date = line.partition("|")
        name = ref.split("/", 1)[1] if "/" in ref else ref
        if name in ("HEAD", base):
            continue
        rows.append((name, date))
    return sorted(rows, key=lambda r: r[1], reverse=True)


def report_one(rep, verbose=True):
    if rep.unrelated:
        print(f"  ?  {rep.branch}")
        print("     shares no history with the base branch - it predates a "
              "history rewrite, so nothing can be concluded from its commits.")
        return
    if rep.delivered:
        print(f"  ok {rep.branch}: every commit is in {rep.base_ref}.")
        return
    print(f"  !! {rep.branch}: {rep.ahead} commit(s) NOT in {rep.base_ref}")
    if verbose:
        for line in rep.subjects[:12]:
            print(f"       {line}")
        if rep.ahead > 12:
            print(f"       ... and {rep.ahead - 12} more")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true",
                    help="check every branch on the remote, not just this one")
    ap.add_argument("--branch", help="check this branch instead of the current one")
    ap.add_argument("--remote", default="origin")
    ap.add_argument("--base", help="base branch (default: the remote's HEAD)")
    args = ap.parse_args()

    base = args.base or remote_default_branch(args.remote)
    base_ref = f"{args.remote}/{base}"
    if not git("rev-parse", "--verify", "--quiet", base_ref, check=False):
        print(f"No {base_ref}. Fetch first: git fetch {args.remote} {base}")
        return 2

    if args.all:
        print(f"Branches on {args.remote} carrying work that is not in {base_ref}:\n")
        undelivered, unrelated, ok = [], [], 0
        for name, date in remote_branches(args.remote, base):
            rep = Report(name, base_ref, args.remote)
            if rep.unrelated:
                unrelated.append((name, date))
            elif rep.delivered:
                ok += 1
            else:
                undelivered.append((rep, date))
        if undelivered:
            for rep, date in undelivered:
                print(f"  {rep.ahead:>4} commit(s)  {date}  {rep.branch}")
        else:
            print("  (none)")
        print(f"\n  {ok} branch(es) fully in {base_ref}.")
        if unrelated:
            print(f"  {len(unrelated)} branch(es) share no history with it "
                  "(pre-rewrite; nothing can be concluded):")
            for name, date in unrelated:
                print(f"       {date}  {name}")
        if undelivered:
            print("\nFor each branch listed above, check on GitHub whether an "
                  "OPEN pull request has it as its head.")
            print("  none                -> the work was never proposed; open one.")
            print("  a MERGED one        -> these commits came after the merge and "
                  "are stranded; rebase onto")
            print("                         the base branch and open a NEW pull "
                  "request. A merged PR cannot take them.")
            print("  a CLOSED one        -> it was rejected or abandoned; confirm "
                  "with the author before reopening.")
            return 1
        return 0

    branch = args.branch or git("rev-parse", "--abbrev-ref", "HEAD")
    if branch == "HEAD":
        print("Detached HEAD - pass --branch.")
        return 2
    if not git("rev-parse", "--verify", "--quiet",
               f"{args.remote}/{branch}", check=False):
        print(f"!! {branch} is not on {args.remote} at all - nothing has been "
              "pushed, so nothing can be delivered.")
        return 1

    local = git("rev-parse", branch, check=False)
    remote_sha = git("rev-parse", f"{args.remote}/{branch}", check=False)
    if local and remote_sha and local != remote_sha:
        ahead = git("rev-list", "--count", f"{args.remote}/{branch}..{branch}",
                    check=False)
        if ahead and ahead != "0":
            print(f"!! {ahead} local commit(s) not pushed to {args.remote}/{branch}.")

    rep = Report(branch, base_ref, args.remote)
    report_one(rep)
    if rep.delivered:
        return 0
    if not rep.unrelated:
        print(f"\n  Pushed is not delivered. Check on GitHub that an OPEN pull "
              f"request has\n  {args.remote}/{branch} as its head and that its "
              f"head commit is {remote_sha[:8]}.")
        print("  If its pull request is already merged, these commits are "
              "stranded: rebase onto\n  the base branch, force-with-lease, and "
              "open a NEW pull request.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
