- **Assistant chats end on how much code still needs a pull request.**
  `AGENTS.md` (*Reporting back → The closing line*) and `CLAUDE.md` now require
  the last reply before a chat waits for the user to end with
  `Code needs to be PRed (N lines)`, where `N` is the lines the checkout
  differs from the merge base with `origin/main` — committed, uncommitted and
  untracked — measured with `git diff --shortstat` rather than remembered,
  `(0 lines)` when nothing differs, and ` — open as PR #<n>` appended when a
  pull request already exists.
