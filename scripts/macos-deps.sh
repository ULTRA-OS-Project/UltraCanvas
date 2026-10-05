#!/usr/bin/env bash
# scripts/macos-deps.sh - build the libraries the macOS apps bundle, for the
# oldest macOS they support, and print the prefix they are installed in.
#
# Homebrew builds each bottle for the macOS of the machine that built it, and
# dyld refuses a library built for a newer macOS than the one running - so
# apps bundling Homebrew's libraries start only on the build machine's macOS
# and newer. This builds the same libraries with vcpkg instead, from the
# manifest, overlay ports and triplets in MacOS/deps/, with
# VCPKG_OSX_DEPLOYMENT_TARGET from the triplet (MacOS/deps/README.md).
#
# Usage: scripts/macos-deps.sh
#   Prints the install prefix (<install root>/<triplet>) as its last line.
#   Pass it to CMake as -DULTRACANVAS_MACOS_DEPS_PREFIX=<prefix> and to
#   package-macos.sh as UC_MACOS_DEPS_PREFIX=<prefix>.
#
# Environment:
#   UC_VCPKG_ROOT          vcpkg checkout (default: .vcpkg/vcpkg). Cloned at
#                          VCPKG_COMMIT below and bootstrapped when missing.
#   UC_DEPS_INSTALL_ROOT   install root (default: .vcpkg/installed)
#   UC_VCPKG_KEEP_GOING=1  build every port it can and list all failures,
#                          instead of stopping at the first
#   VCPKG_BINARY_SOURCES   vcpkg's own binary cache setting (CI points it at a
#                          directory it restores and saves between runs)
#   MACOSX_DEPLOYMENT_TARGET  when set, must match the triplet's target
#
# Needs Xcode's command line tools, git, and the build tools vcpkg's ports
# expect on macOS: brew install autoconf autoconf-archive automake libtool nasm pkg-config

set -euo pipefail

# The vcpkg ports tree the manifest resolves against. Moving it updates every
# library to that commit's versions - check the overlays in MacOS/deps/ports
# (and drop the ones vcpkg has caught up with) when you do.
VCPKG_COMMIT=19780d9cdf84d0944cf9a318666703b89ab6629c # 2026-10-04

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEPS_DIR="$ROOT/MacOS/deps"
VCPKG_ROOT="${UC_VCPKG_ROOT:-$ROOT/.vcpkg/vcpkg}"
INSTALL_ROOT="${UC_DEPS_INSTALL_ROOT:-$ROOT/.vcpkg/installed}"

if [ "$(uname -s)" != "Darwin" ]; then
    echo "macos-deps: this builds macOS libraries and runs on macOS only" >&2
    exit 1
fi

case "$(uname -m)" in
    arm64)  triplet=arm64-osx-ultracanvas; host=arm64-osx ;;
    x86_64) triplet=x64-osx-ultracanvas;   host=x64-osx ;;
    *) echo "macos-deps: unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac

# The deployment target lives in the triplet; CI also exports it for its own
# compiler runs. Two numbers that must agree are checked, not trusted.
triplet_target=$(sed -nE 's/^set\(VCPKG_OSX_DEPLOYMENT_TARGET[[:space:]]+([0-9.]+)\).*/\1/p' \
    "$DEPS_DIR/triplets/$triplet.cmake")
if [ -n "${MACOSX_DEPLOYMENT_TARGET:-}" ] && [ "$MACOSX_DEPLOYMENT_TARGET" != "$triplet_target" ]; then
    echo "macos-deps: MACOSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET, but $triplet.cmake builds for $triplet_target" >&2
    exit 1
fi

# vcpkg at the pinned commit: a shallow fetch of that one commit, so the
# checkout costs seconds rather than vcpkg's whole history.
if [ ! -x "$VCPKG_ROOT/vcpkg" ] || [ "$(git -C "$VCPKG_ROOT" rev-parse HEAD 2>/dev/null)" != "$VCPKG_COMMIT" ]; then
    echo "macos-deps: checking out vcpkg $VCPKG_COMMIT into $VCPKG_ROOT" >&2
    mkdir -p "$VCPKG_ROOT"
    git -C "$VCPKG_ROOT" init -q
    git -C "$VCPKG_ROOT" fetch -q --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_COMMIT"
    git -C "$VCPKG_ROOT" checkout -q --force FETCH_HEAD
    "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics >&2
fi

args=(
    install
    "--x-manifest-root=$DEPS_DIR"
    "--x-install-root=$INSTALL_ROOT"
    "--triplet=$triplet"
    "--host-triplet=$host"
    --clean-after-build
)
[ "${UC_VCPKG_KEEP_GOING:-}" = "1" ] && args+=(--keep-going)

echo "macos-deps: building for $triplet (macOS $triplet_target and newer)" >&2
# Build output to stderr, so the prefix is the only thing on stdout; a copy
# is kept to find the ports that failed.
vcpkg_log=$(mktemp "${TMPDIR:-/tmp}/macos-deps.XXXXXX")
trap 'rm -f "$vcpkg_log"' EXIT
if ! VCPKG_ROOT="$VCPKG_ROOT" "$VCPKG_ROOT/vcpkg" "${args[@]}" 2>&1 | tee "$vcpkg_log" >&2; then
    # vcpkg names a failed port's logs but does not show them, and in CI they
    # go with the machine: print them. Every port leaves its logs in
    # buildtrees, so pick the failed ones from vcpkg's "building <port>:<triplet>
    # failed with: ..." lines and its end summary.
    failed=$(grep -oE '[a-z0-9][a-z0-9-]*:[a-z0-9-]+( failed with)?: [A-Z_]+' "$vcpkg_log" \
        | grep -vE ': (SUCCEEDED|CASCADED_[A-Z_]*)$' | cut -d: -f1 | sort -u | tr '\n' ' ' || true)
    for port in $failed; do
        for log in "$VCPKG_ROOT/buildtrees/$port"/*.log; do
            [ -f "$log" ] || continue
            echo "::group::$port/$(basename "$log") (last 80 lines)" >&2
            tail -n 80 "$log" >&2
            echo "::endgroup::" >&2
        done
    done
    echo "macos-deps: vcpkg failed; ports that failed to build: ${failed:-none named - see the vcpkg output above}" >&2
    grep -E 'failed with: |: (BUILD_FAILED|POST_BUILD_CHECKS_FAILED|FILE_CONFLICTS|CASCADED_)' "$vcpkg_log" >&2 || true
    exit 1
fi

prefix="$INSTALL_ROOT/$triplet"
if [ ! -d "$prefix/lib/pkgconfig" ]; then
    echo "macos-deps: vcpkg finished but $prefix/lib/pkgconfig does not exist" >&2
    exit 1
fi
echo "$prefix"
