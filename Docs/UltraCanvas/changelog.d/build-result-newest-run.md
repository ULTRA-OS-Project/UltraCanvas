- **CI: a cancelled duplicate Build run no longer leaves a commit without a
  "Build result".** `build-result.yml` let only the newest Build run of a
  commit set the status, and took the newest to be the first run the API
  listed. Two labels added together on pull request #1402 started two Build
  runs in the same second; the build's concurrency cancelled the older, the
  API listed that one first, and every event of the real run - its pending,
  its failure, its re-run's success - was skipped, so the commit never had a
  "Build result" at all. The newest is now the run with the highest id among
  the commit's runs that were not cancelled (among all of them when every one
  was), so a run its successor cancelled never stands in for it.
