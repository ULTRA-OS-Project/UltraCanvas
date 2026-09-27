- **CorelDRAW (`.cdr`) import on Windows.** The CDR plugin was off in every
  Windows build: the top-level CMake skipped the pkg-config checks with
  `if(NOT WIN32)`, and CI passed `-DULTRACANVAS_PLUGIN_CDR=OFF`. MSYS2
  packages everything the plugin needs, so the Windows builds (CLANG64 and
  CLANGARM64) now install librevenge, lcms2, ICU and Boost (plus libcdr as
  the fallback), find them through MSYS2's pkgconf, and build the patched
  libcdr from `third_party/libcdr`. `package-win.sh` already copies every
  MinGW DLL a packaged binary imports, so librevenge, lcms2 and ICU ship
  with it.
  - CI now fails if the CDR plugin, or its vendored libcdr, is not enabled
    on any platform. That check found macOS silently without CDR import:
    Homebrew's ICU is keg-only, and the top-level gate ran pkg-config before
    the CDR subdirectory added ICU's pkgconfig dir, so `libcdr-0.1` (which
    requires `icu-i18n`) and the ICU check both failed. The gate now adds it
    first.
  - The macOS and Windows jobs run `VectorFormatsPluginTest` (`detailed.cdr`
    with its masked bitmaps and PowerClips) and `CDRWriterTest`. Those rows
    build no full test suite (`BUILD_TESTS` is Linux-only), so the new
    `ULTRACANVAS_BUILD_VECTOR_FORMAT_TESTS` option builds just these two;
    their definitions moved to `Tests/VectorFormatsTests.cmake`, which
    `Tests/CMakeLists.txt` includes as before.
- **Windows: one graphics plugin registry per process.** The registry's
  storage (`Plugins()`, `ExtensionMap()`, `Initialized()`) sat in inline
  functions in `UltraCanvasGraphicsPluginSystem.h`. On Windows each module
  gets its own copy of an inline function's statics. The core is a DLL there
  and the apps link the format plugins into the executable, so plugins
  registered into the executable's copy, while the core's own readers saw an
  empty one. Those readers include the supported-format inventory (file
  dialogs, the Filer's classification) and the vector previews. The storage
  is defined in `core/UltraCanvasGraphicsPluginSystem.cpp` again: still
  built on first use, and now one copy for every module.
  `VectorFormatsPluginTest`'s inventory checks, which now run on Windows,
  found it.
