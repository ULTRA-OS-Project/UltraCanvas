# UltraCanvas Start and the SDK packages

**Version:** 1.1.0
**Last Modified:** 2026-11-26
**Author:** UltraCanvas Framework

UltraCanvas is a C++20 framework for desktop applications on Linux, Windows
and macOS. To write an application with it you need two things: a computer
with a compiler and a few libraries, and the framework itself. **UltraCanvas
Start** takes care of the first, and the **SDK packages** are the second,
ready to use. Together they take a computer that has nothing from a fresh
download to a first application that builds, without compiling the framework
and without reading a page of setup instructions.

## UltraCanvas Start

UltraCanvas Start is a small application that sets a computer up for
UltraCanvas development. You download it, open it, answer a few questions,
and it checks, installs and writes everything else. It walks you through six
steps:

1. **Your computer** - shows what it found: operating system, architecture,
   package manager, git, the AI coding assistants installed, and the way in
   for your platform.
2. **Features** - choose what your application will use (the core is
   always included; CDR, PDF, OCR, vectorising, audio, barcodes and
   networking are optional) and which AI assistant you work with, if any:
   Claude Code, Codex, Copilot, Gemini or another.
3. **Tools** - checks every compiler, tool and library those choices need,
   one row each, and installs what is missing with your system's package
   manager (`apt`, `dnf`, `pacman`, `zypper`, Homebrew, or MSYS2 on Windows,
   which it sets up itself when it is not there yet). You see the exact
   command before it runs and its output while it runs.
4. **Framework** - downloads the SDK package for your platform, verifies it
   against the published checksums and unpacks it. You can also point it at
   an archive you downloaded yourself, or choose to build the framework from
   a clone of the source instead.
5. **Project** - writes a new application: `CMakeLists.txt`, `main.cpp`,
   `CMakePresets.json`, a `README.md`, and the instruction file for your AI
   assistant.
6. **Build** - builds that application, to prove the installation works,
   and hands over the first prompt for your assistant.

Every step can be revisited at any time, and a report of the whole setup can
be copied for support.

### Updating to a newer SDK

Each UltraCanvas Start downloads the SDK of its own release, and also asks
the server for the newest one. When a newer SDK is out, the Framework step
says so and offers **Download** for it, with a link to the UltraCanvas Start
of that release, whose checks know what the new SDK needs. **Move a
project...** then points an existing application at the new SDK, so its next
build uses it. From a terminal: `UltraCanvasStart --update-check`,
`--update-sdk <folder>` and `--update-project <folder> --sdk <prefix>`. UltraCanvas Start also runs without a window -
`UltraCanvasStart --check`, `--plan` and `--create` work over ssh and in
scripts.

### Getting UltraCanvas Start

UltraCanvas Start is published with every framework release, on its own, so
it can reach a computer that has nothing installed yet:

| Platform | Download |
|---|---|
| Linux (x86_64, arm64) | `UltraCanvasStart-Linux-<version>-<arch>.tar.xz` |
| Windows (x86_64, arm64) | `UltraCanvasStart-Windows-<version>-<arch>.zip` |
| macOS (Apple silicon, Intel) | `UltraCanvasStart-MacOS-<version>-<arch>.dmg` - signed and notarized |

The downloads are on the release page:

```
https://github.com/ULTRA-OS-Project/UltraCanvas/releases/tag/v<version>
```

On Windows the executable is not code-signed yet, so SmartScreen asks once
before the first start.

## The SDK packages

An SDK package is the UltraCanvas framework **already built**: the headers,
the shared core library, the file-format plug-ins and the CMake package that
`find_package(UltraCanvas)` reads, packed into one archive. Unpack it, point
CMake at it, and your application builds against it - no forty-minute
framework build first.

| Package | Built for |
|---|---|
| `UltraCanvas-SDK-Linux-<version>-x86_64.tar.xz`, `-arm64` | Linux, Ubuntu 22.04 library ABI and newer |
| `UltraCanvas-SDK-Windows-<version>-x86_64.zip`, `-arm64` | Windows with the MSYS2 CLANG64 / CLANGARM64 toolchain |
| `UltraCanvas-SDK-MacOS-<version>-arm64.tar.gz`, `-x86_64` | macOS, Apple silicon and Intel |

Linux x86_64 and arm64, Windows x86_64 and macOS arm64 are in every release;
Windows arm64 is in nearly every release and macOS Intel only in some.

Each package contains:

- `include/` - the framework's headers;
- `lib/` - the shared core (`libUltraCanvas`) with every module inside it,
  the plug-ins, and the CMake package;
- `deps/` (Windows and macOS) - the libraries the framework uses (cairo,
  pango, glib, freetype, libvips and the rest), so no Homebrew or MSYS2
  packages are needed to build against it; on Linux the distribution's
  development packages take that role, and UltraCanvas Start installs them;
- `examples/PackageConsumer/` - a two-file application that opens a window
  and loads every file format: copy it, rename it, start there;
- this framework's licenses and its SDK documentation.

### Getting an SDK package

Every release is published on the UltraOS server, with a `SHA256SUMS` file
to verify the download, and on the GitHub release:

```
https://start.ultraos.eu/pub/<version>/<archive>
https://start.ultraos.eu/pub/<version>/SHA256SUMS
https://start.ultraos.eu/pub/LATEST          (the newest version, one line)
https://github.com/ULTRA-OS-Project/UltraCanvas/releases/download/v<version>/<archive>
```

UltraCanvas Start's Framework step picks the right archive for your platform
and version, downloads, verifies and unpacks it for you.

### Using it by hand

```bash
tar xf UltraCanvas-SDK-Linux-<version>-x86_64.tar.xz
cmake -S MyApp -B build -DCMAKE_PREFIX_PATH=$PWD/UltraCanvas-SDK-Linux-<version>-x86_64
cmake --build build
```

with this in `MyApp/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)
find_package(UltraCanvas CONFIG REQUIRED)
add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE UltraCanvas::UltraCanvas
                                    ${ULTRACANVAS_PLUGIN_TARGETS}
                                    UltraCanvas::UltraCanvasAllFormats)
```

### What you still need

The SDK saves building the framework; it does not bring a compiler. The
computer still needs a C++20 compiler, CMake 3.16 or newer and pkg-config -
on Windows, MSYS2's CLANG64 (or CLANGARM64) toolchain. UltraCanvas Start's
Tools step checks and installs exactly these.

## Versions and quality

UltraCanvas Start, the SDK packages and the release share one version
number: the SDK for `v0.9.604` is `UltraCanvas-SDK-…-0.9.604-…`, and the
UltraCanvas Start of that release downloads it. Every release is tested
before it is published: each UltraCanvas Start archive is started once
after packing, and on Linux a clean Ubuntu 22.04 container runs UltraCanvas
Start's install, then builds its project skeleton and the `PackageConsumer`
example against the very SDK that is published.

## Further reading

- [`Apps/UltraCanvasStart/README.md`](../../Apps/UltraCanvasStart/README.md) - UltraCanvas Start in detail: every step, the command line, the engine
- [`Docs/UltraCanvasSDK.md`](../UltraCanvasSDK.md) - the SDK's layout, linking and run-time details per platform
- [`Docs/GettingStarted.md`](../GettingStarted.md) - the same setup, step by step, by hand
- [`Docs/GettingStarted-Cloud.md`](../GettingStarted-Cloud.md) - working with an AI assistant without a local compiler
