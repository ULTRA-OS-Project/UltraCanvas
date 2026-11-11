// include/UltraCanvasDesktopShellBackend.h
// Internal contract between UltraCanvasDesktopShell and its per-platform
// backend. Applications include UltraCanvasDesktopShell.h instead; this
// header exists so OS/<Platform>/UltraCanvas*DesktopShell.cpp and the shared
// core agree on one set of entry points.
// Version: 1.10.0 - CanOpenDisplaySettings, OpenDisplaySettings
// Version: 1.9.0 - CaptureWindowImage
// Version: 1.8.0 - macOS: ListNativeApplications and LaunchShortcut native
//                  (ULTRACANVAS_DESKTOPSHELL_NATIVE_APPLICATIONS)
// Version: 1.7.0 - GetWindowBounds, MoveWindowTo, KillProcess
// Version: 1.6.0 - LockSession; the loudspeaker's volume; TrayHostMenuActivate
// Version: 1.5.0 - LaunchPackagedApplication (Microsoft Store applications),
//                  ListScreens
// Version: 1.4.0 - DesktopReplacement::Instead, the tray host, EndSession
// Version: 1.3.0 - the Windows backend; ListNativeApplications;
//                  in place of the system's desktop
// Last Modified: 2026-11-01
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
// macOS has no window-list backend, but its applications are listed and
// started natively (OS/MacOS/UltraCanvasMacOSApplications.mm):
// ListNativeApplications and LaunchShortcut come from there, every other
// entry point from the fallback.
#if defined(__APPLE__) && !defined(ULTRACANVAS_DESKTOPSHELL_NATIVE)
    #include <TargetConditionals.h>
    #if TARGET_OS_OSX
        #define ULTRACANVAS_DESKTOPSHELL_NATIVE_APPLICATIONS 1
    #endif
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
    // The window's outer rectangle in screen pixels (the same pixels as
    // ListScreens); a minimized window's place when restored where the
    // platform keeps it. False when the window is gone.
    bool GetWindowBounds(uint64_t id, int& x, int& y, int& width, int& height);
    // Move the window's top-left corner there, its size kept.
    bool MoveWindowTo(uint64_t id, int x, int y);
    // End the process at once (Linux: SIGTERM, then SIGKILL after two
    // seconds; Windows: TerminateProcess). Refuses this process itself.
    bool KillProcess(int processId, std::string& error);
    int  GetVirtualDesktopCount();
    int  GetCurrentVirtualDesktop();
    bool SetCurrentVirtualDesktop(int index);
    bool SetVirtualDesktopCount(int count);
    bool MoveWindowToVirtualDesktop(uint64_t id, int index);

    // The screen.
    bool GetScreenSize(int& width, int& height);
    // Every monitor, any order; empty when the platform tells none apart.
    std::vector<DesktopScreenInfo> ListScreens();
    bool ReserveScreenEdges(uint64_t id, int left, int right, int top, int bottom);
    bool CaptureScreen(const std::string& pngPath, std::string& error);
    bool CaptureScreenImage(DesktopScreenImage& out, std::string& error);
    // One rectangle of the screen, in the pixels of ListScreens, clipped to
    // the screen by the backend; false (with the reason) when nothing of it
    // is on the screen. `out.x` / `out.y` say what was captured.
    bool CaptureScreenArea(int x, int y, int width, int height, DesktopScreenImage& out, std::string& error);
    // One window at its own size (the core scales it down); false (with the
    // reason) for a minimized or vanished window.
    bool CaptureWindowImage(uint64_t id, DesktopScreenImage& out, std::string& error);

    // The installed applications, where the platform keeps them somewhere
    // other than freedesktop desktop entries: the Start menu's shortcuts on
    // Windows, the application bundles on macOS. False where desktop entries are the answer, and the core then
    // reads them itself; true with `out` filled (unsorted) otherwise.
    bool ListNativeApplications(int iconSize, std::vector<UCDesktopEntry>& out);
    // Open one of those applications' shortcuts the way the platform's own
    // desktop would (Windows: the .lnk through the shell; macOS: the bundle
    // through NSWorkspace). Only called for an entry with shortcutPath set.
    bool LaunchShortcut(const std::string& shortcutPath, std::string& error);
    // Start a Microsoft Store application by its AppUserModelID (an entry
    // with appUserModelId set). False with the reason where there are none.
    bool LaunchPackagedApplication(const std::string& appUserModelId, std::string& error);

    // In place of the system's desktop (UltraCanvasDesktopShell.h). The core
    // has checked that `program` is an existing absolute path, and that `how`
    // is not Off.
    bool CanReplaceSystemDesktop(DesktopReplacement how);
    DesktopReplacement GetSystemDesktopReplacement();
    bool ReplaceSystemDesktop(const std::string& program, const std::vector<std::string>& arguments,
                              DesktopReplacement how, std::string& error);
    bool RestoreSystemDesktop(std::string& error);
    bool IsSystemDesktopRunning();
    // Why a platform that has the switch cannot use it from this copy of the
    // program, in a sentence for the user; "" when it can, or when the
    // platform has no such switch at all.
    std::string SystemDesktopSwitchUnavailableReason();

    // The end of the session, and the lock.
    bool CanEndSession();
    bool EndSession(SessionEnd how, std::string& error);
    bool CanLockSession();
    bool LockSession(std::string& error);

    // The system's own display settings, started and not waited for.
    bool CanOpenDisplaySettings();
    bool OpenDisplaySettings(std::string& error);

    // The default output's volume: `available` false with `error` set when
    // no sound server answers.
    DesktopAudioOutput ReadAudioOutput();
    bool SetAudioOutputVolume(int percent, std::string& error);
    bool SetAudioOutputMuted(bool muted, std::string& error);

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

    // The notification area. `TrayHostOpen` checks there is one to host
    // (nullptr + error otherwise); the core's thread then calls
    // `TrayHostAttach` once - the window the programs talk to is that
    // thread's - and `TrayHostWait` until it returns false. Wait answers
    // true when the icons changed, false when `TrayHostWake` was called (it
    // has taken the window down by then). Icons and clicks from any thread.
    struct TrayHostState;
    TrayHostState* TrayHostOpen(std::string& error);
    bool TrayHostAttach(TrayHostState* state, std::string& error);
    bool TrayHostWait(TrayHostState* state);
    void TrayHostWake(TrayHostState* state);
    void TrayHostClose(TrayHostState* state);
    std::vector<DesktopTrayIcon> TrayHostIcons(TrayHostState* state);
    bool TrayHostClick(TrayHostState* state, const std::string& id, TrayClick button);
    // The choice from DesktopTrayIcon::menu; false where the icons carry no
    // menu (Windows) or the entry is gone.
    bool TrayHostMenuActivate(TrayHostState* state, const std::string& id, int32_t itemId);

} // namespace DesktopShellBackend
} // namespace UltraCanvas

#endif // ULTRACANVASDESKTOPSHELLBACKEND_H
