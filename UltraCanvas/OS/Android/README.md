# OS/Android — Android platform backend (phase 1)

The full investigation and phased plan live in
[`Docs/UltraCanvas/AndroidPortInvestigation.md`](../../../Docs/UltraCanvas/AndroidPortInvestigation.md);
this backend implements its **Phase 1 — pixels on screen** scope.

## What is here

| File | Role |
|---|---|
| `UltraCanvasAndroidApplication.{h,cpp}` | All `UltraCanvasApplicationBase` pure virtuals + `GetInstance()`. `CollectAndProcessNativeEvents` pumps the glue's `ALooper` (activity commands + input); cross-thread wakeup is `ALooper_wake` (no eventfd needed). Touch → mouse translation (pointer 0, with double-tap synthesis), `AKEYCODE_*` → `UCKeys` mapping with layout-aware Unicode text via JNI `KeyCharacterMap` (US-ASCII derivation kept as fallback), back button → `WindowCloseRequest`, fontconfig/Pango bundled-font registration, Roboto / Droid Sans Mono defaults. Soft keyboard: `Show/HideSoftKeyboard()` (JNI `InputMethodManager` — the NDK's `ANativeActivity_showSoftInput` is unreliable by long-standing platform bug), driven automatically by `UltraCanvasCaret::onTextEditingChanged` with hides deferred one loop turn so focus moves between text widgets don't flicker the IME. Cursor + mouse-capture virtuals are folded in as accepted no-ops (no separate cursor file). |
| `UltraCanvasAndroidJni.{h,cpp}` | Shared JNI plumbing: lazy `AttachCurrentThread` for the glue thread (detached once at shutdown), activity handle, exception clear+log, jstring→std::string. |
| `UltraCanvasAndroidClipboard.{h,cpp}` | `UltraCanvasClipboardBackend` over JNI `ClipboardManager`. Text only (images/files need the SAF `content://` adapter — later phase); change detection via `ClipDescription.getTimestamp()` (API 26+). Android 10+ denies reads while the app lacks input focus; callers just see "no text" then. |
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

## Touch input

Every finger is delivered as `TouchStart` / `TouchMove` / `TouchEnd`, carrying
`UCEvent::pointerId` (stable for that finger's whole life — ids are reused
only after it lifts, so never assume 0 is still the finger you saw earlier)
and `touchPointCount`. The core routes each one to the element under *that*
finger, with bubbling and element-local `pointer` coordinates, and without any
hover, cursor or capture handling — a second finger must not disturb the state
the first one established.

On top of that, the **primary finger is also translated to mouse events**
(`MouseDown`/`MouseMove`/`MouseUp`, plus synthesized `MouseDoubleClick`), which
is what makes every existing mouse-written widget work untouched on a
touchscreen. The two streams are mutually exclusive per gesture:

- One finger → touch events **and** mouse events.
- The moment a second finger lands → a final `MouseUp` closes the mouse
  interaction (so no widget is left believing a button is held) and mouse
  synthesis stays off until *every* finger has lifted. A pinch therefore
  cannot also drag whatever the first finger happened to be on.

So a widget that only handles mouse events keeps working; one that handles
touch events sees complete, unambiguous multi-finger input. A widget handling
both should treat `touchPointCount >= 2` as "this is a gesture, not a click".
`ACTION_CANCEL` (the system taking the gesture over, e.g. a system-gesture
swipe) ends every finger with `TouchEnd`, so no widget keeps a dangling touch.

## Text input (soft keyboard)

The framework-wide "text editing started/stopped" signal is the caret:
`UltraCanvasCaret::onTextEditingChanged` (a core hook that stays null on
desktop) fires when a widget claims the caret with no previous owner or the
last owner releases it. The Android application maps that to
`ShowSoftKeyboard()` / `HideSoftKeyboard()`. Printable keys — soft and
physical alike — are translated through the device's `KeyCharacterMap`
into `UCEvent::text` as UTF-8, so non-US layouts type correctly. Dead-key
composition and full IME text (composing regions, voice input) are not
supported yet; that requires a Java `InputConnection` proxy (GameTextInput
territory) in a later phase.

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

Full IME (composing text via an `InputConnection` proxy), SAF dialogs +
`content://` adapter (which also unlocks clipboard images/files), UltraNet
CA bundle, gesture recognition on top of the touch stream (pinch/rotate →
`PinchZoom`), audio/video/PDF, Gradle packaging + a full sysroot CI build.
