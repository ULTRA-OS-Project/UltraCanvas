#!/usr/bin/env bash
# scripts/ios-check-sysroot.sh - check that an iOS sysroot holds what it says:
# static archives built for the right Apple platform, and a pkg-config file
# for every library the core asks pkg-config for.
#
# Usage: scripts/ios-check-sysroot.sh <prefix> <simulator|device>
#   <prefix> is the last line scripts/ios-bootstrap-sysroot.sh prints.
#
# Why: a simulator object and a device object are both arm64 and differ only
# in the platform their LC_BUILD_VERSION names (7 = iOS simulator, 2 = iOS).
# A port that ignores the triplet's sysroot builds for the wrong one - or for
# macOS (1) - and nothing fails until an app's link, several phases later.
# This finds it now. Runs on macOS (otool is Xcode's).
set -euo pipefail

prefix="${1:?usage: ios-check-sysroot.sh <prefix> <simulator|device>}"
kind="${2:?usage: ios-check-sysroot.sh <prefix> <simulator|device>}"
case "$kind" in
    simulator) want=7; want_name="iOS simulator" ;;
    device)    want=2; want_name="iOS" ;;
    *) echo "ios-check-sysroot: kind must be simulator or device, not '$kind'" >&2; exit 2 ;;
esac

if [ ! -d "$prefix/lib" ]; then
    echo "ios-check-sysroot: '$prefix/lib' does not exist" >&2
    exit 2
fi

status=0

# Dynamic libraries have no place here: the triplets build static ones, and an
# iOS app could load a stray .dylib only from a signed framework.
dylibs=$(find "$prefix/lib" -name '*.dylib' -print)
if [ -n "$dylibs" ]; then
    echo "::error::dynamic libraries in a static iOS sysroot:"
    echo "$dylibs"
    status=1
fi

# Nor frameworks: a port that builds one on iOS (libtiff did) leaves no
# lib<name>.a for pkg-config's -l<name> to find.
frameworks=$(find "$prefix/lib" -maxdepth 1 -name '*.framework' -print)
if [ -n "$frameworks" ]; then
    echo "::error::frameworks in the sysroot, where a static archive was wanted:"
    echo "$frameworks"
    status=1
fi

archives=$(find "$prefix/lib" -maxdepth 1 -name '*.a' | sort)
if [ -z "$archives" ]; then
    echo "::error::no static archives under $prefix/lib"
    exit 1
fi

checked=0
for a in $archives; do
    # Every object's LC_BUILD_VERSION platform (older toolchains wrote
    # LC_VERSION_MIN_IPHONEOS instead, which only a device object has).
    platforms=$(otool -l "$a" 2>/dev/null | awk '
        /cmd LC_BUILD_VERSION/ { inbv = 1; next }
        inbv && $1 == "platform" { print $2; inbv = 0 }
        /cmd LC_VERSION_MIN_IPHONEOS/ { print 2 }' | sort -u | tr '\n' ' ')
    platforms="${platforms% }"
    checked=$((checked + 1))
    if [ -z "$platforms" ]; then
        echo "::warning::$(basename "$a"): no platform recorded in its objects"
    elif [ "$platforms" != "$want" ]; then
        echo "::error::$(basename "$a"): built for platform(s) $platforms, want $want ($want_name)"
        status=1
    else
        echo "ok   $(basename "$a")"
    fi
done

# The modules UltraCanvas/CMakeLists.txt asks pkg-config for, as the manifest
# lists them (the Android set).
for pc in cairo pango pangocairo fontconfig freetype2 harfbuzz glib-2.0 gobject-2.0 tinyxml2 vips vips-cpp librsvg-2.0; do
    if [ -f "$prefix/lib/pkgconfig/$pc.pc" ] || [ -f "$prefix/share/pkgconfig/$pc.pc" ]; then
        echo "ok   $pc.pc"
    else
        echo "::error::no $pc.pc in $prefix"
        status=1
    fi
done

# cairo must not have been built with its Quartz backend: its header pulls in
# ApplicationServices, which iOS does not have.
if [ -f "$prefix/include/cairo/cairo-quartz.h" ]; then
    echo "::error::cairo was built with its Quartz backend (cairo-quartz.h is installed)"
    status=1
fi

echo
echo "ios-check-sysroot: $checked archives checked for $want_name; $( [ $status -eq 0 ] && echo passed || echo FAILED )"
exit $status
