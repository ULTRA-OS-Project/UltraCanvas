- **AGENTS.md rule 7 names both spellings of the Claude Code Remote tool.**
  It said to call `set_session_title` on "the claude-code-remote MCP server".
  Some builds register that server as `mcp__Claude_Code_Remote__…`, so a
  session could take the tool for missing. The rule now gives both names.
  The chat-title hook accepts both since #731.
