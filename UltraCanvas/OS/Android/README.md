# OS/Android — Android platform backend (phase 1)

The full investigation and phased plan live in
[`Docs/UltraCanvas/AndroidPortInvestigation.md`](../../../Docs/UltraCanvas/AndroidPortInvestigation.md);
this backend implements its **Phase 1 — pixels on screen** scope.

## What is here

| File | Role |
|---|---|
| `UltraCanvasAndroidApplication.{h,cpp}` | All `UltraCanvasApplicationBase` pure virtuals + `GetInstance()`. `CollectAndProcessNativeEvents` pumps the glue's `ALooper` (activity commands + input); cross-thread wakeup is `ALooper_wake` (no eventfd needed). Touch → mouse translation (pointer 0, with double-tap synthesis), `AKEYCODE_*` → `UCKeys` mapping, back button → `WindowCloseRequest`, fontconfig/Pango bundled-font registration, Roboto / Droid Sans Mono defaults. Cursor + mouse-capture virtuals are folded in as accepted no-ops (no separate cursor file). |
| `UltraCanvasAndroidWindow.{h,cpp}` | All `UltraCanvasWindowBase` pure virtuals. Cairo **image** surface at physical px (the Windows backend's model), presented via `ANativeWindow_lock` → xRGB→RGBX row copy → `unlockAndPost`. `QueryNativeDeviceScale()` = `AConfiguration_getDensity`/160. Handles `APP_CMD_INIT_WINDOW`/`TERM_WINDOW`/`WINDOW_RESIZED` surface lifecycle; desktop window-management calls are no-ops. |
| `UltraCanvasAndroidMain.cpp` | `android_main()` on top of `android_native_app_glue` (compiled from the NDK by CMake). Exports `HOME`/`TMPDIR`/`XDG_CACHE_HOME` into the app sandbox, waits for the first surface, then calls the app-provided `extern "C" int ultracanvas_app_main(int argc, char** argv)` — an app's existing `main()` under a different name. |
| `UltraCanvasAndroidNativeDialogs.cpp` | All `UltraCanvasNativeDialogs` statics as logged "Cancel" stubs. Real dialogs are callback-based JNI (AlertDialog / Storage Access Framework) and need an async bridge — phase 2 (investigation §3.5). |
| `UltraCanvasAndroidFileLoader.cpp` | `NotifyRecentFile` no-op. |

**Not copied here on purpose:** UltraNet's platform glue. `OS/Linux/UltraNetSupport.cpp`
(getenv proxy detection) and `OS/Linux/UltraNetTlsImpl.cpp` (OpenSSL) are
bionic-compatible as-is — their `#ifdef __linux__` guards are satisfied on
Android — so the Android UltraNet build reuses those files directly
(`UltraCanvas/CMakeLists.txt`, UltraNet section) instead of committing copies
that would drift. DNS always goes through **c-ares** (`ULTRANET_HAS_CARES` is
mandatory for Android; bionic has no `res_n*`/libresolv).

## Clipboard

No backend file: `core/UltraCanvasClipboard.cpp` deliberately instantiates no
Android backend and every clipboard call degrades to a safe no-op on the null
backend. The JNI `ClipboardManager` bridge is phase 2.

## Building

Needs the NDK toolchain plus a cross-compiled dependency sysroot (cairo, pango,
glib, harfbuzz, fontconfig, freetype, tinyxml2) reachable through
`PKG_CONFIG_LIBDIR` — see investigation §4:

```sh
cmake -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
```

The root CMakeLists defaults everything except the core library OFF for
Android (apps, plugins, vips, audio/video, UltraNet, database, VirtualFS);
each is an ordinary cache option that `-D...=ON` re-enables once its
dependency exists in the sysroot. GL surfaces are forced OFF until the
GLES/EGL context manager lands (phase 2).

## Still to come (phases 2–3, investigation §7)

Lifecycle virtuals (pause/resume without quitting `Run()`), JNI clipboard,
soft-keyboard/IME hook, SAF dialogs + `content://` adapter, UltraNet CA
bundle, EGL/GLES context manager, real multi-touch in the core event model,
audio/video/PDF, Gradle packaging + CI job.
