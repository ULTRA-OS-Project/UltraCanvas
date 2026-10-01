#!/usr/bin/env bash
# .claude/hooks/check-chat-title.sh
# Enforces AGENTS.md, Branch and pull-request rules, rule 7: once a session has
# a pull request, its chat title starts with the number - "#<n> <title>".
#
# Why this exists. The rule was written as prose, and prose is what sessions
# skip. A chat list of "Fix build" x6 does not say which chat drives which PR;
# "#628 Fix build" does. So the rule is checked, the same way the closing line
# is checked by check-delivery.sh.
#
# Two modes:
#
#   --record   PostToolUse on mcp__github__create_pull_request and
#              mcp__claude-code-remote__set_session_title. Remembers the PR a
#              session opened and the number its title was last given, in a
#              per-session file under .git/ (never in the tree: a state file in
#              the checkout would show as uncommitted work and count towards
#              the closing line). Opening a PR also puts the instruction in
#              front of the assistant at once, with the number filled in.
#
#   (default)  Stop. Works out which PR the session has - the one it opened,
#              or the one its own closing line names (" - open as PR #<n>"),
#              which also covers a PR opened from the Claude UI rather than by
#              a tool call - and blocks once if the title was never given that
#              number. Blocks once only (stop_hook_active): a title the user
#              set by hand, or a session that cannot rename itself, says so
#              and stops.
#
# Enforced only in Claude Code Remote sessions (CLAUDE_CODE_REMOTE=true), the
# only place set_session_title exists; elsewhere rule 7 asks the session to
# give the user the number instead, and a hook cannot check that.
#
# Version: 1.0.0
# Last Modified: 2026-10-01
# Author: UltraCanvas Framework
set -u

mode="stop"
[ "${1:-}" = "--record" ] && mode="record"

[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0
command -v jq >/dev/null 2>&1 || exit 0

payload="$(cat 2>/dev/null || true)"

cd "${CLAUDE_PROJECT_DIR:-.}" 2>/dev/null || exit 0
git_dir="$(git rev-parse --absolute-git-dir 2>/dev/null)" || exit 0

session="$(printf '%s' "$payload" | jq -r '.session_id // empty' 2>/dev/null)"
[ -n "$session" ] || session="${CLAUDE_CODE_SESSION_ID:-default}"
session="$(printf '%s' "$session" | tr -c 'A-Za-z0-9_-' '_')"
state_dir="$git_dir/claude-chat-title"
pr_file="$state_dir/$session.pr"
title_file="$state_dir/$session.title"

if [ "$mode" = "record" ]; then
    tool="$(printf '%s' "$payload" | jq -r '.tool_name // empty' 2>/dev/null)"
    mkdir -p "$state_dir" 2>/dev/null || exit 0
    case "$tool" in
        *create_pull_request)
            # The response is the PR as JSON, possibly wrapped in a text block
            # and so escaped once; its html_url ends in /pull/<n> either way.
            response="$(printf '%s' "$payload" | jq -c '.tool_response // empty' 2>/dev/null)"
            pr="$(printf '%s' "$response" | grep -oE '/pull/[0-9]+' | head -n 1 | tr -dc '0-9')"
            [ -n "$pr" ] || pr="$(printf '%s' "$response" |
                                  grep -oE 'number\\?"[[:space:]]*:[[:space:]]*[0-9]+' |
                                  head -n 1 | grep -oE '[0-9]+$')"
            [ -n "$pr" ] || exit 0
            printf '%s\n' "$pr" > "$pr_file"
            jq -nc --arg c "Pull request #$pr is open. AGENTS.md rule 7 (Branch and pull-request
rules): rename this chat now so its title starts with the number - call
set_session_title with \"#$pr <current title>\" (get_session gives the current
title; replace an older #<n> rather than stacking a second one, and keep the
words after the number). A Stop hook checks this." \
               '{hookSpecificOutput:{hookEventName:"PostToolUse", additionalContext:$c}}'
            ;;
        *set_session_title)
            title="$(printf '%s' "$payload" | jq -r '.tool_input.title // empty' 2>/dev/null)"
            # Only "#<n> <words>" counts: a bare "#628" loses what the chat is.
            num="$(printf '%s' "$title" | sed -nE 's/^#([0-9]+)[[:space:]]+[^[:space:]].*$/\1/p')"
            printf '%s\n' "${num:-none}" > "$title_file"
            ;;
    esac
    exit 0
fi

# Stop: once only.
if printf '%s' "$payload" | grep -q '"stop_hook_active"[[:space:]]*:[[:space:]]*true'; then
    exit 0
fi

# The PR the closing line names, from the reply being finished (newer builds
# pass it as last_assistant_message; older ones only give the transcript).
reply="$(printf '%s' "$payload" | jq -r '.last_assistant_message // empty' 2>/dev/null)"
if [ -z "$reply" ]; then
    transcript="$(printf '%s' "$payload" | jq -r '.transcript_path // empty' 2>/dev/null)"
    if [ -n "$transcript" ] && [ -r "$transcript" ]; then
        reply="$(tail -n 400 "$transcript" 2>/dev/null |
                 jq -rs '[.[] | select(.type == "assistant")
                              | .message.content[]?
                              | select(.type == "text") | .text] | last // empty' 2>/dev/null)"
    fi
fi
last_line="$(printf '%s\n' "$reply" | awk 'NF { l = $0 } END { print l }')"
named="$(printf '%s' "$last_line" | grep -oE 'open as PR #[0-9]+' | grep -oE '[0-9]+$')"

opened=""
[ -r "$pr_file" ] && opened="$(tr -dc '0-9' < "$pr_file")"

# The closing line wins: it is stated against the PR open now, and a PR opened
# earlier in the session may since have been merged and replaced.
pr="${named:-$opened}"
[ -n "$pr" ] || exit 0

titled=""
[ -r "$title_file" ] && titled="$(tr -dc '0-9a-z' < "$title_file")"
[ "$titled" = "$pr" ] && exit 0

if [ -z "$titled" ]; then
    have="this session has not renamed its chat since the PR was opened"
elif [ "$titled" = "none" ]; then
    have="the last set_session_title did not start with \"#<n> \" followed by the title"
else
    have="the chat title was last given #$titled"
fi
reason="This session has pull request #$pr, but $have.
AGENTS.md, Branch and pull-request rules, rule 7: the chat title starts with
the PR number. Call set_session_title (claude-code-remote) with
\"#$pr <current title>\" - get_session gives the current title; replace an
older #<n> rather than adding a second one, and keep the words after it.
If the title already reads \"#$pr ...\" (set by hand) or the tool is not
available, say so in one line; stopping again after this message is allowed."
jq -nc --arg r "$reason" '{decision:"block", reason:$r}'
exit 0
