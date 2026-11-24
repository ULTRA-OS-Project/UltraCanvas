- **DesktopShell: `GetPointerPosition(x, y)`.** Where the mouse pointer is,
  in the pixels of `ListScreens`, so an application can open a window on the
  monitor the person is working on. X11 (`XQueryPointer`) and Windows
  (`GetCursorPos`) answer; Wayland and macOS answer false.
