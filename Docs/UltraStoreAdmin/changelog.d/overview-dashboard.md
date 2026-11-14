- **The Overview is the design's dashboard.** The last 30 days as tiles -
  what the buyers paid against the 30 days before, the paid orders and how
  many carried a code, the apps with how many lack a release or changed
  since the last publish, what the partners are owed - then what was paid
  per week as a bar chart over twelve weeks, the top apps of the 30 days
  with their share, the latest sales, the last publish with the warnings
  the next one would raise, the apps lacking texts in the target languages
  (a button opens the one with the most missing), and the signing keys.
  `Finance.h` gains a date-range period (`YYYY-MM-DD..YYYY-MM-DD`), the
  weeks (`WeeklyFinance`) and the date helpers behind it; the database
  tells when each component was last written (`ComponentUpdates`).
