# macOS dependency libraries

The libraries the macOS apps link and `package-macos.sh` bundles into the
suite's shared `Frameworks/` — cairo, pango, glib, libvips, tesseract, MuPDF,
the audio codecs and the rest — built from source with
[vcpkg](https://vcpkg.io) for the **oldest macOS the apps support: 14.0
(Sonoma)**, on Apple Silicon and Intel alike.

## Why not Homebrew

A Mach-O binary records the oldest macOS it runs on, and dyld refuses to load
one built for a newer macOS than the running system - the app, or any library
it pulls in. Homebrew builds each bottle for the macOS of the machine that
built it, so apps bundling Homebrew's libraries started only on the build
machine's macOS and newer: macOS 26 for the arm64 build once GitHub moved
`macos-latest` there in July 2026. Homebrew cannot do better any more: 7.0
(September 2026) builds no bottles for macOS 14 or for Intel at all, and
GitHub retires its macOS 14 runner in November 2026. vcpkg builds every
library here for the deployment target in the triplets instead.

Homebrew still provides the build tools (`cmake`, `pkg-config`, `ccache`,
autotools, `nasm`), and remains the easy way to build locally - those apps run
on the macOS they were built on and newer.

## What is here

| Path | Contents |
|---|---|
| `vcpkg.json` | The libraries, with the features the apps rely on (libvips' file formats, harfbuzz's CoreText shaper, ...) |
| `vcpkg-configuration.json` | Points vcpkg at the overlays below; no registry, so the ports are those of the pinned vcpkg commit |
| `triplets/` | `arm64-osx-ultracanvas` and `x64-osx-ultracanvas`: shared libraries, release only, `VCPKG_OSX_DEPLOYMENT_TARGET 14.0` |
| `ports/libvips` | vcpkg's port with the features it lacks (FITS, MAT, OpenEXR, JPEG 2000, RAW, FFTW, highway, dzsave archives, and the built-in GIF/PPM/Radiance/Analyze loaders, which vcpkg's portfile turns off unless named) |
| `ports/mupdf` | MuPDF, not in vcpkg: a shared libmupdf against vcpkg's freetype, harfbuzz and friends, as Homebrew built it, with its lcms2mt fork's `cms*` symbols kept out of the library's exports |
| `ports/zbar` | zbar, not in vcpkg: the library only |
| `ports/librevenge` | librevenge, not in vcpkg: needed by the vendored libcdr (CorelDRAW plug-in) |

The vcpkg commit is pinned in `scripts/macos-deps.sh` (`VCPKG_COMMIT`); every
library's version is the one at that commit.

## Building

```bash
brew install cmake pkg-config ccache autoconf autoconf-archive automake libtool nasm
prefix=$(scripts/macos-deps.sh)          # builds for this Mac's architecture
cmake -B build -DULTRACANVAS_MACOS_DEPS_PREFIX="$prefix" ...
cmake --build build
UC_MACOS_DEPS_PREFIX="$prefix" MACOSX_DEPLOYMENT_TARGET=14.0 ./package-macos.sh --no-sign
```

The first build compiles every library and takes an hour or more; vcpkg's
binary cache (`~/.cache/vcpkg/archives` locally, `.vcpkg/binary-cache` in CI,
restored and saved between runs) makes later ones take a minute or two.
`cmake/UltraCanvasMacOSDeps.cmake` is what makes CMake use the prefix instead
of `brew --prefix`; `package-macos.sh` bundles from it and checks that every
binary in the suite runs on `MACOSX_DEPLOYMENT_TARGET`.

## Changing things

- **A new library** the apps link: add it to `vcpkg.json` (with the features
  the code needs). If vcpkg has no port, add one under `ports/` and say why in
  its portfile. Record the library in `Docs/Dependencies.md`,
  `master_dependencies.yaml` and `THIRD_PARTY_LICENSES.md` as for any new
  dependency.
- **The oldest macOS**: change `VCPKG_OSX_DEPLOYMENT_TARGET` in both triplets
  and `MACOSX_DEPLOYMENT_TARGET` in `.github/workflows/build.yml` together -
  `scripts/macos-deps.sh` refuses to run when they disagree.
- **Newer library versions**: move `VCPKG_COMMIT`, and check whether vcpkg now
  has what the overlays add (drop the overlay when it does).
