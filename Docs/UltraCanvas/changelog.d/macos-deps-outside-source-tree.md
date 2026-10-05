- **macOS builds configure again: the vcpkg libraries install outside the
  checkout.** Since the switch to vcpkg-built libraries, `scripts/macos-deps.sh`
  installed them into `.vcpkg/installed` inside the source tree. The
  UltraCanvas package exports the prefix's `lib` directory, and CMake refuses
  to generate an `install(EXPORT)` whose link directories lie inside the source
  or build tree, so both macOS CI legs (and a local build following
  `MacOS/deps/README.md`) failed at *Configure CMake* with one "…which is
  prefixed in the source directory" error per library. This was not a macOS 15
  compatibility problem; the arm64 and Intel legs failed identically before
  anything compiled. The default install root is now
  `~/.cache/ultracanvas/vcpkg-installed` (`$XDG_CACHE_HOME` is honoured, and
  `UC_DEPS_INSTALL_ROOT` still overrides it). The script refuses an install
  root inside the checkout before building anything, and
  `cmake/UltraCanvasMacOSDeps.cmake` stops at configure with one clear message
  when `ULTRACANVAS_MACOS_DEPS_PREFIX` lies in the source or build tree. vcpkg's
  checkout and binary cache stay in `.vcpkg/`; the cached archives do not
  depend on where they are installed, so CI reuses them.
- **macOS packaging: libmupdf no longer links the build machine's OpenSSL.**
  With the configure step fixed, the Intel leg got as far as
  `package-macos.sh` for the first time since the switch to vcpkg, and its
  suite check refused the bundle: "libmupdf.dylib still loads
  /usr/local/opt/openssl@3/lib/libcrypto.3.dylib from the build machine".
  MuPDF's Makerules asks pkg-config for libcrypto (PDF digital signatures) on
  macOS, and the port only prepended vcpkg's directories to `PKG_CONFIG_PATH`,
  so pkg-config still found Homebrew's OpenSSL. The apps do not use MuPDF's
  signature support: the port now builds with `HAVE_LIBCRYPTO=no` and confines
  pkg-config to vcpkg's prefix with `PKG_CONFIG_LIBDIR` (port-version 1).
