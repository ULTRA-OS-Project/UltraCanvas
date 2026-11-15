- **An application no longer crashes on its way out.** A window's destructor
  closes its popups, and closing a popup hides its window - which on X11 asks
  the application for the display. But `windows` is a member of
  `UltraCanvasApplicationBase`, so the windows were destroyed by the *base*
  destructor, which runs after the platform half of the application has
  already gone and cleared its instance pointer: `Hide()` then read the
  display through a null application and the process died with SIGSEGV. Every
  platform application now calls the new `DestroyWindows()` at the top of its
  own destructor, so a window is always destroyed while the application it
  reaches back for is still whole, and `UltraCanvasLinuxWindow::Hide()`
  survives having no application left. The first display-backed CI run of
  UltraMail's message-pane suite caught this: 60 of 60 assertions passed and
  the binary still exited 139.
