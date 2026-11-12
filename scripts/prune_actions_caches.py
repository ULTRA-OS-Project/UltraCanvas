#!/usr/bin/env python3
"""Keeps the repository's GitHub Actions cache to what the next runs restore.

GitHub keeps 10 GB of Actions cache per repository and, past that, evicts the
least recently used entries. Several workflows save a cache under a key ending
in the run's number - build.yml's ccache for each of its six legs and vcpkg's
binary cache for each macOS leg, the Wayland and element-module rows' ccache,
the Android build's vcpkg caches - and restore the newest one by prefix
(`restore-keys`). Every run on main saves a fresh entry, and nothing ever
removed the one it replaced. On 2026-11-10 main ran ten release builds in
seven hours, each saving 2-3 GB, so the budget overflowed within a few runs
and evicted the caches the next run needed: both macOS legs found no ccache
at all (8.5 % of their compiles hit) and the Intel leg no vcpkg libraries, so
a leg that takes half an hour ran for two to almost four, and every pull
request's macOS check waited hours behind them for a runner.

What this deletes:

- A run-numbered entry - a key ending in `-<run id>` or
  `-<run id>-<attempt>` - that is not the newest of its kind on its ref. Its
  kind is the key without that suffix, so `ccache-macos-15-Release-123` and
  `ccache-macos-15-Release-456` are one kind and only the newer stays, while
  `ccache-macos-15-intel-Release-456` is another. A restore by prefix only
  ever takes the newest, so the older ones are never read again.
- Every entry of a pull request that is closed (merged or not): a cache is
  scoped to the ref it was saved on, and refs/pull/<n>/merge runs no more.

Everything else stays: an entry keyed by content (a package hash, a
version) is the one entry of its kind, and GitHub drops what no run uses for
seven days.

    python3 scripts/prune_actions_caches.py --repo OWNER/NAME [--dry-run]

Needs the gh CLI with a token that may delete caches (GITHUB_TOKEN with
`actions: write`, as .github/workflows/cache-prune.yml gives it) and read
pull requests. The plan itself is pure (plan_deletions) and tested in
scripts/tests/test_prune_actions_caches.py.
"""
import argparse
import json
import re
import subprocess
import sys
from collections import defaultdict

# A key ending in the run's number, and possibly its attempt: what
# `${{ github.run_id }}` and `${{ github.run_attempt }}` append. A run id is
# long (eleven digits today); requiring several digits keeps a key ending in
# a short version number ("nsis-Windows-X64-1") out.
_RUN_SUFFIX = re.compile(r"^(?P<kind>.+)-(?P<run>\d{6,})(?:-(?P<attempt>\d{1,3}))?$")
_PULL_REF = re.compile(r"^refs/pull/(?P<number>\d+)/(?:merge|head)$")


def run_kind(key):
    """The kind of a run-numbered cache key - the key without its run (and
    attempt) suffix - or None for a key that is not run-numbered."""
    m = _RUN_SUFFIX.match(key)
    return m.group("kind") if m else None


def pull_number(ref):
    """The pull request a cache's ref belongs to, or None for a branch."""
    m = _PULL_REF.match(ref or "")
    return int(m.group("number")) if m else None


def plan_deletions(caches, closed_pulls):
    """The caches to delete, as (cache, reason) pairs.

    `caches` are dicts with at least id, key, ref and created_at (an
    ISO-8601 time, which sorts as text); `closed_pulls` is the set of pull
    request numbers that are closed. The newest entry of each run-numbered
    kind on each ref is kept; on a ref of a closed pull request nothing is.
    """
    out = []
    newest = defaultdict(list)   # (ref, kind) -> caches of that kind on that ref
    for cache in caches:
        number = pull_number(cache.get("ref"))
        if number is not None and number in closed_pulls:
            out.append((cache, f"pull request #{number} is closed"))
            continue
        kind = run_kind(cache["key"])
        if kind is not None:
            newest[(cache.get("ref"), kind)].append(cache)
    for (ref, kind), entries in newest.items():
        # Newest first; the id breaks a tie of two saved in the same second.
        entries.sort(key=lambda c: (c["created_at"], c["id"]), reverse=True)
        for cache in entries[1:]:
            out.append((cache, f"superseded by {entries[0]['key']}"))
    return out


def _gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def _list_caches(repo):
    caches = []
    # Pages of up to 100; --paginate follows them and prints each page's
    # objects, one JSON value per line with --jq.
    out = _gh("api", "--paginate", f"repos/{repo}/actions/caches?per_page=100",
              "--jq", ".actions_caches[] | {id, key, ref, created_at, size_in_bytes}")
    for line in out.splitlines():
        if line.strip():
            caches.append(json.loads(line))
    return caches


def _closed_pulls(repo, numbers):
    closed = set()
    for number in sorted(numbers):
        try:
            state = _gh("api", f"repos/{repo}/pulls/{number}", "--jq", ".state").strip()
        except subprocess.CalledProcessError as e:
            # A pull request that cannot be read is left alone: deleting
            # caches on a guess is worse than keeping them a while longer.
            print(f"pull request #{number}: not read ({e.stderr.strip()}) - its caches stay",
                  file=sys.stderr)
            continue
        if state == "closed":
            closed.add(number)
    return closed


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", required=True, help="OWNER/NAME")
    parser.add_argument("--dry-run", action="store_true", help="print the plan, delete nothing")
    args = parser.parse_args()

    caches = _list_caches(args.repo)
    pulls = {n for n in (pull_number(c.get("ref")) for c in caches) if n is not None}
    closed = _closed_pulls(args.repo, pulls)
    plan = plan_deletions(caches, closed)

    total = sum(c.get("size_in_bytes", 0) for c in caches)
    freed = sum(c.get("size_in_bytes", 0) for c, _ in plan)
    print(f"{len(caches)} caches, {total / 2**20:.0f} MB; "
          f"{len(plan)} to delete, {freed / 2**20:.0f} MB")
    failures = 0
    for cache, reason in plan:
        line = f"{cache['key']} ({cache.get('ref')}, {cache.get('size_in_bytes', 0) / 2**20:.0f} MB): {reason}"
        if args.dry_run:
            print("would delete " + line)
            continue
        try:
            _gh("api", "-X", "DELETE", f"repos/{args.repo}/actions/caches/{cache['id']}")
            print("deleted " + line)
        except subprocess.CalledProcessError as e:
            # Already gone - evicted, or deleted by a prune running beside
            # this one - is what was wanted.
            if "404" in (e.stderr or ""):
                print("already gone " + line)
            else:
                failures += 1
                print(f"::warning::could not delete {line}: {e.stderr.strip()}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
