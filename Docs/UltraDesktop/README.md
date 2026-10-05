# UltraDesktop

## Overview

UltraDesktop is the ULTRA OS desktop: the taskbar with the running
applications, the desktop organiser with the virtual desktops, the
Stickerboard, the clipboard and the screenshot, and the info panel with the
devices and services — on one screen-sized window under everything else. It
is built from UltraCanvas elements on the
[UltraCanvasDesktopShell](../UltraCanvas/UltraCanvasDesktopShell.md) module,
which is where every question about windows, desktops, devices and installed
applications is answered, so the desktop itself never touches the window
system.

- Source: [`Apps/UltraDesktop`](../../Apps/UltraDesktop/README.md)
- Version: its own, from the first line of [`CHANGELOG.md`](CHANGELOG.md).
  `cmake/UltraCanvasVersion.cmake` reads it into `ULTRADESKTOP_VERSION`,
  which the window title and `--version` print. The app does not move when
  the framework releases.
- Build option: `BUILD_ULTRADESKTOP` (on by default); target and binary
  `UltraDesktop`. It runs straight from the build tree: configuring links
  `build/share/media` to the repository's `media/`, where the bar icons and
  the default wallpaper are found.

## Layout

```
┌────┬──────────────────────────────────────────────────────────────┬────┐
│ ⚙  │                                                              │ 1  │  desktop
│ ⋮⋮ │                                                              │ 2  │  organiser
│╲   │                                                              │ 3  │
│ ╲  │                                                              │ 📌 │  Stickerboard
│ ▣  │                     wallpaper                                │ 📋 │  clipboard
│ ▣  │            (application windows float above)                 │ 📷 │  screenshot
│ ▣  │                                                              │╱   │
│ ▣  │                                                              │    │
│  ╲ │                                                              │ ✉14│  info panel
│   ╲│                                                              │ ⇧⇩ │  (devices and
│ ▤  │                                                              │ ⌨ ●│   services)
│ 📁 │                                                              │ 🔋 │
└────┴──────────────────────────────────────────────────────────────┴────┘
 taskbar                                                               right bar
```

### Taskbar

On the left by default; the settings move it to the top or bottom edge, and
`--edge top` does so for one run. Three groups, joined by the S-curve wave
separators:

1. **System** — *ULTRA OS settings* and the *app starter*. *ULTRA OS
   settings* starts **UOS-Settings**, the system's settings application
   (`Docs/UOSSettings/README.md`), which opens on its *Desktop* page.
2. **Running applications** — one button per open window on the current
   desktop, with the application's own icon (resolved from its desktop
   entry; the first letter of its class when it has none). The active
   window's button is highlighted. A click activates the window or minimizes
   it if it already is active; a right-click offers *Activate*, *Minimize*,
   *Move to desktop …* and *Close window*. Drag a button to reorder; when
   the group is full it scrolls with the mouse wheel. New windows join next
   to their application's other windows.
3. **Pinned** — the *RAM disc* (opens the path from the settings, `/dev/shm`
   by default, in the file manager) and *UltraFiler*.

The taskbar's edge and the right bar are reserved with the window manager
(`ReserveScreenEdges` in the module), so maximised and tiled application
windows stop short of the bars instead of covering them.

### Right bar

- **Desktop organiser**: the virtual desktops as numbered toggles — as many
  as the window manager has (read at start-up and followed when it changes),
  the settings' count only where there is no window manager to ask; choosing
  a count in the settings asks the window manager for that many — the
  *Stickerboard* toggle, the *clipboard*
  (a menu of the last fifteen things copied; choosing one puts it back on
  the clipboard) and *screenshot* (the whole screen to
  `~/Pictures/Screenshots/Screenshot <date> <time>.png`; the button shows a
  green dot for three seconds and its tooltip names the file).
- **Info panel**, anchored to the bottom: Email, Upload, Download,
  Internet/LAN, VPN, Bluetooth, Wi-Fi, USB, Keyboard, Webcam, Microphone,
  Loudspeaker, Battery and Task Manager. The markers:

  | Marker | Meaning | On |
  |---|---|---|
  | red dot | a device is on, or a link is down | webcam in use, microphone recording, Bluetooth powered; Internet/LAN with no link, Wi-Fi present but not joined |
  | yellow dot | activity | download or upload traffic (more than 4 KB in two seconds), loudspeaker playing, VPN up |
  | grey pill | a count or a value | Email unread (from UltraMail), USB devices attached, battery percentage (green while charging, red at 15 % and below), keyboard layout |

  The tooltip of every icon says it in words ("Wi-Fi: HomeNet", "Download:
  1.2 MB/s", "Battery: 84%, charging"). Clicking opens what the icon is
  about: Email starts UltraMail, the network icons UltraNetMonitor, USB,
  keyboard and webcam DeviceExplorer, the rest the Task Manager.

  When the organiser leaves the panel short of room — five or more desktops
  on a 900 px screen — the panel scrolls rather than losing its last items:
  a chevron over the edge marks where the icons continue, a click on it
  scrolls a page, and so does the wheel over the panel.

### Windows the desktop opens

- **Applications** (app starter): every installed application from its
  desktop entry as an icon tile, a filter box above them, one click to
  start. What the application menu of any other desktop shows, so an
  application installed by its package appears without being told about
  ULTRA OS.
- **Task Manager**: the open windows with *Activate* and *Close* on the
  first tab, the machine — CPU load and temperature, memory, storage, the
  interfaces — on the framework's hardware panel on the second.
- **Settings**: the desktop's settings - the taskbar's edge, the wallpaper,
  the RAM disc path, the file manager program, the number of virtual
  desktops - are the *Desktop* page of UOS-Settings. They live in the
  desktop's settings file; the desktop checks it once a second and rebuilds
  the bars in place when UOS-Settings changed one of them.
- **Stickerboard**: sticky notes over the wallpaper. *+* in the corner adds
  one; each note is edited in place, dragged by its top bar, cycled through
  six paper colours and closed with ×. Notes come back where they were.

### Notifications

The desktop is also where notifications appear. On ULTRA OS no other
notification server runs: UltraMessage serves `org.freedesktop.Notifications`
itself (the desktop usually hosts the UltraMessage broker, being the first
program of the session), so every application's notification - Telegram,
the browser, a download, UltraMail's new mail - arrives on the UltraMessage
bus. The desktop draws each one as a toast in the top-right corner, beside the
right bar and below the taskbar when that runs along the top: the
application's icon and name, the summary, the body, the notification's own
buttons and a close button, newest on top, four at most.

- A click on the text does what the notification offers by default (UltraMail
  opens the mail); a button does what it says; × dismisses it. The
  application is told either way.
- A toast goes by itself after 8 seconds (5 for a low-priority one) - not
  while the pointer rests on it - and a critical one (battery low) stays until
  closed. Gone from the screen is not gone: the message feed keeps it.
- On a desktop with its own notification server (GNOME, Plasma, dunst, ...)
  that server draws them and the desktop draws nothing, so nothing appears
  twice.

The toasts are the framework's
[UltraCanvasNotificationToast](../UltraCanvas/UltraCanvasNotificationToast.md)
element and host, in windows that stay above everything and never take the
keyboard focus.

## Command line

```
UltraDesktop                    open the desktop
UltraDesktop --edge bottom      taskbar on the bottom edge for this run
UltraDesktop --settings <file>  another settings file
UltraDesktop --windows          print the open windows and exit
UltraDesktop --apps             print the installed applications and exit
UltraDesktop --devices          print the device activity and exit
UltraDesktop --screenshot [f]   capture the screen (default: the Pictures folder)
UltraDesktop --version | --help
```

The headless modes print what the module sees, which makes it checkable over
ssh (`--windows` needs a display; `--apps` and `--devices` do not).

## Settings file

`~/.config/ultraos/desktop.json` (`$XDG_CONFIG_HOME` when set):

```json
{
  "taskbarEdge": "left",
  "wallpaper": "",
  "ramDiscPath": "/dev/shm",
  "filerProgram": "UltraFiler",
  "virtualDesktops": 3,
  "stickerboardVisible": false,
  "stickers": [ { "id": "note1", "text": "…", "x": 60, "y": 60, "width": 220, "height": 160, "color": "#FFF59D" } ]
}
```

An empty wallpaper shows the framework's `media/images/landscape.jpg`.

## How the mail count gets there

UltraMail publishes its unread total through
`UltraCanvasDesktopShell::PublishNotice("UltraMail", count, text)` whenever
its account bar refreshes; the desktop reads that notice every five seconds.
Any application can publish a count the same way, and the desktop will show
the ones it has an icon for.

## Platform

The window list, the virtual desktops, the screenshot and the device markers
need the module's X11 backend (Linux, the BSDs). Elsewhere the desktop still
runs — application list, launcher, settings and Stickerboard work — but the
running-apps group stays empty and the Task Manager says so.
