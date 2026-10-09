# UltraCanvasDesktopShell

<!-- doc-check: std::shared_ptr<UltraCanvasToolbar> taskbar; void TogglePanel(); -->

The running desktop as a shell sees it: the windows other applications have
open and which one is active, the virtual desktops, the installed applications
a launcher lists, a screenshot of the screen, the live state of the devices an
info panel shows, and the counts an application publishes for the desktop to
show beside its icon.

```cpp
#include "UltraCanvasDesktopShell.h"

for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
    if (w.skipTaskbar) continue;
    const uint64_t id = w.id;
    taskbar->AddToggleButton("win-" + std::to_string(id), "", w.iconFile, [id](bool on) {
        if (on) UltraCanvasDesktopShell::ActivateWindow(id);
        else UltraCanvasDesktopShell::MinimizeWindow(id);
    });
}

UltraCanvasDesktopShell::SetCurrentVirtualDesktop(2);
UltraCanvasDesktopShell::CaptureScreen(UltraCanvasDesktopShell::DefaultScreenshotPath());
DesktopDeviceActivity now = UltraCanvasDesktopShell::ReadDeviceActivity();

UltraCanvasDesktopShellMonitor monitor;
monitor.Start([this]() { windowsDirty.store(true); });   // background thread!
```

This is the module the ULTRA OS desktop ([UltraDesktop](../UltraDesktop/README.md))
is built on, and the reason it is a module rather than desktop code: any
application that wants a window list, a virtual-desktop switch or a screenshot
gets the same one, and a second implementation of the window-manager protocol
cannot disagree with the first about which windows exist.

Headers: `UltraCanvasDesktopShell.h` (the data and the API);
`UltraCanvasDesktopShellBackend.h` is the internal contract with the platform
backends and is not for applications.

## Why this is neither UltraCanvasHardwareInfo nor IODeviceManager

[UltraCanvasHardwareInfo](UltraCanvasHardwareInfo.md) *describes the machine*:
which interfaces, adapters and devices it has. [IODeviceManager](../Modules/IODeviceManager/Architecture.md)
*operates peripherals*: opens a camera, scans a page. This module reports what is
*happening on the desktop right now* — a window opened, the microphone in use,
bytes flowing — and acts on **windows**, never on devices. Where the answer is
already in the hardware inventory (the Wi-Fi association, the USB device list,
the Bluetooth adapters) it reads it from there, so a desktop and the hardware
panel never disagree about an interface.

## API

### Windows

| Call | Meaning |
|---|---|
| `ListWindows()` | Every managed window in stacking order, bottom first, as `DesktopWindowInfo { id, title, appClass, appName, iconFile, virtualDesktop, processId, active, minimized, skipTaskbar }`. Docks, panels, the desktop and tooltips are included with `skipTaskbar` true so a caller can tell them apart; a taskbar lists the rest. `iconFile` is the application's own icon, resolved from `appClass` through the icon themes ([UltraCanvasDesktopEntry](UltraCanvasDesktopEntry.md)) — for the applications this repository ships, `StartupWMClass` and `Icon=` carry the same name. |
| `GetActiveWindow()` | The focused window's id, 0 if none. |
| `ActivateWindow(id)` | Raise, un-minimize and focus; switches to the window's desktop first. |
| `MinimizeWindow(id)` | Iconify. |
| `CloseWindow(id)` | Ask the window to close through the window manager (`WM_DELETE_WINDOW`), never kill. |

### Virtual desktops

`GetVirtualDesktopCount()`, `GetCurrentVirtualDesktop()` (0-based, −1 when
unknown), `SetCurrentVirtualDesktop(i)`, `SetVirtualDesktopCount(n)` (1..32)
and `MoveWindowToVirtualDesktop(id, i)` (−1 = on every desktop).

### The screen

| Call | Meaning |
|---|---|
| `GetScreenSize(w, h)` | The default screen's size in pixels. |
| `ReserveScreenEdges(windowId, left, right, top, bottom)` | Reserve strips along the screen's edges for a window — a desktop's bars, a dock — so maximised and tiled windows stop short of them (`_NET_WM_STRUT_PARTIAL` and `_NET_WM_STRUT` on X11; the work area shrinks accordingly). Physical pixels, 0 for an edge that reserves nothing, each strip the full length of its edge; call again to change, four zeros to release. `windowId` is the window's native handle. |
| `CaptureScreen(pngPath, &error)` | The whole screen as PNG. The directory is created; false with the reason. |
| `CaptureScreenImage(image, &error)` | The whole screen as pixels in memory, in a `DesktopScreenImage`: `width`, `height`, `stride` and `pixels`, four bytes a pixel in memory order B, G, R, unused — cairo's RGB24, the QR scanner's `BGRA32` — so a row goes to either without a copy. Nothing is written anywhere; for a caller that must not leave a file behind (UltraAuthenticator reads an enrolment QR code off the screen this way, and a PNG of it would be the seed in the clear). False with the reason, and `image` empty. |
| `DefaultScreenshotPath()` | `<Pictures>/Screenshots/Screenshot 2026-09-29 14.05.31.png` — the user's Pictures folder from `GetWellKnownUserFolders`, the home directory when there is none. Dots in the time, since a colon is not a file-name character on Windows. |

### Devices

`ReadDeviceActivity()` returns a `DesktopDeviceActivity`, every field
best-effort and `warnings` saying in words what could not be read:

| Field | Source (Linux) |
|---|---|
| `webcamInUse` | a process of the user's holds a `/dev/video*` descriptor (`/proc/<pid>/fd`) |
| `microphoneInUse`, `speakerPlaying` | `/proc/asound/card*/pcm*c/sub*/status` (capture) and `pcm*p` (playback) say `state: RUNNING` — no sound-server dependency |
| `bluetoothPresent`, `bluetoothPowered`, `bluetoothConnectedDevices` | `UltraCanvasHardwareInfo::ListBluetoothAdapters` |
| `wifiPresent`, `wifiConnected`, `wifiSsid`, `lanConnected`, `vpnConnected`, `internetInterfaceUp` | `UltraCanvasHardwareInfo::ListNetworkInterfaces`; a tunnel is the `Tunnel` link type or a `tun`/`tap`/`wg`/`ppp`/`vpn`/`tailscale`… name |
| `bytesReceived`, `bytesSent` | running totals over every non-loopback interface — keep the previous reading and show the difference |
| `usbDeviceCount` | `UltraCanvasHardwareInfo::ListUSBDevices`, hubs not counted |
| `batteryPresent`, `batteryPercent`, `batteryCharging` | `/sys/class/power_supply/*` of type `Battery` |
| `keyboardLayout` | the XKB layout list the server was configured with, indexed by the group in effect |

It costs one procfs/sysfs walk and one hardware listing; polling it every
second or two is fine.

### Applications

| Call | Meaning |
|---|---|
| `ListApplications(iconSize)` | Every menu-visible desktop entry in the standard `applications` directories (`XDG_DATA_HOME`, then `XDG_DATA_DIRS`), one per desktop-file id — a user's entry shadows the system's, and a hidden user entry hides it — sorted by name, `iconFile` resolved at `iconSize`. Entries whose `TryExec` is not installed are left out. |
| `LaunchApplication(entry, files, &error)` | Start it detached through `DesktopEntryCommand`; a `Terminal=true` entry is wrapped in the terminal the machine has. |
| `LaunchProgram(name, args, &error)` / `FindProgram(name)` | A program by name — `"UltraFiler"` — looked for next to this executable first (a build tree, a bundle), then on `PATH`. |

### Notices

A count an application publishes for the desktop to show: UltraMail publishes
its unread total under `"UltraMail"`, the desktop shows it as the badge on the
mail icon. The transport is one small JSON file per application in
`NoticesDirectory()` (`$XDG_RUNTIME_DIR/ultraos/notices`, else
`$XDG_CACHE_HOME`, else `~/.cache`), written atomically, so a publisher needs
no link to the desktop and the desktop none to the publisher.

`PublishNotice(app, count, text)`, `RemoveNotice(app)`, `ReadNotices()`,
`ReadNotice(app)` (a `DesktopNotice { application, count, text,
updatedUnixSeconds }`; `updatedUnixSeconds` 0 means nobody published).

### The monitor

`UltraCanvasDesktopShellMonitor::Start(onChanged)` reports when the window
list, the active window, the current desktop or the number of desktops
changes; `Stop()` joins, so no callback runs after it returns. The callback
runs on the monitor's thread and must only hand the news over — set an
atomic, post to the UI thread. One change can produce several callbacks; the
receiver coalesces. Where the platform cannot notify, `Start()` succeeds and
nothing is ever reported: `IsNative()` says which.

### Global shortcuts

`UltraCanvasGlobalShortcut` is a key combination that reaches the program
whichever window has the focus — UltraDesktop's `Super+V` for its clipboard
panel:

```cpp
UltraCanvasGlobalShortcut shortcut;   // a member: Stop() runs in its destructor
std::string error;
if (!shortcut.Start("Super+V", [this]() {
        // The shortcut's own thread: hand the press to the UI thread.
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            app->PostToUIThread([this]() { TogglePanel(); });
        }
    }, &error)) {
    debugOutput << "Super+V is not available: " << error << std::endl;
}
```

`Start(accelerator, onPressed, &error)` reads `Super`, `Ctrl`, `Alt` and
`Shift` and one key (`"Ctrl+Alt+H"`, `"Super+V"`); it fails, saying why, when
the combination cannot be read, when another program already holds it, and on
a platform without global shortcuts. `Stop()` joins the thread, so no
callback runs after it returns; `IsRunning()`.

## Backends

| Platform | Backend |
|---|---|
| Linux, BSD | `x11`: the EWMH root-window properties every window manager on ULTRA OS and the Linux desktops maintains (`_NET_CLIENT_LIST_STACKING`, `_NET_ACTIVE_WINDOW`, `_NET_WM_DESKTOP`, `_NET_CURRENT_DESKTOP`, …); actions as the client messages the specification prescribes, so the manager decides how a window is raised or closed; the screenshot through `XGetImage` on the root window into a BGRx buffer, which `CaptureScreenImage` hands over as is and `CaptureScreen` writes with cairo's PNG writer; the monitor on its own connection with `PropertyChangeMask` on the root, sleeping in `poll()` on it and a wake pipe; a global shortcut as a passive `XGrabKey` on the root window (with the Caps Lock and Num Lock variants), on a connection and thread of its own the same way, and `BadAccess` reported as "another program holds it". The queries open a connection of their own, so they run from any thread and without an UltraCanvas window (the headless `UltraDesktop --windows`). |
| Windows, macOS, Android, WebAssembly | `null`: `IsAvailable()` false, every window and desktop query empty, `CaptureScreen`, `CaptureScreenImage` and `UltraCanvasGlobalShortcut::Start` fail with a reason; the application list, launcher and notices still work. |

The split is `ULTRACANVAS_DESKTOPSHELL_NATIVE`, decided in
`UltraCanvasDesktopShellBackend.h` by platform macro; the core file emits the
fallback when it is absent.

## See also

- [UltraDesktop](../UltraDesktop/README.md) — the desktop built on this.
- [UltraCanvasClipboardHistory](UltraCanvasClipboardHistory.md) — the
  clipboard history the desktop's `Super+V` panel shows.
- [UltraCanvasWaveSeparator](UltraCanvasWaveSeparator.md) and the
  [toolbar's item badges and reordering](UltraCanvasToolbarExamples.md) — the
  elements the desktop's bars are made of.
- `WindowType::Desktop` in `UltraCanvasWindow.h` — the screen-sized window at
  the bottom of the stack a desktop draws into; `WindowType::Notification`
  the toast above everything that never takes the focus, which
  [UltraCanvasNotificationToast](UltraCanvasNotificationToast.md) draws
  notifications in.
