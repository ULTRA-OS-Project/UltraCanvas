- **The Windows and macOS SDKs carry the development files of the libraries
  the framework uses.** `scripts/sdk-bundle-deps.sh` adds a `deps/` tree
  (headers, import libraries or dylibs, static archives, relocatable `.pc`
  and CMake config files, and on Windows the run-time DLLs) for the
  pkg-config closure of cairo, pango, harfbuzz, freetype, glib, tinyxml2 and
  libvips, plus fmt, libcurl and zlib where the build used the system ones.
  `UltraCanvasConfig.cmake` puts `deps/` first on `CMAKE_PREFIX_PATH`, remaps
  exported libraries found by full path on the build machine to their bundled
  copies, and on macOS links consumers with an rpath to `deps/lib`, where the
  bundled dylibs have `@rpath` install names. `ULTRACANVAS_DEPS_DIR` names the
  directory. CI builds `Tests/PackageConsumer` against the bundled files alone
  on both platforms (`Docs/UltraCanvasSDK.md`).
