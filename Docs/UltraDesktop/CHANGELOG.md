#### 2026-10-06 *0.1.4*
- **A clipboard history that lasts.** Every copy - texts, links, colours,
  code, formatted text, images and files - is recorded into a history on
  disk, encrypted, instead of the last hundred texts of this session in
  memory. It survives logging out, and copies of images and files are kept
  too (framework changelog: `clipboard-history`).
- **The clipboard quick panel replaces the clipboard menu.** The clipboard
  button, or `Super+V` from any window, opens a panel beside the bar: a
  search field, the pinned entries and the ten latest - typing searches the
  whole history - each with a thumbnail and Copy, Edit and Delete. The
  keyboard does everything (Up / Down, Enter copies, F2 edits, Delete
  deletes with Undo, Escape closes). The *Recording* switch pauses the
  history, and *Open UltraClipboard* shows all of it in the new UltraClipboard
  application, which also edits an entry (framework changelog:
  `global-shortcut`).
- Right-click the clipboard button for *Pause recording*, *Open
  UltraClipboard* and *Clear history*; while recording is paused the button
  is crossed out.
- **A copy outlives the program it came from.** On X11 the clipboard goes
  with the program that owns it; when that program quits, the desktop puts
  its last copy back, so it can still be pasted.
- **The clipboard menu lists the newest copies, not the oldest.** The menu
  walked the history from the back, believing the newest entry was last; the
  history keeps the newest first, so once more than fifteen things had been
  copied the menu showed the fifteen oldest, oldest first, and nothing copied
  since could be chosen from it.
- **A password copied from UltraPassword is no longer kept.** The desktop's
  clipboard monitor recorded every text it saw, so a password stayed in the
  clipboard history, and its first characters in the clipboard menu, after
  UltraPassword had cleared the clipboard. Copies marked secret by their
  source are not recorded now - UltraPassword's, and those of KeePassXC and
  other password managers that mark theirs (framework changelog:
  `clipboard-secret-hint`).

#### 2026-10-05 *0.1.3*
- **The desktop keeps its settings in a Windows profile named in any
  script.** `desktop.json` lives under `%APPDATA%\ultraos`, and `APPDATA`
  was read through the narrow `getenv`, which answers in the ANSI code page;
  for a user name outside it the settings were neither saved nor read. It is
  read with the framework's `GetEnvUtf8` now (framework changelog:
  `env-narrow`).

#### 2026-10-01 *0.1.2*
- **The desktop's settings moved to UOS-Settings.** The system's settings now
  have an application of their own, UOS-Settings, and the taskbar's *ULTRA OS
  settings* button starts it. Its *Desktop* page holds what the desktop's own
  settings window held - the taskbar's edge, the wallpaper, the RAM disc, the
  file manager, the number of virtual desktops - and that window is gone.
  - UOS-Settings writes the desktop's settings file; the desktop checks the
    file once a second and, when one of those settings changed, takes it over
    and rebuilds its bars, as Apply used to. The sticky notes in the same file
    stay the desktop's: what is in memory wins and is written back.
  - When UOS-Settings cannot be started, the button says so instead of
    doing nothing.

#### 2026-10-01 *0.1.1*
- **The info panel keeps its last items on a short bar.** With five or more
  virtual desktops on a 900 px screen the organiser left the panel less room
  than its fourteen icons need, and the last of them (loudspeaker, battery,
  Task Manager) were cut off with nothing to say so. The panel is a scrolling
  toolbar, and the toolbar now marks the edge its items continue past with a
  chevron that scrolls a page when clicked (`UltraCanvasToolbar` 1.6.0); the
  wheel over the panel scrolls it as before.
- The desktop runs straight from the build tree: configuring links
  `build/share/media` to the repository's `media/`, where the bar icons and
  the wallpaper are found.

#### 2026-09-29 *0.1.0*
- **First release.** UltraDesktop (`Apps/UltraDesktop`) is the ULTRA OS
  desktop: one screen-sized window at the bottom of the stack (the new
  `WindowType::Desktop`) holding the wallpaper and two bars, built from
  UltraCanvas elements on the new `UltraCanvasDesktopShell` module, which is
  where every window, desktop, device and launcher question is answered.
  - **Taskbar** on the left by default, or on the top or bottom edge (the
    settings, or `--edge` for one run): ULTRA OS settings and the app
    starter at the top, the running applications in the middle — one toggle
    per open window with the application's own icon, the active one
    highlighted, a click activates or minimizes, a right-click offers
    Activate, Minimize, Move to desktop and Close, drag to reorder, and the
    group scrolls with the wheel when it is full — and the pinned RAM disc and
    UltraFiler at the bottom. The groups are joined by the S-curve
    `UltraCanvasWaveSeparator`. The bars' edges are reserved with the window
    manager (`UltraCanvasDesktopShell::ReserveScreenEdges`), so a maximised
    application window stops short of them instead of covering them.
  - **Right bar**: the desktop organiser (the virtual desktops as the window
    manager has them — its count is read at start-up and followed when it
    changes; the settings' count where there is none to ask, and choosing a
    count in the settings asks the manager for that many — the Stickerboard
    toggle, the clipboard history menu
    and the screenshot button) above the info panel with the devices and
    services. A red marker means a device is on (webcam, microphone,
    Bluetooth; a link that is down), a yellow one activity (download, upload,
    loudspeaker, VPN), and pill badges carry counts (Email 14, USB 3, the
    battery's percentage, the keyboard layout). Every marker is an
    `UltraCanvasToolbar` item badge fed by
    `UltraCanvasDesktopShell::ReadDeviceActivity` every two seconds, and the
    mail count is the notice UltraMail publishes.
  - **Stickerboard**: sticky notes over the wallpaper — an
    `UltraCanvasTextArea` on a coloured card, dragged by its bar, six papers
    to cycle, kept in the settings so they come back where they were.
  - **Apps window**: every installed application from its desktop entry,
    as icon tiles with a filter box; **Task Manager**: the open windows
    with Activate and Close, and the machine on the framework's hardware
    panel; **Settings**: the taskbar's edge, the wallpaper, the RAM disc, the
    file manager, the number of virtual desktops.
  - Headless `--windows`, `--apps`, `--devices` and `--screenshot [file]`
    print what the module sees and exit.
