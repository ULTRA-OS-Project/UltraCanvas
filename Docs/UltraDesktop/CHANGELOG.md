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
    `UltraCanvasWaveSeparator`.
  - **Right bar**: the desktop organiser (virtual desktops 1–3, or as many
    as the settings say, the Stickerboard toggle, the clipboard history menu
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
