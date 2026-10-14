# UltraCanvasDesktopShell

<!-- doc-check: std::shared_ptr<UltraCanvasToolbar> taskbar; void TogglePanel(); -->

The running desktop as a shell sees it: the windows other applications have
open and which one is active, the virtual desktops, the installed applications
a launcher lists, a screenshot of the screen, the live state of the devices an
info panel shows, and the counts an application publishes for the desktop to
show beside its icon. For a desktop that is the session's shell it also hosts
the programs' tray icons and ends the session.

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
| `ListWindows()` | Every managed window in stacking order, bottom first, as `DesktopWindowInfo { id, title, appClass, appName, iconFile, virtualDesktop, processId, active, minimized, skipTaskbar }`. Docks, panels, the desktop and tooltips are included with `skipTaskbar` true so a caller can tell them apart; a taskbar lists the rest. `iconFile` is the application's own icon, resolved from `appClass` through the icon themes ([UltraCanvasDesktopEntry](UltraCanvasDesktopEntry.md)) — for the applications this repository ships, `StartupWMClass` and `Icon=` carry the same name. On Windows `id` is the `HWND`, `appClass` the program's name (`firefox`, `UltraFiler` — for a Microsoft Store application the program inside its frame), `appName` the same in lower case, and `iconFile` the executable's icon as a PNG. |
| `GetActiveWindow()` | The focused window's id, 0 if none. |
| `ActivateWindow(id)` | Raise, un-minimize and focus; switches to the window's desktop first. |
| `MinimizeWindow(id)` | Iconify. |
| `CloseWindow(id)` | Ask the window to close through the window manager (`WM_DELETE_WINDOW`; on Windows `WM_SYSCOMMAND SC_CLOSE`, what the taskbar's *Close window* sends), never kill. |

### Virtual desktops

`GetVirtualDesktopCount()`, `GetCurrentVirtualDesktop()` (0-based, −1 when
unknown), `SetCurrentVirtualDesktop(i)`, `SetVirtualDesktopCount(n)` (1..32)
and `MoveWindowToVirtualDesktop(id, i)` (−1 = on every desktop).

Windows documents none of this. The count, the order and the current desktop
are read where Explorer keeps them (`VirtualDesktopIDs` and
`CurrentVirtualDesktop` in the registry, as PowerToys reads them), a window's
desktop through `IVirtualDesktopManager`. Switching sends Explorer's own
Ctrl+Win+Left/Right, so it needs Explorer running; `SetVirtualDesktopCount`
answers false (the count is the user's, in Task View), and
`MoveWindowToVirtualDesktop` moves only the calling program's own windows and
never to "every desktop".

### The screen

| Call | Meaning |
|---|---|
| `GetScreenSize(w, h)` | The default screen's size in pixels; on Windows the primary monitor's, the one at 0,0 that a desktop window covers. |
| `ReserveScreenEdges(windowId, left, right, top, bottom)` | Reserve strips along the screen's edges for a window — a desktop's bars, a dock — so maximised and tiled windows stop short of them (`_NET_WM_STRUT_PARTIAL` and `_NET_WM_STRUT` on X11; the work area shrinks accordingly). Physical pixels, 0 for an edge that reserves nothing, each strip the full length of its edge; call again to change, four zeros to release. `windowId` is the window's native handle. On Windows each edge is an appbar of its own while Explorer's taskbar runs (placed beside Explorer's bars, and released when the window closes), and the work area itself (`SPI_SETWORKAREA`) when nothing else keeps it — UltraDesktop as the shell; call it from the window's own thread. Asking again for the same strips changes nothing. |
| `CaptureScreen(pngPath, &error)` | The whole screen as PNG. The directory is created; false with the reason. |
| `CaptureScreenImage(image, &error)` | The whole screen as pixels in memory, in a `DesktopScreenImage`: `width`, `height`, `stride` and `pixels`, four bytes a pixel in memory order B, G, R, unused — cairo's RGB24, the QR scanner's `BGRA32` — so a row goes to either without a copy. Nothing is written anywhere; for a caller that must not leave a file behind (UltraAuthenticator reads an enrolment QR code off the screen this way, and a PNG of it would be the seed in the clear). False with the reason, and `image` empty. |
| `DefaultScreenshotPath()` | `<Pictures>/Screenshots/Screenshot 2026-09-29 14.05.31.png` — the user's Pictures folder from `GetWellKnownUserFolders`, the home directory when there is none. Dots in the time, since a colon is not a file-name character on Windows. |

### Devices

`ReadDeviceActivity()` returns a `DesktopDeviceActivity`, every field
best-effort and `warnings` saying in words what could not be read:

| Field | Source (Linux) | Source (Windows) |
|---|---|---|
| `webcamInUse` | a process of the user's holds a `/dev/video*` descriptor (`/proc/<pid>/fd`) | the camera consent store Windows' privacy indicator reads: a program's `LastUsedTimeStart` with no `LastUsedTimeStop` (Windows 10 1903 and later) |
| `microphoneInUse`, `speakerPlaying` | `/proc/asound/card*/pcm*c/sub*/status` (capture) and `pcm*p` (playback) say `state: RUNNING` — no sound-server dependency | an active Core Audio session on any capture or render endpoint |
| `bluetoothPresent`, `bluetoothPowered`, `bluetoothConnectedDevices` | `UltraCanvasHardwareInfo::ListBluetoothAdapters` | the same |
| `wifiPresent`, `wifiConnected`, `wifiSsid`, `lanConnected`, `vpnConnected`, `internetInterfaceUp` | `UltraCanvasHardwareInfo::ListNetworkInterfaces`; a tunnel is the `Tunnel` link type or a `tun`/`tap`/`wg`/`ppp`/`vpn`/`tailscale`… name | the same |
| `bytesReceived`, `bytesSent` | running totals over every non-loopback interface — keep the previous reading and show the difference | the same |
| `usbDeviceCount` | `UltraCanvasHardwareInfo::ListUSBDevices`, hubs not counted | the same |
| `batteryPresent`, `batteryPercent`, `batteryCharging` | `/sys/class/power_supply/*` of type `Battery` | `GetSystemPowerStatus`; on the mains and full counts as charging, as Linux's `Full` does |
| `keyboardLayout` | the XKB layout list the server was configured with, indexed by the group in effect | the input locale of the window being typed into, as its lower-case country (`us`, `de`) |

It costs one procfs/sysfs walk and one hardware listing; polling it every
second or two is fine.

### Applications

| Call | Meaning |
|---|---|
| `ListApplications(iconSize)` | Every menu-visible desktop entry in the standard `applications` directories (`XDG_DATA_HOME`, then `XDG_DATA_DIRS`), one per desktop-file id — a user's entry shadows the system's, and a hidden user entry hides it — sorted by name, `iconFile` resolved at `iconSize`. Entries whose `TryExec` is not installed are left out. On Windows the Start menu's shortcuts instead (`%APPDATA%` and `%ProgramData%` `\Microsoft\Windows\Start Menu\Programs`), the user's shadowing the machine's of the same name: named as the folder's `desktop.ini` names them, programs only (no uninstallers, help files or web links), `program` the shortcut's target, `startupWMClass` its name (so `MatchApplication` finds its windows), `shortcutPath` the `.lnk`, `iconFile` a PNG of its icon. Microsoft Store applications have no shortcut there and are not listed yet. |
| `LaunchApplication(entry, files, &error)` | Start it detached through `DesktopEntryCommand`; a `Terminal=true` entry is wrapped in the terminal the machine has. An entry with a `shortcutPath` is opened the way Explorer opens the shortcut — files, if any, go to its `program`. While no Explorer taskbar runs (the desktop is the shell), a shortcut to a bare `explorer.exe` — the Start menu's *File Explorer* — opens *This PC*: started with nothing to open, Explorer would take over as the session's desktop. |
| `LaunchProgram(name, args, &error)` / `FindProgram(name)` | A program by name — `"UltraFiler"` — looked for next to this executable first (a build tree, a bundle), then on `PATH`. |
| `MatchApplication(window, applications)` | The desktop entry a window (`DesktopWindowInfo`) belongs to, among `ListApplications()`: the entry whose `StartupWMClass` is the window's `WM_CLASS`, else whose program, icon name or name the class or instance spells, case aside (`Gimp-2.10` is `gimp-2.10`'s). `nullptr` when none matches. UltraDesktop's clipboard panel uses it to know what the window being pasted into takes. |

### In place of the system's desktop

<!-- doc-check: std::string program; -->

A desktop takes the place of the system's in one of two ways, a
`DesktopReplacement`:

- **`Beside`** — started at sign-in, while the system's desktop keeps running.
  On Windows: a value *UltraCanvas Desktop* under the user's `Run` key (so it
  is listed among Task Manager's startup apps) and Explorer's taskbar set to
  auto-hide, its state before the first switch recorded. Explorer's tray, its
  notifications, its Start menu and the programs that need it go on working.
  It takes effect at once.
- **`Instead`** — the session's shell: the system's desktop does not start at
  all. On Windows the account's own shell setting (Winlogon's `Shell` under
  `HKEY_CURRENT_USER`, which sign-in reads before the machine's) names
  **UltraCanvasShellGuard**, which must sit beside `program`, followed by
  `program` and its arguments. It takes effect at the next sign-in.
- `Off` — the system's own desktop.

```cpp
std::string error;
if (UltraCanvasDesktopShell::CanReplaceSystemDesktop(DesktopReplacement::Instead) &&
    !UltraCanvasDesktopShell::ReplaceSystemDesktop(program, {"--shell"}, &error,
                                                   DesktopReplacement::Instead)) {
    debugOutput << "Not the shell: " << error << std::endl;
}
```

| Call | Meaning |
|---|---|
| `CanReplaceSystemDesktop(how = Beside)` | Whether this platform has that switch for which desktop a session starts: true on Windows, for both; false where the session itself decides (ULTRA OS, Linux) and elsewhere. |
| `ReplaceSystemDesktop(program, arguments, &error, how = Beside)` | From now on this account's sign-in starts `program` (an absolute path) with `arguments`, beside the system's desktop or instead of it. One at a time: switching to one removes the other. `Instead` is refused, writing nothing, when UltraCanvasShellGuard is not beside `program`. No administrator rights; no other account changes. |
| `RestoreSystemDesktop(&error)` | The way back from either: the start at sign-in removed, the account's shell setting removed when it names the guard (never another program's), and the taskbar given the recorded state — as it was, not a default. True, changing nothing, when nothing was replaced (and on every platform without the switch). |
| `GetSystemDesktopReplacement()` | `Off`, `Beside` or `Instead`, as the account is set now. |
| `IsSystemDesktopReplaced()` | Whether it is anything but `Off`. |
| `IsSystemDesktopRunning()` | Whether the system's own desktop runs in this session — Explorer's taskbar on Windows (a notification area this process hosts does not count). False in a session where a desktop runs `Instead`. |

UltraDesktop switches over with `--use-as-desktop` (beside) or
`--use-instead-of-explorer`, and back with `--restore-desktop`; it passes
itself `--session` or `--shell` for the sign-in, and closes within a second
when a session desktop finds the switch gone — so turning it off in
UOS-Settings brings the Windows desktop back at once. See
[UltraDesktop on Windows](../UltraDesktop/README.md#on-windows).

#### UltraCanvasShellGuard

What the sign-in starts when a desktop runs instead of Explorer
(`OS/MSWindows/ShellGuard/`, built beside the applications). Its one job is
that the screen is never left empty:

- it starts the desktop, and starts it again when it crashes;
- it starts Explorer for the session when the desktop fails three times
  within a minute or cannot start at all — and tells the user why;
- it starts Explorer when the desktop exits by itself (switched back, or
  closed — not when the session is ending, which closes it too), and, should
  the desktop hang, when the account's shell setting
  stops naming the guard — `UltraDesktop --restore-desktop` from Task
  Manager's *Run new task* — after five seconds' grace;
- it does for the session what Explorer would have: tells the sign-in screen
  the desktop is up, hides minimized windows instead of parking them at the
  screen's corner, and starts the programs that start with Windows (the `Run`
  and `RunOnce` keys, the policy `Run` lists, the Startup folders — except
  those switched off in Task Manager).

It is a program of its own, linking nothing of the framework and its C++
runtime statically, so a desktop that cannot even load — a DLL missing after
a half-finished update — still gets its fallback. Started any other way (by
hand, by a test, while Explorer runs) it only starts and restarts the program.
What it did is logged to `%LOCALAPPDATA%\UltraCanvas\ShellGuard.log`. Exit
codes: 0 the desktop exited normally or Explorer took over, 1 it could not be
started, 2 no program named, 3 three failures within a minute.

Without Explorer, Windows has nobody for the parts of its interface Explorer
hosts: the Start menu, the quick settings and the notification centre,
Explorer's own keyboard shortcuts (Win+E, Win+R, Win+X …), and programs that
measure the taskbar (`SHAppBarMessage`) are told there is none.

### The notification area

<!-- doc-check: std::atomic<bool> trayDirty; -->

`UltraCanvasTrayHost` is the system tray for a desktop that is the session's
shell: on Windows the window every program's `Shell_NotifyIcon` talks to
(`Shell_TrayWnd`), on a thread of its own, so a busy desktop never stalls a
program adding its icon.

```cpp
UltraCanvasTrayHost tray;   // a member: Stop() runs in its destructor
std::string error;
if (!tray.Start([this]() { trayDirty.store(true); }, &error)) {   // the host's thread!
    debugOutput << "No tray icons: " << error << std::endl;
}
// On the UI thread, when trayDirty was set:
for (const DesktopTrayIcon& icon : tray.ListIcons()) {
    const std::string id = icon.id;
    taskbar->AddButton("tray-" + id, "", icon.iconFile, [&tray, id]() {
        tray.Click(id, TrayClick::Left);
    });
}
```

| Call | Meaning |
|---|---|
| `Start(onChanged, &error)` | Hosts the notification area and asks the programs already running to add their icons again (`TaskbarCreated`). False, saying why, while another notification area runs — Explorer's taskbar, when the desktop runs beside it; only one can — and on a platform with none to host (X11 here, whose trays are a protocol of their own). `onChanged` runs on the host's thread and must only hand the news over. |
| `Stop()` | Takes the window down; joins, so no callback runs after it returns. `IsRunning()`. |
| `ListIcons()` | The icons, in the order the programs added them, hidden ones left out: `DesktopTrayIcon { id, tooltip, iconFile, appClass, processId, balloonTitle, balloonText, balloonSerial }`. `iconFile` is a PNG of the icon in the disk cache, named after its pixels (an animated icon's frames are each written once); `appClass` the program's name, as `DesktopWindowInfo`'s. A new balloon gets a new `balloonSerial`: a desktop shows each serial once, as a toast. Any thread. |
| `Click(id, button)` | A click at the mouse pointer, handed to the program as the notification area would — in the form the program asked for with `NIM_SETVERSION` (the button message alone; plus `NIN_SELECT` and `WM_CONTEXTMENU`; or the anchor point and the icon's id packed in) — with the foreground handed on, so the program's menu or window can come forward. Two left clicks within the double-click time are a double click. False when the icon is gone. Any thread. |

The host answers `Shell_NotifyIconGetRect` with a square around the point the
icon was last clicked at, so a program's popup opens beside it, and drops the
icon of a program that ended without removing it. It does not answer
`SHAppBarMessage`.

### The end of the session

| Call | Meaning |
|---|---|
| `CanEndSession()` | Whether this backend can sign out, restart and shut down: true on Windows; on Linux the session's own menu does it. |
| `EndSession(how, &error)` | `SessionEnd::SignOut`, `Restart` or `ShutDown`: the system asks every program to close, and a program may still refuse. On Windows `ExitWindowsEx`, with the shutdown privilege every account that may shut down holds switched on first. What a desktop's menu offers when it is the shell and no Start menu runs. |

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
nothing is ever reported: `IsNative()` says which. On Windows the monitor's
thread sets WinEvent hooks (a window shown or hidden, the foreground,
minimizing, the cloaking a desktop switch does) and watches Explorer's
virtual-desktop registry key; the hooks go in a moment after `Start()`
returns, on that thread.

### Global shortcuts

<!-- doc-check: void TogglePanel(); -->

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

The callback runs when the combination's key is **let go**, once however long
it is held. While the key is down the shortcut's grab holds the keyboard, and a
window opened and focused in that time could lose the focus again when the
grab ended; a window opened on release keeps it.

On Windows a shortcut is a `RegisterHotKey` on the shortcut's own thread, and
the callback runs when the combination is pressed, once however long it is
held (there is no grab to lose the focus to). Explorer keeps most Win-key
combinations for itself while it runs — `Super+V` is its clipboard history —
so `Start` reports those as taken by another program; with UltraDesktop as
the shell they are free.

## Backends

| Platform | Backend |
|---|---|
| Linux, BSD | `x11`: the EWMH root-window properties every window manager on ULTRA OS and the Linux desktops maintains (`_NET_CLIENT_LIST_STACKING`, `_NET_ACTIVE_WINDOW`, `_NET_WM_DESKTOP`, `_NET_CURRENT_DESKTOP`, …); actions as the client messages the specification prescribes, so the manager decides how a window is raised or closed; the screenshot through `XGetImage` on the root window into a BGRx buffer, which `CaptureScreenImage` hands over as is and `CaptureScreen` writes with cairo's PNG writer; the monitor on its own connection with `PropertyChangeMask` on the root, sleeping in `poll()` on it and a wake pipe; a global shortcut as a passive `XGrabKey` on the root window (with the Caps Lock and Num Lock variants), on a connection and thread of its own the same way, and `BadAccess` reported as "another program holds it". The queries open a connection of their own, so they run from any thread and without an UltraCanvas window (the headless `UltraDesktop --windows`). No switch at sign-in, tray host or session end: the session owns those. |
| Windows | `win32` (`OS/MSWindows/UltraCanvasWindowsDesktopShell.cpp` and `UltraCanvasWindowsTrayHost.cpp`), with Explorer running beside UltraDesktop or with UltraDesktop as the shell: the window list from `EnumWindows`, filtered as Explorer's taskbar filters (visible; `WS_EX_APPWINDOW`, or unowned and no tool window), cloaked windows on the current desktop left out, the shell's own windows listed with `skipTaskbar`; titles through `InternalGetWindowText`, which sends nothing, so a hung program cannot block the list; actions asynchronous (`ShowWindowAsync`, a posted `SC_CLOSE`), and activation through `SetForegroundWindow`; virtual desktops as described above; the screenshot through `BitBlt` of the whole virtual screen into a 32-bit DIB, already the BGRx layout; the edges as appbars or the work area; the devices from the consent store, Core Audio, the power status and the keyboard layout; the monitor through WinEvent hooks and a registry notification, pumping its own thread's messages in `MsgWaitForMultipleObjectsEx`; a global shortcut through `RegisterHotKey`; the application list from the Start menu; the switch at sign-in through the `Run` key or the account's shell setting and UltraCanvasShellGuard; the notification area as a `Shell_TrayWnd` of its own, reading the `WM_COPYDATA` `Shell_NotifyIcon` sends — `NOTIFYICONDATAW` in its 32-bit layout behind a two-word header, the layout every replacement shell reads; the end of the session through `ExitWindowsEx`. Executable, shortcut and tray icons are extracted into PNG files in the disk cache's `desktop-icons` folder (`%LOCALAPPDATA%\UltraCanvas`) and swept by its rule. |
| macOS, Android, WebAssembly | `null`: `IsAvailable()` false, every window and desktop query empty, `CaptureScreen`, `CaptureScreenImage`, `UltraCanvasGlobalShortcut::Start`, `UltraCanvasTrayHost::Start` and `EndSession` fail with a reason; the application list, launcher and notices still work. |

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
  the bottom of the stack a desktop draws into (on Windows an undecorated tool
  window the size of the primary monitor, held at the bottom, on every
  virtual desktop and out of the taskbar and Alt+Tab); `WindowType::Notification`
  the toast above everything that never takes the focus, which
  [UltraCanvasNotificationToast](UltraCanvasNotificationToast.md) draws
  notifications in.
