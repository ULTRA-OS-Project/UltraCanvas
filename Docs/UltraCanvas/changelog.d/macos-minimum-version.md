- **The macOS apps run on macOS 14 (Sonoma) and later, on Apple Silicon and
  Intel; CI published apps that started only on the macOS they were built on
  - for Apple Silicon, macOS 26.** The arm64 leg ran on `macos-latest`, which
  GitHub moved to macOS 26 in July 2026; nothing set a deployment target, so
  clang compiled our code for the runner's SDK, and the bundled Homebrew
  libraries carry the macOS of the machine their bottle was built on. dyld
  refuses a binary built for a newer macOS than the running one, so on macOS
  14 or 15 the apps did not start, while their `Info.plist` still said
  `LSMinimumSystemVersion` 12.0.
  - The libraries the apps bundle are built from source with vcpkg for macOS
    14.0 (`MacOS/deps/`, `scripts/macos-deps.sh`, `MacOS/deps/README.md`)
    instead of coming from Homebrew, which since 7.0 (September 2026) builds
    no bottles for macOS 14 or for Intel. vcpkg lacks MuPDF, zbar and
    librevenge, so they have ports in `MacOS/deps/ports`, as does libvips for
    the formats Homebrew's linked in (FITS, MAT, OpenEXR, JPEG 2000, RAW, ...)
    and the built-in loaders vcpkg's port turns off. vcpkg's binary cache is
    kept between CI runs. Homebrew still provides the build tools.
  - Our code is compiled for `MACOSX_DEPLOYMENT_TARGET=14.0`, the arm64 leg
    runs on `macos-15`, and `cmake/UltraCanvasMacOSDeps.cmake` replaces every
    `brew --prefix` in the CMake files, so the build takes its libraries from
    `ULTRACANVAS_MACOS_DEPS_PREFIX` (Homebrew's by default, for local builds).
    UltraNet stays on Apple's libcurl although vcpkg's tesseract brings its own.
  - `package-macos.sh` reads the minimum macOS from every executable, plug-in
    and dylib in the suite - the shared `Frameworks/` once, then each app's
    own binaries - writes it into each app as `LSMinimumSystemVersion`
    instead of a fixed 12.0, and fails when a binary needs a newer macOS than
    `MACOSX_DEPLOYMENT_TARGET`, naming each one. The job summary's suite table
    gains a *Needs macOS* column. Its first run caught Homebrew's arm64
    tesseract bottle declaring macOS 15.7.5.
  - Two packaging bugs fixed on the way: the install-name rewrite no longer
    repoints a system library (`/usr/lib`, `/System`) at a bundled one with
    the same file name - Homebrew's bundle already swapped the apps' Apple
    libcurl for Homebrew's that way - and run paths into the build machine's
    library prefix are deleted from the shipped binaries. MuPDF's library no
    longer exports its lcms2mt fork's `cms*` functions, which share lcms2's
    names with other signatures.
  - Dropped: three optional libvips helpers vcpkg does not have - `cgif` (the
    apps write GIF themselves when `gifsave` is missing), `libimagequant` and
    `libultrahdr` - and MuPDF's own OCR, which the apps do not use.
