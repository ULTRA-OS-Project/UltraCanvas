- **A second Android app: ChatCube.** The Android build no longer
  packages the demo alone. `scripts/android-package-apk.sh` takes
  `--manifest` and `--assets` (a list of repository paths for `assets/`),
  so an app brings its own manifest and asset set; without them it packages
  the demo as before. `scripts/android-bootstrap-sysroot.sh --with-chatcube`
  builds the sysroot with the manifest's new `chatcube` feature - UltraNet's
  OpenSSL, c-ares and curl, plus SQLite, libsodium and Opus - and the
  Android build workflow has a `chatcube` job that builds `libChatCube.so`
  with UltraNet and UltraDatabase on and uploads `ChatCube-<abi>` APKs. The
  root CMake's Android defaults turn off the TDLib download and Matrix's
  Rust end-to-end encryption, which have no Android build.
