#!/usr/bin/env bash
# Type-check the Android backend against REAL NDK headers (bionic, android/*,
# android_native_app_glue) without needing the cross-compiled dependency
# sysroot: NDK clang++ -fsyntax-only, with the host's cairo/pango/fontconfig/
# glib/tinyxml2 headers standing in for the sysroot's (they are architecture-
# independent aside from glibconfig.h, whose LP64 type widths match aarch64).
#
# This is the anti-rot gate from Docs/UltraCanvas/AndroidPortInvestigation.md
# §6 ("Android code is compiled from day one") until the real sysroot + full
# NDK CI build exist (§4/§5). It catches base-class signature drift, NDK API
# misuse, and __linux__/__ANDROID__ ordering regressions - not link errors.
#
# Usage: scripts/android-syntax-check.sh [NDK_ROOT]
#   NDK_ROOT defaults to $ANDROID_NDK_ROOT / $ANDROID_NDK_LATEST_HOME /
#   $ANDROID_NDK (in that order); GitHub's ubuntu runners preinstall an NDK
#   and export ANDROID_NDK_LATEST_HOME.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

ndk="${1:-${ANDROID_NDK_ROOT:-${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK:-}}}}"
if [ -z "$ndk" ] || [ ! -d "$ndk" ]; then
    echo "android-syntax-check: no NDK found (pass a path or set ANDROID_NDK_ROOT)" >&2
    exit 2
fi

host_tag="linux-x86_64"
cxx="$ndk/toolchains/llvm/prebuilt/$host_tag/bin/clang++"
if [ ! -x "$cxx" ]; then
    echo "android-syntax-check: $cxx not found/executable" >&2
    exit 2
fi
target="--target=aarch64-linux-android26"

# Host headers for the not-yet-cross-compiled dependencies.
pkgs="cairo pango pangocairo fontconfig glib-2.0 tinyxml2 freetype2"
if ! hostflags="$(pkg-config --cflags $pkgs)"; then
    echo "android-syntax-check: host dev headers missing (need: $pkgs)" >&2
    exit 2
fi

# OpenSSL headers for the reused UltraNet TLS source. Host /usr/include must
# NOT go on the search path (it would shadow bionic's libc headers), so link
# just the openssl/ directory into a private include root.
shim="$(mktemp -d)"
trap 'rm -rf "$shim"' EXIT
[ -d /usr/include/openssl ] && ln -s /usr/include/openssl "$shim/openssl"

flags=(-std=c++20 -fsyntax-only $target
    -I "$repo/UltraCanvas/include"
    -I "$repo/UltraCanvas/core"
    -I "$repo/UltraCanvas/OS/Android"
    -I "$repo/UltraCanvas/Plugins"
    -I "$repo/UltraCanvas/third_party"
    -isystem "$ndk/sources/android/native_app_glue"
    $hostflags)

status=0
check() {
    printf '  %-58s ' "${1#"$repo"/}"
    if out="$("$cxx" "${flags[@]}" "$1" 2>&1)"; then
        echo "OK"
    else
        echo "FAIL"
        printf '%s\n' "$out" | head -40
        status=1
    fi
}

echo "android-syntax-check: NDK $ndk"
for f in "$repo"/UltraCanvas/OS/Android/*.cpp; do
    check "$f"
done

# The Android UltraNet build reuses these Linux sources verbatim (their
# #ifdef __linux__ guards are satisfied by bionic) - keep them compiling
# under __ANDROID__ too.
check "$repo/UltraCanvas/OS/Linux/UltraNetSupport.cpp"
if [ -e "$shim/openssl" ]; then
    flags+=(-isystem "$shim")
    check "$repo/UltraCanvas/OS/Linux/UltraNetTlsImpl.cpp"
else
    echo "  (skipping UltraNetTlsImpl.cpp - no host OpenSSL headers)"
fi

exit $status
