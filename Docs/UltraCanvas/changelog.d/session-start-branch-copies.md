- **Cloud sessions no longer see a pushed branch as unpushed.** The cloud
  clone is shallow and single-branch, so a branch pushed with `git push -u`
  had no `origin/<branch>` copy and both Stop hooks - the repository's and
  the image's own, which the image rewrites every turn - counted every
  commit on it as unpushed. `.claude/hooks/session-start.sh` now widens the
  clone's fetch refspec to every branch, as a full clone has it, so a push
  keeps the copy current for any branch, and fetches the session branch's
  copy at the start.
