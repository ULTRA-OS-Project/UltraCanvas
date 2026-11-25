- **CI: a merge with a changelog entry is folded and released within ten to
  twenty minutes, not hours.** `changelog-fold.yml` ran only on its
  ten-minute schedule, which GitHub honoured every six to ten hours. Main went
  that long without a release build, so its ccache was not saved, and every
  pull request in between recompiled what the merges had touched: 298 of the
  Linux row's 3,378 objects on 2026-11-25, after five merges since the last
  fold. A push to main that adds an entry now starts the fold, which waits
  ten quiet minutes (the next such merge cancels the wait and waits in its
  place) and never longer than twenty from the oldest pending entry. The fold
  itself is still never cancelled, and the schedule stays as the fallback.
