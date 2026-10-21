# Packaging an UltraCanvas app as an APK

**Status: CI builds an installable demo APK; it has not been run yet.** The
manifest is complete and every attribute in it is load-bearing (each is
annotated with what breaks without it). `vcpkg.json` and
`scripts/android-bootstrap-sysroot.sh` cross-compile cairo, pango, glib,
harfbuzz, fontconfig, freetype, tinyxml2, fmt and libvips for `arm64-v8a` and
`x86_64`: `.github/workflows/android-build.yml` runs them on every pull request
that touches the Android build, compiles the core library against the result,
links the demo into `libUltraCanvasDemo.so`, the library the manifest here
names (see *The app library* below), and packages it with
`scripts/android-package-apk.sh` (see *Building the APK*). Each run uploads
`UltraCanvasDemo-arm64-v8a` and `UltraCanvasDemo-x86_64`, a debug-signed APK
each, as artifacts. `build.gradle.template` has still never been run.

## Building the sysroot

`vcpkg.json` + `scripts/android-bootstrap-sysroot.sh` implement the route
[`AndroidPortInvestigation.md` §4](../../../../Docs/UltraCanvas/AndroidPortInvestigation.md)
recommends evaluating first — vcpkg's `arm64-android` triplet, chainloading the
NDK's own toolchain — chosen over hand-written meson cross-files because a
declarative dependency list fails in obvious ways (a port exists or it doesn't)
rather than in subtle ones:

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk      # r25 or later
scripts/android-bootstrap-sysroot.sh --with-net   # --with-net is optional
```

Without `VCPKG_ROOT`, the script checks out vcpkg at the commit
`scripts/macos-deps.sh` pins, so Android and macOS build the same library
versions. libvips comes from the macOS overlay port
(`vcpkg-configuration.json`), which adds the GIF and Netpbm loaders vcpkg's
own port leaves out.

The sysroot goes to `~/.cache/ultracanvas/android-sysroot` unless
`UC_ANDROID_SYSROOT` names another directory. It must be outside the source
tree: the core library exports the sysroot's lib directory, and CMake refuses
to export one from inside the source tree.

The script prints the exact `cmake` invocation to use afterwards. It uses the
NDK's toolchain file, and sets the `PKG_CONFIG_LIBDIR` that stops pkg-config
falling back to the host's `/usr/lib/pkgconfig` and silently linking desktop
libraries into an Android build.

**What CI covers:** `arm64-v8a`, without `--with-net`. The other ABIs and the
`net` feature (OpenSSL, c-ares, curl) have not been run yet. The likely
failure there is a port that has no support for that triplet.

## The app library

An Android app is a shared library that `NativeActivity` loads, not a program.
DemoApp shows the shape every app takes:

- **Its `main()` becomes `ultracanvas_app_main()` on Android**
  (`Apps/DemoApp/main.cpp`, under `#ifdef __ANDROID__`). The framework's
  `android_main()` calls it once the sandbox is set up, the assets are
  unpacked and the first surface exists. Code guarded by `#ifdef __linux__`
  for X11 must say `!defined(__ANDROID__)` as well: bionic defines
  `__linux__`.
- **CMake builds it with `add_library(... SHARED)`** where the desktop uses
  `add_executable`; the root `CMakeLists.txt` does this for `UltraCanvasDemo`,
  giving `libUltraCanvasDemo.so`. The manifest's `android.app.lib_name` is that
  name without `lib` and `.so`.
- **Nothing else is needed for the entry points.** The core is a static
  archive, and the functions called into the library from outside it —
  `ANativeActivity_onCreate`, and the `Java_*` functions behind
  `UltraCanvasActivity`'s native methods — are referenced by nothing in the
  app, so a plain link would leave them out. The core's link interface carries
  a `-u` for each (`UltraCanvas/CMakeLists.txt`, read from
  `OS/Android/*.cpp` at configure time), so any app linking it gets them, and
  CI checks the demo library exports every one.

## Building the APK

`scripts/android-package-apk.sh` makes the APK from an app library with the
SDK's build tools alone, no Gradle:

```sh
scripts/android-package-apk.sh --abi x86_64 \
    --lib build-android/lib/libUltraCanvasDemo.so \
    --out UltraCanvasDemo-x86_64.apk --ndk "$ANDROID_NDK_HOME"
```

1. `UltraCanvasActivity.java` is compiled against `android.jar` and turned
   into `classes.dex` (`javac`, `d8`).
2. The launcher icon the manifest names (`@mipmap/ic_launcher`) is made from
   `media/appicon/Demo.png` (`--icon` picks another) at the five launcher
   densities, 48 to 192 px, each square with the image centred on
   transparency, so a non-square icon is not stretched (ImageMagick, then
   `aapt2 compile`).
3. `aapt2 link` writes the manifest here, with `minSdk 28` and
   `targetSdk 34`, the icon and the assets.
4. The library goes in stripped (the NDK's Release build keeps its debug
   information) and stored, under `lib/<abi>/`.
5. `zipalign` puts the library on a page boundary, and `apksigner` signs with
   a key made for the run: a debug APK for a test install, not one for a
   store. The script then verifies the signature and prints what a device
   reads from the package.

The assets are the set the demo's WebAssembly page preloads (about 53 MB):
the media its pages show, without the largest 3D samples and the audio and
video ones, plus `Docs/` and the demo's own sources for its source viewer.
The script lists them; another app would pass its own.

Install one with `adb install UltraCanvasDemo-x86_64.apk` on an x86_64
emulator, or the `arm64-v8a` one on a phone.

## What already works without packaging

Everything in `OS/Android/` is compiled in CI against the real NDK
(`scripts/android-syntax-check.sh`), and the Java against `android.jar`
(`scripts/android-java-check.sh`). So the code is type-checked continuously —
but **none of it has run on a device** yet: CI builds the APK, and nothing
has installed it.
Treat every runtime behaviour described in the backend README as designed and
reviewed, not as observed.

## Which ABI to build first

`build.gradle.template` says `abiFilters 'arm64-v8a'`, which is what ships —
but it is the wrong ABI to *start* with, because nothing on an x86_64 Linux
desktop can run it without a translation layer or whole-CPU emulation. Build
`x86_64` first (`scripts/android-bootstrap-sysroot.sh x86_64`, already wired to
vcpkg's `x64-android` triplet), get the APK running on an emulator, then add
arm64. See
[`AndroidOnLinuxInvestigation.md`](../../../../Docs/UltraCanvas/AndroidOnLinuxInvestigation.md).

## Layout a Gradle project needs

```
your-app/
  settings.gradle
  app/
    build.gradle              <- from build.gradle.template
    src/main/
      AndroidManifest.xml     <- from AndroidManifest.xml here
      assets/                 <- media/, Docs/, app resources (see below)
      jniLibs/arm64-v8a/      <- the cross-compiled dependency .so files
```

## Assets

The desktop builds `copy_assets` next to the executable and every loader
`fopen`s those paths. An APK has no such filesystem, so anything the app loads
at runtime goes in `src/main/assets/`, and the backend unpacks it to
`$HOME/share` on the first launch after an install or update
(`UltraCanvasAndroidAssets.cpp`) — which is exactly where `SetResourcesDir`'s
Android arm points, so path-based loading keeps working unchanged.

Keep `assets/` small: everything there is copied into the app's private storage
on first launch, so it costs install size **and** disk.

## Signing

`apksigner` replaces the desktop `SignUltraDemo.ps1` / `package-*.sh`
machinery. Note that `package-linux.sh`'s "exclude host-provided libraries"
logic **inverts** here: on Android nothing is host-provided, so every
dependency ships inside the APK.
