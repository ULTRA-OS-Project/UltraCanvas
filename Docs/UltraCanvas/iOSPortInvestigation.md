# iOS Port Investigation

**Status:** Investigation / planning document. Written before any iOS code
exists: there is no `UltraCanvas/OS/iOS/`, no iOS CMake branch beyond the one
that stops the configure, no dependency build and no CI job. An iOS compile
stops at `#error "iOS backend not implemented yet"` in
`include/UltraCanvasWindow.h` and `include/UltraCanvasApplication.h`, and an
iOS configure (`-DCMAKE_SYSTEM_NAME=iOS`) stops in the platform detection of
`UltraCanvas/CMakeLists.txt` with a message pointing here.
**Goal:** Bring UltraCanvas to iPhone and iPad with the support the Android
backend has: one full-screen surface, touch and gestures, the on-screen
keyboard with composition, clipboard, message dialogs, document picking and
saving, fonts, networking, audio and video, and (later) GL surfaces.
**Model:** [`AndroidPortInvestigation.md`](AndroidPortInvestigation.md), the
audit that preceded the Android backend. Most of what it found about a
single-surface, touch-first platform is solved in the core now, and this
document leans on those solutions instead of repeating them.

---

## Contents

- [1. Executive summary](#1-executive-summary)
- [2. How an iOS backend plugs in](#2-how-an-ios-backend-plugs-in)
- [3. Parts list: `UltraCanvas/OS/iOS/`](#3-parts-list-ultracanvasosios)
- [4. Building the dependencies for iOS](#4-building-the-dependencies-for-ios)
- [5. The GL / Metal decision](#5-the-gl--metal-decision)
- [6. The CMake branch](#6-the-cmake-branch)
- [7. Packaging, signing and CI](#7-packaging-signing-and-ci)
- [8. Phased plan](#8-phased-plan)
- [9. Key file index](#9-key-file-index)

---

## 1. Executive summary

- **Feasible, and cheaper than Android was.** The Android port had to add the
  mobile concepts to the core first; they exist now and are
  platform-neutral:
  - touch events with a stable `pointerId`, `touchPointCount`, routing in
    `DispatchEvent`, and the `PinchZoom` event (`include/UltraCanvasEvent.h`);
  - the "text editing started / stopped" signal that raises a soft keyboard
    (`UltraCanvasCaret::onTextEditingChanged`, `include/UltraCanvasCaret.h`);
  - IME pre-edit through the `TextComposition` event and
    `DrawsTextComposition()`;
  - saving to a document provider through
    `UltraCanvasNativeDialogs::SaveContent(data, size, options)`, which takes
    the bytes up front (`include/UltraCanvasNativeDialogs.h`);
  - popups and the tooltip drawn inside the one surface: the switch already
    excludes iOS (`core/UltraCanvasWindow.cpp:95`,
    `core/UltraCanvasTooltipManager.cpp:63`);
  - fallbacks in core for the platform services a backend does not supply
    (hardware info, wireless, removable drives, desktop shell, geolocation,
    network monitor), selected by `ULTRACANVAS_<X>_NATIVE`.
- **Two templates, not one.** The macOS backend (`OS/MacOS/`, 10.9k lines,
  Objective-C++) is the template for the language, the build (`.mm` sources,
  ARC, Apple frameworks) and the run-loop wakeup (`CFRunLoopSource`). The
  Android backend (`OS/Android/`, 4.5k lines) is the template for everything
  mobile: one surface, lifecycle, touch, the soft keyboard, synchronous
  dialogs over an asynchronous platform, and a sandboxed file system.
- **Almost nothing in `OS/MacOS/` compiles for iOS as it is.** Clipboard,
  dialogs, window, application, accessibility and spell check import
  AppKit/Cocoa; the hardware, Wi-Fi, Bluetooth, removable-drive, screen
  capture and folder-watch services use IOKit, CoreWLAN, IOBluetooth,
  DiskArbitration, ScreenCaptureKit and FSEvents, which iOS lacks. The iOS
  backend is a sibling directory, not a set of `#if`s in the macOS one
  (house rule 4: platform logic stays in `OS/<Platform>/`).
- **The three cost centres:**
  1. the dependency stack (cairo, pango, glib, harfbuzz, fontconfig,
     freetype, libvips and its codecs, OpenSSL) built static for
     `arm64-ios` and `arm64-ios-simulator`;
  2. the run loop: UIKit owns the main thread's loop through
     `UIApplicationMain`, which never returns, while an UltraCanvas program's
     `main()` creates its windows and then blocks in `Run()` (§3.2);
  3. no plug-in DSOs: an App Store app may not `dlopen` code it did not ship
     in a signed framework, so every plug-in and chart module is compiled in,
     as on Android and WebAssembly.
- **A Mac is required for every step past this document.** Xcode, the iOS SDK
  and the simulator exist only on macOS; nothing here can be built or run in
  the Linux CI containers. The GitHub macOS runners already used for the
  macOS row can build and run the simulator.

---

## 2. How an iOS backend plugs in

Platform selection is at compile time, by alias, exactly as described in
§2 of the Android investigation. The iOS hooks that already exist:

| Site | Today | iOS needs |
|---|---|---|
| `include/UltraCanvasWindow.h:806` | `#elif TARGET_OS_IPHONE` → `#error` | include `../OS/iOS/UltraCanvasiOSWindow.h`, `using UltraCanvasWindow = UltraCanvasiOSWindow;` |
| `include/UltraCanvasApplication.h:731` | `#elif TARGET_OS_IPHONE` → `#error` | include `../OS/iOS/UltraCanvasiOSApplication.h`, `using UltraCanvasApplication = UltraCanvasiOSApplication;` |
| `include/UltraCanvasMenu.h:48` | macOS shortcut format excluded on iOS | nothing: the pass-through format is right for a hardware keyboard on iPad |
| `include/UltraCanvasNativeHandle.h:24` | `__APPLE__` → opaque `void*` | nothing: a `UIView*` (or `UIWindow*`) fits the same opaque handle |
| `include/UltraCanvasEvent.h:372` | `__APPLE__` native-handle arm | check it holds for a `UIView*` |
| `core/UltraCanvasClipboard.cpp:29,110` | `__APPLE__` → `OS/MacOS/UltraCanvasMacOSClipboard.h` | a `TARGET_OS_IPHONE` arm for the iOS clipboard, before the `__APPLE__` one |
| `libspecific/GL/GLFramebuffer.cpp:6` | `__APPLE__` → `<OpenGL/gl3.h>` | a GLES arm (§5), or GL off |
| `include/UltraCanvasHardwareInfoBackend.h:20`, `UltraCanvasWirelessBackend.h`, `UltraCanvasRemovableDrivesBackend.h`, `UltraCanvasDesktopShellBackend.h` | `defined(__APPLE__)` sets `ULTRACANVAS_<X>_NATIVE` | `&& !TARGET_OS_IPHONE`, so iOS links the core fallbacks until it has probes of its own |
| `core/UltraCanvasWindow.cpp:95`, `core/UltraCanvasTooltipManager.cpp:63` | popups in their own windows off on iOS | nothing |

That list is not the whole audit. There are 70 `__APPLE__` tests in 38 files
under `UltraCanvas/core`, `include` and `libspecific`, and each must be read
once for "means macOS" versus "means any Apple OS". The rule for the port:
**an `__APPLE__` test that reaches AppKit, IOKit or any framework iOS lacks
becomes `__APPLE__ && !TARGET_OS_IPHONE`**. The core then falls back to its
portable path there, and the iOS arm is added only where iOS has something
of its own.

The same audit applies to CMake: `UltraCanvas/CMakeLists.txt` has 12
`ULTRACANVAS_PLATFORM STREQUAL "MacOS"` branches and 17 `APPLE` tests. The
`MacOS` ones are safe once iOS has its own `ULTRACANVAS_PLATFORM`; the `APPLE`
ones must each be checked (§6).

---

## 3. Parts list: `UltraCanvas/OS/iOS/`

### 3.1 Files and their contracts

The pure-virtual sets are the ones the Android investigation lists (§3.2
there). Today `UltraCanvasApplicationBase` has 20 pure virtuals and
`UltraCanvasWindowBase` has 23. As on every backend, the statics
(`GetInstance()`, `instance`) are declared in the backend, not the base.

| File | Contract | Template |
|---|---|---|
| `UltraCanvasiOSApplication.{h,mm}` | the application base's pure virtuals; `RunBeforeMainLoop` / `RunInEventLoop` overrides (§3.2); `CFRunLoopSource` wakeup | macOS `UltraCanvasMacOSApplication.mm:271-358` (wakeup and timed wait); Android for lifecycle |
| `UltraCanvasiOSMain.mm` | the process entry: `main()` calls `UIApplicationMain` with the backend's app delegate and scene delegate; the application's own `main()` is renamed `ultracanvas_app_main` (§3.2) | Android `UltraCanvasAndroidMain.cpp` (`ultracanvas_app_main`) |
| `UltraCanvasiOSWindow.{h,mm}` | the window base's pure virtuals; a `UIView` subclass that presents the Cairo image surface (§3.3); `QueryNativeDeviceScale()` from the screen's scale; safe-area insets; rotation and iPad split view resize the surface | macOS `UltraCanvasMacOSWindow.mm` (view subclass, `drawRect:`); Android for "most window-management calls are no-ops" |
| `UltraCanvasiOSTextInput.mm` | a view adopting `UITextInput` (`UIKeyInput` alone gives no composition): `insertText:` → `UCEvent::text`, `deleteBackward` → Backspace, `setMarkedText:selectedRange:` → `TextComposition`, `unmarkText` commits; `becomeFirstResponder` / `resignFirstResponder` driven by `onTextEditingChanged`; hardware keys from `pressesBegan:` (`UIKey.keyCode`) → `UCKeys` | Android `UltraCanvasAndroidTextInput.cpp` |
| `UltraCanvasiOSClipboard.{h,mm}` | the clipboard backend's virtuals over `UIPasteboard.generalPasteboard`: text, images, URLs; `changeCount` replaces polling | Android `UltraCanvasAndroidClipboard.cpp` (simple model), macOS for the Objective-C |
| `UltraCanvasiOSNativeDialogs.mm` | the static `UltraCanvasNativeDialogs` functions: messages and questions through `UIAlertController`; opening through `UIDocumentPickerViewController` (security-scoped URL → copy into the app's caches so path-based callers keep working); `SaveContent` through the picker's export mode; `SaveFile` and `SelectFolder` stubs, for the reasons Android gives (§3.5 there) | Android `UltraCanvasAndroidDialogBridge.cpp` and `UltraCanvasAndroidNativeDialogs.cpp` |
| `UltraCanvasiOSCursor.mm` | the two cursor functions: `return true` on iPhone; iPad pointer shapes (`UIPointerInteraction`) later | Android `UltraCanvasAndroidCursor` stubs |
| `UltraCanvasiOSFileLoader.mm` | `NotifyRecentFile` stub | `UltraCanvasAndroidFileLoader.cpp` |
| `UltraCanvasiOSSystemLanguage.mm` | `NSLocale.preferredLanguages` | macOS `UltraCanvasMacOSSystemLanguage.mm` (Foundation only, so likely shared) |
| `UltraCanvasiOSHostAppearance.mm` | dark mode, contrast, accent, Dynamic Type from `UITraitCollection` | macOS `UltraCanvasMacOSHostAppearance.mm` |
| `UltraCanvasSpellCheckSupport.mm` | `UITextChecker` in place of `NSSpellChecker` | macOS `UltraCanvasSpellCheckSupport.mm` |
| `UltraCanvasiOSAssets.mm` | `SetResourcesDir` arm: the app bundle's resource directory is a real, read-only file system, so `media/` and the rest are read in place, with no Android-style unpacking; writable state goes under `Library/Application Support` and `Library/Caches` | Android `UltraCanvasAndroidAssets.cpp` (what it had to work around) |
| `UltraNetSupport.mm` | proxy settings (`CFNetworkCopySystemProxySettings` exists on iOS) | macOS `UltraNetSupport.mm`; check each call against the iOS SDK |
| `UltraNetTlsImpl.mm` | TLS | **not** the macOS file as it is: it imports `Security/SecImportExport.h` (macOS only) and is built on SecureTransport, deprecated on every Apple OS. Use the OpenSSL implementation the other platforms build, and verify peers against the system roots through `SecTrustEvaluateWithError` |
| `UltraNetDnsImpl.mm` | DNS | macOS file uses `<arpa/nameser.h>` / libresolv, which the iOS SDK has; or use the c-ares path, as Android does, and drop the file |
| `GLContextManager*_iOS.*` | phase 3 only (§5) | Android `GLContextManagerEGL_Android.cpp` if ANGLE is chosen |

Not ported (the core fallbacks answer, with a note saying what is
unavailable): hardware info (no IOKit registry), Wi-Fi and Bluetooth lists
(CoreWLAN and IOBluetooth do not exist; CoreBluetooth is a different API and
a later decision), removable drives, volume monitor, folder watcher (no
FSEvents; a `dispatch_source` on a file descriptor can come later), file
associations, trash, aliases, host file icons, session lock, screen capture,
portable devices, and "applications" enumeration.

### 3.2 Run loop and lifecycle

On every desktop platform the application's `main()` creates its windows and
then blocks in `UltraCanvasApplicationBase::Run()`
(`core/UltraCanvasApplication.cpp`), which pumps native events, timers and
posted tasks and renders dirty windows. iOS breaks that in two ways:

1. UIKit owns the main thread's run loop through `UIApplicationMain`, which
   never returns.
2. No window can be shown before the system connects a `UIWindowScene`, and
   that happens only after `UIApplicationMain` has started.

There are two ways to fit `Run()` onto that. The second already exists in
the tree.

**A. Nested pump (recommended for phase 1; zero core changes).**
`UltraCanvasiOSMain.mm` provides `main()`, which calls `UIApplicationMain`.
When the scene connects, the delegate creates the root `UIWindow` and view
controller. It then *schedules* `ultracanvas_app_main(argc, argv)` on the
main queue (`dispatch_async`) rather than calling it from
`scene:willConnectToSession:`. A launch callback that does not return is
killed by the system's launch watchdog. `ultracanvas_app_main` runs the
application's unchanged body. Its windows attach to the root view
controller, and `Run()` blocks. `CollectAndProcessNativeEvents` runs the main
run loop for one turn (`CFRunLoopRunInMode` in the default mode, returning
after one source), with the timeout from `GetTimeUntilNextTimer()`. UIKit's touch, keyboard and
lifecycle sources fire inside that nested turn, so their callbacks queue
`UCEvent`s as the macOS backend's do. The macOS backend already waits this
way (`UltraCanvasMacOSApplication.mm:293`), and its `CFRunLoopSource` wakeup
(`:335-358`) ports unchanged. SDL's UIKit backend has run applications this
way for years (an outside reference; nothing in this repository depends on
it). This is the closest analogue to Android's `android_main` thread.

**B. Tick from the display (later, when frame pacing matters).**
`RunBeforeMainLoop()` (`include/UltraCanvasApplication.h:648`) is the hook
the WebAssembly backend uses: `Run()` calls it just before its loop, and
`emscripten_set_main_loop` takes the loop over
(`OS/WASM/UltraCanvasWASMApplication.cpp:288-296`). On iOS it would start a
`CADisplayLink` and return control to UIKit. Each display frame then runs one
iteration: events, timers, tasks and `UpdateAndRender`. The loop paces itself
to the screen, including ProMotion's 120 Hz, and never nests. It needs the
application's `main()` to have returned to UIKit by then, which is the same
"`Run()` must not block" contract the WebAssembly port already lives with.

**Lifecycle**, handled inside the backend as Android does
(`OS/Android/UltraCanvasAndroidApplication.cpp:394-434`), with no new
base-class virtuals:
- `sceneDidEnterBackground:`: stop rendering and stop any GL work. iOS
  terminates an app that submits GPU work in the background. Flush settings,
  because a backgrounded app can be killed without another callback.
- `sceneWillEnterForeground:`: mark every window dirty.
- Size changes (rotation, split view, Stage Manager):
  `viewWillTransitionToSize:` → `DoResizeNative` → `RecreateNativeSurface`.
- `Run()` exits when `windows.empty()`. On iOS the user never closes the last
  window, so the root window must not be destroyed by a scene disconnecting.
  A disconnected scene drops its surface and keeps the window, like
  Android's `APP_CMD_TERM_WINDOW`.

### 3.3 Presentation and HiDPI

The core renders each window into an offscreen Cairo surface and composites
it onto the backend's `nativeSurface` (§2 of the Android investigation). On
iOS:

- **`nativeSurface` is a `cairo_image_surface_create(ARGB32)`**, the
  Windows / Android model, **not** `cairo_quartz_surface_create` as on macOS.
  `cairo-quartz.h` includes `<ApplicationServices/ApplicationServices.h>`,
  which the iOS SDK does not have, so cairo's Quartz surface needs patching
  to build for iOS at all. An image surface needs nothing from cairo's
  platform code.
- **Present:** in `InvalidateWindowNative()`, wrap the surface's pixels in a
  `CGImage` (`CGDataProvider` over the buffer, premultiplied BGRA, which is
  cairo's ARGB32 on a little-endian CPU) and set it as the view layer's
  `contents`. Equivalently, draw it in `drawRect:` as the macOS view does
  (`UltraCanvasMacOSWindow.mm:106-121`). Use `layer.contents` with
  `contentsScale` set to the screen scale. It avoids a second copy and is
  what CoreAnimation composites anyway.
- **Scale:** `QueryNativeDeviceScale()` returns the view's
  `traitCollection.displayScale` (2 or 3). The shared `deviceScale` /
  `LogicalToPhysical` machinery does the rest
  (`include/UltraCanvasWindow.h`), as on Android.
- **Safe area:** the window's logical size is the view's bounds. The
  application lays out inside `safeAreaInsets`, which the backend should
  expose. That is new API, and it belongs on the window base so Android's
  display cut-outs can use it too.

### 3.4 Input

- Touch: `touchesBegan/Moved/Ended/Cancelled:` → `TouchStart` /
  `TouchMove` / `TouchEnd`, with `pointerId` taken from the `UITouch`
  identity (a per-gesture map to small integers). Pointer-0 mouse synthesis
  follows the Android rule: it stops for the rest of a gesture once a second
  finger lands. The core does this already.
- Gestures: two-finger pinch and rotate → `PinchZoom`, computed from the raw
  touches as Android does. `UIPinchGestureRecognizer` would swallow the
  touches the element under the finger needs.
- Apple Pencil: `UITouch.force` / `maximumPossibleForce` → `UCEvent::pressure`.
- iPad trackpad and mouse: `UIHoverGestureRecognizer` → `MouseMove` without
  a button; `UIEvent.buttonMask` for the right button.
- Hardware keyboard: `pressesBegan:` / `pressesEnded:` → `KeyDown` / `KeyUp`.
  The mapping table is the macOS `kVK_*` one translated to
  `UIKeyboardHIDUsage`.

### 3.5 Files, sandbox and paths

- Paths stay UTF-8 through `PathToUtf8` / `PathFromUtf8`
  (`UltraCanvasPathUtf8.h`); `NSString.fileSystemRepresentation` is UTF-8 on
  Apple platforms.
- `HOME` points at the app container, and `TMPDIR` is set by the system. The
  backend sets `XDG_CACHE_HOME` / `XDG_CONFIG_HOME` under `Library/`, as
  Android's `ExportSandboxEnvironment` does, so path-based settings and cache
  code works unchanged.
- Every load and save still goes through `UltraCanvasFileLoader`. The
  document picker is the only way out of the sandbox, which is why saving is
  `SaveContent` and not `SaveFile` (§3.1).
- `GetExecutableDir`: `NSBundle.mainBundle.bundlePath`, not
  `_NSGetExecutablePath` + `..`. The executable sits at the bundle's root on
  iOS, not in `Contents/MacOS/`.
- `OpenURL`: `[UIApplication openURL:options:completionHandler:]`.

### 3.6 Fonts

fontconfig builds for iOS. The core's `SetupBundledFontconfig()`
(`core/UltraCanvasApplication.cpp:960`) writes a runtime `fonts.conf` for
platforms without a system one, and is reused as on Android. Bundle the
fonts. Reading `/System/Library/Fonts` on iOS is not a documented interface,
and its contents change between releases. If the system fonts are wanted,
the supported route is CoreText, which harfbuzz's `coretext` feature
already uses on macOS. That would be a later Pango backend change, not a
fontconfig one. Defaults: the bundled UI font, or "SF Pro" through CoreText
once that route exists.

---

## 4. Building the dependencies for iOS

There is no cross-build for Apple mobile today. The macOS deps are Homebrew
locally or vcpkg in CI (`cmake/UltraCanvasMacOSDeps.cmake`,
`scripts/macos-deps.sh`, `MacOS/deps/`), and Android's sysroot is vcpkg with
the NDK chainloaded (`scripts/android-bootstrap-sysroot.sh`,
`UltraCanvas/OS/Android/packaging/vcpkg.json`). iOS follows the Android
route with vcpkg's iOS triplets.

### 4.1 Layout

| Piece | Where | Modelled on |
|---|---|---|
| manifest | `UltraCanvas/OS/iOS/packaging/vcpkg.json` | the Android manifest: cairo, pango, fontconfig, freetype, harfbuzz, glib, tinyxml2, fmt, libvips; then openssl, c-ares, sqlite3 |
| triplets | `UltraCanvas/OS/iOS/packaging/triplets/arm64-ios-ultracanvas.cmake`, `arm64-ios-simulator-ultracanvas.cmake` | `MacOS/deps/triplets/arm64-osx-ultracanvas.cmake` |
| overlay ports | reuse `MacOS/deps/ports/` (libvips, librevenge, mupdf, zbar) where they apply | the macOS overlays |
| driver | `scripts/ios-bootstrap-sysroot.sh` | `scripts/android-bootstrap-sysroot.sh` and `scripts/macos-deps.sh` (same pinned vcpkg commit) |

A triplet, in outline (not yet written or tried):

```text
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 16.0)
# simulator triplet adds: set(VCPKG_OSX_SYSROOT iphonesimulator)
```

Linkage is **static**. A dynamic library on iOS must be wrapped in a signed
`.framework` inside the app bundle. That is possible, but it multiplies the
packaging work for no gain, since there is only one process to share the
libraries with. The deployment target is a decision to make with the
packaging (16.0 is a placeholder).

Device and simulator are two builds: same architecture, different SDKs.
Bundling both into an `.xcframework` is only needed when the core is
distributed as a binary; an app built from source picks one per
configuration.

### 4.2 The stack, dependency by dependency

| Dependency | iOS notes |
|---|---|
| freetype, harfbuzz, fmt, tinyxml2, zlib | straightforward; zlib is in the SDK |
| glib | the hardest one, as on Android. It needs a meson cross file for iOS; `gspawn` (no `fork`/`exec` on iOS) and the GIO modules must be built out or left unused. Check whether vcpkg's glib port builds for `arm64-ios` as it is |
| fontconfig | builds; needs the runtime `fonts.conf` (§3.6) and a cache directory under `Library/Caches` |
| cairo | image surface only. Turn the Quartz surface and font backends off (they include ApplicationServices); no XLib, no Win32 |
| pango (+pangocairo) | the fontconfig/freetype backend, as on Android |
| libvips | part of UltraCanvas on every platform (decided 2026-10-15, see the Android investigation §4.2). Start with the codecs Android carries (JPEG, PNG, WebP, TIFF, EXIF, lcms, GIF, Netpbm, SVG through librsvg 2.40) and add HEIF next: HEIC is the iPhone camera's format |
| openssl | vcpkg builds it for iOS; needed for UltraNet TLS (§3.1) and the vendored curl |
| c-ares | replaces the resolver file, as on Android |
| sqlite3 | the SDK has `libsqlite3`, but vendoring the amalgamation keeps every platform on the same version |
| libcurl | the vendored `third_party/curl` against the OpenSSL above; its Linux-only selection logic (the `objdump -T` probe) must not run |
| miniaudio | vendored, and already has the iOS path (`AVAudioSession`, `MA_APPLE_MOBILE`); no build work, but the app must configure the audio session category |
| GStreamer | not used; video is AVFoundation, which iOS has. `libspecific/Video/VideoBackendAVFoundation.mm` already draws into a raw `CGBitmapContext` rather than an AppKit view, so it is a candidate to compile as it is |
| MuPDF, tesseract/leptonica, zbar, libcdr/ICU, MicroTeX, the Rust vectorizer | off for phase 1; each is a later, separate decision. MuPDF has its own iOS build; the vectorizer needs a Rust `aarch64-apple-ios` target |

### 4.3 No plug-in DSOs

The element, chart and UltraNet plug-ins are `MODULE` libraries `dlopen`ed at
run time. iOS allows loading only the signed frameworks inside the app
bundle, and App Review rejects downloaded code. So:
- `ULTRACANVAS_BUILD_SHARED` defaults to OFF on iOS, as on Android and
  WebAssembly (`UltraCanvas/CMakeLists.txt:72`).
- `ULTRACANVAS_CHART_RUNTIME_MODULES` is forced OFF
  (`UltraCanvas/CMakeLists.txt:1289` does it for WebAssembly and Android;
  add iOS to that condition).
- Every format plug-in an iOS app wants is compiled in; UltraNet protocol
  plug-ins likewise.

---

## 5. The GL / Metal decision

**What needs a GPU at all.** UltraCanvas's 2D rendering is Cairo on the CPU,
presented as an image (§3.3). That is already how Windows and Android work,
and it does not need Metal or GL. The only GPU users are the GL surfaces
(`ULTRACANVAS_ENABLE_GL`: `UltraCanvasGLSurface`, the 3D chart elements such
as `ContourSurface3D`, `GLFramebuffer`), which degrade to "unavailable" when
GL is off.

**The options for GL surfaces:**

| Option | For | Against |
|---|---|---|
| 1. **GL off** | nothing to build; GL elements already degrade | no 3D charts or GL surfaces on iOS |
| 2. OpenGL ES 3.0 through EAGL (`EAGLContext`, `GLKit`) | in the SDK; the GLES code paths already exist for Android (`GLFramebuffer.cpp`'s `__ANDROID__` arm) | deprecated since iOS 12, which warns at every build and can be removed in any release; EAGL is not EGL, so it needs a third context manager of its own |
| 3. **ANGLE on its Metal back end** (GLES 3 + EGL over Metal) | the EGL context manager Android uses (`OS/Android/GLContextManagerEGL_Android.cpp`) ports nearly as it is; the GLES headers are the ones Android includes; Metal underneath is Apple's supported API | ANGLE is a large dependency to build for iOS (it is not in vcpkg for iOS today, so a script of our own); adds to `Docs/Dependencies.md`, `master_dependencies.yaml` and `THIRD_PARTY_LICENSES.md` (house rule 5) |
| 4. A native Metal render path | the best performance | every GL surface and element rewritten against a second graphics API, for one platform. Out of proportion |

**Recommendation: 1 now, 3 when 3D on iOS is wanted, never 2 or 4.**
Phase 1 to 2 configure with `ULTRACANVAS_ENABLE_GL=OFF`. When GL surfaces
are wanted, ANGLE's Metal back end keeps one GL code path (GLES 3 over EGL)
shared by Android and iOS, and Apple's deprecation of OpenGL ES no longer
matters. EAGL would be work spent on an API Apple has scheduled for removal.
Rendering 2D through Metal (uploading the Cairo surface as an `MTLTexture`
each frame) is a presentation detail that can replace `layer.contents`
later if profiling asks for it. It is not a reason to start with Metal.

---

## 6. The CMake branch

### 6.1 What is there now

`UltraCanvas/CMakeLists.txt:179` tests `CMAKE_SYSTEM_NAME STREQUAL "iOS"`
before `APPLE`, which an iOS toolchain also sets, and stops with a
`FATAL_ERROR` that names this document. Without it, an iOS configure picks
`OS/MacOS` and fails much later on Cocoa, IOKit or CoreWLAN. The root
`CMakeLists.txt` already skips UltraStore when `IOS` is set (`:1038`).

### 6.2 What the branch becomes

When `OS/iOS/` exists, the `FATAL_ERROR` is replaced by:

```text
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(ULTRACANVAS_PLATFORM "iOS")
    set(ULTRACANVAS_PLATFORM_DIR "OS/iOS")
```

and these follow:

1. **Objective-C++ sources.** The `*.mm` glob and the
   `LANGUAGE OBJCXX` / `-fobjc-arc` properties at
   `UltraCanvas/CMakeLists.txt:1829` are inside
   `if(ULTRACANVAS_PLATFORM STREQUAL "MacOS")`; that becomes
   `MacOS OR iOS`. `enable_language(OBJCXX)` already runs for any `APPLE`.
2. **Frameworks.** A new `elseif(ULTRACANVAS_PLATFORM STREQUAL "iOS")` arm
   beside the macOS `find_library` block: UIKit, Foundation, CoreGraphics,
   QuartzCore, CoreText, CoreFoundation, Security, UniformTypeIdentifiers,
   AVFoundation, CoreMedia, CoreVideo, AudioToolbox. Not Cocoa, AppKit,
   IOKit, CoreWLAN, IOBluetooth, DiskArbitration, ScreenCaptureKit,
   ImageCaptureCore or CoreServices. The link line at the end
   (`"-framework AppKit"`) gets its own iOS arm.
3. **The 17 `APPLE` tests**, each read once. Some mean "any Apple" (the
   `-dead_strip_dylibs` link option, OBJCXX) and stay as they are. Some mean
   macOS (bundles, `Contents/MacOS`, notarisation, Homebrew) and become
   `APPLE AND NOT IOS`.
4. **`cmake/UltraCanvasMacOSDeps.cmake`** returns early for `NOT APPLE`; it
   must also return for `IOS` (it runs `brew --prefix`, whose answer is
   macOS libraries). A sibling `cmake/UltraCanvasiOSDeps.cmake` points
   `CMAKE_FIND_ROOT_PATH` and `PKG_CONFIG_LIBDIR` at the vcpkg prefix (§4),
   as the WebAssembly build does with `cmake/UltraCanvasWasmSysroot.cmake`.
5. **Defaults:** `ULTRACANVAS_BUILD_SHARED` OFF, chart runtime modules OFF,
   plug-ins that need an unbuilt dependency OFF, `ULTRACANVAS_ENABLE_GL` OFF
   until §5 option 3.
6. **Applications:** the root `CMakeLists.txt` builds an `.app` only for the
   applications meant for iOS (DemoApp first), with
   `MACOSX_BUNDLE`, an iOS `Info.plist` (`UILaunchScreen`,
   `UIApplicationSceneManifest`, supported orientations) and
   `XCODE_ATTRIBUTE_DEVELOPMENT_TEAM` left to the developer. Desktop-only
   applications are skipped on `IOS` the way UltraStore already is.

A configure for the simulator would then read:

```bash
cmake -B build-ios-sim -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 -DULTRACANVAS_IOS_DEPS_PREFIX=<vcpkg prefix>
```

(`ULTRACANVAS_IOS_DEPS_PREFIX` does not exist yet; the name follows
`ULTRACANVAS_MACOS_DEPS_PREFIX`.)

---

## 7. Packaging, signing and CI

- **Simulator first.** A simulator build needs no signing and no Apple
  developer account, and runs on the GitHub macOS runners
  (`xcrun simctl boot`, `install`, `launch`, `io screenshot`). That is the
  iOS counterpart of `scripts/android-emulator-smoke.sh`, and it should land
  as an `ios-build.yml` workflow: build the sysroot (cached like Android's),
  build DemoApp for the simulator, launch it, and fail on a crash or a blank
  screenshot.
- **Device builds** need a development team and a provisioning profile.
  Those are secrets of whoever ships, so CI stops at an unsigned device
  build (`CODE_SIGNING_ALLOWED=NO`), which proves it compiles and links.
- **Store builds** add the privacy manifest (`PrivacyInfo.xcprivacy`, which
  lists the "required reason" APIs: file timestamps, disk space, user
  defaults), export compliance for the cryptography, and the review rule
  against downloaded code (§4.3).
- Rule 8 of *Branch and pull-request rules* in `AGENTS.md`: every pull
  request of this port is platform-specific and gets the `ci:distro` label.

---

## 8. Phased plan

| Phase | Delivers | Exit test |
|---|---|---|
| 0 | this document; the `#error` and `FATAL_ERROR` guards | done |
| 1 | `scripts/ios-bootstrap-sysroot.sh`, triplets and manifest; the core library compiles and links for `arm64-ios-simulator` with GL off; the `__APPLE__` audit of §2 | `ios-build.yml` builds the core library |
| 2 | `OS/iOS/` application, window, main, touch, run loop (§3.2 option A), presentation (§3.3); DemoApp as an `.app` | DemoApp starts in the simulator, draws, and scrolls under a simulated touch |
| 3 | text input with composition, clipboard, message dialogs, document picker open and `SaveContent`, host appearance, language | a text field takes input from the software keyboard in the simulator; a document opens and saves through the picker |
| 4 | UltraNet (OpenSSL TLS with system roots, c-ares), audio (session category), AVFoundation video | a TLS fetch succeeds; a video plays |
| 5 | GL surfaces through ANGLE/Metal (§5 option 3); option B run loop if frame pacing is wanted; iPad pointer and Pencil | the GL demo pages render |
| 6 | a device build, signing documented, the privacy manifest | runs on an iPhone and an iPad |

Phase 1 is almost entirely build work and can proceed without a single line
of UIKit. It also tells, early, whether glib and libvips are a day's work on
iOS or a week's.

---

## 9. Key file index

| What | Where |
|---|---|
| iOS stops in the headers | `UltraCanvas/include/UltraCanvasWindow.h:806`, `UltraCanvas/include/UltraCanvasApplication.h:731` |
| iOS stops in CMake | `UltraCanvas/CMakeLists.txt:179` |
| macOS backend (Objective-C++ template) | `UltraCanvas/OS/MacOS/` |
| macOS run-loop wait and wakeup | `UltraCanvas/OS/MacOS/UltraCanvasMacOSApplication.mm:271-358` |
| macOS view presentation | `UltraCanvas/OS/MacOS/UltraCanvasMacOSWindow.mm:106-121`, `:644` |
| Android backend (mobile template) | `UltraCanvas/OS/Android/`, its `README.md` |
| Android entry and `ultracanvas_app_main` | `UltraCanvas/OS/Android/UltraCanvasAndroidMain.cpp` |
| Android lifecycle | `UltraCanvas/OS/Android/UltraCanvasAndroidApplication.cpp:394-434` |
| WebAssembly's loop takeover (`RunBeforeMainLoop`) | `UltraCanvas/OS/WASM/UltraCanvasWASMApplication.cpp:288-296`, `include/UltraCanvasApplication.h:648` |
| Service fallbacks | `include/UltraCanvas*Backend.h` (`ULTRACANVAS_<X>_NATIVE`), e.g. `UltraCanvasHardwareInfoBackend.h:20` |
| macOS dependencies | `cmake/UltraCanvasMacOSDeps.cmake`, `scripts/macos-deps.sh`, `MacOS/deps/` |
| Android dependencies | `scripts/android-bootstrap-sysroot.sh`, `UltraCanvas/OS/Android/packaging/vcpkg.json` |
| Shared-core and chart-module defaults | `UltraCanvas/CMakeLists.txt:72`, `:1289` |
| Framework comparison (says "No iOS") | `Docs/UltraCanvasComparison.md:73`, `:210` |
