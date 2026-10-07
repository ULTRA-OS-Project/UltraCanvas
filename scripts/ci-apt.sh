#!/usr/bin/env bash
# apt-get for CI jobs: a download that stops dead is stopped and retried.
#
# A degraded Ubuntu mirror can hold a job for an hour, and apt's own network
# timeouts never fire because the connection stays technically alive. On
# 2026-10-07 an `apt-get update` sat on one InRelease download from
# archive.ubuntu.com for fifty minutes without receiving another byte, until
# the job was cancelled; its re-run took 19 seconds.
#
# So apt-get runs under a watchdog. When nothing has been downloaded for
# CI_APT_IDLE seconds (default 120), it is stopped and started again on a
# fresh connection, up to CI_APT_ATTEMPTS times (default 3). apt resumes the
# partial files, so a retry loses nothing.
#
# The watchdog looks at progress, not elapsed time, on purpose. The same
# mirrors also had a slow day: azure.archive.ubuntu.com trickled packages at
# about 30 KB/s, and installs that normally take three minutes took fifteen
# to twenty-seven, and succeeded. A deadline would have failed those runs.
#
# Only downloading is ever interrupted. `install` fetches with --download-only
# first, under the watchdog, and then installs from the local cache without
# it: killing apt while dpkg is unpacking would leave the package database
# half configured for every later step.
#
# Usage: scripts/ci-apt.sh update
#        scripts/ci-apt.sh install [apt-get install options] package...
#
# Version: 1.0.0
# Last Modified: 2026-10-07
# Author: UltraCanvas Framework
set -euo pipefail

# Root for the whole script, so that the watchdog can see apt's files and
# stop apt's processes. The runners' sudo needs no password.
if [[ $EUID -ne 0 ]]; then
    exec sudo --preserve-env=CI_APT_IDLE,CI_APT_ATTEMPTS "$0" "$@"
fi

idle_limit="${CI_APT_IDLE:-120}"
attempts="${CI_APT_ATTEMPTS:-3}"
poll=5

# Bytes apt has on disk: its lists and package cache, partial files included.
# Any change means data is arriving.
downloaded() {
    du -sb /var/lib/apt/lists /var/cache/apt/archives 2>/dev/null \
        | awk '{ total += $1 } END { print total + 0 }'
}

# Stops apt-get $1 and the download methods it started (they can outlive it
# on a dead socket, and would keep writing into the partial files the next
# attempt resumes).
stop_apt() {
    local pid=$1 waited=0
    pkill -TERM -P "$pid" 2>/dev/null || true
    kill -TERM "$pid" 2>/dev/null || true
    while kill -0 "$pid" 2>/dev/null && (( waited < 30 )); do
        sleep 1
        waited=$(( waited + 1 ))
    done
    kill -KILL "$pid" 2>/dev/null || true
    pkill -KILL -f '^/usr/lib/apt/methods/' 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

# apt-get "$@" under the watchdog. 0 when it succeeded, 1 when it failed
# or stalled.
watched() {
    local pid last now idle=0
    apt-get "$@" &
    pid=$!
    last=$(downloaded)
    while kill -0 "$pid" 2>/dev/null; do
        sleep "$poll"
        now=$(downloaded)
        if [[ "$now" != "$last" ]]; then
            last=$now
            idle=0
            continue
        fi
        idle=$(( idle + poll ))
        if (( idle >= idle_limit )); then
            echo "::warning::apt-get $1: nothing downloaded for ${idle_limit}s, stopping it" >&2
            stop_apt "$pid"
            return 1
        fi
    done
    wait "$pid"
}

# apt-get "$@" under the watchdog, retried.
fetch() {
    local attempt
    for (( attempt = 1; attempt <= attempts; attempt++ )); do
        if watched "$@"; then
            return 0
        fi
        echo "::warning::apt-get $1 failed (attempt $attempt of $attempts)" >&2
        if (( attempt < attempts )); then
            sleep 10
        fi
    done
    echo "::error::apt-get did not complete in $attempts attempts: apt-get $*" >&2
    return 1
}

command="${1:-}"
shift || true
case "$command" in
    update)
        fetch update "$@"
        ;;
    install)
        fetch install -y --download-only "$@"
        apt-get install -y "$@"
        ;;
    *)
        echo "usage: $0 update | install [apt-get install options] package..." >&2
        exit 2
        ;;
esac
