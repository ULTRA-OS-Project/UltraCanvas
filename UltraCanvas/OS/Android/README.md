# OS/Android — Android platform backend

The full investigation and phased plan live in
[`Docs/UltraCanvas/AndroidPortInvestigation.md`](../../../Docs/UltraCanvas/AndroidPortInvestigation.md).
Implemented here: the window/application backend, lifecycle handling, the
EGL/GLES context manager, clipboard, soft keyboard and full IME, multi-touch
with pinch/rotate gestures, message dialogs, SAF file opening and saving, and
APK asset extraction.

**Still open, and it gates everything: the cross-compiled dependency sysroot
and APK packaging.** No APK can be built until that exists, so none of this has
run on a device — it is compiled continuously in CI, not observed. See
[`packaging/`](packaging/README.md).

## What is here

| File | Role |
|---|---|
| `UltraCanvasAndroidApplication.{h,cpp}` | All `UltraCanvasApplicationBase` pure virtuals + `GetInstance()`. `CollectAndProcessNativeEvents` pumps the glue's `ALooper` (activity commands + input); cross-thread wakeup is `ALooper_wake` (no eventfd needed). Touch → mouse translation (pointer 0, with double-tap synthesis), `AKEYCODE_*` → `UCKeys` mapping with layout-aware Unicode text via JNI `KeyCharacterMap` (US-ASCII derivation kept as fallback), back button → `WindowCloseRequest`, fontconfig/Pango bundled-font registration, Roboto / Droid Sans Mono defaults. Soft keyboard: `Show/HideSoftKeyboard()` (JNI `InputMethodManager` — the NDK's `ANativeActivity_showSoftInput` is unreliable by long-standing platform bug), driven automatically by `UltraCanvasCaret::onTextEditingChanged` with hides deferred one loop turn so focus moves between text widgets don't flicker the IME. Cursor + mouse-capture virtuals are folded in as accepted no-ops (no separate cursor file). |
| `UltraCanvasAndroidJni.{h,cpp}` | Shared JNI plumbing: lazy `AttachCurrentThread` for the glue thread (detached once at shutdown), activity handle, exception clear+log, jstring→std::string. |
| `UltraCanvasAndroidClipboard.{h,cpp}` | `UltraCanvasClipboardBackend` over JNI `ClipboardManager`. Text only (images/files need the SAF `content://` adapter — later phase); change detection via `ClipDescription.getTimestamp()` (API 26+). Android 10+ denies reads while the app lacks input focus; callers just see "no text" then. |
| `UltraCanvasAndroidWindow.{h,cpp}` | All `UltraCanvasWindowBase` pure virtuals. Cairo **image** surface at physical px (the Windows backend's model), presented via `ANativeWindow_lock` → xRGB→RGBX row copy → `unlockAndPost`. `QueryNativeDeviceScale()` = `AConfiguration_getDensity`/160. Handles `APP_CMD_INIT_WINDOW`/`TERM_WINDOW`/`WINDOW_RESIZED` surface lifecycle (see Lifecycle below); desktop window-management calls are no-ops. |
| `UltraCanvasAndroidMain.cpp` | `android_main()` on top of `android_native_app_glue` (compiled from the NDK by CMake). Exports `HOME`/`TMPDIR`/`XDG_CACHE_HOME` into the app sandbox, unpacks the APK's assets (below), waits for the first surface, then calls the app-provided `extern "C" int ultracanvas_app_main(int argc, char** argv)` — an app's existing `main()` under a different name. |
| `UltraCanvasAndroidNativeDialogs.cpp` | All `UltraCanvasNativeDialogs` statics. Message dialogs and file *opening* are real (AlertDialog / SAF through the bridge below); `SaveFile`, `SelectFolder` and input dialogs stay logged "Cancel" stubs — **Dialogs** below explains why the first two are blocked on an API decision rather than unfinished. |
| `UltraCanvasAndroidDialogBridge.{h,cpp}` | Sync-over-async bridge to the Java dialogs: shows the dialog, then pumps activity commands on the glue thread until the Java UI thread delivers the answer. Falls back cleanly when the app runs a plain `NativeActivity`. |
| `java/org/ultraos/ultracanvas/UltraCanvasActivity.java` | Optional `NativeActivity` subclass hosting everything that needs a real Activity on the Java UI thread: `AlertDialog`, the SAF picker with its `onActivityResult`, and the invisible input view whose `InputConnection` gives the IME something to compose into. Compiled against `android.jar` in CI by `scripts/android-java-check.sh`. |
| `UltraCanvasAndroidTextInput.cpp` | JNI entry points for that `InputConnection`: committed text, key events routed through the input view, and `deleteSurroundingText` replayed as Backspace presses. |
| `UltraCanvasAndroidAssets.{h,cpp}` | Unpacks the APK's `assets/` tree into `$HOME/share` on the first launch after an install or update (stamped against the APK's mtime+size). Inside an APK nothing is on the filesystem, so without this every path-based `fopen` for a font, icon or media file fails. Uses Java `AssetManager.list()` to walk the tree — the NDK's `AAssetDir` cannot see subdirectories — and the native `AAssetManager` to read contents. |
| `packaging/` | Manifest + Gradle **scaffolding** for building an APK, and the sysroot blocker that stops one being built today. |
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
  <activity android:name="org.ultraos.ultracanvas.UltraCanvasActivity"
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
Java UI thread can (an `AlertDialog`, `startActivityForResult` for SAF
and an `InputConnection` for the IME). Declare it in the manifest
*instead of* `android.app.NativeActivity`:

```xml
<activity android:name="org.ultraos.ultracanvas.UltraCanvasActivity"
          android:configChanges="orientation|screenSize|screenLayout|keyboardHidden|density">
    <meta-data android:name="android.app.lib_name" android:value="YourAppLib"/>
</activity>
```

It is **optional**. The C++ side looks every bridge method up by name and
degrades when it is absent — dialogs return "cancelled", and the soft keyboard
is raised against the decor view without an IME session — so an app that needs
neither native dialogs nor composed text input can ship with no Java at all.

Message dialogs (`ShowInfo` / `ShowWarning` / `ShowError` / `ShowQuestion` /
`ShowMessage` / `Confirm` / `ConfirmYesNo`) and **opening** files
(`OpenFile` / `OpenMultipleFiles`) go through it for real, as does saving via
`SaveContent`. `SaveFile`, `SelectFolder` and the input dialogs are still
stubs — see below for why the first two are not merely unfinished.

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

### Saving: use `SaveContent`, not `SaveFile`

`SaveFile` is still a stub on Android, and cannot be anything else. Its
contract — hand back a path, let the caller write to it afterwards — has no
Android implementation: SAF yields a `content://` URI rather than a path, and
nothing tells the framework when the caller has finished writing, so there is
no moment at which the bytes could be delivered to the document
(`NotifyRecentFile` fires *before* the caller writes one). A bridged `SaveFile`
would silently drop the user's work.

`UltraCanvasNativeDialogs::SaveContent(data, size, options)` closes that gap by
taking the content up front, which removes the ambiguity entirely:

```cpp
FileDialogOptions opts;
opts.SetTitle("Export").SetDefaultFileName("notes.txt");
bool saved = UltraCanvasNativeDialogs::SaveContent(text, opts);
```

On Android it runs `ACTION_CREATE_DOCUMENT` and writes through the
`ContentResolver`; on every desktop backend it is exactly `SaveFile()` followed
by a write, so portable code can call it everywhere. It reports success only if
the user chose a destination *and* the stream closed cleanly — a provider only
sees the document as complete on close, so a failed close is a failed save.

The MIME type SAF needs is derived from `defaultFileName`'s extension. Content
is passed to Java as a `byte[]`, so it is briefly held twice; this API suits
documents, not multi-gigabyte exports.

**`SelectFolder`** remains a stub: `ACTION_OPEN_DOCUMENT_TREE` returns a tree
URI that callers would enumerate and write through, which no single filesystem
path can stand in for.

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
touch events sees complete, unambiguous multi-finger input.

### Pinch and rotate

Two fingers moving together are recognised **in the core**, not here — the
touch stream is platform-neutral and so is the geometry, so every future touch
backend gets this from one implementation. The recogniser emits `PinchZoom` to
the element under the midpoint between the fingers, carrying:

- `scale` — 1.0 at the start of the gesture, 2.0 when the fingers are twice as
  far apart as they were.
- `rotation` — radians, positive clockwise, wrapped to (-π, π].
- `pointer` — the midpoint, in element-local coordinates as usual.

Both are measured against **the moment the gesture began**, not the previous
event, so a handler can map them directly onto a zoom or rotation anchored at
that point without accumulating drift over a long gesture.

The raw `TouchMove` events are still delivered first, so a widget doing its own
finger tracking has already had its say before any gesture is reported. A third
finger suspends recognition rather than guessing which pair to measure, and a
finger landing or lifting re-establishes the baseline instead of reporting the
discontinuity as a huge jump. A widget handling
both should treat `touchPointCount >= 2` as "this is a gesture, not a click".
`ACTION_CANCEL` (the system taking the gesture over, e.g. a system-gesture
swipe) ends every finger with `TouchEnd`, so no widget keeps a dangling touch.

## Text input (soft keyboard and IME)

The framework-wide "text editing started/stopped" signal is the caret:
`UltraCanvasCaret::onTextEditingChanged` (a core hook that stays null on
desktop) fires when a widget claims the caret with no previous owner or the
last owner releases it. The Android application maps that to
`ShowSoftKeyboard()` / `HideSoftKeyboard()`, with hides deferred one event-loop
turn so focus moving between two text widgets does not flicker the keyboard.

**With `UltraCanvasActivity`** the keyboard is raised against an invisible,
focusable input view that supplies a real `InputConnection`. That binding is
what makes an IME an IME: autocorrect, the suggestion bar, gesture typing, and
every language where a word is composed from several keystrokes or picked from
candidates. Committed text arrives as `UCEvent::text` (UTF-8); the soft
keyboard's backspace, which arrives as `deleteSurroundingText` rather than a
key event, is replayed as Backspace presses so existing text widgets handle it
unchanged. While the input view holds focus it forwards key events to the
native side itself, so a hardware keyboard keeps working during editing.

**Composing text stays inside the IME** and is never forwarded. That is not an
Android shortcut — it matches the framework's cross-platform text model: the
Linux backend likewise takes only the committed result out of
`Xutf8LookupString`, and no core widget can render an inline preedit region.
Showing composition inline would be a new cross-platform capability (a core
text model that carries a composing range, rendering for it, and support in
every backend), not something the Android layer can add on its own. The IME
shows its candidates in its own UI meanwhile, so CJK and suggestion-based input
work today.

**With a plain `NativeActivity`** there is no editor for the IME to attach to,
so the keyboard is raised against the decor view and only committed key events
arrive. Printable keys — soft and physical alike — are still translated through
the device's `KeyCharacterMap` into `UCEvent::text`, so non-US layouts type
correctly. Dead-key composition needs the `InputConnection` path.

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

**That sysroot does not exist yet, so no APK can be built and none of this has
run on a device.** Everything here is compiled in CI against the real NDK, and
the Java against `android.jar`, so the code is type-checked continuously — but
treat the runtime behaviour described in this file as designed and reviewed,
not as observed. `packaging/` holds the manifest and Gradle scaffolding, and
spells out what the sysroot needs to contain.

## Still to come (phases 2–3, investigation §7)

The cross-compiled dependency sysroot and a real APK build (the blocker for
everything below, since nothing can be observed until then), clipboard
images/files via the `content://` adapter, UltraNet CA bundle, gesture
recognition on top of the touch stream (pinch/rotate → `PinchZoom`), inline
IME composition (a cross-platform core change, not an Android one), and
audio/video/PDF.
