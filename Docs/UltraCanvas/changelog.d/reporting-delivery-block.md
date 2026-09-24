- **Assistant sessions must now say where the code ended up.** `AGENTS.md`'s
  *Reporting back* rules asked every reply that reports work to end with
  `## Next Task` and `## Other recommendations`, and neither of those says
  whether the work reached anyone. A session could write a feature, commit it,
  push it and describe it in detail while never mentioning that no pull
  request had been opened - and "done and pushed" reads as delivered, so a
  reader had no way to tell. That happened: three commits over two replies,
  1130 lines, and the omission only surfaced because the user asked.
  A third block, `## Delivery`, now comes first and answers three questions in
  order of danger. **Is anything still uncommitted?** - these sessions run in
  a container that is reclaimed when the session ends, so an edit that was
  never committed is not pending, it is gone, and a reply describing it as
  written reports a delivery that never existed; a reply reporting finished
  work may not end with a tracked file uncommitted unless it says so in as
  many words. **How much, and is it pushed?** - files and +/- lines from
  `git diff --shortstat`, the branch and SHA, or that the commits are still
  local. **Is it a pull request?** - its number and state, or the words "no
  pull request". The numbers come from `git status --short` and
  `git diff --shortstat` run before the block is written, not from memory:
  the block exists to catch the gap between what the assistant believes it
  delivered and what the repository holds. Required whenever any code was
  written, including when the answer is unwelcome. `CLAUDE.md` carries the
  short form.
