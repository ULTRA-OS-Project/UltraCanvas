# The Size of a Standalone Application Package — Investigation

**Status:** Investigation, with one change made (the Linux archive is xz).
The question was whether *a core that loads its optional modules on demand*
would cut the standalone UltraCanvasStart archives to a fraction of their
size. Measured, it would not: on Linux the modules an application could do
without weigh about 15% of the package. What the package is made of, and
what does and does not reduce it, is below.
**Scope:** `scripts/package-ultracanvasstart.sh` (Linux, Windows),
`package-macos.sh --start-app` (macOS), and the shared core they ship.
**Last Modified:** 2026-10-09

## 1. What the packages weigh

The first release builds with the standalone packages (0.9.221, pull request
#761) produced:

| Platform | Download | Unpacked | Libraries | Of which the shared core |
|---|---:|---:|---:|---:|
| Linux x86_64 | 95.5 MB (`.tar.gz`) | 221 MB | 183 | `libUltraCanvas.so` 60.7 MB |
| Windows ARM64 | 73.2 MB (`.zip`) | 146 MB | 105 DLLs | |
| macOS x86_64 | 55 MB (`.dmg`, LZMA) | 120 MB | 71 dylibs | |

Each package is one executable of half a megabyte, the shared core, and the
closure of libraries the core links. The application itself is not the
weight; the core and its libraries are.

## 2. Where the Linux weight is

Measured on Ubuntu 24.04 (CI builds on 22.04: 171 libraries and 126 MB here
against 183 and 160 MB there, the same shape) by taking each library group
the core links, resolving its closure with `ldd` the way `package-linux.sh`
does (glibc, GL, X11 and udev left to the host), and attributing every
library to the groups whose closures contain it. "Exclusive" is what the
package loses when that group goes.

| Group the core links | Exclusive | Libraries | Its whole closure |
|---|---:|---:|---:|
| base: cairo, pango, harfbuzz, freetype, fontconfig, glib, rsvg, tinyxml2, fmt, libarchive, curl, sqlite, X11 extras, libstdc++ | 1.6 MB | 3 | 73.3 MB |
| GTK 3 (file dialogs, the print dialog) | 10.9 MB | 11 | 26.3 MB |
| libvips (image loading) and its delegates | 17.6 MB | 27 | 90.0 MB |
| audio (GStreamer, FLAC, Vorbis, Opus, LAME) | 5.5 MB | 14 | 10.8 MB |
| remote desktop (FreeRDP, UltraWin) | 6.1 MB | 11 | 64.9 MB |
| devices (CUPS, SANE, libusb, Avahi) | 0.1 MB | 1 | 48.7 MB |
| CDR (libcdr, librevenge, lcms2, ICU i18n) | 4.2 MB | 4 | 38.8 MB |
| PDF (MuPDF is static; its system libraries) | 0.5 MB | 2 | 1.3 MB |
| barcode (zbar) | 0.4 MB | 3 | 4.8 MB |
| spelling (hunspell) | 0.7 MB | 1 | 3.4 MB |
| UltraCrypt (libsodium), UltraDatabase (libpq), UltraNet DNS (c-ares) | 0.8 MB | 3 | |
| shared by two or more groups | 77.9 MB | 91 | |

The groups an application like UltraCanvasStart never uses - audio, remote
desktop, CDR, barcode, spelling, the three small modules - come to **18 MB
exclusive, 14% of the 126 MB**. Everything else is shared with the base, or
is the base.

The largest libraries, and who pulls them:

| Library | Size | Pulled by |
|---|---:|---|
| `libicudata.so` | 29.4 MB | libxml2, which librsvg, libarchive, CUPS, libvips and FreeRDP link |
| `libgtk-3.so` | 7.8 MB | GTK 3 |
| `librsvg-2.so` | 6.5 MB | the base (SVG icons) and libvips |
| `libcrypto.so` | 5.1 MB | curl, libpq, libvips, FreeRDP |
| `libhdf5_serial.so` | 3.4 MB | libvips (Matlab files) |
| `libicui18n.so` | 3.3 MB | CDR |
| `libOpenEXR.so` | 3.0 MB | libvips |
| `libstdc++.so` | 2.5 MB | everything |
| `libfftw3.so` | 2.1 MB | libvips |
| `libgnutls.so` | 2.0 MB | curl, libpq, CUPS, libvips, FreeRDP |

Three facts follow:

1. **The single largest item, ICU's data at 29 MB (23%), is not a module of
   ours.** Ubuntu's libxml2 links ICU, and libxml2 is reached from the base
   alone (librsvg, libarchive). No split of the core touches it.
2. **The second largest block, libvips' delegates (OpenEXR, HDF5, FITS,
   FFTW, Matlab, OpenSlide, Poppler, ImageMagick), is the distribution's
   choice, not ours.** Ubuntu's `libvips.so` links every delegate
   unconditionally. On macOS the delegates are chosen in
   `MacOS/deps/vcpkg.json`, and the comment there records that the suite
   wants them (UltraViewer reads those formats).
3. **The shared core itself is 60.7 MB on Linux**, the largest single file
   in the package. It carries the framework and, folded in whole, UltraNet,
   UltraWin, UltraCrypt, UltraVault, UltraDatabase, UltraMessage,
   NetworkMonitor, VirtualFS and the static MuPDF with its embedded fonts
   (`UltraCanvas/CMakeLists.txt`, *MODULE HOMES*). A shared core is a
   deliberate decision for the suite: one copy of each module per process.

## 3. What an application links when it links only what it uses

The same application built against a **static** core on this machine
(Ubuntu's linker defaults to `--as-needed`, so a library is a dependency
only when referenced code needs it):

| | Static core, this machine | Shared core, CI |
|---|---:|---:|
| Framework code in the package | 12.8 MB (the executable) | 60.7 MB (`libUltraCanvas.so`) |
| Libraries | 122 | 183 |
| Unpacked | 123 MB | 221 MB |
| Download, gzip | 48.9 MB | 95.5 MB |
| Download, xz | 35.3 MB | about 70 MB (estimated from the ratio) |

So "load only what you use" is worth about half the package - but it is
already available without a new mechanism: a static link of the one
application. What a static core costs is listed in `UltraCanvas/CMakeLists.txt`
(each application its own copy of the modules; on Linux the lcms2 clash that
crashes the PDF viewer, which this application does not have). A second,
static configure and build of the core on each CI leg would cost ten to
fifteen minutes per leg.

## 4. An on-demand module core, costed

The precedent is the LaTeX module: the core does not link it,
`CreateLaTeXView()` loads `libUltraCanvasLaTeX` with `dlopen` on first use
through a C ABI (`core/UltraCanvasLaTeXModuleLoader.cpp`,
`Plugins/LaTeX/UltraCanvasLaTeXModuleABI.h`), and the plug-in loaders share
the DSO helpers in `core/UltraCanvasPluginLoader.cpp`. Giving the same
treatment to the audio backend, UltraWin's FreeRDP tier, the CDR reader, the
OCR plug-in, zbar and hunspell would take each one's libraries out of the
closure of an application that never calls it.

What it would save on Linux: the 18 MB exclusive to those groups (§2), plus
the part of the core's own 60.7 MB that is those modules' code. That part is
not measurable from this machine's static build (its archive holds the core
proper, 396 objects and 23 MB of code; the modules are separate archives
here), and the two heaviest folded-in items, MuPDF with its fonts and
UltraNet, are not candidates: every suite application that shows a document
needs the one, and UltraCanvasStart itself needs the other for the SDK
download. The whole gain is therefore in the region of 20 to 30 MB of 221,
**roughly 15%**, for a change that touches every one of those modules'
call sites with a C ABI and a loader, and every application's packaging.
That is the wrong trade for the size alone. It stays worth doing for a
reason of its own - a suite whose optional modules are optional at run
time - and then the size is a by-product.

## 5. What does reduce the download

| Lever | Effect | Status |
|---|---|---|
| xz instead of gzip for the Linux archive | 49 MB to 35 MB on the same tree (28%); the libraries compress well, the core even better | **Done** in `scripts/package-ultracanvasstart.sh` 1.1.0. The macOS image was LZMA already; the Windows zip stays a zip, which Explorer opens. |
| xz for the Linux SDK archives | The same ratio on 70 MB | Not done: the SDK's `Download` button unpacks with `tar`, which reads xz on every supported platform, so it is a one-line change in `build.yml` when wanted. |
| A static core for the standalone application (§3) | About half the package | Not done: a second core build per Linux and macOS leg. The Windows leg already builds a static core when the LaTeX plug-in is on. |
| Trimming libvips' delegates on macOS (`MacOS/deps/vcpkg.json`) | A few dylibs and perhaps 10 MB of the 120 | Not done: the suite wants the formats, and the SDK bundles the same build. |
| A reduced ICU data file | Up to 25 MB of 221 on Linux | Not done, and not recommended: libxml2 needs ICU only for encoding conversion, but which converters a user's documents need is not ours to guess, and a libxml2 built without ICU is a distribution decision. |
| An on-demand module core (§4) | About 15% | Not done; costed above. |

## 6. Method

The attribution in §2 is `scripts`-free and reproducible: for each root
library of a group, `ldd` lists the transitive closure; a library belongs to
every group whose closure has it; sizes are the installed files'. The groups
are the `pkg_check_modules` calls of `UltraCanvas/CMakeLists.txt`. The CI
figures are the `Package sizes` tables in the build legs' job summaries and
the standalone packagers' own lines in the job logs.
