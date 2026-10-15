# UltraDesktop

The ULTRA OS desktop: one screen-sized window at the bottom of the stack
holding the wallpaper, the taskbar with the running applications on the left
(or the top or bottom edge), and the right bar with the desktop organiser and
the info panel. Every control on the bars is an UltraCanvas element and
everything they show or do goes through the
[`UltraCanvasDesktopShell`](../../Docs/UltraCanvas/UltraCanvasDesktopShell.md)
module.

User guide: [`Docs/UltraDesktop/README.md`](../../Docs/UltraDesktop/README.md).
Changelog and version: [`Docs/UltraDesktop/CHANGELOG.md`](../../Docs/UltraDesktop/CHANGELOG.md).

## Layout

| Path | What is in it |
|---|---|
| `ui/UltraDesktopSettings.*` | No UI: the settings (taskbar edge, wallpaper, RAM disc, file manager, virtual desktops, the sticky notes, the Sticker board) as JSON in `~/.config/ultraos/desktop.json`, through `UltraCanvasJSON` |
| `ui/UltraDesktopWindow.*` | The desktop window: the bars (`UltraCanvasToolbar` groups joined by `UltraCanvasWaveSeparator`), the wallpaper (`UltraCanvasImageElement`), the running-apps list fed by the shell monitor, the info panel fed by the device poll thread and the notices, the window menu, the clipboard history's recorder and `Super+V`, the notification toasts (`UltraCanvasNotificationToastHost`, top right beside the right bar), and on Windows as the shell the tray icons, the Win key's tap and a wallpaper window for each other monitor |
| `ui/UltraDesktopClipboardPanel.*` | The clipboard quick panel: an undecorated window beside the bar (centred for `Super+V`) with the history's search, list and recording switch, closed on blur, Escape and after a copy |
| `ui/UltraDesktopStickerboard.*` | The free notes over the wallpaper (the note button; the pinboard opens the Sticker board): an `UltraCanvasTextArea` on a coloured card, dragged by its bar, persisted in the settings |
| `ui/UltraDesktopCategoryBoard.*` | The Sticker board: an `UltraCanvasKanbanBoardElement` over the wallpaper whose columns are categories (Docs, Work, Home by default); files dropped on a category become cards in it (`onFilesDropped`), a bar with an `UltraCanvasDropdown` for the display style and an "Add category" button (which scrolls to the new category), a corner label to resize the panel by; board, style, place and size persisted in the settings. The pinboard button opens it |
| `ui/UltraDesktopAppStarter.*` | The Apps window: tiles from `UltraCanvasDesktopShell::ListApplications` with a filter box |
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
