- **The delivery hook no longer reports a pushed branch as unpushed.** A
  cloud session's clone is shallow and single-branch, so a branch pushed
  with `git push -u` never got its `origin/<branch>` copy, and the Stop and
  SessionStart hook (`.claude/hooks/check-delivery.sh`) counted every commit
  on it as unpushed - a session was told twice to push what the remote
  already held. The hook now fetches the branch's copy once when it is
  missing and adds it to the fetch refspec, so later pushes keep it current;
  a branch the remote does not have is checked as before.
