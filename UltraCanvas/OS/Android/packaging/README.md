# Packaging an UltraCanvas app as an APK

**Status: scaffolding, not a working build.** The manifest here is complete and
every attribute in it is load-bearing (each one is annotated with what breaks
without it). `build.gradle.template` is a starting point that has **never been
run**, because the piece it depends on does not exist yet:

> UltraCanvas needs cairo, pango, glib, harfbuzz, fontconfig, freetype and
> tinyxml2 **cross-compiled for `arm64-v8a`**. There is no such sysroot in this
> repository and no script that builds one. Until there is, no APK can be
> produced — the native library cannot link.

See [`AndroidPortInvestigation.md` §4](../../../../Docs/UltraCanvas/AndroidPortInvestigation.md)
for the dependency analysis, including which libraries are genuinely required
and which were made optional to keep the list short. Its recommendation is to
evaluate vcpkg's `arm64-android` triplet first, with Conan or hand-written
meson cross-files as fallbacks; that evaluation has not been done.

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
