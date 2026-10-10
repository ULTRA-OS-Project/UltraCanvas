# The UltraCanvas SDK

**Version:** 1.3.0
**Last Modified:** 2026-10-09
**Author:** UltraCanvas Framework

The SDK is the framework **already built and installed**, zipped up: the
headers, the libraries, the file-format plug-ins and the CMake package that
`find_package(UltraCanvas)` reads. It exists so that an application outside
this repository can be built without first compiling the framework, and so
that UltraCanvasStart can set a machine up by unpacking one folder instead of
running a forty-minute build.

CI produces it on every pull request and every push to `main`, one per
platform leg, as a workflow artifact, and every release build of `main`
attaches the same six archives to a GitHub release:

| Artifact | Contents |
|---|---|
| `UltraCanvas-SDK-Linux-<version>-x86_64`, `-arm64` | shared core, Ubuntu 22.04 ABI, with the vendored `libcurl.so.4` the core needs |
| `UltraCanvas-SDK-MacOS-<version>-arm64`, `-x86_64` | shared core (`lib/libUltraCanvas.dylib`), built against the vcpkg libraries CI makes for the oldest supported macOS, with those libraries' development files in `deps/` |
| `UltraCanvas-SDK-Windows-<version>-x86_64`, `-arm64` | shared core (`bin/libUltraCanvas.dll`), MSYS2 CLANG64 / CLANGARM64, with the MSYS2 packages' development files and DLLs in `deps/` |

Each is the result of `cmake --install build --prefix <sdk>` for that leg,
plus a copy of this page, the licenses and the `PackageConsumer` example,
as a `.tar.xz` on Linux, a `.tar.gz` on macOS and a `.zip` on Windows. The
version in the name is the framework's, from the first line of
`Docs/UltraCanvas/CHANGELOG.md`.

**Where to get one.** Each version of `main` has a GitHub release tagged
`v<version>` (the `publish-sdk` job of `.github/workflows/build.yml` creates
it from the release build that `changelog-fold.yml` dispatches, with the
version's changelog section as its notes), and the archives are its assets at
a fixed address:

```
https://github.com/ULTRA-OS-Project/UltraCanvas/releases/download/v<version>/<archive>
https://github.com/ULTRA-OS-Project/UltraCanvas/releases/download/v0.9.211/UltraCanvas-SDK-Windows-0.9.211-x86_64.zip
```

That is what UltraCanvasStart's Project page fetches with its *Download*
button, for the platform and architecture it runs on and the version it was
built from. UltraCanvasStart itself is on the same release, on its own, as
`UltraCanvasStart-<OS>-<version>-<arch>` (a `.tar.xz` on Linux, a `.zip` on
Windows, a signed and notarized `.dmg` on macOS): the application, the
libraries it loads and nothing else, for a computer that has neither the
toolchain nor a clone yet (`Apps/UltraCanvasStart/README.md`). The workflow
artifacts (seven days, a signed-in browser) remain for pull-request builds,
and for the minutes between a merge and the end of its release build.

## Layout

```
UltraCanvas-SDK-<platform>-<version>-<arch>/
  include/ultracanvas/          the headers, as a mirror of the source layout
    include/                    the public headers - this is the include path
    Plugins/                    the chart, diagram and document headers
    libspecific/  OS/           reached only through "../" from the public headers
  include/ultracanvas/plugins/  the format plug-ins' public headers
  lib/                          libUltraCanvas, the module archives, the plug-ins
  bin/                          on Windows: libUltraCanvas.dll (the import library is in lib/)
  deps/                         Windows and macOS: the libraries the framework uses
    include/                    cairo, pango, harfbuzz, freetype, glib, libvips, tinyxml2, ... headers
    lib/                        import libraries (.dll.a) or dylibs, static archives, lib/pkgconfig/*.pc, lib/cmake/*
    bin/                        Windows: the DLLs the core and those libraries need at run time
  lib/cmake/UltraCanvas/        UltraCanvasConfig.cmake and the exported targets
  lib/cmake/VirtualFS/          VirtualFS, found through its own package
  lib/ultracanvas/              the UltraCanvasAllFormats registrar's objects
  examples/PackageConsumer/     the two-file application that uses all of it
  README.md  LICENSE  THIRD_PARTY_LICENSES.md
```

## Using it

```bash
tar xf UltraCanvas-SDK-Linux-0.9.142-x86_64.tar.xz   # .tar.gz on macOS, unzip on Windows
cmake -S MyApp -B build -DCMAKE_PREFIX_PATH=$PWD/UltraCanvas-SDK-Linux-0.9.142-x86_64
cmake --build build
```

and in `MyApp/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)
find_package(UltraCanvas CONFIG REQUIRED)
add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE UltraCanvas::UltraCanvas
                                    ${ULTRACANVAS_PLUGIN_TARGETS}
                                    UltraCanvas::UltraCanvasAllFormats)
```

`examples/PackageConsumer` inside the SDK is exactly this, with a `main.cpp`
that opens a window and registers the formats. Copy it, rename it, start
there. The imported targets and variables the package provides are listed at
the top of `UltraCanvasConfig.cmake`.

The core is a shared library on every platform, and the modules (UltraNet,
UltraDatabase, UltraCrypt, UltraVault, UltraMessage, VirtualFS, ...) are
inside it: `UltraCanvas::UltraDatabase` and the other module targets resolve
to the core, which exports their whole API, so an application links nothing
else. On Linux the shared core sits in `lib/`; run an application with that
directory on `LD_LIBRARY_PATH`, or set an rpath, or copy the `.so` files beside
the executable the way `package-linux.sh` does. On macOS it is
`lib/libUltraCanvas.dylib` with an `@rpath` install name: the application
needs a run path to that directory (CMake writes one into a build-tree
executable), or the dylib copied beside it the way `package-macos.sh` does
into the suite's `Frameworks/`. On Windows the core is
`bin/libUltraCanvas.dll`: put that directory on `PATH`, or copy the DLL beside
the executable, which is what `package-win.sh` does for a release.

## What the SDK does not replace

The SDK saves building the framework. It does not carry a compiler, CMake or
pkg-config: the host still needs, from [`GettingStarted.md`](GettingStarted.md)
step 1, a C++20 compiler, CMake ≥ 3.16 and pkg-config - on Windows that is
MSYS2's CLANG64 (or CLANGARM64) toolchain, which the SDK's headers and import
libraries are built for.

The **development files of the libraries the framework uses** are a platform
matter. The public headers include `<cairo/cairo.h>`, `<glib.h>` and
`<vips/vips8>`, and the installed package re-finds cairo, pango, freetype,
glib, tinyxml2 and libvips through pkg-config:

- **Windows and macOS SDKs carry them**, in `deps/`: `scripts/sdk-bundle-deps.sh`
  follows the pkg-config closure of those modules (plus fontconfig, fmt,
  libcurl and zlib where the build used the system ones). A package the
  closure reaches through `Requires` - one a consumer includes and links -
  comes whole: headers, import libraries or dylibs, `.pc` and CMake config
  files. A package reached only through `Requires.private` contributes its
  `.pc` files alone, because pkg-config refuses a module whose private
  requirement it cannot find, while nothing of such a package is included or
  linked by a shared build. A static archive is left out where the same
  library exists as a DLL or dylib beside it. The DLLs the core needs at run
  time are walked from its import table on Windows, private packages'
  included, so `deps/bin` runs the result. The `.pc` files are
  relocatable (`prefix=${pcfiledir}/../..`), and `UltraCanvasConfig.cmake`
  puts `deps/` first on `CMAKE_PREFIX_PATH`, which is all FindPkgConfig and
  `find_dependency()` need. A consumer therefore builds with no MSYS2 or
  Homebrew packages installed; CI proves it by building `PackageConsumer`
  with `PKG_CONFIG_LIBDIR` pointed at an empty directory. The libraries
  macOS itself provides (zlib, expat, libxml2, libarchive, libcurl) come
  as Homebrew's stub `.pc` files pointing at the system SDK. On macOS the
  bundled dylibs carry `@rpath` install names and the package links consumers
  with an rpath to `deps/lib`, so the application runs on a Mac without
  Homebrew; `package-macos.sh` still re-bundles them into the app for a
  release. On Windows put `deps/bin` on `PATH` to run, or copy the DLLs beside
  the executable as `package-win.sh` does. `ULTRACANVAS_DEPS_DIR` names the
  directory, or is empty.
- **The Linux SDK does not**: the distribution's `-dev` packages are the
  right source there (Ubuntu 22.04's for the SDK's ABI), as step 1 lists them.

## Compatibility

An SDK is built on one platform leg and is for that platform and architecture
only. The Linux SDK follows Ubuntu 22.04's library ABI; a newer distribution
runs it, an older one may not. The Windows SDK's DLL expects the MSYS2 runtime
DLLs it was built against (`libc++.dll`, cairo, pango, vips and the rest) on
`PATH`, as any MSYS2-built program does. The macOS SDK is static and carries no
runtime dependency beyond the libraries named above.

The package version in `UltraCanvasConfigVersion.cmake` is the framework's,
with `SameMajorVersion` compatibility, so `find_package(UltraCanvas 0.9)`
accepts any 0.x SDK.
