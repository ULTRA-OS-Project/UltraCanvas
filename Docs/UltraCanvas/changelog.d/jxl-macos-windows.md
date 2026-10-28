- **JPEG XL on macOS and Windows.** Only a Linux build on a distribution
  whose libvips has libjxl could read or write `.jxl`; neither package could,
  so the apps offered no JPEG XL there and DemoApp's performance page left
  its JXL column out.
  - macOS: the libvips overlay port (`MacOS/deps/ports/libvips`) gains a
    `jpeg-xl` feature on vcpkg's `libjxl`, and `MacOS/deps/vcpkg.json` selects
    it - the port builds every libvips option it is not given as disabled.
    The bundle picks up libjxl, brotli and Highway by its `otool` walk.
  - Windows: MSYS2's libvips has JPEG XL only through its `vips-jxl` module,
    which loads only with libjxl installed. CI now installs `libjxl`, and
    `package-win.sh` ships `vips-jxl.dll` beside `vips-heif` and
    `vips-magick`; its DLL walk brings libjxl and its dependencies.
  - `ImageJxlTest` now runs on the macOS and Windows CI rows too (the new
    `ULTRACANVAS_BUILD_JXL_TESTS` builds it without `BUILD_TESTS`), and the
    step fails unless its checks ran rather than skipped.
  - `Docs/Dependencies.md` lists libjxl and Highway.
