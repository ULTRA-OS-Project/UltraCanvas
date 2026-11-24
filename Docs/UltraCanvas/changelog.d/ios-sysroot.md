- **iOS phase 1: the dependency stack builds for iOS, in CI.**
  `scripts/ios-bootstrap-sysroot.sh [--with-net] [simulator|device]` builds
  cairo, pango, glib, harfbuzz, fontconfig, freetype, tinyxml2, fmt and
  libvips as static libraries. It uses vcpkg at the commit macOS and Android
  pin, the manifest and triplets in `UltraCanvas/OS/iOS/packaging/`
  (`arm64-ios-simulator-ultracanvas`, `arm64-ios-ultracanvas`, iOS 16.0) and
  the macOS libvips overlay. `scripts/ios-check-sysroot.sh` checks that every
  archive's objects name the right platform (simulator 7, device 2), that no
  `.dylib` is present, that the `.pc` files the core asks for are there, and
  that cairo was built without its Quartz backend.
  `.github/workflows/ios-build.yml` runs both triplets on `macos-15` with a
  vcpkg binary cache. `cmake/UltraCanvasMacOSDeps.cmake` no longer asks
  Homebrew for an iOS, tvOS, visionOS or watchOS configure. There is still no
  backend: the core and DemoApp are phase 2 of
  `Docs/UltraCanvas/iOSPortInvestigation.md`.
