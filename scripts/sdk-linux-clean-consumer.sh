#!/usr/bin/env bash
# scripts/sdk-linux-clean-consumer.sh <sdk-archive> <ultracanvasstart-archive> <out-dir>
#
# The Linux SDK on the machine of someone who has nothing else: run as root
# in a clean ubuntu:22.04 container (the sdk-linux job of
# .github/workflows/build.yml), never on a build runner, which has every
# library the framework was built with and so hides what a user would miss.
# On 2026-11-03 a user's build of UltraCanvasStart's skeleton stopped with
# "No rule to make target '/usr/lib/x86_64-linux-gnu/libmp3lame.so'": the SDK
# named a file that only the build runner had, and the CMake package test on
# that runner could not see it.
#
#   1. The desktop the standalone applications expect from the host - the
#      libraries package-linux.sh's EXCLUDE_PATTERNS leave out (glibc, the GL
#      stack, X11/xcb, Wayland, libudev) - and nothing else.
#   2. UltraCanvasStart, the standalone package from the same build: --plan,
#      then the apt-get line its install step prints, run as printed (root
#      needs no sudo), then --check, which must find nothing missing.
#   3. The SDK unpacked, and what its core still cannot load on this machine
#      added to its lib/ from UltraCanvasStart's lib/ (which holds the core's
#      whole closure), each with an $ORIGIN runpath, the core's included, so
#      an application finds them without LD_LIBRARY_PATH. A library the core
#      needs that neither has fails the job.
#   4. The project skeleton written by UltraCanvasStart --create and built
#      with the two commands its Build step runs; PackageConsumer, shipped in
#      the SDK, built and run.
#   5. The SDK packed again into <out-dir>, under its own name: that archive,
#      with the libraries added, is what the release publishes.
#
# Version: 0.1.0
# Author: UltraCanvas Framework / ULTRA OS
set -euo pipefail

[ $# -eq 3 ] || { echo "usage: $0 <sdk-archive> <ultracanvasstart-archive> <out-dir>" >&2; exit 2; }
SDK_ARCHIVE=$(readlink -f "$1")
START_ARCHIVE=$(readlink -f "$2")
OUT=$3
[ "$(id -u)" -eq 0 ] || { echo "error: run as root, in a throwaway container" >&2; exit 1; }
grep -q 'VERSION_ID="22.04"' /etc/os-release || { echo "error: not Ubuntu 22.04" >&2; exit 1; }

export DEBIAN_FRONTEND=noninteractive
WORK=/opt/sdk-clean
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT"
OUT=$(readlink -f "$OUT")
group() { echo "::group::$*"; }
endgroup() { echo "::endgroup::"; }

# ---- 1. the desktop -----------------------------------------------------------
group "1. the desktop: what the standalone packages expect from the host"
apt-get update -q
# xz-utils and ca-certificates are on every desktop; the rest is the host's
# share in package-linux.sh's EXCLUDE_PATTERNS, by Ubuntu 22.04 package. patchelf
# is the one tool of this job's own (step 3); it changes no library lookup.
apt-get install -y -q --no-install-recommends \
    xz-utils ca-certificates patchelf \
    libgl1 libegl1 libopengl0 libgles2 libglx0 libgbm1 libdrm2 \
    libx11-6 libx11-xcb1 libxcb1 libxcb-render0 libxcb-shm0 \
    libwayland-client0 libwayland-cursor0 libwayland-egl1 libudev1
endgroup

# ---- 2. UltraCanvasStart sets the machine up ------------------------------------
group "2. UltraCanvasStart: --plan, its install command, --check"
mkdir -p "$WORK/start"
tar -xf "$START_ARCHIVE" -C "$WORK/start"
START=$(ls -d "$WORK"/start/UltraCanvasStart-Linux-*)
missing=$(LD_LIBRARY_PATH="$START/lib" ldd "$START/bin/UltraCanvasStart" | grep 'not found' || true)
if [ -n "$missing" ]; then
    echo "::error::the standalone UltraCanvasStart does not start on a 22.04 desktop:"
    echo "$missing"
    exit 1
fi
set +e
"$START/UltraCanvasStart" --plan > "$WORK/plan.txt" 2>&1
status=$?
set -e
cat "$WORK/plan.txt"
[ "$status" -eq 0 ] || [ "$status" -eq 2 ] || { echo "::error::UltraCanvasStart --plan exited with $status"; exit 1; }
# The install step's command, as the window and the report show it:
# "     $ sudo /usr/bin/apt-get install -y clang build-essential ...".
install_line=$(grep -m1 -E '^ +\$ (sudo )?[^ ]*apt-get install' "$WORK/plan.txt" || true)
if [ -n "$install_line" ]; then
    read -r -a install_cmd <<< "$(sed -E 's/^ +\$ (sudo )?//' <<< "$install_line")"
    echo "Running what the plan prints: ${install_cmd[*]}"
    "${install_cmd[@]}"
else
    echo "::warning::the plan has no apt-get install step on a clean 22.04"
fi
set +e
"$START/UltraCanvasStart" --check > "$WORK/check.txt" 2>&1
status=$?
set -e
cat "$WORK/check.txt"
[ "$status" -eq 0 ] || { echo "::error::UltraCanvasStart --check still finds something missing after its own install step"; exit 1; }
endgroup

# ---- 3. the SDK, with what this machine lacks -----------------------------------
group "3. the SDK: the core's run-time libraries this machine lacks"
mkdir -p "$WORK/sdk"
tar -xf "$SDK_ARCHIVE" -C "$WORK/sdk"
SDK=$(ls -d "$WORK"/sdk/UltraCanvas-SDK-Linux-*)
SDK_NAME=$(basename "$SDK")
CORE=$(readlink -f "$SDK/lib/libUltraCanvas.so")
added=0
# ldd follows the whole closure, so one pass names every missing soname;
# repeat because a library just added may need one ldd could not reach yet.
for _ in 1 2 3 4 5 6 7 8; do
    names=$(LD_LIBRARY_PATH="$SDK/lib" ldd "$CORE" | awk '/not found/ {print $1}' | sort -u)
    [ -n "$names" ] || break
    for name in $names; do
        if [ ! -e "$START/lib/$name" ]; then
            echo "::error::the core needs $name, which neither this machine nor UltraCanvasStart's lib/ has"
            exit 1
        fi
        cp -L "$START/lib/$name" "$SDK/lib/$name"
        echo "  + $name"
        added=$((added + 1))
    done
done
if LD_LIBRARY_PATH="$SDK/lib" ldd "$CORE" | grep -q 'not found'; then
    echo "::error::the core still has libraries it cannot load:"
    LD_LIBRARY_PATH="$SDK/lib" ldd "$CORE" | grep 'not found'
    exit 1
fi
# $ORIGIN on the core and on each library added: the loader looks beside the
# file that needs a library, so neither the application nor the user sets
# LD_LIBRARY_PATH. The vendored libcurl.so.4 beside the core is found the
# same way.
for f in "$SDK"/lib/*.so*; do
    [ -L "$f" ] && continue
    # shellcheck disable=SC2016  # the literal $ORIGIN, which the loader expands
    patchelf --set-rpath '$ORIGIN' "$f"
done
echo "$added libraries added to $SDK_NAME/lib"
if env -u LD_LIBRARY_PATH ldd "$CORE" | grep -q 'not found'; then
    echo "::error::without LD_LIBRARY_PATH the core still misses:"
    ldd "$CORE" | grep 'not found'
    exit 1
fi
endgroup

# ---- 4. an application, the way a programmer builds one --------------------------
group "4. the skeleton (UltraCanvasStart --create, the Build step's two commands)"
"$START/UltraCanvasStart" --create "$WORK/MyApp" --name MyApp --sdk "$SDK"
( cd "$WORK/MyApp" && cmake --preset default && cmake --build --preset default )
app=$(find "$WORK/MyApp/build" -maxdepth 2 -type f -name MyApp -perm -u+x | head -1)
[ -n "$app" ] || { echo "::error::the skeleton built no MyApp executable"; exit 1; }
echo "--- dependencies of $app"
ldd "$app"
if ldd "$app" | grep -q 'not found'; then
    echo "::error::the skeleton does not find every library it needs"
    exit 1
fi
endgroup

group "4. PackageConsumer, built and run"
cmake -S "$SDK/examples/PackageConsumer" -B "$WORK/consumer" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$SDK"
cmake --build "$WORK/consumer" --parallel 2
"$WORK/consumer/PackageConsumer"
endgroup

# ---- 5. the SDK the release publishes -------------------------------------------
group "5. pack $SDK_NAME again"
rm -f "$OUT/$SDK_NAME.tar.xz"
XZ_OPT="-T0" tar -C "$WORK/sdk" -cJf "$OUT/$SDK_NAME.tar.xz" "$SDK_NAME"
ls -la "$OUT"
endgroup
echo "OK: $SDK_NAME builds and runs an application on a clean Ubuntu 22.04 set up by UltraCanvasStart"
