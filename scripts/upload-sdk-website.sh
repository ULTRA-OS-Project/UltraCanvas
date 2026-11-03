#!/usr/bin/env bash
# scripts/upload-sdk-website.sh <archive-dir> <version>
# scripts/upload-sdk-website.sh --check
#
# Puts the six SDK archives of one release on the UltraOS website, the server
# behind https://ultraos.eu/download-file/ (the page UltraCanvasStart's
# Framework step opens - Apps/UltraCanvasStart/engine/StartSdk.cpp), so the
# page can offer the version UltraCanvasStart names without anyone copying
# the files over by hand. The publish-sdk-website job of
# .github/workflows/build.yml runs it after the GitHub release has the same
# archives (Docs/UltraCanvasSDK.md).
#
# It speaks SFTP only, in batch mode: a web host that gives an SFTP account
# with no shell and no rsync is enough. On the server:
#
#   $ULTRAOS_SFTP_DIR/                 must exist; the script never creates it
#     <version>/                       created on the first upload
#       UltraCanvas-SDK-<OS>-<version>-<arch>.<ext>   the six archives
#       SHA256SUMS                     `sha256sum -c SHA256SUMS` checks them
#
# Each file goes up as <name>.part and is renamed to <name> once it is
# complete, so the page never serves half an archive under its real name. A
# second run for the same version replaces the files; older versions are
# left alone.
#
# --check uploads no archive: it logs in, puts a small probe file into
# ULTRAOS_SFTP_DIR, renames it the way an archive is renamed, lists the
# directory and removes the probe. That proves the key, the host key, the
# directory and the rights the upload needs, without a release
# (.github/workflows/sdk-website-check.yml runs it).
#
# Settings, from the environment (the jobs take each from the repository's
# Actions variables, or from a secret of the same name when no variable has
# it; the key and the known hosts belong in secrets):
#
#   ULTRAOS_SFTP_HOST         the server's host name
#   ULTRAOS_SFTP_PORT         its SSH port; 22 when empty
#   ULTRAOS_SFTP_USER         the SFTP account
#   ULTRAOS_SFTP_DIR          the directory the versions go under
#   ULTRAOS_SFTP_KEY          the account's private key, OpenSSH    (secret)
#   ULTRAOS_SFTP_KNOWN_HOSTS  the server's line(s) from
#                             `ssh-keyscan -p <port> <host>`        (secret)
#
# The host key is checked against ULTRAOS_SFTP_KNOWN_HOSTS and never
# accepted on first sight: a runner that connects to whatever answers would
# hand the archives - and the account - to an impostor.
#
# Version: 0.2.0 - --check; the settings may be variables or secrets
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS
set -euo pipefail

usage() {
    echo "usage: $0 <archive-dir> <version>" >&2
    echo "       $0 --check" >&2
    exit 2
}

CHECK=0
if [ $# -eq 1 ] && [ "$1" = --check ]; then
    CHECK=1
elif [ $# -eq 2 ]; then
    ARCHIVE_DIR=$1
    VERSION=$2
    [ -d "$ARCHIVE_DIR" ] || { echo "error: $ARCHIVE_DIR is not a directory" >&2; exit 1; }
    [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
        echo "error: '$VERSION' is not a version (x.y.z)" >&2
        exit 1
    }
else
    usage
fi

missing=()
for name in ULTRAOS_SFTP_HOST ULTRAOS_SFTP_USER ULTRAOS_SFTP_DIR \
            ULTRAOS_SFTP_KEY ULTRAOS_SFTP_KNOWN_HOSTS; do
    [ -n "${!name:-}" ] || missing+=("$name")
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "error: not set: ${missing[*]} (scripts/upload-sdk-website.sh names each)" >&2
    exit 1
fi
PORT=${ULTRAOS_SFTP_PORT:-22}
[[ "$PORT" =~ ^[0-9]+$ ]] || { echo "error: ULTRAOS_SFTP_PORT '$PORT' is not a port" >&2; exit 1; }
# The remote directory without a trailing slash, so "<dir>/<version>" is one
# slash whether or not the setting ended in one.
REMOTE_DIR=${ULTRAOS_SFTP_DIR%/}
[ -n "$REMOTE_DIR" ] || REMOTE_DIR=/
# sftp's batch grammar splits on whitespace and reads quotes; a path that
# needs either is refused here rather than half-understood there.
case "$REMOTE_DIR" in
    *[[:space:]\"\'\\]*)
        echo "error: ULTRAOS_SFTP_DIR '$REMOTE_DIR' has whitespace, a quote or a backslash" >&2
        exit 1 ;;
esac

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
umask 077
printf '%s\n' "$ULTRAOS_SFTP_KEY" > "$WORK/key"
printf '%s\n' "$ULTRAOS_SFTP_KNOWN_HOSTS" > "$WORK/known_hosts"

# One sftp session over $WORK/batch. The host key is checked, never learnt.
run_sftp() {
    sftp -b "$WORK/batch" -P "$PORT" -i "$WORK/key" \
        -o BatchMode=yes \
        -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=yes \
        -o UserKnownHostsFile="$WORK/known_hosts" \
        -o GlobalKnownHostsFile=/dev/null \
        -o ConnectTimeout=30 \
        -o ServerAliveInterval=30 \
        "$ULTRAOS_SFTP_USER@$ULTRAOS_SFTP_HOST"
}

if [ "$CHECK" = 1 ]; then
    probe=".upload-check-$(date -u +%Y%m%dT%H%M%SZ)-$$"
    echo "probe file written by scripts/upload-sdk-website.sh --check" > "$WORK/probe"
    # No leading '-': every step must work, the way the upload needs it to.
    {
        echo "put \"$WORK/probe\" \"$REMOTE_DIR/$probe.part\""
        echo "rename \"$REMOTE_DIR/$probe.part\" \"$REMOTE_DIR/$probe\""
        echo "ls -l $REMOTE_DIR"
        echo "rm \"$REMOTE_DIR/$probe\""
    } > "$WORK/batch"
    echo "Checking $ULTRAOS_SFTP_USER@$ULTRAOS_SFTP_HOST:$REMOTE_DIR (port $PORT)"
    if ! run_sftp; then
        echo "::error::the SFTP check failed - the lines above say at which step"
        echo "  'Permission denied (publickey...)': the server does not accept ULTRAOS_SFTP_KEY for ULTRAOS_SFTP_USER"
        echo "  'Host key verification failed': ULTRAOS_SFTP_KNOWN_HOSTS is not this server's key (ssh-keyscan -p $PORT $ULTRAOS_SFTP_HOST)"
        echo "  'No such file' / 'Permission denied' at put or rename: ULTRAOS_SFTP_DIR is missing or not writable for the account"
        exit 1
    fi
    echo "OK: the account can log in, write, rename and delete in $REMOTE_DIR"
    exit 0
fi

# The archives: the names SdkArchiveName gives, for this version only.
shopt -s nullglob
archives=()
for f in "$ARCHIVE_DIR"/UltraCanvas-SDK-*-"$VERSION"-*.tar.xz \
         "$ARCHIVE_DIR"/UltraCanvas-SDK-*-"$VERSION"-*.tar.gz \
         "$ARCHIVE_DIR"/UltraCanvas-SDK-*-"$VERSION"-*.zip; do
    archives+=("$(basename "$f")")
done
shopt -u nullglob
if [ ${#archives[@]} -eq 0 ]; then
    echo "error: no UltraCanvas-SDK-*-$VERSION-* archive in $ARCHIVE_DIR" >&2
    ls -la "$ARCHIVE_DIR" >&2
    exit 1
fi
if [ ${#archives[@]} -ne 6 ]; then
    # The release job warns the same way: a leg that failed leaves a gap,
    # and the archives that exist are still worth publishing.
    echo "::warning::${#archives[@]} SDK archives for $VERSION instead of 6"
fi

( cd "$ARCHIVE_DIR" && sha256sum "${archives[@]}" ) > "$WORK/SHA256SUMS"
chmod og+r "$WORK/SHA256SUMS"

# The batch: a leading '-' lets a command fail without ending the batch
# (the version directory exists on a re-run; there is no old file to remove
# on a first one). Everything else must succeed, or sftp exits non-zero.
target="$REMOTE_DIR/$VERSION"
{
    echo "-mkdir $target"
    for name in "${archives[@]}" SHA256SUMS; do
        if [ "$name" = SHA256SUMS ]; then src="$WORK/SHA256SUMS"; else src="$ARCHIVE_DIR/$name"; fi
        echo "put \"$src\" \"$target/$name.part\""
        echo "-rm \"$target/$name\""
        echo "rename \"$target/$name.part\" \"$target/$name\""
    done
    echo "ls -l $target"
} > "$WORK/batch"

echo "Uploading ${#archives[@]} archives and SHA256SUMS to $ULTRAOS_SFTP_USER@$ULTRAOS_SFTP_HOST:$target"
run_sftp
echo "Done: $target"
