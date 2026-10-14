// OS/MSWindows/UltraCanvasWindowsDesktopShellInternal.h
// INTERNAL: what the two halves of the Win32 desktop-shell backend share -
// UltraCanvasWindowsDesktopShell.cpp (windows, desktops, the switch at
// sign-in) and UltraCanvasWindowsTrayHost.cpp (the notification area). Not a
// public header; applications include UltraCanvasDesktopShell.h.
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasImage.h"   // UCPixmap

#include <windows.h>

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace DesktopShellBackend {
namespace Win32 {

    // The PNG for `key` in the desktop-icons cache (%LOCALAPPDATA%\UltraCanvas),
    // made by `make` when it is not there yet; "" when there is no cache or
    // nothing to draw. The key decides the file's name: the same key is the
    // same file, so a key must change whenever the picture does.
    std::string CachedIconPng(const std::string& key, const std::function<std::shared_ptr<UCPixmap>()>& make);

    // The program a process runs, by name - "OneDrive", "DesktopShellTest" -
    // the closest Windows has to a window's WM_CLASS; "" when it cannot be read.
    std::string ProgramNameOfProcess(DWORD pid);

    // Whether a Shell_TrayWnd of another process runs: Explorer's taskbar,
    // or another shell's notification area.
    bool ForeignTrayWindowRunning();

} // namespace Win32
} // namespace DesktopShellBackend
} // namespace UltraCanvas
