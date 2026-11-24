# MobilePC — UltraDesktop on a TV or monitor, driven by an Android phone (Design Proposal)

**Date:** 2026-11-24
**Status:** Being implemented. Done: the embedded window manager and its
title bar (§5.1, Cairo compositor), the Android desktop mode with the
presentation-display output, the phone's-screen output and the captured
pointer (§5.2), the hosted-application registry and the desktop shell's
embedded host (§5.3 in part, §5.4), and `Apps/MobilePC` with UltraDesktop,
UltraViewer, UltraPaint and ArtCreator, with a CI job that builds the APK
and runs it on an emulator with a simulated second display. Not yet: Texter, UltraFiler, per-app
identity beyond the catalogues, the GLES compositor, popup layers, the
foreground service, wireless streaming (§4 mode 2). See
[`Docs/MobilePC/README.md`](../MobilePC/README.md).
**Builds on:** the Android backend in [`UltraCanvas/OS/Android/`](../../UltraCanvas/OS/Android/README.md)
and its investigation, [`Docs/UltraCanvas/AndroidPortInvestigation.md`](../UltraCanvas/AndroidPortInvestigation.md);
[UltraDesktop](../UltraDesktop/README.md) and the
[`UltraCanvasDesktopShell`](../UltraCanvas/UltraCanvasDesktopShell.md) module it is built on;
[UltraRTC](../Modules/UltraRTC/README.md) for the wireless stream.
**Not to be confused with:** [UltraAndroid](UltraAndroidDesignProposal.md), which
runs *Android apps on ULTRA OS*. MobilePC runs *ULTRA OS apps on Android*.

---

## 1. Purpose

MobilePC is an Android app that turns a phone (or tablet) into a desktop
computer. Connected to a TV or monitor — by cable or wirelessly — it shows
**UltraDesktop** on that screen, full size, and runs the UltraCanvas desktop
applications in windows on it: **Texter, UltraFiler, UltraPaint, ArtCreator**,
then UltraViewer, UltraMail, ChatCube and the rest. A Bluetooth or USB keyboard
and mouse drive it. With no keyboard or mouse, the phone's own screen becomes a
touchpad and keyboard.

What the user sees:

| Screen | Shows |
|---|---|
| TV / monitor | UltraDesktop: wallpaper, taskbar, the Apps window, application windows with title bars that can be moved, resized, minimised and maximised, and a mouse pointer |
| Phone | The *control pad*: a touchpad area (one finger moves the pointer, tap clicks, two fingers scroll, long press is a right click), a keyboard button (the phone's IME types into the window that has focus on the TV), an app-switcher button, and *Disconnect* |
| Phone, no display connected | A start page: "Connect a display", the connection options of §4, and on a tablet *Use this screen* (the same desktop on the built-in display) |

---

## 2. What the investigation found

### 2.1 The Android backend is far along, but it is a one-window phone backend

`UltraCanvas/OS/Android/` already provides the application/window backend
(`android_native_app_glue`, ALooper pump, Cairo image surface blitted with
`ANativeWindow_lock`/`unlockAndPost`), the lifecycle (background, foreground,
rotation), touch with pinch/rotate, physical keys, the full IME path,
clipboard, SAF open/save, message dialogs, GLES, audio (miniaudio/AAudio),
video and camera (MediaCodec/Camera2), UltraNet with the platform's trust
roots, and APK asset extraction. CI builds the dependency sysroot (vcpkg,
`arm64-android` and `x86_64`), the core, DemoApp and ChatCube for Android, and
packages a debug APK whose emulator smoke test sees a first frame, a tap,
typing and a rotation.

Three facts in it shape this proposal:

1. **Only one window is ever presented.** `UltraCanvasAndroidApplication::GetPrimaryWindow()`
   is `windows.front()`; every input event and surface command goes to it, and
   `UltraCanvasAndroidWindow` "always covers the whole surface; desktop
   window-management calls are accepted no-ops". A second top-level window —
   UltraFiler's dialogs, UltraPaint's *File > New Window*, every application
   UltraDesktop starts — has a render context but nowhere to appear.
2. **Each app is its own APK with its own `main()`.** An app's `main()` becomes
   `ultracanvas_app_main()` inside `lib<App>.so` (DemoApp, ChatCube), owns the
   process-wide `UltraCanvasApplication`, and calls `Run()`. There is no
   notion of several applications in one process.
3. **Nothing has run on a real device yet** — only on the emulator. MobilePC
   inherits every unobserved part of the backend (dialogs, the IME's composing
   path, gestures). Phase 0 includes the first real-device run.

### 2.2 UltraDesktop is a shell for someone else's window manager

UltraDesktop draws the bars, the Apps window and the panels, but every window
it lists, raises, minimises or closes belongs to the platform's window manager
(X11 EWMH, Wayland foreign-toplevel, Windows' shell), reached through the
`UltraCanvasDesktopShell` backend contract
(`ULTRACANVAS_DESKTOPSHELL_NATIVE`, `..._NATIVE_CAPTURE`,
`..._NATIVE_APPLICATIONS`; a null backend otherwise). Applications are found
as `.desktop` / Start-menu entries and started as **separate processes**
(`UltraCanvasDesktopShell::LaunchApplication`).

On Android there is no window manager we can be a shell for, and a third-party
app cannot draw another app's windows. **MobilePC therefore has to supply the
window manager itself.** That is the central piece of work in this proposal.

### 2.3 The target applications are portable

| App | Size (lines) | Files with platform code | Notes for Android |
|---|---|---|---|
| Texter | ~13,300 | 3 | Spell checking already has an Android arm (`UltraCanvasSpellCheckSupport.cpp`) |
| UltraPaint | ~8,800 | 1 | libvips is in the Android sysroot; opens several windows (*New Window*), one dialog window |
| ArtCreator | ~8,400 | 1 | One standard window |
| UltraFiler | ~26,000 | 13 | The real port: volumes, trash, mounts, open-with, storage permission (§7.4) |
| UltraViewer | ~500 | 1 | Trivial; a good first hosted app |
| ChatCube | — | — | Already builds for Android |

All of them follow the same bootstrap — `UltraCanvasSingleInstance` claim,
`UltraCanvasApplication app; app.Initialize("<Name>")`, a main window,
`app.Run()` — which is what makes a mechanical conversion to "hosted"
entry points (§6) possible.

### 2.4 Android facts that decide the design

- **An app can draw on an external display** with
  `android.app.Presentation` on a display from
  `DisplayManager.getDisplays(DISPLAY_CATEGORY_PRESENTATION)` (API 17+), or by
  launching an activity there with `ActivityOptions.setLaunchDisplayId`. Both
  give MobilePC its own full-screen surface on the TV that is *not* a mirror
  of the phone.
- **What appears as such a display** depends on the phone: a USB-C cable or
  dock where the phone supports DisplayPort Alt Mode, and OEM wireless display
  (Miracast-class, e.g. Samsung Smart View) where the OEM exposes it as a
  presentation display. Google Cast "cast screen", DisplayLink adapters and
  most stock wireless paths are **mirroring** only.
- **The emulator can simulate one.** *Simulate secondary displays*
  (`adb shell settings put global overlay_display_devices 1920x1080/240`)
  creates a presentation display — so the TV case can be tested in CI with
  the existing emulator job, no hardware.
- **The mouse pointer is the platform's problem unless we take it.** Which
  display shows the system pointer varies by version and OEM.
  `View.requestPointerCapture()` (API 26) hands the focused activity raw
  relative mouse motion instead; MobilePC draws its own pointer on the TV.
- **System UI lands on the phone, not the TV:** the SAF picker, permission
  prompts and the IME appear on the default display. The IME there is
  welcome (the phone is the keyboard); file pickers there are not (§7.3).

---

## 3. Architecture decision

**One process, one APK, and an in-process window manager that composites
every UltraCanvas window onto the TV.**

```
 Phone display                         TV / monitor
 ┌───────────────────────┐            ┌──────────────────────────────────────────┐
 │ MobilePCActivity      │            │ Presentation (or encoder surface, §4)     │
 │  control pad, IME,    │            │  ┌──────────── output surface ──────────┐ │
 │  pointer capture      │            │  │ UltraDesktop (WindowType::Desktop)   │ │
 └─────────┬─────────────┘            │  │   ┌ Texter ─────┐  ┌ UltraPaint ──┐  │ │
           │ input (keys, deltas,     │  │   │             │  │              │  │ │
           │ touchpad gestures, text) │  │   └─────────────┘  └──────────────┘  │ │
           ▼                          │  │                          ↖ pointer   │ │
 ┌───────────────────────────────────────────────────────────────────────────┐ │ │
 │ libMobilePC.so  (one UltraCanvasApplication, one UI thread)               │ │ │
 │                                                                           │ │ │
 │  Embedded window manager (core, platform-neutral)                         │─┘ │
 │   z-order · focus · move/resize · decorations · software pointer ·        │   │
 │   damage-tracked compositing of each window's offscreen surface           │───┘
 │                                                                           │
 │  Hosted apps: UltraDesktop · Texter · UltraFiler · UltraPaint · ArtCreator│
 │  DesktopShell backend "Hosted": ListWindows/Activate/Launch… from the WM  │
 └───────────────────────────────────────────────────────────────────────────┘
```

Why this and not the alternatives:

| Option | Verdict |
|---|---|
| **A. One process, in-process window manager** | **Chosen.** Works on every phone that can show a presentation display (and, via §4, every phone at all). Each UltraCanvas window already renders into its own offscreen Cairo surface, and on Android `nativeSurface` is already an image surface — compositing them is a blit per window, not a new rendering path. Clipboard and drag-and-drop between apps become in-process and free. Cost: the apps share a crash domain, and a "hosted app" mode must be added (§6). |
| B. A process per app, with our own compositor protocol (a Wayland-like socket + shared `AHardwareBuffer`s) | Deferred. Gives crash isolation and keeps every app's `main()` untouched, but means designing a display-server protocol, an input/IME/clipboard IPC layer and a new client backend before the first pixel. Option A's window manager is the compositor B would need, so B stays open as a later upgrade if crash isolation proves necessary. |
| C. Rely on Android's own desktop windowing (Samsung DeX, Motorola Ready For, Android's connected-display desktop mode), one APK per app | Complementary, not the product. Per-app APKs (as ChatCube already is) run in those modes for free, but UltraDesktop cannot be the shell there — a third-party app cannot list, raise or decorate other apps' windows — and the modes exist only on some devices. |

---

## 4. Connecting to the screen

MobilePC has one **output** — the surface the window manager composites into
— and picks it from what is available, best first:

| Mode | How | Phones | Quality |
|---|---|---|---|
| **1. Presentation display** (wired, or OEM wireless) | `DisplayManager` presentation display → a `Presentation` holding a `SurfaceView`; its `ANativeWindow` is the output | DP Alt Mode over USB-C cable/dock; OEM wireless displays that expose a presentation display | Native resolution, no encoding, lowest latency. **The primary mode.** |
| **2. MobilePC stream** (wireless, any phone) | The output is a `MediaCodec` H.264/HEVC encoder's input surface (`AMediaCodec_createInputSurface`). Frames go over the LAN as a WebRTC video track through UltraRTC (libdatachannel already vendored; packetise MediaCodec's H.264 rather than the FFmpeg/libvpx path, which is not in the Android sysroot) | Any phone on the same Wi-Fi as the receiver | 1080p at 30–60 fps, target < 100 ms glass-to-glass on a good LAN |
| **3. Mirror mode** (fallback) | The output is the phone's own window, landscape, full screen. Whatever mirrors the phone (Google Cast, DisplayLink, a mirroring OEM wireless display) shows the desktop | Every phone | Phone's resolution and aspect ratio; the phone cannot also be the touchpad (use a BT mouse) |
| **4. This screen** (tablets, ChromeOS) | Same as 3, but as a deliberate choice: the tablet *is* the desktop's screen | Tablets with keyboard covers | Native |

Receivers for mode 2, in order:

1. **A web receiver page** — plain WebRTC in a browser: a smart TV's browser,
   a laptop, any monitor attached to a PC. No install. Pairing by a QR code
   the page shows and the phone scans with its camera (Camera2 is in the
   backend; the QR reader is the one UltraAuthenticator uses); signalling over
   a small WebSocket on the phone, LAN-only.
2. **MobilePC Receiver for Android TV / Google TV** — the same APK with a
   leanback launcher entry; decodes with `MediaCodec` straight to a
   `SurfaceView`. Lower latency than a TV browser and starts from the TV's home
   screen.
3. **A Google Cast custom web receiver** for Chromecasts without Google TV,
   if WebRTC playback on Cast receivers proves adequate (to be verified).

Mode 2's frames reuse mode 1's compositor unchanged: only the surface it
draws into differs. Encoding is the phone's hardware encoder; the window
manager's damage tracking (§5.2) keeps a static desktop at near-zero cost.

---

## 5. Framework work

### 5.1 Embedded window manager — `core/`, platform-neutral

New: `include/UltraCanvasEmbeddedWindowManager.h`,
`core/UltraCanvasEmbeddedWindowManager.cpp`. Platform-neutral on purpose
(house rule 4): the same piece can later give the WebAssembly backend, which
also has one canvas, real movable windows.

- **Windows as clients.** Each top-level `UltraCanvasWindow` gets a frame:
  position and size in output coordinates, z-order, minimised/maximised state,
  focus. `SetWindowPosition`, `SetWindowSize`, `Minimize`, `Maximize`,
  `Restore`, `RaiseAndFocus`, `SetWindowTitle`, `SetWindowIcon` stop being
  no-ops and act on the frame. `WindowType::Desktop` stays at the bottom and
  fills the output; `WindowConfig::desktopArea` and `ReserveScreenEdges` give
  UltraDesktop's bars their strips, and maximised windows stop short of them.
- **Decorations** — title bar with icon, title, minimise/maximise/close —
  are built from UltraCanvas elements (`UltraCanvasButton` and a label in a
  frame container), not hand-painted (house rule 2). Dragging the bar moves,
  edges and corners resize, double-click maximises, close sends the normal
  `WindowCloseRequest`, so "Save the changes?" works as on the desktop.
  Undecorated window types (popups, menus, tooltips, UltraDesktop's panels)
  get no frame.
- **Software pointer** drawn last; `SelectMouseCursorNative` sets which.
  The framework keeps only its custom cursors as images
  (`media/lib/cursor/`) and takes the standard ones from the X cursor theme
  on Linux, so a standard set (arrow, I-beam, hand, resize, wait) is added
  there as SVGs. Hidden after a few seconds of touch-only use.
- **Input routing.** Pointer events are hit-tested against frames top-down
  and delivered in window-local coordinates; keyboard and IME text go to the
  focused window; the existing mouse-capture semantics are honoured.
  Window-manager keys: `Alt+Tab`, `Super` (UltraDesktop's Apps window, as on
  the desktop), `Alt+F4`.
- **Compositing.** Each window's `InvalidateWindowNative` reports its dirty
  rectangles in output coordinates; the manager composites only the damaged
  region of the output, in z-order: window surfaces (already composed by the
  core's `FlushToSurface` with popups, caret and tooltip), frames, pointer.
  First implementation: Cairo onto the output's locked buffer (the Android
  backend's existing present path). Second (phase 3): GLES — each window's
  surface a texture updated with `glTexSubImage2D` for its dirty rectangles,
  composited on the GPU, which is what 4K needs.
- **Popups beyond a window's edge.** Today a menu or dropdown renders inside
  its window's surface. The manager will let popups be layers of their own so
  a menu near a window's bottom edge is not cut off — phase 3, not needed for
  the first milestone.
- **Scale.** The output's logical scale is chosen from its size and viewing
  distance, not its DPI (a 55″ 4K TV has low DPI): 1.0 at 1080p, 2.0 at 4K,
  adjustable in MobilePC's settings. At 4K the output can be rendered at 1080p
  and scaled by the display hardware (`ANativeWindow_setBuffersGeometry` with
  a smaller buffer) until the GLES compositor lands.

### 5.2 Android backend: a desktop mode

In `UltraCanvas/OS/Android/` (platform-specific only):

- **Output surface** abstraction with the three sources of §4 (presentation
  `SurfaceView`, encoder input surface, the activity's own window), switched
  at runtime when a display arrives or leaves (`DisplayManager.DisplayListener`
  — `UltraCanvasActivity` already registers one for rotation). Losing the
  output hides nothing: windows keep their state and reappear on the next
  output, as backgrounding already works.
- **Several windows.** In desktop mode `UltraCanvasAndroidWindow` no longer
  owns an `ANativeWindow`; it is a window-manager client. `GetPrimaryWindow()`
  routing is replaced by the manager's hit-testing and focus. Phone mode (one
  full-screen window, today's behaviour) stays the default for ordinary
  single-app APKs.
- **Input from the control pad.** `MobilePCActivity` holds pointer capture
  (`requestPointerCapture`), so a physical mouse delivers relative motion to
  it whichever display Android would put the system pointer on; the touchpad
  area turns finger motion into the same relative motion plus clicks, scroll
  and right-click. The `Presentation` is created non-focusable, so physical
  keyboard events and IME commits keep arriving at the phone activity, which
  hands them to the focused window on the TV. Text input already flows through
  `UltraCanvasAndroidTextInput.cpp`; only its destination changes.
- **Staying alive.** A foreground service while a desktop session runs (type
  to be settled against Android 14+ rules; `connectedDevice` is the candidate),
  `FLAG_KEEP_SCREEN_ON`, and a dimmed, black control pad to save the phone's
  battery. Whether a presentation stays visible while the phone is locked
  varies; Phase 0 measures it.

### 5.3 Hosted applications — several apps in one process

New: `include/UltraCanvasHostedApp.h`, `core/UltraCanvasHostedApp.cpp`.

- **Registry.** An app registers an entry point instead of owning `main()`:
  its name, icon, categories (UltraDesktop's Apps window groups by them) and a
  `Launch(args)` function that does what its `main()` does between
  `app.Initialize(...)` and `app.Run()` — create the main window, open the
  files named. The host (MobilePC) owns `UltraCanvasApplication`, initialises
  it once and runs the one loop; `Run()` does not end while the desktop window
  exists.
- **Per-app identity.** Today `Initialize(appName)` sets one process-wide name
  used by `UltraLocalisation::Initialize(appName, …)`, the modal dialogs and
  the filer widget. Hosted mode adds an app context (name, default icon,
  settings directory, single-instance key) attached to each window at
  creation. **Localisation, first version:** load every hosted app's
  catalogue on top of the standard set; an application's catalogue holds only
  its own strings (AGENTS.md, house rule 2), so clashes are rare, and a check
  script reports any source string two hosted apps translate differently.
  Per-window catalogue selection can follow if that check ever fires.
- **Single instance in-process.** `UltraCanvasSingleInstance::Claim` in
  hosted mode asks the registry: the first launch starts the app, the next
  one is handed to the running copy's open-request handler — no socket.
- **Quitting an app closes its windows**, never the process. Each app's
  `exit()` calls (7 across the four target apps), signal handlers and
  `HandleFatalError` paths are audited; in hosted mode they close that app's
  windows and log.
- **Desktop builds are untouched.** Each app's `main.cpp` keeps its `main()`
  and calls the same `Launch` function, so Linux, Windows and macOS builds
  change only by that refactor.

### 5.4 DesktopShell backend "Hosted"

UltraDesktop runs unmodified except for what Android cannot do. The backend
(`ULTRACANVAS_DESKTOPSHELL_NATIVE` and `..._NATIVE_APPLICATIONS` defined for
the MobilePC build) answers from the window manager and the registry:

| Shell call | MobilePC answer |
|---|---|
| `ListWindows`, `GetActiveWindow`, `ActivateWindow`, `MinimizeWindow`, `CloseWindow`, `GetWindowBounds`, `MoveWindow`, `BringWindowOnScreen` | The window manager's frames; `processId` the app context's id |
| `KillWindowProcess` | Closes every window of that hosted app, after the usual confirmation |
| `ListApplications`, `LaunchApplication` | The hosted-app registry; in-process launch |
| `ListScreens`, `GetScreenSize`, `ReserveScreenEdges` | The current output |
| `CaptureScreen*`, `CaptureWindowImage` | Read straight from the compositor's surfaces — **now possible**, where the Android phone backend answers `CanCaptureScreen() == false`; UltraDesktop's window previews work, and so would a hosted UltraSnip |
| Virtual desktops | The window manager keeps them (phase 5) |
| `ReadDeviceActivity` | The phone's battery (`BatteryManager`) and network (`ConnectivityManager`) over JNI; the rest best-effort with `warnings` |
| Wi-Fi / Bluetooth panels | Open Android's settings panels on the phone (`Settings.Panel`); apps cannot switch radios since Android 10 |
| Lock, sign out, restart, shut down | Replaced by *Disconnect display* and *Quit MobilePC* |

---

## 6. Converting an application

Per app, the same mechanical change:

1. Move the body of `main()` between `Initialize` and `Run` into
   `<App>Launch(const std::vector<std::string>& args)`, and register it with
   the hosted-app registry. `main()` on the desktop keeps the bootstrap and
   calls it.
2. Build the app's sources as a static library (`<App>Core`) that both its
   desktop executable and `libMobilePC.so` link. UltraDesktop splits the same
   way (`UltraDesktopCore` + `main.cpp`).
3. Replace `exit()` and signal-handler paths as in §5.3.
4. Link plug-ins statically (no `dlopen` from outside the APK; the precedent is
   ChatCube's linked UltraIM providers, `Apps/ChatCube/android/`).
5. Give the app's platform code an `__ANDROID__` arm where it has none (before
   any `__linux__` test, since bionic defines `__linux__`).

---

## 7. Product concerns

### 7.1 Files and storage

- Every file still goes through `UltraCanvasFileLoader` (CLAUDE.md). Paths are
  real paths: MobilePC asks for **All files access**
  (`MANAGE_EXTERNAL_STORAGE`, API 30+), which Google Play grants to file
  managers — UltraFiler is one. Without it, the user's documents folder is the
  app-private storage plus whatever SAF imports.
- `HOME` is already the app sandbox; MobilePC adds the shared storage roots
  (`/storage/emulated/0`, SD cards, USB drives from `StorageManager`) as the
  well-known folders and volumes UltraFiler and the file dialogs show.

### 7.2 Clipboard and drag-and-drop

In-process between hosted apps — copy in Texter, paste in UltraPaint, drag a
file from UltraFiler onto ArtCreator — through the framework's existing
in-process paths. The Android clipboard backend still exchanges text with
other Android apps.

### 7.3 Dialogs on the right screen

In desktop mode, file and message dialogs must appear on the TV, not as
Android's SAF picker and `AlertDialog` on the phone. Desktop mode therefore
routes `UltraCanvasNativeDialogs` to the framework's own dialogs
(`UltraCanvasFileDialog`, the modal dialogs) as windows of the window
manager. SAF stays available as *Import from another app…* in the file
dialog, for cloud-provider documents.

### 7.4 UltraFiler specifics

The largest port (13 files with platform code): volumes from `StorageManager`;
a trash folder of its own (Android has no system trash for arbitrary files);
*Open with* offers the hosted apps first, then an Android `ACTION_VIEW` intent
(shown on the phone); no mounting (Android does not let apps mount); removable
drives' *Safely remove* is the system's.

### 7.5 Distribution

One APK (`org.ultraos.mobilepc`), `minSdk 28` as the backend's packaging
template, `arm64-v8a` (+ `x86_64` for the emulator). Sideload and an F-Droid
style build first; Play Store needs the All-files-access and foreground-service
declarations reviewed.

---

## 8. Layout in the repository

| Path | What |
|---|---|
| `Apps/MobilePC/` | The app: `MobilePCHost.cpp` (hosted-app registration, output selection, session), `ui/` (the phone's control pad and start page, as UltraCanvas elements), `android/` (manifest, `MobilePCActivity.java` extending `UltraCanvasActivity`: `Presentation`, pointer capture, foreground service, receiver pairing), `CMakeLists.txt` building `libMobilePC.so` |
| `Apps/MobilePC/receiver/` | The web receiver page (HTML/JS, WebRTC) and the Android TV receiver activity |
| `UltraCanvas/core/UltraCanvasEmbeddedWindowManager.cpp`, `UltraCanvasHostedApp.cpp` | §5.1, §5.3 |
| `UltraCanvas/OS/Android/UltraCanvasAndroidDesktopMode.cpp`, `UltraCanvasAndroidHostedShell.cpp` | §5.2, §5.4 |
| `Docs/MobilePC/` | README, Manual, CHANGELOG, as every app has |
| `Docs/UltraCanvas/UltraCanvasEmbeddedWindowManager.md`, `UltraCanvasHostedApp.md` | Component docs for the two new public APIs |

CI: `android-build.yml` gains a MobilePC APK, and the emulator job a second
smoke run with a simulated secondary display that requires UltraDesktop's
first frame on that display, a window opened from the Apps window, and a
pointer click landing in it.

---

## 9. Phases

| Phase | Deliverable | Gate to the next | Rough effort |
|---|---|---|---|
| **0. Spike** | DemoApp's window drawn on a presentation display (emulator overlay display and one real phone over USB-C → HDMI); pointer capture driving a software pointer; first real-device run of the existing backend | Input reaches the phone activity while the TV shows the desktop; 1080p full-frame composite under 16 ms on a mid-range phone; locked-phone behaviour known | 2–3 weeks |
| **1. Window manager + desktop mode** | §5.1 (Cairo compositor) and §5.2; UltraDesktop, DemoApp and UltraViewer in one process on the TV; control pad | Windows move, resize, stack and take focus; emulator CI smoke test green | 6–8 weeks |
| **2. Hosted apps** | §5.3, §5.4, §6 for Texter, UltraPaint, ArtCreator; framework dialogs on the TV (§7.3) | Open, edit, save a document in each; copy/paste between them | 6–8 weeks |
| **3. UltraFiler + performance** | §7.1, §7.4; GLES compositor; popup layers | 4K output at 60 fps with a static desktop near-idle; UltraFiler browses shared storage and USB drives | 6–8 weeks |
| **4. Wireless** | §4 mode 2 (encoder + UltraRTC + web receiver + Android TV receiver), mode 3 mirror fallback | < 100 ms on a home LAN at 1080p30 | 6–8 weeks |
| **5. Product** | Virtual desktops, settings (scale, output, pad), UltraMail/ChatCube/others hosted, store packaging, tablet mode | — | ongoing |

Effort is for one experienced developer on this codebase and assumes the
backend behaves on hardware as it does in the emulator; Phase 0 is where that
assumption is tested.

---

## 10. Risks and open questions

1. **Device variance in display output** — DP Alt Mode and OEM wireless
   displays differ by phone. Mitigation: modes 2 and 3 work everywhere; keep a
   tested-devices list from Phase 0 on.
2. **The backend has never run on hardware.** Phase 0 runs it before anything
   is built on it.
3. **Shared crash domain** — one app's crash ends the session. Mitigation:
   autosave in the editors; Option B (§3) if it proves a real problem.
4. **CPU compositing at 4K** — addressed by hardware scaling first and the
   GLES compositor in Phase 3.
5. **Memory** — several apps and libvips in one process. A foreground service
   keeps it from being killed in the background; measure in Phase 2.
6. **Presentation while the phone is locked** and the right foreground-service
   type — both to be settled by measurement and Android 14+ rules in Phase 0.
7. **Play Store policy** for All files access — UltraFiler qualifies as a file
   manager; sideloading needs nothing.
8. **Localisation clashes** between hosted apps — detected by a check (§5.3),
   resolved per window only if needed.
