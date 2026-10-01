- **The closing line is checked, not just asked for.** The `Stop` hook
  `.claude/hooks/check-delivery.sh` now measures how many lines the checkout
  differs from the merge base with `origin/main` (committed, uncommitted and
  untracked) and reads the reply being finished: a reply whose last line is not
  `Code needs to be PRed (N lines)` with that `N` is blocked once, naming the
  line it found and the one expected. Backticks, bold and the
  ` — open as PR #<n>` suffix are accepted; the line quoted earlier in a reply
  is not. The reply comes from `last_assistant_message`, or the transcript on
  older Claude Code builds; when neither is readable the line is not checked.
  The `SessionStart` brief now states the rule and the current `N`.
