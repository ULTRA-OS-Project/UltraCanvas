# UltraClaude

A desktop chat window for Claude, built on UltraCanvas, that runs on a
**Claude Pro or Max subscription** instead of a pay-per-token API key.

UltraClaude does not talk to Anthropic's servers itself. It starts the
official **Claude Code CLI** (`claude`) as a child process for every prompt
and shows what the CLI streams back. The CLI uses the account it is signed in
with, so usage counts against your subscription. UltraClaude never handles a
password, an OAuth token or an API key.

## Requirements

- Claude Code installed and on `PATH` (`claude --version` works in a terminal),
  or its path given with `--claude <path>`.
- A Claude account with a Pro or Max subscription.

## Using it

The app opens on the sign-in page:

- **Log in** runs `claude auth login --claudeai`, which opens Anthropic's
  sign-in page in your browser. The optional email field pre-fills that page.
  You type your password (or use Google / SSO) on Anthropic's page, not in
  UltraClaude.
- **Create account** opens claude.ai, where accounts and subscriptions are made.
- When Claude Code is already signed in on the computer, the button reads
  **Continue**.

In the chat view, pick the **Model**, the **Permissions** (what Claude may do
in the folder without asking: *Ask* refuses tools that need permission,
*Accept edits*, *Plan only*, *Allow everything*), and the **Folder** Claude
works in. Press Enter or **Send**. **Stop** ends the answer. **New chat**
starts a fresh conversation, and **Log out** signs Claude Code out on this
computer (`claude auth logout`).

From a terminal, the same engine without a window:

```
UltraClaude --print "Summarise README.md" --model sonnet --cwd ~/project
```

## How it works

| File | Role |
|---|---|
| `engine/ClaudeCliProcess` | Starts one child process with piped stdin/stdout/stderr and hands stdout back line by line (POSIX `fork`/`execvp`, Windows `CreateProcessW` in a job object). |
| `engine/ClaudeStreamParser` | Turns the CLI's `stream-json` lines into chat events: text deltas, tool calls, tool errors, the turn's result. |
| `engine/ClaudeChatSession` | Builds the command line, writes the prompt to stdin as a stream-json message, keeps the session id and resumes it (`--resume`) on the next prompt. |
| `ui/UltraClaudeWindow` | The sign-in page and the chat view. Events from the reader thread are queued and applied on the UI thread by a timer. |

Each prompt runs:

```
claude -p --input-format stream-json --output-format stream-json --verbose \
       --include-partial-messages [--model M] [--permission-mode P] [--resume ID]
```

The prompt goes through standard input, never through the command line, so no
shell quoting applies, even on Windows where an npm-installed `claude.cmd`
runs through `cmd.exe`.

## Terms

This is the supported way to use a subscription from your own interface: the
client talking to Anthropic is Anthropic's own CLI, on your own computer and
your own account. Do not extract the CLI's OAuth token to call Anthropic's API
directly from another program. That breaks Anthropic's consumer terms. Use an
API key for anything you distribute to other people.
