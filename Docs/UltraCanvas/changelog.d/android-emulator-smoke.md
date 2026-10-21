- **CI runs the Android demo on an emulator.** A new job in
  `android-build.yml` installs the `UltraCanvasDemo-x86_64` APK on an API 30
  x86_64 emulator (KVM on the runner), starts it, and passes only when the app
  presents a first frame with something drawn in it -
  `scripts/android-emulator-smoke.sh`, which also runs against a local
  emulator. It fails when the app dies first, draws a frame of one colour
  everywhere, or shows nothing in four minutes, and uploads the screenshot and
  logcat as `android-emulator-smoke` whatever the outcome. The window backend
  logs `first frame presented: WxH, with content` once per process, straight
  to logcat, for it to wait on.
- **Android APKs now carry `libc++_shared.so`; before, the app could not
  start.** The emulator's first run stopped in `UltraCanvasActivity.onCreate`
  with `dlopen failed: library "libc++_shared.so" not found`: the demo links
  the NDK's shared C++ runtime, which no device provides. The APKs CI built
  since the packaging landed are all affected. `android-package-apk.sh` now
  reads the library's `NEEDED` entries, ships what the NDK provides, leaves
  API 28 system libraries to the device, and stops on anything else.
