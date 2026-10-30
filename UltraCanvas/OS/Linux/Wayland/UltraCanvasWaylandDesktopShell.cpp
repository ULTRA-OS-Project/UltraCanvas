// OS/Linux/Wayland/UltraCanvasWaylandDesktopShell.cpp
// The UltraCanvasDesktopShell backend of a Wayland session, phase 1: what
// the core protocol gives every client. The screens come from the
// compositor's outputs, the devices from the shared Linux probes, the
// keyboard layout from the seat's keymap; the session's end and lock
// (logind), the loudspeaker (the sound server) and the tray host (D-Bus)
// are the shared Linux files, compiled into this build too. The window
// list, the virtual desktops, reserved edges, screenshots and global
// shortcuts need the shell's own protocols (wlr-foreign-toplevel,
// ext-workspace, wlr-layer-shell, the screenshot and GlobalShortcuts
// portals) and are phase 2 - Docs/UltraCanvas/WaylandDesktopShellProposal.md.
// They answer "not on Wayland yet" here, so a program can say so.
// Version: 0.1.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasWaylandApplication.h"
#include "../UltraCanvasLinuxDeviceProbes.h"
#include "../UltraCanvasLinuxDesktopShellInternal.h"

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {
    const char* kNotYet = "Not available on Wayland yet: the desktop shell's own protocols are the next step.";

    UltraCanvasWaylandApplication* App() { return UltraCanvasWaylandApplication::GetInstance(); }
}

// ===== WHAT THE TRAY HOST SHARES =====
namespace X11 {
    bool QueryPointerPosition(int& x, int& y) {
        // There is no screen position on Wayland; the last place the pointer
        // was in one of our windows is the best a tray click can get.
        auto* app = App();
        if (app && app->GetLastPointerPosition(x, y)) return true;
        x = y = 0;
        return false;
    }
}

// ===== BACKEND =====

std::string BackendName() { return "wayland"; }

bool IsAvailable() {
    auto* app = App();
    return app && app->GetDisplay() != nullptr;
}

std::vector<DesktopWindowInfo> ListWindows() { return {}; }
uint64_t GetActiveWindow() { return 0; }
bool ActivateWindow(uint64_t) { return false; }
bool MinimizeWindow(uint64_t) { return false; }
bool CloseWindow(uint64_t) { return false; }
int GetVirtualDesktopCount() { return 1; }
int GetCurrentVirtualDesktop() { return 0; }
bool SetCurrentVirtualDesktop(int) { return false; }
bool SetVirtualDesktopCount(int) { return false; }
bool MoveWindowToVirtualDesktop(uint64_t, int) { return false; }

bool GetScreenSize(int& width, int& height) {
    auto* app = App();
    if (!app) return false;
    app->GetScreenSize(width, height);
    return width > 0 && height > 0;
}

std::vector<DesktopScreenInfo> ListScreens() {
    std::vector<DesktopScreenInfo> screens;
    auto* app = App();
    if (!app) return screens;
    for (const auto& output : app->GetOutputs()) {
        if (output.Width() <= 0 || output.Height() <= 0) continue;
        DesktopScreenInfo screen;
        screen.x = output.x;
        screen.y = output.y;
        screen.width = output.Width();
        screen.height = output.Height();
        screens.push_back(screen);
    }
    // Wayland names no primary; the first the compositor announced is it.
    if (!screens.empty()) screens.front().primary = true;
    return screens;
}

bool ReserveScreenEdges(uint64_t, int, int, int, int) { return false; }

bool CaptureScreenImage(DesktopScreenImage&, std::string& error) {
    error = kNotYet;
    return false;
}

bool CaptureScreen(const std::string&, std::string& error) {
    error = kNotYet;
    return false;
}

// Desktop entries are this platform's application list; the core reads them.
bool ListNativeApplications(int, std::vector<UCDesktopEntry>&) { return false; }
bool LaunchShortcut(const std::string& shortcutPath, std::string& error) {
    return FileAssociations::OpenWithDefaultApplication({shortcutPath}, error);
}
bool LaunchPackagedApplication(const std::string&, std::string& error) {
    error = "Microsoft Store applications run on Windows only.";
    return false;
}

bool CanReplaceSystemDesktop(DesktopReplacement) { return false; }
DesktopReplacement GetSystemDesktopReplacement() { return DesktopReplacement::Off; }
bool ReplaceSystemDesktop(const std::string&, const std::vector<std::string>&, DesktopReplacement,
                          std::string& error) {
    error = "On this system the session starts the desktop; choose UltraDesktop's session at sign-in.";
    return false;
}
bool RestoreSystemDesktop(std::string&) { return true; }
bool IsSystemDesktopRunning() { return true; }
std::string SystemDesktopSwitchUnavailableReason() { return {}; }

void ReadDeviceActivity(DesktopDeviceActivity& activity) {
    LinuxProbes::ProbeAudio(activity);
    LinuxProbes::ProbeCamera(activity);
    LinuxProbes::ProbeBattery(activity);
    if (auto* app = App()) {
        const std::string layout = app->GetKeyboardLayoutName();
        if (!layout.empty()) activity.keyboardLayout = layout;
    }
}

// ===== THE MONITOR, THE SHORTCUTS: PHASE 2 =====
struct MonitorState {};
MonitorState* MonitorOpen() { return nullptr; }
bool MonitorWait(MonitorState*) { return false; }
void MonitorWake(MonitorState*) {}
void MonitorClose(MonitorState*) {}

struct ShortcutState {};
ShortcutState* ShortcutOpen(const std::string&, std::string& error) {
    error = kNotYet;
    return nullptr;
}
bool ShortcutWait(ShortcutState*) { return false; }
void ShortcutWake(ShortcutState*) {}
void ShortcutClose(ShortcutState*) {}

} // namespace DesktopShellBackend
} // namespace UltraCanvas
