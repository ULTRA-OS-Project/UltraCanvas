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
- While sign-in is waiting, **Open sign-in page** opens the sign-in URL again
  (for when no browser opened), and if Anthropic's page ends by showing a
  code, paste it into **Login code** and choose **Submit**. That code is a
  one-time exchange code for the CLI, not your password.

The chat view lists your chats on the left, newest first. **New chat**
starts one; your first message gives it its title. Click a chat to see it
again and carry on where it stopped: UltraClaude resumes the same Claude Code
session, in the chat's own folder. **Delete chat** forgets the selected one.
The list lives in the UltraCanvas settings folder, under `UltraClaude`
(`chats.json` and one transcript file per chat); `UltraClaude --list-chats`
prints it.

Each chat in the list carries a badge with the lines its folder holds that
the repository's default branch does not yet have - committed, uncommitted
and new files together: in colour while there is work waiting to be PRed,
a grey `0` once it is all in, nothing for a folder that is not a git
repository. It is counted when the list opens, when you open a chat and
after every answer. `UltraClaude --count-lines <folder>` prints the same
number.

In the chat view, pick the **Model**, the **Permissions** (what Claude may do
in the folder without asking: *Ask* refuses tools that need permission,
*Accept edits*, *Plan only*, *Allow everything*), and the **Folder** Claude
works in. Type your message (it can be several lines: Shift+Enter starts a
new line) and press Enter or **Send**. **Stop** ends the answer. **New chat**
starts a fresh conversation, and **Log out** signs Claude Code out on this
computer (`claude auth logout`).

From a terminal, the same engine without a window:

```
UltraClaude --print "Summarise README.md" --model sonnet --cwd ~/project
```

## How it works

| File | Role |
|---|---|
| `engine/ClaudeCliProcess` | Starts one child process with piped stdin/stdout/stderr and hands stdout back line by line (POSIX `fork`/`execvp`, Windows `CreateProcessW` in a job object). `InputMode::KeepOpen` keeps stdin open for `WriteInput`, which is how the login code reaches `claude auth login`. |
| `engine/ClaudeStreamParser` | Turns the CLI's `stream-json` lines into chat events: text deltas, tool calls, tool errors, the turn's result. |
| `engine/ClaudeChatSession` | Builds the command line, writes the prompt to stdin as a stream-json message, keeps the session id and resumes it (`--resume`) on the next prompt. |
| `engine/ChatStore` | The remembered chats (id, CLI session id, title, folder, model, permission mode, times) and their transcripts, on disk. |
| `engine/RepoStatus` | Counts a folder's lines not in its default branch (fetch, `git diff --numstat` against the merge base, untracked files), with git's prompts off and network timeouts. |
| `ui/ChatListView` | The chat list's model and delegate: the title and the lines-not-PRed badge per row. |
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
