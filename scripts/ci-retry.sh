#!/usr/bin/env bash
# scripts/ci-retry.sh - run a CI command again when it fails.
#
# For the steps that reach the network through a tool with no retry of its
# own - a git clone, a submodule update, `emsdk install`, rustup - so a short
# outage on github.com or a download host costs a pause, not the whole job.
# (A plain HTTP download needs no wrapper: curl --retry 5 --retry-all-errors
# does it, as the workflows' other downloads do.) On 2026-11-03 a momentary
# failure fetching llvm.sh failed both Linux legs of a Build run in under a
# second; that step retries since, and these are the others like it.
#
# Usage:
#   scripts/ci-retry.sh [--attempts N] [--wait SECONDS] [--clean PATH]... -- COMMAND [ARGS...]
#
#   --attempts N   tries in all (default 3)
#   --wait S       pause after the first failure; it grows by S each time
#                  (default 30: 30 s, then 60 s)
#   --clean PATH   removed before every try, so a clone or download a failed
#                  try left half-done does not make the next one fail too
#                  (may be given more than once)
#
# The exit status is the command's own: 0 as soon as one try succeeds, the
# last try's status when all fail. Each retry is announced as a GitHub
# Actions warning, the final failure as an error.

set -uo pipefail

attempts=3
wait_s=30
clean=()
while [ $# -gt 0 ]; do
    case "$1" in
        --attempts) attempts="$2"; shift 2 ;;
        --wait)     wait_s="$2";   shift 2 ;;
        --clean)    clean+=("$2"); shift 2 ;;
        --)         shift; break ;;
        *)          echo "ci-retry: unknown option '$1' (the command goes after --)" >&2; exit 2 ;;
    esac
done
if [ $# -eq 0 ]; then
    echo "ci-retry: no command given (usage: ci-retry.sh [options] -- command [args...])" >&2
    exit 2
fi
case "$attempts" in ''|*[!0-9]*) echo "ci-retry: --attempts wants a number" >&2; exit 2 ;; esac
case "$wait_s"   in ''|*[!0-9]*) echo "ci-retry: --wait wants a number of seconds" >&2; exit 2 ;; esac
[ "$attempts" -ge 1 ] || attempts=1

status=0
for ((try = 1; try <= attempts; try++)); do
    for path in ${clean[@]+"${clean[@]}"}; do
        rm -rf -- "$path"
    done
    "$@"
    status=$?
    [ "$status" -eq 0 ] && exit 0
    if [ "$try" -lt "$attempts" ]; then
        pause=$((try * wait_s))
        echo "::warning::'$*' failed (exit $status, try $try of $attempts); trying again in ${pause} s"
        sleep "$pause"
    fi
done
echo "::error::'$*' failed $attempts times (last exit $status)"
exit "$status"
