#!/usr/bin/env bash
# Type-check VideoFX against libc++ - the standard library the Windows CI
# builds use (MSYS2 CLANG64 / CLANGARM64) - with clang++ -fsyntax-only, so a
# Windows-only compile error is seen before a push rather than 15 minutes
# into the Windows job. Linux and macOS compile the module against libstdc++
# and Apple's libc++, and the three disagree on details a Linux build never
# shows: libc++ gives std::filesystem::file_time_type a 128-bit tick count,
# which std::to_string has no overload for, and PR #973's first Windows
# build failed on exactly that ("call to 'to_string' is ambiguous").
#
# What is checked: VideoFX/core/*.cpp (the engine, as built with FFmpeg -
# VideoFXUnavailable.cpp is the stub built without it, so it is left out),
# VideoFX/OS/Linux (the host's OS arm; the Windows and macOS arms need their
# own system headers), the command-line tool, and VideoFXTest. Only errors
# are printed: the Windows build does not treat warnings as errors either.
#
# Usage: scripts/check_libcxx_syntax.sh [file.cpp ...]
#   Without arguments, the files above; with, only those (paths relative to
#   the repository or absolute). CLANGXX names the compiler (default:
#   clang++); JOBS the parallel compiles (default: the processor count).
#
# Needs clang++ with libc++ (Debian/Ubuntu: apt install clang libc++-dev
# libc++abi-dev) and FFmpeg's development headers (as the build does).
# Exit 0: every file compiles; 1: some do not; 2: the toolchain is missing.
#
# Version: 1.0.0
# Author: UltraCanvas Framework
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
cxx="${CLANGXX:-clang++}"
jobs="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

if ! command -v "$cxx" >/dev/null 2>&1; then
    echo "check_libcxx_syntax: $cxx not found (apt install clang libc++-dev libc++abi-dev, or set CLANGXX)" >&2
    exit 2
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# libc++ itself: a header that only it has, compiled as the module is.
printf '#include <__config>\n#include <filesystem>\nint main() { return 0; }\n' > "$work/probe.cpp"
if ! "$cxx" -std=gnu++20 -stdlib=libc++ -fsyntax-only "$work/probe.cpp" >/dev/null 2>&1; then
    echo "check_libcxx_syntax: $cxx cannot compile with -stdlib=libc++ (apt install libc++-dev libc++abi-dev)" >&2
    exit 2
fi

ffmpeg="libavformat libavcodec libavfilter libavutil libswscale"
if ! ffflags="$(pkg-config --cflags $ffmpeg 2>/dev/null)"; then
    echo "check_libcxx_syntax: FFmpeg development headers missing (pkg-config: $ffmpeg)" >&2
    exit 2
fi

# The flags VideoFX/CMakeLists.txt gives the module (the version a
# placeholder) and Tests/VideoFXTests.cmake the test; -w, since only errors
# are the question here.
flags=(-std=gnu++20 -stdlib=libc++ -fsyntax-only -w
       -I"$repo/VideoFX/include" -I"$repo/VideoFX/core" -I"$repo/UltraCanvas/include"
       -DVIDEOFX_HAS_FFMPEG=1 '-DVIDEOFX_VERSION_STRING="libc++-check"'
       "-DVIDEOFX_TEST_DATA_DIR=\"$repo/Tests/data/videofx\"")
# shellcheck disable=SC2206
flags+=($ffflags)

files=()
if [ "$#" -gt 0 ]; then
    for f in "$@"; do
        case "$f" in /*) files+=("$f") ;; *) files+=("$repo/$f") ;; esac
    done
else
    while IFS= read -r f; do files+=("$f"); done < <(
        find "$repo/VideoFX/core" -name '*.cpp' ! -name 'VideoFXUnavailable.cpp' | sort
        find "$repo/VideoFX/OS/Linux" -name '*.cpp' | sort
        echo "$repo/VideoFX/tools/videofx_cli.cpp"
        echo "$repo/Tests/VideoFXTest.cpp"
    )
fi

# One compile per file, in parallel; each writes its errors to a file of
# its own so the report is per file and in order. The flags reach the
# subshells as one shell-quoted line.
i=0
for f in "${files[@]}"; do
    i=$((i + 1))
    printf '%s\t%s\n' "$i" "$f"
done > "$work/list"
printf '%q ' "${flags[@]}" > "$work/flags"
export WORK="$work" cxx
xargs -P "$jobs" -n 2 bash -c '
    n="$0"; f="$1"
    eval "set -- $(cat "$WORK/flags")"
    if "$cxx" "$@" "$f" > "$WORK/$n.out" 2>&1; then : > "$WORK/$n.ok"; fi
' < "$work/list"

failed=0
while IFS=$'\t' read -r n f; do
    rel="${f#"$repo"/}"
    if [ -e "$work/$n.ok" ]; then
        echo "  ok    $rel"
    else
        failed=$((failed + 1))
        echo "  FAIL  $rel"
        sed 's/^/        /' "$work/$n.out"
    fi
done < "$work/list"

if [ "$failed" -gt 0 ]; then
    echo "check_libcxx_syntax: $failed of ${#files[@]} file(s) do not compile against libc++"
    exit 1
fi
echo "check_libcxx_syntax: ${#files[@]} file(s) compile against libc++ ($("$cxx" --version | head -1))"
