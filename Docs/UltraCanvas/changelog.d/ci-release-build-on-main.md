- **CI builds releases on main again.** Since the changelog fold arrived, a
  merge to main is gated off (its framework version is not folded yet) and
  the release was meant to come from the fold's "Changelog: x.y.z" commit -
  but that commit is pushed with `GITHUB_TOKEN`, and GitHub starts no
  workflow for such a push. No release artifacts were built for 0.9.72 to
  0.9.75; the newest packages were 0.9.71. `changelog-fold.yml` now
  dispatches `build.yml` with `release=true` after its push (a
  `workflow_dispatch` is the one event that token may still trigger), and
  `build.yml` treats that dispatch on main as the release build - gated the
  same way, signed and notarized. A dispatch by hand stays a validation run.
  The dispatch names the fold commit (`sha`), and every checkout in
  `build.yml` builds exactly that commit - not whatever head main has when
  the runner starts, which could already be the next merge. A named commit
  must be on main, and each release dispatch has its own concurrency group,
  so a burst of merges cannot make GitHub drop a waiting release.
- **The UltraMail engine suite runs in CI** (`ULTRACANVAS_BUILD_ULTRAMAIL_TESTS`
  on every row; ctest on Linux runs it). Nobody built it before, so a stale
  Gmail expectation had been failing unseen since 2026-09-23.
