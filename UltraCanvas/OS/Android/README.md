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
| `UltraCanvasAndroidNativeDialogs.cpp` | All `UltraCanvasNativeDialogs` statics. Message dialogs are real (AlertDialog through the bridge below); file and input dialogs remain logged "Cancel" stubs until the SAF `content://` adapter lands (investigation §3.5). |
| `UltraCanvasAndroidDialogBridge.{h,cpp}` | Sync-over-async bridge to the Java dialogs: shows the dialog, then pumps activity commands on the glue thread until the Java UI thread delivers the answer. Falls back cleanly when the app runs a plain `NativeActivity`. |
| `java/org/ultraos/ultracanvas/UltraCanvasActivity.java` | Optional `NativeActivity` subclass hosting the Java-side dialogs (see **Dialogs** below). Type-checked in CI by `scripts/android-java-check.sh`. |
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

## Dialogs and the optional Java activity

`java/org/ultraos/ultracanvas/UltraCanvasActivity.java` is the Java half of the
backend: a `NativeActivity` subclass supplying what only a real Activity on the
Java UI thread can (an `AlertDialog` today; `startActivityForResult` for SAF
and an `InputConnection` for full IME next). Declare it in the manifest
*instead of* `android.app.NativeActivity`:

```xml
<activity android:name="org.ultraos.ultracanvas.UltraCanvasActivity"
          android:configChanges="orientation|screenSize|screenLayout|keyboardHidden|density">
    <meta-data android:name="android.app.lib_name" android:value="YourAppLib"/>
</activity>
```

It is **optional**. The C++ side looks its bridge methods up by name and falls
back to the "cancelled" stub when they are absent, so an app that never opens a
native dialog needs no Java at all.

Message dialogs (`ShowInfo` / `ShowWarning` / `ShowError` / `ShowQuestion` /
`ShowMessage` / `Confirm` / `ConfirmYesNo`) and **opening** files
(`OpenFile` / `OpenMultipleFiles`) go through it for real. `SaveFile`,
`SelectFolder` and the input dialogs are still stubs — see below for why the
first two are not merely unfinished.

### Opening files: SAF, and why you get a copy

`OpenFile` launches the system document picker (`ACTION_OPEN_DOCUMENT`), which
answers with a `content://` URI. No POSIX call can open one, and the
framework's API is path-based — callers `fopen` whatever the dialog returns.
So the Java side **copies the chosen document into the app cache** and returns
that path. Copying is the only approach that works for every provider,
including ones streaming from the network with no underlying file, and the
copy runs off the UI thread (the native thread is parked in its pump anyway).

The consequence to know about: callers read a *snapshot*. Edits written back
to that path stay in the cache and never reach the original document.

Filters are mapped extension → MIME type, since SAF filters by MIME. Any
unmapped extension or a wildcard widens the picker to everything rather than
risk hiding a file the user asked for.

### Why `SaveFile` and `SelectFolder` are still stubs

Not oversights — both are blocked on the *shape* of the cross-platform API,
not on Android:

- **`SaveFile`** returns a path and then returns; the caller writes to it
  afterwards and nothing tells us when it has finished. Opening survives this
  because a copy is a complete answer at return time. Saving would need that
  copy pushed back to the `content://` URI at a commit point this API cannot
  express (`NotifyRecentFile` fires *before* the caller writes a byte), so a
  bridged `SaveFile` would silently drop the user's work. Closing this needs a
  cross-platform decision — a save variant that takes the bytes, or an explicit
  commit call — so it stays an honest stub rather than a plausible-looking one
  that loses data.
- **`SelectFolder`** would return a tree URI that callers enumerate and write
  through; no single filesystem path can stand in for that.

**How a synchronous API survives an asynchronous platform:** the framework's
dialog calls return the answer (`ShowQuestion(...) -> DialogResult`), while
every Android dialog is asynchronous and lives on the Java UI thread. The
bridge asks the activity to show the dialog and then blocks the calling thread
in `PumpWhileModal` until the answer arrives. That is safe here precisely
because the framework does *not* run on the Java main thread — it has
android_native_app_glue's own thread — so the UI thread stays free to run the
dialog and deliver the result. Blocking the same way on a desktop UI thread
would freeze the app.

While blocked, the pump processes **activity commands only**, and both halves
of that matter:

- Commands *must* be processed: the glue parks the Java main thread inside some
  of them (`APP_CMD_TERM_WINDOW` waits for the native thread to acknowledge the
  surface is gone). That is the very thread that owes us the dialog result, so
  ignoring commands would deadlock the two threads against each other.
- Input must *not* be: a widget is sitting inside its own dialog call, and
  re-entering it with fresh input is exactly the reentrancy a modal dialog
  exists to prevent. Input stays queued until the dialog returns.

Every path out of the Java dialog — button, back button, dismissal, even a
failure to show it — delivers exactly one result, because anything less leaves
the native thread pumping forever.

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
