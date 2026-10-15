- **libvips is part of UltraCanvas on every platform.** The
  `ULTRACANVAS_ENABLE_VIPS` switch is gone:
  - Configuring with `-DULTRACANVAS_ENABLE_VIPS=OFF` now stops with a message
    saying where each platform gets libvips.
  - The one build without libvips is a single-threaded WebAssembly program
    (`-DULTRACANVAS_WASM_THREADS=OFF`). libvips runs every image on a worker
    thread, which such a program cannot start. That build keeps the
    stb_image and cairo fallbacks.
- **Android builds libvips.** The Android defaults no longer turn libvips
  off.
  - `OS/Android/packaging/vcpkg.json` lists libvips with JPEG, PNG, WebP,
    TIFF, EXIF, lcms, GIF and Netpbm. It comes from the macOS overlay port,
    which names the built-in GIF and PPM loaders that vcpkg's own port
    leaves out.
  - `scripts/android-bootstrap-sysroot.sh` checks out vcpkg at the commit
    `scripts/macos-deps.sh` pins when it is given no vcpkg.
  - The script prints a configure line with the NDK's own toolchain, the
    sysroot as CMake's find root and static pkg-config link lists. vcpkg's
    toolchain would have installed the repository's root `vcpkg.json`, the
    Windows set, for Android.
- **New `android-build.yml` workflow.** It builds that sysroot for
  arm64-android with the runner's NDK, keeps the libraries in vcpkg's binary
  cache, and compiles the core library against it.
- **CI changes.** The Build workflow's Android configure test installs
  libvips. The WebAssembly workflow's `no-vips` variant is gone.
