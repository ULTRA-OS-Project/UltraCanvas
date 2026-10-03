# The UltraCanvas SDK

**Version:** 1.0.0
**Last Modified:** 2026-10-03
**Author:** UltraCanvas Framework

The SDK is the framework **already built and installed**, zipped up: the
headers, the libraries, the file-format plug-ins and the CMake package that
`find_package(UltraCanvas)` reads. It exists so that an application outside
this repository can be built without first compiling the framework, and so
that UltraCanvasStart can set a machine up by unpacking one folder instead of
running a forty-minute build.

CI produces it on every pull request and every push to `main`, one per
platform leg, as a workflow artifact:

| Artifact | Contents |
|---|---|
| `UltraCanvas-SDK-Linux-<version>-x86_64`, `-arm64` | shared core, Ubuntu 22.04 ABI, with the vendored `libcurl.so.4` the core needs |
| `UltraCanvas-SDK-MacOS-<version>-arm64`, `-x86_64` | static core, built against Homebrew libraries |
| `UltraCanvas-SDK-Windows-<version>-x86_64`, `-arm64` | static core, MSYS2 CLANG64 / CLANGARM64 |

Each is the result of `cmake --install build --prefix <sdk>` for that leg,
plus a copy of this page, the licenses and the `PackageConsumer` example. The
version in the name is the framework's, from the first line of
`Docs/UltraCanvas/CHANGELOG.md`. Artifacts are kept for seven days; a release
copy is a matter of attaching the same file to a GitHub release.

## Layout

```
UltraCanvas-SDK-<platform>-<version>-<arch>/
  include/ultracanvas/          the headers, as a mirror of the source layout
    include/                    the public headers - this is the include path
    Plugins/                    the chart, diagram and document headers
    libspecific/  OS/           reached only through "../" from the public headers
  include/ultracanvas/plugins/  the format plug-ins' public headers
  lib/                          libUltraCanvas, the module archives, the plug-ins
  lib/cmake/UltraCanvas/        UltraCanvasConfig.cmake and the exported targets
  lib/cmake/VirtualFS/          VirtualFS, found through its own package
  lib/ultracanvas/              the UltraCanvasAllFormats registrar's objects
  examples/PackageConsumer/     the two-file application that uses all of it
  README.md  LICENSE  THIRD_PARTY_LICENSES.md
```

## Using it

```bash
tar xzf UltraCanvas-SDK-Linux-0.9.142-x86_64.tar.gz   # unzip on Windows
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

On Linux the shared core sits in `lib/`; run an application with that
directory on `LD_LIBRARY_PATH`, or set an rpath, or copy the `.so` files beside
the executable the way `package-linux.sh` does.

## What the SDK does not replace

The SDK saves building the framework. It does not carry a compiler, CMake, or
the **development packages of the libraries the framework uses**: the public
headers include `<cairo/cairo.h>`, `<glib.h>` and `<vips/vips8>`, and the
installed package re-finds cairo, pango, freetype, glib, tinyxml2 and libvips
through pkg-config on the consuming machine. So the host still needs, from
[`GettingStarted.md`](GettingStarted.md) step 1:

- a C++20 compiler and CMake ≥ 3.16;
- the `-dev` packages of the core libraries on Linux, the Homebrew formulae on
  macOS, the MSYS2 CLANG64 packages on Windows.

Bundling those headers and import libraries into the Windows and macOS SDKs,
where the dependency trees are relocatable, is the planned next step; on Linux
the distribution's packages stay the right source.

## Compatibility

An SDK is built on one platform leg and is for that platform and architecture
only. The Linux SDK follows Ubuntu 22.04's library ABI; a newer distribution
runs it, an older one may not. The macOS and Windows SDKs are static and carry
no runtime dependency beyond the libraries named above.

The package version in `UltraCanvasConfigVersion.cmake` is the framework's,
with `SameMajorVersion` compatibility, so `find_package(UltraCanvas 0.9)`
accepts any 0.x SDK.
