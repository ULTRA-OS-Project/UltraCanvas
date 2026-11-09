# UltraDesktop

## Overview

UltraDesktop is the ULTRA OS desktop: the taskbar with the running
applications, the desktop organiser with the virtual desktops, the
Sticker board, the notes, the clipboard and the screenshot, and the info panel with the
devices and services — on one screen-sized window under everything else. It
is built from UltraCanvas elements on the
[UltraCanvasDesktopShell](../UltraCanvas/UltraCanvasDesktopShell.md) module,
which is where every question about windows, desktops, devices and installed
applications is answered, so the desktop itself never touches the window
system.

- User manual: [`Manual.md`](Manual.md) — the desktop task by task, using it
  as the Windows desktop and getting the Windows desktop back, and
  troubleshooting. This README is the reference.
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
│ ⚙  │                                                              │ 🕒 │  clock
│ ⋮⋮ │                                                              │30Oct│  and date
│╲   │                                                              │ 1  │  desktop
│ ╲  │                                                              │ 2  │  organiser
│ ▣  │                     wallpaper                                │ 3  │
│ ▣  │            (application windows float above)                 │ 📌 │  Sticker board
│ ▣  │                                                              │ 🗒 │  notes
│  ╲ │                                                              │ 📋 │  clipboard
│   ╲│                                                              │ 📷 │  screenshot
│ ▤  │                                                              │╱   │
│ 📁 │                                                              │ ✉14│  info panel
│ 🎨 │                                                              │ ⇧⇩ │  (devices and
│    │                                                              │ 🔊 │   services)
└────┴──────────────────────────────────────────────────────────────┴────┘
 taskbar                                                               right bar
```

### Taskbar

On the left by default; the settings move it to the top or bottom edge, and
`--edge top` does so for one run. Three groups, joined by the S-curve wave
separators. The two waves mirror each other, so the dark band of running
applications narrows the same way at both ends, and each wave starts 4 px
from the icon next to it - as the right bar's wave does below the
organiser.

1. **System** — *ULTRA OS settings*, shown as the ULTRA OS logo, and the
   *app starter*. *ULTRA OS settings* starts **UOS-Settings**, the system's settings application
   (`Docs/UOSSettings/README.md`), which opens on its *Desktop* page.
2. **Running applications** — one button per application with open
   windows on the current desktop, with the application's own icon
   (resolved from its desktop entry, on Windows a Store application's tile
   icon from the shell; the first letter of its class when it has none).
   An application's windows share its button - by their class on X11, on
   Windows by the application identity the shell gives them
   (`appUserModelId`: every Explorer window is Explorer's), else by their
   program - and a count on the button says how many there are. The button
   is highlighted while one of its windows is active.
   - **One window:** a click activates it, or minimizes it if it already is
     active; a right-click offers *Activate*, *Minimize*, *Move to desktop
     …*, *Pin to taskbar* and *Close window*.
   - **Several windows:** rest the pointer on the button and their pictures
     open beside it (above it with the taskbar at the bottom, below it at
     the top) - one tile per window with its icon, title and a picture of
     the window, the active one framed, a minimized one showing its icon.
     A click on a tile brings that window forward, its **×** asks it to
     close. The pictures follow the pointer along the bar to the next
     application and close shortly after it leaves both. A click on the
     button opens or closes them at once. A right-click lists the windows,
     with *Minimize all*, *Pin to taskbar* and *Close all windows*. On
     Windows each window draws its own picture and on Wayland the
     compositor copies it, so a covered window shows its own content; on
     X11 the picture is the window's place on the screen, which shows
     whatever lies on top of it there. A Wayland compositor without window
     capture (`ext-image-copy-capture-v1`) gives no picture: the tile shows
     the application's icon.

   Drag a button to reorder; when the group is full it scrolls with the
   mouse wheel. New applications join at the end.
3. **Pinned** — the *RAM disc* (opens the path from the settings, `/dev/shm`
   by default, in the file manager), *UltraFiler*, and the applications the
   user pinned - from a running window's menu or a tile's in the
   Applications window - each with its own icon, one click to start, and
   *Unpin from taskbar* on its right-click menu. The pins are kept in the
   settings by the program's path (on Windows the shortcut or the Store
   application's id), so a renamed application stays pinned, and one that is
   uninstalled shows again when it is back.

The settings button's right-click menu also offers *Lock screen* (Super+L)
and - on Linux, and on Windows as the shell - *Sign out*, *Restart* and
*Shut down*, each asked first, through
[UltraCanvasDesktopShell](../UltraCanvas/UltraCanvasDesktopShell.md)
(logind on Linux, `ExitWindowsEx` and `LockWorkStation` on Windows).

The taskbar's edge and the right bar are reserved with the window manager
(`ReserveScreenEdges` in the module), so maximised and tiled application
windows stop short of the bars instead of covering them.

With several monitors the desktop window - the bars and the wallpaper -
covers the **primary** monitor (the one the session marks, else the one at
the origin), and every other monitor gets a wallpaper window of its own; a
monitor plugged in later is covered within seconds, and when the primary
moves (`xrandr --output <name> --primary`, the display settings) the desktop
follows within seconds - its window is made anew on the new primary, the
bars with it, and the old one closes. On X11 an
edge is reserved only where the primary lies on that edge of the whole
screen: with the primary on the left of a second monitor the taskbar on its
left edge is reserved, the right bar between the two is not (a strut is
measured from the screen's edge, and one across the other monitor would push
its windows aside), so a maximised window on the primary may cover the right
bar there - put the taskbar on the top or bottom edge in that layout, or make
the right-hand monitor the primary.

### Right bar

- **Clock**, at the top of the bar, white on black (black on white in the
  light theme): by default the
  framework's gauge element in its analog clock mode
  ([UltraCanvasGaugeDiagramElement](../UltraCanvas/UltraCanvasGaugeExamples.md)),
  which ticks on its own, over the date ("30 Oct"); the tooltip has the
  whole date and time. A right click chooses how it shows: *Analog clock
  with the date*, *Analog clock*, *Digital clock with the date*, *Digital
  clock* (21:50) or *Digital clock with seconds* (21:50:52), kept as
  `clockStyle` in the settings. A click opens the clock panel beside the
  bar: the time large as an analog face and in figures (the gauge's digital
  clock), today in words, and the month on the calendar view with today
  marked.
- **Desktop organiser**: the virtual desktops as numbered toggles — as many
  as the window manager has (read at start-up and followed when it changes),
  the settings' count only where there is no window manager to ask; choosing
  a count in the settings asks the window manager for that many — the
  *Sticker board* (the pinboard) and the *notes* toggles, the *clipboard* and *screenshot*. A click
  on the screenshot button opens [UltraSnip](../UltraSnip/README.md) to snip
  an area; its right-click menu offers a window, the whole screen, a video
  recording of each, *Screenshot now, to Pictures* (the whole screen to
  `~/Pictures/Screenshots/Screenshot <date> <time>.png` without UltraSnip; the
  button shows a green dot for three seconds and its tooltip names the file)
  and *Open UltraSnip*; `Super+Shift+S` from any window snips an area. Where
  UltraSnip is not installed beside the desktop, the click takes the plain
  screenshot.
- **Clipboard**: the desktop records every copy into the clipboard history
  ([UltraCanvasClipboardHistory](../UltraCanvas/UltraCanvasClipboardHistory.md)),
  on disk and encrypted, leaving out copies a password manager marks as
  secret. The clipboard button, or `Super+V` from any window, opens the quick
  panel: a search field, the pinned entries and the ten latest (typing
  searches the whole history), each with a thumbnail and Copy, Edit and
  Delete. What the program being pasted into takes comes first, under "For
  <program>" and chosen - the newest images for a paint program, files for
  a file manager, code and text for an editor - from its desktop entry's
  categories and file types; the bar's button uses the window that had the
  focus before it was clicked. Up / Down choose, Enter copies, Delete deletes (with Undo), F2
  edits in [UltraClipboard](../UltraClipboard/README.md), Escape closes; the
  *Recording* switch pauses the history and *Open UltraClipboard* shows all
  of it. Right-click the button for *Pause recording*, *Open UltraClipboard*
  and *Clear history*; while recording is paused the button is crossed out.
  When a program that made a copy quits, the desktop puts that copy back on
  the clipboard, so it can still be pasted. Without a history (a build
  without SQLite) the button shows a menu of this session's last fifteen
  copies, as before.
- **Info panel**, anchored to the bottom: Email, Notifications, Upload, Download,
  Internet/LAN, VPN, Bluetooth, Wi-Fi, USB, Keyboard, Webcam, Microphone,
  Loudspeaker, Battery and Task Manager. On a screen less than 960 pixels
  high the icons are smaller, and those that still do not fit fold into a
  chevron menu at the bar's foot, least used first - keyboard, VPN, USB,
  upload, Task Manager, download, LAN, email, notifications - so the webcam's and
  microphone's in-use dots stay in sight. The markers:

  | Marker | Meaning | On |
  |---|---|---|
  | red dot | a device is on, or a link is down | webcam in use, microphone recording, Bluetooth powered; Internet/LAN with no link, Wi-Fi present but not joined |
  | green dot | a link is up | Wi-Fi joined |
  | yellow dot | activity | download or upload traffic (more than 4 KB in two seconds), loudspeaker playing, VPN up |
  | grey pill | a count or a value | Email unread (from UltraMail), notifications unread (from UltraNotifications, while it runs), USB devices attached, battery percentage (green while charging, red at 15 % and below), keyboard layout |

  The tooltip of every icon says it in words ("Wi-Fi: HomeNet", "Download:
  1.2 MB/s", "Battery: 84%, charging", "Loudspeaker: 63%, playing"). Clicking
  opens what the icon is about: Email starts UltraMail, the bell
  UltraNotifications (every notification after its pop-up has gone), the traffic, LAN and
  VPN icons UltraNetMonitor, Wi-Fi and Bluetooth their panels (below), USB,
  keyboard, webcam, microphone and battery a panel saying what is attached
  or in use with a button on to Device Explorer or the Task Manager, the
  loudspeaker its volume panel, and the last icon the Task Manager.
- **USB**: the USB panel lists the drives plugged in (USB sticks, external
  disks, memory cards - never the system's own) with their size and where
  they are mounted, each with *Safely remove*: its volumes are unmounted once
  everything is written out and the drive is powered off, after which the
  panel says it can be pulled out - or that a program still has a file open
  on it. Below them, the other USB devices. The list follows what is
  plugged in while the panel is open. Through UDisks2 on Linux,
  Windows' "Safely Remove Hardware", and Finder's Eject on macOS.
- **Wi-Fi**: the Wi-Fi panel beside the bar - Wi-Fi on a switch, the network
  joined, and the networks in reach with their signal and security. *Connect*
  joins a saved or open network at once and asks for the password of a new
  one (kept only once the network has been joined, so a mistyped password is
  not saved); *Disconnect* leaves the network, *Forget* deletes its saved
  password, *Scan* looks again, *Traffic* opens UltraNetMonitor. The list is
  read again every five seconds while the panel is open. Through
  NetworkManager on Linux, Windows' Wi-Fi service, and CoreWLAN on macOS,
  which shows network names only once Location Services allows UltraDesktop
  (the panel says so until then). On Windows and macOS *System settings*
  opens the system's own page.
- **Bluetooth**: the Bluetooth panel - Bluetooth on a switch, *My devices*
  with *Connect* / *Disconnect* and *Remove* (unpair), the device's kind and,
  where it reports it, its battery, and *Add device*, which looks for devices
  in pairing mode for a minute and lists them with *Pair* (pair, trust,
  connect). Through BlueZ on Linux, the Bluetooth API on Windows, IOBluetooth
  on macOS. Windows switches Bluetooth on and off only in its own settings,
  which *System settings* opens, and shows its own prompt when a device wants
  a code. On Linux and macOS the panel shows the code itself: the six digits
  to type on a keyboard (then Enter), *Yes, pair* / *No* when a phone shows a
  number to compare, or a field for a PIN. On Linux UltraDesktop is the
  system's pairing agent from the start, so when a phone starts pairing
  with the computer, or a device that is not trusted asks to connect, the
  panel opens by itself and asks.
- **Loudspeaker**: the volume panel beside the bar - the output's name, its
  volume on a slider, its mute on a switch - through the sound server
  (PulseAudio or PipeWire on Linux, Core Audio on Windows; the panel says so
  when none answers). The icon shows a red dot while muted and reads the
  volume again every ten seconds, so a change made with the keyboard's
  volume keys shows.
- **Tray icons**: where the desktop hosts the notification area - as the
  shell on Windows, and on Linux, where it is the StatusNotifierWatcher the
  tray programs register with - the programs' icons sit above the services.
  A click opens the program; a right click opens its menu, which on Linux
  the program hands over as data and the desktop draws itself, submenus
  and check marks included.

  When the organiser leaves the panel short of room — five or more desktops
  on a 900 px screen — the panel scrolls rather than losing its last items:
  a chevron over the edge marks where the icons continue, a click on it
  scrolls a page, and so does the wheel over the panel.

### Theme and context menus

The bars come in two themes, kept as `theme` in the settings: **dark**
(light icons on dark grey, the running applications and the organiser
carved in near black, the clock white on black - the default) and
**light** (dark icons on light grey, the carved-in groups near white, the
clock black on white, the ULTRA OS logo in its variant for a light ground).
The panels and windows the bars open keep their own colours.

- **The wallpaper** (right click): *Change wallpaper...* (an Open dialog
  with every picture type this build reads, through FileLoader), *Default
  wallpaper* (when another is set), *Theme* (*Dark*, *Light*), *Taskbar
  position* (*on the left*, *at the top*, *at the bottom*), *Notes on the
  wallpaper*, *Display settings...* and *Desktop settings...* (UOS-Settings).
  *Display settings...* opens the system's own
  (`UltraCanvasDesktopShell::OpenDisplaySettings`): Settings > Display on
  Windows, System Settings > Displays on macOS, and on Linux the first
  display tool installed - the session's own desktop's first
  (`XDG_CURRENT_DESKTOP`): GNOME's, Plasma's, Xfce's, Cinnamon's, MATE's,
  LXQt's or Budgie's settings, then wdisplays, nwg-displays, and on X11
  arandr and lxrandr. Where there is none the entry is left out.
- **The bars** (right click on a group, where the button under the pointer
  has no menu of its own - between buttons, the Applications button, a
  virtual desktop, a service icon): the taskbar's edge, the theme and
  *Desktop settings...*. The ⚙ button's menu has *Taskbar position* and
  *Theme* as submenus.

A change is saved at once and the bars are rebuilt in place; one made in
UOS-Settings is followed within a second.

### Windows the desktop opens

- **Applications** (app starter): every installed application from its
  desktop entry, one click to start; a right click pins it to the taskbar
  or unpins it. What the application menu of any other desktop shows, so an
  application installed by its package appears without being told about
  ULTRA OS. A tap of the Super key opens it from any window - on Linux, and
  on Windows as the shell (beside Explorer the key is the Start menu's).
  - **Categories.** *Recent* (the last applications started - from this
    window or a taskbar pin - eight by default), then *Office*, *Internet*,
    *Graphics*, *Video*, *Audio*, *CAD*, *Games*, *Tools* and *System*;
    an empty one is left out. The category is the one the application's
    desktop entry declares (`Categories=Network;WebBrowser` is Internet). A
    Windows Start-menu shortcut declares none, so its name, its program and
    its Start-menu folder are matched against well-known applications and
    words in English and German (Excel, Outlook, *Datenträgerbereinigung*,
    a shortcut under *Windows Tools*); what matches nothing is in Tools.
    The names come from a catalogue of about 2,400 well-known Windows,
    macOS and Linux applications (`UltraDesktopAppCatalogue.cpp`); on macOS
    a bundle's `LSApplicationCategoryType` is read as its category.
  - **Folders.** On Windows a Start-menu folder holding five or more
    applications - a maker's folder such as MAGIX, Lenovo or LAV Filters - is
    a category of its own, after the others, named after the folder; on
    macOS so is such a folder under `/Applications` (not `/Applications` or
    `Utilities` themselves).
    Windows' own folders (Windows Tools, Accessories, System Tools) are not:
    their applications are in System.
  - **Moving an application.** Drag a tile onto another category's heading
    or tiles, or a row of the list onto a category in the tree; or choose
    *Move to* on its right-click menu. It stays there (`appMoves` in the
    desktop's settings); *Back to …* - or dragging it back - returns it to
    the category its own name gives it.
  - **Sub-categories.** A category with more than eight applications (UOS-
    Settings > Desktop > *Sub-categories*) is split into its kinds - Web
    browsers, Email, Painting, Photos, Text editors; for Games 3D, Retro,
    Board games, Adventure, Strategy and Simulation - with *Other* last.
  - **Two views**, switched by the two icons at the window's top left and
    remembered: *Tiles*, each category a heading over a grid of tiles as on
    Windows' Start, the name under the icon on as many lines as it needs
    (four, then "…"; the tooltip has the whole name); and *Categories and a
    list*, the categories as a tree, their sub-categories under them, beside
    a list of the chosen one's applications.
  - **Opens at once.** The window opens first and says it is loading; the
    applications come after it, read on a thread of their own. The desktop
    reads them once ahead of time, five seconds after it starts, so the
    window usually shows them straight away and reads them again behind
    them.
- **Task Manager**: the open windows on the first tab, under a header -
  each with its application's icon, its state as an icon and a word (open,
  maximized or minimized; blue for the window in front), the display
  (monitor) it is on as a number in a bordered box - on Windows the number
  its display settings show, elsewhere 1 for the primary and the others left
  to right; "?" for a window on no display - and, where there
  is more than one virtual desktop, its desktop the same way. *Show* brings
  the window to the front and, when it is off every display, centres it on
  one; *Close* brings it to the front first, so a program that asks "Save
  the changes?" asks where it is seen; *Kill task* ends the program at once
  after asking, for one that does not close. The desktop's own windows (the
  Task Manager itself, its dialogs, the Sticker board) are not listed - the machine — CPU load and temperature, memory, storage, the
  interfaces — on the framework's hardware panel on the second.
- **Settings**: the desktop's settings - the taskbar's edge, the theme, the wallpaper,
  the RAM disc path, the file manager program, the number of virtual
  desktops - are the *Desktop* page of UOS-Settings. They live in the
  desktop's settings file; the desktop checks it once a second and rebuilds
  the bars in place when UOS-Settings changed one of them.
- **Sticker board** (the pinboard button): files and notes sorted into
  categories on a Kanban board that fills the whole space between the bars.
  It shows only when the pinboard button is clicked, and then just below the
  application that was in front - that application stays visible on top of
  it; *✕* in the board's bar (or the button again) hides it. A file's card
  shows a small thumbnail as UltraFiler's tiles do: the picture itself for a
  photo or a drawing, a PDF's first page, a book's cover, a small page of a
  text document's first lines (Word, LibreOffice, Markdown, a spreadsheet),
  and the system's icon for anything else (a folder, an archive). It starts with *Docs*,
  *Work* and *Home*. Drop files from the file manager onto a category and
  they are added to it straight away, without a question; a file already on
  the board moves to the category it was dropped on. A double-click opens a
  file with its default application; cards are dragged from one category to
  another, and *+ Add card* writes a note. *+ Add category* adds a category,
  scrolls to it when the board is too wide to show them all, and lets you
  type its name (double-click a category's title to rename it later). The
  picker in the bar switches the display style: *Sticky notes*, *Poster*,
  *Schematic* or *Dark*. A right click on a card offers *Open*, *Copy* (the
  file onto the clipboard, as the file manager copies it), *Delete* (into the
  trash, after a question), *Remove from the board* and *Move to* another
  category; on a note, *Open*, *Copy* its text and *Delete* it. *Display*,
  in the same menu (anywhere on the board), shows the cards as *Text only*,
  *Thumbnail and text* (the picture with the name centred under it - the
  default) or *Thumbnail and text in a row*, and sets the size of the *Mouse
  hover preview* (*Small*, *Medium*, *Large*): resting the pointer on a
  file's card for two seconds opens a larger picture of it beside the card.
  The *Desktop* page of UOS-Settings has the same two choices. The board,
  its style and its display are kept.
- **Notes** (the note button below it): free sticky notes over the
  wallpaper. *+* in the corner adds one; each note is edited in place,
  dragged by its top bar, cycled through six paper colours and closed with
  ×. Notes come back where they were.

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
UltraDesktop --use-as-desktop   Windows: in place of the Windows desktop from now on
UltraDesktop --restore-desktop  Windows: back to the Windows desktop, and exit
UltraDesktop --session          started by the sign-in: close when switched back
UltraDesktop --version | --help
```

The headless modes print what the module sees, which makes it checkable over
ssh (`--windows` needs a display; `--apps` and `--devices` do not).

On Windows `UltraDesktop.exe` is a GUI program, so no console window flashes
up when the sign-in, the Start menu or UOS-Settings starts it. Beside it,
`UltraDesktop.com` is its console companion. Typed at a prompt without an
extension, `UltraDesktop --windows` runs the companion: it starts the `.exe`,
waits for it and returns its exit code, so the listings print and the prompt
waits as for any console program. Typed as `UltraDesktop.exe --windows`, the
prompt does not wait.

## Settings file

`~/.config/ultraos/desktop.json` (`$XDG_CONFIG_HOME` when set;
`%APPDATA%\ultraos\desktop.json` on Windows;
`~/Library/Application Support/ultraos/desktop.json` on macOS, where a file
earlier versions kept under `~/.config` is moved on the first start):

```json
{
  "taskbarEdge": "left",
  "wallpaper": "",
  "ramDiscPath": "/dev/shm",
  "filerProgram": "UltraFiler",
  "virtualDesktops": 3,
  "clockStyle": "analog-date",
  "theme": "dark",
  "stickerboardVisible": false,
  "stickers": [ { "id": "note1", "text": "…", "x": 60, "y": 60, "width": 220, "height": 160, "color": "#FFF59D" } ],
  "categoryBoard": { "visible": false, "design": "stickynotes", "display": "thumbnail", "hoverPreview": "medium",
                     "x": 320, "y": 60, "width": 780, "height": 460,
                     "board": { "type": "kanban-board", "version": 1, "columns": [ … ], "cards": [ … ] } },
  "pinnedApps": [ "/usr/bin/gimp", "/usr/bin/kate" ],
  "startWithDesktop": [ "ChatCube", "UltraMail" ]
}
```

An empty wallpaper shows ULTRA OS's own picture, `media/images/desktop/ULTRA OS A.avif`
(the framework's `media/images/landscape.jpg` where a package lacks it or
cannot read AVIF). An
empty `ramDiscPath` leaves the RAM disc button out; on Windows, which has no
RAM disc of its own, that is the default.
`categoryBoard.board` is the Kanban element's own JSON document
([UltraCanvasKanbanBoard](../UltraCanvas/UltraCanvasKanbanBoard.md)); without
it the Sticker board starts with Docs, Work and Home. `design` is one of
`stickynotes`, `poster`, `schematic`, `dark`; `display` one of `text`,
`thumbnail` (the default) and `row`; `hoverPreview` one of `small`, `medium`
(the default) and `large`. `visible`, `x`, `y`, `width`
and `height` are kept for older versions and no longer used: the board fills
the work area and starts hidden. `pinnedApps` are the pinned
applications' keys in the order they were pinned: the program's path, or on
Windows the shortcut's path or `store:<AppUserModelID>`.
`theme` is `dark` (the default) or `light`; `clockStyle` one of
`analog-date` (the default), `analog`, `digital-date`, `digital` and
`digital-seconds`.
`startWithDesktop` names the programs started with the desktop - `ChatCube`
and `UltraMail` are the ones it offers; empty (the default), none.

## Programs that start with the desktop

Some programs can start with the desktop, unseen, so they are at work or
ready before they are asked for. Tick them in UOS-Settings > Desktop >
*Start with the desktop*, or on the settings button's right-click menu
(*Start with the desktop*). Three seconds after the desktop starts - so it
shows first - each ticked one that is not running yet is started:

| Program | Started with | What it does in the background |
|---|---|---|
| ChatCube | `--minimized` | messages arrive; its window is on the taskbar |
| UltraMail | `--minimized` | new mail is fetched and counted on the mail icon |
| UltraClipboard | `--background` | no window; opens at once from the clipboard panel |
| UltraNotifications | `--background` | no window; counts the unread on the bell, opens at once |
| Sticker board | - | the desktop's own board: its window and file thumbnails are made, not shown, so the first click shows it at once |

A minimized program that is running already is left alone, since a second
start of these single-copy programs would only bring their window forward; a
second `--background` start reaches the running copy, which takes it as
nothing to do. Ticking one in the menu starts it at once as well.

## How the mail count gets there

UltraMail publishes its unread total through
`UltraCanvasDesktopShell::PublishNotice("UltraMail", count, text)` whenever
its account bar refreshes; the desktop reads that notice every five seconds.
Any application can publish a count the same way, and the desktop will show
the ones it has an icon for.

## Platform

The window list, the virtual desktops, the screenshot, the device markers,
the tray, the lock, the end of the session and the loudspeaker come from the
module's backends: X11 (with logind, the session bus and the sound server)
on Linux and the BSDs, Win32 on Windows. Elsewhere (macOS) the desktop still
runs — application list, launcher, pins, clock, settings, the Sticker board
and the notes work — but the running-apps group stays empty, the Task
Manager says so, and the lock, session and volume calls answer with a
reason.

## On Windows

UltraDesktop runs on Windows too, from the same suite folder as the other
applications, and can take the place of the Windows desktop for one account
— beside Explorer, or instead of it — with the way back always one click
away.

### Trying it

Start `UltraDesktop.exe`. It covers the main screen below every window, its
bars keep their room (maximised windows stop beside them), and Explorer goes
on running underneath with its own taskbar along the bottom, over the foot of
the right bar. *Close UltraDesktop* on the settings button's right-click menu
ends the trial.

### Beside Explorer

Any one of these switches over, for this account only, without administrator
rights:

- **UOS-Settings → Desktop → At sign-in**: *UltraDesktop beside Explorer*;
- the settings button's right-click menu: *Use as the Windows desktop (beside
  Explorer)*;
- `UltraDesktop --use-as-desktop` (what the installer's finish page runs).

From then on the sign-in starts UltraDesktop (an entry *UltraCanvas Desktop*
in Task Manager's startup apps) and Explorer's taskbar hides itself; it slides
in when the pointer touches the bottom edge. Explorer keeps running, so its
notification area, its notifications, its Start menu and the programs that
rely on it work as before. This is the gentle way, and takes effect at once.

### Instead of Explorer

- **UOS-Settings → Desktop → At sign-in**: *UltraDesktop instead of
  Explorer*;
- the settings button's right-click menu: *Use instead of Explorer (from the
  next sign-in)*;
- `UltraDesktop --use-instead-of-explorer`.

From the next sign-in UltraDesktop is the whole desktop and Explorer does not
start. UltraDesktop then also

- shows the programs' **tray icons** (OneDrive, a VPN client, a chat program)
  on the info panel, above its services: a click opens the program, a right
  click its menu, and their balloons appear as notifications;
- offers **Sign out, Restart and Shut down** on the settings button's
  right-click menu, which the Start menu would otherwise;
- opens its **Applications** window when the Win key is pressed and let go
  on its own, where the Start menu would open (Win with another key is that
  combination, not a tap);
- puts the **wallpaper on every other monitor**, which would otherwise stay
  black — a monitor plugged in or taken away is followed within a few
  seconds, and so is one whose wallpaper was closed (Alt+F4).

The sign-in actually starts *UltraCanvasShellGuard*, which starts
UltraDesktop — and keeps the screen from ever staying empty:

- it starts UltraDesktop again when it crashes; after the third crash within a
  minute, or when UltraDesktop cannot start at all, it starts Explorer for
  that session and says why (UltraDesktop is tried again at the next sign-in);
- it starts the programs that start with Windows, as Explorer would — the
  ones switched off in Task Manager stay off;
- it writes what it did to `%LOCALAPPDATA%\UltraCanvas\ShellGuard.log`.

Without Explorer there is nothing for the parts of Windows that Explorer
hosts: the Start menu, the quick settings (Wi-Fi, volume), the notification
centre and Explorer's own keyboard shortcuts (Win+E, Win+R, Win+X). File
Explorer windows still open — UltraDesktop opens *This PC* for the Start
menu's *File Explorer*, which would otherwise start a second desktop.

### Back to the Windows desktop

- UOS-Settings → Desktop → At sign-in: *The Windows desktop*;
- the settings button's right-click menu: *Switch back to the Windows
  desktop*;
- `UltraDesktop --restore-desktop` — also from Task Manager when nothing else
  is at hand: Ctrl+Shift+Esc, *Run new task*, `UltraDesktop --restore-desktop`
  (the installer registers the name, so no path is needed);
- uninstalling the suite does the same.

Each of them removes the start at sign-in, beside or instead, and gives the
taskbar back the state it had before the switch — auto-hide or not, as you had
it. A desktop the sign-in started closes within a second; instead of Explorer,
Explorer then starts in its place at once, and should UltraDesktop not respond
any more, the guard ends it after five seconds and starts Explorer all the
same.

### What works, and what differs from Linux

| | On Windows |
|---|---|
| Running applications | every window Explorer's taskbar would show, with its program's icon; Microsoft Store apps by the program inside their frame |
| Applications window | the Start menu's programs, named as the Start menu names them, and the Microsoft Store apps (Calculator, Photos, Settings …) with their tile icons; instead of Explorer the Win key opens it |
| Virtual desktops | the organiser follows Windows' desktops and switches between them while Explorer runs; adding or removing one is done in Task View, and *Move to* moves only UltraDesktop's own windows |
| Super+V | taken by Windows' own clipboard history while Explorer runs, so the clipboard panel opens from its bar button instead; free instead of Explorer |
| Other monitors | beside Explorer, Explorer's desktop shows there; instead of Explorer, UltraDesktop's wallpaper (the bars stay on the main monitor) |
| RAM disc | no button unless a folder is set (UOS-Settings → Desktop → RAM disc) |
| Info panel | webcam from Windows' privacy indicator, microphone and speaker from the audio sessions, the volume from the default endpoint, battery, keyboard layout, network, USB and Bluetooth; instead of Explorer also the tray icons |
| Lock screen | `LockWorkStation`, from the ⚙ menu; Win+L is Windows' own |
| Notifications | beside Explorer other programs' notifications are drawn by Windows, as before; instead of Explorer the tray icons' balloons are drawn by UltraDesktop |
