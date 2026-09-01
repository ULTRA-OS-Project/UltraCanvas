#!/usr/bin/env bash
# Compile the Android backend's Java sources against a platform android.jar.
#
# The Java half of the backend (UltraCanvasActivity) is loaded by name at
# runtime and called over JNI, so nothing else in the build ever type-checks
# it: a typo or a signature that drifts from the C++ side would only surface
# on a device. This is the Java counterpart of android-syntax-check.sh - the
# anti-rot gate from Docs/UltraCanvas/AndroidPortInvestigation.md §6.
#
# It checks the Java compiles. It cannot check that the native method
# signatures still match their JNI counterparts; keep them in step by hand
# (they are listed next to each other in UltraCanvasAndroidDialogBridge.cpp).
#
# Usage: scripts/android-java-check.sh [ANDROID_JAR]
#   ANDROID_JAR defaults to the newest platform under $ANDROID_SDK_ROOT /
#   $ANDROID_HOME; GitHub's ubuntu runners preinstall an SDK and export both.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
java_root="$repo/UltraCanvas/OS/Android/java"

android_jar="${1:-}"
if [ -z "$android_jar" ]; then
    for sdk in "${ANDROID_SDK_ROOT:-}" "${ANDROID_HOME:-}" /usr/local/lib/android/sdk; do
        [ -n "$sdk" ] && [ -d "$sdk/platforms" ] || continue
        # Newest platform wins: version-sort the android-NN directories.
        candidate="$(ls -d "$sdk"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)"
        if [ -n "$candidate" ]; then
            android_jar="$candidate"
            break
        fi
    done
fi

if [ -z "$android_jar" ] || [ ! -f "$android_jar" ]; then
    echo "android-java-check: no android.jar found (pass a path or set ANDROID_SDK_ROOT)" >&2
    exit 2
fi

if ! command -v javac >/dev/null 2>&1; then
    echo "android-java-check: javac not found (needs a JDK)" >&2
    exit 2
fi

mapfile -t sources < <(find "$java_root" -name '*.java' | sort)
if [ ${#sources[@]} -eq 0 ]; then
    echo "android-java-check: no Java sources under $java_root" >&2
    exit 2
fi

outdir="$(mktemp -d)"
trap 'rm -rf "$outdir"' EXIT

echo "android-java-check: android.jar $android_jar"

# A real android.jar carries the java.* classes too, so it can replace the
# boot classpath outright - that is the strict check, catching any use of a
# JDK class Android does not ship. A cut-down stand-in (the stub jar used for
# local smoke runs) has only the android.* classes, so it goes on the regular
# classpath with the JDK supplying java.*.
if unzip -p "$android_jar" java/lang/Object.class >/dev/null 2>&1; then
    platform_flags=(-bootclasspath "$android_jar")
else
    echo "  (partial android.jar: java.* resolved from the JDK)"
    platform_flags=(-classpath "$android_jar")
fi

status=0
# -Xlint:-options silences the "bootstrap classpath not set" note that every
# --release-less cross-compile against android.jar produces.
if javac -Xlint:all -Xlint:-options "${platform_flags[@]}" \
         -source 8 -target 8 -d "$outdir" "${sources[@]}"; then
    for src in "${sources[@]}"; do
        printf '  %-58s OK\n' "${src#"$repo"/}"
    done
else
    status=1
fi

exit $status
