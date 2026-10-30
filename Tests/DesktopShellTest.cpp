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
// Version: 1.8.0 - two RandR monitors under Xvfb: ListScreens and the per-monitor struts;
//                  the lock, session and loudspeaker calls
// Version: 1.7.0 - the Linux notification area (--tray)
// Version: 1.6.0 - the X11 backend on a real display; Stop() within seconds
// Version: 1.5.0 - ListScreens, Microsoft Store applications, the Win key's tap
// Version: 1.4.0 - instead of Explorer, the shell guard, the tray host
// Version: 1.3.0 - ReplaceSystemDesktop / RestoreSystemDesktop
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShell.h"

#include <atomic>
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

// The platforms whose backend is X11 (UltraCanvasDesktopShellBackend.h).
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
#  define DESKTOPSHELLTEST_X11 1
#  include <X11/Xlib.h>
#  include <X11/Xatom.h>
#  include <X11/extensions/Xrandr.h>
#  include <unistd.h>
#  if defined(DESKTOPSHELLTEST_HAVE_GIO)
#    include <gio/gio.h>
#  endif
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

#if defined(DESKTOPSHELLTEST_X11)
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
    (void)argc;
    (void)argv;
#endif
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
        std::string why;
        Check(!UltraCanvasDesktopShell::CanEndSession() &&
                  !UltraCanvasDesktopShell::EndSession(SessionEnd::SignOut, &why) && !why.empty(),
              "ending the session is the session manager's here: " + why);
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

    std::error_code ec;
    fs::remove_all(scratch, ec);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
