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
