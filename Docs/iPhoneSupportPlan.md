# iPhone (iOS) Support — Investigation & Porting Plan

**Date:** 2026-08-10
**Status:** Investigation — no iOS code exists yet; this document records the
current state, the gaps, and the recommended path.

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
2. **Long term (recommended):** a native `OS/iOS/` UIKit backend, estimated as
   a medium-large effort with the majority of the work in the dependency stack
   (Cairo/Pango) and touch-first widget behaviour, not in the platform glue.

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
   Canvas 2D API with **no Cairo at all** — direct precedent for a
   CoreGraphics/CoreText backend if cross-compiling Cairo is rejected.
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

### Phase 2 — Native `OS/iOS/` backend (~6-10 weeks MVP)

| Work item | Approach |
|---|---|
| CMake | `elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")` before the `APPLE` branch; iOS toolchain file; static-only build |
| App lifecycle | `UIApplicationDelegate`/`UIScene` → `UltraCanvasiOSApplication`; map foreground/background to existing window focus/visibility events |
| Window | Single fullscreen `UIWindow` + custom `UIView` backed by `CALayer`; safe-area insets exposed to the layout engine |
| Rendering (option A, faster) | Cross-compile Cairo/Pango/freetype static for iOS; keep `cairo-quartz` drawing into the view's CGContext, as macOS does today |
| Rendering (option B, cleaner) | New `IRenderContext` on CoreGraphics/CoreText, following the WASM precedent; drops the Cairo/Pango/GLib payload but is the single largest work item and duplicates text layout (`libspecific/Cairo/UCTextLayout`) |
| Input | `touchesBegan/Moved/Ended` → `Touch*` UCEvents; `UIKeyCommand` for hardware keyboards; virtual keyboard + IME via a hidden `UITextInput` view driving the existing text-entry pipeline |
| Clipboard | `UIPasteboard` |
| Dialogs | `UIDocumentPickerViewController` for open/save; `UIAlertController` for message dialogs |
| GL surface | Disable `ULTRACANVAS_ENABLE_GL` for the MVP; Metal or ANGLE later |
| Plugins | Static registration table replacing `dlopen` on iOS |
| Packaging | Xcode generator or `xcodebuild` signing step; the repo's macOS notarization scripts (`package_and_notarize-macos.sh`) are the precedent |

Recommendation: start with **option A** (Cairo cross-compile) to reach a
running MVP quickly, and treat a CoreGraphics `IRenderContext` as a follow-up
that also unlocks a leaner macOS build.

### Explicitly out of scope for the MVP

Multi-window, drag & drop, mouse cursors, system tray, video *recording*, and
every UltraNet plug-in with an unported C dependency (MQTT, AMQP, CoAP, SNMP,
SSH, LDAP — audit individually).

---

## 4. Decision points

1. **Rendering backend:** cross-compiled Cairo/Pango (fast to MVP, heavy
   binary) vs. native CoreGraphics/CoreText `IRenderContext` (lean, large
   effort). Affects binary size, text shaping fidelity, and maintenance.
2. **Distribution:** App Store (forces static plugins, signing, review) vs.
   in-house/TestFlight only.
3. **Whether Phase 0/1 (WASM + core touch) is worth shipping regardless** —
   it is low-cost, de-risks the native port, and improves ULTRA OS
   touchscreen support on its own.
