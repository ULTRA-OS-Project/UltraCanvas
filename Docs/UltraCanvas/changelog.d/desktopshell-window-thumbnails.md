- **`UltraCanvasDesktopShell::ShowWindowThumbnail` / `HideWindowThumbnail`:
  live window pictures drawn by the compositor.** A taskbar's preview of a
  minimized window had nothing to show: `CaptureWindowImage` asks the window
  to draw itself (`PrintWindow` on Windows), which a minimized window does
  not. On Windows the new call registers a DWM thumbnail
  (`DwmRegisterThumbnail`), as Explorer's taskbar does: the source drawn
  live into a rectangle of one of the program's own windows, its proportions
  kept, and a minimized window as it last looked - also one minimized before
  the program started. Elsewhere it answers 0 and the caller captures.
  `DesktopShellTest` checks a minimized window's thumbnail on Windows.
