// include/UltraCanvasDesktopShellBackend.h
// Internal contract between UltraCanvasDesktopShell and its per-platform
// backend. Applications include UltraCanvasDesktopShell.h instead; this
// header exists so OS/<Platform>/UltraCanvas*DesktopShell.cpp and the shared
// core agree on one set of entry points.
// Version: 1.3.0 - the Windows backend; ListNativeApplications;
//                  in place of the system's desktop
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASDESKTOPSHELLBACKEND_H
#define ULTRACANVASDESKTOPSHELLBACKEND_H

#include "UltraCanvasDesktopShell.h"

// A platform has a native backend when one of the OS/<Platform> files is
// compiled into the build. Everything else links the fallback in
// core/UltraCanvasDesktopShell.cpp, which answers empty and says so.
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__FreeBSD__) || \
    defined(__OpenBSD__) || defined(__NetBSD__) || defined(_WIN32)
    #define ULTRACANVAS_DESKTOPSHELL_NATIVE 1
#endif

namespace UltraCanvas {
namespace DesktopShellBackend {

    std::string BackendName();
    bool        IsAvailable();

    // Windows and virtual desktops. Every call is best-effort and returns
    // false, empty or -1 when the window system cannot answer.
    std::vector<DesktopWindowInfo> ListWindows();
    uint64_t GetActiveWindow();
    bool ActivateWindow(uint64_t id);
    bool MinimizeWindow(uint64_t id);
    bool CloseWindow(uint64_t id);
    int  GetVirtualDesktopCount();
    int  GetCurrentVirtualDesktop();
    bool SetCurrentVirtualDesktop(int index);
    bool SetVirtualDesktopCount(int count);
    bool MoveWindowToVirtualDesktop(uint64_t id, int index);

    // The screen.
    bool GetScreenSize(int& width, int& height);
    bool ReserveScreenEdges(uint64_t id, int left, int right, int top, int bottom);
    bool CaptureScreen(const std::string& pngPath, std::string& error);
    bool CaptureScreenImage(DesktopScreenImage& out, std::string& error);

    // The installed applications, where the platform keeps them somewhere
    // other than freedesktop desktop entries: the Start menu's shortcuts on
    // Windows. False where desktop entries are the answer, and the core then
    // reads them itself; true with `out` filled (unsorted) otherwise.
    bool ListNativeApplications(int iconSize, std::vector<UCDesktopEntry>& out);

    // In place of the system's desktop (UltraCanvasDesktopShell.h). The core
    // has checked that `program` is an existing absolute path.
    bool CanReplaceSystemDesktop();
    bool IsSystemDesktopReplaced();
    bool ReplaceSystemDesktop(const std::string& program, const std::vector<std::string>& arguments,
                              std::string& error);
    bool RestoreSystemDesktop(std::string& error);

    // The parts of the device activity only the platform can read: audio
    // and video capture in use, playback running, the battery, the keyboard
    // layout. The core fills the network, USB and Bluetooth fields from
    // UltraCanvasHardwareInfo before calling this.
    void ReadDeviceActivity(DesktopDeviceActivity& activity);

    // The monitor: `Wait` blocks until something changed or `Wake` was
    // called, returning true for a change; the core runs it on its thread.
    struct MonitorState;
    MonitorState* MonitorOpen();               // nullptr when the platform cannot notify
    bool MonitorWait(MonitorState* state);     // false = woken to stop, or the connection is gone
    void MonitorWake(MonitorState* state);
    void MonitorClose(MonitorState* state);

    // A global shortcut: `ShortcutWait` blocks until the combination is
    // pressed (true) or `ShortcutWake` was called (false).
    struct ShortcutState;
    ShortcutState* ShortcutOpen(const std::string& accelerator, std::string& error);   // nullptr + error
    bool ShortcutWait(ShortcutState* state);
    void ShortcutWake(ShortcutState* state);
    void ShortcutClose(ShortcutState* state);

} // namespace DesktopShellBackend
} // namespace UltraCanvas

#endif // ULTRACANVASDESKTOPSHELLBACKEND_H
