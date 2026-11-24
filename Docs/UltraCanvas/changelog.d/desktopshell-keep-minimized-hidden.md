- **`UltraCanvasDesktopShell::KeepMinimizedWindowsHidden`: a shell without
  Explorer keeps minimized windows out of sight.** Without Explorer's
  taskbar, Windows parks each minimized window as a small title bar along
  the bottom of the screen unless the session's `ARW_HIDE` is on. The shell
  guard set it once at sign-in, but an Explorer started and ended later in
  the session puts it back as it ends, and windows parked meanwhile stayed
  where they were. The new call switches it on again whenever it is off and
  moves a parked minimized window off the screen, still minimized; false
  beside Explorer's taskbar and on other systems. `DesktopShellTest` checks
  both sides on Windows.
