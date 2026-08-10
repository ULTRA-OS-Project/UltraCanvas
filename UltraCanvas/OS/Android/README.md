# OS/Android — Android platform backend (phase 1)

The full investigation and phased plan live in
[`Docs/UltraCanvas/AndroidPortInvestigation.md`](../../../Docs/UltraCanvas/AndroidPortInvestigation.md);
this backend implements its **Phase 1 — pixels on screen** scope.

## What is here

| File | Role |
|---|---|
| `UltraCanvasAndroidApplication.{h,cpp}` | All `UltraCanvasApplicationBase` pure virtuals + `GetInstance()`. `CollectAndProcessNativeEvents` pumps the glue's `ALooper` (activity commands + input); cross-thread wakeup is `ALooper_wake` (no eventfd needed). Touch → mouse translation (pointer 0, with double-tap synthesis), `AKEYCODE_*` → `UCKeys` mapping, back button → `WindowCloseRequest`, fontconfig/Pango bundled-font registration, Roboto / Droid Sans Mono defaults. Cursor + mouse-capture virtuals are folded in as accepted no-ops (no separate cursor file). |
| `UltraCanvasAndroidWindow.{h,cpp}` | All `UltraCanvasWindowBase` pure virtuals. Cairo **image** surface at physical px (the Windows backend's model), presented via `ANativeWindow_lock` → xRGB→RGBX row copy → `unlockAndPost`. `QueryNativeDeviceScale()` = `AConfiguration_getDensity`/160. Handles `APP_CMD_INIT_WINDOW`/`TERM_WINDOW`/`WINDOW_RESIZED` surface lifecycle (see Lifecycle below); desktop window-management calls are no-ops. |
| `UltraCanvasAndroidMain.cpp` | `android_main()` on top of `android_native_app_glue` (compiled from the NDK by CMake). Exports `HOME`/`TMPDIR`/`XDG_CACHE_HOME` into the app sandbox, waits for the first surface, then calls the app-provided `extern "C" int ultracanvas_app_main(int argc, char** argv)` — an app's existing `main()` under a different name. |
| `UltraCanvasAndroidNativeDialogs.cpp` | All `UltraCanvasNativeDialogs` statics as logged "Cancel" stubs. Real dialogs are callback-based JNI (AlertDialog / Storage Access Framework) and need an async bridge — phase 2 (investigation §3.5). |
| `UltraCanvasAndroidFileLoader.cpp` | `NotifyRecentFile` no-op. |
| `GLContextManagerEGL_Android.cpp` | EGL + **OpenGL ES** context manager (ES 3 preferred, ES 2 fallback) behind the same `CreateGLContextManagerEGL()` factory symbol the dispatcher uses on Linux. Same offscreen model as the Linux EGL manager: 1×1 pbuffer made current, all real rendering into FBOs (`GLFramebuffer.cpp` compiles against `<GLES3/gl3.h>` on Android, and `ICompositeStrategy.cpp` reads back `GL_RGBA` + swizzles to Cairo's word order, since core GLES has no `GL_BGRA` readback). `GLSurfaceConfig`'s desktop fields (`glVersionMajor/Minor`, `coreProfile`) are ignored. Note: the FBO layer uses ES 3 sized formats (`GL_RGBA8`), so the ES 2 fallback context is best-effort only — every `minSdk 26` device ships ES 3.x. |

**Not copied here on purpose:** UltraNet's platform glue. `OS/Linux/UltraNetSupport.cpp`
(getenv proxy detection) and `OS/Linux/UltraNetTlsImpl.cpp` (OpenSSL) are
bionic-compatible as-is — their `#ifdef __linux__` guards are satisfied on
Android — so the Android UltraNet build reuses those files directly
(`UltraCanvas/CMakeLists.txt`, UltraNet section) instead of committing copies
that would drift. DNS always goes through **c-ares** (`ULTRANET_HAS_CARES` is
mandatory for Android; bionic has no `res_n*`/libresolv).

## Lifecycle (background / foreground / rotation)

`Run()` survives the whole activity lifecycle; only `APP_CMD_DESTROY`
(observed as `destroyRequested`) exits it.

- **Backgrounding** (`APP_CMD_TERM_WINDOW`): the presentation surface is
  destroyed and the window auto-hides, so `UpdateAndRender` stops doing
  per-frame work (timers still run). The offscreen render context survives.
  `UpdateAndRender` in the core additionally early-outs for any window whose
  `nativeSurface` is gone, so a stray dirty rect can never composite into a
  missing surface.
- **Foregrounding** (`APP_CMD_INIT_WINDOW`): the surface is rebuilt at the
  current size/density, the window re-shows itself, and a full-window
  composite is queued. A rotation that happened while backgrounded arrives
  as a size mismatch and runs the normal resize path.
- **In-place rotation / fold change** (`APP_CMD_CONFIG_CHANGED` /
  `WINDOW_RESIZED`): handled as a resize. For this path to be used, the app's
  manifest **must** keep the activity alive across configuration changes:

  ```xml
  <activity android:name="android.app.NativeActivity"
            android:configChanges="orientation|screenSize|screenLayout|keyboardHidden|density">
  ```

  Without that attribute Android destroys and recreates the NativeActivity on
  every rotation: the glue raises `destroyRequested`, `Run()` exits, and the
  app restarts from `android_main` — functional, but state is lost.
- `APP_CMD_PAUSE`/`STOP`/`START`/`RESUME` need no backend work beyond the
  above; `GAINED_FOCUS`/`LOST_FOCUS` map to `WindowFocus`/`WindowBlur`.

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
dependency exists in the sysroot. GL surfaces stay ON: EGL and GLESv3 come
from the NDK sysroot itself (no pkg-config probing), wired through
`GLContextManagerEGL_Android.cpp`.

## Still to come (phases 2–3, investigation §7)

JNI clipboard,
soft-keyboard/IME hook, SAF dialogs + `content://` adapter, UltraNet CA
bundle, real multi-touch in the core event model,
audio/video/PDF, Gradle packaging + a full sysroot CI build.
