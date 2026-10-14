// OS/MSWindows/UltraCanvasWindowsTrayHost.cpp
// The notification area for a desktop that is the session's shell: the
// window every program's Shell_NotifyIcon talks to, as Explorer's taskbar is
// when Explorer is the shell.
//
// Shell_NotifyIcon finds the window by its class, Shell_TrayWnd, and hands
// it the icon in a WM_COPYDATA:
//   - dwData 1: a NOTIFYICONDATAW, always in its newest layout (shell32
//     repackages whatever size the program passed) and always 32-bit - a
//     window or icon handle is 32 significant bits, so 64-bit and 32-bit
//     programs send the same bytes - behind a two-word header. The answer
//     is the call's result.
//   - dwData 3: Shell_NotifyIconGetRect asking where an icon is on screen,
//     answered with the corners packed into the result.
//   - dwData 0: SHAppBarMessage, which this host does not answer: a program
//     asking where the taskbar is hears "nowhere" and places itself.
// The layout is not documented; it is the one Cairo's ManagedShell, ReactOS
// and every replacement shell read, and the one Windows has kept since XP.
//
// A click on an icon goes back to the program as its callback message, in
// the form the program asked for with NIM_SETVERSION: the button message
// alone (version 0), plus NIN_SELECT / WM_CONTEXTMENU (3), or with the
// anchor point and the icon's id packed in (4).
//
// The window lives on the host's own thread (TrayHostAttach / TrayHostWait),
// so a desktop busy drawing never stalls a program's Shell_NotifyIcon. The
// icons' HICON copies are only ever touched on that thread; the list the
// desktop reads is guarded by a mutex.
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0A00
#endif
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif

#include "UltraCanvasDesktopShellBackend.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasWindowsDesktopShellInternal.h"
#include "UltraCanvasWindowsIcons.h"

#include <windows.h>
#include <shellapi.h>

#include <cairo/cairo.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

// The notification-area words older SDK headers may lack.
#ifndef NIN_SELECT
#  define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIF_GUID
#  define NIF_GUID 0x00000020
#endif
#ifndef NIS_HIDDEN
#  define NIS_HIDDEN 0x00000001
#endif
#ifndef NIM_SETVERSION
#  define NIM_SETVERSION 0x00000004
#endif
#ifndef NIM_SETFOCUS
#  define NIM_SETFOCUS 0x00000003
#endif
#ifndef MSGFLT_ALLOW
#  define MSGFLT_ALLOW 1
#endif

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {

    // ===== WHAT Shell_NotifyIcon SENDS =====

    struct NotifyIconData32 {
        uint32_t cbSize;
        uint32_t hWnd;
        uint32_t uID;
        uint32_t uFlags;
        uint32_t uCallbackMessage;
        uint32_t hIcon;
        uint16_t szTip[128];
        uint32_t dwState;
        uint32_t dwStateMask;
        uint16_t szInfo[256];
        uint32_t uVersion;          // uTimeout, before NIM_SETVERSION used it
        uint16_t szInfoTitle[64];
        uint32_t dwInfoFlags;
        GUID     guidItem;
        uint32_t hBalloonIcon;
    };
    static_assert(offsetof(NotifyIconData32, szTip) == 24, "NOTIFYICONDATAW, 32-bit layout");
    static_assert(offsetof(NotifyIconData32, szInfo) == 288, "NOTIFYICONDATAW, 32-bit layout");
    static_assert(offsetof(NotifyIconData32, guidItem) == 936, "NOTIFYICONDATAW, 32-bit layout");
    static_assert(sizeof(NotifyIconData32) == 956, "NOTIFYICONDATAW, 32-bit layout");

    struct TrayMessage32 {
        uint32_t signature;         // 0x34753423 from shell32; not checked
        uint32_t message;           // NIM_ADD, NIM_MODIFY, ...
        NotifyIconData32 data;
    };

    // Shell_NotifyIconGetRect's question.
    struct IconLocationQuery32 {
        uint32_t signature;
        uint32_t message;           // 1: the top-left corner, 2: the bottom-right one
        uint32_t cbSize;
        uint32_t padding;
        uint32_t hWnd;
        uint32_t uID;
        GUID     guidItem;
    };
    static_assert(sizeof(IconLocationQuery32) == 40, "NOTIFYICONIDENTIFIER, 32-bit layout");

    constexpr ULONG_PTR kIconMessage = 1;
    constexpr ULONG_PTR kIconLocation = 3;

    // A handle in its 32-bit form, back to a handle: sign-extended, as
    // Windows widens them.
    template <typename Handle>
    Handle FromHandle32(uint32_t value) {
        return reinterpret_cast<Handle>(static_cast<intptr_t>(static_cast<int32_t>(value)));
    }

    // A fixed-size UTF-16 field, up to its first NUL.
    std::string FieldText(const uint16_t* field, size_t capacity) {
        size_t length = 0;
        while (length < capacity && field[length] != 0) ++length;
        std::wstring text(length, L'\0');
        for (size_t i = 0; i < length; ++i) text[i] = static_cast<wchar_t>(field[i]);
        return PathUtf8Detail::Utf16ToUtf8(text);
    }

    // ===== ONE ICON =====

    struct HostedIcon {
        uint64_t serial = 0;        // the id's number
        HWND     window = nullptr;  // the program's window that gets the clicks
        UINT     uid = 0;
        GUID     guid = {};
        bool     hasGuid = false;
        UINT     callbackMessage = 0;
        UINT     version = 0;       // NIM_SETVERSION: 0, 3 or 4
        HICON    icon = nullptr;    // our copy; the host thread's alone
        bool     iconDirty = false;
        bool     hidden = false;
        bool     dead = false;      // its window went: removed on the host thread
        std::string tooltip;
        DWORD    processId = 0;
        std::string appClass;
        std::string iconFile;
        std::string balloonTitle;
        std::string balloonText;
        uint64_t balloonSerial = 0;
        POINT    anchor = {};       // where it was last clicked
        bool     haveAnchor = false;
        ULONGLONG lastLeftClick = 0;

        std::string Id() const { return "tray-" + std::to_string(serial); }
    };

    constexpr wchar_t kTrayClass[] = L"Shell_TrayWnd";
    constexpr wchar_t kNotifyClass[] = L"TrayNotifyWnd";

} // namespace

// ===== THE HOST =====

struct TrayHostState {
    std::mutex mutex;
    std::vector<HostedIcon> icons;  // in the order added
    uint64_t nextSerial = 1;
    uint64_t nextBalloon = 1;
    HANDLE wake = nullptr;          // auto-reset: TrayHostWake
    HWND window = nullptr;          // Shell_TrayWnd, on the host thread
    HWND notify = nullptr;          // its TrayNotifyWnd child
    bool changed = false;           // host thread only
    ULONGLONG lastSweep = 0;
};

namespace {

    bool SameGuid(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }
    bool IsNullGuid(const GUID& g) {
        static const GUID none = {};
        return SameGuid(g, none);
    }

    // The icon a message is about: by its GUID when it names one, else by
    // the window and the program's own number for it. Caller holds the mutex.
    HostedIcon* FindIcon(TrayHostState* state, HWND window, UINT uid, const GUID* guid) {
        for (HostedIcon& icon : state->icons) {
            if (icon.dead) continue;
            if (guid && !IsNullGuid(*guid)) {
                if (icon.hasGuid && SameGuid(icon.guid, *guid)) return &icon;
            } else if (icon.window == window && icon.uid == uid) {
                return &icon;
            }
        }
        return nullptr;
    }

    void ApplyFields(TrayHostState* state, HostedIcon& icon, const NotifyIconData32& data) {
        if (data.uFlags & NIF_MESSAGE) icon.callbackMessage = data.uCallbackMessage;
        if (data.uFlags & NIF_ICON) {
            // The program may destroy its icon the moment the call returns:
            // the host keeps a copy of its own.
            if (icon.icon) DestroyIcon(icon.icon);
            const HICON given = FromHandle32<HICON>(data.hIcon);
            icon.icon = given ? CopyIcon(given) : nullptr;
            icon.iconDirty = true;
        }
        if (data.uFlags & NIF_TIP) icon.tooltip = FieldText(data.szTip, std::size(data.szTip));
        if ((data.uFlags & NIF_STATE) && (data.dwStateMask & NIS_HIDDEN)) {
            icon.hidden = (data.dwState & NIS_HIDDEN) != 0;
        }
        if (data.uFlags & NIF_INFO) {
            const std::string text = FieldText(data.szInfo, std::size(data.szInfo));
            if (!text.empty()) {
                icon.balloonTitle = FieldText(data.szInfoTitle, std::size(data.szInfoTitle));
                icon.balloonText = text;
                icon.balloonSerial = state->nextBalloon++;
            }
        }
        if ((data.uFlags & NIF_GUID) && !IsNullGuid(data.guidItem)) {
            icon.guid = data.guidItem;
            icon.hasGuid = true;
        }
    }

    // Shell_NotifyIcon's call, answered as its result.
    LRESULT OnIconMessage(TrayHostState* state, const COPYDATASTRUCT* copy) {
        // At least everything up to the tip: what every version carries.
        constexpr size_t kHeader = offsetof(TrayMessage32, data);
        if (!copy->lpData || copy->cbData < kHeader + offsetof(NotifyIconData32, dwState)) return FALSE;
        TrayMessage32 message = {};
        std::memcpy(&message, copy->lpData, std::min<size_t>(copy->cbData, sizeof(message)));
        const NotifyIconData32& data = message.data;
        const HWND window = FromHandle32<HWND>(data.hWnd);
        const GUID* guid = (data.uFlags & NIF_GUID) ? &data.guidItem : nullptr;

        std::lock_guard<std::mutex> lock(state->mutex);
        HostedIcon* icon = FindIcon(state, window, data.uID, guid);
        switch (message.message) {
            case NIM_ADD:
                if (!icon) {
                    if (!window || !IsWindow(window)) return FALSE;
                    HostedIcon added;
                    added.serial = state->nextSerial++;
                    added.window = window;
                    added.uid = data.uID;
                    GetWindowThreadProcessId(window, &added.processId);
                    added.appClass = Win32::ProgramNameOfProcess(added.processId);
                    state->icons.push_back(std::move(added));
                    icon = &state->icons.back();
                }
                ApplyFields(state, *icon, data);
                state->changed = true;
                return TRUE;
            case NIM_MODIFY:
                if (!icon) return FALSE;
                ApplyFields(state, *icon, data);
                state->changed = true;
                return TRUE;
            case NIM_DELETE:
                if (!icon) return FALSE;
                icon->dead = true;   // its HICON goes on this thread's sweep
                state->changed = true;
                return TRUE;
            case NIM_SETFOCUS:
                return icon ? TRUE : FALSE;
            case NIM_SETVERSION:
                if (!icon || data.uVersion > 4) return FALSE;
                icon->version = data.uVersion;
                return TRUE;
            default:
                return FALSE;
        }
    }

    // Where the icon is: around the point it was last clicked at, else
    // around the mouse pointer - where its program's popup belongs.
    LRESULT OnIconLocation(TrayHostState* state, const COPYDATASTRUCT* copy) {
        if (!copy->lpData || copy->cbData < sizeof(IconLocationQuery32)) return 0;
        IconLocationQuery32 query = {};
        std::memcpy(&query, copy->lpData, sizeof(query));
        POINT at = {};
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            HostedIcon* icon = FindIcon(state, FromHandle32<HWND>(query.hWnd), query.uID, &query.guidItem);
            if (!icon) return 0;
            if (icon->haveAnchor) at = icon->anchor;
            else GetCursorPos(&at);
        }
        constexpr int kHalf = 8;
        if (query.message == 1) return MAKELPARAM(at.x - kHalf, at.y - kHalf);
        if (query.message == 2) return MAKELPARAM(at.x + kHalf, at.y + kHalf);
        return 0;
    }

    LRESULT CALLBACK TrayWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* state = reinterpret_cast<TrayHostState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_COPYDATA && state) {
            const auto* copy = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            if (copy && copy->dwData == kIconMessage) return OnIconMessage(state, copy);
            if (copy && copy->dwData == kIconLocation) return OnIconLocation(state, copy);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    HINSTANCE TrayModule() {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&TrayWindowProc), &module);
        return module;
    }

    bool RegisterTrayClass(const wchar_t* name) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TrayWindowProc;
        wc.hInstance = TrayModule();
        wc.lpszClassName = name;
        return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    // A changed icon picture into the PNG the desktop draws, named after
    // its pixels: an animated icon's frames are each written once.
    std::string IconPng(HICON handle) {
        const std::shared_ptr<UCPixmap> pixmap = WindowsIcons::PixmapFromIcon(handle);
        if (!pixmap || !pixmap->IsValid()) return {};
        cairo_surface_t* surface = pixmap->GetSurface();
        cairo_surface_flush(surface);
        const unsigned char* data = cairo_image_surface_get_data(surface);
        const int width = cairo_image_surface_get_width(surface);
        const int height = cairo_image_surface_get_height(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        if (!data || width <= 0 || height <= 0) return {};
        uint64_t hash = 1469598103934665603ull;   // FNV-1a, 64 bit
        for (int y = 0; y < height; ++y) {
            const unsigned char* row = data + static_cast<size_t>(y) * stride;
            for (int x = 0; x < width * 4; ++x) {
                hash ^= row[x];
                hash *= 1099511628211ull;
            }
        }
        char key[64] = {};
        std::snprintf(key, sizeof key, "tray|%dx%d|%016llx", width, height, static_cast<unsigned long long>(hash));
        return Win32::CachedIconPng(key, [&pixmap]() { return pixmap; });
    }

    // On the host thread, after the messages: new pictures drawn, icons of
    // windows that are gone (a program that crashed says no NIM_DELETE)
    // dropped with their copies.
    void Settle(TrayHostState* state) {
        const ULONGLONG now = GetTickCount64();
        const bool sweep = now - state->lastSweep >= 2000;
        if (sweep) state->lastSweep = now;

        std::vector<std::pair<uint64_t, HICON>> toDraw;
        std::vector<HICON> toDestroy;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            for (HostedIcon& icon : state->icons) {
                if (sweep && !icon.dead && !IsWindow(icon.window)) icon.dead = true;
                if (icon.dead) {
                    if (icon.icon) toDestroy.push_back(icon.icon);
                    icon.icon = nullptr;
                    state->changed = true;
                    continue;
                }
                if (icon.iconDirty) {
                    icon.iconDirty = false;
                    if (icon.icon) toDraw.emplace_back(icon.serial, icon.icon);
                    else icon.iconFile.clear();
                }
            }
            state->icons.erase(std::remove_if(state->icons.begin(), state->icons.end(),
                                              [](const HostedIcon& icon) { return icon.dead; }),
                               state->icons.end());
        }
        for (HICON icon : toDestroy) DestroyIcon(icon);
        if (toDraw.empty()) return;
        // Drawn outside the lock (the handles are this thread's alone), then
        // handed in - unless the picture changed again meanwhile.
        std::vector<std::pair<uint64_t, std::string>> drawn;
        for (const auto& [serial, handle] : toDraw) drawn.emplace_back(serial, IconPng(handle));
        std::lock_guard<std::mutex> lock(state->mutex);
        for (const auto& [serial, file] : drawn) {
            for (HostedIcon& icon : state->icons) {
                if (icon.serial == serial && !icon.iconDirty) icon.iconFile = file;
            }
        }
        state->changed = true;
    }

    void TakeDown(TrayHostState* state) {
        if (state->notify) DestroyWindow(state->notify);
        if (state->window) DestroyWindow(state->window);
        state->notify = nullptr;
        state->window = nullptr;
        std::vector<HICON> copies;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            for (HostedIcon& icon : state->icons) {
                if (icon.icon) copies.push_back(icon.icon);
            }
            state->icons.clear();
        }
        for (HICON icon : copies) DestroyIcon(icon);
    }

} // namespace

TrayHostState* TrayHostOpen(std::string& error) {
    if (Win32::ForeignTrayWindowRunning()) {
        error = "Another program shows the notification area - Explorer's taskbar, while Explorer is the "
                "desktop - and only one can.";
        return nullptr;
    }
    if (FindWindowW(kTrayClass, nullptr)) {
        error = "This program already hosts the notification area.";
        return nullptr;
    }
    auto* state = new TrayHostState();
    state->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!state->wake) {
        delete state;
        error = "No event for the notification area's thread.";
        return nullptr;
    }
    return state;
}

bool TrayHostAttach(TrayHostState* state, std::string& error) {
    if (!state) return false;
    if (Win32::ForeignTrayWindowRunning() || FindWindowW(kTrayClass, nullptr)) {
        error = "Another notification area started a moment ago.";
        return false;
    }
    if (!RegisterTrayClass(kTrayClass) || !RegisterTrayClass(kNotifyClass)) {
        error = "The notification area's window class could not be registered (error " +
                std::to_string(GetLastError()) + ").";
        return false;
    }
    // Never shown: the desktop draws the icons. It sits at the primary
    // screen's bottom-right corner, where a program that measures the
    // taskbar to place its popup then puts it - beside the desktop's own
    // bar of tray icons.
    const int x = std::max(0, GetSystemMetrics(SM_CXSCREEN) - 1);
    const int y = std::max(0, GetSystemMetrics(SM_CYSCREEN) - 1);
    state->window = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayClass, L"", WS_POPUP, x, y, 1, 1, nullptr, nullptr,
                                    TrayModule(), state);
    if (!state->window) {
        error = "The notification area's window could not be made (error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    // Some programs look for the icons' own window inside the taskbar.
    state->notify = CreateWindowExW(0, kNotifyClass, L"", WS_CHILD, 0, 0, 1, 1, state->window, nullptr,
                                    TrayModule(), state);
    // A program running at a lower integrity level (a sandboxed one) may
    // still add its icon.
    ChangeWindowMessageFilterEx(state->window, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    // Every program that showed an icon before - in Explorer, or in the
    // host that ran before this one - adds it again.
    const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreated) SendNotifyMessageW(HWND_BROADCAST, taskbarCreated, 0, 0);
    state->lastSweep = GetTickCount64();
    return true;
}

// Sent messages - which WM_COPYDATA from another thread or program is - are
// delivered inside PeekMessage; the wait also wakes once a second to look for
// icons whose window has gone.
bool TrayHostWait(TrayHostState* state) {
    if (!state || !state->wake) return false;
    for (;;) {
        const DWORD result = MsgWaitForMultipleObjectsEx(1, &state->wake, 1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result == WAIT_OBJECT_0 + 1 || result == WAIT_TIMEOUT) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
            Settle(state);
            if (state->changed) {
                state->changed = false;
                return true;
            }
            continue;
        }
        // Woken to stop, or the wait failed: the window goes with the thread.
        TakeDown(state);
        return false;
    }
}

void TrayHostWake(TrayHostState* state) {
    if (state && state->wake) SetEvent(state->wake);
}

void TrayHostClose(TrayHostState* state) {
    if (!state) return;
    // TakeDown ran on the host's thread when its wait ended; a host that
    // failed to attach never made a window.
    if (state->wake) CloseHandle(state->wake);
    delete state;
}

std::vector<DesktopTrayIcon> TrayHostIcons(TrayHostState* state) {
    std::vector<DesktopTrayIcon> icons;
    if (!state) return icons;
    std::lock_guard<std::mutex> lock(state->mutex);
    for (const HostedIcon& icon : state->icons) {
        if (icon.dead || icon.hidden) continue;
        DesktopTrayIcon out;
        out.id = icon.Id();
        out.tooltip = icon.tooltip;
        out.iconFile = icon.iconFile;
        out.appClass = icon.appClass;
        out.processId = static_cast<int>(icon.processId);
        out.balloonTitle = icon.balloonTitle;
        out.balloonText = icon.balloonText;
        out.balloonSerial = icon.balloonSerial;
        icons.push_back(std::move(out));
    }
    return icons;
}

bool TrayHostClick(TrayHostState* state, const std::string& id, TrayClick button) {
    if (!state) return false;
    POINT at = {};
    GetCursorPos(&at);   // fails on a desktop without the input; 0,0 then
    HWND window = nullptr;
    UINT uid = 0, callback = 0, version = 0;
    DWORD processId = 0;
    bool doubleClick = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        HostedIcon* found = nullptr;
        for (HostedIcon& icon : state->icons) {
            if (!icon.dead && icon.Id() == id) found = &icon;
        }
        if (!found || !IsWindow(found->window)) return false;
        found->anchor = at;
        found->haveAnchor = true;
        if (button == TrayClick::Left) {
            const ULONGLONG now = GetTickCount64();
            doubleClick = found->lastLeftClick != 0 && now - found->lastLeftClick <= GetDoubleClickTime();
            found->lastLeftClick = doubleClick ? 0 : now;
        }
        window = found->window;
        uid = found->uid;
        callback = found->callbackMessage;
        version = found->version;
        processId = found->processId;
    }
    // An icon that asked for no callback message takes no clicks.
    if (callback == 0) return true;
    // The program usually opens a window or a menu: it may take the
    // foreground, which this desktop - the one clicked - can hand on.
    AllowSetForegroundWindow(processId);
    const auto send = [&](UINT event) {
        WPARAM wParam = uid;
        LPARAM lParam = event;
        if (version >= 4) {
            wParam = MAKEWPARAM(static_cast<WORD>(at.x), static_cast<WORD>(at.y));
            lParam = MAKELPARAM(event, uid);
        }
        // Posted, never waited for: a hung program cannot hang the desktop.
        SendNotifyMessageW(window, callback, wParam, lParam);
    };
    switch (button) {
        case TrayClick::Left:
            send(doubleClick ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN);
            send(WM_LBUTTONUP);
            // Documented for version 4; Explorer sends it for 3 as well.
            if (version >= 3) send(NIN_SELECT);
            break;
        case TrayClick::Right:
            send(WM_RBUTTONDOWN);
            send(WM_RBUTTONUP);
            if (version >= 3) send(WM_CONTEXTMENU);
            break;
        case TrayClick::Middle:
            send(WM_MBUTTONDOWN);
            send(WM_MBUTTONUP);
            break;
    }
    return true;
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas
