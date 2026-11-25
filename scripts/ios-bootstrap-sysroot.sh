#!/usr/bin/env bash
# scripts/ios-bootstrap-sysroot.sh - cross-compile UltraCanvas's dependency
# stack for iOS with vcpkg, and print where it was installed.
#
# Phase 1 of Docs/UltraCanvas/iOSPortInvestigation.md (section 4): the
# libraries only. There is no iOS backend yet, so nothing links against them;
# .github/workflows/ios-build.yml runs this on GitHub's macOS runner and
# checks that every archive was built for the platform it names.
#
# Usage: scripts/ios-bootstrap-sysroot.sh [--with-net] [simulator|device]
#   simulator (default)  arm64-ios-simulator-ultracanvas: the iphonesimulator SDK
#   device               arm64-ios-ultracanvas: the iphoneos SDK
#   --with-net           also OpenSSL, c-ares and curl (the manifest's net
#                        feature), for UltraNet
#   Prints the install prefix (<sysroot>/<triplet>) as its last line.
#
# Environment:
#   VCPKG_ROOT (optional)            a bootstrapped vcpkg checkout; without one,
#       vcpkg is checked out at VCPKG_COMMIT below into .vcpkg/vcpkg and
#       bootstrapped (as scripts/macos-deps.sh does).
#   VCPKG_BINARY_SOURCES (optional)  vcpkg's binary cache setting; CI points it
#       at a cached directory.
#   UC_IOS_SYSROOT (optional)        the install root (default:
#       ~/.cache/ultracanvas/ios-sysroot). Outside the source tree on purpose:
#       CMake refuses to export a link directory from inside it, as
#       scripts/macos-deps.sh and scripts/android-bootstrap-sysroot.sh explain.
#   UC_VCPKG_KEEP_GOING=1            build every port it can and list all
#       failures, instead of stopping at the first.
#
# Needs macOS with Xcode (the iOS SDKs come with it, not with the command line
# tools alone) and the build tools vcpkg's ports expect:
#   brew install autoconf autoconf-archive automake libtool nasm pkg-config
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
manifest_dir="$repo/UltraCanvas/OS/iOS/packaging"

with_net=0
kind="simulator"
for arg in "$@"; do
    case "$arg" in
        --with-net) with_net=1 ;;
        simulator|device) kind="$arg" ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "ios-bootstrap-sysroot: unknown argument: $arg" >&2; exit 2 ;;
    esac
done

case "$kind" in
    simulator) triplet="arm64-ios-simulator-ultracanvas"; sdk="iphonesimulator" ;;
    device)    triplet="arm64-ios-ultracanvas";           sdk="iphoneos" ;;
esac

if [ "$(uname -s)" != "Darwin" ]; then
    echo "ios-bootstrap-sysroot: the iOS SDKs exist only on macOS; run this on a Mac with Xcode" >&2
    exit 2
fi
# The host tools (pkgconf, meson's helpers, glib's code generators) are built
# for this Mac; vcpkg needs to be told which one.
case "$(uname -m)" in
    arm64)  host="arm64-osx" ;;
    x86_64) host="x64-osx" ;;
    *) echo "ios-bootstrap-sysroot: unsupported host architecture $(uname -m)" >&2; exit 2 ;;
esac
if ! sdk_path="$(xcrun --sdk "$sdk" --show-sdk-path 2>/dev/null)"; then
    echo "ios-bootstrap-sysroot: no $sdk SDK - install Xcode (not only the command line" >&2
    echo "  tools) and select it with: sudo xcode-select -s /Applications/Xcode.app" >&2
    exit 2
fi

# The vcpkg ports tree the manifest resolves against: the commit
# scripts/macos-deps.sh pins, so iOS, Android and macOS build the same library
# versions (and the same libvips overlay, packaging/vcpkg-configuration.json).
VCPKG_COMMIT=19780d9cdf84d0944cf9a318666703b89ab6629c # 2026-10-04, as scripts/macos-deps.sh

vcpkg_root="${VCPKG_ROOT:-$repo/.vcpkg/vcpkg}"
if [ -z "${VCPKG_ROOT:-}" ] &&
   { [ ! -x "$vcpkg_root/vcpkg" ] || [ "$(git -C "$vcpkg_root" rev-parse HEAD 2>/dev/null)" != "$VCPKG_COMMIT" ]; }; then
    echo "ios-bootstrap-sysroot: checking out vcpkg $VCPKG_COMMIT into $vcpkg_root" >&2
    mkdir -p "$vcpkg_root"
    git -C "$vcpkg_root" init -q
    git -C "$vcpkg_root" fetch -q --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_COMMIT"
    git -C "$vcpkg_root" checkout -q --force FETCH_HEAD
    "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics >&2
fi
if [ ! -x "$vcpkg_root/vcpkg" ]; then
    echo "ios-bootstrap-sysroot: VCPKG_ROOT '$vcpkg_root' is not a bootstrapped vcpkg checkout" >&2
    exit 2
fi

sysroot="${UC_IOS_SYSROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/ultracanvas/ios-sysroot}"
mkdir -p "$sysroot"
sysroot="$(cd "$sysroot" && pwd -P)"
case "$sysroot/" in
    "$(cd "$repo" && pwd -P)/"*)
        echo "ios-bootstrap-sysroot: the sysroot $sysroot is inside the source tree, where CMake" >&2
        echo "  refuses to export link directories from; set UC_IOS_SYSROOT outside $repo" >&2
        exit 2 ;;
esac

install_args=(
    install
    "--triplet=$triplet"
    "--host-triplet=$host"
    "--x-manifest-root=$manifest_dir"
    "--x-install-root=$sysroot"
    --clean-after-build
)
[ "$with_net" -eq 1 ] && install_args+=(--x-feature=net)
[ "${UC_VCPKG_KEEP_GOING:-}" = "1" ] && install_args+=(--keep-going)

{
    echo "ios-bootstrap-sysroot: SDK      $sdk_path"
    echo "ios-bootstrap-sysroot: vcpkg    $vcpkg_root"
    echo "ios-bootstrap-sysroot: triplet  $triplet (host $host)"
    echo "ios-bootstrap-sysroot: sysroot  $sysroot"
    echo
} >&2

# Build output to stderr, so the prefix is the only thing on stdout; a copy is
# kept to name the ports that failed (as scripts/macos-deps.sh does).
vcpkg_log="$(mktemp "${TMPDIR:-/tmp}/ios-bootstrap-sysroot.XXXXXX")"
trap 'rm -f "$vcpkg_log"' EXIT
if ! VCPKG_ROOT="$vcpkg_root" "$vcpkg_root/vcpkg" "${install_args[@]}" 2>&1 | tee "$vcpkg_log" >&2; then
    failed=$(grep -oE '[a-z0-9][a-z0-9-]*:[a-z0-9-]+( failed with)?: [A-Z_]+' "$vcpkg_log" \
        | grep -vE ': (SUCCEEDED|CASCADED_[A-Z_]*)$' | cut -d: -f1 | sort -u | tr '\n' ' ' || true)
    for port in $failed; do
        for log in "$vcpkg_root/buildtrees/$port"/*.log; do
            [ -f "$log" ] || continue
            echo "::group::$port/$(basename "$log") (last 80 lines)" >&2
            tail -n 80 "$log" >&2
            echo "::endgroup::" >&2
        done
    done
    echo "ios-bootstrap-sysroot: vcpkg failed; ports that failed to build: ${failed:-none named - see the vcpkg output above}" >&2
    exit 1
fi

installed="$sysroot/$triplet"
if [ ! -d "$installed/lib/pkgconfig" ]; then
    echo "ios-bootstrap-sysroot: vcpkg finished but '$installed/lib/pkgconfig' does not exist." >&2
    echo "  The repo finds every dependency through pkg-config, so a configure against" >&2
    echo "  this sysroot could not work." >&2
    exit 1
fi
echo "$installed"
