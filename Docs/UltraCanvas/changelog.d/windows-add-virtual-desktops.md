- **`UltraCanvasDesktopShell::SetVirtualDesktopCount` adds desktops on
  Windows.** It answered false; it now sends Explorer's Ctrl+Win+D once per
  missing desktop, waits for Explorer to record them, and switches back to
  the desktop that was current. A smaller count still answers false -
  removing a desktop moves its windows and stays the user's, in Task View.
