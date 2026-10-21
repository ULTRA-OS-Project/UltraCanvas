- **CI builds an installable Android APK of the demo, for phones and for an
  emulator.** `scripts/android-package-apk.sh` packages an app library with
  the SDK's build tools alone - `UltraCanvasActivity` compiled to dex, the
  manifest and assets linked by aapt2, the library stripped and stored,
  zipaligned and signed with a debug key made for the run - so it needs
  nothing a GitHub runner lacks, and no Gradle. The assets are the set the
  demo's WebAssembly page preloads, laid out as `$HOME/share` expects.
  `android-build.yml` now builds `arm64-v8a` (phones) and `x86_64` (an
  emulator on a Linux host or runner, at full speed) side by side and uploads
  `UltraCanvasDemo-arm64-v8a` and `UltraCanvasDemo-x86_64` as run artifacts.
  Nothing has installed one yet; that is the next step.
