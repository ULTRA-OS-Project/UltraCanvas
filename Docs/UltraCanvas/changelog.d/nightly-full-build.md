- **CI builds all six platform rows on `main` every night** (02:23 UTC, a
  `schedule` in `build.yml`). A full build otherwise ran only when something
  merged, so a change in a runner image or in a library the runners install
  fresh - MSYS2's OpenSSL on 2026-11-16 - first turned an unrelated pull
  request red. The nightly run is a validation run: nothing is signed or
  published, and it has its own concurrency group so it never replaces a
  release build waiting on `main`.
