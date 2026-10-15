- **`UltraCanvasDesktopShell::ListScreens()`: every monitor.** It returns
  `DesktopScreenInfo` values (position, size, `primary`), with the primary
  first.
  - Windows reads them with `EnumDisplayMonitors`; X11 reads RandR's
    monitors.
  - An X server where nobody chose a primary gives the monitor at the
    origin as the primary.
  - Where the platform tells no monitors apart, the list is the one screen
    `GetScreenSize` reports.
- **`WindowConfig::desktopArea`** places a `WindowType::Desktop` window on any
  monitor, in physical pixels of the virtual screen.
  - Empty means the primary monitor on Windows and the whole screen on X11,
    as before.
  - On Windows a desktop that is the shell covers the other monitors with
    these windows, so they no longer stay black.
- **Microsoft Store applications in `ListApplications` on Windows.**
  - They come from the shell's *Applications* folder, which the Start menu's
    list is made of.
  - Each entry carries the new `UCDesktopEntry::appUserModelId`, its display
    name and a PNG of its tile icon.
  - `LaunchApplication` starts them through `IApplicationActivationManager`,
    as the Start menu does, and refuses files for them with a reason.
  - The backend contract adds `LaunchPackagedApplication`.
- **`UltraCanvasGlobalShortcut::Start("Super")`: a tap of the Win key** on
  Windows.
  - A tap is the key pressed and let go with no other key between, so
    `Super+E` is not one.
  - A low-level keyboard hook on the shortcut's thread sees the key without
    taking it from anyone.
  - X11 refuses it, and any other modifier alone is refused everywhere.
- **On X11, `Stop()` of the monitor and of a global shortcut no longer hangs
  for good.** The wake pipe was blocking, so the drain after the wake waited
  for a byte that never came. A program with a display hung on exit; without
  one the monitor had no connection to stop. Both ends of the pipe no longer
  block.
- Windows internals: `PixmapFromBitmap` turns an `HBITMAP` into a pixmap, and
  takes back premultiplied alpha where the shell hands it over that way.
- **The Windows installer is built in CI.**
  - The Windows rows build the NSIS installer (`package-win.sh --installer`).
  - They install it silently, check the files, *App Paths*, the Start-menu
    entries and the uninstall entry, and uninstall it again, checking
    nothing is left behind.
  - The installer is uploaded as `UltraCanvas-Setup-Windows-<version>-<arch>`.
  - `scripts/build-windows-installer.sh` finds NSIS on `PATH`, in its
    Windows install folder, or at `MAKENSIS`.
- **The signing scripts change the certificate's password**
  (`SignUltraDemo.ps1` / `SignUltraTexter.ps1 -Mode RotatePassword`).
  - The new password comes from `ULTRACANVAS_SIGN_PFX_PASSWORD`, and the old
    one, when the certificate is not in the store, from
    `ULTRACANVAS_SIGN_OLD_PFX_PASSWORD`.
  - The PFX is exported again and checked before it replaces the old one.
  - They refuse the password that was once published in this repository, and
    any password under 12 characters.
