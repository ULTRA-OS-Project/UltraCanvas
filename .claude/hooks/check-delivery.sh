#!/usr/bin/env bash
# .claude/hooks/check-delivery.sh
# Stop hook: refuse to end a turn on work that is not in the repository.
#
# Why this exists. An assistant session runs against a clone that is thrown
# away when the session ends - on the cloud runners, the whole container is.
# A file edited and never committed is therefore not "pending", it is gone,
# and a reply that describes it as written reports a delivery that never
# existed. Commits that were never pushed die the same way. This has already
# happened here: a session wrote 1130 lines across three commits, described
# them in detail over two replies, and never mentioned that none of it was in
# a pull request. Prose in AGENTS.md asks for that to be stated; prose is the
# thing that failed, so this checks instead.
#
# It does not judge the work. It only refuses SILENCE: once the reason below
# has been shown, the assistant has been told, and a second stop is allowed
# through (stop_hook_active) so it can commit, push, or say plainly what it
# is leaving behind and why.
#
# It also checks the closing line every reply ends on before the chat waits for
# the user (AGENTS.md, "The closing line"): "Code needs to be PRed (N lines)".
# N is measured here and compared with the reply's, so a remembered figure is
# refused the same way a missing line is.
#
# Version: 1.2.0
# Last Modified: 2026-10-01
# Author: UltraCanvas Framework
set -u

# --brief: the SessionStart form. Same facts, but stated to a session that has
# not done anything yet, and never blocking - it reports what it INHERITED
# (a previous session's uncommitted files, a branch that was never pushed) and
# restates the rule, so the rule is in front of every chat from turn one
# rather than only when one is about to end badly.
mode="stop"
[ "${1:-}" = "--brief" ] && mode="brief"

payload="$(cat 2>/dev/null || true)"

# Second time around: the assistant has already been shown the message and is
# answering it. Blocking again would be a loop, and the rule is "never in
# silence", not "never uncommitted".
if [ "$mode" = "stop" ] && \
   printf '%s' "$payload" | grep -q '"stop_hook_active"[[:space:]]*:[[:space:]]*true'; then
    exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-.}" 2>/dev/null || exit 0
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || exit 0

dirty="$(git status --porcelain 2>/dev/null)"

# Commits that exist only in this clone. A branch with no upstream is checked
# against every remote ref instead: a fresh branch cut from origin/main holds
# only commits the remote already has, and listing them as unpushed sent
# sessions chasing work that was never at risk. Commits on no remote ref at
# all are as lost as an uncommitted file.
unpushed=""
branch="$(git rev-parse --abbrev-ref HEAD 2>/dev/null)"
if [ -n "$branch" ] && [ "$branch" != "HEAD" ]; then
    if git rev-parse --abbrev-ref '@{u}' >/dev/null 2>&1; then
        unpushed="$(git log --oneline '@{u}..HEAD' 2>/dev/null)"
    else
        unpushed="$(git log --oneline -n 20 HEAD --not --remotes 2>/dev/null)"
        [ -n "$unpushed" ] && unpushed="(branch '$branch' has no upstream - these commits are on no remote branch)
$unpushed"
    fi
fi

# N for the closing line: the working tree against the merge base with the
# default branch (so committed and uncommitted edits both count), insertions
# plus deletions as `git diff --shortstat` reports them, plus every line of an
# untracked, non-ignored file. The remote-tracking ref is used as it stands -
# a hook must not fetch - which is also what the AGENTS.md recipe measures
# once the session has fetched.
pr_lines=""
base_ref=""
for ref in origin/main origin/master; do
    if git rev-parse --verify -q "$ref" >/dev/null 2>&1; then base_ref="$ref"; break; fi
done
if [ -n "$base_ref" ]; then
    merge_base="$(git merge-base "$base_ref" HEAD 2>/dev/null)"
    if [ -n "$merge_base" ]; then
        tracked="$(git diff --numstat "$merge_base" 2>/dev/null |
                   awk '$1 ~ /^[0-9]+$/ { n += $1 + $2 } END { print n + 0 }')"
        untracked="$(git ls-files -z --others --exclude-standard 2>/dev/null |
                     xargs -0 -r cat 2>/dev/null | wc -l | tr -d ' ')"
        pr_lines=$(( ${tracked:-0} + ${untracked:-0} ))
    fi
fi
pr_line=""
[ -n "$pr_lines" ] && pr_line="Code needs to be PRed ($pr_lines lines)"

if [ "$mode" = "brief" ]; then
    # Always says something: "the tree is clean" is worth knowing too, and a
    # brief that only appears when something is wrong teaches a session to
    # read silence as "fine", which is the habit this whole check exists to
    # break.
    brief="Delivery rule (AGENTS.md, 'Reporting back'): every reply that reports
code ends with a ## Delivery block - is anything uncommitted, how much and is
it pushed, and is there a pull request (number, or 'no pull request'). Measure
it with 'git status --short' and 'git diff --shortstat <base>...HEAD'; do not
recall it. This clone is discarded when the session ends, so uncommitted work
is lost, not pending. A Stop hook blocks a silent finish."
    [ -n "$pr_line" ] && brief="$brief

Closing-line rule (AGENTS.md, 'The closing line'): the last reply before the
chat waits for the user ends with 'Code needs to be PRed (N lines)', N measured
against $base_ref ('(0 lines)' when nothing differs; append ' - open as PR #<n>'
when one is open). The Stop hook measures N itself and blocks a reply whose
last line is missing or carries another number. Right now: $pr_line."
    state="At session start this checkout is clean and everything is pushed."
    if [ -n "$dirty" ] || [ -n "$unpushed" ]; then
        state="At session start this checkout ALREADY has work that is not in the
repository - inherited, not yours, but yours to report or resolve:"
        [ -n "$dirty" ] && state="$state

Uncommitted:
$dirty"
        [ -n "$unpushed" ] && state="$state

Committed but not pushed:
$unpushed"
    fi
    if command -v jq >/dev/null 2>&1; then
        jq -nc --arg c "$brief

$state" \
           '{hookSpecificOutput:{hookEventName:"SessionStart", additionalContext:$c}}'
    fi
    exit 0
fi

# The reply being finished. Newer Claude Code builds pass it to Stop hooks as
# last_assistant_message; older ones only give the transcript, whose last
# assistant text block is the end of the same reply. If neither can be read
# the closing line is not checked: a hook that cannot see the reply has no
# grounds to refuse it.
closing_wrong=""
if [ -n "$pr_line" ] && command -v jq >/dev/null 2>&1; then
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
    if [ -n "$reply" ]; then
        # The LAST non-blank line, so the line quoted mid-reply does not count;
        # surrounding backticks or bold are tolerated, the text is not.
        last_line="$(printf '%s\n' "$reply" | awk 'NF { l = $0 } END { print l }' |
                     sed -e 's/^[[:space:]*`]*//')"
        case "$last_line" in
            "$pr_line"*) ;;
            *) closing_wrong="$last_line" ;;
        esac
    fi
fi

[ -z "$dirty" ] && [ -z "$unpushed" ] && [ -z "$closing_wrong" ] && exit 0

reason=""
if [ -n "$dirty" ] || [ -n "$unpushed" ]; then
    reason="This turn is ending with work that is not in the repository. The clone
this session runs in is discarded when the session ends, so anything below is
lost rather than pending."
    [ -n "$dirty" ] && reason="$reason

Uncommitted (git status --porcelain):
$dirty"
    [ -n "$unpushed" ] && reason="$reason

Committed but not pushed:
$unpushed"
    reason="$reason

Commit and push it, or say so explicitly in the ## Delivery block of your
reply - how much, whether it is committed and pushed, and whether there is a
pull request (its number, or 'no pull request'). See 'Reporting back' in
AGENTS.md. Stopping again after this message is allowed; stopping without
saying anything is what this check exists to prevent."
fi
if [ -n "$closing_wrong" ]; then
    [ -n "$reason" ] && reason="$reason

"
    reason="${reason}The reply does not end with the closing line (AGENTS.md, 'The closing
line'). Its last line is:
  $closing_wrong
Measured against $base_ref (committed, uncommitted and untracked), it must be:
  $pr_line
followed by ' - open as PR #<n>' if a pull request is already open."
fi

# jq is the only dependency, and it is what every other hook example uses.
# Without it, say so on stderr rather than silently passing.
if command -v jq >/dev/null 2>&1; then
    jq -nc --arg r "$reason" '{decision:"block", reason:$r}'
else
    echo "check-delivery.sh: jq not found; cannot report uncommitted work" >&2
fi
exit 0
