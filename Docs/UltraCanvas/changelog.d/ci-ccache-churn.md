- **CI: a release build saves its ccache only when it compiled something,
  and the cache prune is no longer starved.** A fold commit changes a
  changelog line, so each of the six legs hit on every object but the two
  that carry the version - and still saved a fresh 500 MB entry, a copy of
  the one it had restored. With five release builds running at once on the
  evening of 2026-11-12, those copies and the macOS vcpkg libraries
  overflowed the 10 GB cache budget before `cache-prune.yml` could delete
  the superseded entries: its concurrency group kept one run queued at
  most, so each Build completion cancelled the prune still waiting for a
  runner (twelve in a row), and the least-recently-used eviction took the
  one Linux entry a pull request then needed - its build step ran 77
  minutes against 23 (#1226). `build.yml` now saves a leg's cache only when
  nothing was restored or at least one compile in a hundred missed; the
  prune runs without a concurrency group (two beside each other are harmless
  by design) and skips the completions of pull-request runs, which save
  nothing; and `ultraai-llamacpp.yml`, which saved a 300 MB entry under a
  commit-suffixed key on every run that the prune never recognised, keeps
  its ccache as `build.yml` does and is on the prune's list.
