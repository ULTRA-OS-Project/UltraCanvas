# Pending changelog entries

One file per change, holding **only the bullets** — no `####` header, no
version number. CI assigns the number when the change reaches `main`.

This page explains every product's `changelog.d/`, not only the framework's.
Each directory sits next to the `CHANGELOG.md` it feeds:

| Change to | Entry goes in |
|---|---|
| the framework (`UltraCanvas/`, the modules, the build, CI, DemoApp) | `Docs/UltraCanvas/changelog.d/<change-name>.md` |
| an application | `Docs/<App>/changelog.d/<change-name>.md` — e.g. `Docs/UltraFiler/changelog.d/`, `Docs/Texter/changelog.d/` |
| UltraWin | `Docs/Modules/UltraWin/changelog.d/<change-name>.md` |

Any product whose changelog is in the table in `AGENTS.md` (*Versioning*) has
one; create the directory with your first entry. A change that touches the
framework and an app writes one file in each.

```
Docs/UltraCanvas/changelog.d/dns-timeout-deadlock.md
Docs/UltraFiler/changelog.d/archive-preview-crash.md
```

```markdown
- **A synchronous DNS timeout hung the calling thread forever.** On expiry
  `Resolve` called `ares_cancel` while still holding the lock its `wait_for`
  had taken …
  - Sub-bullets are fine, and so is everything else the changelog already
    uses.
```

## Why the number is not yours to pick

Line 1 of a product's `CHANGELOG.md` *is* its version —
`cmake/UltraCanvasVersion.cmake` reads it and every `project(VERSION …)`,
compile definition and packaging script follows from it. That made it the
most contended line in the repository: every branch wanted to write it, and
two open at once always collided. On 2026-09-23 a single branch was renumbered
five times in one morning (0.9.23 → 0.9.27 → 0.9.28 → 0.9.29 → 0.9.31), each
renumber invalidating a full six-platform CI matrix, and 0.9.29 was consumed
and lost entirely in the churn. The applications followed once several
branches touched one app: on 2026-10-21 one pull request was renumbered five
times (UltraFiler 1.77.1 → 1.83.0 → 1.87.0, UltraPaint 0.2.26 → 0.2.27),
conflicting each time on line 1 of the changelog and of UltraFiler's `.rc`
and `.manifest`.

Two branches adding two *files* never collide. So the numbers are assigned
once, on `main`, after the merge — by `.github/workflows/changelog-fold.yml`,
which runs `scripts/fold_changelog.py` to move every product's pending entries
into its own `CHANGELOG.md` under its next version and delete them from here.

The next version follows the product's own habit, read from its file: the
component that changed between its two latest releases is bumped. UltraFiler
1.88.0 → 1.89.0, UltraPaint 0.2.28 → 0.2.29, Texter 1.69 → 1.70, the
framework 0.9.364 → 0.9.365. When UltraFiler or UltraTexter is folded, the
fold also runs `./set-version.sh`, so their Windows `.rc` and `.manifest` move
in the same commit.

## What this means for you

- Add a file in the product's `changelog.d/` instead of editing its
  `CHANGELOG.md`. Any name that will not clash with another open branch's (the
  change, not the branch: `dns-timeout`, not `claude-great-cerf`).
- Do not write a version number anywhere, and do not run `./set-version.sh` or
  touch the `.rc` / `.manifest` files. `scripts/check_changelog.py` refuses a
  `####` header in these files, because a number chosen on a branch is the
  collision this directory exists to end.
- Write the entry exactly as it should read in the changelog: the reader is a
  person deciding whether a release affects them, so say what changed and why
  it mattered, not which files moved.
- Nothing to run afterwards. The fold happens on `main`; your branch's diff
  stays one new file that can never conflict with anyone else's.

A release that has to carry a specific number — a hand-cut hotfix, say — can
still be written straight into `CHANGELOG.md` as a top entry (for UltraFiler
or UltraTexter, then run `./set-version.sh` and commit the resource files).
The guard and the fold both accept that; it is only the routine case that
moved here. `README.md` files in any `changelog.d/` are never folded.
