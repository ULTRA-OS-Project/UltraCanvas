- **The session-start "Code needs to be PRed" count no longer counts main's
  own commits.** A cloud clone can arrive with its branch cut from a newer
  main than its `origin/main` ref; `.claude/hooks/check-delivery.sh --brief`
  measured against that stale ref, so every commit main had gained in between
  counted as the session's work - one chat opened on "128418 lines" with 0
  lines to PR, and the `Stop` form repeated the figure until something
  fetched. The session-start form now fetches the default branch first
  (no prompt, 12-second limit), which puts the `Stop` form on the real base
  too; when the fetch cannot run, the brief says the count may be stale.
  AGENTS.md, *The closing line*, describes it.
