- **Progress window: the time left no longer jumps between seconds and
  hours.** It came from the last three seconds' byte speed, which over many
  small files swings with each file's size. It is now the whole run's average:
  how far the work is - the share of items and of bytes done, half each - and
  the active time that took (`CombinedProgress`, `EstimateSecondsLeft`).
  `Tests/ProgressTimeLeftTest` replays a copy of 44 000 files.
- **Filer widget: a copy of several entries shows its totals.** It counts the
  entries before the first is touched (`CountPasteTotals`), so the window's
  Items and Size read "done / total"; a skipped entry comes off them.
