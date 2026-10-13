- **CI: the compiled-object cache is saved from main only.** Every run of
  every branch used to save a fresh ccache for each of the six build legs,
  and under the repository's 10 GB cache limit the least-recently-used
  eviction regularly took main's caches with it. The leg that paid was the
  macOS Intel one, whose runner compiles five to ten times slower than the
  others: a cold start there was a 65-82 minute build step against 13 with
  the cache, with the whole matrix waiting on it. A pull request now
  restores main's cache on every push and saves none; a run on main saves
  its objects after the build, whether or not the build succeeded.
