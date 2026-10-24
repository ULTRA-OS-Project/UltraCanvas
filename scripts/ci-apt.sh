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
# The packages themselves are kept between runs as well (cache-in,
# cache-out): on 2026-10-23 the same mirror served the Ubuntu x86_64 job so
# slowly that its dependency step ran out of its 45 minutes twice in a row,
# downloading packages it had downloaded the day before. With the .deb files
# of the last run on main restored first, apt fetches only what changed.
#
# Usage: scripts/ci-apt.sh update
#        scripts/ci-apt.sh install [apt-get install options] package...
#        scripts/ci-apt.sh cache-in DIR    the .deb files in DIR into apt's cache
#        scripts/ci-apt.sh cache-out DIR   apt's cache, as installed, into DIR;
#                                          prints a hash of the file names
#
# Version: 1.1.0 - cache-in, cache-out: the downloaded packages kept between runs
# Version: 1.0.0
# Last Modified: 2026-10-24
# Author: UltraCanvas Framework
set -euo pipefail

# Root for the whole script, so that the watchdog can see apt's files and
# stop apt's processes. The runners' sudo needs no password.
if [[ $EUID -ne 0 ]]; then
    exec sudo --preserve-env=CI_APT_IDLE,CI_APT_ATTEMPTS "$0" "$@"
fi

archives=/var/cache/apt/archives

idle_limit="${CI_APT_IDLE:-120}"
attempts="${CI_APT_ATTEMPTS:-3}"

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
# or stalled. It looks every second: a look is one `du` of two small
# directories, and the loop's sleep is also how long a finished apt-get waits
# to be noticed - at five seconds that came to about a minute per Linux job,
# whose dependency step makes two dozen apt calls.
watched() {
    local pid last now since
    apt-get "$@" &
    pid=$!
    last=$(downloaded)
    since=$SECONDS
    while kill -0 "$pid" 2>/dev/null; do
        sleep 1
        now=$(downloaded)
        if [[ "$now" != "$last" ]]; then
            last=$now
            since=$SECONDS
            continue
        fi
        if (( SECONDS - since >= idle_limit )); then
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
    cache-in)
        dir="${1:?cache-in needs a folder}"
        # apt-get keeps what it downloads unless told otherwise, and a runner
        # image may tell it otherwise (Docker's docker-clean hook): say so
        # explicitly, so cache-out finds every package this job fetched.
        rm -f /etc/apt/apt.conf.d/docker-clean
        printf '%s\n' 'APT::Keep-Downloaded-Packages "true";' \
                       'Binary::apt::APT::Keep-Downloaded-Packages "true";' \
            > /etc/apt/apt.conf.d/99ci-keep-downloaded-packages
        restored=0
        if [[ -d "$dir" ]]; then
            for deb in "$dir"/*.deb; do
                [[ -e "$deb" ]] || continue
                [[ -e "$archives/${deb##*/}" ]] && continue
                cp "$deb" "$archives/"
                restored=$(( restored + 1 ))
            done
        fi
        # apt checks each file against the index before it uses it, so a
        # stale or damaged one is downloaded again rather than installed.
        echo "ci-apt: $restored package files restored into $archives"
        ;;
    cache-out)
        dir="${1:?cache-out needs a folder}"
        mkdir -p "$dir"
        rm -f "$dir"/*.deb
        # Only the version of each package now installed: an older one would
        # never be used again and would only grow the cache.
        kept=0
        for deb in "$archives"/*.deb; do
            [[ -e "$deb" ]] || continue
            read -r package version arch < <(dpkg-deb --show \
                --showformat='${Package} ${Version} ${Architecture}\n' "$deb") || continue
            if dpkg-query -W -f='${Architecture} ${Version} ${db:Status-Status}\n' "$package" 2>/dev/null \
                    | grep -qxF "$arch $version installed"; then
                cp "$deb" "$dir/"
                kept=$(( kept + 1 ))
            fi
        done
        if [[ -n "${SUDO_UID:-}" ]]; then
            chown -R "$SUDO_UID:${SUDO_GID:-$SUDO_UID}" "$dir"
        fi
        echo "ci-apt: $kept package files kept in $dir ($(du -sh "$dir" | cut -f1))" >&2
        # The same set of files is the same cache: the caller keys it by this.
        ls "$dir" | sort | sha1sum | cut -c1-16
        ;;
    *)
        echo "usage: $0 update | install [apt-get install options] package... | cache-in DIR | cache-out DIR" >&2
        exit 2
        ;;
esac
