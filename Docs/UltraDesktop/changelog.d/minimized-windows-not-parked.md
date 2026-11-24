- **Instead of Explorer, minimized windows no longer appear as title bars
  along the bottom of the screen.** UltraDesktop now keeps them out of sight
  itself, every two seconds (`UltraCanvasDesktopShell::KeepMinimizedWindowsHidden`),
  as Explorer does: they are on the taskbar's buttons only, also after
  Explorer was started and ended during the session, which switched Windows
  back to parking them.
