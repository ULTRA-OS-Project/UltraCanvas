#!/usr/bin/env bash
# Install MobilePC on a running emulator, give it a second display, and pass
# only if the desktop is drawn on that display, two hosted applications start
# on it (UltraPaint, and UltraFiler by its id), and the desktop moves back to the phone's screen when the display
# goes - with the process alive throughout.
#
# The second display is the emulator's simulated one (Settings > Developer
# options > Simulate secondary displays, set here through
# `settings put global overlay_display_devices`): Android offers it as a
# presentation display, exactly as it offers a monitor on a USB-C cable, so
# UltraCanvasActivity puts its Presentation there and the framework's desktop
# mode draws the desktop into it (OS/Android/UltraCanvasAndroidDesktop.cpp).
# That code logs (tag UltraCanvas) where the desktop is and its first frame
# there; MobilePC logs each application its --launch argument started.
#
# Whatever happens it leaves the evidence in OUT_DIR: screenshots of the
# phone and of the second display where screencap can reach it, the whole
# logcat, the crash buffer, and the app's own lines.
#
# Usage: scripts/android-mobilepc-smoke.sh APK OUT_DIR [TIMEOUT_SECONDS]
#   adb must be on PATH with exactly one device attached.
#   TIMEOUT_SECONDS defaults to 240: the first launch unpacks the assets.
set -euo pipefail

apk="${1:?usage: android-mobilepc-smoke.sh APK OUT_DIR [TIMEOUT_SECONDS]}"
out="${2:?usage: android-mobilepc-smoke.sh APK OUT_DIR [TIMEOUT_SECONDS]}"
timeout="${3:-240}"
package="org.ultraos.mobilepc"
activity="org.ultraos.ultracanvas.UltraCanvasActivity"
display_spec="1280x720/213"
mkdir -p "$out"

collect() {
    adb exec-out screencap -p > "$out/screenshot-phone.png" || true
    # The other displays by their SurfaceFlinger ids (API 29+); the first is
    # the phone's own, the rest the simulated one.
    for id in $(adb shell dumpsys SurfaceFlinger --display-id 2>/dev/null | tr -d '\r' |
                sed -n 's/^Display \([0-9]*\).*/\1/p' | tail -n +2); do
        adb exec-out screencap -d "$id" -p > "$out/screenshot-display-$id.png" || true
    done
    adb logcat -d -v time > "$out/logcat.txt" || true
    adb logcat -d -b crash -v time > "$out/logcat-crash.txt" || true
    adb logcat -d -v time -s UltraCanvas:V UltraCanvas-stdio:V > "$out/logcat-ultracanvas.txt" || true
    adb shell settings put global overlay_display_devices null >/dev/null 2>&1 || true
}
trap collect EXIT

app_lines() { adb logcat -d -s UltraCanvas:V; }

# wait_for PATTERN SECONDS: the first app line matching PATTERN, or fail if
# the process died or the time ran out.
wait_for() {
    local pattern="$1" seconds="$2" line=""
    for ((waited = 0; waited < seconds; waited += 2)); do
        line="$(app_lines | grep -m1 -E "$pattern" || true)"
        if [ -n "$line" ]; then
            echo "android-mobilepc-smoke: ${line#*: }"
            return 0
        fi
        if [ "$waited" -gt 10 ] && [ -z "$(adb shell pidof "$package" | tr -d '\r')" ]; then
            echo "::error::MobilePC is no longer running (waiting for: $pattern)"
            adb logcat -d -b crash | tail -30 || true
            exit 1
        fi
        sleep 2
    done
    echo "::error::no '$pattern' line within ${seconds}s"
    app_lines | tail -30 || true
    exit 1
}

echo "android-mobilepc-smoke: $(adb shell getprop ro.build.version.release | tr -d '\r') " \
     "(API $(adb shell getprop ro.build.version.sdk | tr -d '\r'), $(adb shell getprop ro.product.cpu.abi | tr -d '\r'))"
adb install -r "$apk"
adb logcat -c
adb shell setprop debug.ultracanvas.log 1

# The second display first, so it is there when the desktop starts.
adb shell settings put global overlay_display_devices "$display_spec"
sleep 3
# adb shell hands one string to the device's shell, which splits it again:
# the value is quoted for that shell too, or "ultrapaint" becomes am's
# package argument and the app gets "--launch" alone.
adb shell am start -W -n "$package/$activity" -e ultracanvas.args "'--launch ultrapaint --launch ultrafiler'"

wait_for 'desktop mode enabled' "$timeout"
wait_for 'desktop output: the external display' 60
wait_for 'desktop frame presented on external display' 60
for app in ultrapaint ultrafiler; do
    wait_for "MobilePC: ($app did not start|started $app)" 60
    if app_lines | grep -q "MobilePC: $app did not start"; then
        echo "::error::$app did not start on the desktop"
        exit 1
    fi
done
sleep 5   # the screenshots show the settled desktop
adb exec-out screencap -p > "$out/screenshot-phone-control-pad.png" || true

# The display goes: the desktop comes back to the phone's own screen. The
# log is cleared first: the desktop may have been on the phone for a moment
# at the start, before the second display's surface arrived.
adb logcat -d -v time > "$out/logcat-before-unplug.txt" || true
adb logcat -c
adb shell settings put global overlay_display_devices null
wait_for "desktop output: the phone's screen" 60
wait_for 'desktop frame presented on phone' 60
sleep 3
if [ -z "$(adb shell pidof "$package" | tr -d '\r')" ]; then
    echo "::error::MobilePC died after the second display went"
    exit 1
fi
adb exec-out screencap -p > "$out/screenshot-phone-desktop.png" || true
echo "android-mobilepc-smoke: passed"
