- **Application changelogs take pending entries too, so app version numbers
  stop colliding between pull requests.** A change to an application now drops
  its bullets in `Docs/<App>/changelog.d/<change-name>.md` — no header, no
  number — exactly as framework changes already did in
  `Docs/UltraCanvas/changelog.d/`. On 2026-10-21 one pull request had to be
  renumbered five times (UltraFiler 1.77.1 → 1.83.0 → 1.87.0, UltraPaint
  0.2.26 → 0.2.27), because every other merge took the next app version and
  conflicted with it on line 1 of the changelog and of UltraFiler's `.rc` and
  `.manifest`; two files never conflict.
  - `scripts/fold_changelog.py` folds every product's pending entries on
    `main`, each into its own `CHANGELOG.md` under its own next version. The
    step follows each file's habit — the component that changed between its
    two latest releases (UltraFiler 1.88.0 → 1.89.0, UltraPaint 0.2.28 →
    0.2.29, Texter 1.69 → 1.70, the framework 0.9.x → 0.9.x+1) — and never
    goes below the highest version in the file. `--version UltraFiler=2.0.0`
    overrides one app; a bare `--version` is still the framework's.
  - Folding UltraFiler or UltraTexter runs `./set-version.sh`, and the fold
    commit carries their `.rc` and `.manifest`; a branch no longer touches
    them. The commit names every version folded
    (`Changelog: 0.9.365, UltraFiler 1.89.0`).
  - The release build skips a merge commit with any product's entry pending,
    not only the framework's, and builds the fold commit instead — once — so
    no app ships under its previous number.
  - `scripts/check_changelog.py` applies the framework's rules to every
    product's `changelog.d/` (no `####` header, not empty) and refuses entries
    in a `changelog.d/` that belongs to no declared changelog. A hand-written
    top entry for a hotfix is still accepted and checked as before.
