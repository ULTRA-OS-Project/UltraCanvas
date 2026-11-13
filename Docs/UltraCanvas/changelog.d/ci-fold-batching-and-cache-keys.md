- **CI: merges that land within minutes of each other are folded and
  released together, and a new check keeps cache keys prunable.**
  `changelog-fold.yml` dispatched one release build per fold commit, so the
  five merges between 19:00 and 20:03 on 2026-11-12 became five release
  builds of six legs running at once - which is what overflowed the cache
  budget and held every macOS runner. The fold now runs every ten minutes
  instead of on every push, and folds once `main` has been quiet for ten
  minutes, or when the oldest pending entry has waited twenty: one fold
  commit, one release build, for the burst, and no runner held while it
  waits; a dispatch by hand folds at once. `scripts/check_cache_keys.py`
  (`cache-keys.yml`, on every change under `.github/workflows/`) reports a
  cache key ending in a commit hash or carrying `github.run_number`, which
  `prune_actions_caches.py` never recognises as run-numbered so the entries
  pile up until GitHub's seven-day expiry; the combined `actions/cache` with
  a run-numbered key, which saves on every run of every branch; a
  run-numbered restore without `restore-keys`; and a workflow that saves
  run-numbered caches without being on the prune's list - the shape
  `ultraai-llamacpp.yml` shipped on 2026-11-12. Its tests are in
  `scripts/tests/test_check_cache_keys.py`.
