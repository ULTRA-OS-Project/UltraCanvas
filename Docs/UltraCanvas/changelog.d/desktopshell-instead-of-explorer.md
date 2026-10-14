- **`UltraCanvasDesktopShell`: a desktop instead of Explorer, not only
  beside it.** `ReplaceSystemDesktop` takes a `DesktopReplacement`:
  - `Beside`, as before: started from the `Run` key, Explorer's taskbar hidden.
  - `Instead`: the account's own shell setting (Winlogon's `Shell` under
    `HKEY_CURRENT_USER`, no administrator rights) names the new
    **UltraCanvasShellGuard** followed by the desktop. It takes effect at the
    next sign-in.
  - Switching to one removes the other. `GetSystemDesktopReplacement()` says
    which is set, and `RestoreSystemDesktop` undoes either. It removes a
    shell setting only when that setting names the guard.
  - `IsSystemDesktopRunning()` says whether Explorer's taskbar runs in this
    session.
  - Linux and the other platforms still answer that the session decides.
- **UltraCanvasShellGuard** (`OS/MSWindows/ShellGuard/`, built beside the
  applications) keeps a desktop that fails from leaving the screen empty:
  - It starts the desktop and starts it again after a crash.
  - It starts Explorer for the session after three failures within a minute,
    when the desktop cannot start at all, or when it exits, unless the session
    is ending. Should the desktop hang, it does so when the shell setting stops
    naming the guard, after five seconds' grace.
  - It does what Explorer would do at sign-in: signals the sign-in screen,
    hides minimized windows, and starts the `Run`, `RunOnce`, policy `Run`
    and Startup-folder programs, except those switched off in Task Manager.
  - It links nothing of the framework and its C++ runtime statically, so it
    starts even when the desktop's DLLs cannot.
  - It logs to `%LOCALAPPDATA%\UltraCanvas\ShellGuard.log`.
- **`UltraCanvasTrayHost`: the notification area for a desktop that is the
  shell** (`OS/MSWindows/UltraCanvasWindowsTrayHost.cpp`).
  - It is the `Shell_TrayWnd` every program's `Shell_NotifyIcon` talks to, on
    a thread of its own, and asks the running programs to add their icons
    again (`TaskbarCreated`).
  - `ListIcons()` gives each icon as a `DesktopTrayIcon`: tooltip, a PNG of
    the icon, the program, and the last balloon with a serial.
  - `Click(id, TrayClick)` hands the program its click in every
    `NIM_SETVERSION` form, double clicks included, and passes on the
    foreground.
  - It answers `Shell_NotifyIconGetRect` and drops icons whose window is gone.
  - It refuses to start while Explorer's taskbar runs, and on X11.
- **`EndSession(SessionEnd)` / `CanEndSession()`**: sign out, restart and
  shut down (Windows: `ExitWindowsEx`), for a desktop with no Start menu
  beside it.
- While no Explorer taskbar runs, `LaunchApplication` opens a shortcut to a
  bare `explorer.exe` (the Start menu's *File Explorer*) on *This PC*.
  Started with nothing to open, Explorer would take over as a second
  desktop. The backend opens shortcuts now (`LaunchShortcut`).
- The Win32 backend no longer takes its own process's `Shell_TrayWnd` for
  Explorer's taskbar: screen edges, the taskbar state and the desktop switch
  look at other processes' windows only.
- `DesktopShellTest` on the Windows CI rows also runs:
  - the guard on itself as a desktop that crashes on request;
  - the notification area on a Windows desktop of its own, through shell32's
    real `Shell_NotifyIcon`;
  - on CI only, the switch to instead-of-Explorer and back.
- The Windows installer (`Windows/Installer/UltraCanvasSetup.nsi` 0.2.0):
  - It registers `UltraDesktop` in App Paths, so *Run new task* finds it.
  - It gives the guard no Start-menu entry.
  - On uninstall it clears a shell setting that names the guard, even when
    UltraDesktop no longer runs, and waits for the guard to hand over to
    Explorer before removing the files.
