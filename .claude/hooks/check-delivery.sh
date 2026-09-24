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
# Version: 1.1.0
# Last Modified: 2026-09-24
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

# Commits that exist only in this clone. No upstream at all counts: a branch
# that was never pushed is as lost as an uncommitted file.
unpushed=""
branch="$(git rev-parse --abbrev-ref HEAD 2>/dev/null)"
if [ -n "$branch" ] && [ "$branch" != "HEAD" ]; then
    if git rev-parse --abbrev-ref '@{u}' >/dev/null 2>&1; then
        unpushed="$(git log --oneline '@{u}..HEAD' 2>/dev/null)"
    else
        unpushed="$(git log --oneline -n 20 2>/dev/null | head -n 20)"
        [ -n "$unpushed" ] && unpushed="(branch '$branch' has no upstream - nothing has been pushed)
$unpushed"
    fi
fi

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

[ -z "$dirty" ] && [ -z "$unpushed" ] && exit 0

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

# jq is the only dependency, and it is what every other hook example uses.
# Without it, say so on stderr rather than silently passing.
if command -v jq >/dev/null 2>&1; then
    jq -nc --arg r "$reason" '{decision:"block", reason:$r}'
else
    echo "check-delivery.sh: jq not found; cannot report uncommitted work" >&2
fi
exit 0
