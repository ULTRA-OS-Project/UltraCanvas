// OS/MSWindows/UltraCanvasWindowsDesktopShell.cpp
// The Win32 backend of UltraCanvasDesktopShell - what the desktop shell
// module answers on Windows, whether Explorer runs beside UltraDesktop or
// UltraDesktop is the shell:
//
//   - Windows: EnumWindows, filtered the way Explorer's taskbar filters
//     (visible, unowned or WS_EX_APPWINDOW, not a tool window). appClass is
//     the executable's name ("firefox", "UltraFiler"), the closest Windows
//     has to WM_CLASS; for a Microsoft Store application it is the program
//     behind the ApplicationFrameHost frame. The icon is the executable's,
//     extracted once into a PNG cache.
//   - Virtual desktops: their order and the current one as Explorer records
//     them in the registry (VirtualDesktopIDs, CurrentVirtualDesktop - read
//     by tools such as PowerToys, since Windows has no documented call for
//     either), a window's desktop through IVirtualDesktopManager. Windows
//     moves the user between desktops only on its own shortcut, so switching
//     sends Ctrl+Win+Left/Right; adding or removing desktops, and moving
//     another program's window, have no call at all and answer false.
//   - Screen edges: an appbar per reserved edge (SHAppBarMessage) while
//     Explorer's taskbar runs, and the work area itself (SPI_SETWORKAREA)
//     when UltraDesktop is the shell and nobody else keeps it.
//   - Screenshot: BitBlt of the whole virtual screen into a 32-bit DIB, whose
//     rows are already the B, G, R, unused layout DesktopScreenImage holds.
//   - Devices: the camera from the consent store Windows' own privacy
//     indicator reads, microphone and speaker from the Core Audio sessions
//     that are running, the battery from GetSystemPowerStatus, the keyboard
//     layout from the foreground window's thread.
//   - Monitor: WinEvent hooks (shown, hidden, foreground, minimized, cloaked)
//     and a registry notification for the desktop count, on the monitor's
//     own thread, which pumps its messages in MsgWaitForMultipleObjectsEx.
//   - Global shortcut: RegisterHotKey on the shortcut's thread. Explorer
//     keeps most Win-key combinations for itself (Win+V among them), so
//     those are reported as taken while it runs.
//   - Applications: the Start menu's shortcuts, the user's and the
//     machine's, named as the Start menu names them (desktop.ini) and
//     launched by opening the shortcut, as Explorer does.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

// IVirtualDesktopManager, DWMWA_CLOAKED and the cloaking events are declared
// for Windows 10 headers. Raise the floor before any Windows header is pulled
// in, keeping NTDDI_VERSION consistent with it as the SDK derives it; nothing
// below is linked that an older Windows lacks - the calls answer "no" there.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif

#include "UltraCanvasDesktopShellBackend.h"
#include "UltraCanvasDiskCache.h"
#include "UltraCanvasNativeFileIcons.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasShellLink.h"
#include "UltraCanvasWindowsIcons.h"

#include <windows.h>
#include <audiopolicy.h>
#include <dwmapi.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlwapi.h>    // SHLoadIndirectString
#include <shobjidl.h>   // IVirtualDesktopManager

#include <cairo/cairo.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {

    // ===== STRINGS, HANDLES, COM =====

    std::wstring Widen(const std::string& utf8) {
        return PathUtf8Detail::Utf8ToUtf16<std::wstring>(utf8);
    }

    std::string Narrow(const std::wstring& wide) {
        return PathUtf8Detail::Utf16ToUtf8(wide);
    }

    HWND ToHwnd(uint64_t id) {
        return reinterpret_cast<HWND>(static_cast<uintptr_t>(id));
    }

    uint64_t ToId(HWND hwnd) {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd));
    }

    std::wstring LowerCase(std::wstring text) {
        for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
        return text;
    }

    std::string LowerAscii(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    // "C:\Program Files\App\App.exe" -> "App.exe" / "App", on the wide
    // string itself: no path object, no code page.
    std::wstring FileNameOf(const std::wstring& path) {
        const size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? path : path.substr(slash + 1);
    }

    std::wstring StemOf(const std::wstring& path) {
        const std::wstring name = FileNameOf(path);
        const size_t dot = name.find_last_of(L'.');
        return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
    }

    std::wstring ExtensionOf(const std::wstring& path) {
        const std::wstring name = FileNameOf(path);
        const size_t dot = name.find_last_of(L'.');
        return dot == std::wstring::npos ? std::wstring() : LowerCase(name.substr(dot));
    }

    // COM for the length of one call. The UI thread is already in an
    // apartment (RPC_E_CHANGED_MODE, or S_FALSE): fine for everything here,
    // and only a join this scope made is balanced.
    struct ComScope {
        bool joined = false;
        ComScope() {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            joined = (hr == S_OK || hr == S_FALSE);
        }
        ~ComScope() {
            if (joined) CoUninitialize();
        }
        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;
    };

    template <typename T>
    void Release(T*& object) {
        if (object) object->Release();
        object = nullptr;
    }

    // Not every SDK carries the class ids below in its uuid library; they are
    // the documented values.
    const CLSID kClsidVirtualDesktopManager =
        {0xAA509086, 0x5CA9, 0x4C25, {0x8F, 0x95, 0x58, 0x9D, 0x3C, 0x07, 0xB4, 0x8A}};
    const CLSID kClsidMMDeviceEnumerator =
        {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};

    // ===== THE PNG CACHE FOR ICONS =====
    // The taskbar and the app starter draw image files. An executable's or a
    // shortcut's icon is extracted once into the "desktop-icons" folder of
    // the disk cache (%LOCALAPPDATA%\UltraCanvas) and kept there, stamped on
    // every use and swept by the shared disk-cache rule, so an uninstalled
    // program's PNG goes in time.

    std::string HashKey(const std::string& text) {
        uint64_t hash = 1469598103934665603ull;          // FNV-1a, 64 bit
        for (unsigned char c : text) {
            hash ^= static_cast<uint64_t>(std::tolower(c));
            hash *= 1099511628211ull;
        }
        char buffer[17] = {};
        std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));
        return buffer;
    }

    std::string IconCacheDirectory() {
        const std::string dir = DiskCache::Directory("desktop-icons");
        if (dir.empty()) return dir;
        static std::once_flag sweepOnce;
        std::call_once(sweepOnce, [dir]() { DiskCache::Sweep(dir, {".png", ".tmp"}); });
        return dir;
    }

    // The PNG for `key`, made by `make` when it is not cached yet; "" when
    // there is no cache or nothing to draw.
    template <typename Make>
    std::string CachedIconFile(const std::string& key, Make make) {
        const std::string dir = IconCacheDirectory();
        if (dir.empty()) return {};
        const std::string path = PathToUtf8(PathFromUtf8(dir) / PathFromUtf8(HashKey(key) + ".png"));
        std::error_code ec;
        if (fs::exists(PathFromUtf8(path), ec)) {
            DiskCache::Touch(path);
            return path;
        }
        const std::shared_ptr<UCPixmap> pixmap = make();
        if (!pixmap || !pixmap->IsValid()) return {};
        return WindowsIcons::WriteSurfacePng(pixmap->GetSurface(), path) ? path : std::string();
    }

    // The icon of an executable, by its path. Remembered for the process as
    // well, since the window list asks for the same few programs on every
    // change.
    std::string ExecutableIconFile(const std::wstring& exePath) {
        if (exePath.empty()) return {};
        static std::mutex mutex;
        static std::map<std::wstring, std::string> known;
        const std::wstring key = LowerCase(exePath);
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = known.find(key);
            if (it != known.end()) return it->second;
        }
        const std::string file = CachedIconFile("exe|" + Narrow(key) + "|48", [&exePath]() {
            return WindowsIcons::LoadIconResourcePixmap(exePath, 0, 48);
        });
        std::lock_guard<std::mutex> lock(mutex);
        known[key] = file;
        return file;
    }

    // ===== ONE WINDOW =====

    // The title as the window manager keeps it. InternalGetWindowText never
    // sends WM_GETTEXT, so a hung program - or this program's own UI thread,
    // when the list is read from another - cannot block the caller.
    std::string WindowTitle(HWND hwnd) {
        wchar_t buffer[512];
        const int n = InternalGetWindowText(hwnd, buffer, static_cast<int>(std::size(buffer)));
        return n > 0 ? Narrow(std::wstring(buffer, static_cast<size_t>(n))) : std::string();
    }

    std::wstring WindowClassName(HWND hwnd) {
        wchar_t buffer[256];
        const int n = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
        return n > 0 ? std::wstring(buffer, static_cast<size_t>(n)) : std::wstring();
    }

    // The executable a process runs. PROCESS_QUERY_LIMITED_INFORMATION is
    // granted for elevated processes too; only protected ones refuse.
    std::wstring ProcessImagePath(DWORD pid) {
        if (pid == 0) return {};
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};
        wchar_t buffer[MAX_PATH * 4];
        DWORD size = static_cast<DWORD>(std::size(buffer));
        std::wstring path;
        if (QueryFullProcessImageNameW(process, 0, buffer, &size)) path.assign(buffer, size);
        CloseHandle(process);
        return path;
    }

    struct HostedSearch {
        DWORD framePid = 0;
        DWORD found = 0;
    };

    BOOL CALLBACK FindHostedProcess(HWND child, LPARAM param) {
        auto* search = reinterpret_cast<HostedSearch*>(param);
        DWORD pid = 0;
        GetWindowThreadProcessId(child, &pid);
        if (pid != 0 && pid != search->framePid) {
            search->found = pid;
            return FALSE;
        }
        return TRUE;
    }

    // A Microsoft Store application's window is a frame ApplicationFrameHost
    // draws; the application is the process behind the core window inside.
    DWORD HostedApplicationProcess(HWND frame, DWORD framePid) {
        HostedSearch search;
        search.framePid = framePid;
        EnumChildWindows(frame, FindHostedProcess, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

    // What Explorer's taskbar shows: a window that asks to be there
    // (WS_EX_APPWINDOW), else one that is no tool window, takes activation
    // and has no owner.
    bool BelongsInTaskbar(HWND hwnd, LONG_PTR exStyle) {
        if (exStyle & WS_EX_APPWINDOW) return true;
        if (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return false;
        return GetWindow(hwnd, GW_OWNER) == nullptr;
    }

    // The shell's own windows: the desktop, its wallpaper layer, the
    // taskbars. Listed, but never taskbar material.
    bool IsShellWindow(HWND hwnd, const std::wstring& className) {
        return hwnd == GetShellWindow() || className == L"Progman" || className == L"WorkerW" ||
               className == L"Shell_TrayWnd" || className == L"Shell_SecondaryTrayWnd";
    }

    DWORD CloakedState(HWND hwnd) {
        DWORD cloaked = 0;
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) return 0;
        return cloaked;
    }

    // ===== VIRTUAL DESKTOPS =====
    // Windows documents no call for the list of desktops or for the current
    // one. Explorer persists both in the registry - the list in order as
    // VirtualDesktopIDs (16-byte GUIDs back to back), the current one as
    // CurrentVirtualDesktop beside it on Windows 11 and under the session's
    // own key on Windows 10 - and that is what is read here, as PowerToys
    // does. Neither value exists until a second desktop was ever made, which
    // is read as the one desktop there is.

    constexpr wchar_t kExplorerKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer";
    constexpr wchar_t kVirtualDesktopsKey[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";

    bool ReadBinaryValue(const std::wstring& subKey, const wchar_t* name, std::vector<BYTE>& out) {
        DWORD size = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, subKey.c_str(), name, RRF_RT_REG_BINARY, nullptr, nullptr, &size) !=
                ERROR_SUCCESS || size == 0)
            return false;
        out.resize(size);
        if (RegGetValueW(HKEY_CURRENT_USER, subKey.c_str(), name, RRF_RT_REG_BINARY, nullptr, out.data(), &size) !=
                ERROR_SUCCESS)
            return false;
        out.resize(size);
        return true;
    }

    struct VirtualDesktopLayout {
        std::vector<GUID> ids;      // in Task View's order; empty = one desktop
        int current = 0;

        int Count() const { return ids.empty() ? 1 : static_cast<int>(ids.size()); }
        int IndexOf(const GUID& id) const {
            for (size_t i = 0; i < ids.size(); ++i) {
                if (IsEqualGUID(ids[i], id)) return static_cast<int>(i);
            }
            return -1;
        }
    };

    VirtualDesktopLayout ReadVirtualDesktops() {
        VirtualDesktopLayout layout;
        std::vector<BYTE> bytes;
        if (ReadBinaryValue(kVirtualDesktopsKey, L"VirtualDesktopIDs", bytes)) {
            for (size_t at = 0; at + sizeof(GUID) <= bytes.size(); at += sizeof(GUID)) {
                GUID id;
                std::memcpy(&id, bytes.data() + at, sizeof(GUID));
                layout.ids.push_back(id);
            }
        }
        if (layout.ids.empty()) return layout;

        bool found = ReadBinaryValue(kVirtualDesktopsKey, L"CurrentVirtualDesktop", bytes) &&
                     bytes.size() >= sizeof(GUID);
        if (!found) {
            DWORD session = 0;
            if (ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
                const std::wstring sessionKey = std::wstring(kExplorerKey) + L"\\SessionInfo\\" +
                                                std::to_wstring(session) + L"\\VirtualDesktops";
                found = ReadBinaryValue(sessionKey, L"CurrentVirtualDesktop", bytes) &&
                        bytes.size() >= sizeof(GUID);
            }
        }
        if (found) {
            GUID current;
            std::memcpy(&current, bytes.data(), sizeof(GUID));
            layout.current = std::max(0, layout.IndexOf(current));
        }
        return layout;
    }

    IVirtualDesktopManager* CreateVirtualDesktopManager() {
        IVirtualDesktopManager* manager = nullptr;
        if (FAILED(CoCreateInstance(kClsidVirtualDesktopManager, nullptr, CLSCTX_ALL,
                                    __uuidof(IVirtualDesktopManager), reinterpret_cast<void**>(&manager))))
            return nullptr;
        return manager;
    }

    // Keys pressed and let go again as one injected sequence: Windows' own
    // desktop-switch shortcut is the only way it offers to switch.
    bool SendShortcut(std::initializer_list<WORD> modifiers, WORD key) {
        std::vector<INPUT> inputs;
        auto add = [&inputs](WORD vk, bool up) {
            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = vk;
            input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
            if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP ||
                vk == VK_DOWN)
                input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            inputs.push_back(input);
        };
        for (WORD modifier : modifiers) add(modifier, false);
        add(key, false);
        add(key, true);
        for (auto it = std::rbegin(modifiers); it != std::rend(modifiers); ++it) add(*it, true);
        return SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
    }

    bool ExplorerTaskbarRunning() {
        return FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
    }

    // ===== SCREEN EDGES =====
    // While Explorer's taskbar runs, the work area is Explorer's to compute:
    // every reserved edge becomes an appbar - one per edge, since an appbar
    // has one - on a window of its own, invisible and click-through, which
    // Explorer keeps informed and which keeps its strip when Explorer moves
    // its own bars. Without Explorer (UltraDesktop is the shell) nobody keeps
    // the work area, and it is set directly.

    constexpr wchar_t kEdgeWindowClass[] = L"UltraCanvasDesktopShellEdge";
    constexpr UINT kAppBarCallback = WM_APP + 0x5A;

    struct EdgeStrips {
        int left = 0, right = 0, top = 0, bottom = 0;
        bool explorer = false;       // appbars (true) or the work area itself
        std::vector<HWND> appBars;
    };

    std::mutex g_edgeMutex;
    std::map<uint64_t, EdgeStrips> g_edges;     // by the window that reserved them

    RECT PrimaryMonitorRect() {
        RECT rc = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        return rc;
    }

    // Claim the appbar's strip: propose the whole edge, let Explorer move
    // the proposal inside the bars already there, then take the thickness.
    void PlaceAppBar(HWND bar) {
        const LONG_PTR packed = GetWindowLongPtrW(bar, GWLP_USERDATA);
        const UINT edge = static_cast<UINT>(packed & 3);
        const int thickness = static_cast<int>(packed >> 2);
        APPBARDATA data = {};
        data.cbSize = sizeof(data);
        data.hWnd = bar;
        data.uEdge = edge;
        data.rc = PrimaryMonitorRect();
        SHAppBarMessage(ABM_QUERYPOS, &data);
        switch (edge) {
            case ABE_LEFT:   data.rc.right = data.rc.left + thickness; break;
            case ABE_RIGHT:  data.rc.left = data.rc.right - thickness; break;
            case ABE_TOP:    data.rc.bottom = data.rc.top + thickness; break;
            default:         data.rc.top = data.rc.bottom - thickness; break;
        }
        SHAppBarMessage(ABM_SETPOS, &data);
        SetWindowPos(bar, nullptr, data.rc.left, data.rc.top, data.rc.right - data.rc.left,
                     data.rc.bottom - data.rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    LRESULT CALLBACK EdgeWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case kAppBarCallback:
                // Another bar, or Explorer's taskbar, moved: claim again.
                if (wParam == ABN_POSCHANGED) PlaceAppBar(hwnd);
                return 0;
            case WM_DESTROY: {
                APPBARDATA data = {};
                data.cbSize = sizeof(data);
                data.hWnd = hwnd;
                SHAppBarMessage(ABM_REMOVE, &data);
                return 0;
            }
            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    HINSTANCE ThisModule() {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&EdgeWindowProc), &module);
        return module;
    }

    bool RegisterEdgeWindowClass() {
        static const bool registered = []() {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = EdgeWindowProc;
            wc.hInstance = ThisModule();
            wc.lpszClassName = kEdgeWindowClass;
            return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }();
        return registered;
    }

    // Owned by the window that reserved the edge, so it goes - and its
    // reservation with it (WM_DESTROY) - when that window does.
    HWND CreateAppBar(HWND owner, UINT edge, int thickness) {
        if (!RegisterEdgeWindowClass()) return nullptr;
        // Shown, so that nothing about it reads as a hidden bar, yet fully
        // transparent and click-through: the strip is drawn by the owner.
        HWND bar = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                                   kEdgeWindowClass, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, ThisModule(),
                                   nullptr);
        if (!bar) return nullptr;
        SetLayeredWindowAttributes(bar, 0, 0, LWA_ALPHA);
        SetWindowLongPtrW(bar, GWLP_USERDATA, static_cast<LONG_PTR>(thickness) << 2 | static_cast<LONG_PTR>(edge));
        APPBARDATA data = {};
        data.cbSize = sizeof(data);
        data.hWnd = bar;
        data.uCallbackMessage = kAppBarCallback;
        if (!SHAppBarMessage(ABM_NEW, &data)) {
            DestroyWindow(bar);
            return nullptr;
        }
        PlaceAppBar(bar);
        ShowWindow(bar, SW_SHOWNOACTIVATE);
        return bar;
    }

    // Destroyed on the thread that made it; from any other, through its own
    // message loop (WM_CLOSE -> DestroyWindow -> WM_DESTROY -> ABM_REMOVE).
    void DestroyAppBar(HWND bar) {
        if (!IsWindow(bar)) return;
        if (GetWindowThreadProcessId(bar, nullptr) == GetCurrentThreadId()) DestroyWindow(bar);
        else PostMessageW(bar, WM_CLOSE, 0, 0);
    }

    // Without Explorer: the primary monitor less the widest strip any window
    // reserved on each edge. Caller holds g_edgeMutex.
    void ApplyWorkAreaLocked() {
        int left = 0, right = 0, top = 0, bottom = 0;
        for (const auto& [id, strips] : g_edges) {
            if (strips.explorer) continue;
            left = std::max(left, strips.left);
            right = std::max(right, strips.right);
            top = std::max(top, strips.top);
            bottom = std::max(bottom, strips.bottom);
        }
        RECT area = PrimaryMonitorRect();
        area.left += left;
        area.right -= right;
        area.top += top;
        area.bottom -= bottom;
        if (area.right <= area.left || area.bottom <= area.top) return;
        SystemParametersInfoW(SPI_SETWORKAREA, 0, &area, SPIF_SENDCHANGE);
    }

    // ===== DEVICES =====

    // Whether any program has `capability` ("webcam", "microphone") in use,
    // as Windows' privacy indicator knows it: each program that used the
    // device has a key under the capability's consent store with
    // LastUsedTimeStart and LastUsedTimeStop, and a start with no stop after
    // it is a use that has not ended. Desktop programs sit one level deeper,
    // under NonPackaged. Windows 10 1903 and later; `known` false before.
    bool KeyShowsUseInProgress(HKEY key) {
        ULONGLONG start = 0, stop = 0;
        DWORD size = sizeof(start);
        if (RegGetValueW(key, nullptr, L"LastUsedTimeStart", RRF_RT_REG_QWORD, nullptr, &start, &size) !=
            ERROR_SUCCESS)
            return false;
        size = sizeof(stop);
        if (RegGetValueW(key, nullptr, L"LastUsedTimeStop", RRF_RT_REG_QWORD, nullptr, &stop, &size) !=
            ERROR_SUCCESS)
            return false;
        return start != 0 && stop == 0;
    }

    bool AnySubkeyInUse(HKEY parent, bool descendIntoNonPackaged) {
        wchar_t name[512];
        for (DWORD index = 0;; ++index) {
            DWORD length = static_cast<DWORD>(std::size(name));
            const LSTATUS status = RegEnumKeyExW(parent, index, name, &length, nullptr, nullptr, nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) return false;
            if (status != ERROR_SUCCESS) continue;
            HKEY child = nullptr;
            if (RegOpenKeyExW(parent, name, 0, KEY_READ, &child) != ERROR_SUCCESS) continue;
            const bool inUse = (descendIntoNonPackaged && _wcsicmp(name, L"NonPackaged") == 0)
                                   ? AnySubkeyInUse(child, false)
                                   : KeyShowsUseInProgress(child);
            RegCloseKey(child);
            if (inUse) return true;
        }
    }

    bool CapabilityInUse(const wchar_t* capability, bool& known) {
        const std::wstring path =
            std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\") +
            capability;
        bool inUse = false;
        for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
            HKEY key = nullptr;
            if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
            known = true;
            if (AnySubkeyInUse(key, true)) inUse = true;
            RegCloseKey(key);
            if (inUse) break;
        }
        return inUse;
    }

    // Whether a stream runs on any active endpoint of `flow`: the
    // counterpart of an ALSA substream in state RUNNING.
    bool AudioSessionRunning(IMMDeviceEnumerator* enumerator, EDataFlow flow) {
        IMMDeviceCollection* devices = nullptr;
        if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices))) return false;
        bool running = false;
        UINT deviceCount = 0;
        devices->GetCount(&deviceCount);
        for (UINT d = 0; d < deviceCount && !running; ++d) {
            IMMDevice* device = nullptr;
            IAudioSessionManager2* manager = nullptr;
            IAudioSessionEnumerator* sessions = nullptr;
            if (SUCCEEDED(devices->Item(d, &device)) &&
                SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void**>(&manager))) &&
                SUCCEEDED(manager->GetSessionEnumerator(&sessions))) {
                int sessionCount = 0;
                sessions->GetCount(&sessionCount);
                for (int s = 0; s < sessionCount && !running; ++s) {
                    IAudioSessionControl* control = nullptr;
                    AudioSessionState state = AudioSessionStateInactive;
                    if (SUCCEEDED(sessions->GetSession(s, &control)) && SUCCEEDED(control->GetState(&state)) &&
                        state == AudioSessionStateActive)
                        running = true;
                    Release(control);
                }
            }
            Release(sessions);
            Release(manager);
            Release(device);
        }
        Release(devices);
        return running;
    }

    void ProbeAudio(DesktopDeviceActivity& activity) {
        IMMDeviceEnumerator* enumerator = nullptr;
        if (FAILED(CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(&enumerator)))) {
            activity.warnings.push_back("Windows audio is not available, so the microphone and speaker "
                                        "state is unknown.");
            return;
        }
        activity.microphoneInUse = AudioSessionRunning(enumerator, eCapture);
        activity.speakerPlaying = AudioSessionRunning(enumerator, eRender);
        Release(enumerator);
    }

    void ProbeCamera(DesktopDeviceActivity& activity) {
        bool known = false;
        activity.webcamInUse = CapabilityInUse(L"webcam", known);
        if (!known) {
            activity.warnings.push_back("This Windows does not record camera use, so the webcam state is "
                                        "unknown.");
        }
    }

    void ProbeBattery(DesktopDeviceActivity& activity) {
        SYSTEM_POWER_STATUS power = {};
        if (!GetSystemPowerStatus(&power)) return;
        // 128: no system battery; 255: the status is unknown.
        if (power.BatteryFlag == 255 || (power.BatteryFlag & 128)) return;
        activity.batteryPresent = true;
        if (power.BatteryLifePercent <= 100) activity.batteryPercent = power.BatteryLifePercent;
        // Charging, or on the mains and full - the "Full" a Linux battery
        // reports, shown the same way.
        activity.batteryCharging = (power.BatteryFlag & 8) != 0 ||
                                   (power.ACLineStatus == 1 && activity.batteryPercent >= 100);
    }

    // The layout of the window being typed into, named the way X11's layouts
    // are named: the lower-case country of its input locale ("us", "de").
    void ProbeKeyboardLayout(DesktopDeviceActivity& activity) {
        const HWND foreground = GetForegroundWindow();
        const DWORD thread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
        const HKL layout = GetKeyboardLayout(thread);
        const LANGID language = LOWORD(reinterpret_cast<uintptr_t>(layout));
        if (language == 0) return;
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
        if (LCIDToLocaleName(MAKELCID(language, SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0) <= 0) return;
        wchar_t country[16] = {};
        if (GetLocaleInfoEx(locale, LOCALE_SISO3166CTRYNAME, country, static_cast<int>(std::size(country))) <= 0)
            return;
        activity.keyboardLayout = Narrow(LowerCase(country));
    }

    // ===== THE START MENU =====

    // A localized string a shortcut or desktop.ini names indirectly
    // ("@%SystemRoot%\system32\shell32.dll,-22563"); the text itself when it
    // is not one, "" when it cannot be resolved.
    std::wstring ResolveIndirectString(const std::wstring& text) {
        if (text.empty() || text[0] != L'@') return text;
        wchar_t expanded[1024] = {};
        const DWORD n = ExpandEnvironmentStringsW(text.c_str(), expanded, static_cast<DWORD>(std::size(expanded)));
        const std::wstring source = (n > 0 && n <= std::size(expanded)) ? std::wstring(expanded) : text;
        wchar_t buffer[512] = {};
        if (FAILED(SHLoadIndirectString(source.c_str(), buffer, static_cast<UINT>(std::size(buffer)), nullptr)))
            return {};
        return buffer;
    }

    // The name the Start menu shows for a shortcut: its folder's desktop.ini
    // may give it a localized one ("Editor" for Notepad.lnk in German),
    // otherwise it is the file's name.
    std::string ShortcutDisplayName(const fs::path& link) {
        const fs::path ini = link.parent_path() / L"desktop.ini";
        std::error_code ec;
        if (fs::is_regular_file(ini, ec)) {
            wchar_t value[512] = {};
            const std::wstring fileName = FileNameOf(link.native());
            GetPrivateProfileStringW(L"LocalizedFileNames", fileName.c_str(), L"", value,
                                     static_cast<DWORD>(std::size(value)), ini.c_str());
            const std::wstring localized = ResolveIndirectString(value);
            if (!localized.empty()) return Narrow(localized);
        }
        return PathToUtf8(link.stem());
    }

    // An application, as far as a launcher is concerned: a shortcut to a
    // program, or one that names no plain file at all (an installer's
    // advertised shortcut, which the shell resolves). A shortcut to a help
    // file, a web page or a folder is not, and neither is an uninstaller.
    bool IsApplicationShortcut(const UCShellLink& link) {
        if (link.targetIsDirectory) return false;
        if (link.targetPath.empty()) return true;
        const std::wstring target = Widen(link.targetPath);
        static const std::set<std::wstring> programs = {L".exe", L".com", L".bat", L".cmd", L".msc", L".cpl"};
        if (!programs.count(ExtensionOf(target))) return false;
        const std::wstring stem = LowerCase(StemOf(target));
        return stem.rfind(L"unins", 0) != 0 && stem.find(L"uninstall") == std::wstring::npos;
    }

    std::vector<fs::path> StartMenuDirectories() {
        std::vector<fs::path> dirs;
        // The user's first, so that a user's shortcut wins over the
        // machine's one of the same name, as the Start menu shows it.
        for (const char* root : {"APPDATA", "ProgramData"}) {
            const std::string base = GetEnvUtf8(root);
            if (base.empty()) continue;
            dirs.push_back(PathFromUtf8(base) / "Microsoft" / "Windows" / "Start Menu" / "Programs");
        }
        return dirs;
    }

    // ===== THE MONITOR =====

    struct WindowsMonitorState {
        HANDLE wake = nullptr;              // auto-reset: MonitorWake
        HANDLE registryChanged = nullptr;   // auto-reset: the desktops' registry key
        HKEY watchedKey = nullptr;
        bool watchingParent = false;        // the VirtualDesktops key does not exist yet
        std::vector<HWINEVENTHOOK> hooks;
        bool started = false;
        bool changed = false;
    };

    // The hooks run on the thread that set them, inside its message pump, so
    // the state they report into is that thread's.
    thread_local WindowsMonitorState* t_monitor = nullptr;

    void CALLBACK OnWindowEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
        WindowsMonitorState* state = t_monitor;
        if (!state || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
        // Child controls appear and vanish all the time; only top-level
        // windows change the list.
        if (event != EVENT_SYSTEM_FOREGROUND && GetAncestor(hwnd, GA_PARENT) != GetDesktopWindow()) return;
        state->changed = true;
    }

    // (Re)arm the notification for the desktops' registry key. Before a
    // second desktop was ever made the key does not exist: watch its parent
    // for the key appearing, then move to the key itself.
    void ArmRegistryWatch(WindowsMonitorState* state) {
        if (!state->registryChanged) return;
        if (state->watchedKey && state->watchingParent) {
            RegCloseKey(state->watchedKey);
            state->watchedKey = nullptr;
        }
        if (!state->watchedKey) {
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kVirtualDesktopsKey, 0, KEY_NOTIFY, &state->watchedKey) ==
                ERROR_SUCCESS) {
                state->watchingParent = false;
            } else if (RegOpenKeyExW(HKEY_CURRENT_USER, kExplorerKey, 0, KEY_NOTIFY, &state->watchedKey) ==
                       ERROR_SUCCESS) {
                state->watchingParent = true;
            } else {
                state->watchedKey = nullptr;
                return;
            }
        }
        RegNotifyChangeKeyValue(state->watchedKey, FALSE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                                state->registryChanged, TRUE);
    }

    void StartWatching(WindowsMonitorState* state) {
        t_monitor = state;
        static const DWORD events[] = {
            EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
            // A switch of virtual desktop cloaks the windows of the one left
            // and uncloaks those of the one entered.
            EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED,
        };
        for (DWORD event : events) {
            HWINEVENTHOOK hook = SetWinEventHook(event, event, nullptr, OnWindowEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
            if (hook) state->hooks.push_back(hook);
        }
        ArmRegistryWatch(state);
        state->started = true;
    }

    // On the thread that set the hooks: UnhookWinEvent must be called there.
    void StopWatching(WindowsMonitorState* state) {
        for (HWINEVENTHOOK hook : state->hooks) UnhookWinEvent(hook);
        state->hooks.clear();
        if (t_monitor == state) t_monitor = nullptr;
    }

    // ===== THE GLOBAL SHORTCUT =====

    std::atomic<int> g_nextHotKeyId{0x5C00};

    // A key name in the spelling accelerators use - "V", "F5", "Space",
    // "Return", "Left", "Print" - as a virtual-key code; 0 when unknown.
    UINT VirtualKeyFor(const std::string& key) {
        const std::string name = LowerAscii(key);
        if (name.size() == 1) {
            const unsigned char c = static_cast<unsigned char>(key[0]);
            if (std::isalpha(c)) return static_cast<UINT>(std::toupper(c));
            if (std::isdigit(c)) return c;
            // Punctuation lies wherever the keyboard layout puts it.
            const SHORT scan = VkKeyScanW(static_cast<wchar_t>(c));
            return scan == -1 ? 0 : static_cast<UINT>(LOBYTE(scan));
        }
        if (name.size() >= 2 && name[0] == 'f' &&
            std::all_of(name.begin() + 1, name.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            const int number = std::atoi(name.c_str() + 1);
            if (number >= 1 && number <= 24) return static_cast<UINT>(VK_F1 + number - 1);
        }
        static const std::map<std::string, UINT> named = {
            {"space", VK_SPACE}, {"return", VK_RETURN}, {"enter", VK_RETURN}, {"tab", VK_TAB},
            {"escape", VK_ESCAPE}, {"esc", VK_ESCAPE}, {"backspace", VK_BACK}, {"delete", VK_DELETE},
            {"del", VK_DELETE}, {"insert", VK_INSERT}, {"home", VK_HOME}, {"end", VK_END},
            {"prior", VK_PRIOR}, {"pageup", VK_PRIOR}, {"next", VK_NEXT}, {"pagedown", VK_NEXT},
            {"left", VK_LEFT}, {"right", VK_RIGHT}, {"up", VK_UP}, {"down", VK_DOWN},
            {"print", VK_SNAPSHOT}, {"printscreen", VK_SNAPSHOT}, {"pause", VK_PAUSE},
        };
        auto it = named.find(name);
        return it == named.end() ? 0 : it->second;
    }

} // namespace

// ===== BACKEND =====

std::string BackendName() { return "win32"; }

bool IsAvailable() {
    // An interactive window station: a service or a locked-down job has none.
    return GetDesktopWindow() != nullptr && GetSystemMetrics(SM_CXSCREEN) > 0;
}

// ===== WINDOWS =====

namespace {
    BOOL CALLBACK CollectTopLevelWindow(HWND hwnd, LPARAM param) {
        reinterpret_cast<std::vector<HWND>*>(param)->push_back(hwnd);
        return TRUE;
    }

    struct ProcessIdentity {
        std::string appClass;   // "firefox"
        std::string iconFile;
    };

    // The program behind a window: its executable's name and icon. For a
    // Store application's frame, the program inside it.
    ProcessIdentity IdentifyProcess(HWND hwnd, DWORD pid, bool frame) {
        ProcessIdentity identity;
        std::wstring exe = ProcessImagePath(pid);
        if (frame) {
            const DWORD hosted = HostedApplicationProcess(hwnd, pid);
            if (hosted != 0) exe = ProcessImagePath(hosted);
        }
        if (exe.empty()) return identity;
        identity.appClass = Narrow(StemOf(exe));
        identity.iconFile = ExecutableIconFile(exe);
        return identity;
    }
} // namespace

std::vector<DesktopWindowInfo> ListWindows() {
    std::vector<HWND> handles;   // top of the stack first
    EnumWindows(CollectTopLevelWindow, reinterpret_cast<LPARAM>(&handles));

    ComScope com;
    IVirtualDesktopManager* desktops = CreateVirtualDesktopManager();
    const VirtualDesktopLayout layout = ReadVirtualDesktops();
    const HWND foreground = GetForegroundWindow();
    std::map<DWORD, ProcessIdentity> processes;   // asked once per process per call

    std::vector<DesktopWindowInfo> windows;
    windows.reserve(handles.size());
    for (auto it = handles.rbegin(); it != handles.rend(); ++it) {   // bottom first, as the contract says
        const HWND hwnd = *it;
        if (!IsWindowVisible(hwnd)) continue;

        DesktopWindowInfo info;
        info.id = ToId(hwnd);
        info.virtualDesktop = layout.ids.empty() ? 0 : -1;
        bool onCurrentDesktop = true;
        if (desktops && !layout.ids.empty()) {
            GUID desktopId = {};
            if (SUCCEEDED(desktops->GetWindowDesktopId(hwnd, &desktopId))) {
                // A window shown on every desktop carries an id that is none
                // of them, and reads as -1.
                info.virtualDesktop = layout.IndexOf(desktopId);
                onCurrentDesktop = info.virtualDesktop < 0 || info.virtualDesktop == layout.current;
            }
        }
        // Cloaked: on another virtual desktop - still listed, with its
        // desktop - or kept invisible by its program or the shell (a Store
        // application's frame that is not running), which is not a window
        // anybody sees.
        if (CloakedState(hwnd) != 0 && onCurrentDesktop) continue;

        const std::wstring className = WindowClassName(hwnd);
        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        info.title = WindowTitle(hwnd);
        info.skipTaskbar = IsShellWindow(hwnd, className) || !BelongsInTaskbar(hwnd, exStyle);
        info.minimized = IsIconic(hwnd) != FALSE;
        info.active = hwnd == foreground;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        info.processId = static_cast<int>(pid);
        // One ApplicationFrameHost draws the frames of every Store
        // application, so a frame is identified on its own, never by process.
        ProcessIdentity identity;
        if (className == L"ApplicationFrameWindow") {
            identity = IdentifyProcess(hwnd, pid, true);
        } else {
            auto known = processes.find(pid);
            if (known == processes.end()) known = processes.emplace(pid, IdentifyProcess(hwnd, pid, false)).first;
            identity = known->second;
        }
        info.appClass = identity.appClass;
        info.iconFile = identity.iconFile;
        // WM_CLASS carries an instance name beside the class; the program's
        // name in lower case is what X11 programs put there.
        info.appName = LowerAscii(info.appClass);
        windows.push_back(std::move(info));
    }
    Release(desktops);
    return windows;
}

uint64_t GetActiveWindow() {
    return ToId(GetForegroundWindow());
}

bool ActivateWindow(uint64_t id) {
    const HWND hwnd = ToHwnd(id);
    if (id == 0 || !IsWindow(hwnd)) return false;
    // Asynchronous, so a hung program cannot hang the taskbar. A window on
    // another virtual desktop takes Windows there when it comes forward.
    if (IsIconic(hwnd)) ShowWindowAsync(hwnd, SW_RESTORE);
    if (SetForegroundWindow(hwnd)) return true;
    // Windows hands the foreground only to the program the user last gave
    // input to - which a taskbar click is, a call from elsewhere may not be.
    // A key event is input: an Alt press and release lifts the lock without
    // typing anything.
    INPUT alt[2] = {};
    alt[0].type = INPUT_KEYBOARD;
    alt[0].ki.wVk = VK_MENU;
    alt[1].type = INPUT_KEYBOARD;
    alt[1].ki.wVk = VK_MENU;
    alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, alt, sizeof(INPUT));
    return SetForegroundWindow(hwnd) != FALSE;
}

bool MinimizeWindow(uint64_t id) {
    const HWND hwnd = ToHwnd(id);
    if (id == 0 || !IsWindow(hwnd)) return false;
    return ShowWindowAsync(hwnd, SW_MINIMIZE) != FALSE;
}

bool CloseWindow(uint64_t id) {
    const HWND hwnd = ToHwnd(id);
    if (id == 0 || !IsWindow(hwnd)) return false;
    // What the taskbar's "Close window" sends: the program may ask to save,
    // or refuse.
    return PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0) != FALSE;
}

// ===== VIRTUAL DESKTOPS =====

int GetVirtualDesktopCount() {
    return ReadVirtualDesktops().Count();
}

int GetCurrentVirtualDesktop() {
    return ReadVirtualDesktops().current;
}

bool SetCurrentVirtualDesktop(int index) {
    const VirtualDesktopLayout layout = ReadVirtualDesktops();
    if (index < 0 || index >= layout.Count()) return false;
    if (index == layout.current) return true;
    // The shortcut is Explorer's: without it nobody switches.
    if (!ExplorerTaskbarRunning()) return false;
    const WORD arrow = index > layout.current ? VK_RIGHT : VK_LEFT;
    for (int step = std::abs(index - layout.current); step > 0; --step) {
        if (!SendShortcut({VK_LCONTROL, VK_LWIN}, arrow)) return false;
    }
    return true;
}

bool SetVirtualDesktopCount(int) {
    // Windows offers no call that adds or removes a desktop; the count is
    // the user's, in Task View.
    return false;
}

bool MoveWindowToVirtualDesktop(uint64_t id, int index) {
    const HWND hwnd = ToHwnd(id);
    if (id == 0 || !IsWindow(hwnd)) return false;
    // "Every desktop" (pinning) has no public call.
    if (index < 0) return false;
    const VirtualDesktopLayout layout = ReadVirtualDesktops();
    if (index >= static_cast<int>(layout.ids.size())) return false;
    ComScope com;
    IVirtualDesktopManager* desktops = CreateVirtualDesktopManager();
    if (!desktops) return false;
    // Windows moves only the caller's own windows this way; another
    // program's window answers E_ACCESSDENIED.
    const bool moved = SUCCEEDED(desktops->MoveWindowToDesktop(hwnd, layout.ids[static_cast<size_t>(index)]));
    Release(desktops);
    return moved;
}

// ===== THE SCREEN =====

// The primary monitor, which is the one at 0,0: where a desktop window and
// its bars go. Physical pixels for a DPI-aware program, as every UltraCanvas
// program is.
bool GetScreenSize(int& width, int& height) {
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    return width > 0 && height > 0;
}

// Call it from the thread the window belongs to (the UI thread): the appbar
// windows are made there.
bool ReserveScreenEdges(uint64_t id, int left, int right, int top, int bottom) {
    const HWND owner = ToHwnd(id);
    if (id == 0 || !IsWindow(owner)) return false;
    std::lock_guard<std::mutex> lock(g_edgeMutex);
    const bool explorer = ExplorerTaskbarRunning();

    auto previous = g_edges.find(id);
    if (previous != g_edges.end()) {
        const EdgeStrips& held = previous->second;
        // Asked again for what it already holds - a relayout: nothing moves,
        // so maximized windows are not resized for nothing.
        if (held.left == left && held.right == right && held.top == top && held.bottom == bottom &&
            held.explorer == explorer)
            return true;
        for (HWND bar : held.appBars) DestroyAppBar(bar);
        g_edges.erase(previous);
    }

    EdgeStrips strips;
    strips.left = left;
    strips.right = right;
    strips.top = top;
    strips.bottom = bottom;
    strips.explorer = explorer;
    const bool releasing = left == 0 && right == 0 && top == 0 && bottom == 0;

    if (!explorer) {
        if (!releasing) g_edges.emplace(id, std::move(strips));
        ApplyWorkAreaLocked();
        return true;
    }
    if (releasing) return true;

    const std::pair<UINT, int> edges[] = {{ABE_LEFT, left}, {ABE_RIGHT, right}, {ABE_TOP, top}, {ABE_BOTTOM, bottom}};
    bool all = true;
    for (const auto& [edge, thickness] : edges) {
        if (thickness <= 0) continue;
        HWND bar = CreateAppBar(owner, edge, thickness);
        if (bar) strips.appBars.push_back(bar);
        else all = false;
    }
    g_edges.emplace(id, std::move(strips));
    return all;
}

bool CaptureScreenImage(DesktopScreenImage& out, std::string& error) {
    out = DesktopScreenImage();
    // The whole virtual screen, every monitor, as X11's root window is.
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        error = "Windows reports an empty screen.";
        return false;
    }
    HDC screen = GetDC(nullptr);
    if (!screen) {
        error = "No screen to capture: this program runs without a desktop.";
        return false;
    }
    BITMAPINFO bitmap = {};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = width;
    bitmap.bmiHeader.biHeight = -height;   // rows top to bottom
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bitmap, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC memory = dib ? CreateCompatibleDC(screen) : nullptr;
    bool ok = false;
    if (!dib || !memory || !bits) {
        error = "Not enough memory for a " + std::to_string(width) + "x" + std::to_string(height) + " image.";
    } else {
        HGDIOBJ previous = SelectObject(memory, dib);
        // No CAPTUREBLT: with the compositor (Windows 8 and later, always)
        // layered windows are in the copy anyway, and the flag makes the
        // mouse pointer flicker while it runs.
        if (!BitBlt(memory, 0, 0, width, height, screen, left, top, SRCCOPY)) {
            error = "Windows refused to hand over the screen contents (the screen is locked, or a secure "
                    "desktop is showing).";
        } else {
            GdiFlush();
            const int stride = width * 4;   // 32-bit DIB rows have no padding
            try {
                out.pixels.assign(static_cast<const uint8_t*>(bits),
                                  static_cast<const uint8_t*>(bits) + static_cast<size_t>(stride) * height);
                out.width = width;
                out.height = height;
                out.stride = stride;
                ok = true;
            } catch (const std::bad_alloc&) {
                out = DesktopScreenImage();
                error = "Not enough memory for a " + std::to_string(width) + "x" + std::to_string(height) +
                        " image.";
            }
        }
        SelectObject(memory, previous);
    }
    if (memory) DeleteDC(memory);
    if (dib) DeleteObject(dib);
    ReleaseDC(nullptr, screen);
    return ok;
}

bool CaptureScreen(const std::string& pngPath, std::string& error) {
    DesktopScreenImage shot;
    if (!CaptureScreenImage(shot, error)) return false;
    // The rows are already cairo's RGB24 layout, so the surface sits on them.
    cairo_surface_t* surface = cairo_image_surface_create_for_data(shot.pixels.data(), CAIRO_FORMAT_RGB24,
                                                                   shot.width, shot.height, shot.stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        error = std::string("Could not wrap the screen image: ") +
                cairo_status_to_string(cairo_surface_status(surface));
        cairo_surface_destroy(surface);
        return false;
    }
    const bool written = WindowsIcons::WriteSurfacePng(surface, pngPath);
    cairo_surface_destroy(surface);
    if (!written) {
        error = "Could not write \"" + pngPath + "\".";
        return false;
    }
    return true;
}

// ===== APPLICATIONS =====

bool ListNativeApplications(int iconSize, std::vector<UCDesktopEntry>& out) {
    ComScope com;   // the shell's icon extraction is COM
    const int size = std::max(16, iconSize);
    std::set<std::wstring> seen;   // relative paths: the user's shortcut shadows the machine's
    for (const fs::path& root : StartMenuDirectories()) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path file = it->path();
            const std::string path = PathToUtf8(file);
            if (!IsShellLinkPath(path)) continue;
            std::error_code relativeError;
            const std::wstring relative = LowerCase(fs::relative(file, root, relativeError).native());
            if (relativeError || !seen.insert(relative).second) continue;

            UCShellLink link;
            if (!ReadShellLink(path, link) || !IsApplicationShortcut(link)) continue;

            UCDesktopEntry entry;
            entry.kind = UCDesktopEntry::Kind::Application;
            entry.name = ShortcutDisplayName(file);
            entry.comment = Narrow(ResolveIndirectString(Widen(link.description)));
            entry.program = link.hostTargetPath;
            entry.workingDirectory = link.workingDirectory;
            entry.shortcutPath = path;
            // A window's appClass is its program's name: the same name here
            // lets MatchApplication tie the two together.
            if (!entry.program.empty()) entry.startupWMClass = Narrow(StemOf(Widen(entry.program)));
            const std::string key = "lnk|" + path + "|" + link.iconLocation + "|" + std::to_string(link.iconIndex) +
                                    "|" + link.targetPath + "|" + std::to_string(size);
            entry.iconFile = CachedIconFile(key, [&path, size]() { return LoadNativeFileIconPixmap(path, size); });
            out.push_back(std::move(entry));
        }
    }
    return true;
}

// ===== DEVICES =====

void ReadDeviceActivity(DesktopDeviceActivity& activity) {
    ComScope com;
    ProbeAudio(activity);
    ProbeCamera(activity);
    ProbeBattery(activity);
    ProbeKeyboardLayout(activity);
}

// ===== THE MONITOR =====

struct MonitorState : WindowsMonitorState {};

MonitorState* MonitorOpen() {
    auto* state = new MonitorState();
    state->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    state->registryChanged = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!state->wake) {
        if (state->registryChanged) CloseHandle(state->registryChanged);
        delete state;
        return nullptr;
    }
    return state;
}

// The hooks are set on the first call, which is on the monitor's thread:
// their events are delivered to that thread and run inside PeekMessage,
// pumped here. The wait also wakes every 200 ms to pump: the wake bit a
// queued WinEvent sets is not one of the documented QS_ALLINPUT bits, and a
// late taskbar is better than a stale one.
bool MonitorWait(MonitorState* state) {
    if (!state || !state->wake) return false;
    if (!state->started) StartWatching(state);
    for (;;) {
        HANDLE handles[2] = {state->wake, state->registryChanged};
        const DWORD count = (state->registryChanged && state->watchedKey) ? 2 : 1;
        const DWORD result = MsgWaitForMultipleObjectsEx(count, handles, 200, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result == WAIT_OBJECT_0) {   // woken to stop
            StopWatching(state);
            return false;
        }
        if (count == 2 && result == WAIT_OBJECT_0 + 1) {
            ArmRegistryWatch(state);
            return true;
        }
        if (result == WAIT_OBJECT_0 + count || result == WAIT_TIMEOUT) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
            if (state->changed) {
                state->changed = false;
                return true;
            }
            continue;
        }
        StopWatching(state);
        return false;
    }
}

void MonitorWake(MonitorState* state) {
    if (state && state->wake) SetEvent(state->wake);
}

void MonitorClose(MonitorState* state) {
    if (!state) return;
    // The hooks went with the thread that set them (StopWatching); a monitor
    // that never waited set none.
    if (state->watchedKey) RegCloseKey(state->watchedKey);
    if (state->registryChanged) CloseHandle(state->registryChanged);
    if (state->wake) CloseHandle(state->wake);
    delete state;
}

// ===== A SHORTCUT FOR THE WHOLE DESKTOP =====

struct ShortcutState {
    int id = 0;
    UINT modifiers = 0;
    UINT key = 0;
    HANDLE wake = nullptr;
    bool registered = false;
};

ShortcutState* ShortcutOpen(const std::string& accelerator, std::string& error) {
    // "Super+V" -> MOD_WIN and 'V'.
    UINT modifiers = 0;
    std::string key;
    size_t start = 0;
    while (start <= accelerator.size()) {
        size_t plus = accelerator.find('+', start);
        if (plus == start && plus + 1 == accelerator.size()) plus = std::string::npos;   // "Ctrl++"
        const std::string part =
            accelerator.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        const std::string lower = LowerAscii(part);
        if (lower == "super" || lower == "win" || lower == "meta") modifiers |= MOD_WIN;
        else if (lower == "ctrl" || lower == "control") modifiers |= MOD_CONTROL;
        else if (lower == "alt") modifiers |= MOD_ALT;
        else if (lower == "shift") modifiers |= MOD_SHIFT;
        else key = part;
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    if (key.empty()) {
        error = "\"" + accelerator + "\" names no key.";
        return nullptr;
    }
    const UINT vk = VirtualKeyFor(key);
    if (vk == 0) {
        error = "\"" + key + "\" is not a key name.";
        return nullptr;
    }

    // A hot key belongs to the thread that registers it, and the shortcut's
    // own thread registers it for real (ShortcutWait). Trying it here first
    // is what lets Start report a combination another program holds.
    const int id = g_nextHotKeyId.fetch_add(1);
    if (!RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, vk)) {
        const DWORD reason = GetLastError();
        error = reason == ERROR_HOTKEY_ALREADY_REGISTERED
                    ? accelerator + " is taken by another program" +
                          ((modifiers & MOD_WIN) ? " (Windows keeps most Win-key shortcuts for itself)." : ".")
                    : accelerator + " cannot be registered (error " + std::to_string(reason) + ").";
        return nullptr;
    }
    UnregisterHotKey(nullptr, id);

    auto* state = new ShortcutState();
    state->id = id;
    state->modifiers = modifiers | MOD_NOREPEAT;
    state->key = vk;
    state->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!state->wake) {
        delete state;
        error = "No event for the shortcut's thread.";
        return nullptr;
    }
    return state;
}

// WM_HOTKEY arrives once per press (MOD_NOREPEAT: holding the keys repeats
// nothing).
bool ShortcutWait(ShortcutState* state) {
    if (!state || !state->wake) return false;
    if (!state->registered) {
        if (!RegisterHotKey(nullptr, state->id, state->modifiers, state->key)) return false;
        state->registered = true;
    }
    auto finish = [state]() {
        if (state->registered) UnregisterHotKey(nullptr, state->id);
        state->registered = false;
        return false;
    };
    for (;;) {
        const DWORD result = MsgWaitForMultipleObjectsEx(1, &state->wake, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result != WAIT_OBJECT_0 + 1) return finish();   // woken to stop, or the wait failed
        bool pressed = false;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && static_cast<int>(msg.wParam) == state->id) pressed = true;
            else DispatchMessageW(&msg);
        }
        if (pressed) return true;
    }
}

void ShortcutWake(ShortcutState* state) {
    if (state && state->wake) SetEvent(state->wake);
}

void ShortcutClose(ShortcutState* state) {
    if (!state) return;
    // Unregistered on its own thread when the wait ended.
    if (state->wake) CloseHandle(state->wake);
    delete state;
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas
