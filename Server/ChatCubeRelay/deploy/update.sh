#!/bin/bash
# Server/ChatCubeRelay/deploy/update.sh
# Updates a running ChatCube relay to the code in SOURCE_DIR, safely: the
# database backed up first (an upgrade migrates it, and the older relay then
# refuses it), the code installed, the relay restarted and asked for its
# version - and, when it does not come back with the version it was given,
# the previous code and database put back and started again.
#
#   sudo deploy/update.sh [SOURCE_DIR]        # SOURCE_DIR: a copy of Server/ChatCubeRelay
#
# For the layout of deploy/chatcube-relay.service: the code in
# /opt/chatcube-relay with its virtual environment in .venv, run as the user
# chatcube-relay, its settings in /etc/chatcube-relay.env (the database's
# path in CHATCUBE_RELAY_DB, the address it listens on in
# CHATCUBE_RELAY_HOST / _PORT). Another layout sets these variables:
#
#   RELAY_HOME      /opt/chatcube-relay        the installed code
#   RELAY_USER      chatcube-relay             who runs it (and owns RELAY_HOME)
#   RELAY_SERVICE   chatcube-relay             the systemd unit
#   RELAY_ENV_FILE  /etc/chatcube-relay.env
#   RELAY_BACKUPS   /var/lib/chatcube-relay/backups   (the last KEEP_BACKUPS kept)
#   KEEP_BACKUPS    5
#   SYSTEMCTL       systemctl                  (tests put a stand-in here)
#   RUN_AS          "sudo -u $RELAY_USER"      how a command runs as RELAY_USER
#
# The CI deploy job (.github/workflows/chatcube-relay.yml) copies the new
# code to the server and runs this through sudo; by hand it is the same
# command. Exit 0: the relay runs the new version. Exit 1: it was rolled back
# (or nothing was changed) and runs what it ran before.
#
# Version: 0.1.0
# Last Modified: 2026-11-15
# Author: UltraCanvas Framework / ULTRA OS
set -euo pipefail

SOURCE_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
RELAY_HOME="${RELAY_HOME:-/opt/chatcube-relay}"
RELAY_USER="${RELAY_USER:-chatcube-relay}"
RELAY_SERVICE="${RELAY_SERVICE:-chatcube-relay}"
RELAY_ENV_FILE="${RELAY_ENV_FILE:-/etc/chatcube-relay.env}"
RELAY_BACKUPS="${RELAY_BACKUPS:-/var/lib/chatcube-relay/backups}"
KEEP_BACKUPS="${KEEP_BACKUPS:-5}"
SYSTEMCTL="${SYSTEMCTL:-systemctl}"
RUN_AS="${RUN_AS:-sudo -u $RELAY_USER}"
export PIP_DISABLE_PIP_VERSION_CHECK=1

say() { echo "update.sh: $*"; }
die() { echo "update.sh: $*" >&2; exit 1; }

# ---- what is being installed, and where it runs ------------------------------
[ -f "$SOURCE_DIR/pyproject.toml" ] && [ -d "$SOURCE_DIR/chatcube_relay" ] ||
    die "$SOURCE_DIR is not a copy of Server/ChatCubeRelay (no pyproject.toml / chatcube_relay)"
[ -x "$RELAY_HOME/.venv/bin/pip" ] || die "$RELAY_HOME/.venv is missing - install the relay first (README: Running it)"
new_version=$(sed -n 's/^version *= *"\([^"]*\)".*/\1/p' "$SOURCE_DIR/pyproject.toml" | head -1)
[ -n "$new_version" ] || die "no version in $SOURCE_DIR/pyproject.toml"

# The settings the unit reads: the database and the address to ask.
env_value() {
    [ -f "$RELAY_ENV_FILE" ] || return 0
    sed -n "s/^$1=//p" "$RELAY_ENV_FILE" | tail -1 | sed 's/^"\(.*\)"$/\1/'
}
db="$(env_value CHATCUBE_RELAY_DB)"; db="${db:-/var/lib/chatcube-relay/relay.sqlite3}"
host="$(env_value CHATCUBE_RELAY_HOST)"; host="${host:-127.0.0.1}"
port="$(env_value CHATCUBE_RELAY_PORT)"; port="${port:-8790}"
[ "$host" = "0.0.0.0" ] && host=127.0.0.1
health="http://$host:$port/healthz"

running_version() {
    curl -fsS --max-time 3 "$health" 2>/dev/null | sed -n 's/.*"version" *: *"\([^"]*\)".*/\1/p'
}
wait_for_version() {        # the version the relay answers with, within 30 s ("" if none)
    local v=""
    for _ in $(seq 1 60); do
        v=$(running_version || true)
        [ -n "$v" ] && break
        sleep 0.5
    done
    echo "$v"
}

old_version=$(running_version || true)
say "relay at $health: ${old_version:-not answering}; installing $new_version from $SOURCE_DIR"
if [ "$old_version" = "$new_version" ] && [ "${FORCE:-0}" != 1 ]; then
    say "already $new_version - nothing to do (FORCE=1 installs it again)"
    exit 0
fi

# ---- back up, install, restart ----------------------------------------------------
stamp=$(date -u +%Y%m%d-%H%M%S)
backup="$RELAY_BACKUPS/$stamp-from-${old_version:-unknown}"
mkdir -p "$backup"
"$SYSTEMCTL" stop "$RELAY_SERVICE"
# Stopped, the database is whole on disk; its -wal / -shm, when there are
# any, go with it.
for f in "$db" "$db-wal" "$db-shm"; do
    [ -f "$f" ] && cp -p "$f" "$backup/"
done
# The code as it was, for a roll-back (not the virtual environment: pip
# puts the old package back into it).
tar -C "$RELAY_HOME" --exclude=.venv -czf "$backup/code.tar.gz" .
say "backed up the database and the code to $backup"

rollback() {
    say "rolling back to ${old_version:-the previous code}"
    "$SYSTEMCTL" stop "$RELAY_SERVICE" || true
    find "$RELAY_HOME" -mindepth 1 -maxdepth 1 ! -name .venv ! -name .env -exec rm -rf {} +
    tar -C "$RELAY_HOME" -xzf "$backup/code.tar.gz"
    for f in "$db" "$db-wal" "$db-shm"; do
        rm -f "$f"
        [ -f "$backup/$(basename "$f")" ] && cp -p "$backup/$(basename "$f")" "$f"
    done
    $RUN_AS "$RELAY_HOME/.venv/bin/pip" install -q -e "$RELAY_HOME" || true
    "$SYSTEMCTL" start "$RELAY_SERVICE"
    local back
    back=$(wait_for_version)
    say "after the roll-back the relay answers: ${back:-nothing}"
    exit 1
}

# The new code in place of the old (tar, which every server has); the
# virtual environment and a local .env stay.
find "$RELAY_HOME" -mindepth 1 -maxdepth 1 ! -name .venv ! -name .env -exec rm -rf {} + || rollback
tar -C "$SOURCE_DIR" --exclude=./.venv --exclude=__pycache__ --exclude=./.env --exclude='*.sqlite3*' -cf - . |
    tar -C "$RELAY_HOME" -xf - || rollback
chown -R "$RELAY_USER": "$RELAY_HOME" 2>/dev/null || true
$RUN_AS "$RELAY_HOME/.venv/bin/pip" install -q -e "$RELAY_HOME" || rollback
"$SYSTEMCTL" start "$RELAY_SERVICE" || rollback

now=$(wait_for_version)
if [ "$now" != "$new_version" ]; then
    say "the relay answered '${now:-nothing}', not $new_version"
    rollback
fi
say "the relay runs $new_version (was ${old_version:-unknown})"

# The last KEEP_BACKUPS backups kept.
ls -1d "$RELAY_BACKUPS"/*/ 2>/dev/null | sort | head -n -"$KEEP_BACKUPS" | xargs -r rm -rf
