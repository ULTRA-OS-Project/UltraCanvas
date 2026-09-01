# Packaging an UltraCanvas app as an APK

**Status: scaffolding, not a working build.** The manifest is complete and
every attribute in it is load-bearing (each is annotated with what breaks
without it). Everything else here — `vcpkg.json`, `build.gradle.template`, and
`scripts/android-bootstrap-sysroot.sh` — has **never been run**, because
UltraCanvas needs cairo, pango, glib, harfbuzz, fontconfig, freetype and
tinyxml2 cross-compiled for `arm64-v8a`, and no such sysroot exists yet.
Until one does, the native library cannot link and no APK can be produced.

## Building the sysroot (untested)

`vcpkg.json` + `scripts/android-bootstrap-sysroot.sh` implement the route
[`AndroidPortInvestigation.md` §4](../../../../Docs/UltraCanvas/AndroidPortInvestigation.md)
recommends evaluating first — vcpkg's `arm64-android` triplet, chainloading the
NDK's own toolchain — chosen over hand-written meson cross-files because a
declarative dependency list fails in obvious ways (a port exists or it doesn't)
rather than in subtle ones:

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk-r26
export VCPKG_ROOT=/path/to/vcpkg          # bootstrapped
scripts/android-bootstrap-sysroot.sh --with-net   # --with-net is optional
```

It prints the exact `cmake` invocation to use afterwards, including the
`PKG_CONFIG_LIBDIR` that stops pkg-config falling back to the host's
`/usr/lib/pkgconfig` and silently linking desktop libraries into an Android
build.

**Expect to iterate.** The script's header lists the likely failure points in
order: ports without arm64-android support (pango and cairo are the ones to
watch), glib's cross-build (the investigation calls it "the heaviest pain point
after GTK"), and pkg-config files with hardcoded host paths. None of those mean
the approach is wrong; they mean it needs a run on a machine with an NDK.

## What already works without packaging

Everything in `OS/Android/` is compiled in CI against the real NDK
(`scripts/android-syntax-check.sh`), and the Java against `android.jar`
(`scripts/android-java-check.sh`). So the code is type-checked continuously —
but **none of it has run on a device**, and it cannot until the sysroot exists.
Treat every runtime behaviour described in the backend README as designed and
reviewed, not as observed.

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
on first launch, so it costs install size **and** disk. This is another reason
the Android build defaults libvips off.

## Signing

`apksigner` replaces the desktop `SignUltraDemo.ps1` / `package-*.sh`
machinery. Note that `package-linux.sh`'s "exclude host-provided libraries"
logic **inverts** here: on Android nothing is host-provided, so every
dependency ships inside the APK.
