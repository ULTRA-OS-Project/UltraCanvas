// OS/Linux/UltraCanvasLinuxDesktopShellInternal.h
// INTERNAL: what the halves of the X11 desktop-shell backend share -
// UltraCanvasLinuxDesktopShell.cpp (windows, desktops, the screen, the
// devices) and UltraCanvasLinuxTrayHost.cpp (the notification area over
// D-Bus, which has no X connection of its own). Not a public header;
// applications include UltraCanvasDesktopShell.h.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

namespace UltraCanvas {
namespace DesktopShellBackend {
namespace X11 {

    // Where the mouse pointer is on the screen, in root-window pixels, from
    // the query connection the window list uses (so a click on a tray icon
    // opens no display of its own). False, with 0,0, when there is no
    // display or the server cannot say.
    bool QueryPointerPosition(int& x, int& y);

} // namespace X11
} // namespace DesktopShellBackend
} // namespace UltraCanvas
