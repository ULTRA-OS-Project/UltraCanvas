# ChatCube for Android

**Status: built by CI (the `chatcube` job of
`.github/workflows/android-build.yml`), not yet run on a phone or an
emulator.** What is here is the build, the packaging and the phone layout;
treat the behaviour on a device as designed, not observed.

ChatCube for Android is the desktop ChatCube's code, built as the shared
library `libChatCube.so` that the framework's activity loads
(`UltraCanvas/OS/Android/README.md`). Nothing in `ui/` is Android-only
except two small `#if defined(__ANDROID__)` arms (the permissions and the
back key).

## What is different from the desktop

| | Desktop | Android |
|---|---|---|
| Entry point | `main()` | `ultracanvas_app_main()`, called by `android_main()` |
| Networks | modules in `Plugins/UltraIM`, loaded on demand | linked into the library, registered at start (`ChatCubeAndroidProviders.cpp`) |
| Telegram, Signal | yes | no: TDLib and signal-cli do not run there; their buttons are hidden |
| Matrix end-to-end encryption | yes, with cargo | no: vodozemac has no Android cross-build yet (`ULTRAIM_MATRIX_E2EE` is off); encrypted rooms say they cannot be read |
| Video calls | VP8 through FFmpeg | sound only: FFmpeg is not in the Android sysroot |
| Finding people on the local network | the mDNS plug-in | not yet: it is an UltraNet module, and none is loaded on Android |
| One copy at a time | `UltraCanvasSingleInstance` | the activity's `singleTask` launch mode |
| Microphone, camera | the system asks by itself | asked for at the first recording or call (`HaveMediaPermission`) |
| Layout | side by side, one column on a narrow window | one column (the compact layout, below 640 logical px wide) |

The compact layout is not Android's: any ChatCube window narrower than
640 logical pixels uses it, so it can be tried on the desktop by making the
window narrow.

## Building

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk      # r25 or later
scripts/android-bootstrap-sysroot.sh --with-chatcube x86_64
```

then the `cmake` line the script prints (it adds
`-DULTRACANVAS_ENABLE_NET=ON -DULTRACANVAS_ENABLE_DATABASE=ON`), and

```sh
cmake --build build-android --target ChatCube
scripts/android-package-apk.sh --abi x86_64 \
    --lib build-android/lib/libChatCube.so \
    --out ChatCube-x86_64.apk --ndk "$ANDROID_NDK_HOME" \
    --manifest Apps/ChatCube/android/AndroidManifest.xml \
    --assets Apps/ChatCube/android/assets.txt \
    --icon media/appicon/ChatCube.png \
    --version-name "$(head -1 Docs/ChatCube/CHANGELOG.md | grep -o '\*[0-9][0-9.]*\*' | tr -d '*')"
adb install ChatCube-x86_64.apk
```

`arm64-v8a` in place of `x86_64` for a phone. CI does all of this on every
pull request that touches `Apps/ChatCube/` or the Android build, and uploads
`ChatCube-arm64-v8a` and `ChatCube-x86_64` (debug-signed APKs).

## Files

- `AndroidManifest.xml` - package `org.ultraos.chatcube`, library `ChatCube`,
  the network, microphone and camera permissions.
- `assets.txt` - what goes into the APK's `assets/` (fonts, icons, the logo,
  the translations), unpacked to `$HOME/share` at the first start.
- `ChatCubeAndroidProviders.h/.cpp` - the providers linked in, registered
  with UltraIM.

## Known gaps

- The soft keyboard covers the bottom of the window: the framework's
  Android window does not yet shrink above it, so the message field is
  hidden while typing on a phone held upright.
- Messages arrive only while ChatCube is in the foreground: there is no
  background service or push notification yet.
- Not run on a device or an emulator; `scripts/android-emulator-smoke.sh`
  is the demo's (it taps the demo's tree filter) and has no ChatCube
  variant yet.
