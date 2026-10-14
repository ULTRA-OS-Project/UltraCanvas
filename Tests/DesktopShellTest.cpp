// Tests/DesktopShellTest.cpp
// The platform-neutral half of UltraCanvasDesktopShell: the notices an
// application publishes for the desktop, the program lookup, the application
// list from desktop entries, the screenshot file name - plus the monitor's
// lifetime. Nothing here needs a display: the test points XDG_RUNTIME_DIR
// and XDG_DATA_HOME at a throwaway directory and asserts against what it put
// there, so it passes on every platform and on a headless runner.
//
// On Windows it also drives the Win32 backend against the real desktop: the
// application list from a Start menu it builds (APPDATA and ProgramData
// pointed at the throwaway directory), and a window of its own - listed with
// its UTF-8 title and its program's name, reported by the monitor,
// minimized, given a reserved screen edge that the work area has to make
// room for, and closed - plus a global shortcut and a screenshot file. On a
// CI runner (CI=true) it also switches the desktop at sign-in to itself and
// back, and checks the Run value and the taskbar's state come back as they
// were; anywhere else that is skipped, so a developer's own taskbar and
// sign-in are never touched.
// Version: 1.3.0 - ReplaceSystemDesktop / RestoreSystemDesktop
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShell.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include "UltraCanvasPathUtf8.h"

#if defined(_WIN32)
#include "UltraCanvasShellLink.h"
#include "ShellLinkTestSupport.h"
#include <windows.h>
#include <shellapi.h>   // SHAppBarMessage
#endif

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

#if defined(_WIN32)
void WriteBytes(const fs::path& file, const std::vector<uint8_t>& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A Start-menu shortcut to `target`, with `description` as its comment.
void WriteShortcut(const fs::path& file, const std::string& target, const std::string& description = "") {
    using namespace ShellLinkTestSupport;
    LinkSpec spec;
    spec.flags = IsUnicode | HasLinkInfo | (description.empty() ? 0u : static_cast<uint32_t>(HasName));
    spec.localBasePath = target;
    spec.description = description;
    WriteBytes(file, BuildShellLink(spec));
}

uint64_t IdOf(HWND hwnd) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd));
}

// This thread's messages, until `done` holds or `ms` milliseconds pass.
template <typename Done>
bool PumpUntil(Done done, int ms) {
    const ULONGLONG until = GetTickCount64() + static_cast<ULONGLONG>(ms);
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done()) return true;
        if (GetTickCount64() >= until) return false;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
}

HWND OpenTestWindow(const std::string& title) {
    static const bool registered = []() {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;   // WM_CLOSE -> DestroyWindow
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));   // IDC_ARROW
        wc.lpszClassName = L"UltraCanvasDesktopShellTestWindow";
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered) return nullptr;
    const std::wstring wideTitle = PathUtf8Detail::Utf8ToUtf16<std::wstring>(title);
    HWND hwnd = CreateWindowExW(0, L"UltraCanvasDesktopShellTestWindow", wideTitle.c_str(), WS_OVERLAPPEDWINDOW,
                                120, 120, 360, 220, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
    }
    return hwnd;
}

const DesktopWindowInfo* FindListed(const std::vector<DesktopWindowInfo>& windows, HWND hwnd) {
    for (const DesktopWindowInfo& w : windows) {
        if (w.id == IdOf(hwnd)) return &w;
    }
    return nullptr;
}

RECT WorkArea() {
    RECT area = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    return area;
}
#endif

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
#if defined(_WIN32)
    // Windows lists the Start menu's shortcuts, not desktop entries: the
    // user's and the machine's, the user's shadowing the machine's of the
    // same name, named as the folder's desktop.ini says, programs only.
    {
        const fs::path user = scratch / "AppData";
        const fs::path machine = scratch / "ProgramData";
        SetEnvironmentVariableW(L"APPDATA", user.c_str());
        SetEnvironmentVariableW(L"ProgramData", machine.c_str());
        const fs::path userPrograms = user / "Microsoft" / "Windows" / "Start Menu" / "Programs";
        const fs::path machinePrograms = machine / "Microsoft" / "Windows" / "Start Menu" / "Programs";

        // Programs that exist: an entry's program is resolved against this
        // machine and stays empty for a file that is not there.
        const fs::path writer = (scratch / "Tools" / "Writer" / "writer.exe").make_preferred();
        const fs::path tool = (scratch / "Tools" / "tool.exe").make_preferred();
        WriteFile(writer, "MZ");
        WriteFile(tool, "MZ");
        WriteShortcut(userPrograms / "Writer.lnk", PathToUtf8(writer), "Writes");
        WriteShortcut(machinePrograms / "Writer.lnk", "C:\\Program Files\\Writer\\writer-old.exe");
        WriteShortcut(machinePrograms / "Accessories" / "Tool.lnk", PathToUtf8(tool));
        WriteFile(machinePrograms / "Accessories" / "desktop.ini",
                  "[LocalizedFileNames]\r\nTool.lnk=Localized Tool\r\n");
        WriteShortcut(machinePrograms / "Writer" / "Uninstall Writer.lnk",
                      "C:\\Program Files\\Writer\\unins000.exe");
        WriteShortcut(machinePrograms / "Writer" / "Read me.lnk", "C:\\Program Files\\Writer\\readme.txt");
        WriteFile(machinePrograms / "Writer" / "Website.url", "[InternetShortcut]\r\nURL=https://example.com\r\n");

        const auto apps = UltraCanvasDesktopShell::ListApplications(32);
        Check(apps.size() == 2, "two applications in the Start menu (got " + std::to_string(apps.size()) + ")");
        if (apps.size() == 2) {
            Check(apps[0].name == "Localized Tool", "desktop.ini names a shortcut, and the list is sorted: " +
                                                        apps[0].name);
            Check(apps[1].name == "Writer", "a shortcut is named after its file");
            Check(apps[1].comment == "Writes", "the user's shortcut shadows the machine's");
            std::error_code same;
            Check(!apps[1].program.empty() && fs::equivalent(PathFromUtf8(apps[1].program), writer, same),
                  "the program is the shortcut's target: " + apps[1].program);
            Check(apps[1].startupWMClass == "writer", "its windows are the program's: " + apps[1].startupWMClass);
            Check(IsShellLinkPath(apps[1].shortcutPath), "launching opens the shortcut: " + apps[1].shortcutPath);
            DesktopWindowInfo window;
            window.appClass = "writer";
            window.appName = "writer";
            const UCDesktopEntry* entry = UltraCanvasDesktopShell::MatchApplication(window, apps);
            Check(entry && entry->name == "Writer", "a window of writer.exe belongs to the Writer shortcut");
        }
    }
#else
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

#endif

    // ===== WHICH APPLICATION A WINDOW BELONGS TO =====
    {
        std::vector<UCDesktopEntry> apps(4);
        apps[0].name = "UltraPaint";
        apps[0].startupWMClass = "UltraPaint";
        apps[0].program = "/usr/bin/UltraPaint";
        apps[1].name = "GNU Image Manipulation Program";
        apps[1].program = "/usr/bin/gimp-2.10";
        apps[1].iconName = "gimp";
        apps[2].name = "Text Editor";
        apps[2].iconName = "org.gnome.TextEditor";
        apps[2].program = "/usr/bin/gnome-text-editor";
        apps[3].name = "Kate";
        apps[3].program = "/usr/bin/kate";
        apps[3].startupWMClass = "kate-editor";
        auto match = [&apps](const std::string& appClass, const std::string& appName) {
            DesktopWindowInfo window;
            window.appClass = appClass;
            window.appName = appName;
            const UCDesktopEntry* entry = UltraCanvasDesktopShell::MatchApplication(window, apps);
            return entry ? entry->name : std::string("(none)");
        };
        Check(match("UltraPaint", "ultrapaint") == "UltraPaint", "StartupWMClass names a window's application");
        Check(match("Gimp-2.10", "gimp-2.10") == "GNU Image Manipulation Program",
              "the program's name does, case aside (got " + match("Gimp-2.10", "gimp-2.10") + ")");
        Check(match("org.gnome.TextEditor", "gnome-text-editor") == "Text Editor",
              "so does its icon name or program");
        Check(match("kate-editor", "kate") == "Kate", "StartupWMClass wins over a later guess");
        Check(match("xterm", "xterm") == "(none)", "a window of no listed application has none");
        Check(match("", "") == "(none)", "nor has a window with no class");
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

    // ===== IN PLACE OF THE SYSTEM'S DESKTOP =====
    {
        std::string error;
        Check(UltraCanvasDesktopShell::RestoreSystemDesktop(&error), "restoring when nothing was replaced is fine " +
                                                                         error);
        Check(!UltraCanvasDesktopShell::ReplaceSystemDesktop("relative/UltraDesktop", {}, &error) && !error.empty(),
              "a program that is not an absolute path is refused: " + error);
#if !defined(_WIN32)
        Check(!UltraCanvasDesktopShell::CanReplaceSystemDesktop(), "here the session decides which desktop runs");
        Check(!UltraCanvasDesktopShell::IsSystemDesktopReplaced(), "so nothing is replaced");
#endif
    }

#if defined(_WIN32)
    // ===== WINDOWS: THE WIN32 BACKEND ON THE REAL DESKTOP =====
    {
        Check(UltraCanvasDesktopShell::GetBackendName() == "win32", "the backend is win32");
        const int desktops = UltraCanvasDesktopShell::GetVirtualDesktopCount();
        const int current = UltraCanvasDesktopShell::GetCurrentVirtualDesktop();
        Check(desktops >= 1 && current >= 0 && current < desktops,
              "the virtual desktops are counted (" + std::to_string(current + 1) + " of " +
                  std::to_string(desktops) + ")");
        Check(!UltraCanvasDesktopShell::SetVirtualDesktopCount(desktops + 1),
              "adding a desktop has no Windows call, and says so");

        UltraCanvasDesktopShellMonitor monitor;
        std::atomic<int> changes{0};
        Check(monitor.Start([&changes]() { ++changes; }) && monitor.IsNative(), "the monitor watches natively");
        // Its hooks go in on its own thread, a moment after Start returns;
        // count only what happens after that.
        PumpUntil([]() { return false; }, 500);
        changes.store(0);

        // Thai in the title: the list has to hand it back as UTF-8.
        const std::string title = "Desktop shell test \xE0\xB8\x97\xE0\xB8\x94\xE0\xB8\xAA\xE0\xB8\xAD\xE0\xB8\x9A";
        HWND hwnd = OpenTestWindow(title);
        Check(hwnd != nullptr, "a window of the test's own opens");
        if (hwnd) {
            const uint64_t id = IdOf(hwnd);
            Check(PumpUntil([&changes]() { return changes.load() > 0; }, 5000), "the monitor reports it");
            const auto windows = UltraCanvasDesktopShell::ListWindows();
            const DesktopWindowInfo* listed = FindListed(windows, hwnd);
            Check(listed != nullptr, "ListWindows lists it (" + std::to_string(windows.size()) + " windows)");
            if (listed) {
                Check(listed->title == title, "with its title in UTF-8: " + listed->title);
                Check(listed->appClass == "DesktopShellTest", "named after its program: " + listed->appClass);
                Check(listed->processId == static_cast<int>(GetCurrentProcessId()), "with this process's id");
                Check(!listed->skipTaskbar && !listed->minimized, "a taskbar window, not minimized");
            }
            bool shellListed = false;
            for (const DesktopWindowInfo& w : windows) {
                if (w.id == IdOf(GetShellWindow()) && GetShellWindow()) shellListed = w.skipTaskbar;
            }
            Check(!GetShellWindow() || shellListed, "the shell's desktop is listed, out of the taskbar");

            // A reserved edge: the work area makes room for it, and gets it back.
            const RECT before = WorkArea();
            Check(UltraCanvasDesktopShell::ReserveScreenEdges(id, 40, 0, 0, 0), "a left edge of 40 px is reserved");
            RECT during = {};
            const bool shrank = PumpUntil([&]() { during = WorkArea(); return during.left >= before.left + 40; }, 5000);
            Check(shrank, "the work area makes room (left " + std::to_string(before.left) + " -> " +
                              std::to_string(during.left) + ")");
            Check(UltraCanvasDesktopShell::ReserveScreenEdges(id, 0, 0, 0, 0), "the edge is released");
            RECT after = {};
            const bool restored = PumpUntil([&]() { after = WorkArea(); return after.left == before.left; }, 5000);
            Check(restored, "the work area is whole again (left " + std::to_string(after.left) + ")");

            Check(UltraCanvasDesktopShell::MinimizeWindow(id), "MinimizeWindow accepts it");
            Check(PumpUntil([hwnd]() { return IsIconic(hwnd) != FALSE; }, 5000), "the window minimizes");
            const auto minimizedList = UltraCanvasDesktopShell::ListWindows();
            const DesktopWindowInfo* minimized = FindListed(minimizedList, hwnd);
            Check(minimized && minimized->minimized, "and is listed as minimized");

            Check(UltraCanvasDesktopShell::CloseWindow(id), "CloseWindow asks it to close");
            Check(PumpUntil([hwnd]() { return IsWindow(hwnd) == FALSE; }, 5000), "the window closes");
            Check(!UltraCanvasDesktopShell::ActivateWindow(id), "a closed window cannot be activated");
        }
        monitor.Stop();

        UltraCanvasGlobalShortcut shortcut;
        std::string why;
        Check(shortcut.Start("Ctrl+Alt+Shift+F11", []() {}, &why), "a free combination is registered " + why);
        shortcut.Stop();
        std::string refused;
        Check(!shortcut.Start("Ctrl+NoSuchKey", []() {}, &refused) && !refused.empty(),
              "an unknown key name is refused: " + refused);
        std::string winV;
        const bool superV = shortcut.Start("Super+V", []() {}, &winV);
        std::printf("  (Super+V: %s)\n", superV ? "registered" : winV.c_str());
        shortcut.Stop();

        DesktopScreenImage shot;
        std::string captureError;
        if (UltraCanvasDesktopShell::CaptureScreenImage(shot, &captureError)) {
            const fs::path png = scratch / "Screenshots" / "shot.png";
            std::string writeError;
            Check(UltraCanvasDesktopShell::CaptureScreen(PathToUtf8(png), &writeError),
                  "the screen is written as a PNG file " + writeError);
            std::ifstream in(png, std::ios::binary);
            char signature[8] = {};
            in.read(signature, 8);
            Check(in && std::string(signature + 1, 3) == "PNG", "the file is a PNG");
        } else {
            std::printf("  (no screen here to write: %s)\n", captureError.c_str());
        }

        // The desktop at sign-in: switched to this test program and back. It
        // writes the user's Run key and hides the taskbar, so only on a CI
        // runner - a developer running the suite keeps their sign-in as is.
        const char* ci = std::getenv("CI");   // path-string-ok: ASCII flag
        if (ci && std::string(ci) == "true") {
            Check(UltraCanvasDesktopShell::CanReplaceSystemDesktop(), "Windows can start another desktop at sign-in");
            const bool explorer = FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
            APPBARDATA bar = {};
            bar.cbSize = sizeof(bar);
            const UINT taskbarBefore = static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &bar));
            wchar_t self[MAX_PATH * 2] = {};
            GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
            const std::string program = PathUtf8Detail::Utf16ToUtf8(std::wstring(self));
            std::string why;
            Check(UltraCanvasDesktopShell::ReplaceSystemDesktop(program, {"--session", "two words"}, &why),
                  "the switch is made " + why);
            Check(UltraCanvasDesktopShell::IsSystemDesktopReplaced(), "and reads back as made");
            wchar_t command[1024] = {};
            DWORD size = sizeof(command);
            RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                         L"UltraCanvas Desktop", RRF_RT_REG_SZ, nullptr, command, &size);
            const std::wstring expected = L"\"" + std::wstring(self) + L"\" --session \"two words\"";
            Check(std::wstring(command) == expected,
                  "the sign-in starts the program with its arguments: " + PathUtf8Detail::Utf16ToUtf8(std::wstring(command)));
            if (explorer) {
                Check(PumpUntil([&bar]() { return (SHAppBarMessage(ABM_GETSTATE, &bar) & ABS_AUTOHIDE) != 0; }, 3000),
                      "the taskbar hides itself");
            }
            Check(UltraCanvasDesktopShell::ReplaceSystemDesktop(program, {"--session"}, &why),
                  "switching again is fine " + why);
            Check(UltraCanvasDesktopShell::RestoreSystemDesktop(&why), "the switch is undone " + why);
            Check(!UltraCanvasDesktopShell::IsSystemDesktopReplaced(), "and nothing starts at sign-in any more");
            if (explorer) {
                Check(PumpUntil([&]() { return static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &bar)) == taskbarBefore; },
                                3000),
                      "the taskbar is as it was before the first switch (" + std::to_string(taskbarBefore) + ")");
            }
        } else {
            std::printf("  (desktop switch at sign-in: not on a CI runner, left alone)\n");
        }

        const DesktopDeviceActivity devices = UltraCanvasDesktopShell::ReadDeviceActivity();
        std::printf("  (devices: microphone %s, speaker %s, webcam %s, keyboard \"%s\")\n",
                    devices.microphoneInUse ? "in use" : "idle", devices.speakerPlaying ? "playing" : "idle",
                    devices.webcamInUse ? "in use" : "idle", devices.keyboardLayout.c_str());
    }
#endif

    std::error_code ec;
    fs::remove_all(scratch, ec);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
