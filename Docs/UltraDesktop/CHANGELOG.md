#### 2026-10-01 *0.1.1*
- **The ULTRA OS settings button starts UOS-Settings.** The system's
  settings now have an application of their own, UOS-Settings, and the
  taskbar's settings button launches it. The desktop's own page - taskbar
  edge, wallpaper, RAM disc, file manager, virtual desktops - is one
  right-click away on the same button (*Desktop settings...*), and is what
  the button opens when UOS-Settings is not installed.

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
