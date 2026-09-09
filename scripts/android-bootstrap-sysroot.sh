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
#   VCPKG_ROOT                             - a bootstrapped vcpkg checkout.
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

vcpkg_root="${VCPKG_ROOT:-}"
if [ -z "$vcpkg_root" ] || [ ! -x "$vcpkg_root/vcpkg" ]; then
    echo "android-bootstrap-sysroot: set VCPKG_ROOT to a bootstrapped vcpkg checkout" >&2
    echo "  git clone https://github.com/microsoft/vcpkg && ./vcpkg/bootstrap-vcpkg.sh" >&2
    exit 2
fi

sysroot="$repo/build-android-sysroot"
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

  PKG_CONFIG_LIBDIR=$installed/lib/pkgconfig \\
  cmake -B build-android \\
    -DCMAKE_TOOLCHAIN_FILE=$vcpkg_root/scripts/buildsystems/vcpkg.cmake \\
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake \\
    -DVCPKG_TARGET_TRIPLET=$triplet \\
    -DANDROID_ABI=$abi \\
    -DANDROID_PLATFORM=android-26$([ "$with_net" -eq 1 ] && echo " \\
    -DULTRACANVAS_ENABLE_NET=ON")

PKG_CONFIG_LIBDIR (not PKG_CONFIG_PATH) is what stops pkg-config falling back
to the host's /usr/lib/pkgconfig and silently linking desktop libraries into
an Android build.
EOF
