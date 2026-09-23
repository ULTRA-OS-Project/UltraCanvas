# Pending changelog entries

One file per change, holding **only the bullets** — no `####` header, no
version number. CI assigns the number when the change reaches `main`.

```
Docs/UltraCanvas/changelog.d/dns-timeout-deadlock.md
```

```markdown
- **A synchronous DNS timeout hung the calling thread forever.** On expiry
  `Resolve` called `ares_cancel` while still holding the lock its `wait_for`
  had taken …
  - Sub-bullets are fine, and so is everything else the changelog already
    uses.
```

## Why the number is not yours to pick

Line 1 of [`CHANGELOG.md`](../CHANGELOG.md) *is* the framework's version —
`cmake/UltraCanvasVersion.cmake` reads it and every `project(VERSION …)`,
compile definition and packaging script follows from it. That made it the
most contended line in the repository: every branch wanted to write it, and
two open at once always collided. On 2026-09-23 a single branch was renumbered
five times in one morning (0.9.23 → 0.9.27 → 0.9.28 → 0.9.29 → 0.9.31), each
renumber invalidating a full six-platform CI matrix, and 0.9.29 was consumed
and lost entirely in the churn.

Two branches adding two *files* never collide. So the number is assigned once,
on `main`, after the merge — by `.github/workflows/changelog-fold.yml`, which
runs `scripts/fold_changelog.py` to move every pending entry into
`CHANGELOG.md` under the next free version and delete it from here.

## What this means for you

- Add a file here instead of editing `CHANGELOG.md`. Any name that will not
  clash with another open branch's (the change, not the branch: `dns-timeout`,
  not `claude-great-cerf`).
- Do not write a version number anywhere. `scripts/check_changelog.py` refuses
  a `####` header in these files, because a number chosen on a branch is the
  collision this directory exists to end.
- Write the entry exactly as it should read in the changelog: the reader is a
  person deciding whether a release affects them, so say what changed and why
  it mattered, not which files moved.
- Nothing to run afterwards. The fold happens on `main`; your branch's diff
  stays one new file that can never conflict with anyone else's.

A release that has to carry a specific number — a hand-cut hotfix, say — can
still be written straight into `CHANGELOG.md` as a top entry. The guard and
the fold both accept that; it is only the routine case that moved here.
