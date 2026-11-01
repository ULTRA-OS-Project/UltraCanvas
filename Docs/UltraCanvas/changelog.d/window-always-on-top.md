- **`WindowConfig::alwaysOnTop` works on X11 and macOS.** Only the Windows
  backend honoured it; X11 now sets `_NET_WM_STATE_ABOVE` on the window
  before it is mapped (any type but a notification, which has it, and a
  desktop, which is kept below), and macOS puts the window at the floating
  level, as it now does a notification too.
