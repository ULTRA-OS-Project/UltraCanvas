# iPhone (iOS) Support — Investigation & Porting Plan

**Date:** 2026-08-10
**Status:** Investigation — no iOS code exists yet; this document records the
current state, the gaps, and the recommended path.

**Decisions taken (2026-08-10):**

1. **App Store distribution is required.** Everything the Store mandates is
   therefore a hard requirement, not an option: no `dlopen` (static plugin
   registration), code signing + review, privacy manifests, and static
   linking of all dependencies.
2. **CoreGraphics/CoreText is the end-target rendering backend.** The port
   must converge on the most optimised native solution; a cross-compiled
   Cairo/Pango stack is at most a temporary de-risking bridge, never the
   shipping configuration.

---

## Verdict

UltraCanvas has **no iPhone/iOS support today**, and an iOS build fails at the
CMake configure step. However, the architecture is unusually well prepared for
a port: five platform backends already exist behind clean abstractions
(`OS/{Linux,MSWindows,MacOS,BSD,WASM}`), the render context is an abstract
interface with a proven non-Cairo implementation (WASM/Canvas 2D), and the
event model already reserves touch event types. There are two viable routes:

1. **Short term (works now, with caveats):** run apps in mobile Safari via the
   existing WebAssembly backend, which already registers touch callbacks.
2. **Long term (committed):** a native `OS/iOS/` UIKit backend rendering
   through a new CoreGraphics/CoreText `IRenderContext`, with all plugins
   statically linked for App Store distribution. The majority of the work is
   in the render/text backend and touch-first widget behaviour, not in the
   platform glue.

---

## 1. Current state — why iOS does not build

### 1.1 Platform detection has no iOS branch

`UltraCanvas/CMakeLists.txt:96-107` selects the backend:

```cmake
if(WIN32)            → OS/MSWindows
elseif(APPLE)        → OS/MacOS
elseif(UNIX)         → OS/Linux
else()               → FATAL_ERROR "Unsupported platform"
```

When configured with an iOS toolchain, `APPLE` is TRUE, so CMake selects the
**macOS** backend — which imports `<Cocoa/Cocoa.h>` and `<AppKit/AppKit.h>`
(`OS/MacOS/UltraCanvasMacOSApplication.h:20-22`). AppKit does not exist on
iOS (iOS uses UIKit), so compilation fails immediately. `CMAKE_SYSTEM_NAME
STREQUAL "iOS"` must be tested *before* the generic `APPLE` branch.

### 1.2 The macOS backend is AppKit-only

Everything in `OS/MacOS/` is built on desktop-only frameworks and concepts:
`NSWindow`/`NSWindowDelegate`, `NSEvent` (`ProcessCocoaEvent`), `NSPasteboard`
clipboard, `NSCursor`, `NSOpenPanel` native dialogs, and CGL OpenGL contexts
(`GLContextManagerCGL_MacOS.mm`). None of these exist on iOS. The backend
renders through **cairo-quartz** (`OS/MacOS/UltraCanvasMacOSWindow.h:30-31`),
which is the one piece that conceptually carries over (CoreGraphics exists on
iOS), but the surface it draws into is an NSView.

### 1.3 The rendering stack is a hard dependency wall

The core library requires Cairo and Pango via pkg-config
(`UltraCanvas/CMakeLists.txt:130-134`, `REQUIRED`), plus GLib, and uses
librsvg and libvips inside `libspecific/Cairo/`. **None of these ship on
iOS.** They can be cross-compiled (static), but fontconfig/freetype setup on
iOS is famously awkward, and the whole stack adds tens of MB to an app binary.

### 1.4 Touch input is declared but not implemented

The event model was designed with mobile in mind:

- `UCEventType` reserves `TouchStart`, `TouchMove`, `TouchEnd`, `Tap`,
  `PinchZoom` — commented *"Touch Events (for future mobile support)"*
  (`include/UltraCanvasEvent.h:49-54`).
- `UCEvent` carries a touch `pressure` field (`:305`) and `IsTouchEvent()`
  (`:375`).

But **nothing generates or consumes these events**. The only match in `core/`
is the event-name string table (`core/UltraCanvasEvent.cpp:39-43`). Even the
WASM backend, which registers `touchstart/touchend/touchmove/touchcancel`
callbacks (`OS/WASM/UltraCanvasWASMWindow.cpp:81-84`), converts the *first*
touch point into synthetic **mouse** events
(`ConvertTouchEvent`, `:550-584`). Consequences on a touchscreen today:
single-finger tap/drag works, but there is no multi-touch, no pinch zoom, no
long-press, and no touch-specific hit-target/scrolling behaviour.

### 1.5 Desktop-idiom UI assumptions

Core widgets assume hover (tooltips), right-click context menus, double-click,
resizable multi-window management, native file dialogs, drag & drop, and a
hardware keyboard. All ~60 widgets work through the shared event pipeline, so
touch adaptation is a core-level effort, not per-platform glue — but it has
not been started.

### 1.6 OpenGL and the plugin loader are iOS-blockers

- `ULTRACANVAS_ENABLE_GL` builds desktop OpenGL context managers per platform
  (GLX/EGL, WGL, CGL). iOS has no desktop OpenGL and OpenGL ES is deprecated;
  the GL surface feature needs Metal (or ANGLE), or must be disabled for iOS.
- Runtime plugin loading uses `dlopen`
  (`core/UltraCanvasPluginLoader.cpp:31`,
  `core/UltraCanvasLaTeXModuleLoader.cpp:67`). The App Store prohibits
  loading executable code at runtime — every plugin must be statically linked
  and registered at build time on iOS.

---

## 2. Assets that make a port tractable

1. **Proven platform abstraction.** `UltraCanvasApplicationBase` /
   `UltraCanvasWindowBase` expose a small native surface
   (`InitializeNative`, `ShutdownNative`, `RunInEventLoop`, cursor
   selection, …) and five independent backends already implement it —
   including BSD and WebAssembly. A sixth (`OS/iOS/`) fits the existing
   pattern.
2. **The render context is swappable.** `IRenderContext`
   (`include/UltraCanvasRenderContext.h`, ~160 pure-virtual methods) is fully
   abstract, and `UltraCanvasWASMRenderContext` implements it on the HTML
   Canvas 2D API with **no Cairo at all** — direct precedent for the
   CoreGraphics/CoreText backend that is now the committed end target.
3. **Touch types already exist** in `UCEventType`, so a native backend can
   emit real touch events without core API changes.
4. **Retina/DPI model matches.** The macOS window already separates logical
   points from backing pixels (`OS/MacOS/UltraCanvasMacOSWindow.h:91`); iOS
   uses the identical points/scale model.
5. **iOS-compatible subsystems.** UltraNet is libcurl-based (fine on iOS);
   video on Apple platforms already uses AVFoundation (exists on iOS);
   the CSS-like layout engine (`Docs/CSSLayout.md`) supports the responsive
   layouts small screens require.
6. **The WASM backend runs in mobile Safari today**, giving an immediate
   evaluation vehicle on actual iPhones with zero porting work.

---

## 3. Recommended path

### Phase 0 — WASM in mobile Safari (days)

Ship a demo as a static site / PWA using the existing `OS/WASM` backend.
Fix `ConvertTouchEvent` to emit real `TouchStart/Move/End` events *and*
synthesize mouse events for compatibility. This provides real-device feedback
on font sizes, hit targets and scrolling before any native work. Limitations:
no App Store presence (unless wrapped in WKWebView), IndexedDB-only storage,
no native dialogs.

### Phase 1 — Core touch support (platform-independent, ~2-3 weeks)

Benefits WASM, ULTRA OS touchscreens and the future iOS backend equally:

- Route `Touch*` events through the window/element event pipeline with
  mouse-event fallback synthesis for widgets that don't opt in.
- Gestures: tap, long-press (→ context-menu semantics), pinch (`PinchZoom`),
  kinetic/momentum scrolling in scroll containers.
- Hover-free operation audit: tooltips, hover-revealed controls, hit-target
  sizes.

### Phase 2 — CoreGraphics/CoreText render backend (~6-10 weeks)

The committed end target. A new `libspecific/CoreGraphics/` implementation of
`IRenderContext` (~160 pure-virtual methods; the WASM Canvas 2D backend is
the precedent that this is feasible without Cairo). This is the single
largest work item, and it is **platform work that can start on macOS today**
— CoreGraphics/CoreText are identical on macOS, so the backend can be
developed and pixel-compared against the Cairo backend on desktop long before
any iOS device is involved. It also unlocks a leaner macOS build (dropping
the Cairo/Pango/GLib/fontconfig payload) as a side benefit.

Sub-items, replacing what the Cairo stack provides today:

| Cairo-stack component | CoreGraphics replacement |
|---|---|
| `RenderContextCairo` (paths, fills, strokes, gradients, clipping, transforms) | `CGContext` drawing — closest to a 1:1 mapping |
| `UCTextLayout` (Pango text shaping/layout) | CoreText (`CTFramesetter`/`CTLine`); must reproduce the existing metrics contract so widget layout stays identical |
| `ImageCairo` + libvips loaders | ImageIO (`CGImageSource`) for PNG/JPEG/GIF/WebP/HEIC; keep the QOI codec (plain C, portable) |
| `SvgDocumentCairo` (librsvg) | No system SVG renderer on Apple platforms — either render SVG through the framework's own vector path API on `CGContext`, or vendor a small portable SVG rasterizer |
| fontconfig font enumeration | CoreText font descriptors (`CTFontManager`) |

Definition of done: the DemoApp renders pixel-equivalent (within
anti-aliasing tolerance) on macOS with `ULTRACANVAS_RENDER_BACKEND=CoreGraphics`
vs. the Cairo build, and the text-layout regression tests pass.

An interim cross-compiled Cairo build remains available as a *de-risking
bridge only* (e.g. to unblock Phase 3 device testing while Phase 2 is in
flight); it must not ship, per the decision above.

### Phase 3 — Native `OS/iOS/` backend (~4-6 weeks on top of Phase 2)

| Work item | Approach |
|---|---|
| CMake | `elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")` before the `APPLE` branch; iOS toolchain file; static-only build (App Store requirement) |
| App lifecycle | `UIApplicationDelegate`/`UIScene` → `UltraCanvasiOSApplication`; map foreground/background to existing window focus/visibility events |
| Window | Single fullscreen `UIWindow` + custom `UIView` backed by `CALayer`; the CoreGraphics render context draws into the view's `CGContext`; safe-area insets exposed to the layout engine |
| Input | `touchesBegan/Moved/Ended` → `Touch*` UCEvents; `UIKeyCommand` for hardware keyboards; virtual keyboard + IME via a hidden `UITextInput` view driving the existing text-entry pipeline |
| Clipboard | `UIPasteboard` |
| Dialogs | `UIDocumentPickerViewController` for open/save; `UIAlertController` for message dialogs |
| GL surface | Disable `ULTRACANVAS_ENABLE_GL` for the MVP; Metal (or ANGLE) later |
| Plugins | Static registration table replacing `dlopen` — mandatory for the App Store, no fallback |
| Packaging & review | `xcodebuild` signing + `.ipa` export; App Store privacy manifest (`PrivacyInfo.xcprivacy`, required since iOS 17 for apps using file-timestamp/user-defaults APIs); export-compliance declaration for TLS (standard exemption); the repo's macOS notarization scripts (`package_and_notarize-macos.sh`) are the workflow precedent |

### Explicitly out of scope for the MVP

Multi-window, drag & drop, mouse cursors, system tray, video *recording*, and
every UltraNet plug-in with an unported C dependency (MQTT, AMQP, CoAP, SNMP,
SSH, LDAP — audit individually).

---

## 4. Decisions and open questions

**Resolved:**

1. **Distribution: App Store.** Static plugin registration replaces `dlopen`
   on iOS with no fallback; signing, review, privacy manifest and export
   compliance are in scope from the first TestFlight build.
2. **Rendering: CoreGraphics/CoreText is the end target.** The shipping iOS
   build carries no Cairo/Pango/GLib payload. A cross-compiled Cairo build
   may be used mid-project to unblock device testing, but is explicitly
   non-shipping.

**Still open:**

1. **Minimum iOS version** — recommend iOS 15+ (covers >96% of devices,
   allows `UIScene` lifecycle without legacy paths).
2. **Whether Phase 0/1 (WASM + core touch) ships as its own deliverable** —
   recommended: it is low-cost, de-risks the native port, and improves
   ULTRA OS touchscreen support on its own.
3. **Which UltraNet plug-ins ship on iOS** — each C-dependency plug-in
   (MQTT, AMQP, CoAP, SNMP, SSH, LDAP) needs an individual port/licensing
   audit; the libcurl-native core (HTTP/S, WebSocket, FTP) is unaffected.
