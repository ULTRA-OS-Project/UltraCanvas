- **Cloud sessions may rebase their own branches.** The auto-mode
  classifier refused `git rebase`, `git commit --amend` and
  `git push --force-with-lease` as destructive, so a session that had to
  bring its branch up to date merged `main` into it instead of the rebase
  the branch rules ask for. `.claude/settings.json` now tells the
  classifier that these are routine on a branch named `claude/…`, and
  lets a `--force-with-lease` push to such a branch through without a
  prompt. A plain `--force`, and any of it on `main` or another branch,
  stays blocked.
