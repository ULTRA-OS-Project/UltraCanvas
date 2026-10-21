#!/usr/bin/env bash
# Install an UltraCanvas APK on a running emulator (or device), start it, and
# pass only if the app presents a first frame with something drawn in it.
#
# "The activity started" proves nothing: a native library that crashes still
# leaves an activity behind. The window backend logs one line the first time
# it presents a frame (OS/Android/UltraCanvasAndroidWindow.cpp, tag
# UltraCanvas), saying whether that frame has content or is one colour
# everywhere. This waits for that line, and fails when the process dies
# first, when the frame is blank, or when the time runs out.
#
# Whatever happens it leaves the evidence in OUT_DIR: a screenshot, the whole
# logcat, the crash buffer, and the app's own lines (tags UltraCanvas and
# UltraCanvas-stdio) on their own.
#
# Usage: scripts/android-emulator-smoke.sh APK OUT_DIR [TIMEOUT_SECONDS]
#   adb must be on PATH with exactly one device attached (the CI job's
#   emulator, or `adb -s` exported as ANDROID_SERIAL).
#   TIMEOUT_SECONDS defaults to 240: the first launch after an install
#   unpacks the APK's assets (about 53 MB for the demo) before any frame.
set -euo pipefail

apk="${1:?usage: android-emulator-smoke.sh APK OUT_DIR [TIMEOUT_SECONDS]}"
out="${2:?usage: android-emulator-smoke.sh APK OUT_DIR [TIMEOUT_SECONDS]}"
timeout="${3:-240}"
package="org.ultraos.ultracanvas.demo"
activity="org.ultraos.ultracanvas.UltraCanvasActivity"
mkdir -p "$out"

collect() {
    adb exec-out screencap -p > "$out/screenshot.png" || true
    adb logcat -d -v time > "$out/logcat.txt" || true
    adb logcat -d -b crash -v time > "$out/logcat-crash.txt" || true
    adb logcat -d -v time -s UltraCanvas:V UltraCanvas-stdio:V > "$out/logcat-ultracanvas.txt" || true
}
trap collect EXIT

echo "android-emulator-smoke: $(adb shell getprop ro.build.version.release | tr -d '\r') " \
     "(API $(adb shell getprop ro.build.version.sdk | tr -d '\r'), $(adb shell getprop ro.product.cpu.abi | tr -d '\r'))"
adb install -r "$apk"
adb logcat -c
# debugOutput on, to logcat: the framework's own diagnostics in the evidence.
adb shell setprop debug.ultracanvas.log 1
adb shell am start -W -n "$package/$activity"

started=0
for ((elapsed = 0; elapsed < timeout; elapsed += 2)); do
    line="$(adb logcat -d -s UltraCanvas:V | grep -m1 'first frame presented' || true)"
    if [ -n "$line" ]; then
        echo "android-emulator-smoke: ${line#*: }"
        # Leave it running a moment so the screenshot shows the settled UI.
        sleep 5
        if grep -q 'with content' <<<"$line"; then
            echo "android-emulator-smoke: PASS"
            exit 0
        fi
        echo "::error::the first frame was blank - the app runs but drew nothing"
        exit 1
    fi
    if [ -n "$(adb shell pidof "$package" | tr -d '\r')" ]; then
        started=1
    elif [ "$started" -eq 1 ] || [ "$elapsed" -ge 10 ]; then
        # Gone, or never seen at all after ten seconds: an app that fails to
        # load its library dies before the first look.
        echo "::error::$package is not running and presented no frame (see logcat-crash.txt)"
        adb logcat -d -b crash | tail -40 || true
        adb logcat -d -s UltraCanvas:V AndroidRuntime:E | tail -20 || true
        exit 1
    fi
    sleep 2
done
echo "::error::no frame within ${timeout}s ($( [ "$started" -eq 1 ] && echo 'the process is running' || echo 'the process never started'))"
adb logcat -d -s UltraCanvas:V UltraCanvas-stdio:V | tail -40 || true
exit 1
