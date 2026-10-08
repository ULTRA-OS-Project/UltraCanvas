- **The chat-title hook recognises the rename under either tool name.** The
  Claude Code Remote server is registered as `mcp__claude-code-remote__...` in
  some builds and `mcp__Claude_Code_Remote__...` in others. The
  `PostToolUse` matcher in `.claude/settings.json` named only the first, so a
  rename made through the second was never recorded, and
  `check-chat-title.sh` kept asking for a title that was already right. The
  matcher now takes either case and either separator, and the hook's message
  no longer names one spelling (hook 1.0.1).
- **mDNS plugin 0.3.1:** the Bonjour branch's `OnBrowseReply` no longer
  declares a variable it never uses, which macOS builds warned about.
