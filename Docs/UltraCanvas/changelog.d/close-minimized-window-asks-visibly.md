- **Closing a window that asks first no longer looks like nothing
  happened.** `UltraCanvasDesktopShell::CloseWindow` brings the window to the
  front - and onto a screen - before asking it to close, and an UltraCanvas
  window whose `onWindowClosing` keeps it open (to ask "Save the changes?")
  restores itself if it was minimized - so ArtCreator's question about an
  unsaved drawing is seen when UltraDesktop's Task Manager or taskbar closes
  it.
- **Windows off every screen, and ending a program.**
  `UltraCanvasDesktopShell::GetWindowBounds` and `MoveWindow` read and move a
  window; `BringWindowOnScreen` centres a window whose title bar is on no
  screen (one left on a monitor that was unplugged) on the screen it is
  mostly over; `KillWindowProcess` ends a window's program at once, as a task
  manager's "End task" (Linux `SIGTERM`, then `SIGKILL`; Windows
  `TerminateProcess`), refusing the desktop itself and programs it may not
  end.
