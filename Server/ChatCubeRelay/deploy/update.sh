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
#   CHATCUBE_RELAY_HOME      /opt/chatcube-relay           the installed code
#   CHATCUBE_RELAY_USER      chatcube-relay                who runs it (and owns the code)
#   CHATCUBE_RELAY_SERVICE   chatcube-relay                the systemd unit
#   CHATCUBE_RELAY_ENV_FILE  /etc/chatcube-relay.env
#   CHATCUBE_RELAY_BACKUPS   /var/lib/chatcube-relay/backups  (the last CHATCUBE_KEEP_BACKUPS kept)
#   CHATCUBE_KEEP_BACKUPS    5
#   CHATCUBE_SYSTEMCTL       systemctl                     (tests put a stand-in here)
#   CHATCUBE_RUN_AS          "sudo -u $CHATCUBE_RELAY_USER"  how a command runs as that user
#   CHATCUBE_FORCE           0                             1: install even the version running
#
# Before 0.2.0 these had no CHATCUBE_ prefix (RELAY_HOME, KEEP_BACKUPS, ...);
# an old name is still read when the new one is unset, with a warning.
# Through sudo, which clears the environment, set them on its command line:
# sudo CHATCUBE_RELAY_HOME=/srv/relay deploy/update.sh ...
#
# The CI deploy job (.github/workflows/chatcube-relay.yml) copies the new
# code to the server and runs this through sudo; by hand it is the same
# command. Exit 0: the relay runs the new version. Exit 1: it was rolled back
# (or nothing was changed) and runs what it ran before.
#
# Version: 0.2.0 - its settings are named CHATCUBE_* (CHATCUBE_RELAY_HOME, ...);
#                 the 0.1 names (RELAY_HOME, ...) are still read, with a warning
# Version: 0.1.0
# Last Modified: 2026-11-17
# Author: UltraCanvas Framework / ULTRA OS
set -euo pipefail

SOURCE_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
# Each setting by its CHATCUBE_ name; the name it had before 0.2.0 (without
# the prefix) is still read when the new one is unset, with a warning, so a
# server set up for 0.1 keeps its layout until its operator renames it.
setting() {     # setting NAME DEFAULT: $CHATCUBE_NAME, else $NAME (warned), else DEFAULT
    local new="CHATCUBE_$1" old="$1"
    if [ -n "${!new:-}" ]; then
        printf '%s' "${!new}"
    elif [ -n "${!old:-}" ]; then
        echo "update.sh: $old is read as $new - rename it (the unprefixed name goes in a later version)" >&2
        printf '%s' "${!old}"
    else
        printf '%s' "$2"
    fi
}
CHATCUBE_RELAY_HOME="$(setting RELAY_HOME /home/chatcube-relay/ChatCubeRelay)"
CHATCUBE_RELAY_USER="$(setting RELAY_USER chatcube-relay)"
CHATCUBE_RELAY_SERVICE="$(setting RELAY_SERVICE chatcube-relay)"
CHATCUBE_RELAY_ENV_FILE="$(setting RELAY_ENV_FILE /home/chatcube-relay/conf/.env)"
CHATCUBE_RELAY_BACKUPS="$(setting RELAY_BACKUPS /home/chatcube-relay/backups)"
CHATCUBE_KEEP_BACKUPS="$(setting KEEP_BACKUPS 5)"
CHATCUBE_SYSTEMCTL="$(setting SYSTEMCTL systemctl)"
CHATCUBE_RUN_AS="$(setting RUN_AS "sudo -u $CHATCUBE_RELAY_USER")"
CHATCUBE_FORCE="$(setting FORCE 0)"
export PIP_DISABLE_PIP_VERSION_CHECK=1

say() { echo "update.sh: $*"; }
die() { echo "update.sh: $*" >&2; exit 1; }

# ---- what is being installed, and where it runs ------------------------------
[ -f "$SOURCE_DIR/pyproject.toml" ] && [ -d "$SOURCE_DIR/chatcube_relay" ] ||
    die "$SOURCE_DIR is not a copy of Server/ChatCubeRelay (no pyproject.toml / chatcube_relay)"
[ -x "$CHATCUBE_RELAY_HOME/.venv/bin/pip" ] || die "$CHATCUBE_RELAY_HOME/.venv is missing - install the relay first (README: Running it)"
new_version=$(sed -n 's/^version *= *"\([^"]*\)".*/\1/p' "$SOURCE_DIR/pyproject.toml" | head -1)
[ -n "$new_version" ] || die "no version in $SOURCE_DIR/pyproject.toml"

# The settings the unit reads: the database and the address to ask.
env_value() {
    [ -f "$CHATCUBE_RELAY_ENV_FILE" ] || return 0
    sed -n "s/^$1=//p" "$CHATCUBE_RELAY_ENV_FILE" | tail -1 | sed 's/^"\(.*\)"$/\1/'
}
db="$(env_value CHATCUBE_RELAY_DB)"; db="${db:-/home/chatcube-relay/data/relay.sqlite3}"
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
if [ "$old_version" = "$new_version" ] && [ "$CHATCUBE_FORCE" != 1 ]; then
    say "already $new_version - nothing to do (CHATCUBE_FORCE=1 installs it again)"
    exit 0
fi

# ---- back up, install, restart ----------------------------------------------------
stamp=$(date -u +%Y%m%d-%H%M%S)
backup="$CHATCUBE_RELAY_BACKUPS/$stamp-from-${old_version:-unknown}"
mkdir -p "$backup"
"$CHATCUBE_SYSTEMCTL" stop "$CHATCUBE_RELAY_SERVICE"
# Stopped, the database is whole on disk; its -wal / -shm, when there are
# any, go with it.
for f in "$db" "$db-wal" "$db-shm"; do
    [ -f "$f" ] && cp -p "$f" "$backup/"
done
# The code as it was, for a roll-back (not the virtual environment: pip
# puts the old package back into it).
tar -C "$CHATCUBE_RELAY_HOME" --exclude=.venv -czf "$backup/code.tar.gz" .
say "backed up the database and the code to $backup"

rollback() {
    say "rolling back to ${old_version:-the previous code}"
    "$CHATCUBE_SYSTEMCTL" stop "$CHATCUBE_RELAY_SERVICE" || true
    find "$CHATCUBE_RELAY_HOME" -mindepth 1 -maxdepth 1 ! -name .venv ! -name .env -exec rm -rf {} +
    tar -C "$CHATCUBE_RELAY_HOME" -xzf "$backup/code.tar.gz"
    for f in "$db" "$db-wal" "$db-shm"; do
        rm -f "$f"
        [ -f "$backup/$(basename "$f")" ] && cp -p "$backup/$(basename "$f")" "$f"
    done
    $CHATCUBE_RUN_AS "$CHATCUBE_RELAY_HOME/.venv/bin/pip" install -q -e "$CHATCUBE_RELAY_HOME" || true
    "$CHATCUBE_SYSTEMCTL" start "$CHATCUBE_RELAY_SERVICE"
    local back
    back=$(wait_for_version)
    say "after the roll-back the relay answers: ${back:-nothing}"
    exit 1
}

# The new code in place of the old (tar, which every server has); the
# virtual environment and a local .env stay.
find "$CHATCUBE_RELAY_HOME" -mindepth 1 -maxdepth 1 ! -name .venv ! -name .env -exec rm -rf {} + || rollback
tar -C "$SOURCE_DIR" --exclude=./.venv --exclude=__pycache__ --exclude=./.env --exclude='*.sqlite3*' -cf - . |
    tar -C "$CHATCUBE_RELAY_HOME" -xf - || rollback
chown -R "$CHATCUBE_RELAY_USER": "$CHATCUBE_RELAY_HOME" 2>/dev/null || true
$CHATCUBE_RUN_AS "$CHATCUBE_RELAY_HOME/.venv/bin/pip" install -q -e "$CHATCUBE_RELAY_HOME" || rollback
"$CHATCUBE_SYSTEMCTL" start "$CHATCUBE_RELAY_SERVICE" || rollback

now=$(wait_for_version)
if [ "$now" != "$new_version" ]; then
    say "the relay answered '${now:-nothing}', not $new_version"
    rollback
fi
say "the relay runs $new_version (was ${old_version:-unknown})"

# The last CHATCUBE_KEEP_BACKUPS backups kept.
ls -1d "$CHATCUBE_RELAY_BACKUPS"/*/ 2>/dev/null | sort | head -n -"$CHATCUBE_KEEP_BACKUPS" | xargs -r rm -rf
