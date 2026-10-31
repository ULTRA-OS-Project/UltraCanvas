# UltraDesktop

The ULTRA OS desktop: one screen-sized window at the bottom of the stack
holding the wallpaper, the taskbar with the running applications on the left
(or the top or bottom edge), and the right bar with the desktop organiser and
the info panel. Every control on the bars is an UltraCanvas element and
everything they show or do goes through the
[`UltraCanvasDesktopShell`](../../Docs/UltraCanvas/UltraCanvasDesktopShell.md)
module.

User manual: [`Docs/UltraDesktop/Manual.md`](../../Docs/UltraDesktop/Manual.md).
Reference: [`Docs/UltraDesktop/README.md`](../../Docs/UltraDesktop/README.md).
Changelog and version: [`Docs/UltraDesktop/CHANGELOG.md`](../../Docs/UltraDesktop/CHANGELOG.md).

## Layout

| Path | What is in it |
|---|---|
| `ui/UltraDesktopSettings.*` | No UI: the settings (taskbar edge, wallpaper, RAM disc, file manager, virtual desktops, the sticky notes, the Sticker board, the pinned applications) as JSON in `~/.config/ultraos/desktop.json`, through `UltraCanvasJSON` |
| `ui/UltraDesktopWindow.*` | The desktop window: the bars (`UltraCanvasToolbar` groups joined by `UltraCanvasWaveSeparator`), the clock (`UltraCanvasGaugeDiagramElement` in `AnalogClock` mode over a date label), the wallpaper (`UltraCanvasImageElement`), the running-apps list fed by the shell monitor, the pinned applications, the info panel fed by the device poll thread (the loudspeaker's volume every fifth poll) and the notices, the window, pin, tray and settings menus (lock, sign out, restart, shut down), the clipboard history's recorder and `Super+V`, the Super tap and `Super+L`, the notification toasts (`UltraCanvasNotificationToastHost`, top right beside the right bar), the tray icons where the desktop hosts the notification area, and on Windows as the shell a wallpaper window for each other monitor |
| `ui/UltraDesktopPopupPanel.*` | What the small panels beside the bars share: an undecorated window placed by its top-right corner, closed on blur and Escape; the clock and volume panels derive from it |
| `ui/UltraDesktopClockPanel.*` | The clock panel: the gauge element's analog and digital clocks, today in words, the month on `UltraCanvasCalendarView` with today marked; also the date and time words the bar shows |
| `ui/UltraDesktopVolumePanel.*` | The volume panel: the output's name, an `UltraCanvasSlider` for the volume (written a moment after the hand stops) and an `UltraCanvasSwitch` for the mute, through `UltraCanvasDesktopShell::ReadAudioOutput` and the setters |
| `ui/UltraDesktopClipboardPanel.*` | The clipboard quick panel: an undecorated window beside the bar (centred for `Super+V`) with the history's search, list and recording switch, closed on blur, Escape and after a copy |
| `ui/UltraDesktopStickerboard.*` | The free notes over the wallpaper (the note button; the pinboard opens the Sticker board): an `UltraCanvasTextArea` on a coloured card, dragged by its bar, persisted in the settings |
| `ui/UltraDesktopCategoryBoard.*` | The Sticker board: an `UltraCanvasKanbanBoardElement` over the wallpaper whose columns are categories (Docs, Work, Home by default); files dropped on a category become cards in it (`onFilesDropped`), a bar with an `UltraCanvasDropdown` for the display style and an "Add category" button (which scrolls to the new category), a corner label to resize the panel by; board, style, place and size persisted in the settings. The pinboard button opens it |
| `ui/UltraDesktopWifiPanel.*` | The Wi-Fi panel (`UltraDesktopPopupPanel`): the radio on an `UltraCanvasSwitch`, the networks as rows with Connect / Disconnect / Forget, a password row (`CreatePasswordInput`) for a new secured network, Scan; every `UltraCanvasWireless` call on a worker thread |
| `ui/UltraDesktopBluetoothPanel.*` | The Bluetooth panel: the radio, the paired devices with Connect / Disconnect / Remove, Add device (a minute's discovery) and the found devices with Pair; the same worker-thread arrangement |
| `ui/UltraDesktopDevicePanel.*` | The USB, keyboard, webcam, microphone and battery panels: one class that shows entries read on a worker thread by a function the window hands it (`UltraDesktopWindow::OpenDevicePanel`), an entry's own button run off the UI thread (the USB drives' *Safely remove*, `UltraCanvasRemovableDrives`), and buttons on to Device Explorer or the Task Manager |
| `ui/UltraDesktopPanelWork.h` | `UltraDesktopPanelWork::Run`: a call on a thread of its own, its answer handed to the UI thread with `PostToUIThread` unless the panel has gone meanwhile |
| `ui/UltraDesktopAppStarter.*` | The Apps window: tiles from `UltraCanvasDesktopShell::ListApplications` with a filter box; a tile's right click pins it to the taskbar through the desktop's callbacks |
| `ui/UltraDesktopTasksWindow.*` | The Task Manager: the open windows with Activate and Close, and the machine on `UltraCanvasHardwareInfoPanel` |
| `main.cpp` | Bootstrap and the command line: `--edge`, `--settings`, `--windows`, `--apps`, `--devices`, `--screenshot`, `--version`, `--help` |
| `UltraDesktop.desktop` | The freedesktop entry (NoDisplay: the session starts the desktop, not the menu); `make install` places it with the app icon (`media/appicon/UltraDesktop.png` / `.svg`) in the `hicolor` theme |

Bar icons are in `media/icons/desktop/` — monochrome SVGs the toolbar tints
through the icon mask.

## Data flow

- **Windows.** `UltraCanvasDesktopShellMonitor` sets a flag on its thread
  when a window opens or closes or the active window or desktop changes; the
  250 ms UI timer re-reads `ListWindows()` and updates the running-apps
  group: a toggle per window, pressed for the active one, new windows placed
  next to their application's other windows, gone ones removed. The user's
  drag order is kept for the session.
- **Devices.** A worker thread calls `ReadDeviceActivity()` every two
  seconds and parks the reading; the UI timer turns it into badges: red dots
  for webcam, microphone, Bluetooth on and a link that is down; yellow for
  download, upload, speaker and VPN; pills for the USB count, the battery
  percentage and the keyboard layout; the tooltips carry the words.
- **Notices.** Every five seconds the mail notice is read; a count becomes
  the badge on the mail icon.
- **The loudspeaker.** Every fifth device poll (ten seconds) reads
  `ReadAudioOutput()` on the poll thread; the UI timer puts the volume into
  the speaker icon's tooltip and a red dot on it while muted. The volume
  panel reads it again when it opens and after each change it writes.
- **The clock.** The gauge element redraws its face every second on its own
  timer; the UI timer changes the date label and the tooltip once a minute.
- **Pins.** `settings_.pinnedApps` holds keys (`PinKey`: the program's path,
  or the shortcut / Store id on Windows); `BuildLayout` resolves them against
  `ListApplications()` (cached five minutes) into the pinned group's buttons.
  Pinning saves the settings and rebuilds the bars.
- **Notifications.** An `UltraCanvasNotificationToastHost` connected to the
  UltraMessage bus at start (hosting the broker when the desktop is the
  first program of the session) draws every `system.notification` that
  nothing else draws - on ULTRA OS, where UltraMessage serves
  `org.freedesktop.Notifications`, that is all of them - and answers clicks
  and closes on the bus. Its margins follow the bars when the taskbar
  moves (`PlaceNotifications`). Linked when `UltraMessageCenter` is built
  (`ULTRADESKTOP_HAVE_NOTIFICATIONS`).
- **Settings.** Apply in the settings window writes the file and rebuilds
  the bars in place.
