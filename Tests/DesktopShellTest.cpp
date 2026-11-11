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
// back - beside Explorer and instead of it - and checks the Run value, the
// account's shell setting and the taskbar's state come back as they were;
// anywhere else that is skipped, so a developer's own taskbar and sign-in
// are never touched.
//
// On X11 with a display - CI runs it a second time under Xvfb, with
// ULTRACANVAS_TEST_DISPLAY_REQUIRED=1 so a missing display fails rather than
// skips - it drives the X11 backend: the monitor reports a root-window
// property set the way a window manager sets it, the virtual desktops and
// RandR's monitors are read back, a free combination is grabbed, and every
// Stop() must return within seconds (a blocking wake pipe once hung them for
// good; a hang ends the test with a failure instead of holding it).
//
// On Wayland (a build with ULTRACANVAS_WAYLAND, under a compositor that
// speaks wlr-foreign-toplevel-management and wlr-layer-shell: sway headless,
// labwc, the one ULTRA OS ships) it drives the Wayland backend: a window
// this test opens in a program of its own (--wayland-window) is listed with
// its UTF-8 title and app id, reported by the monitor, active, minimized,
// activated again and closed; a Desktop window (--wayland-desktop) goes on
// the layer shell and its reserved edges shrink the compositor's workspace,
// read back through swaymsg where the compositor is sway; the virtual
// desktops are read (ext-workspace where the compositor has it, one desktop
// where not), a window's RaiseAndFocus (xdg-activation) makes it the
// active one, a SessionLock window locks the session (ext-session-lock)
// and unlocks it when closed, a child of a toplevel is an xdg_popup put
// where asked and a child of a layer-shell desktop a panel beside it, a
// file drag starts (or says why not) and a GL surface gets a context from
// EGL on the compositor's connection. Without a Wayland display the section is skipped
// (ULTRACANVAS_TEST_DISPLAY_REQUIRED=1 fails it instead); the window list's
// checks are skipped under a compositor without the manager (weston).
//
// The portals the Wayland backend takes screenshots and global shortcuts
// through run as their own CTest entry (DesktopShellPortalTest: this test
// with --portals, under dbus-run-session): a fake org.freedesktop.portal.Desktop
// this test exports itself in a program of its own (--fake-portal) answers
// Screenshot with a small PNG, CreateSession and BindShortcuts as the
// real one does and fires Activated; the backend's CaptureScreen,
// CaptureScreenImage, CaptureScreenArea and UltraCanvasGlobalShortcut are
// driven against it, with the trigger spelling and the session's Close
// read back from the fake's log.
//
// Two parts of the Windows backend run in programs of their own, safely
// anywhere:
//   - UltraCanvasShellGuard, started on this test as the "desktop" that
//     crashes on request (--guard-child): it must start it again, and give
//     up after the third failure within a minute. Not being the session's
//     shell, it starts no Explorer and no startup programs.
//   - The notification area: this test again (--tray-child), on a Windows
//     desktop of its own that has no Explorer taskbar, so the host can own
//     Shell_TrayWnd there and shell32's real Shell_NotifyIcon reaches it -
//     icons added, changed, hidden, ballooned and removed, clicks in every
//     NIM_SETVERSION form, Shell_NotifyIconGetRect - while the user's own
//     taskbar is never touched.
//
// On Linux the notification area runs as its own CTest entry
// (DesktopShellTrayTest: this test with --tray, under dbus-run-session): a
// StatusNotifierItem the test exports itself over GDBus, with a pixmap, a
// tooltip and a dbusmenu, is listed by UltraCanvasTrayHost with its PNG and
// its menu as data, clicked, hidden and dropped when its connection closes.
// Version: 1.11.0 - the session lock, a popup and a panel placed by their parent,
//                   a file drag and a GL surface on Wayland
// Version: 1.10.0 - the portals (--portals, a fake portal of this test's own), the
//                   virtual desktops and RaiseAndFocus on Wayland
// Version: 1.9.0 - the Wayland backend under a compositor: the window list, the
//                  monitor, activation and closing, the layer shell's reserved edges
// Version: 1.8.1 - the X11 checks are left out of a Wayland build of the framework
// Version: 1.8.0 - two RandR monitors under Xvfb: ListScreens and the per-monitor struts;
//                  the lock, session and loudspeaker calls
// Version: 1.7.0 - the Linux notification area (--tray)
// Version: 1.6.0 - the X11 backend on a real display; Stop() within seconds
// Version: 1.5.0 - ListScreens, Microsoft Store applications, the Win key's tap
// Version: 1.4.0 - instead of Explorer, the shell guard, the tray host
// Version: 1.3.0 - ReplaceSystemDesktop / RestoreSystemDesktop
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShell.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <initializer_list>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "UltraCanvasPathUtf8.h"

#if defined(_WIN32)
// Shell_NotifyIconGetRect and NOTIFYICONDATAW's newest fields are declared
// for Windows 7 headers and later.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif
#include "UltraCanvasShellLink.h"
#include "ShellLinkTestSupport.h"
#include <windows.h>
#include <shellapi.h>   // SHAppBarMessage, Shell_NotifyIconW
#endif

// The platforms whose backend is X11 (UltraCanvasDesktopShellBackend.h);
// a Wayland build of the framework (ULTRACANVAS_WAYLAND) has none of it.
#if ((defined(__linux__) && !defined(__ANDROID__)) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
     defined(__NetBSD__)) && !defined(ULTRACANVAS_DISPLAY_WAYLAND)
#  define DESKTOPSHELLTEST_X11 1
#  include <X11/Xlib.h>
#  include <X11/Xatom.h>
#  include <X11/extensions/Xrandr.h>
#  include <unistd.h>
#  if defined(DESKTOPSHELLTEST_HAVE_GIO)
#    include <gio/gio.h>
#  endif
#endif

// A Wayland build of the framework: the backend is driven through windows
// this test opens in programs of its own (UltraCanvasApplication).
#if defined(__linux__) && defined(ULTRACANVAS_DISPLAY_WAYLAND)
#  define DESKTOPSHELLTEST_WAYLAND 1
#  include "UltraCanvasApplication.h"
#  include "UltraCanvasWindow.h"
#  if defined(DESKTOPSHELLTEST_HAVE_GIO)
#    include <gio/gio.h>
#    include <cairo/cairo.h>
#  endif
#  if defined(ULTRACANVAS_ENABLE_GL)
#    include "UltraCanvasGLSurface.h"
#    include <GL/gl.h>
#  endif
#  include <csignal>
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>
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

std::wstring ThisProgram() {
    wchar_t self[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
    return self;
}

std::wstring FolderOf(const std::wstring& path) {
    return path.substr(0, path.find_last_of(L"\\/"));
}

std::wstring Quoted(const std::wstring& text) {
    return L"\"" + text + L"\"";
}

// A program run to its end: its exit code, or -1 when it could not start or
// did not end within `ms`. Its output goes to this test's own (the
// handles are inherited), so its PASS and FAIL lines are part of the run.
// `desktop`, when given, is the Windows desktop it starts on.
int RunToEnd(const std::wstring& program, const std::wstring& commandLine, DWORD ms,
             const wchar_t* desktop = nullptr) {
    std::fflush(stdout);
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    std::wstring desktopName = desktop ? desktop : L"";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    if (desktop) startup.lpDesktop = desktopName.data();
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    BOOL inherit = FALSE;
    if (out && out != INVALID_HANDLE_VALUE) {
        SetHandleInformation(out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        if (err && err != INVALID_HANDLE_VALUE) SetHandleInformation(err, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = out;
        startup.hStdError = err;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        inherit = TRUE;
    }
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(program.c_str(), buffer.data(), nullptr, nullptr, inherit, 0, nullptr, nullptr, &startup,
                        &process)) {
        std::printf("  (could not start %s: error %lu)\n", PathUtf8Detail::Utf16ToUtf8(program).c_str(),
                    GetLastError());
        return -1;
    }
    CloseHandle(process.hThread);
    int code = -1;
    if (WaitForSingleObject(process.hProcess, ms) == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(process.hProcess, &exitCode)) code = static_cast<int>(exitCode);
    } else {
        TerminateProcess(process.hProcess, 99);
    }
    CloseHandle(process.hProcess);
    return code;
}

int ReadCount(const fs::path& file) {
    std::ifstream in(file);
    int n = 0;
    in >> n;
    return n;
}

// --guard-child <mode> <counter file>: the "desktop" the shell guard starts.
// It counts its starts, then crashes (an access violation's exit code),
// crashes the first time only, or exits cleanly.
int GuardChild(const std::string& mode, const fs::path& counter) {
    const int n = ReadCount(counter) + 1;
    {
        std::ofstream out(counter, std::ios::trunc);
        out << n << "\n";
    }
    if (mode == "crash" || (mode == "crash-once" && n == 1)) ExitProcess(0xC0000005u);
    return 0;
}

// ----- the notification area, on a desktop of its own (--tray-child) -----

// What the tray test's window was sent: its callback message's arguments,
// and whether TaskbarCreated reached it.
struct TrayInbox {
    std::vector<std::pair<WPARAM, LPARAM>> callbacks;
    bool taskbarCreated = false;
};
TrayInbox g_inbox;
constexpr UINT kTrayCallback = WM_APP + 0x31;
UINT g_taskbarCreated = 0;

LRESULT CALLBACK TrayTestWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == kTrayCallback) {
        g_inbox.callbacks.emplace_back(wParam, lParam);
        return 0;
    }
    if (g_taskbarCreated != 0 && msg == g_taskbarCreated) {
        g_inbox.taskbarCreated = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND OpenTrayTestWindow() {
    static const bool registered = []() {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TrayTestWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"UltraCanvasTrayTestWindow";
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered) return nullptr;
    // Top-level and never shown, as a tray program's own window usually is.
    return CreateWindowExW(0, L"UltraCanvasTrayTestWindow", L"tray test", WS_OVERLAPPED, 0, 0, 10, 10, nullptr,
                           nullptr, GetModuleHandleW(nullptr), nullptr);
}

// A 16x16 icon of one colour, made in memory.
HICON MakeIcon(COLORREF colour) {
    BITMAPV5HEADER header = {};
    header.bV5Size = sizeof(header);
    header.bV5Width = 16;
    header.bV5Height = -16;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    // No device context: an RGB DIB needs none, and this desktop has no screen.
    HBITMAP colourBitmap = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits,
                                            nullptr, 0);
    if (!colourBitmap || !bits) return nullptr;
    const uint32_t pixel = 0xFF000000u | (GetRValue(colour) << 16) | (GetGValue(colour) << 8) | GetBValue(colour);
    for (int i = 0; i < 16 * 16; ++i) static_cast<uint32_t*>(bits)[i] = pixel;
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, nullptr);
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmColor = colourBitmap;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(colourBitmap);
    DeleteObject(mask);
    return icon;
}

void CopyText(wchar_t* field, size_t capacity, const std::string& utf8) {
    const std::wstring wide = PathUtf8Detail::Utf8ToUtf16<std::wstring>(utf8);
    wcsncpy(field, wide.c_str(), capacity - 1);
    field[capacity - 1] = L'\0';
}

NOTIFYICONDATAW IconData(HWND window, UINT id) {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = id;
    return data;
}

const DesktopTrayIcon* FindTrayIcon(const std::vector<DesktopTrayIcon>& icons, const std::string& tooltip) {
    for (const DesktopTrayIcon& icon : icons) {
        if (icon.tooltip == tooltip) return &icon;
    }
    return nullptr;
}

int TrayChild() {
    std::printf("  [notification area, on a desktop of its own]\n");
    Check(!UltraCanvasDesktopShell::IsSystemDesktopRunning(), "no Explorer taskbar on this desktop");
    Check(FindWindowW(L"Shell_TrayWnd", nullptr) == nullptr, "and no notification area yet");

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    HWND window = OpenTrayTestWindow();
    Check(window != nullptr, "the test program's window opens");
    if (!window) return g_failures;

    UltraCanvasTrayHost host;
    std::atomic<int> changes{0};
    std::string why;
    Check(host.Start([&changes]() { ++changes; }, &why), "the tray host starts " + why);
    Check(FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr, "it owns Shell_TrayWnd");
    Check(!UltraCanvasDesktopShell::IsSystemDesktopRunning(), "which is not taken for Explorer's taskbar");
    Check(PumpUntil([]() { return g_inbox.taskbarCreated; }, 3000), "programs are asked to add their icons (TaskbarCreated)");
    {
        UltraCanvasTrayHost second;
        std::string refused;
        Check(!second.Start([]() {}, &refused) && !refused.empty(), "a second host is refused: " + refused);
    }

    // Added through shell32's own Shell_NotifyIcon, the icon destroyed at
    // once - as programs do - and a Thai tooltip.
    const std::string tip = "Tray test \xE0\xB8\x97\xE0\xB8\x94\xE0\xB8\xAA\xE0\xB8\xAD\xE0\xB8\x9A";
    NOTIFYICONDATAW data = IconData(window, 7);
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayCallback;
    data.hIcon = MakeIcon(RGB(220, 30, 30));
    CopyText(data.szTip, std::size(data.szTip), tip);
    Check(Shell_NotifyIconW(NIM_ADD, &data) != FALSE, "Shell_NotifyIcon adds an icon");
    DestroyIcon(data.hIcon);
    std::vector<DesktopTrayIcon> icons;
    Check(PumpUntil([&]() {
              icons = host.ListIcons();
              return icons.size() == 1 && !icons[0].iconFile.empty();
          }, 5000),
          "the host lists it, with its picture (" + std::to_string(icons.size()) + " icons)");
    std::string id;
    if (icons.size() == 1) {
        const DesktopTrayIcon& icon = icons[0];
        id = icon.id;
        Check(icon.tooltip == tip, "its tooltip in UTF-8: " + icon.tooltip);
        Check(icon.appClass == "DesktopShellTest", "named after its program: " + icon.appClass);
        Check(icon.processId == static_cast<int>(GetCurrentProcessId()), "with this process's id");
        std::ifstream png(PathFromUtf8(icon.iconFile), std::ios::binary);
        char signature[8] = {};
        png.read(signature, 8);
        Check(png && std::string(signature + 1, 3) == "PNG", "its picture is a PNG: " + icon.iconFile);
    }

    // Clicks, version 0: the button messages alone, the icon's id in wParam.
    g_inbox.callbacks.clear();
    Check(host.Click(id, TrayClick::Left), "a left click is handed over");
    PumpUntil([]() { return g_inbox.callbacks.size() >= 2; }, 3000);
    Check(g_inbox.callbacks.size() == 2 && g_inbox.callbacks[0].first == 7 &&
              g_inbox.callbacks[0].second == WM_LBUTTONDOWN && g_inbox.callbacks[1].second == WM_LBUTTONUP,
          "as WM_LBUTTONDOWN, WM_LBUTTONUP with the icon's id (" + std::to_string(g_inbox.callbacks.size()) +
              " messages)");
    g_inbox.callbacks.clear();
    host.Click(id, TrayClick::Left);   // within the double-click time of the first
    PumpUntil([]() { return g_inbox.callbacks.size() >= 2; }, 3000);
    Check(!g_inbox.callbacks.empty() && g_inbox.callbacks[0].second == WM_LBUTTONDBLCLK,
          "a second click soon after is a double click");

    // Version 4: the event and the id packed in lParam, the point in wParam,
    // and WM_CONTEXTMENU after a right click.
    data = IconData(window, 7);
    data.uVersion = NOTIFYICON_VERSION_4;
    Check(Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE, "NIM_SETVERSION 4 is accepted");
    g_inbox.callbacks.clear();
    Check(host.Click(id, TrayClick::Right), "a right click is handed over");
    PumpUntil([]() { return g_inbox.callbacks.size() >= 3; }, 3000);
    const auto& got = g_inbox.callbacks;
    Check(got.size() == 3 && LOWORD(got[0].second) == WM_RBUTTONDOWN && HIWORD(got[0].second) == 7 &&
              LOWORD(got[1].second) == WM_RBUTTONUP && LOWORD(got[2].second) == WM_CONTEXTMENU,
          "as WM_RBUTTONDOWN, WM_RBUTTONUP, WM_CONTEXTMENU with the id beside them (" +
              std::to_string(got.size()) + " messages)");

    // Where it is: around the point it was clicked at.
    NOTIFYICONIDENTIFIER where = {};
    where.cbSize = sizeof(where);
    where.hWnd = window;
    where.uID = 7;
    RECT rect = {};
    Check(SUCCEEDED(Shell_NotifyIconGetRect(&where, &rect)) && rect.right - rect.left == 16 &&
              rect.bottom - rect.top == 16,
          "Shell_NotifyIconGetRect answers a rectangle (" + std::to_string(rect.right - rect.left) + " wide)");

    // A new tooltip, a balloon, hidden and shown.
    data = IconData(window, 7);
    data.uFlags = NIF_TIP | NIF_INFO;
    CopyText(data.szTip, std::size(data.szTip), "changed");
    CopyText(data.szInfoTitle, std::size(data.szInfoTitle), "Backup");
    CopyText(data.szInfo, std::size(data.szInfo), "Finished");
    Check(Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE, "NIM_MODIFY is accepted");
    const DesktopTrayIcon* changed = nullptr;
    PumpUntil([&]() {
        icons = host.ListIcons();
        changed = FindTrayIcon(icons, "changed");
        return changed != nullptr;
    }, 3000);
    Check(changed != nullptr, "the tooltip changes");
    Check(changed && changed->balloonSerial != 0 && changed->balloonTitle == "Backup" &&
              changed->balloonText == "Finished",
          "and the balloon is there to show");
    data = IconData(window, 7);
    data.uFlags = NIF_STATE;
    data.dwState = NIS_HIDDEN;
    data.dwStateMask = NIS_HIDDEN;
    Shell_NotifyIconW(NIM_MODIFY, &data);
    Check(PumpUntil([&]() { return host.ListIcons().empty(); }, 3000), "a hidden icon is not listed");
    data.dwState = 0;
    Shell_NotifyIconW(NIM_MODIFY, &data);
    Check(PumpUntil([&]() { return host.ListIcons().size() == 1; }, 3000), "and is listed again when shown");

    // A program that ends without NIM_DELETE: its icon goes with its window.
    HWND other = OpenTrayTestWindow();
    NOTIFYICONDATAW orphan = IconData(other, 1);
    orphan.uFlags = NIF_TIP;
    CopyText(orphan.szTip, std::size(orphan.szTip), "orphan");
    Shell_NotifyIconW(NIM_ADD, &orphan);
    Check(PumpUntil([&]() { return FindTrayIcon(host.ListIcons(), "orphan") != nullptr; }, 3000),
          "a second icon is listed");
    DestroyWindow(other);
    Check(PumpUntil([&]() { return FindTrayIcon(host.ListIcons(), "orphan") == nullptr; }, 6000),
          "and dropped when its window is gone");

    data = IconData(window, 7);
    Check(Shell_NotifyIconW(NIM_DELETE, &data) != FALSE, "NIM_DELETE is accepted");
    Check(PumpUntil([&]() { return host.ListIcons().empty(); }, 3000), "and the icon is gone");
    Check(Shell_NotifyIconW(NIM_MODIFY, &data) == FALSE, "a deleted icon cannot be changed");
    Check(changes.load() > 0, "the host reported its changes (" + std::to_string(changes.load()) + ")");

    host.Stop();
    Check(FindWindowW(L"Shell_TrayWnd", nullptr) == nullptr, "stopped, Shell_TrayWnd is gone");
    DestroyWindow(window);
    return g_failures;
}
#endif

#if defined(DESKTOPSHELLTEST_X11) || defined(DESKTOPSHELLTEST_WAYLAND)
// A step that must not hang - a Stop() joining a thread - on a thread of its
// own. Still running after `ms`, it never returns, and nothing after it could
// run either (the stuck join holds the process): say so and end the test.
void FinishesWithin(const std::function<void()>& step, int ms, const std::string& what) {
    auto finished = std::make_shared<std::promise<void>>();
    std::future<void> done = finished->get_future();
    std::thread([step, finished]() {
        step();
        finished->set_value();
    }).detach();
    if (done.wait_for(std::chrono::milliseconds(ms)) == std::future_status::ready) {
        Check(true, what);
        return;
    }
    Check(false, what + " - still running after " + std::to_string(ms / 1000) + " s, so the test ends here");
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    std::fflush(stdout);
    std::_Exit(1);
}

template <typename Done>
bool WaitUntil(Done done, int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done()) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}
#endif

#if defined(DESKTOPSHELLTEST_WAYLAND)
// ----- a window of this test's own, in a program of its own -----
// --wayland-window <title>: an ordinary window with that title, open until
// the compositor closes it (CloseWindow from the parent) or a minute passes.
// --wayland-desktop <title>: a Desktop window (a layer surface on the
// compositor's background layer) that reserves 40 pixels at the top and 60
// on the right, printing "reserved=1" when the backend took them; open until
// the parent ends it.
int WaylandWindowChild(const std::string& title, bool desktop) {
    UltraCanvasApplication app;
    if (!app.Initialize("DesktopShellTest")) return 2;
    WindowConfig config;
    config.title = title;
    config.width = 320;
    config.height = 200;
    if (desktop) config.type = WindowType::Desktop;
    auto window = CreateWindow(config);
    if (!window) return 3;
    window->Show();
    if (desktop) {
        const uint64_t id = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(window->GetNativeHandle()));
        const bool reserved = UltraCanvasDesktopShell::ReserveScreenEdges(id, 0, 60, 40, 0);
        std::printf("reserved=%d\n", reserved ? 1 : 0);
        std::fflush(stdout);
    }
    // The main loop, until the window is closed (the parent's CloseWindow
    // through the compositor) or a minute passes; the native loop runs
    // only inside Run().
    SetEnv("ULTRACANVAS_CLOSE_AFTER_MS", "60000");
    app.Run();
    return 0;
}

// --wayland-two-windows <title>: two windows, "<title> A" then "<title> B"
// (the compositor focuses the second), and a second and a half later A's
// RaiseAndFocus, which asks for the focus through xdg-activation. Open
// until both are closed or a minute passes.
int WaylandTwoWindowsChild(const std::string& title) {
    UltraCanvasApplication app;
    if (!app.Initialize("DesktopShellTest")) return 2;
    WindowConfig config;
    config.width = 320;
    config.height = 200;
    config.title = title + " A";
    auto a = CreateWindow(config);
    config.title = title + " B";
    auto b = CreateWindow(config);
    if (!a || !b) return 3;
    a->Show();
    b->Show();
    app.StartTimer(1500, false, [a](TimerId) { a->RaiseAndFocus(); });
    SetEnv("ULTRACANVAS_CLOSE_AFTER_MS", "60000");
    app.Run();
    return 0;
}

// --wayland-lock <title>: a SessionLock window; "locked=1" once the
// compositor granted the lock (then the window closes, which unlocks, and
// the program ends), "locked=0" when it refused, "locked=timeout" after
// ten seconds of neither.
int WaylandLockChild(const std::string& title) {
    UltraCanvasApplication app;
    if (!app.Initialize("DesktopShellTest")) return 2;
    WindowConfig config;
    config.title = title;
    config.type = WindowType::SessionLock;
    auto window = CreateWindow(config);
    if (!window) return 3;
    window->Show();
    const auto started = std::chrono::steady_clock::now();
    app.StartTimer(100, true, [&](TimerId id) {
        const char* verdict = nullptr;
        if (app.IsSessionLocked()) verdict = "locked=1";
        else if (app.IsSessionLockRefused()) verdict = "locked=0";
        else if (std::chrono::steady_clock::now() - started > std::chrono::seconds(10)) verdict = "locked=timeout";
        if (!verdict) return;
        std::printf("%s\n", verdict);
        std::fflush(stdout);
        app.StopTimer(id);
        window->Close();
    });
    app.Run();
    return 0;
}

// --wayland-popup <title>: a toplevel, and a borderless child of it asked
// for at 40,30 inside it; a second and a half later "popup=<x>,<y> <w>x<h>"
// as the compositor placed and sized it, then both close.
int WaylandPopupChild(const std::string& title) {
    UltraCanvasApplication app;
    if (!app.Initialize("DesktopShellTest")) return 2;
    WindowConfig config;
    config.title = title;
    config.width = 320;
    config.height = 200;
    auto parent = CreateWindow(config);
    if (!parent) return 3;
    parent->Show();
    WindowConfig child;
    child.title = title + " popup";
    child.type = WindowType::Borderless;
    child.width = 100;
    child.height = 80;
    child.parentWindow = parent.get();
    auto popup = CreateWindow(child);
    if (!popup) return 3;
    int px = 0, py = 0;
    parent->GetWindowPosition(px, py);
    popup->SetWindowPosition(px + 40, py + 30);
    popup->Show();
    app.StartTimer(1500, false, [&](TimerId) {
        int x = 0, y = 0;
        popup->GetWindowPosition(x, y);
        std::printf("popup=%d,%d %dx%d\n", x - px, y - py, popup->GetConfig().width, popup->GetConfig().height);
        std::fflush(stdout);
        popup->Close();
        parent->Close();
    });
    SetEnv("ULTRACANVAS_CLOSE_AFTER_MS", "60000");
    app.Run();
    return 0;
}

// --wayland-panel <title>: a Desktop window and a borderless child of it at
// 100,50 (a panel beside a bar); "panel=<w>x<h>" with the child's size once
// configured, then both close.
int WaylandPanelChild(const std::string& title) {
    UltraCanvasApplication app;
    if (!app.Initialize("DesktopShellTest")) return 2;
    WindowConfig config;
    config.title = title;
    config.type = WindowType::Desktop;
    auto desktop = CreateWindow(config);
    if (!desktop) return 3;
    desktop->Show();
    WindowConfig child;
    child.title = title + " panel";
    child.type = WindowType::Borderless;
    child.width = 300;
    child.height = 200;
    child.parentWindow = desktop.get();
    auto panel = CreateWindow(child);
    if (!panel) return 3;
    panel->SetWindowPosition(100, 50);
    panel->Show();
    app.StartTimer(1500, false, [&](TimerId) {
        std::printf("panel=%dx%d\n", panel->GetConfig().width, panel->GetConfig().height);
        std::fflush(stdout);
        panel->Close();
        desktop->Close();
    });
    SetEnv("ULTRACANVAS_CLOSE_AFTER_MS", "60000");
    app.Run();
    return 0;
}

// --wayland-drag <title>: a window that starts a file drag of this program
// a second after it is up: "drag=no-serial" where the compositor gave the
// program no input to drag from (a seat without devices), "drag=cancelled"
// or "drag=accepted" as the compositor answered it, "drag=timeout" when it
// did not within five seconds.
int WaylandDragChild(const std::string& title) {
    UltraCanvasApplication app;
    if (!app.Initialize("DesktopShellTest")) return 2;
    WindowConfig config;
    config.title = title;
    config.width = 320;
    config.height = 200;
    auto window = CreateWindow(config);
    if (!window) return 3;
    window->Show();
    std::string verdict;
    char self[4096];
    const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    const std::string program = n > 0 ? std::string(self, static_cast<size_t>(n)) : "/bin/true";
    app.StartTimer(1000, false, [&](TimerId) {
        const bool started = window->StartNativeFileDrag({program}, [&](bool accepted, bool) {
            verdict = accepted ? "drag=accepted" : "drag=cancelled";
        });
        if (!started) verdict = "drag=no-serial";
    });
    app.StartTimer(6000, false, [&](TimerId) {
        if (verdict.empty()) verdict = "drag=timeout";
    });
    app.StartTimer(100, true, [&](TimerId id) {
        if (verdict.empty()) return;
        std::printf("%s\n", verdict.c_str());
        std::fflush(stdout);
        app.StopTimer(id);
        window->Close();
    });
    SetEnv("ULTRACANVAS_CLOSE_AFTER_MS", "60000");
    app.Run();
    return 0;
}

#if defined(ULTRACANVAS_ENABLE_GL)
// --wayland-gl <title>: a window with a GL surface; "gl=<renderer>" once
// its context is up and a frame drawn, "gl=none" when the surface got no
// context within five seconds.
class ProbeGLSurface : public UltraCanvasGLSurface {
public:
    using UltraCanvasGLSurface::UltraCanvasGLSurface;
    std::string renderer;
    int frames = 0;
    void OnGLInit() override {
        const GLubyte* name = glGetString(GL_RENDERER);
        renderer = name ? reinterpret_cast<const char*>(name) : "?";
    }
    void OnGLRender(const RenderSurfaceInfo&) override {
        glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ++frames;
    }
};

int WaylandGLChild(const std::string& title) {
    UltraCanvasApplication app;
    if (!app.Initialize("DesktopShellTest")) return 2;
    WindowConfig config;
    config.title = title;
    config.width = 320;
    config.height = 200;
    auto window = CreateWindow(config);
    if (!window) return 3;
    auto surface = std::make_shared<ProbeGLSurface>("gl", 10, 10, 200, 150);
    window->AddChild(surface);
    window->Show();
    const auto started = std::chrono::steady_clock::now();
    app.StartTimer(100, true, [&](TimerId id) {
        std::string verdict;
        if (surface->frames > 0 && !surface->renderer.empty()) verdict = "gl=" + surface->renderer;
        else if (std::chrono::steady_clock::now() - started > std::chrono::seconds(5)) verdict = "gl=none";
        if (verdict.empty()) return;
        std::printf("%s\n", verdict.c_str());
        std::fflush(stdout);
        app.StopTimer(id);
        window->Close();
    });
    SetEnv("ULTRACANVAS_CLOSE_AFTER_MS", "60000");
    app.Run();
    return 0;
}
#endif

// The first line starting with `prefix` a child wrote to `output`, once
// it has written one (the framework's own notes go to the same stream).
std::string FirstLine(const fs::path& output, int ms, const std::string& prefix) {
    std::string found;
    WaitUntil([&]() {
        std::ifstream in(output);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind(prefix, 0) == 0) {
                found = line;
                return true;
            }
        }
        return false;
    }, ms);
    return found;
}

// This test again with `arguments`, its output to `output` (a file). The
// child's pid, or -1.
pid_t SpawnSelf(const std::vector<std::string>& arguments, const fs::path& output) {
    char self[4096];
    const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0) return -1;
    self[n] = 0;
    const pid_t pid = fork();
    if (pid != 0) return pid;
    const int fd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);   // path-string-ok: ASCII temp path
    if (fd >= 0) {
        dup2(fd, 1);
        ::close(fd);
    }
    std::vector<char*> argv;
    argv.push_back(self);
    for (const std::string& a : arguments) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    execv(self, argv.data());
    _exit(127);
}

// The child's exit status, waited for up to `ms`; -1 when it is still
// running then (and left running).
int WaitChild(pid_t pid, int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
        int status = 0;
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        if (done < 0) return -1;
        if (std::chrono::steady_clock::now() >= until) return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void EndChild(pid_t pid) {
    if (pid <= 0) return;
    kill(pid, SIGTERM);
    if (WaitChild(pid, 3000) < 0) {
        kill(pid, SIGKILL);
        WaitChild(pid, 3000);
    }
}

// sway's own view of the area its windows get (swaymsg, through the
// socket sway gives its children in SWAYSOCK): the first "rect" of the
// reply to `query` (get_outputs: the output; get_workspaces: the output
// less every exclusive zone). False where this is not sway, or swaymsg
// is not installed.
bool SwayRect(const char* query, int& x, int& y, int& width, int& height) {
    if (!std::getenv("SWAYSOCK")) return false;
    const std::string command = std::string("swaymsg -r -t ") + query + " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return false;
    std::string reply;
    char buffer[4096];
    size_t got = 0;
    while ((got = fread(buffer, 1, sizeof(buffer), pipe)) > 0) reply.append(buffer, got);
    if (pclose(pipe) != 0) return false;
    // swaymsg pretty-prints even with -r: the spaces go before the search.
    reply.erase(std::remove_if(reply.begin(), reply.end(), [](unsigned char c) { return std::isspace(c); }), reply.end());
    const size_t at = reply.find("\"rect\":{");
    if (at == std::string::npos) return false;
    auto number = [&reply, at](const char* key) {
        const size_t k = reply.find(std::string("\"") + key + "\":", at);
        return k == std::string::npos ? -1 : std::atoi(reply.c_str() + k + std::strlen(key) + 3);
    };
    x = number("x");
    y = number("y");
    width = number("width");
    height = number("height");
    return width > 0 && height > 0;
}
#endif

#if defined(DESKTOPSHELLTEST_WAYLAND) && defined(DESKTOPSHELLTEST_HAVE_GIO)
// ===== A FAKE PORTAL (--fake-portal <dir>) =====
// org.freedesktop.portal.Desktop as xdg-desktop-portal exports it, for the
// two interfaces the backend uses: Screenshot answers with a 4x3 PNG it
// writes under <dir>; CreateSession and BindShortcuts answer as the real
// portal does (a Request whose Response carries the session, the bound
// shortcuts echoed) and Activated fires twice after the binding. What it
// did goes to <dir>/portal.log, one line each, for the test to read.
namespace {

constexpr const char* kFakePortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kFakePortalXml =
    "<node>"
    "  <interface name='org.freedesktop.portal.Screenshot'>"
    "    <method name='Screenshot'>"
    "      <arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.freedesktop.portal.GlobalShortcuts'>"
    "    <method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
    "    <method name='BindShortcuts'>"
    "      <arg type='o' direction='in'/><arg type='a(sa{sv})' direction='in'/><arg type='s' direction='in'/>"
    "      <arg type='a{sv}' direction='in'/><arg type='o' direction='out'/>"
    "    </method>"
    "    <signal name='Activated'><arg type='o'/><arg type='s'/><arg type='t'/><arg type='a{sv}'/></signal>"
    "  </interface>"
    "</node>";
constexpr const char* kFakeSessionXml =
    "<node><interface name='org.freedesktop.portal.Session'><method name='Close'/></interface></node>";

struct FakePortal {
    GDBusConnection* bus = nullptr;
    fs::path dir;
    int shots = 0;
    GDBusNodeInfo* sessionInfo = nullptr;
};
FakePortal g_portal;

void PortalLog(const std::string& line) {
    std::ofstream out(g_portal.dir / "portal.log", std::ios::app);
    out << line << "\n";
}

std::string SenderPart(const char* sender) {
    std::string part = sender ? sender : "";
    if (!part.empty() && part[0] == ':') part.erase(0, 1);
    std::replace(part.begin(), part.end(), '.', '_');
    return part;
}

std::string OptionString(GVariant* options, const char* key) {
    const char* value = nullptr;
    if (options && g_variant_lookup(options, key, "&s", &value) && value) return value;
    return {};
}

void EmitResponse(const char* sender, const std::string& requestPath, GVariantBuilder* results) {
    g_dbus_connection_emit_signal(g_portal.bus, sender, requestPath.c_str(), "org.freedesktop.portal.Request",
                                  "Response", g_variant_new("(ua{sv})", 0u, results), nullptr);
}

gboolean FireActivated(gpointer data) {
    auto* session = static_cast<std::string*>(data);
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    g_dbus_connection_emit_signal(g_portal.bus, nullptr, kFakePortalPath, "org.freedesktop.portal.GlobalShortcuts",
                                  "Activated",
                                  g_variant_new("(osta{sv})", session->c_str(), "shortcut",
                                                static_cast<guint64>(g_get_monotonic_time()), &options),
                                  nullptr);
    PortalLog("activated");
    delete session;
    return G_SOURCE_REMOVE;
}

void OnFakeSessionMethod(GDBusConnection*, const gchar*, const gchar* path, const gchar*, const gchar* method,
                         GVariant*, GDBusMethodInvocation* invocation, gpointer) {
    if (std::strcmp(method, "Close") == 0) PortalLog(std::string("closed ") + path);
    g_dbus_method_invocation_return_value(invocation, nullptr);
}
const GDBusInterfaceVTable kFakeSessionVTable = {OnFakeSessionMethod, nullptr, nullptr, {nullptr}};

void OnFakePortalMethod(GDBusConnection*, const gchar* sender, const gchar*, const gchar*, const gchar* method,
                        GVariant* parameters, GDBusMethodInvocation* invocation, gpointer) {
    const std::string requests = std::string(kFakePortalPath) + "/request/" + SenderPart(sender) + "/";
    if (std::strcmp(method, "Screenshot") == 0) {
        const char* parent = nullptr;
        GVariant* options = nullptr;
        g_variant_get(parameters, "(&s@a{sv})", &parent, &options);
        const std::string requestPath = requests + OptionString(options, "handle_token");
        g_variant_unref(options);
        // 4x3, one colour: blue 0.6, green 0.4, red 0.2 - 0x99, 0x66, 0x33.
        const fs::path file = g_portal.dir / ("portal-shot-" + std::to_string(++g_portal.shots) + ".png");
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 4, 3);
        cairo_t* cr = cairo_create(surface);
        cairo_set_source_rgb(cr, 0.2, 0.4, 0.6);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_write_to_png(surface, file.c_str());   // path-string-ok: ASCII temp path
        cairo_surface_destroy(surface);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", requestPath.c_str()));
        GVariantBuilder results;
        g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&results, "{sv}", "uri", g_variant_new_string(("file://" + file.string()).c_str()));   // path-string-ok: ASCII temp path
        EmitResponse(sender, requestPath, &results);
        PortalLog("screenshot " + file.string());   // path-string-ok: ASCII temp path
    } else if (std::strcmp(method, "CreateSession") == 0) {
        GVariant* options = nullptr;
        g_variant_get(parameters, "(@a{sv})", &options);
        const std::string requestPath = requests + OptionString(options, "handle_token");
        const std::string sessionPath = std::string(kFakePortalPath) + "/session/" + SenderPart(sender) + "/" +
                                        OptionString(options, "session_handle_token");
        g_variant_unref(options);
        g_dbus_connection_register_object(g_portal.bus, sessionPath.c_str(), g_portal.sessionInfo->interfaces[0],
                                          &kFakeSessionVTable, nullptr, nullptr, nullptr);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", requestPath.c_str()));
        GVariantBuilder results;
        g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&results, "{sv}", "session_handle", g_variant_new_string(sessionPath.c_str()));
        EmitResponse(sender, requestPath, &results);
        PortalLog("session " + sessionPath);
    } else if (std::strcmp(method, "BindShortcuts") == 0) {
        const char* session = nullptr;
        GVariant* shortcuts = nullptr;
        const char* parent = nullptr;
        GVariant* options = nullptr;
        g_variant_get(parameters, "(&o@a(sa{sv})&s@a{sv})", &session, &shortcuts, &parent, &options);
        const std::string requestPath = requests + OptionString(options, "handle_token");
        GVariantIter iter;
        g_variant_iter_init(&iter, shortcuts);
        const char* id = nullptr;
        GVariant* shortcutOptions = nullptr;
        while (g_variant_iter_next(&iter, "(&s@a{sv})", &id, &shortcutOptions)) {
            PortalLog(std::string("bind ") + id + " " + OptionString(shortcutOptions, "preferred_trigger"));
            g_variant_unref(shortcutOptions);
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", requestPath.c_str()));
        GVariantBuilder results;
        g_variant_builder_init(&results, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&results, "{sv}", "shortcuts", shortcuts);
        EmitResponse(sender, requestPath, &results);
        g_timeout_add(300, FireActivated, new std::string(session));
        g_timeout_add(600, FireActivated, new std::string(session));
        g_variant_unref(options);
    } else {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownMethod", method);
    }
}
const GDBusInterfaceVTable kFakePortalVTable = {OnFakePortalMethod, nullptr, nullptr, {nullptr}};

void OnPortalNameAcquired(GDBusConnection*, const gchar*, gpointer) {
    std::ofstream(g_portal.dir / "ready") << "ready\n";
}

int FakePortalMain(const fs::path& dir) {
    g_portal.dir = dir;
    fs::create_directories(dir);
    GError* error = nullptr;
    g_portal.bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!g_portal.bus) {
        std::fprintf(stderr, "fake portal: no session bus: %s\n", error ? error->message : "");
        return 2;
    }
    GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(kFakePortalXml, nullptr);
    g_portal.sessionInfo = g_dbus_node_info_new_for_xml(kFakeSessionXml, nullptr);
    for (GDBusInterfaceInfo** iface = info->interfaces; iface && *iface; ++iface) {
        g_dbus_connection_register_object(g_portal.bus, kFakePortalPath, *iface, &kFakePortalVTable, nullptr, nullptr,
                                          nullptr);
    }
    g_bus_own_name_on_connection(g_portal.bus, "org.freedesktop.portal.Desktop", G_BUS_NAME_OWNER_FLAGS_NONE,
                                 OnPortalNameAcquired, nullptr, nullptr, nullptr);
    for (;;) g_main_context_iteration(nullptr, TRUE);   // until the parent ends this program
}

// ===== THE PORTALS, AGAINST THE FAKE (--portals) =====
int PortalTest() {
    std::printf("The portals (screenshots, global shortcuts) against a fake org.freedesktop.portal.Desktop\n");
    const fs::path scratch = MakeScratch();
    const pid_t fake = SpawnSelf({"--fake-portal", scratch.string()}, scratch / "fake.out");   // path-string-ok: ASCII temp path
    Check(fake > 0 && WaitUntil([&scratch]() { return fs::exists(scratch / "ready"); }, 10000),
          "the fake portal owns org.freedesktop.portal.Desktop on the test's bus");
    auto logLines = [&scratch]() {
        std::vector<std::string> lines;
        std::ifstream in(scratch / "portal.log");
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        return lines;
    };
    auto logged = [&logLines](const std::string& prefix) {
        for (const std::string& line : logLines()) {
            if (line.rfind(prefix, 0) == 0) return true;
        }
        return false;
    };

    // ----- screenshots -----
    std::string why;
    const fs::path shot = scratch / "shots" / "screen.png";
    Check(UltraCanvasDesktopShell::CaptureScreen(shot.string(), &why) && fs::exists(shot),   // path-string-ok: ASCII temp path
          "CaptureScreen writes the portal's picture to the path asked for " + why);
    Check(!fs::exists(scratch / "portal-shot-1.png"), "and the portal's own file is gone (moved)");
    DesktopScreenImage image;
    Check(UltraCanvasDesktopShell::CaptureScreenImage(image, &why) && image.width == 4 && image.height == 3 &&
              image.stride == 16,
          "CaptureScreenImage reads it into memory, 4x3 BGRx " + why);
    Check(image.IsValid() && image.pixels[0] == 0x99 && image.pixels[1] == 0x66 && image.pixels[2] == 0x33 &&
              image.pixels[3] == 0xFF,
          "... the pixels in cairo's layout: blue, green, red, opaque");
    Check(!fs::exists(scratch / "portal-shot-2.png"), "and nothing of it is left on disk");
    DesktopScreenImage area;
    Check(UltraCanvasDesktopShell::CaptureScreenArea(2, 1, 10, 10, area, &why) && area.x == 2 && area.y == 1 &&
              area.width == 2 && area.height == 2,
          "CaptureScreenArea crops it to what is on the screen " + why);
    Check(!UltraCanvasDesktopShell::CaptureScreenArea(40, 40, 5, 5, area, &why) && !why.empty(),
          "an area off the screen fails with a reason: " + why);

    // ----- global shortcuts -----
    UltraCanvasGlobalShortcut shortcut;
    std::atomic<int> pressed{0};
    why.clear();
    Check(shortcut.Start("Ctrl+Alt+Shift+F11", [&pressed]() { ++pressed; }, &why), "a shortcut is bound through the portal " + why);
    Check(WaitUntil([&logged]() { return logged("bind shortcut CTRL+ALT+SHIFT+F11"); }, 5000),
          "with the trigger in the shortcuts specification's spelling");
    Check(WaitUntil([&pressed]() { return pressed.load() >= 2; }, 5000),
          "the portal's Activated reaches the callback, once per press (" + std::to_string(pressed.load()) + ")");
    FinishesWithin([&shortcut]() { shortcut.Stop(); }, 5000, "the shortcut stops within seconds");
    Check(!shortcut.IsRunning() && WaitUntil([&logged]() { return logged("closed "); }, 5000),
          "and its session is closed");
    UltraCanvasGlobalShortcut superTap;
    why.clear();
    Check(superTap.Start("Super", []() {}, &why) && WaitUntil([&logged]() { return logged("bind shortcut LOGO"); }, 5000),
          "the Super key alone is asked for as LOGO " + why);
    FinishesWithin([&superTap]() { superTap.Stop(); }, 5000, "and stops within seconds");
    UltraCanvasGlobalShortcut altAlone;
    Check(!altAlone.Start("Alt", []() {}, &why) && !why.empty(), "another modifier alone is no shortcut: " + why);

    EndChild(fake);
    std::error_code ec;
    fs::remove_all(scratch, ec);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

} // namespace
#endif

#if defined(DESKTOPSHELLTEST_X11)
// A root-window property, set or removed the way a window manager would.
void SetRootCardinal(Display* display, const char* name, long value) {
    XChangeProperty(display, DefaultRootWindow(display), XInternAtom(display, name, False), XA_CARDINAL, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char*>(&value), 1);
    XFlush(display);
}

void DeleteRootProperty(Display* display, const char* name) {
    XDeleteProperty(display, DefaultRootWindow(display), XInternAtom(display, name, False));
    XFlush(display);
}
#endif

#if defined(DESKTOPSHELLTEST_X11) && defined(DESKTOPSHELLTEST_HAVE_GIO)
// ----- the notification area over D-Bus (--tray) -----
// A StatusNotifierItem of the test's own, exported over GDBus on a private
// connection (so closing it is a program that went away), with a dbusmenu
// at /MenuBar. Its calls land on this thread's main context, which the
// waits below pump.

struct FakeTrayItem {
    GDBusConnection* bus = nullptr;
    guint itemObject = 0;
    guint menuObject = 0;
    std::string uniqueName;
    std::string status = "Active";
    std::string tooltipTitle = "Test item";
    std::string tooltipText = "with a description";
    int aboutToShow = 0;
    guint32 revision = 1;
    bool extraEntry = false;                                  // a fourth entry, after LayoutUpdated
    std::vector<std::pair<std::string, std::pair<int, int>>> calls;   // Activate & co. with their point
    std::vector<std::pair<int, std::string>> events;          // dbusmenu Event: id, "clicked"
};
FakeTrayItem g_fake;

constexpr const char* kFakeItemXml =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "    <method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "    <method name='ContextMenu'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "    <signal name='NewStatus'><arg type='s'/></signal>"
    "    <signal name='NewToolTip'/>"
    "  </interface>"
    "</node>";

constexpr const char* kFakeMenuXml =
    "<node>"
    "  <interface name='com.canonical.dbusmenu'>"
    "    <method name='GetLayout'>"
    "      <arg type='i' direction='in'/><arg type='i' direction='in'/><arg type='as' direction='in'/>"
    "      <arg type='u' direction='out'/><arg type='(ia{sv}av)' direction='out'/>"
    "    </method>"
    "    <method name='AboutToShow'><arg type='i' direction='in'/><arg type='b' direction='out'/></method>"
    "    <method name='Event'>"
    "      <arg type='i' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <signal name='LayoutUpdated'><arg type='u'/><arg type='i'/></signal>"
    "  </interface>"
    "</node>";

// 16x16, opaque red, ARGB32 in network byte order as the protocol says.
GVariant* FakePixmaps() {
    static std::vector<unsigned char> pixels;
    if (pixels.empty()) {
        for (int i = 0; i < 16 * 16; ++i) {
            pixels.push_back(0xff);
            pixels.push_back(0xff);
            pixels.push_back(0x00);
            pixels.push_back(0x00);
        }
    }
    GVariantBuilder frames;
    g_variant_builder_init(&frames, G_VARIANT_TYPE("a(iiay)"));
    g_variant_builder_add(&frames, "(ii@ay)", 16, 16,
                          g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, pixels.data(), pixels.size(), 1));
    return g_variant_builder_end(&frames);
}

GVariant* FakeMenuEntry(int id, std::initializer_list<std::pair<const char*, GVariant*>> props) {
    GVariantBuilder p;
    g_variant_builder_init(&p, G_VARIANT_TYPE("a{sv}"));
    for (const auto& [name, value] : props) g_variant_builder_add(&p, "{sv}", name, value);
    GVariantBuilder c;
    g_variant_builder_init(&c, G_VARIANT_TYPE("av"));
    return g_variant_new("(ia{sv}av)", id, &p, &c);
}

GVariant* FakeMenuLayout() {
    GVariantBuilder p;
    g_variant_builder_init(&p, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&p, "{sv}", "children-display", g_variant_new_string("submenu"));
    GVariantBuilder c;
    g_variant_builder_init(&c, G_VARIANT_TYPE("av"));
    g_variant_builder_add(&c, "v", FakeMenuEntry(1, {{"label", g_variant_new_string("_Open")}}));
    g_variant_builder_add(&c, "v", FakeMenuEntry(2, {{"type", g_variant_new_string("separator")}}));
    g_variant_builder_add(&c, "v", FakeMenuEntry(3, {{"label", g_variant_new_string("Mute")},
                                                     {"toggle-type", g_variant_new_string("checkmark")},
                                                     {"toggle-state", g_variant_new_int32(1)}}));
    g_variant_builder_add(&c, "v", FakeMenuEntry(4, {{"label", g_variant_new_string("Hidden")},
                                                     {"visible", g_variant_new_boolean(FALSE)}}));
    if (g_fake.extraEntry) {
        g_variant_builder_add(&c, "v", FakeMenuEntry(5, {{"label", g_variant_new_string("Save __as__")},
                                                         {"enabled", g_variant_new_boolean(FALSE)}}));
    }
    return g_variant_new("(ia{sv}av)", 0, &p, &c);
}

GVariant* OnFakeItemProperty(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* property,
                             GError** error, gpointer) {
    const std::string name = property ? property : "";
    if (name == "Category") return g_variant_new_string("ApplicationStatus");
    if (name == "Id") return g_variant_new_string("TestItem");
    if (name == "Title") return g_variant_new_string("The test's item");
    if (name == "Status") return g_variant_new_string(g_fake.status.c_str());
    if (name == "IconName") return g_variant_new_string("");
    if (name == "IconPixmap") return FakePixmaps();
    if (name == "ToolTip") {
        return g_variant_new("(s@a(iiay)ss)", "", g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0),
                             g_fake.tooltipTitle.c_str(), g_fake.tooltipText.c_str());
    }
    if (name == "ItemIsMenu") return g_variant_new_boolean(FALSE);
    if (name == "Menu") return g_variant_new_object_path("/MenuBar");
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "No such property: %s", name.c_str());
    return nullptr;
}

void OnFakeItemMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                      GVariant* parameters, GDBusMethodInvocation* invocation, gpointer) {
    gint32 x = 0, y = 0;
    g_variant_get(parameters, "(ii)", &x, &y);
    g_fake.calls.emplace_back(method ? method : "", std::make_pair(x, y));
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

void OnFakeMenuMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                      GVariant* parameters, GDBusMethodInvocation* invocation, gpointer) {
    const std::string name = method ? method : "";
    if (name == "GetLayout") {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))", g_fake.revision, FakeMenuLayout()));
    } else if (name == "AboutToShow") {
        ++g_fake.aboutToShow;
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
    } else if (name == "Event") {
        gint32 id = 0;
        const gchar* eventId = nullptr;
        GVariant* data = nullptr;
        guint32 timestamp = 0;
        g_variant_get(parameters, "(i&svu)", &id, &eventId, &data, &timestamp);
        if (data) g_variant_unref(data);
        g_fake.events.emplace_back(id, eventId ? eventId : "");
        g_dbus_method_invocation_return_value(invocation, nullptr);
    } else {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "%s", name.c_str());
    }
}

const GDBusInterfaceVTable kFakeItemVTable = {OnFakeItemMethod, OnFakeItemProperty, nullptr, {nullptr}};
const GDBusInterfaceVTable kFakeMenuVTable = {OnFakeMenuMethod, nullptr, nullptr, {nullptr}};

// Pumps this thread's main context - the fake item's calls - while waiting.
template <typename Done>
bool PumpUntil(Done done, int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
        if (done()) return true;
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool FakeCalled(const std::string& method) {
    for (const auto& call : g_fake.calls) {
        if (call.first == method) return true;
    }
    return false;
}

bool NameHasOwner(GDBusConnection* bus, const char* name) {
    GVariant* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus", "NameHasOwner", g_variant_new("(s)", name),
                                                  G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
    if (!reply) return false;
    gboolean owned = FALSE;
    g_variant_get(reply, "(b)", &owned);
    g_variant_unref(reply);
    return owned != FALSE;
}

const DesktopTrayIcon* FindTestIcon(const std::vector<DesktopTrayIcon>& icons, std::vector<DesktopTrayIcon>& keep) {
    keep = icons;
    for (const DesktopTrayIcon& icon : keep) {
        if (icon.appClass == "TestItem") return &icon;
    }
    return nullptr;
}

int TrayTest() {
    std::printf("Notification area (StatusNotifierWatcher over D-Bus)\n");
    const std::string address = GetEnvUtf8("DBUS_SESSION_BUS_ADDRESS");
    if (address.empty()) {
        std::printf("  (no DBUS_SESSION_BUS_ADDRESS: the notification area's checks are skipped)\n");
        std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
        return 0;
    }
    GError* error = nullptr;
    GDBusConnection* shared = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    Check(shared != nullptr, std::string("the session bus answers ") + (error ? error->message : ""));
    if (error) g_error_free(error);
    if (!shared) {
        std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
        return 1;
    }
    Check(!NameHasOwner(shared, "org.kde.StatusNotifierWatcher"), "no StatusNotifierWatcher on this bus yet");

    UltraCanvasTrayHost host;
    std::atomic<int> changes{0};
    std::string why;
    Check(host.Start([&changes]() { ++changes; }, &why), "the tray host starts " + why);
    Check(NameHasOwner(shared, "org.kde.StatusNotifierWatcher"), "it owns org.kde.StatusNotifierWatcher");
    {
        UltraCanvasTrayHost second;
        std::string refused;
        Check(!second.Start([]() {}, &refused) && !refused.empty(), "a second host is refused: " + refused);
    }

    // The fake item, on a connection of its own.
    gchar* busAddress = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    g_fake.bus = busAddress ? g_dbus_connection_new_for_address_sync(
                                  busAddress,
                                  static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                                                    G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                                  nullptr, nullptr, &error)
                            : nullptr;
    g_free(busAddress);
    Check(g_fake.bus != nullptr, std::string("the fake item has a connection of its own ") + (error ? error->message : ""));
    if (error) g_error_free(error);
    if (g_fake.bus) {
        g_dbus_connection_set_exit_on_close(g_fake.bus, FALSE);
        g_fake.uniqueName = g_dbus_connection_get_unique_name(g_fake.bus);
        GDBusNodeInfo* itemInfo = g_dbus_node_info_new_for_xml(kFakeItemXml, nullptr);
        GDBusNodeInfo* menuInfo = g_dbus_node_info_new_for_xml(kFakeMenuXml, nullptr);
        g_fake.itemObject = g_dbus_connection_register_object(g_fake.bus, "/StatusNotifierItem", itemInfo->interfaces[0],
                                                              &kFakeItemVTable, nullptr, nullptr, nullptr);
        g_fake.menuObject = g_dbus_connection_register_object(g_fake.bus, "/MenuBar", menuInfo->interfaces[0],
                                                              &kFakeMenuVTable, nullptr, nullptr, nullptr);
        Check(g_fake.itemObject != 0 && g_fake.menuObject != 0, "the fake item and its menu are exported");
        // Registered by path alone: the watcher takes the sender's name.
        GVariant* reply = g_dbus_connection_call_sync(
            g_fake.bus, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
            "RegisterStatusNotifierItem", g_variant_new("(s)", "/StatusNotifierItem"), nullptr,
            G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &error);
        Check(reply != nullptr, std::string("RegisterStatusNotifierItem is answered ") + (error ? error->message : ""));
        if (reply) g_variant_unref(reply);
        if (error) g_error_free(error);
        error = nullptr;
        g_dbus_node_info_unref(itemInfo);
        g_dbus_node_info_unref(menuInfo);
    }

    std::vector<DesktopTrayIcon> icons;
    const DesktopTrayIcon* icon = nullptr;
    Check(PumpUntil([&]() { return (icon = FindTestIcon(host.ListIcons(), icons)) != nullptr; }, 5000),
          "the item is listed (" + std::to_string(icons.size()) + " icons)");
    std::string id;
    if (icon) {
        id = icon->id;
        Check(icon->id == g_fake.uniqueName + "/StatusNotifierItem", "its id is the bus name and path: " + icon->id);
        Check(icon->tooltip == "Test item: with a description", "its tooltip is the title and the description: " + icon->tooltip);
        Check(icon->processId == static_cast<int>(getpid()), "with this process's id");
        char signature[8] = {};
        std::FILE* png = icon->iconFile.empty() ? nullptr : OpenFileUtf8(icon->iconFile, "rb");
        if (png) {
            if (std::fread(signature, 1, 8, png) != 8) signature[1] = 0;
            std::fclose(png);
        }
        Check(png && std::string(signature + 1, 3) == "PNG", "its pixmap became a PNG file: " + icon->iconFile);
        Check(icon->balloonSerial == 0 && icon->balloonTitle.empty(), "no balloon (the protocol has none)");
    }
    // The menu: AboutToShow first, then the layout as data.
    Check(PumpUntil([&]() { return (icon = FindTestIcon(host.ListIcons(), icons)) != nullptr && icon->menu.size() == 3; }, 5000),
          "the menu is handed over as data (" + std::to_string(icon ? icon->menu.size() : 0) + " entries)");
    Check(g_fake.aboutToShow == 1, "AboutToShow was sent once before reading it");
    if (icon && icon->menu.size() == 3) {
        const auto& menu = icon->menu;
        Check(menu[0].id == 1 && menu[0].label == "Open" && !menu[0].separator && menu[0].enabled && !menu[0].checkable,
              "entry 1: \"" + menu[0].label + "\", the mnemonic stripped");
        Check(menu[1].id == 2 && menu[1].separator, "entry 2: a separator");
        Check(menu[2].id == 3 && menu[2].label == "Mute" && menu[2].checkable && menu[2].checked, "entry 3: a check mark, on");
    }

    // Clicks reach the program at the pointer's position.
    Check(host.Click(id, TrayClick::Left), "a left click is handed over");
    Check(PumpUntil([]() { return FakeCalled("Activate"); }, 3000), "as Activate");
    Check(host.Click(id, TrayClick::Middle), "a middle click is handed over");
    Check(PumpUntil([]() { return FakeCalled("SecondaryActivate"); }, 3000), "as SecondaryActivate");
    Check(host.Click(id, TrayClick::Right), "a right click is accepted");
    PumpUntil([]() { return false; }, 300);
    Check(!FakeCalled("ContextMenu"), "but not sent on: the desktop draws the menu it was given");
    Check(host.ActivateMenuItem(id, 3), "a menu entry is activated");
    Check(PumpUntil([]() { return !g_fake.events.empty(); }, 3000) && g_fake.events[0].first == 3 &&
              g_fake.events[0].second == "clicked",
          "as the dbusmenu Event \"clicked\"");
    Check(!host.ActivateMenuItem(id, 99), "an entry the menu has not is refused");
    Check(!host.Click("nobody/StatusNotifierItem", TrayClick::Left), "a click on an unknown icon is refused");

    // Changes the program announces.
    if (g_fake.bus) {
        g_fake.status = "Passive";
        g_dbus_connection_emit_signal(g_fake.bus, nullptr, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewStatus",
                                      g_variant_new("(s)", "Passive"), nullptr);
        Check(PumpUntil([&]() { return FindTestIcon(host.ListIcons(), icons) == nullptr; }, 5000), "a passive item is not listed");
        g_fake.status = "Active";
        g_fake.tooltipTitle = "Changed";
        g_fake.tooltipText.clear();
        g_dbus_connection_emit_signal(g_fake.bus, nullptr, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewStatus",
                                      g_variant_new("(s)", "Active"), nullptr);
        Check(PumpUntil([&]() { return (icon = FindTestIcon(host.ListIcons(), icons)) != nullptr && icon->tooltip == "Changed"; }, 5000),
              "listed again when active, with the new tooltip");
        g_fake.extraEntry = true;
        g_fake.revision = 2;
        g_dbus_connection_emit_signal(g_fake.bus, nullptr, "/MenuBar", "com.canonical.dbusmenu", "LayoutUpdated",
                                      g_variant_new("(ui)", 2u, 0), nullptr);
        Check(PumpUntil([&]() { return (icon = FindTestIcon(host.ListIcons(), icons)) != nullptr && icon->menu.size() == 4; }, 5000),
              "LayoutUpdated brings the new entry");
        if (icon && icon->menu.size() == 4) {
            Check(icon->menu[3].label == "Save _as_" && !icon->menu[3].enabled, "a doubled underscore is one, disabled: " + icon->menu[3].label);
        }
        Check(g_fake.aboutToShow == 1, "AboutToShow is not sent again on a re-read");
        Check(changes.load() > 0, "the host reported its changes (" + std::to_string(changes.load()) + ")");

        // The program goes: its connection closes without a goodbye.
        g_dbus_connection_close_sync(g_fake.bus, nullptr, nullptr);
        Check(PumpUntil([&]() { return FindTestIcon(host.ListIcons(), icons) == nullptr; }, 5000),
              "an item whose connection left is dropped");
        g_object_unref(g_fake.bus);
        g_fake.bus = nullptr;
    }

    FinishesWithin([&host]() { host.Stop(); }, 5000, "the host stops within seconds");
    Check(!host.IsRunning(), "and is stopped");
    Check(!NameHasOwner(shared, "org.kde.StatusNotifierWatcher"), "the watcher's name is released");
    g_object_unref(shared);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
#endif

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    // The programs of their own this test starts (see the head comment).
    if (argc == 4 && std::strcmp(argv[1], "--guard-child") == 0) {
        return GuardChild(argv[2], PathFromUtf8(argv[3]));
    }
    if (argc == 2 && std::strcmp(argv[1], "--tray-child") == 0) {
        const int failures = TrayChild();
        std::printf("  [%d checks on the tray's desktop, %d failures]\n", g_checks, failures);
        return failures;
    }
#else
#if defined(DESKTOPSHELLTEST_X11) && defined(DESKTOPSHELLTEST_HAVE_GIO)
    // The notification area, under a session bus of its own (CTest's
    // DesktopShellTrayTest runs it through dbus-run-session).
    if (argc == 2 && std::strcmp(argv[1], "--tray") == 0) return TrayTest();
#endif
#if defined(DESKTOPSHELLTEST_WAYLAND)
    // The windows the Wayland section opens in programs of their own.
    if (argc == 3 && std::strcmp(argv[1], "--wayland-window") == 0) return WaylandWindowChild(argv[2], false);
    if (argc == 3 && std::strcmp(argv[1], "--wayland-desktop") == 0) return WaylandWindowChild(argv[2], true);
    if (argc == 3 && std::strcmp(argv[1], "--wayland-two-windows") == 0) return WaylandTwoWindowsChild(argv[2]);
    if (argc == 3 && std::strcmp(argv[1], "--wayland-lock") == 0) return WaylandLockChild(argv[2]);
    if (argc == 3 && std::strcmp(argv[1], "--wayland-popup") == 0) return WaylandPopupChild(argv[2]);
    if (argc == 3 && std::strcmp(argv[1], "--wayland-panel") == 0) return WaylandPanelChild(argv[2]);
    if (argc == 3 && std::strcmp(argv[1], "--wayland-drag") == 0) return WaylandDragChild(argv[2]);
#if defined(ULTRACANVAS_ENABLE_GL)
    if (argc == 3 && std::strcmp(argv[1], "--wayland-gl") == 0) return WaylandGLChild(argv[2]);
#endif
#if defined(DESKTOPSHELLTEST_HAVE_GIO)
    // The portals, under a session bus of their own (CTest's
    // DesktopShellPortalTest runs it through dbus-run-session), and the
    // fake portal that section starts.
    if (argc == 2 && std::strcmp(argv[1], "--portals") == 0) return PortalTest();
    if (argc == 3 && std::strcmp(argv[1], "--fake-portal") == 0) return FakePortalMain(PathFromUtf8(argv[2]));
#endif
#endif
    (void)argc;
    (void)argv;
#endif
    std::srand(12345);
    const fs::path scratch = MakeScratch();
    // The session's own runtime directory, for the Wayland section at the
    // end: the compositor's socket is there, and the notices' checks above
    // it point the variable at the throwaway directory.
    const char* sessionRuntimeDir = std::getenv("XDG_RUNTIME_DIR");
    const std::string sessionRuntime = sessionRuntimeDir ? sessionRuntimeDir : "";
    (void)sessionRuntime;
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

    // ===== THE LOCK, THE END OF THE SESSION, THE LOUDSPEAKER =====
    // Never locked or ended here - a test must not lock a machine - only
    // asked, and a backend that cannot answer says so instead of crashing.
    {
        if (!UltraCanvasDesktopShell::CanLockSession()) {
            std::string why;
            Check(!UltraCanvasDesktopShell::LockSession(&why) && !why.empty(),
                  "without a locker, LockSession fails with a reason: " + why);
        } else {
            Check(true, "a screen locker is reachable here (not asked to lock)");
        }
        std::printf("  (CanEndSession: %s)\n", UltraCanvasDesktopShell::CanEndSession() ? "yes" : "no");

        const DesktopAudioOutput speaker = UltraCanvasDesktopShell::ReadAudioOutput();
        Check(speaker.available || !speaker.error.empty(),
              speaker.available ? "the loudspeaker is read: " + speaker.device + " at " +
                                      std::to_string(speaker.volumePercent) + "%" + (speaker.muted ? ", muted" : "")
                                : "no loudspeaker here, with the reason: " + speaker.error);
        std::string why;
        if (speaker.available) {
            Check(speaker.volumePercent >= 0, "the volume is a percentage");
            Check(UltraCanvasDesktopShell::SetAudioOutputVolume(speaker.volumePercent, &why),
                  "the same volume is set back " + why);
            Check(UltraCanvasDesktopShell::SetAudioOutputMuted(speaker.muted, &why), "and the same mute " + why);
            const DesktopAudioOutput again = UltraCanvasDesktopShell::ReadAudioOutput();
            Check(again.available && again.muted == speaker.muted &&
                      std::abs(again.volumePercent - speaker.volumePercent) <= 1,
                  "and read back unchanged");
        } else {
            Check(!UltraCanvasDesktopShell::SetAudioOutputVolume(50, &why) && !why.empty(),
                  "without a sound server, setting the volume fails with a reason: " + why);
            why.clear();
            Check(!UltraCanvasDesktopShell::SetAudioOutputMuted(false, &why) && !why.empty(),
                  "and so does muting: " + why);
        }
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

        // The Microsoft Store applications this machine has come from the
        // shell, not from the Start menu built here: set apart.
        std::vector<UCDesktopEntry> apps, store;
        for (const UCDesktopEntry& entry : UltraCanvasDesktopShell::ListApplications(32)) {
            (entry.appUserModelId.empty() ? apps : store).push_back(entry);
        }
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

        // Microsoft Store applications: named, by their AppUserModelID,
        // with no file of their own - however many this machine has.
        size_t withIcon = 0;
        bool wellFormed = true;
        for (const UCDesktopEntry& entry : store) {
            if (entry.name.empty() || entry.appUserModelId.find('!') == std::string::npos ||
                !entry.shortcutPath.empty() || !entry.program.empty())
                wellFormed = false;
            if (!entry.iconFile.empty()) ++withIcon;
        }
        std::printf("  (%zu Microsoft Store applications here, %zu with an icon%s%s)\n", store.size(), withIcon,
                    store.empty() ? "" : ", e.g. ", store.empty() ? "" : store[0].appUserModelId.c_str());
        Check(wellFormed, "Store applications carry a name and their AppUserModelID, and no file");
        if (!store.empty()) {
            std::string why;
            Check(!UltraCanvasDesktopShell::LaunchApplication(store[0], {"C:\\nothing.txt"}, &why) && !why.empty(),
                  "a Store application is not handed files: " + why);
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
        // The screens: the primary first, every one with a size - or none
        // at all where there is no display.
        const auto screens = UltraCanvasDesktopShell::ListScreens();
        bool sized = true;
        for (const DesktopScreenInfo& screen : screens) sized = sized && screen.width > 0 && screen.height > 0;
        int screenWidth = 0, screenHeight = 0;
        const bool haveScreen = UltraCanvasDesktopShell::GetScreenSize(screenWidth, screenHeight);
        Check(sized && (!haveScreen || (!screens.empty() && screens[0].primary)),
              "ListScreens answers, the primary first (" + std::to_string(screens.size()) + " screens)");
#if defined(_WIN32)
        Check(!screens.empty() && screens[0].x == 0 && screens[0].y == 0 && screens[0].width == screenWidth &&
                  screens[0].height == screenHeight,
              "the primary monitor is at 0,0 and is the screen GetScreenSize names (" +
                  std::to_string(screenWidth) + "x" + std::to_string(screenHeight) + ")");
#endif
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
        Check(!UltraCanvasDesktopShell::ReplaceSystemDesktop("relative/UltraDesktop", {}, &error,
                                                             DesktopReplacement::Off),
              "replacing with nothing is refused");
#if !defined(_WIN32)
        Check(!UltraCanvasDesktopShell::CanReplaceSystemDesktop(), "here the session decides which desktop runs");
        Check(!UltraCanvasDesktopShell::CanReplaceSystemDesktop(DesktopReplacement::Instead),
              "beside the system's desktop or instead of it");
        Check(!UltraCanvasDesktopShell::IsSystemDesktopReplaced(), "so nothing is replaced");
        Check(UltraCanvasDesktopShell::GetSystemDesktopReplacement() == DesktopReplacement::Off,
              "and the replacement reads as none");
        Check(UltraCanvasDesktopShell::IsSystemDesktopRunning(), "whatever the session runs is its own desktop");
        // The end of the session is logind's on Linux: a machine with it on
        // the system bus (a CI runner has one) can, and is not asked to - a
        // test must never end its session; one without says why it cannot.
        if (UltraCanvasDesktopShell::CanEndSession()) {
            Check(true, "logind answers here, so the session can be ended (not asked to)");
        } else {
            std::string why;
            Check(!UltraCanvasDesktopShell::EndSession(SessionEnd::SignOut, &why) && !why.empty(),
                  "without logind, ending the session fails with a reason: " + why);
        }
        UltraCanvasTrayHost tray;
        std::string trayWhy;
        Check(!tray.Start([]() {}, &trayWhy) && !trayWhy.empty() && !tray.IsRunning(),
              "no notification area to host here: " + trayWhy);
        Check(tray.ListIcons().empty() && !tray.Click("tray-1", TrayClick::Left), "and so no icons to click");
#endif
    }

#if defined(DESKTOPSHELLTEST_X11)
    // ===== X11: THE BACKEND ON A REAL DISPLAY =====
    {
        const char* required = std::getenv("ULTRACANVAS_TEST_DISPLAY_REQUIRED");
        const bool displayRequired = required && std::strcmp(required, "1") == 0;
        Display* display = UltraCanvasDesktopShell::IsAvailable() ? XOpenDisplay(nullptr) : nullptr;
        if (!display) {
            Check(!displayRequired, "a display was required (ULTRACANVAS_TEST_DISPLAY_REQUIRED=1), and there is none");
            if (!displayRequired) std::printf("  (no X display: the X11 backend's own checks are skipped)\n");
        } else {
            Check(UltraCanvasDesktopShell::GetBackendName() == "x11", "the backend is x11");

            UltraCanvasDesktopShellMonitor monitor;
            std::atomic<int> changes{0};
            Check(monitor.Start([&changes]() { ++changes; }) && monitor.IsNative(),
                  "the monitor watches on a connection of its own");
            // No window manager here: the test sets the desktops as one would.
            SetRootCardinal(display, "_NET_NUMBER_OF_DESKTOPS", 3);
            SetRootCardinal(display, "_NET_CURRENT_DESKTOP", 1);
            Check(WaitUntil([&changes]() { return changes.load() > 0; }, 5000),
                  "the monitor reports the root window's change");
            const int desktops = UltraCanvasDesktopShell::GetVirtualDesktopCount();
            const int current = UltraCanvasDesktopShell::GetCurrentVirtualDesktop();
            Check(desktops == 3 && current == 1, "the virtual desktops are read back (" + std::to_string(current + 1) +
                                                     " of " + std::to_string(desktops) + ")");
            FinishesWithin([&monitor]() { monitor.Stop(); }, 5000, "the monitor stops within seconds");
            Check(!monitor.IsRunning(), "and is stopped");

            UltraCanvasGlobalShortcut shortcut;
            std::string why;
            Check(shortcut.Start("Ctrl+Alt+Shift+F11", []() {}, &why), "a free combination is grabbed " + why);
            FinishesWithin([&shortcut]() { shortcut.Stop(); }, 5000, "the shortcut stops within seconds");
            Check(!shortcut.IsRunning(), "and is stopped");
            // The Super key alone: watched through XInput2's raw events, so
            // nothing is grabbed and every Super+X combination stays its
            // owner's. Xvfb has XInput2; a server without it says so.
            std::string tap;
            UltraCanvasGlobalShortcut superTap;
            const bool tapWatched = superTap.Start("Super", []() {}, &tap);
            Check(tapWatched || tap.find("XInput") != std::string::npos,
                  "the Super key alone is watched on X11 " + tap);
            if (tapWatched) {
                Check(superTap.IsRunning(), "and the watch runs");
                FinishesWithin([&superTap]() { superTap.Stop(); }, 5000, "the watch stops within seconds");
                Check(!superTap.IsRunning(), "and is stopped");
            }
            std::string altAlone;
            Check(!shortcut.Start("Alt", []() {}, &altAlone) && !altAlone.empty(),
                  "another modifier alone is no shortcut: " + altAlone);

            int rootWidth = 0, rootHeight = 0;
            UltraCanvasDesktopShell::GetScreenSize(rootWidth, rootHeight);
            const auto screens = UltraCanvasDesktopShell::ListScreens();
            bool onRoot = !screens.empty();
            for (const DesktopScreenInfo& screen : screens) {
                onRoot = onRoot && screen.x >= 0 && screen.y >= 0 && screen.x + screen.width <= rootWidth &&
                         screen.y + screen.height <= rootHeight;
            }
            Check(onRoot && screens[0].primary, "every monitor lies on the root window (" + std::to_string(rootWidth) +
                                                    "x" + std::to_string(rootHeight) + "), the primary first: " +
                                                    std::to_string(screens.size()) + " screens");
            DesktopScreenImage shot;
            std::string captureError;
            Check(UltraCanvasDesktopShell::CaptureScreenImage(shot, &captureError) && shot.width == rootWidth &&
                      shot.height == rootHeight,
                  "the screen is captured at the root window's size " + captureError);

            // ===== TWO MONITORS (RandR 1.5 virtual monitors) =====
            // A left one and a right, primary, one that takes the screen's
            // output (so the server's automatic whole-screen monitor goes):
            // the list names both, the primary first; a desktop window on
            // the right monitor reserves its right bar on the root's right
            // edge, along its own height, and nothing on the left, which is
            // another monitor's edge.
            if (rootWidth >= 640 && rootHeight >= 480) {
                const Window root = DefaultRootWindow(display);
                const int leftWidth = rootWidth / 2;
                const int rightWidth = rootWidth - leftWidth;
                const int rightHeight = rootHeight - 40;   // shorter: a dead area under it
                XRRScreenResources* resources = XRRGetScreenResources(display, root);
                RROutput output = resources && resources->noutput > 0 ? resources->outputs[0] : 0;
                const Atom leftName = XInternAtom(display, "UltraCanvasTestLeft", False);
                const Atom rightName = XInternAtom(display, "UltraCanvasTestRight", False);
                XRRMonitorInfo left = {};
                left.name = leftName;
                left.x = 0;
                left.y = 0;
                left.width = leftWidth;
                left.height = rootHeight;
                left.mwidth = leftWidth / 4;
                left.mheight = rootHeight / 4;
                XRRMonitorInfo right = {};
                right.name = rightName;
                right.primary = True;
                right.x = leftWidth;
                right.y = 0;
                right.width = rightWidth;
                right.height = rightHeight;
                right.mwidth = rightWidth / 4;
                right.mheight = rightHeight / 4;
                right.noutput = output ? 1 : 0;
                right.outputs = output ? &output : nullptr;
                XRRSetMonitor(display, root, &left);
                XRRSetMonitor(display, root, &right);
                XSync(display, False);

                const auto two = UltraCanvasDesktopShell::ListScreens();
                Check(two.size() == 2 && two[0].primary && two[0].x == leftWidth && two[0].width == rightWidth &&
                          two[0].height == rightHeight && !two[1].primary && two[1].x == 0 && two[1].width == leftWidth,
                      "two monitors are listed, the primary (right) first, the whole-screen one left out: " +
                          std::to_string(two.size()) + " screens");

                const Window onRight = XCreateSimpleWindow(display, root, leftWidth, 0,
                                                           static_cast<unsigned>(rightWidth),
                                                           static_cast<unsigned>(rightHeight), 0, 0, 0);
                XMapWindow(display, onRight);
                XSync(display, False);
                Check(UltraCanvasDesktopShell::ReserveScreenEdges(static_cast<uint64_t>(onRight), 52, 48, 0, 0),
                      "a window on the right monitor reserves its edges");
                Atom type = 0;
                int format = 0;
                unsigned long items = 0, after = 0;
                unsigned char* data = nullptr;
                std::vector<long> strut;
                if (XGetWindowProperty(display, onRight, XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False), 0, 12,
                                       False, XA_CARDINAL, &type, &format, &items, &after, &data) == Success && data) {
                    const long* values = reinterpret_cast<const long*>(data);
                    strut.assign(values, values + items);
                    XFree(data);
                }
                Check(strut.size() == 12 && strut[0] == 0 && strut[1] == 48 && strut[6] == 0 &&
                          strut[7] == rightHeight - 1,
                      "the right bar is reserved along the window's own height and the left edge - another "
                      "monitor's - is not: " + (strut.size() == 12 ? std::to_string(strut[0]) + "," +
                                                                        std::to_string(strut[1]) + " / " +
                                                                        std::to_string(strut[6]) + ".." +
                                                                        std::to_string(strut[7])
                                                                  : std::string("no strut")));
                XDestroyWindow(display, onRight);
                XRRDeleteMonitor(display, root, rightName);
                XRRDeleteMonitor(display, root, leftName);
                XSync(display, False);
                if (resources) XRRFreeScreenResources(resources);
                Check(UltraCanvasDesktopShell::ListScreens().size() == 1, "the monitors are gone again");
            }

            DeleteRootProperty(display, "_NET_CURRENT_DESKTOP");
            DeleteRootProperty(display, "_NET_NUMBER_OF_DESKTOPS");
            XCloseDisplay(display);
        }
    }
#endif

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

        // The Win key alone: a tap of it, with no other key in between.
        std::string altAlone;
        Check(!shortcut.Start("Alt", []() {}, &altAlone) && !altAlone.empty(),
              "another modifier alone is no shortcut: " + altAlone);
        UltraCanvasGlobalShortcut winKey;
        std::atomic<int> taps{0};
        std::string tapWhy;
        Check(winKey.Start("Super", [&taps]() { ++taps; }, &tapWhy), "the Win key alone can be watched " + tapWhy);
        const char* ciTap = std::getenv("CI");   // path-string-ok: ASCII flag
        if (ciTap && std::string(ciTap) == "true") {
            // Typed, on a CI runner only: Explorer opens its Start menu on
            // the tap, which Escape closes again.
            const auto keys = [](std::initializer_list<std::pair<WORD, bool>> sequence) {
                std::vector<INPUT> inputs;
                for (const auto& [vk, up] : sequence) {
                    INPUT input = {};
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = vk;
                    input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (vk == VK_LWIN ? KEYEVENTF_EXTENDEDKEY : 0);
                    inputs.push_back(input);
                }
                SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
            };
            PumpUntil([]() { return false; }, 500);   // the hook goes in on the shortcut's thread
            keys({{VK_LWIN, false}, {VK_LSHIFT, false}, {VK_LSHIFT, true}, {VK_LWIN, true}});
            PumpUntil([]() { return false; }, 500);
            Check(taps.load() == 0, "Win with another key is no tap");
            keys({{VK_LWIN, false}, {VK_LWIN, true}});
            Check(PumpUntil([&taps]() { return taps.load() > 0; }, 3000), "a tap of the Win key is reported");
            keys({{VK_ESCAPE, false}, {VK_ESCAPE, true}});
            PumpUntil([]() { return false; }, 500);
        } else {
            std::printf("  (Win key tap: not typed outside a CI runner)\n");
        }
        winKey.Stop();

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

        // ===== WINDOWS: INSTEAD OF EXPLORER, SAFELY ANYWHERE =====
        const std::wstring self = ThisProgram();
        const std::wstring guard = FolderOf(self) + L"\\UltraCanvasShellGuard.exe";
        const bool haveGuard = GetFileAttributesW(guard.c_str()) != INVALID_FILE_ATTRIBUTES;
        Check(haveGuard, "UltraCanvasShellGuard.exe is built beside the test");
        Check(UltraCanvasDesktopShell::CanReplaceSystemDesktop(DesktopReplacement::Instead),
              "Windows can run a desktop instead of Explorer");
        Check(UltraCanvasDesktopShell::CanEndSession(), "and end the session from it");
        {
            // A program with no guard beside it is never made the shell: the
            // switch is refused before anything is written.
            const fs::path lonely = scratch / "lonely" / "Desktop.exe";
            WriteFile(lonely, "MZ");
            std::string why;
            Check(!UltraCanvasDesktopShell::ReplaceSystemDesktop(PathToUtf8(lonely), {}, &why,
                                                                 DesktopReplacement::Instead) &&
                      why.find("UltraCanvasShellGuard") != std::string::npos,
                  "without the guard beside it, no desktop replaces Explorer: " + why);
        }
        if (haveGuard) {
            // The guard, not as the session's shell: it starts the program,
            // starts it again after a crash, and gives up after the third
            // failure within a minute - starting nothing else.
            const fs::path counter = scratch / "guard-starts.txt";
            const auto runGuard = [&](const char* mode) {
                std::error_code ec;
                fs::remove(counter, ec);
                const std::wstring command = Quoted(guard) + L" " + Quoted(self) + L" --guard-child " +
                                             PathUtf8Detail::Utf8ToUtf16<std::wstring>(mode) + L" " +
                                             Quoted(counter.native());
                return RunToEnd(guard, command, 60000);
            };
            int code = runGuard("ok");
            Check(code == 0 && ReadCount(counter) == 1, "the guard starts the desktop once when it exits cleanly (exit " +
                                                            std::to_string(code) + ", " +
                                                            std::to_string(ReadCount(counter)) + " start)");
            code = runGuard("crash-once");
            Check(code == 0 && ReadCount(counter) == 2,
                  "starts it again after a crash (exit " + std::to_string(code) + ", " +
                      std::to_string(ReadCount(counter)) + " starts)");
            code = runGuard("crash");
            Check(code == 3 && ReadCount(counter) == 3,
                  "and gives up after three failures within a minute (exit " + std::to_string(code) + ", " +
                      std::to_string(ReadCount(counter)) + " starts)");
            Check(RunToEnd(guard, Quoted(guard), 30000) == 2, "a guard given no desktop says so (exit 2)");
        }
        {
            // The notification area: Explorer's taskbar holds it here when it
            // runs, so a host is refused - and is tested on a desktop of its
            // own, through this program started there.
            if (UltraCanvasDesktopShell::IsSystemDesktopRunning()) {
                UltraCanvasTrayHost tray;
                std::string refused;
                Check(!tray.Start([]() {}, &refused) && !refused.empty(),
                      "beside Explorer's taskbar the tray host is refused: " + refused);
            }
            const std::wstring name = L"UltraCanvasTrayTest" + std::to_wstring(GetCurrentProcessId());
            HDESK desk = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
            if (!desk) {
                const char* ciRunner = std::getenv("CI");   // path-string-ok: ASCII flag
                const bool onCi = ciRunner && std::string(ciRunner) == "true";
                const std::string reason = "no desktop of its own for the tray test (error " +
                                           std::to_string(GetLastError()) + ")";
                if (onCi) Check(false, reason);
                else std::printf("  (%s)\n", reason.c_str());
            } else {
                const std::wstring where = L"WinSta0\\" + name;
                const int failures = RunToEnd(self, Quoted(self) + L" --tray-child", 120000, where.c_str());
                Check(failures == 0, "the notification area passes on its own desktop (" +
                                         std::to_string(failures) + " failures)");
                CloseDesktop(desk);
            }
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
            const std::string program = PathUtf8Detail::Utf16ToUtf8(self);
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

            // Instead of Explorer: the account's shell setting names the
            // guard, then this program with its arguments.
            const wchar_t* winlogon = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
            const auto readShell = [winlogon]() {
                wchar_t value[2048] = {};
                DWORD bytes = sizeof(value);
                if (RegGetValueW(HKEY_CURRENT_USER, winlogon, L"Shell", RRF_RT_REG_SZ, nullptr, value, &bytes) !=
                    ERROR_SUCCESS)
                    return std::wstring();
                return std::wstring(value);
            };
            const std::wstring shellBefore = readShell();
            Check(shellBefore.empty(), "the runner's account has no shell of its own: " +
                                           PathUtf8Detail::Utf16ToUtf8(shellBefore));
            if (shellBefore.empty() && haveGuard) {
                Check(UltraCanvasDesktopShell::ReplaceSystemDesktop(program, {"--shell"}, &why,
                                                                   DesktopReplacement::Instead),
                      "the desktop replaces Explorer " + why);
                Check(UltraCanvasDesktopShell::GetSystemDesktopReplacement() == DesktopReplacement::Instead,
                      "and reads back as instead of Explorer");
                const std::wstring expectedShell = Quoted(guard) + L" " + Quoted(self) + L" --shell";
                Check(readShell() == expectedShell,
                      "the shell setting starts the guard, then the desktop: " + PathUtf8Detail::Utf16ToUtf8(readShell()));
                DWORD runBytes = 0;
                Check(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                   L"UltraCanvas Desktop", RRF_RT_REG_SZ, nullptr, nullptr, &runBytes) != ERROR_SUCCESS,
                      "and nothing starts beside Explorer as well");
                Check(UltraCanvasDesktopShell::ReplaceSystemDesktop(program, {"--session"}, &why,
                                                                   DesktopReplacement::Beside) &&
                          UltraCanvasDesktopShell::GetSystemDesktopReplacement() == DesktopReplacement::Beside &&
                          readShell().empty(),
                      "beside Explorer again: the shell setting is gone " + why);
                Check(UltraCanvasDesktopShell::ReplaceSystemDesktop(program, {"--shell"}, &why,
                                                                   DesktopReplacement::Instead) &&
                          UltraCanvasDesktopShell::RestoreSystemDesktop(&why),
                      "instead once more, and restored " + why);
                Check(UltraCanvasDesktopShell::GetSystemDesktopReplacement() == DesktopReplacement::Off &&
                          readShell().empty(),
                      "Explorer is the account's desktop again");
                if (explorer) {
                    Check(PumpUntil([&]() {
                              return static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &bar)) == taskbarBefore;
                          }, 3000),
                          "and its taskbar is as it was");
                }
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

#if defined(DESKTOPSHELLTEST_WAYLAND)
    // ===== WAYLAND: THE BACKEND ON A COMPOSITOR =====
    {
        if (!sessionRuntime.empty()) SetEnv("XDG_RUNTIME_DIR", sessionRuntime);
        const char* required = std::getenv("ULTRACANVAS_TEST_DISPLAY_REQUIRED");
        const bool displayRequired = required && std::strcmp(required, "1") == 0;
        const bool available = std::getenv("WAYLAND_DISPLAY") && UltraCanvasDesktopShell::IsAvailable();
        if (!available) {
            Check(!displayRequired, "a display was required (ULTRACANVAS_TEST_DISPLAY_REQUIRED=1), and there is none");
            if (!displayRequired) std::printf("  (no Wayland display: the Wayland backend's own checks are skipped)\n");
        } else {
            Check(UltraCanvasDesktopShell::GetBackendName() == "wayland", "the backend is wayland");

            // The window list and the monitor need the compositor's
            // foreign-toplevel manager (every wlroots compositor, KWin);
            // the monitor says whether there is one.
            UltraCanvasDesktopShellMonitor monitor;
            std::atomic<int> changes{0};
            Check(monitor.Start([&changes]() { ++changes; }), "the monitor starts");
            if (!monitor.IsNative()) {
                std::printf("  (no foreign-toplevel manager here: the window list's checks are skipped)\n");
            } else {
                Check(true, "the monitor watches the compositor's foreign toplevels on a connection of its own");
                const std::string title = "DesktopShellTest Fenster \xc3\x84\xc3\x96\xc3\x9c " + std::to_string(std::rand());
                const pid_t child = SpawnSelf({"--wayland-window", title}, scratch / "window.out");
                Check(child > 0, "a window of this test's own opens in a program of its own");
                uint64_t id = 0;
                DesktopWindowInfo listed;
                auto find = [&]() {
                    for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
                        if (w.title == title) {
                            listed = w;
                            id = w.id;
                            return true;
                        }
                    }
                    return false;
                };
                Check(WaitUntil(find, 10000), "the window is listed with its UTF-8 title");
                Check(listed.appClass == "DesktopShellTest", "and its app id, the program's name: " + listed.appClass);
                Check(WaitUntil([&changes]() { return changes.load() > 0; }, 5000), "the monitor reported it");
                Check(WaitUntil([id]() { return UltraCanvasDesktopShell::GetActiveWindow() == id; }, 5000),
                      "a new window is the active one");
                Check(UltraCanvasDesktopShell::MinimizeWindow(id), "the compositor is asked to minimize it");
                // sway has no minimized state and leaves the window as it
                // is; labwc and KWin report it minimized.
                const bool minimized = WaitUntil([&]() { return find() && listed.minimized; }, 2000);
                std::printf("  (minimized: %s)\n", minimized ? "reported" : "not reported by this compositor");
                Check(UltraCanvasDesktopShell::ActivateWindow(id), "and to activate it again");
                Check(WaitUntil([&]() { return find() && listed.active && !listed.minimized; }, 5000),
                      "it is active and not minimized");
                const int before = changes.load();
                Check(UltraCanvasDesktopShell::CloseWindow(id), "and to close it");
                Check(WaitUntil([&]() { return !find(); }, 5000), "it is gone from the list");
                Check(WaitUntil([&changes, before]() { return changes.load() > before; }, 5000),
                      "the monitor reported that too");
                const int exit = WaitChild(child, 5000);
                Check(exit == 0, "the program ended when its window was closed (exit " + std::to_string(exit) + ")");
                if (exit < 0) EndChild(child);

                // ----- the desktop on the layer shell -----
                int outX = 0, outY = 0, outW = 0, outH = 0;
                int areaX = 0, areaY = 0, areaW = 0, areaH = 0;
                const bool sway = SwayRect("get_outputs", outX, outY, outW, outH) &&
                                  SwayRect("get_workspaces", areaX, areaY, areaW, areaH);
                const std::string desktopTitle = "DesktopShellTest Desktop " + std::to_string(std::rand());
                const pid_t desktop = SpawnSelf({"--wayland-desktop", desktopTitle}, scratch / "desktop.out");
                Check(desktop > 0, "a Desktop window opens in a program of its own");
                std::string reserved;
                Check(WaitUntil([&]() {
                          std::ifstream in(scratch / "desktop.out");
                          std::getline(in, reserved);
                          return reserved == "reserved=1" || reserved == "reserved=0";
                      }, 10000) && reserved == "reserved=1",
                      "the backend reserved its edges on the layer shell: " + reserved);
                if (sway) {
                    int x = 0, y = 0, w = 0, h = 0;
                    Check(WaitUntil([&]() {
                              return SwayRect("get_workspaces", x, y, w, h) && y == outY + 40 && w == outW - 60 &&
                                     h == outH - 40;
                          }, 5000),
                          "sway's workspace makes room for them: 40 at the top, 60 on the right (" +
                              std::to_string(w) + "x" + std::to_string(h) + " at " + std::to_string(x) + "," +
                              std::to_string(y) + " of " + std::to_string(outW) + "x" + std::to_string(outH) + ")");
                } else {
                    std::printf("  (not sway: the workspace's room for the edges is not read back)\n");
                }
                // A layer surface is no toplevel: the desktop is not in the
                // list a taskbar shows.
                bool desktopListed = false;
                for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
                    if (w.title == desktopTitle) desktopListed = true;
                }
                Check(!desktopListed, "the desktop window is not in the window list");
                EndChild(desktop);
                if (sway) {
                    int x = 0, y = 0, w = 0, h = 0;
                    Check(WaitUntil([&]() {
                              return SwayRect("get_workspaces", x, y, w, h) && y == areaY && w == areaW && h == areaH;
                          }, 5000),
                          "and the workspace is whole again when the desktop is gone");
                }

                // ----- the focus through xdg-activation -----
                // The compositor decides: sway follows an activation request
                // with focus_on_window_activation focus (the test's
                // configuration), marks the window urgent by default.
                const std::string pair = "DesktopShellTest Paar " + std::to_string(std::rand());
                const pid_t two = SpawnSelf({"--wayland-two-windows", pair}, scratch / "two.out");
                Check(two > 0, "two windows open in a program of their own");
                uint64_t idA = 0, idB = 0;
                auto findPair = [&]() {
                    idA = idB = 0;
                    for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
                        if (w.title == pair + " A") idA = w.id;
                        if (w.title == pair + " B") idB = w.id;
                    }
                    return idA != 0 && idB != 0;
                };
                Check(WaitUntil(findPair, 10000), "both are listed");
                Check(WaitUntil([&]() { return UltraCanvasDesktopShell::GetActiveWindow() == idB; }, 5000),
                      "the second one opened is the active one");
                // The token carries the serial of the last input the program
                // had; a seat without a keyboard (sway headless with no
                // input device) gave it none, and sway then marks the window
                // urgent instead of focusing it. With a keyboard on the seat
                // (wtype's virtual one, DESKTOPSHELLTEST_WAYLAND_KEYBOARD=1)
                // the focus has to move.
                const char* keyboard = std::getenv("DESKTOPSHELLTEST_WAYLAND_KEYBOARD");
                const bool focused = WaitUntil([&]() { return UltraCanvasDesktopShell::GetActiveWindow() == idA; }, 6000);
                if (focused || (keyboard && std::strcmp(keyboard, "1") == 0)) {
                    Check(focused, "RaiseAndFocus makes the first one active (xdg-activation)");
                } else {
                    std::printf("  (RaiseAndFocus: the compositor kept the focus - no keyboard on the seat, so no serial for the token)\n");
                }
                UltraCanvasDesktopShell::CloseWindow(idA);
                UltraCanvasDesktopShell::CloseWindow(idB);
                Check(WaitChild(two, 5000) == 0, "and the program ends when both are closed");
                EndChild(two);
            }

            // ----- the virtual desktops -----
            // ext-workspace where the compositor speaks it (sway 1.11,
            // labwc 0.9, KWin); one desktop where it does not (sway 1.9).
            const int desktops = UltraCanvasDesktopShell::GetVirtualDesktopCount();
            const int current = UltraCanvasDesktopShell::GetCurrentVirtualDesktop();
            Check(desktops >= 1 && current >= 0 && current < desktops,
                  "the virtual desktops are read: " + std::to_string(current + 1) + " of " + std::to_string(desktops));
            const bool switched = UltraCanvasDesktopShell::SetCurrentVirtualDesktop(current);
            std::printf("  (ext-workspace: %s)\n", switched ? "the compositor has it, the current desktop was activated again"
                                                              : "not on this compositor, one desktop");
            Check(!UltraCanvasDesktopShell::SetCurrentVirtualDesktop(desktops + 5), "a desktop past the last is refused");

            // ----- the session lock -----
            {
                const pid_t lock = SpawnSelf({"--wayland-lock", "DesktopShellTest Lock"}, scratch / "lock.out");
                const std::string verdict = FirstLine(scratch / "lock.out", 15000, "locked=");
                const int exit = WaitChild(lock, 10000);
                if (exit < 0) EndChild(lock);
                Check(verdict == "locked=1" && exit == 0,
                      "a SessionLock window locks the session and closing it unlocks (" + verdict + ", exit " +
                          std::to_string(exit) + ")");
            }

            // ----- a popup placed by its toplevel parent -----
            {
                const pid_t child = SpawnSelf({"--wayland-popup", "DesktopShellTest Popup"}, scratch / "popup.out");
                const std::string verdict = FirstLine(scratch / "popup.out", 15000, "popup=");
                const int exit = WaitChild(child, 10000);
                if (exit < 0) EndChild(child);
                Check(verdict == "popup=40,30 100x80" && exit == 0,
                      "a child of a toplevel is an xdg_popup placed and sized as asked (" + verdict + ")");
            }

            // ----- a panel placed by its layer-shell desktop -----
            {
                const pid_t child = SpawnSelf({"--wayland-panel", "DesktopShellTest Panel"}, scratch / "panel.out");
                const std::string verdict = FirstLine(scratch / "panel.out", 15000, "panel=");
                const int exit = WaitChild(child, 10000);
                if (exit < 0) EndChild(child);
                Check(verdict == "panel=300x200" && exit == 0,
                      "a child of a layer-shell desktop is a panel on the layer shell at its size (" + verdict + ")");
            }

            // ----- a file drag -----
            {
                const pid_t child = SpawnSelf({"--wayland-drag", "DesktopShellTest Drag"}, scratch / "drag.out");
                const std::string verdict = FirstLine(scratch / "drag.out", 15000, "drag=");
                const int exit = WaitChild(child, 10000);
                if (exit < 0) EndChild(child);
                // Without a button held the compositor cannot take a drag:
                // a seat without devices gives no serial, a serial not from
                // a press is answered with a cancel.
                Check((verdict == "drag=no-serial" || verdict == "drag=cancelled" || verdict == "drag=accepted") && exit == 0,
                      "a file drag is started, or refused with the reason (" + verdict + ")");
            }

#if defined(ULTRACANVAS_ENABLE_GL)
            // ----- a GL surface -----
            {
                const pid_t child = SpawnSelf({"--wayland-gl", "DesktopShellTest GL"}, scratch / "gl.out");
                const std::string verdict = FirstLine(scratch / "gl.out", 15000, "gl=");
                const int exit = WaitChild(child, 10000);
                if (exit < 0) EndChild(child);
                Check(verdict.rfind("gl=", 0) == 0 && verdict != "gl=none" && exit == 0,
                      "a GL surface gets a context from EGL on Wayland and draws (" + verdict + ")");
            }
#endif
            FinishesWithin([&monitor]() { monitor.Stop(); }, 5000, "the monitor stops within seconds");
            Check(!monitor.IsRunning(), "and is stopped");

            const auto screens = UltraCanvasDesktopShell::ListScreens();
            Check(!screens.empty() && screens[0].primary, "the outputs are the screens, the first the primary: " +
                                                              std::to_string(screens.size()));
            // The portals need a session bus with xdg-desktop-portal on it
            // (DesktopShellPortalTest drives them against a fake one); here
            // either a screenshot arrives or the reason does.
            std::string why;
            const bool shotTaken = UltraCanvasDesktopShell::CaptureScreen((scratch / "shot.png").string(), &why);   // path-string-ok: ASCII temp path
            Check(shotTaken ? fs::exists(scratch / "shot.png") : !why.empty(),
                  shotTaken ? "a screenshot through the session's portal" : "no screenshot portal here, and the reason: " + why);
        }
    }
#endif

    std::error_code ec;
    fs::remove_all(scratch, ec);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
