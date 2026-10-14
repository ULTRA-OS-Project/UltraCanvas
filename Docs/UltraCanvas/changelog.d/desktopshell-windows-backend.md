- **`UltraCanvasDesktopShell` on Windows** (`OS/MSWindows/UltraCanvasWindowsDesktopShell.cpp`),
  the first step towards UltraDesktop replacing the Windows desktop. Until
  now Windows had the empty fallback, so UltraDesktop there showed no running
  windows, an empty Apps window, no screenshot and no reserved bars. It works
  with Explorer running beside UltraDesktop and with UltraDesktop as the shell.
  - Windows: `EnumWindows`, filtered the way Explorer's taskbar filters.
    `appClass` is the program's name (for a Microsoft Store app, the program
    inside the frame) and the icon is the executable's. Activate, minimize
    and close are asynchronous, so a hung program cannot hang the taskbar.
  - Virtual desktops: the count, the order and the current desktop come from
    the registry values where Explorer keeps them (Windows documents no call
    for any of them). A window's desktop comes from `IVirtualDesktopManager`.
    Switching desktops sends Explorer's own Ctrl+Win+Left/Right. Changing the
    number of desktops, and moving another program's window to a desktop,
    have no Windows call and answer false.
  - `ReserveScreenEdges`: an appbar for each reserved edge while Explorer's
    taskbar runs, and the work area itself when nothing else keeps it.
  - Screenshot: `BitBlt` of the whole virtual screen.
  - Devices:
    - the webcam from the consent store that Windows' privacy indicator reads;
    - the microphone and speaker from running Core Audio sessions;
    - the battery from `GetSystemPowerStatus`;
    - the keyboard layout from the window being typed into.
  - The monitor uses WinEvent hooks and a registry notification for the
    desktop count. Global shortcuts use `RegisterHotKey`. Explorer reserves
    most Win-key combinations, Win+V among them, so `Start` reports those
    as taken.
  - `ListApplications` lists the Start menu's program shortcuts (the user's
    and the machine's), named as `desktop.ini` names them. `LaunchApplication`
    opens a shortcut the way Explorer does. This adds a new `shortcutPath`
    field to `UCDesktopEntry`. Microsoft Store apps are not listed yet.
- **`UltraCanvasDesktopShell::ReplaceSystemDesktop` /
  `RestoreSystemDesktop` / `IsSystemDesktopReplaced` /
  `CanReplaceSystemDesktop`: a program in place of the system's desktop, and
  the way back.**
  - On Windows a value under the user's Run key starts the program at
    sign-in, and Explorer's taskbar is set to auto-hide. Explorer keeps
    running.
  - Restoring removes the value and gives the taskbar the state recorded
    before the first switch.
  - It applies to one account only and needs no administrator rights.
  - On Linux and elsewhere the session decides which desktop runs:
    `CanReplaceSystemDesktop()` is false there, and restoring is a harmless
    no-op.
  - UltraDesktop 0.3.0 and UOS-Settings 0.4.0 use it.
- **`WindowType::Desktop` on Windows** is now what it is on X11: undecorated,
  the size of the primary monitor, out of the taskbar and Alt+Tab, shown on
  every virtual desktop and kept below every other window. Before, it was a
  normal decorated window.
- `WindowsIcons::WriteSurfacePng`: the "Open with" menu's icon cache and the
  desktop shell now share one PNG writer. It writes through the wide API and
  replaces the file atomically.
- `DesktopShellTest` now runs on the Windows CI rows too
  (`ULTRACANVAS_BUILD_DESKTOPSHELL_TESTS`). It opens a window of its own
  with a Thai title and checks that the window list returns the title. It
  reserves a screen edge and checks that the work area makes room. It also
  minimizes and closes the window, registers a hot key, writes a screenshot
  and lists a Start menu it builds itself.
- **A Windows installer:** `package-win.sh --installer` builds
  `UltraCanvas-Setup-Windows-<version>-<arch>.exe` from the folder the zip is
  made of (`Windows/Installer/UltraCanvasSetup.nsi`,
  `scripts/build-windows-installer.sh`, NSIS).
  - It installs for the current account, with no administrator rights,
    into `%LOCALAPPDATA%\Programs\UltraCanvas`.
  - Every GUI-subsystem program gets a Start-menu entry, read from its PE
    header, so command-line tools get none. The suite gets an
    *Apps & features* entry.
  - The finish page offers UltraDesktop as the Windows desktop, off unless
    ticked.
  - The uninstaller restores the Windows desktop first, then removes exactly
    the files it installed: a folder that still holds a file of the user's
    stays.
  - The commented-out CPack block in `CMakeLists.txt` is gone. The platform
    scripts are what know what a package must hold.
