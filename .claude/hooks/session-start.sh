#!/usr/bin/env bash
# .claude/hooks/session-start.sh
# SessionStart hook: give a cloud session the packages CI builds and tests
# with that the session's image does not carry.
#
# Why this exists. The Claude Code cloud image is a general Ubuntu 24.04 with
# the core graphics stack, but without most of what .github/workflows/build.yml
# installs on its Linux rows. CMake leaves a missing optional library out
# without a word, so a cloud session built and tested a different program than
# CI: no libsodium meant UltraCrypt refused every operation and every
# credential-vault test failed here (UltraMail, UltraSocial, AnchorPoint,
# UltraFIBU's store) while passing in CI; no FFmpeg, GStreamer, MuPDF, c-ares,
# libpq or FreeRDP meant VideoFX, the media backends, the PDF plugin, UltraNet's
# resolver, UltraFIBU's multi-user server and UltraWin were not built at all.
# A session then had to tell a real failure from a missing package. This
# installs them before the session starts, so the build and its tests mean the
# same thing here as in CI.
#
# It runs only in the cloud ($CLAUDE_CODE_REMOTE=true): a developer's own
# machine is theirs to set up. It is idempotent - packages already installed
# are skipped without touching apt (a few milliseconds) - and it never blocks
# the session: when an install fails (no network, no root, a name this
# distribution does not know) it installs what it can, says in one line what
# is missing, and exits 0.
#
# PACKAGES mirrors CI's Linux install step with Ubuntu 24.04's names. Where CI
# builds a library from source because Ubuntu 22.04 ships it too old or not at
# all (MuPDF, libopusenc, c-ares), 24.04's package is new enough and is used
# instead. Not mirrored: ccache (a CI build cache), and the services CI starts
# for its live tests - PostgreSQL, Avahi and the IPP printer - whose tests
# skip unless CI's environment asks for them. To add a package, append it.
#
# Version: 1.1.0 - every library CI installs, not only libsodium
# Version: 1.0.0
# Last Modified: 2026-10-07
# Author: UltraCanvas Framework
set -u

[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0

PACKAGES=(
    # toolchain and the core graphics stack
    cmake pkg-config build-essential
    libcairo2-dev libpango1.0-dev libharfbuzz-dev libvips-dev libglib2.0-dev libfreetype-dev dbus
    libtinyxml2-dev libfmt-dev
    libx11-dev libxcursor-dev libgl1-mesa-dev libgtk-3-dev
    # CorelDRAW import, colour management, Unicode, barcodes
    libcdr-dev librevenge-dev zlib1g-dev libboost-dev liblcms2-dev libicu-dev libzbar-dev
    # devices: hot-plug, printing, scanning, remote desktop (UltraWin)
    libudev-dev libcups2-dev libsane-dev freerdp2-dev
    imagemagick libmagickcore-6.q16-7-extra
    # the PDF plugin: MuPDF and the libraries it links
    libmupdf-dev libjbig2dec0-dev libmujs-dev libgumbo-dev libopenjp2-7-dev libjpeg-dev
    libunwind-dev
    # media: GStreamer, FFmpeg (VideoFX), audio codecs
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
    libavformat-dev libavcodec-dev libavfilter-dev libavutil-dev libswscale-dev
    libflac-dev libvorbis-dev libogg-dev libopus-dev libopusfile-dev libmp3lame-dev libopusenc-dev
    # UltraNet: curl, async DNS
    libcurl4-openssl-dev libpsl-dev libc-ares-dev
    # UltraCrypt (credential vaults), UltraDatabase (SQLite, PostgreSQL client)
    libsodium-dev libsqlite3-dev libpq-dev
)

# The packages not installed yet, in one dpkg call.
missing_packages() {
    local installed
    installed=$(dpkg-query -W -f='${Package} ${db:Status-Status}\n' "${PACKAGES[@]}" 2>/dev/null |
                awk '$2 == "installed" { sub(/:.*/, "", $1); print $1 }')
    local p
    for p in "${PACKAGES[@]}"; do
        grep -qxF "$p" <<<"$installed" || echo "$p"
    done
}

mapfile -t missing < <(missing_packages)
[ "${#missing[@]}" -eq 0 ] && exit 0

if [ "$(id -u)" -eq 0 ]; then
    as_root=()
elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    as_root=(sudo -n)
else
    echo "Cloud setup: cannot install ${missing[*]} (not root, no sudo);" \
         "the build leaves out what needs them and those tests skip or fail in this session."
    exit 0
fi

# apt's output goes to a log, not stdout: stdout of a SessionStart hook is
# added to the session's context.
log="${TMPDIR:-/tmp}/ultracanvas-session-start.log"
export DEBIAN_FRONTEND=noninteractive
apt_install() {
    ${as_root[@]+"${as_root[@]}"} apt-get install -y --no-install-recommends "$@" >>"$log" 2>&1
}
# The image's package lists may be stale or absent; refresh only when the
# first attempt fails, so a warm container does not pay for an update. One
# name apt does not know fails the whole batch, so the last resort installs
# them one at a time and keeps what it can.
if ! apt_install "${missing[@]}"; then
    ${as_root[@]+"${as_root[@]}"} apt-get update >>"$log" 2>&1
    if ! apt_install "${missing[@]}"; then
        for p in "${missing[@]}"; do apt_install "$p" || true; done
    fi
fi

mapfile -t still < <(missing_packages)
if [ "${#still[@]}" -gt 0 ]; then
    echo "Cloud setup: installing ${still[*]} failed (see $log);" \
         "the build leaves out what needs them and those tests skip or fail in this session."
fi
exit 0
