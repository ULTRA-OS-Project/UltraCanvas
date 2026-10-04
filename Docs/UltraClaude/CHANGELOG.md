#### 2026-10-04 *0.2.0*
- **The message box takes several lines.** It is an `UltraCanvasTextArea`
  now: Enter sends, Shift+Enter starts a new line, and a long message wraps
  instead of scrolling sideways. The keys come through the text area's new
  `onBeforeKeyDown` hook (framework, `changelog.d/textarea-before-keydown.md`).
- **Login code box on the sign-in page.** While `claude auth login` waits,
  the page shows *Open sign-in page* - the URL the CLI printed, for when no
  browser opened - and a *Login code* field with Submit: when Anthropic's page
  ends on a code to paste back, it goes to the CLI through its standard input,
  which `ClaudeCliProcess` can now keep open (`InputMode::KeepOpen`,
  `WriteInput`, `CloseInput`). The code is trimmed of the spaces and line
  break it was copied with. A wrong code shows the CLI's answer ("Login
  failed: ...") and Log in can be chosen again. The long sign-in URL no longer
  replaces the status line.
- **Links in the sign-in messages open the browser.** claude.ai (after
  Create account) and claude.com/claude-code (when Claude Code is not
  installed) are underlined in the accent colour and open in the browser on a
  click, with the address as a tooltip - through `UltraCanvasLabel`'s text
  links. The not-found message no longer ends in a doubled ".).".
- The hidden code field no longer leaves its caret blinking on the page
  (framework fixes, `changelog.d/caret-left-by-hidden-input.md` and
  `changelog.d/hidden-container-keeps-focus.md`).

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
