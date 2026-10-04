#### 2026-10-02 *0.1.0*
- **UltraClaude, a desktop chat window for Claude on a Claude subscription.**
  `Apps/UltraClaude` does not call Anthropic's API and holds no API key: it
  starts the official Claude Code CLI (`claude -p --input-format stream-json
  --output-format stream-json --verbose --include-partial-messages`) as a
  child process for every prompt, writes the prompt to its standard input and
  reads its output line by line, so usage runs on the Pro or Max subscription
  the CLI is signed in with. The session id the CLI reports is passed back
  with `--resume`, so a chat keeps its context.
  - **Sign-in page.** The app opens on a page laid out like UltraMail's start
    page: the logo, *Log in* and *Create account*. Log in runs
    `claude auth login --claudeai [--email <address>]`, which opens
    Anthropic's sign-in page in the browser; the email field only pre-fills
    that page, and UltraClaude never asks for or sees a password or token.
    Create account opens claude.ai. When the CLI is already signed in
    (`claude auth status`), the page says so and Log in becomes Continue.
  - **Chat view.** Model (Default / Opus / Sonnet / Haiku), Permissions (Ask,
    Accept edits, Plan only, Allow everything - the CLI's
    `--permission-mode`), the folder Claude works in, New chat and Log out
    (`claude auth logout`). The reply streams into a read-only markdown
    transcript; tool calls appear as one-line quotes, failed tool results and
    refused tool calls are reported, and the status line shows the time, the
    steps and the API-equivalent cost the subscription covers. Send turns
    into Stop while Claude answers, and Stop ends the CLI and the processes it
    started.
  - **`--print "<prompt>"`** runs the same engine without a window and writes
    the streamed reply to standard output (`--model`, `--cwd`,
    `--permission-mode`, `--claude <path>`).
