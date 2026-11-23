- **CI: a build job GitHub never started is re-run, not reported red.** The
  macos-15 leg of a pull request sat queued and ended "cancelled" with no
  runner and no step, twice, while other pull requests' macOS legs got
  runners in the same hour. Each time the Build run ended "failure" and the
  pull request read red with nothing of it wrong. `build-result.yml` now
  re-runs the failed jobs when every unsuccessful job of the run is such a
  dropped job, up to attempt 3, and keeps "Build result" pending until the
  new attempt ends. A job that started and then failed, timed out or was
  cancelled still turns it red.
