- **CI: a "Build result" commit status says whether the Build workflow really
  built a commit.** A new workflow, `build-result.yml`, follows every Build
  run from outside it (`workflow_run`): the status is pending while a run is
  going, green only when the newest run of that commit succeeded and ran jobs,
  and red when it failed, timed out, was cancelled, failed to start or ended
  with no jobs. Until now a run that never started any job left nothing red on
  the commit, and a pull request read as mergeable although no platform had
  built it (#1153 on 2026-11-09). Make "Build result" a required status check
  of `main` in the branch protection settings so that such a pull request
  cannot be merged.
