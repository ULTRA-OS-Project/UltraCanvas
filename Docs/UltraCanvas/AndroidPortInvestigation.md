# Android Port Investigation

**Status:** Investigation / planning document. Written before any Android code
existed; the analysis below is preserved as the original audit, with
**Status:** notes marking what has since been implemented. The backend now
lives in `UltraCanvas/OS/Android/` — see its
[README](../../UltraCanvas/OS/Android/README.md) for what is actually built
today. Phases 0–3 are largely done (backend, lifecycle, GLES, clipboard, soft
keyboard, multi-touch, message dialogs, SAF file opening); the dependency
sysroot, APK packaging, full IME and saving through SAF remain — the last of
those blocked on a cross-platform API decision, see §3.5.
**Goal:** Bring UltraCanvas to Google Android with the same OS-level support the
framework has on Linux (windowing, rendering, input, clipboard, dialogs,
networking, fonts, audio/video, GL).

This document records a full audit of the codebase (platform abstraction, the
Linux backend, prior porting attempts, dependencies, build system and CI) and
derives a concrete, ordered implementation plan.

---

## 1. Executive summary

- **Feasibility: yes, with substantial but well-scoped work.** The framework's
  core (widgets, layout, dirty-rect rendering, event dispatch, timers) is
  platform-neutral C++20, and the Cairo render pipeline draws into plain image
  surfaces — only **one** call in the whole tree ties rendering to X11
  (`cairo_xlib_surface_create` in `OS/Linux/UltraCanvasLinuxWindow.cpp:286`).
- **Hook points already exist but are broken.** `UltraCanvasApplication.h:331`
  and `UltraCanvasWindow.h:478` already contain `#elif defined(__ANDROID__)`
  branches pointing at a non-existent `OS/Android/` directory — and they are
  **unreachable**, because the preceding branch tests `__linux__` first and the
  Android NDK defines `__linux__`. Today an NDK build would silently select the
  X11 backend and fail on `<X11/Xlib.h>`. The same trap exists in CMake:
  `CMAKE_SYSTEM_NAME=Android` satisfies `UNIX`, so platform detection picks
  `OS/Linux` and requires X11/GTK3.
- **There is no usable prior art in-tree.** Both `OS/WASM/` and `OS/BSD/` are
  dead code: never referenced by the build, written against base-class APIs
  that no longer exist, and uncompilable (details in §6). The Android port will
  be the first real non-desktop backend. The **macOS backend is the best
  structural template** (offscreen surface + explicit present + externally
  owned run loop), not Linux.
- **The two big cost centers** are (a) cross-compiling the rendering dependency
  stack (cairo, pango, glib, harfbuzz, fontconfig, freetype) for the NDK —
  there is currently zero cross-compilation infrastructure — and (b) replacing
  the GTK3/X11 desktop services (file dialogs, clipboard, drag-and-drop,
  cursors ≈ 2,750 lines on Linux) with JNI bridges to Android APIs.
- **Recommended de-scoping for a first milestone:** make libvips optional
  (guards already exist), skip drag-and-drop and cursors, stub native dialogs,
  disable video and most plugins, translate touch to mouse events. That yields
  a running DemoApp-class application; the remaining subsystems layer on
  incrementally.

Effort landmarks from the audit: the Linux backend is **6,555 lines** across
16 files; a first Android milestone needs roughly the Application/Window pair
(~1,500–2,000 lines of new code plus JNI glue) after the dependency sysroot
exists. The full render-context interface is already implemented by the shared
Cairo backend, so **no new render context is required** as long as Cairo is
cross-compiled (the WASM port's attempt to re-implement `IRenderContext` — 99
pure virtuals, plus 58 on `ITextLayout` — is exactly the trap to avoid).

---

## 2. How platform support works today

UltraCanvas selects platforms at **compile time via typedef aliases**, not a
runtime factory:

- `UltraCanvasApplicationBase` (`include/UltraCanvasApplication.h:59-309`) and
  `UltraCanvasWindowBase` (`include/UltraCanvasWindow.h:89-448`) are abstract
  bases living in core.
- At the bottom of each public header, an `#ifdef` chain includes the per-OS
  subclass and aliases it:
  `using UltraCanvasApplication = UltraCanvasLinuxApplication;` etc.
- CMake mirrors this: `UltraCanvas/CMakeLists.txt:96-107` maps
  `WIN32/APPLE/UNIX` to `OS/MSWindows`, `OS/MacOS`, `OS/Linux` and globs
  `${ULTRACANVAS_PLATFORM_DIR}/*.cpp` (lines 633-640).
- Core code calls the **concrete alias** directly, including the static
  `UltraCanvasApplication::GetInstance()` — which is declared in each backend,
  *not* in the base class. A new backend must therefore provide the statics
  too, not just the pure virtuals.

The main loop is app-owned and blocking (`core/UltraCanvasApplication.cpp:450-548`):

```
while (running && !windows.empty()):
    CollectAndProcessNativeEvents()   // backend event pump, blocks with timeout
    ProcessEvents()                   // drain UCEvent queue -> DispatchEvent
    ProcessTimers(); ProcessPostedTasks()
    for each visible window: UpdateAndRender()   // dirty rects only
    clipbrd->Update()
```

Rendering per window (`core/UltraCanvasWindow.cpp:438-575`): widgets render
into an **offscreen** Cairo surface (`renderContext`), which is then composited
onto the window's `nativeSurface` (`FlushToSurface`, popups, caret, tooltip)
and presented via the backend's `InvalidateWindowNative()`. The three existing
backends attach `nativeSurface` differently:

| Backend | `nativeSurface` | Present step |
|---|---|---|
| Linux | `cairo_xlib_surface_create` — draws straight into the X window | no-op (server-side) |
| Windows | `cairo_image_surface_create(RGB24)` | `SetDIBitsToDevice` blit |
| macOS | `cairo_quartz_surface_create` | view `drawRect` |

**Android maps cleanly onto the Windows/macOS model:** a Cairo image surface,
presented by `ANativeWindow_lock` → row copy → `ANativeWindow_unlockAndPost`
inside `InvalidateWindowNative()`. Zero core changes needed for presentation.

HiDPI is already generic: `deviceScale`, `QueryNativeDeviceScale()` (override
returns `densityDpi / 160`), `LogicalToPhysical`/`PhysicalToLogical`, and
`cairo_surface_set_device_scale` all live in shared code
(`include/UltraCanvasWindow.h:155-210`, `core/UltraCanvasWindow.cpp:371-408`).
This part of the design is mobile-ready.

---

## 3. Gap analysis: what Android must implement or replace

### 3.1 Platform-selection fixes (prerequisite, small)

The `__ANDROID__` test must come **before** any `__linux__`/`__unix__` test,
because bionic defines `__linux__`. Sites needing an Android arm:

| Site | Issue |
|---|---|
| `include/UltraCanvasApplication.h:312` | `__linux__` branch shadows the existing `__ANDROID__` branch at `:331` |
| `include/UltraCanvasWindow.h:451` | same, shadows `:478` |
| `include/UltraCanvasNativeHandle.h:11` | Android inherits `unsigned long` (X11 XID); should be `void*` for `ANativeWindow*` |
| `include/UltraCanvasEvent.h:329-337` | native handle init switch |
| `include/UltraCanvasCairoDebugExtension.h:12,83` | `#ifdef __linux__` would wrongly activate |
| `core/UltraCanvasClipboard.cpp:15,90` | `#ifdef __linux__` instantiates the X11 clipboard backend |
| `core/UltraCanvasConfig.cpp:32` | `SetResourcesDir` Linux probe (Android: assets/APK paths) |
| `core/UltraCanvasUtils.cpp:521,564` | `GetExecutableDir` reads `/proc/self/exe` (returns the zygote path on Android); `OpenURL` needs a JNI Intent |
| `core/UltraCanvasApplication.cpp:125-383` | `SetupBundledFontconfig` needs an Android arm (app-private cache dir + `/system/fonts`) |
| `libspecific/GL/GLFramebuffer.cpp:6-16` | GL header include falls into desktop `GL/gl.h`; needs a GLES arm |
| `UltraCanvas/CMakeLists.txt:96-107` | add `if(ANDROID)` **before** `elseif(UNIX)`; Android must not require X11/GTK3/desktop OpenGL |

Note also that ~30 further `if(ULTRACANVAS_PLATFORM STREQUAL "Linux")` branches
in `UltraCanvas/CMakeLists.txt` (deps, UltraNet source lists, link lines,
vendored-curl selection with its `objdump -T` probe) must be audited so none of
them fires for Android.

### 3.2 New backend files: `UltraCanvas/OS/Android/`

The CMake glob picks up the directory automatically once the detection arm
exists. Required files and their contracts:

| File | Contract |
|---|---|
| `UltraCanvasAndroidApplication.{h,cpp}` | 13 pure virtuals of `UltraCanvasApplicationBase` (`InitializeNative`, `ShutdownNative`, `CollectAndProcessNativeEvents`, `WakeUpEventLoop`, `InitializeWakeUp`, `ShutdownWakeUp`, `CaptureMouseNative`, `ReleaseMouseNative`, 2× `SelectMouseCursorNative`, `DetectSystemFontStyleNative`, `DetectMonospacedFontStyleNative`, `LoadBundledFontsNative`) **plus** `static GetInstance()` / `static instance` |
| `UltraCanvasAndroidWindow.{h,cpp}` | 19 pure virtuals of `UltraCanvasWindowBase` (`CreateNative`, `DestroyNative`, `DoResizeNative`, `RecreateNativeSurface`, `InvalidateWindowNative`, `Show`, `Hide`, `RaiseAndFocus`, `SetWindowTitle`, `SetWindowIcon`, `SetWindowPosition`, `SetWindowSize`, `Minimize`, `Maximize`, `Restore`, `SetFullscreen`, `SetResizable`, `GetScreenSize`, `GetNativeHandle`) + `QueryNativeDeviceScale()` override. Most window-management calls become no-ops on Android |
| `UltraCanvasAndroidClipboard.{h,cpp}` | 15 virtuals of `UltraCanvasClipboardBackend` (`include/UltraCanvasClipboard.h:89-127`) via JNI `ClipboardManager`. Far simpler than X11: no selections, no TARGETS, no lazy owner-serves model; `OnPrimaryClipChangedListener` replaces the 100 ms polling |
| `UltraCanvasAndroidNativeDialogs.cpp` | 17 static functions of `UltraCanvasNativeDialogs` (link-time selection, no vtable). See §3.5 — hardest impedance mismatch |
| `UltraCanvasAndroidCursor.cpp` | 2 stubs (`return true`; Android has no persistent cursor) |
| `UltraCanvasAndroidFileLoader.cpp` | `NotifyRecentFile` stub |
| `GLContextManagerEGL_Android.cpp` | Near-copy of `GLContextManagerEGL_Linux.cpp` (201 lines, zero X11 references) with `eglBindAPI(EGL_OPENGL_ES_API)`, `EGL_OPENGL_ES3_BIT`, and the desktop core/compat profile attribs removed |
| `UltraNetSupport.cpp` | Copy of the Linux one (pure `getenv`, fully bionic-compatible); optionally improved later via JNI `ConnectivityManager` proxy query |
| `UltraNetTlsImpl.cpp` | Copy of the Linux OpenSSL implementation; must ship a CA bundle (`SSL_CTX_set_default_verify_paths` finds nothing in the app sandbox) or bridge to Android's trust store via JNI |
| `UltraNetDnsImpl.cpp` | Do **not** port the Linux one — it uses `res_ninit`/`res_nquery`/`ns_parserr`, which bionic does not export, and links `-lresolv`, which doesn't exist on Android. Use the existing **c-ares** path instead: when `ULTRANET_HAS_CARES` is set, `core/UltraNet/UltraNetDnsCares.cpp` supplies the resolver and the per-platform DNS file is excluded entirely. Make c-ares mandatory for the Android target |

### 3.3 Event loop and lifecycle

The Linux pump (`OS/Linux/UltraCanvasLinuxApplication.cpp:215-272`) is: drain
`XPending` → `select()` on `{X11 fd, eventfd}` with a timeout from
`GetTimeUntilNextTimer()` → drain again. On Android:

- **`ALooper` is the direct analogue.** With `NativeActivity` +
  `android_native_app_glue`, `android_main()` runs on its own thread and can
  host the framework's blocking `Run()` loop; `CollectAndProcessNativeEvents`
  becomes `ALooper_pollOnce(timeoutMs)` processing input queue + app commands.
- The **eventfd wakeup ports almost verbatim** — eventfd exists in bionic and
  can be registered with `ALooper_addFd`, so `PostToUIThread` keeps working.
- **Lifecycle is the real gap.** Nothing in `UltraCanvasApplicationBase` or
  `UltraCanvasWindowBase` models pause/resume, surface destroyed/recreated, or
  configuration change. `APP_CMD_TERM_WINDOW`/`APP_CMD_INIT_WINDOW` means
  `nativeSurface` can vanish under a live window; `RecreateNativeSurface()`
  (`OS/Linux/UltraCanvasLinuxWindow.cpp:407-420` shows the pattern) is the
  right hook but nothing calls it on surface loss. Also `Run()` exits when
  `windows.empty()` (`core/UltraCanvasApplication.cpp:466`) — rotation-driven
  activity teardown must not be interpreted as "quit". **New base-class
  virtuals are needed** (e.g. `OnAppPause/OnAppResume/OnSurfaceLost/
  OnSurfaceRestored`), defaulted to no-ops so desktop backends are untouched.
- Frame pacing: the desktop loop has none (event/timer driven). Acceptable for
  a first milestone; `AChoreographer_postFrameCallback` can gate composition
  later.

### 3.4 Input: touch, keyboard, IME

- `UCEventType::TouchStart/TouchMove/TouchEnd/Tap/PinchZoom` exist as enum
  values (`include/UltraCanvasEvent.h:50-54`) but **no backend produces them
  and no core code consumes them** — `DispatchEvent` has no touch path and
  `IsTouchEvent()` is never called. `UCEvent` has a `pressure` field but no
  pointer-ID field, so multi-touch cannot currently be represented.
- **Recommended first approach (zero core changes):** translate
  `AMOTION_EVENT_ACTION_DOWN/MOVE/UP` of pointer 0 into
  `MouseDown/MouseMove/MouseUp` (+ synthesized `MouseDoubleClick`, exactly as
  the Linux backend does in `MouseClickInfo`, and two-finger scroll →
  `MouseWheel`). This is what makes every existing widget usable immediately.
  Real multi-touch/gesture support (adding a pointer-ID slot to `UCEvent` and
  touch cases to `DispatchEvent`) is a later, deliberate core extension.
- **Status: both have landed.** `UCEvent` carries `pointerId` (stable per
  finger) and `touchPointCount`; `DispatchEvent` routes touch events to the
  element under that finger with bubbling and element-local coordinates, and
  the Android backend emits `TouchStart/TouchMove/TouchEnd` for every pointer.
  Single-finger gestures still drive the mouse path unchanged, so existing
  widgets are unaffected; mouse synthesis stops for the rest of a gesture as
  soon as a second finger lands. Gesture *recognition* (pinch/rotate → the
  `PinchZoom` event) is still open — the raw stream it needs now exists.
- Physical keys: `AKEYCODE_*` → `UCKeys` mapping table (the Linux
  `ConvertXKeyToUCKey`, ~130 cases, is the template).
- **IME/soft keyboard:** `UCEvent::text` is the UTF-8 delivery channel (XIM
  uses it on Linux via `Xutf8LookupString`), so committed text from
  `InputConnection` maps cleanly. But nothing in the framework requests a
  keyboard: a new hook is needed so that focusing a text widget calls
  `showSoftInput`/`hideSoftInput` via JNI. Composing (pre-edit) text has no
  representation and can be deferred.

### 3.5 Desktop services with no Android analogue

| Subsystem | Linux implementation | Android strategy |
|---|---|---|
| Native dialogs | 808 lines GTK3, **synchronous** (`gtk_dialog_run` nested loop) returning values | `AlertDialog` / SAF (`ACTION_OPEN_DOCUMENT`, `ACTION_CREATE_DOCUMENT`) are **callback-based**. Either the `UltraCanvasNativeDialogs` API gains async variants, or Android uses a nested-loop shim pumping `ALooper` until the Java side posts the result. Additionally SAF yields `content://` URIs, not filesystem paths — downstream code assuming `std::string` paths needs a URI bridge (open via `ContentResolver` → fd → `/proc/self/fd/N`, or copy-to-cache). Phase 1: stub dialogs. **Status:** the nested-loop shim landed — `UltraCanvasAndroidDialogBridge` shows the dialog through the optional `UltraCanvasActivity` and pumps activity commands (never input) until the Java UI thread posts the result, so the synchronous API is preserved. Message dialogs and SAF **opening** are done (documents are copied into the app cache so path-based callers keep working). `SaveFile`/`SelectFolder` remain stubs on purpose: this API returns a path and never signals when writing finished, so there is no commit point at which to push a cache copy back to the `content://` URI - closing that needs a cross-platform API decision. |
| Clipboard | 760 lines of X11 selection protocol | JNI `ClipboardManager` — much simpler, text + URI lists |
| Drag & drop | 960 lines XDnD v5 both directions | No cross-app analogue (outside ChromeOS freeform). `StartNativeFileDrag` → `return false` (default already does this); XDnD file is simply not compiled |
| Mouse cursors | 238 lines Xcursor/libvips | No-ops |
| Recent files | GtkRecentManager | No-op |
| Printing | GTK print dialog + `system("lpr …")` + `/tmp` | `PrintManager` via JNI, later; `/tmp` and `system()` are unusable on Android |
| Window management | `_NET_WM_*`, Motif hints, transient-for, title/icon | Mostly no-ops; `SetFullscreen` → immersive mode; `WindowCloseRequest` ↔ back button; multiple top-levels collapse to one `ANativeWindow` — the framework's in-process popup/overlay system (per-popup render contexts in `UpdateAndRender`) already covers menus/dropdowns/dialogs within the single surface |
| Fonts | fontconfig + `FcConfigAppFontAddFile`; defaults hardcoded `"Ubuntu"`/`"Ubuntu Mono"` | fontconfig **can** be built for NDK; reuse the existing `SetupBundledFontconfig()` runtime-`fonts.conf` mechanism (written for exactly the "no system fonts.conf" case on Windows), pointing at bundled fonts + `/system/fonts`. Defaults → Roboto / Droid Sans Mono |

### 3.6 Rendering and GL

- **Keep Cairo/Pango.** The shared `RenderContextCairo` (1,990 lines,
  implementing all ~99 `IRenderContext` + 58 `ITextLayout` pure virtuals) is
  backend-neutral (`cairo_image_surface_create(ARGB32)`), and core even casts
  `NativeSurfacePtr` to `cairo_surface_t*` directly
  (`core/UltraCanvasWindow.cpp:792`, `core/UltraCanvasCaret.cpp:127`) — so a
  non-Cairo Android backend would be a huge refactor for no benefit.
  Rendering is CPU-side; an `ANativeWindow` blit closes the loop.
- **GL surfaces:** the EGL context manager is the preferred backend on Linux
  already and EGL is native on Android; only the API/bit-mask constants change
  (GLES instead of desktop GL). `ULTRACANVAS_HAS_EGL` gets set for Android in
  CMake; GLX/WGL/CGL paths are untouched. GL-dependent chart elements
  (`ContourSurface3D` etc.) degrade automatically when GL is off — an
  acceptable phase-1 posture.

---

## 4. Dependency strategy for the NDK

There is **no cross-compilation infrastructure today** — no toolchain files,
no `CMAKE_CROSSCOMPILING` handling; every core dependency is found via system
pkg-config. The port needs a prebuilt Android sysroot (vcpkg's
`arm64-android` triplet is the first thing to evaluate; Conan or hand-rolled
meson cross-files are the fallback) with `PKG_CONFIG_LIBDIR` pointed at it.

### 4.1 Irreducible core stack (must cross-compile)

| Dependency | Android notes |
|---|---|
| freetype | Trivial — Android's own font stack is FreeType |
| harfbuzz | Well-supported (ships in Android itself) |
| glib-2.0 | Heaviest pain point after GTK; meson cross-build, known-workable but fiddly (iconv/locale) |
| fontconfig | Buildable; needs bundled `fonts.conf` + app-private cache dir (no `/etc/fonts`) |
| cairo, pango(+pangocairo) | Meson cross-builds; image-surface backend only |
| tinyxml2, fmt | Trivial (fmt is already FetchContent) |
| zlib | In the NDK sysroot |
| openssl | Well-trodden official NDK support; required for UltraNet TLS + vendored curl |
| c-ares | Cross-compiles cleanly; replaces the unportable libresolv DNS code |
| sqlite3 | Vendor the amalgamation (Android's system lib is not NDK-accessible; the CMake comment at the `find_library` fallback already anticipates vendoring) |
| libcurl | Use the **vendored** `third_party/curl` against the cross-built OpenSSL (`--enable-websockets`). The Linux-only vendored-curl selection logic (incl. the `objdump -T` probe) must not run for Android |

### 4.2 Deps to make optional / replace / drop for Android

| Dependency | Action |
|---|---|
| **libvips** (+ ~10 transitive libs) | Currently `REQUIRED` but `HAS_LIBVIPS`/`LIBVIPS_FOUND` guards already exist throughout — **make it optional** (drop `REQUIRED` at `UltraCanvas/CMakeLists.txt:146`, conditionalize the link). This is the single highest-leverage build change and benefits WASM too. Image decode on Android can later go through a smaller path (stb/libjpeg-turbo/libpng or JNI `BitmapFactory`) |
| X11/Xcursor/Xrandr, GTK3, GLX, desktop OpenGL | Never referenced by the Android platform arm |
| librsvg | Skip (Rust+gobject; SVG falls back gracefully) |
| GStreamer | Skip video for phase 1 (`VideoBackendNull` exists exactly for this); the right Android backend later is MediaCodec/ExoPlayer, not GStreamer-android |
| miniaudio | **Works on Android out of the box** (AAudio/OpenSL backends already in the vendored header) — audio is nearly free |
| Optional audio codecs (FLAC/vorbis/opus/LAME) | All plain C with known NDK builds; each just unlocks a format |
| MuPDF (PDF plugin) | Official Android build exists; defer to a later phase |
| tesseract/leptonica (OCR), zbar, CDR (libcdr/ICU), Vectorizer (Rust) | Defer / off by default for Android |

### 4.3 Plugin loading (`dlopen`) constraint

The element/chart/UltraNet plugin system builds `MODULE` DSOs `dlopen`ed at
runtime, resolving core symbols from the host executable via `ENABLE_EXPORTS`
+ `RTLD_GLOBAL`. On Android **there is no host executable** (the app is a `.so`
loaded by the VM), and `dlopen` of libraries outside the APK's
`lib/<abi>/` is blocked on modern API levels. Options:

1. **Phase 1: compile-time selection.** The chart system already has the
   fallback — `if(EMSCRIPTEN)` disables runtime chart modules
   (`UltraCanvas/CMakeLists.txt:535`); extend that condition to `ANDROID`.
   UltraNet protocol plugins likewise become compiled-in or off.
2. Later: package needed plugins as `jniLibs` inside the APK and build the
   core `SHARED` so modules link against it (the Windows model), instead of
   relying on `ENABLE_EXPORTS`.

### 4.4 Assets

`copy_assets` copies `media/`, `Docs/`, app resources next to the binary and
code `fopen`s them by path. In an APK these live in `assets/` behind
`AAssetManager`, which is not the filesystem. Pragmatic approach: on first
launch (or lazily) extract needed assets to `getFilesDir()`/`getCacheDir()`
and keep all existing path-based code working; `SetResourcesDir()`
(`core/UltraCanvasConfig.cpp`) gains an Android arm pointing there. Direct
`AAssetManager` streaming can come later where it matters (fonts, icons).

---

## 5. Bootstrap, packaging, CI

- **Entry point.** All apps use `int main(argc, argv)` (+ `WinMain` on
  Windows). Android adds `android_main(struct android_app*)` via
  `android_native_app_glue` (NativeActivity keeps the whole app in C++ — the
  right fit given the framework owns its event loop), which initializes
  `UltraCanvasApplication` with the `ANativeWindow` and calls the app's
  existing setup code. A thin per-app `#ifdef __ANDROID__` shim is enough.
- **Packaging.** Gradle project with `externalNativeBuild { cmake }` targeting
  the repo's root CMake, `jniLibs` for the cross-compiled dependency `.so`s
  (or a fully static link where licenses permit), `assets/` from `media/`,
  adaptive icons replacing the `.rc`/`.icns` machinery, `apksigner` replacing
  the desktop signing scripts. Note the `package-linux.sh` "exclude
  host-provided libs" logic inverts on Android: nothing is host-provided, so
  everything ships — another reason to trim libvips.
- **CI.** Add a **separate job** (not a matrix row): `ubuntu-latest` + NDK +
  cached prebuilt dependency sysroot, configure with
  `-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake
  -DANDROID_ABI=arm64-v8a`. The three "verify" gates in `build.yml` (OpenGL
  backend string, system-libcurl check — impossible on Android where vendored
  curl is the only option — and the Vectorizer/Rust gate) are all written for
  native host builds and must be conditioned on the target. Manifest must
  declare `INTERNET` permission or every socket call fails with `EACCES`.
- **UltraNet CMake.** The per-platform source list
  (`if(ULTRACANVAS_PLATFORM STREQUAL "Linux") … append OS/Linux/UltraNet*.cpp`)
  needs an Android arm; without it the build fails at link with unresolved
  `ultranet_tls_platform::` / `ultranet_dns_platform::` symbols. Drop
  `-lresolv` from the Android link line.

---

## 6. Warnings from prior porting attempts (WASM, BSD)

Both in-tree "ports" are non-building code drops; neither is a template.

- **`OS/WASM/`** — never wired into any CMakeLists (`add_subdirectory` absent);
  header dispatch uses `__WASM__` (a macro Emscripten never defines) and points
  at a non-existent `OS/Web/`; its window class overrides methods that no
  longer exist on the base; its render context implements ~51 of the required
  ~157 virtuals against deleted types (`DrawingStyle`, `ImageData`); contains
  outright non-compiling statements and dangling-pointer bugs; its README's
  "Production Ready" status and FPS benchmarks cannot have been real. Its three
  genuinely useful artifacts: the `WakeUpEventLoop`/`InitializeWakeUp`/
  `ShutdownWakeUp` no-op trio for externally-owned loops, the
  `if(EMSCRIPTEN)` no-dlopen chart fallback, and the wheel-delta normalization.
- **`OS/BSD/`** — headers override virtuals that no longer exist
  (`RunNative`, `ExitNative`), the promised `UltraCanvasBSDApplication.cpp`
  is missing, and `UltraCanvasBSDWindow.cpp` includes a header
  (`../Linux/UltraCanvasLinuxRenderContext.h`) that no longer exists. Real BSD
  support is achieved by the `__unix__` alias to the Linux X11 backend — a
  reuse pattern that does **not** transfer to Android (Android shares none of
  the five pillars: Xlib, XIM, XDnD, X selections, GTK3).

Process lessons for the Android effort:

1. **Land the CMake platform arm and CI job first**, so Android code is
   compiled from day one and can never rot into a WASM/BSD-style drop.
2. **Do not re-implement `IRenderContext`** — reuse the Cairo backend.
3. Keep every new `OS/Android/` file building against the *current* base
   classes in the same PR that adds it.
4. Consider deleting or clearly fencing `OS/BSD/` and `OS/WASM/` (plus fixing
   the dead `__WASM__`→`OS/Web` branches) so they stop masquerading as
   precedent.

---

## 7. Phased implementation plan

**Phase 0 — build foundations** (no Android code yet)
- Make libvips optional; conditionalize the required-deps block
  (`UltraCanvas/CMakeLists.txt:128-182`) per platform.
- Add `if(ANDROID)` platform arm + `OS/Android/` skeleton + NDK toolchain CI
  job that configures (even if it only builds core + stubs).
- Produce the dependency sysroot (vcpkg `arm64-android` evaluation:
  freetype, harfbuzz, glib, fontconfig, cairo, pango, tinyxml2, openssl,
  c-ares, sqlite3, zlib).

**Phase 1 — pixels on screen**
- `__ANDROID__` reordering fixes (§3.1).
- `UltraCanvasAndroidApplication` (ALooper pump, eventfd wakeup, fontconfig
  setup, Roboto defaults) + `UltraCanvasAndroidWindow` (image surface +
  `ANativeWindow` blit, `densityDpi/160` scale) + `android_main` glue for
  DemoApp.
- Touch→mouse translation, `AKEYCODE_*`→`UCKeys` table.
- Stubs: cursor, dialogs, clipboard (in-process fallback already exists),
  recent files, DnD off, GL off, video off, plugins compiled-in/off.
- **Milestone: DemoApp runs on an arm64 device/emulator.**

**Phase 2 — same OS support as Linux (core services)**
- Lifecycle virtuals (pause/resume/surface loss) + rotation-safe `Run()`.
- JNI clipboard backend; soft-keyboard show/hide hook wired to focus; IME
  committed-text path.
- Native dialogs via SAF/AlertDialog with the async bridge + `content://`
  URI adapter.
- UltraNet fully on: vendored curl + OpenSSL + c-ares, CA bundle from the
  system trust store via JNI.
- EGL/GLES context manager; re-enable GL surfaces.

**Phase 3 — parity extras**
- ~~Multi-touch events in the core event model (pointer IDs, `DispatchEvent`
  touch path)~~ — done; gesture *recognition* (pinch-zoom/rotate) still open.
- Audio (miniaudio AAudio — near-free), then video via MediaCodec backend.
- PDF (MuPDF android), image pipeline decision (trimmed libvips vs.
  platform decoders), printing via `PrintManager`.
- Plugin packaging as `jniLibs`, Play-store packaging polish.

---

## 8. Key file/line index

| Topic | Location |
|---|---|
| Platform alias switch (Application) | `include/UltraCanvasApplication.h:312-341` |
| Platform alias switch (Window) | `include/UltraCanvasWindow.h:451-491` |
| Application pure virtuals | `include/UltraCanvasApplication.h:260-307` |
| Window pure virtuals | `include/UltraCanvasWindow.h:111-286` |
| Main loop | `core/UltraCanvasApplication.cpp:450-548` |
| Event dispatch | `core/UltraCanvasApplication.cpp:948-1230` |
| Compose/present pipeline | `core/UltraCanvasWindow.cpp:438-575` |
| Only X11-tied surface call | `OS/Linux/UltraCanvasLinuxWindow.cpp:286` |
| Linux event pump (select+eventfd) | `OS/Linux/UltraCanvasLinuxApplication.cpp:215-294` |
| X event → UCEvent translation | `OS/Linux/UltraCanvasLinuxApplication.cpp:384-676` |
| Clipboard backend interface | `include/UltraCanvasClipboard.h:89-127` |
| Native dialogs API | `include/UltraCanvasNativeDialogs.h:49+` |
| GLContextManager interface/factory | `include/GL/GLContextManager.h`, `libspecific/GL/GLContextManager.cpp:25-54` |
| EGL manager (port source) | `OS/Linux/GLContextManagerEGL_Linux.cpp` |
| CMake platform detection | `UltraCanvas/CMakeLists.txt:96-107` |
| Required deps block | `UltraCanvas/CMakeLists.txt:128-182` |
| dlopen fallback precedent | `UltraCanvas/CMakeLists.txt:535` |
| UltraNet per-platform sources | `UltraCanvas/CMakeLists.txt:1349-1378` |
| c-ares DNS alternative | `core/UltraNet/UltraNetDnsCares.cpp` |
| Touch enum values (unwired) | `include/UltraCanvasEvent.h:50-54` |
