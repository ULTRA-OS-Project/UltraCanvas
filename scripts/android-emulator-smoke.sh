#!/usr/bin/env bash
# Install an UltraCanvas APK on a running emulator (or device), start it, and
# pass only if the app presents a first frame with something drawn in it.
#
# "The activity started" proves nothing: a native library that crashes still
# leaves an activity behind. The window backend logs one line the first time
# it presents a frame (OS/Android/UltraCanvasAndroidWindow.cpp, tag
# UltraCanvas), saying whether that frame has content or is one colour
# everywhere. This waits for that line, and fails when the process dies
# first, when the frame is blank, or when the time runs out - and, once the
# frame is there, when an SVG failed to load ("is not a known file format":
# the APK's libvips was built without SVG support), and when the display's
# density did not reach the frame: the surface must be exactly the window's
# size in pixels, and the scale the display's density / 160. Run on a
# high-density display (CI runs one at 420 dpi) that is what catches a
# backend drawing at the wrong scale, or a pixel short that the compositor
# then stretches. Then it taps the demo's tree filter field (the demo logs
# where it is) and types a word into it: the backend logs the element the
# first tap landed on, and the demo logs the filter's text, so a touch that
# lands in the wrong place or a key that never reaches the focused field
# fails here. Last, it turns the display to landscape and waits for a frame
# at the new size that again fills the window: the activity is kept across a
# rotation (configChanges in the manifest), so the backend must see the
# window resize in place and rebuild its surface.
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
# --no-about: the demo's modal About window would cover the field to tap.
adb shell am start -W -n "$package/$activity" -e ultracanvas.args "--no-about"

started=0
for ((elapsed = 0; elapsed < timeout; elapsed += 2)); do
    line="$(adb logcat -d -s UltraCanvas:V | grep -m1 'first frame presented' || true)"
    if [ -n "$line" ]; then
        echo "android-emulator-smoke: ${line#*: }"
        # Leave it running a moment so the screenshot shows the settled UI.
        sleep 5
        if grep -q 'with content' <<<"$line"; then
            # "first frame presented: 1080x2337, window 1080x2337, scale 2.625, ..."
            frame="$(sed -n 's/.*first frame presented: \([0-9]*x[0-9]*\).*/\1/p' <<<"$line")"
            window="$(sed -n 's/.*, window \([0-9]*x[0-9]*\).*/\1/p' <<<"$line")"
            scale="$(sed -n 's/.*, scale \([0-9.]*\).*/\1/p' <<<"$line")"
            if [ -z "$frame" ] || [ "$frame" != "$window" ]; then
                echo "::error::the frame is ${frame:-?} but the window is ${window:-?}: the compositor stretches every frame to fit"
                exit 1
            fi
            # "Physical density: 420", or "Override density: ..." when set.
            density="$(adb shell wm density | tr -d '\r' | awk -F': ' '/density/ {d = $2} END {print d}')"
            if [ -z "$density" ] || [ -z "$scale" ] ||
               ! awk -v d="$density" -v s="$scale" 'BEGIN { e = d / 160; exit !(s - e < 0.002 && e - s < 0.002) }'; then
                echo "::error::the display is ${density:-?} dpi but the app drew at scale ${scale:-?} (expected density / 160)"
                exit 1
            fi
            echo "android-emulator-smoke: ${density} dpi, scale ${scale}, frame fills the ${window} window"
            # The demo's icons are SVG: one that libvips cannot read means the
            # APK's libvips was built without SVG support (packaging/vcpkg.json).
            svg_failures="$(adb logcat -d -s UltraCanvas:V | grep -c '\.svg.*is not a known file format' || true)"
            if [ "${svg_failures:-0}" -gt 0 ]; then
                echo "::error::$svg_failures SVG load(s) failed: libvips cannot read SVG (see logcat-ultracanvas.txt)"
                adb logcat -d -s UltraCanvas:V | grep -m3 '\.svg.*is not a known file format' || true
                exit 1
            fi
            # Tap and type: the demo logs its tree filter's place in logical
            # px ("Demo: tree filter at 4,40 232x30"); tap its middle in
            # physical px, then type a word and wait for the demo to see it.
            field="$(adb logcat -d -s UltraCanvas:V | grep -m1 'Demo: tree filter at' || true)"
            if [ -z "$field" ]; then
                echo "::error::the demo never said where its tree filter is (no 'Demo: tree filter at' line)"
                exit 1
            fi
            read -r fx fy fw fh < <(sed -n 's/.*Demo: tree filter at \(-\{0,1\}[0-9]*\),\(-\{0,1\}[0-9]*\) \([0-9]*\)x\([0-9]*\).*/\1 \2 \3 \4/p' <<<"$field")
            read -r tx ty < <(awk -v x="$fx" -v y="$fy" -v w="$fw" -v h="$fh" -v s="$scale" \
                'BEGIN { printf "%d %d\n", (x + w / 2) * s, (y + h / 2) * s }')
            if [ "$tx" -le 0 ] || [ "$ty" -le 0 ] || [ "$tx" -ge "${window%x*}" ] || [ "$ty" -ge "${window#*x}" ]; then
                echo "::error::the tree filter's middle ($tx,$ty px) is outside the ${window} window"
                exit 1
            fi
            adb shell input tap "$tx" "$ty"
            tap=""
            for ((waited = 0; waited < 20; waited += 1)); do
                tap="$(adb logcat -d -s UltraCanvas:V | grep -m1 'first tap:' || true)"
                [ -n "$tap" ] && break
                sleep 1
            done
            if [ -z "$tap" ]; then
                echo "::error::a tap at $tx,$ty px never reached the app"
                exit 1
            fi
            echo "android-emulator-smoke: ${tap#*: }"
            if ! grep -q "element 'CategoryTreeFilter'" <<<"$tap"; then
                echo "::error::the tap at $tx,$ty px (the tree filter's middle) landed elsewhere: ${tap#*: }"
                exit 1
            fi
            sleep 1   # let the field take focus before the keys arrive
            adb shell input text button
            typed=""
            for ((waited = 0; waited < 20; waited += 1)); do
                typed="$(adb logcat -d -s UltraCanvas:V | grep -m1 "Demo: tree filter 'button'" || true)"
                [ -n "$typed" ] && break
                sleep 1
            done
            if [ -z "$typed" ]; then
                echo "::error::typed 'button' into the tree filter, but the demo never saw that text"
                adb logcat -d -s UltraCanvas:V | grep "Demo: tree filter '" | tail -5 || true
                exit 1
            fi
            echo "android-emulator-smoke: typed 'button' into the tree filter"
            sleep 2   # the screenshot shows the filtered tree
            adb exec-out screencap -p > "$out/screenshot-typed.png" || true

            # Rotation in place: turn the display to landscape (auto-rotate off,
            # so nothing turns it back) and wait for a frame at the new size.
            # The keyboard that came up for the field may already have resized
            # the window: only frames logged after the turn count.
            adb exec-out screencap -p > "$out/screenshot-portrait.png" || true
            before="$(adb logcat -d -s UltraCanvas:V | grep -c 'resized frame presented' || true)"
            adb shell settings put system accelerometer_rotation 0
            adb shell settings put system user_rotation 1
            rotated=""
            for ((waited = 0; waited < 60; waited += 2)); do
                rotated="$(adb logcat -d -s UltraCanvas:V | grep 'resized frame presented' |
                           tail -n +"$((${before:-0} + 1))" |
                           awk '{ if (match($0, /presented: [0-9]+x[0-9]+/)) { split(substr($0, RSTART + 11, RLENGTH - 11), d, "x"); if (d[1] + 0 > d[2] + 0) { print; exit } } }' || true)"
                [ -n "$rotated" ] && break
                if [ -z "$(adb shell pidof "$package" | tr -d '\r')" ]; then
                    echo "::error::$package died after the display turned to landscape (see logcat-crash.txt)"
                    adb logcat -d -b crash | tail -40 || true
                    exit 1
                fi
                sleep 2
            done
            if [ -z "$rotated" ]; then
                echo "::error::no landscape frame within 60s of turning the display to landscape - the app kept its ${frame} frame, so the rotation was not seen as a resize"
                exit 1
            fi
            echo "android-emulator-smoke: ${rotated#*: }"
            rframe="$(sed -n 's/.*frame presented: \([0-9]*x[0-9]*\).*/\1/p' <<<"$rotated")"
            rwindow="$(sed -n 's/.*, window \([0-9]*x[0-9]*\).*/\1/p' <<<"$rotated")"
            if [ "$rframe" != "$rwindow" ]; then
                echo "::error::after rotating, the frame is ${rframe:-?} but the window is ${rwindow:-?}"
                exit 1
            fi
            if [ "${rframe%x*}" -le "${rframe#*x}" ]; then
                echo "::error::after turning the display to landscape the frame is ${rframe}, not landscape"
                exit 1
            fi
            if ! grep -q 'with content' <<<"$rotated"; then
                echo "::error::the first frame after rotating was blank"
                exit 1
            fi
            sleep 3   # the landscape screenshot shows the settled UI
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
