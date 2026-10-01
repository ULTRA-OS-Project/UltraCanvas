- **The delivery hook no longer reports pushed work as unpushed.**
  `.claude/hooks/check-delivery.sh` treated a branch with no upstream as
  entirely unpushed and listed its last 20 commits - so every cloud session,
  whose branch is cut from `origin/main` before it is ever pushed, started
  with a warning about 20 "unpushed" commits that `main` already held. A
  branch with no upstream is now checked against every remote ref
  (`git log HEAD --not --remotes`): only commits that are on no remote branch
  are reported, which is the work that would actually be lost.
