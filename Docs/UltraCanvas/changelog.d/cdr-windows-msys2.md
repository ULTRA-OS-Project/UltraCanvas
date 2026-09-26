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
  - The Windows jobs run `VectorFormatsPluginTest` (`detailed.cdr` with its
    masked bitmaps and PowerClips) and `CDRWriterTest`.
