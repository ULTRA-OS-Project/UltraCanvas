// Tests/DesktopShellTest.cpp
// The platform-neutral half of UltraCanvasDesktopShell: the notices an
// application publishes for the desktop, the program lookup, the application
// list from desktop entries, the screenshot file name - plus the monitor's
// lifetime. Nothing here needs a display: the test points XDG_RUNTIME_DIR
// and XDG_DATA_HOME at a throwaway directory and asserts against what it put
// there, so it passes on every platform and on a headless runner.
// Version: 1.1.0
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShell.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include "UltraCanvasPathUtf8.h"

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const std::string& what) {
    ++g_checks;
    std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

void SetEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

fs::path MakeScratch() {
    const fs::path dir = fs::temp_directory_path() / ("ultracanvas-desktopshell-" + std::to_string(std::rand()));
    fs::remove_all(UltraCanvas::PathFromUtf8(dir));
    fs::create_directories(UltraCanvas::PathFromUtf8(dir));
    return dir;
}

void WriteFile(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

} // namespace

int main() {
    std::srand(12345);
    const fs::path scratch = MakeScratch();
    SetEnv("XDG_RUNTIME_DIR", scratch.string());   // path-string-ok: temp_directory_path is ASCII here
    SetEnv("XDG_DATA_HOME", (scratch / "data").string());   // path-string-ok: same
    SetEnv("XDG_DATA_DIRS", (scratch / "system").string());   // path-string-ok: same

    std::printf("Backend: %s (%s)\n", UltraCanvasDesktopShell::GetBackendName().c_str(),
                UltraCanvasDesktopShell::IsAvailable() ? "available" : "not available here");

    // ===== NOTICES =====
    {
        Check(UltraCanvasDesktopShell::ReadNotices().empty(), "no notices before anything is published");
        Check(UltraCanvasDesktopShell::PublishNotice("UltraMail", 14, "14 unread, 3 today"), "publish a notice");
        const DesktopNotice mail = UltraCanvasDesktopShell::ReadNotice("UltraMail");
        Check(mail.count == 14, "the count comes back");
        Check(mail.text == "14 unread, 3 today", "the text comes back");
        Check(mail.updatedUnixSeconds > 0, "the notice is stamped");
        Check(UltraCanvasDesktopShell::ReadNotice("Nobody").updatedUnixSeconds == 0, "an unpublished notice is unstamped");

        Check(UltraCanvasDesktopShell::PublishNotice("UltraMail", 0), "publish zero replaces the notice");
        Check(UltraCanvasDesktopShell::ReadNotice("UltraMail").count == 0, "zero is kept, not dropped");

        Check(UltraCanvasDesktopShell::PublishNotice("../evil/../name", 1), "an odd name is sanitised");
        // Every file the publish created sits inside the notices directory;
        // nothing landed in a parent (the ".." in the name is a name, not a path).
        const fs::path noticesDir = UltraCanvasDesktopShell::NoticesDirectory();
        bool escaped = false;
        for (const auto& entry : fs::recursive_directory_iterator(scratch)) {
            if (!entry.is_regular_file()) continue;
            const std::string where = entry.path().parent_path().string();   // path-string-ok: ASCII scratch path
            if (where != noticesDir.string()) escaped = true;               // path-string-ok: same
        }
        Check(!escaped, "the notice file stays inside the notices directory");
        Check(UltraCanvasDesktopShell::ReadNotices().size() == 2, "two notices listed");
        Check(UltraCanvasDesktopShell::RemoveNotice("UltraMail"), "remove a notice");
        Check(UltraCanvasDesktopShell::ReadNotice("UltraMail").updatedUnixSeconds == 0, "removed notice is gone");
        Check(!UltraCanvasDesktopShell::PublishNotice("", 1), "an empty application name is refused");

        const std::string dir = UltraCanvasDesktopShell::NoticesDirectory();
        Check(dir.find("ultraos") != std::string::npos && dir.find("notices") != std::string::npos,
              "the notices directory is under ultraos/notices");
    }

    // ===== PROGRAMS =====
    {
        Check(UltraCanvasDesktopShell::FindProgram("").empty(), "an empty program name finds nothing");
        Check(UltraCanvasDesktopShell::FindProgram("no-such-program-ultracanvas").empty(),
              "an unknown program finds nothing");
#if !defined(_WIN32)
        const std::string sh = UltraCanvasDesktopShell::FindProgram("sh");
        Check(!sh.empty() && sh.back() == 'h', "sh is found on the PATH: " + sh);
        Check(UltraCanvasDesktopShell::FindProgram("/bin/sh") == "/bin/sh" || fs::exists("/bin/sh") == false,
              "an absolute path is returned as given");
#endif
        std::string error;
        Check(!UltraCanvasDesktopShell::LaunchProgram("no-such-program-ultracanvas", {}, &error) && !error.empty(),
              "launching an unknown program fails with a reason: " + error);
    }

    // ===== APPLICATIONS =====
    {
        // A system entry, a user entry that shadows it by id, a hidden one,
        // one for the menus only (NoDisplay), and a link that is not an app.
        WriteFile(scratch / "system/applications/org.example.Writer.desktop",
                  "[Desktop Entry]\nType=Application\nName=System Writer\nExec=true %U\nIcon=x-writer\n");
        WriteFile(scratch / "data/applications/org.example.Writer.desktop",
                  "[Desktop Entry]\nType=Application\nName=My Writer\nExec=true %F\nComment=Writes\n");
        WriteFile(scratch / "system/applications/org.example.Hidden.desktop",
                  "[Desktop Entry]\nType=Application\nName=Hidden\nExec=true\nNoDisplay=true\n");
        WriteFile(scratch / "system/applications/org.example.Gone.desktop",
                  "[Desktop Entry]\nType=Application\nName=Gone\nExec=true\nHidden=true\n");
        WriteFile(scratch / "system/applications/org.example.Link.desktop",
                  "[Desktop Entry]\nType=Link\nName=A link\nURL=https://example.com\n");
        WriteFile(scratch / "system/applications/org.example.Missing.desktop",
                  "[Desktop Entry]\nType=Application\nName=Missing\nExec=no-such-program-ultracanvas\n"
                  "TryExec=no-such-program-ultracanvas\n");
        WriteFile(scratch / "system/applications/org.example.Alpha.desktop",
                  "[Desktop Entry]\nType=Application\nName=alpha tool\nExec=true\n");

        const auto apps = UltraCanvasDesktopShell::ListApplications(32);
        Check(apps.size() == 2, "two applications listed (got " + std::to_string(apps.size()) + ")");
        if (apps.size() == 2) {
            Check(apps[0].name == "alpha tool", "sorted by name, case-insensitively");
            Check(apps[1].name == "My Writer", "the user's entry shadows the system's");
            Check(apps[1].comment == "Writes", "the entry's fields come through");
        }
    }

    // ===== THE SCREENSHOT NAME =====
    {
        const std::string path = UltraCanvasDesktopShell::DefaultScreenshotPath();
        Check(path.find("Screenshots") != std::string::npos, "the screenshot goes into a Screenshots folder");
        Check(path.size() > 4 && path.substr(path.size() - 4) == ".png", "and is a PNG");
        Check(path.find(':') == std::string::npos || path.find(':') == 1,
              "no colon in the file name (Windows cannot spell one)");
    }

    // ===== THE MONITOR =====
    {
        UltraCanvasDesktopShellMonitor monitor;
        std::atomic<int> changes{0};
        Check(!monitor.Start(nullptr), "Start refuses a missing callback");
        Check(monitor.Start([&changes]() { ++changes; }), "Start succeeds with or without a display");
        Check(monitor.IsRunning(), "running after Start");
        Check(monitor.Start([&changes]() { ++changes; }), "a second Start is a no-op that succeeds");
        monitor.Stop();
        Check(!monitor.IsRunning(), "stopped after Stop");
        monitor.Stop();
        Check(!monitor.IsRunning(), "Stop twice is safe");
        std::printf("  (native monitor: %s)\n", monitor.IsNative() ? "yes" : "no");
    }

    // ===== NO DISPLAY: EVERY QUERY ANSWERS, NONE CRASHES =====
    {
        const auto windows = UltraCanvasDesktopShell::ListWindows();
        Check(true, "ListWindows answers (" + std::to_string(windows.size()) + " windows)");
        Check(UltraCanvasDesktopShell::GetCurrentVirtualDesktop() >= -1, "GetCurrentVirtualDesktop answers");
        Check(!UltraCanvasDesktopShell::SetCurrentVirtualDesktop(-1), "a negative desktop is refused");
        Check(!UltraCanvasDesktopShell::SetVirtualDesktopCount(0), "zero desktops is refused");
        Check(!UltraCanvasDesktopShell::ActivateWindow(0), "window 0 is refused");
        Check(!UltraCanvasDesktopShell::ReserveScreenEdges(0, 52, 48, 0, 0), "reserving edges for window 0 is refused");
        Check(!UltraCanvasDesktopShell::ReserveScreenEdges(1, -1, 0, 0, 0), "a negative edge is refused");
        const DesktopDeviceActivity activity = UltraCanvasDesktopShell::ReadDeviceActivity();
        Check(activity.batteryPercent >= -1 && activity.batteryPercent <= 100, "battery percentage is in range");
        std::string error;
        Check(!UltraCanvasDesktopShell::CaptureScreen("", &error) && !error.empty(),
              "an empty screenshot path fails with a reason");

        // The in-memory capture either hands back a coherent image (a display
        // is present) or fails with a reason and an empty one; it never
        // returns half of either.
        DesktopScreenImage shot;
        shot.width = 7;   // must be reset on failure
        std::string captureError;
        if (UltraCanvasDesktopShell::CaptureScreenImage(shot, &captureError)) {
            Check(shot.IsValid(), "a captured screen image is valid");
            Check(shot.stride >= shot.width * 4, "the stride covers four bytes a pixel");
            Check(shot.pixels.size() == static_cast<size_t>(shot.height) * static_cast<size_t>(shot.stride),
                  "the buffer is height * stride bytes");
            std::printf("  (screen captured: %dx%d)\n", shot.width, shot.height);
        } else {
            Check(!captureError.empty(), "a failed screen capture says why");
            Check(!shot.IsValid() && shot.width == 0 && shot.pixels.empty(),
                  "a failed screen capture leaves the image empty");
            std::printf("  (no screen capture here: %s)\n", captureError.c_str());
        }
    }

    std::error_code ec;
    fs::remove_all(scratch, ec);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
