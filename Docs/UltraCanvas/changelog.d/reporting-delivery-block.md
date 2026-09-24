- **Assistant sessions must now say where the code ended up.** `AGENTS.md`'s
  *Reporting back* rules asked every reply that reports work to end with
  `## Next Task` and `## Other recommendations`, and neither of those says
  whether the work reached anyone. A session could write a feature, commit it,
  push it and describe it in detail while never mentioning that no pull
  request had been opened - and "done and pushed" reads as delivered, so a
  reader had no way to tell. That happened: three commits over two replies,
  1130 lines, and the omission only surfaced because the user asked.
  A third block, `## Delivery`, now comes first and carries three facts: how
  much (files and +/- lines from `git diff --shortstat` against the base
  branch, not a prose estimate), whether it is committed and pushed, and
  whether there is a pull request - its number and state, or the words "no
  pull request". It is required whenever any code was written, including when
  the answer is unwelcome. `CLAUDE.md` carries the short form.
