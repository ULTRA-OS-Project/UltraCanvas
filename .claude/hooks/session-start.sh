#!/usr/bin/env bash
# .claude/hooks/session-start.sh
# SessionStart hook: give a cloud session the libraries the build needs that
# its image does not carry.
#
# Why this exists. The Claude Code cloud image ships libsodium's runtime
# (libsodium23) but not its headers, so pkg-config does not find libsodium,
# UltraCrypt builds without it and refuses every operation, and every test
# that opens a credential vault fails here - the six UltraMail vault and
# OAuth tests among them - while the same tests pass in CI, which installs
# libsodium-dev (.github/workflows/build.yml). A session then has to tell a
# real failure from a missing package. This installs the package before the
# session starts, so the tests mean the same thing here as in CI.
#
# It runs only in the cloud ($CLAUDE_CODE_REMOTE=true): a developer's own
# machine is theirs to set up. It is idempotent - a package already present
# is skipped without touching apt - and it never blocks the session: when an
# install fails (no network, no root) it says which package is missing and
# what that breaks, and exits 0.
#
# Packages are listed as "<pkg-config name>:<Debian package>". To add one the
# image lacks, append it to PACKAGES; CI's install list is the reference.
#
# Version: 1.0.0
# Last Modified: 2026-10-05
# Author: UltraCanvas Framework
set -u

[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0

PACKAGES=(
    "libsodium:libsodium-dev"   # UltraCrypt: credential vaults, AnchorPoint, UltraFIBU store
)

missing=()
for entry in "${PACKAGES[@]}"; do
    pc="${entry%%:*}"
    deb="${entry#*:}"
    pkg-config --exists "$pc" 2>/dev/null || missing+=("$deb")
done
[ "${#missing[@]}" -eq 0 ] && exit 0

if [ "$(id -u)" -eq 0 ]; then
    as_root=()
elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    as_root=(sudo -n)
else
    echo "Cloud setup: cannot install ${missing[*]} (not root, no sudo);" \
         "the build disables what needs them and those tests fail in this session."
    exit 0
fi

# apt's output goes to a log, not stdout: stdout of a SessionStart hook is
# added to the session's context.
log="${TMPDIR:-/tmp}/ultracanvas-session-start.log"
export DEBIAN_FRONTEND=noninteractive
apt_install() {
    ${as_root[@]+"${as_root[@]}"} apt-get install -y --no-install-recommends "${missing[@]}" >>"$log" 2>&1
}
# The image's package lists may be stale or absent; refresh only when the
# first attempt fails, so a warm container does not pay for an update.
if ! apt_install; then
    ${as_root[@]+"${as_root[@]}"} apt-get update >>"$log" 2>&1 && apt_install
fi

still=()
for entry in "${PACKAGES[@]}"; do
    pkg-config --exists "${entry%%:*}" 2>/dev/null || still+=("${entry#*:}")
done
if [ "${#still[@]}" -gt 0 ]; then
    echo "Cloud setup: installing ${still[*]} failed (see $log);" \
         "the build disables what needs them and those tests fail in this session."
fi
exit 0
