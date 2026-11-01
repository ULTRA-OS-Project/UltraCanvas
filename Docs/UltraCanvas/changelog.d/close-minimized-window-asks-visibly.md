- **Closing a minimized window that asks first no longer looks like nothing
  happened.** `UltraCanvasDesktopShell::CloseWindow` shows a minimized window
  before asking it to close, and an UltraCanvas window whose
  `onWindowClosing` keeps it open (to ask "Save the changes?") restores
  itself if it was minimized - so ArtCreator's question about an unsaved
  drawing is seen when UltraDesktop's Task Manager or taskbar closes it.
