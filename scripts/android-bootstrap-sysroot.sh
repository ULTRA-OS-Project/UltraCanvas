#!/usr/bin/env bash
# Cross-compile UltraCanvas's dependency stack for Android via vcpkg, and print
# the CMake invocation that consumes the result.
#
# ============================ READ THIS FIRST ============================
# THIS SCRIPT HAS NEVER BEEN RUN. It was written in an environment with no
# Android NDK and no route to download one, so nothing below has been executed
# even once. It encodes the plan from
# Docs/UltraCanvas/AndroidPortInvestigation.md §4 ("evaluate vcpkg's
# arm64-android triplet first"), not a proven build.
#
# Expect to iterate on it. The likely failure points, in order:
#   1. A port that has no arm64-android triplet support (pango and cairo are
#      the ones to watch; freetype/harfbuzz/tinyxml2 are routine).
#   2. glib - the investigation calls it "the heaviest pain point after GTK"
#      for cross-builds, usually over iconv/locale.
#   3. pkg-config files that hardcode host paths, so the repo's
#      pkg_check_modules calls find nothing.
# None of these mean the approach is wrong; they mean this needs a run.
# =========================================================================
#
# Usage: scripts/android-bootstrap-sysroot.sh [--with-net] [ABI]
#   ABI defaults to arm64-v8a (vcpkg triplet arm64-android).
#
# Requires:
#   ANDROID_NDK_HOME (or ANDROID_NDK_ROOT) - an installed NDK, r25+.
#   VCPKG_ROOT (optional)                  - a bootstrapped vcpkg checkout;
#       without one, vcpkg is checked out at VCPKG_COMMIT below into
#       .vcpkg/vcpkg and bootstrapped (as scripts/macos-deps.sh does).
#   VCPKG_BINARY_SOURCES (optional)        - vcpkg's binary cache setting; CI
#       points it at a cached directory (.github/workflows/android-build.yml).
#   UC_ANDROID_SYSROOT (optional)          - where the libraries are installed
#       (default: ~/.cache/ultracanvas/android-sysroot). Outside the source
#       tree on purpose: the core library exports the sysroot's lib directory
#       as a link directory, and CMake refuses to export one from inside the
#       source tree (scripts/macos-deps.sh installs outside it for the same
#       reason).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
manifest_dir="$repo/UltraCanvas/OS/Android/packaging"

with_net=0
abi="arm64-v8a"
for arg in "$@"; do
    case "$arg" in
        --with-net) with_net=1 ;;
        arm64-v8a|armeabi-v7a|x86_64|x86) abi="$arg" ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

case "$abi" in
    arm64-v8a)   triplet="arm64-android" ;;
    armeabi-v7a) triplet="arm-android" ;;
    x86_64)      triplet="x64-android" ;;
    x86)         triplet="x86-android" ;;
esac

ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [ -z "$ndk" ] || [ ! -d "$ndk" ]; then
    echo "android-bootstrap-sysroot: set ANDROID_NDK_HOME to an installed NDK" >&2
    exit 2
fi
if [ ! -f "$ndk/build/cmake/android.toolchain.cmake" ]; then
    echo "android-bootstrap-sysroot: '$ndk' has no build/cmake/android.toolchain.cmake" >&2
    exit 2
fi

# The vcpkg ports tree the manifest resolves against: the commit
# scripts/macos-deps.sh pins, so Android and macOS build the same library
# versions (and the same libvips overlay, packaging/vcpkg-configuration.json).
VCPKG_COMMIT=19780d9cdf84d0944cf9a318666703b89ab6629c # 2026-10-04, as scripts/macos-deps.sh

vcpkg_root="${VCPKG_ROOT:-$repo/.vcpkg/vcpkg}"
if [ -z "${VCPKG_ROOT:-}" ] &&
   { [ ! -x "$vcpkg_root/vcpkg" ] || [ "$(git -C "$vcpkg_root" rev-parse HEAD 2>/dev/null)" != "$VCPKG_COMMIT" ]; }; then
    echo "android-bootstrap-sysroot: checking out vcpkg $VCPKG_COMMIT into $vcpkg_root" >&2
    mkdir -p "$vcpkg_root"
    git -C "$vcpkg_root" init -q
    git -C "$vcpkg_root" fetch -q --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_COMMIT"
    git -C "$vcpkg_root" checkout -q --force FETCH_HEAD
    "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics >&2
fi
if [ ! -x "$vcpkg_root/vcpkg" ]; then
    echo "android-bootstrap-sysroot: VCPKG_ROOT '$vcpkg_root' is not a bootstrapped vcpkg checkout" >&2
    exit 2
fi

sysroot="${UC_ANDROID_SYSROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/ultracanvas/android-sysroot}"
mkdir -p "$sysroot"

# vcpkg's android triplets read the NDK location from ANDROID_NDK_HOME and
# chainload the NDK's own toolchain file, so the compilers come from the NDK
# rather than vcpkg.
export ANDROID_NDK_HOME="$ndk"

install_args=(
    install
    --triplet "$triplet"
    "--x-manifest-root=$manifest_dir"
    "--x-install-root=$sysroot"
)
[ "$with_net" -eq 1 ] && install_args+=(--x-feature=net)

echo "android-bootstrap-sysroot: NDK      $ndk"
echo "android-bootstrap-sysroot: vcpkg    $vcpkg_root"
echo "android-bootstrap-sysroot: triplet  $triplet"
echo "android-bootstrap-sysroot: sysroot  $sysroot"
echo

"$vcpkg_root/vcpkg" "${install_args[@]}"

installed="$sysroot/$triplet"
if [ ! -d "$installed/lib/pkgconfig" ]; then
    echo >&2
    echo "android-bootstrap-sysroot: vcpkg finished but '$installed/lib/pkgconfig'" >&2
    echo "  does not exist. The repo finds every dependency through pkg-config," >&2
    echo "  so without those .pc files the CMake configure below cannot work." >&2
    exit 1
fi

cat <<EOF

Sysroot ready: $installed

Configure the build against it with:

  PKG_CONFIG_LIBDIR=$installed/lib/pkgconfig:$installed/share/pkgconfig \\
  cmake -G Ninja -B build-android \\
    -DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake \\
    -DANDROID_ABI=$abi \\
    -DANDROID_PLATFORM=android-26 \\
    -DCMAKE_FIND_ROOT_PATH=$installed \\
    -DPKG_CONFIG_ARGN=--static$([ "$with_net" -eq 1 ] && echo " \\
    -DULTRACANVAS_ENABLE_NET=ON")

PKG_CONFIG_LIBDIR (not PKG_CONFIG_PATH) is what stops pkg-config falling back
to the host's /usr/lib/pkgconfig and silently linking desktop libraries into
an Android build. The NDK's own toolchain file, not vcpkg's: vcpkg.cmake would
install the repository's root vcpkg.json (the Windows set) for Android. The
sysroot's libraries are static (vcpkg's Android triplets), so every
pkg-config query asks for the static link list (--static).
EOF
