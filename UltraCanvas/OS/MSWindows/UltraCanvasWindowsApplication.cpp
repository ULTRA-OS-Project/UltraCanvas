// OS/MSWindows/UltraCanvasWindowsApplication.cpp
// Complete Windows application implementation with all methods
// Version: 1.4.0 - Wheel delta normalized to +/-1 per notch
// Last Modified: 2026-07-20
// Author: UltraCanvas Framework

// winsock2.h must precede windows.h (pulled in by the headers below) so the legacy winsock.h v1
// is not included instead. Needed for select()/fd_set used to service host fd-watches.
#include <winsock2.h>

#include "../../include/UltraCanvasApplication.h"
#include "../../include/UltraCanvasWindow.h"
#include "UltraCanvasWindowsApplication.h"
#include "UltraCanvasWindowsDiagnostics.h"
#include <iostream>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <sstream>
#include <pango/pangocairo.h>
#include <fontconfig/fontconfig.h>
#include "UltraCanvasDebug.h"

// Link against IME library
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "gdi32.lib")

namespace UltraCanvas {

    UltraCanvasWindowsApplication* UltraCanvasWindowsApplication::instance = nullptr;

// ===== CONSTRUCTOR =====
    UltraCanvasWindowsApplication::UltraCanvasWindowsApplication()
            : hInstance(nullptr)
            , windowClassAtom(0) {
        instance = this;
        debugOutput << "UltraCanvas: Windows Application created" << std::endl;
    }

    UltraCanvasWindowsApplication::~UltraCanvasWindowsApplication() {
        // Clear the singleton pointer so GetInstance() returns nullptr once the
        // app is gone; widget destructors that run later (e.g. at static exit)
        // then skip CleanupElementReferences instead of locking a dead mutex.
        if (instance == this) instance = nullptr;
    }

// ===== INITIALIZATION =====
    bool UltraCanvasWindowsApplication::InitializeNative() {
        if (initialized) {
            debugOutput << "UltraCanvas: Already initialized" << std::endl;
            return true;
        }

        // STEP 0: Make this process able to explain itself. The Windows targets
        // link as GUI-subsystem executables, so without these three calls a
        // failure to start produces no window, no console output and no log --
        // the failure mode that makes "it doesn't start on that machine"
        // unfixable. None of them affects a successful start.
        AttachParentConsole();
        InstallWindowsCrashReporter(appName);
        LogWindowsStartupBanner(appName);

        debugOutput << "UltraCanvas: Initializing Windows Application..." << std::endl;

        try {
            // STEP 1: Get module handle
            hInstance = GetModuleHandle(nullptr);
            if (!hInstance) {
                ReportWindowsStartupFailure("GetModuleHandle failed",
                                            DescribeWin32Error(GetLastError()));
                return false;
            }

            // STEP 2: Initialize COM/OLE for drag-drop and file dialogs
            HRESULT hr = OleInitialize(nullptr);
            if (FAILED(hr)) {
                std::ostringstream detail;
                detail << "OleInitialize returned 0x" << std::hex << hr;
                ReportWindowsStartupFailure("COM/OLE initialisation failed", detail.str());
                return false;
            }

            // STEP 3: Enable DPI awareness (Windows 10 1703+)
            // Fall back gracefully on older Windows versions
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32) {
                using SetDpiAwarenessContextFunc = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
                auto setDpiFunc = reinterpret_cast<SetDpiAwarenessContextFunc>(
                    GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
                if (setDpiFunc) {
                    setDpiFunc(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                } else {
                    // Fallback for Windows 8.1+
                    SetProcessDPIAware();
                }
            }

            // STEP 4: Build dynamic window class name from app name
            mainWindowClassName = Utf8ToUtf16("UltraCanvas_" + appName + "_Main");

            // STEP 5: Register main window class
            if (!RegisterWindowClass()) {
                // RegisterWindowClass() has already reported the reason, while
                // GetLastError() still held it.
                OleUninitialize();
                return false;
            }

            // STEP 6: Pre-register common custom window classes
            RegisterCustomWindowClass("Dialog");
            RegisterCustomWindowClass("Tool");
            RegisterCustomWindowClass("Popup");

            // STEP 7: Initialize wakeup mechanism for cross-thread signaling
            InitializeWakeUp();

            // STEP 8: Mark as initialized
            initialized = true;
            running = false;

            debugOutput << "UltraCanvas: Windows Application initialized successfully" << std::endl;
            return true;

        } catch (const std::exception& e) {
            ReportWindowsStartupFailure("Exception during initialisation", e.what());
            ShutdownNative();
            return false;
        }
    }

    void UltraCanvasWindowsApplication::ShutdownNative() {
        // Clean up wakeup mechanism
        ShutdownWakeUp();

        // Clean up cached cursors
        for (auto& [key, cursor] : cursors) {
            if (cursor) {
                DestroyCursor(cursor);
            }
        }
        cursors.clear();

        // Unregister window class
        UnregisterWindowClass();

        // Uninitialize OLE
        OleUninitialize();

        hInstance = nullptr;
        debugOutput << "UltraCanvas: Windows Application shut down" << std::endl;
    }

    bool UltraCanvasWindowsApplication::RegisterWindowClass() {
        // Try to load embedded app icon (resource ID 101, set by UltraCanvasAppIcon.cmake)
        embeddedIconBig = LoadIcon(hInstance, MAKEINTRESOURCE(101));
        if (!embeddedIconBig) {
            embeddedIconBig = LoadIcon(nullptr, IDI_APPLICATION);
        }
        embeddedIconSmall = embeddedIconBig;

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = StaticWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = embeddedIconBig;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;  // We handle all painting via Cairo
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = mainWindowClassName.c_str();
        wc.hIconSm = embeddedIconSmall;

        windowClassAtom = RegisterClassExW(&wc);
        if (!windowClassAtom) {
            ReportWindowsStartupFailure(
                "Could not register the window class \"" +
                    Utf16ToUtf8(mainWindowClassName) + "\"",
                DescribeWin32Error(GetLastError()));
            return false;
        }

        debugOutput << "UltraCanvas: Window class registered: "
                    << Utf16ToUtf8(mainWindowClassName) << std::endl;
        return true;
    }

    void UltraCanvasWindowsApplication::UnregisterWindowClass() {
        if (windowClassAtom && hInstance) {
            UnregisterClassW(mainWindowClassName.c_str(), hInstance);
            windowClassAtom = 0;
        }
        // Unregister custom window classes
        for (auto& [suffix, className] : customWindowClasses) {
            UnregisterClassW(className.c_str(), hInstance);
        }
        customWindowClasses.clear();
    }

// ===== CUSTOM WINDOW CLASSES =====

    std::wstring UltraCanvasWindowsApplication::RegisterCustomWindowClass(const std::string& suffix) {
        // Check if already registered
        auto it = customWindowClasses.find(suffix);
        if (it != customWindowClasses.end()) {
            return it->second;
        }

        // Build class name: "UltraCanvas_<appName>_<suffix>"
        std::string className = "UltraCanvas_" + appName + "_" + suffix;
        std::wstring wClassName = Utf8ToUtf16(className);

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = StaticWndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = embeddedIconBig ? embeddedIconBig : LoadIcon(nullptr, IDI_APPLICATION);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = wClassName.c_str();
        wc.hIconSm = wc.hIcon;

        ATOM atom = RegisterClassExW(&wc);
        if (!atom) {
            debugOutput << "UltraCanvas: Failed to register custom window class: "
                        << className << " error=" << GetLastError() << std::endl;
            return L"";
        }

        customWindowClasses[suffix] = wClassName;
        debugOutput << "UltraCanvas: Custom window class registered: " << className << std::endl;
        return wClassName;
    }

    const wchar_t* UltraCanvasWindowsApplication::GetWindowClassName(const std::string& suffix) const {
        if (suffix.empty() || suffix == "Main") {
            return mainWindowClassName.c_str();
        }
        auto it = customWindowClasses.find(suffix);
        if (it != customWindowClasses.end()) {
            return it->second.c_str();
        }
        // Fallback to main class
        return mainWindowClassName.c_str();
    }

// ===== MAIN LOOP =====

    // Poll interval (ms) used to bound the message wait while host fd-watches are registered, so
    // level-triggered socket readiness is picked up promptly without a busy-spin. Winsock select()
    // is edge-agnostic here: we re-poll each iteration rather than relying on WSAEventSelect's
    // edge semantics, which do not match Ladybird's level-triggered Core::Notifier expectations.
    static constexpr DWORD kFdWatchPollMs = 10;

    bool UltraCanvasWindowsApplication::PollAndServiceFdWatches() {
        auto fdWatchKeys = SnapshotFdWatchKeys();
        if (fdWatchKeys.empty())
            return false;

        fd_set readfds;
        fd_set writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        int count = 0;
        for (const auto& key : fdWatchKeys) {
            if (key.fd < 0)
                continue;
            // Ladybird's Windows IPC fds are raw Winsock SOCKETs (see SocketpairWindows.cpp).
            auto sock = static_cast<SOCKET>(static_cast<uintptr_t>(key.fd));
            FD_SET(sock, key.type == FdWatchType::Write ? &writefds : &readfds);
            ++count;
        }
        if (count == 0)
            return true;

        timeval tv { 0, 0 }; // non-blocking poll; the message wait provides the actual blocking.
        int result = select(0, &readfds, &writefds, nullptr, &tv); // nfds is ignored by Winsock.
        if (result <= 0)
            return true;

        for (const auto& key : fdWatchKeys) {
            if (key.fd < 0)
                continue;
            auto sock = static_cast<SOCKET>(static_cast<uintptr_t>(key.fd));
            fd_set* set = key.type == FdWatchType::Write ? &writefds : &readfds;
            if (FD_ISSET(sock, set))
                FireFdWatch(key.id); // invoked unlocked, may re-enter Add/RemoveFdWatch
        }
        return true;
    }

    void UltraCanvasWindowsApplication::CollectAndProcessNativeEvents() {
        MSG msg;
        // 1. Drain all pending messages (non-blocking)
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // 2. Service host fd-watches that are already readable/writable (e.g. Ladybird IPC).
        bool const have_fd_watches = PollAndServiceFdWatches();

        // 3. Compute wait timeout from timer system
        auto timeout = GetTimeUntilNextTimer();
        DWORD waitMs = (timeout == std::chrono::milliseconds::max())
                       ? INFINITE
                       : static_cast<DWORD>(timeout.count());

        // Bound the wait while watching fds so newly-ready sockets are serviced within the poll
        // interval (Winsock offers no way to fold arbitrary fds into MsgWaitForMultipleObjectsEx).
        if (have_fd_watches && (waitMs == INFINITE || waitMs > kFdWatchPollMs))
            waitMs = kFdWatchPollMs;

        // 4. Wait for messages, wakeup event, or timer expiry
        if (wakeupEvent) {
            MsgWaitForMultipleObjectsEx(1, &wakeupEvent, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        } else {
            MsgWaitForMultipleObjectsEx(0, nullptr, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        // WAIT_OBJECT_0     = wakeupEvent signaled (auto-reset clears it)
        // WAIT_OBJECT_0 + 1 = Win32 message available
        // WAIT_TIMEOUT      = timer expired

        // 5. Service fd-watches again post-wait so IPC replies are handled this same iteration.
        if (have_fd_watches)
            PollAndServiceFdWatches();
    }

    // ===== WAKEUP MECHANISM =====
    void UltraCanvasWindowsApplication::InitializeWakeUp() {
        wakeupEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!wakeupEvent) {
            debugOutput << "UltraCanvas: Failed to create wakeup event" << std::endl;
        }
    }

    void UltraCanvasWindowsApplication::ShutdownWakeUp() {
        if (wakeupEvent) {
            CloseHandle(wakeupEvent);
            wakeupEvent = nullptr;
        }
    }

    void UltraCanvasWindowsApplication::WakeUpEventLoop() {
        if (wakeupEvent) {
            SetEvent(wakeupEvent);
        }
    }

// ===== WNDPROC =====

    namespace {
        // The event a window message stands for, for the log line and the
        // dialog of ReportWindowsEventException: "mouse move" tells the reader
        // more than "message 0x0200".
        std::string DescribeWindowMessage(UINT msg) {
            switch (msg) {
                case WM_PAINT:         return "paint";
                case WM_SIZE:          return "resize";
                case WM_MOUSEMOVE:     return "mouse move";
                case WM_LBUTTONDOWN:   return "left button press";
                case WM_LBUTTONUP:     return "left button release";
                case WM_LBUTTONDBLCLK: return "left double-click";
                case WM_RBUTTONDOWN:   return "right button press";
                case WM_RBUTTONUP:     return "right button release";
                case WM_MBUTTONDOWN:   return "middle button press";
                case WM_MBUTTONUP:     return "middle button release";
                case WM_MOUSEWHEEL:    return "mouse wheel";
                case WM_MOUSEHWHEEL:   return "horizontal mouse wheel";
                case WM_MOUSELEAVE:    return "mouse leave";
                case WM_KEYDOWN:       return "key press";
                case WM_KEYUP:         return "key release";
                case WM_SYSKEYDOWN:    return "system key press";
                case WM_SYSKEYUP:      return "system key release";
                case WM_CHAR:          return "character input";
                case WM_SETFOCUS:      return "focus gained";
                case WM_KILLFOCUS:     return "focus lost";
                case WM_TIMER:         return "timer";
                case WM_CLOSE:         return "window close";
                case WM_DPICHANGED:    return "DPI change";
                case WM_SETCURSOR:     return "cursor selection";
                default: {
                    char buffer[40];
                    std::snprintf(buffer, sizeof(buffer), "window message 0x%04X",
                                  static_cast<unsigned>(msg));
                    return buffer;
                }
            }
        }
    } // namespace

    LRESULT CALLBACK UltraCanvasWindowsApplication::StaticWndProc(
            HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

        UltraCanvasWindowsWindow* window = nullptr;

        if (msg == WM_NCCREATE) {
            // Extract the pointer passed via CreateWindowExW's lpParam
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            window = reinterpret_cast<UltraCanvasWindowsWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        } else {
            window = reinterpret_cast<UltraCanvasWindowsWindow*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        // No C++ exception may leave this function. user32 calls it from a
        // callback the kernel dispatched (KiUserCallbackDispatcher), and on
        // x64 the unwinder cannot walk back across that boundary: an
        // exception thrown by any event handler below -- a click on a folder,
        // a hover, a key -- never reaches the application's try/catch around
        // Run(). Instead the process dies, reported by the crash filter as
        // STATUS_BAD_FUNCTION_TABLE (0xC00000FF) in ntdll or as the bare GCC
        // throw code (0x20474343) in KERNELBASE, with the error text lost.
        // Caught here it becomes a logged, reported error and an abandoned
        // event, which is what the same exception is on the other platforms.
        try {
            // Let the application convert messages to UCEvents
            if (instance) {
                instance->ProcessWindowMessage(hwnd, msg, wParam, lParam);
            }

            // Let the window handle its own messages
            if (window) {
                return window->HandleMessage(hwnd, msg, wParam, lParam);
            }
        } catch (const std::exception& e) {
            ReportWindowsEventException(DescribeWindowMessage(msg), e.what());
            // Handled as far as Windows is concerned: DefWindowProc would act
            // on the message itself (WM_CLOSE destroys the window), which
            // the abandoned handler did not decide.
            if (window) return 0;
        } catch (...) {
            ReportWindowsEventException(DescribeWindowMessage(msg),
                                        "exception of a non-std::exception type");
            if (window) return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

// ===== MOUSE CAPTURE =====

    void UltraCanvasWindowsApplication::CaptureMouseNative() {
        auto* fw = GetFocusedWindow();
        if (!fw) {
            debugOutput << "UltraCanvas: Cannot capture mouse - no focused window" << std::endl;
            return;
        }

        HWND hwnd = fw->GetNativeHandle();
        if (hwnd) {
            SetCapture(hwnd);
        }
    }

    void UltraCanvasWindowsApplication::ReleaseMouseNative() {
        ReleaseCapture();
    }

// ===== EVENT PROCESSING =====

    void UltraCanvasWindowsApplication::ProcessWindowMessage(
            HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

        UCEvent event;
        event.timestamp = std::chrono::steady_clock::now();
        event.nativeWindowHandle = hwnd;
        auto* tw = FindWindow(hwnd);
        if (tw) {
            event.targetWindow = tw->GetWindowWeakPtr();
        }

        // Windows delivers mouse coords in PHYSICAL client px (the process is
        // Per-Monitor-V2 aware). Convert every pushed event's pointer fields to
        // LOGICAL px before dispatch so hit-testing matches the logical UI space.
        // Non-pointer events (keyboard/focus/close) carry a {0,0} pointer, which
        // scales to {0,0} — a harmless no-op.
        auto pushEventScaled = [&](UCEvent& e) {
            if (tw) {
                float s = tw->GetDeviceScale();
                if (s != 1.0f) {
                    e.pointerWindow = tw->PhysicalToLogical(e.pointerWindow);
                    e.pointer       = e.pointerWindow;
                    e.pointerGlobal = tw->PhysicalToLogical(e.pointerGlobal);
                }
            }
            PushEvent(e);
        };

        // Common modifier state helper
        auto fillModifiers = [&event]() {
            bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool altDown  = (GetKeyState(VK_MENU) & 0x8000) != 0;
            // AltGr is delivered by Windows as a synthetic LCtrl+RAlt chord.
            // Strip both so AltGr-produced characters reach text consumers
            // as plain text, matching X11/Linux modifier semantics.
            bool isAltGr  = ((GetKeyState(VK_RMENU) & 0x8000) != 0) &&
                            ((GetKeyState(VK_LCONTROL) & 0x8000) != 0);

            event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            event.ctrl  = ctrlDown && !isAltGr;
            event.alt   = altDown  && !isAltGr;
            event.meta  = ((GetKeyState(VK_LWIN) & 0x8000) != 0) ||
                          ((GetKeyState(VK_RWIN) & 0x8000) != 0);
        };

        switch (msg) {
            // ===== KEYBOARD EVENTS =====
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                event.type = UCEventType::KeyDown;
                event.nativeKeyCode = static_cast<int>(wParam);
                event.virtualKey = ConvertVKToUCKey(wParam);
                event.character = 0;
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_KEYUP:
            case WM_SYSKEYUP: {
                event.type = UCEventType::KeyUp;
                event.nativeKeyCode = static_cast<int>(wParam);
                event.virtualKey = ConvertVKToUCKey(wParam);
                event.character = 0;
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_CHAR: {
                // WM_CHAR provides UTF-16 code units from TranslateMessage
                wchar_t ch = static_cast<wchar_t>(wParam);

                // Handle UTF-16 surrogate pairs for characters outside BMP
                if (IS_HIGH_SURROGATE(ch)) {
                    highSurrogate = ch;
                    return;  // Wait for low surrogate
                }

                std::wstring wstr;
                if (IS_LOW_SURROGATE(ch) && highSurrogate) {
                    wstr = {highSurrogate, ch};
                    highSurrogate = 0;
                } else {
                    highSurrogate = 0;
                    // Skip control characters (Ctrl+A..Z produce 0x01..0x1A)
                    // if (ch < 0x20 && ch != L'\t' && ch != L'\r' && ch != L'\n') {
                    if (ch < 0x20) {
                        return;
                    }
                    wstr = {ch};
                }

                std::string utf8 = Utf16ToUtf8(wstr);

                event.type = UCEventType::KeyDown;
                event.text = utf8;
                event.character = (utf8.size() == 1 && (unsigned char)utf8[0] < 128)
                                  ? utf8[0] : 0;
                event.virtualKey = UCKeys::Unknown;
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            // ===== MOUSE BUTTON EVENTS =====
            case WM_LBUTTONDOWN: {
                event.type = UCEventType::MouseDown;
                event.button = UCMouseButton::Left;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_RBUTTONDOWN: {
                event.type = UCEventType::MouseDown;
                event.button = UCMouseButton::Right;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_MBUTTONDOWN: {
                event.type = UCEventType::MouseDown;
                event.button = UCMouseButton::Middle;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_LBUTTONUP: {
                event.type = UCEventType::MouseUp;
                event.button = UCMouseButton::Left;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_RBUTTONUP: {
                event.type = UCEventType::MouseUp;
                event.button = UCMouseButton::Right;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_MBUTTONUP: {
                event.type = UCEventType::MouseUp;
                event.button = UCMouseButton::Middle;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            // ===== EXTENDED (SIDE/THUMB) BUTTON EVENTS =====
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP: {
                event.type = (msg == WM_XBUTTONDOWN) ? UCEventType::MouseDown
                                                     : UCEventType::MouseUp;
                event.button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
                                   ? UCMouseButton::Back : UCMouseButton::Forward;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            // ===== DOUBLE-CLICK EVENTS =====
            case WM_LBUTTONDBLCLK: {
                event.type = UCEventType::MouseDoubleClick;
                event.button = UCMouseButton::Left;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_RBUTTONDBLCLK: {
                event.type = UCEventType::MouseDoubleClick;
                event.button = UCMouseButton::Right;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_MBUTTONDBLCLK: {
                event.type = UCEventType::MouseDoubleClick;
                event.button = UCMouseButton::Middle;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            // ===== MOUSE MOVE =====
            case WM_MOUSEMOVE: {
                event.type = UCEventType::MouseMove;
                event.pointerWindow = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                event.pointer = event.pointerWindow;
                POINT pt = { event.pointer.x, event.pointer.y };
                ClientToScreen(hwnd, &pt);
                event.pointerGlobal = { pt.x, pt.y };
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            // ===== MOUSE WHEEL =====
            case WM_MOUSEWHEEL: {
                event.type = UCEventType::MouseWheel;
                // Wheel delta: positive = scroll up, negative = scroll down
                // Normalize to ±1 per notch (WHEEL_DELTA = 120 per notch);
                // consumers decide how many lines/pixels a notch scrolls.
                int rawDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                event.wheelDelta = rawDelta / WHEEL_DELTA;
                if (event.wheelDelta == 0) event.wheelDelta = (rawDelta > 0) ? 1 : -1;

                // WM_MOUSEWHEEL coordinates are in screen space
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                event.pointerGlobal = { pt.x, pt.y };
                ScreenToClient(hwnd, &pt);
                event.pointerWindow = { pt.x, pt.y };
                event.pointer = event.pointerWindow;
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            case WM_MOUSEHWHEEL: {
                event.type = UCEventType::MouseWheelHorizontal;
                int rawDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                event.wheelDelta = rawDelta / WHEEL_DELTA;
                if (event.wheelDelta == 0) event.wheelDelta = (rawDelta > 0) ? 1 : -1;

                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                event.pointerGlobal = { pt.x, pt.y };
                ScreenToClient(hwnd, &pt);
                event.pointerWindow = { pt.x, pt.y };
                event.pointer = event.pointerWindow;
                fillModifiers();
                pushEventScaled(event);
                return;
            }

            // ===== MOUSE ENTER/LEAVE =====
            case WM_MOUSELEAVE: {
                event.type = UCEventType::MouseLeave;
                pushEventScaled(event);
                return;
            }

            // ===== WINDOW EVENTS =====
//            case WM_SIZE: {
//                int w = LOWORD(lParam);
//                int h = HIWORD(lParam);
//                if (w > 0 && h > 0) {
//                    event.type = UCEventType::WindowResize;
//                    event.width = w;
//                    event.height = h;
//                    pushEventScaled(event);
//                }
//                return;
//            }

            // WM_PAINT intentionally not converted to a UCEvent here.
            // HandleMessage() does BeginPaint/BlitSurfaceToHDC/EndPaint to
            // reblit the cached Cairo surface. Pushing a WindowRepaint event
            // would set needsRedraw and retrigger Flush()->InvalidateRect in
            // a tight loop (burns ~50% CPU at idle).

            case WM_CLOSE: {
                event.type = UCEventType::WindowCloseRequest;
                pushEventScaled(event);
                return;
            }

            case WM_SETFOCUS: {
                debugOutput << "focus hwnd=" << hwnd << std::endl;
                event.type = UCEventType::WindowFocus;
                pushEventScaled(event);
                return;
            }

            case WM_KILLFOCUS: {
                debugOutput << "blur hwnd=" << hwnd << std::endl;
                event.type = UCEventType::WindowBlur;
                pushEventScaled(event);
                return;
            }

            default:
                break;
        }
    }

// ===== KEY CONVERSION =====

    UCKeys UltraCanvasWindowsApplication::ConvertVKToUCKey(WPARAM vk) {
        switch (vk) {
            // Special keys
            case VK_RETURN:    return UCKeys::Enter;
            case VK_ESCAPE:    return UCKeys::Escape;
            case VK_SPACE:     return UCKeys::Space;
            case VK_BACK:      return UCKeys::Backspace;
            case VK_TAB:       return UCKeys::Tab;
            case VK_DELETE:    return UCKeys::Delete;
            case VK_INSERT:    return UCKeys::Insert;

            // Arrow keys
            case VK_LEFT:      return UCKeys::Left;
            case VK_RIGHT:     return UCKeys::Right;
            case VK_UP:        return UCKeys::Up;
            case VK_DOWN:      return UCKeys::Down;

            // Navigation
            case VK_HOME:      return UCKeys::Home;
            case VK_END:       return UCKeys::End;
            case VK_PRIOR:     return UCKeys::PageUp;
            case VK_NEXT:      return UCKeys::PageDown;

            // Function keys
            case VK_F1:        return UCKeys::F1;
            case VK_F2:        return UCKeys::F2;
            case VK_F3:        return UCKeys::F3;
            case VK_F4:        return UCKeys::F4;
            case VK_F5:        return UCKeys::F5;
            case VK_F6:        return UCKeys::F6;
            case VK_F7:        return UCKeys::F7;
            case VK_F8:        return UCKeys::F8;
            case VK_F9:        return UCKeys::F9;
            case VK_F10:       return UCKeys::F10;
            case VK_F11:       return UCKeys::F11;
            case VK_F12:       return UCKeys::F12;

            // Modifier keys
            case VK_LSHIFT:    return UCKeys::LeftShift;
            case VK_RSHIFT:    return UCKeys::RightShift;
            case VK_SHIFT:     return UCKeys::LeftShift;
            case VK_LCONTROL:  return UCKeys::LeftCtrl;
            case VK_RCONTROL:  return UCKeys::RightCtrl;
            case VK_CONTROL:   return UCKeys::LeftCtrl;
            case VK_LMENU:     return UCKeys::LeftAlt;
            case VK_RMENU:     return UCKeys::RightAlt;
            case VK_MENU:      return UCKeys::LeftAlt;
            case VK_LWIN:      return UCKeys::LeftMeta;
            case VK_RWIN:      return UCKeys::RightMeta;

            // Number pad
            case VK_NUMLOCK:   return UCKeys::NumLock;
            case VK_NUMPAD0:   return UCKeys::NumPad0;
            case VK_NUMPAD1:   return UCKeys::NumPad1;
            case VK_NUMPAD2:   return UCKeys::NumPad2;
            case VK_NUMPAD3:   return UCKeys::NumPad3;
            case VK_NUMPAD4:   return UCKeys::NumPad4;
            case VK_NUMPAD5:   return UCKeys::NumPad5;
            case VK_NUMPAD6:   return UCKeys::NumPad6;
            case VK_NUMPAD7:   return UCKeys::NumPad7;
            case VK_NUMPAD8:   return UCKeys::NumPad8;
            case VK_NUMPAD9:   return UCKeys::NumPad9;
            case VK_ADD:       return UCKeys::NumPadPlus;
            case VK_SUBTRACT:  return UCKeys::NumPadMinus;
            case VK_MULTIPLY:  return UCKeys::NumPadMultiply;
            case VK_DIVIDE:    return UCKeys::NumPadDivide;
            case VK_DECIMAL:   return UCKeys::NumPadDecimal;

            // System keys
            case VK_CAPITAL:   return UCKeys::CapsLock;
            case VK_SCROLL:    return UCKeys::ScrollLock;
            case VK_PAUSE:     return UCKeys::Pause;
            case VK_SNAPSHOT:  return UCKeys::PrintScreen;
            case VK_APPS:      return UCKeys::Menu;

            default:
                // Letters A-Z: VK_A (0x41) through VK_Z (0x5A)
                if (vk >= 0x41 && vk <= 0x5A) {
                    return static_cast<UCKeys>(vk);
                }
                // Digits 0-9: VK_0 (0x30) through VK_9 (0x39)
                if (vk >= 0x30 && vk <= 0x39) {
                    return static_cast<UCKeys>(vk);
                }
                // OEM keys (keyboard punctuation)
                if (vk == VK_OEM_1)      return UCKeys::Semicolon;
                if (vk == VK_OEM_PLUS)   return UCKeys::Equal;
                if (vk == VK_OEM_COMMA)  return UCKeys::Comma;
                if (vk == VK_OEM_MINUS)  return UCKeys::Minus;
                if (vk == VK_OEM_PERIOD) return UCKeys::Period;
                if (vk == VK_OEM_2)      return UCKeys::Slash;
                if (vk == VK_OEM_3)      return UCKeys::Grave;
                if (vk == VK_OEM_4)      return UCKeys::LeftBracket;
                if (vk == VK_OEM_5)      return UCKeys::Backslash;
                if (vk == VK_OEM_6)      return UCKeys::RightBracket;
                if (vk == VK_OEM_7)      return UCKeys::Quote;
                return UCKeys::Unknown;
        }
    }

    UCMouseButton UltraCanvasWindowsApplication::ConvertWin32ButtonToUCButton(
            UINT msg, WPARAM wParam) {
        switch (msg) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
                return UCMouseButton::Left;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
                return UCMouseButton::Right;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
                return UCMouseButton::Middle;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
                return (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
                           ? UCMouseButton::Back : UCMouseButton::Forward;
            default:
                return UCMouseButton::Unknown;
        }
    }

// ===== UTF-8 CONVERSION =====

    std::wstring UltraCanvasWindowsApplication::Utf8ToUtf16(const std::string& utf8) {
        if (utf8.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        if (size <= 0) return L"";
        std::wstring result(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], size);
        return result;
    }

    std::string UltraCanvasWindowsApplication::Utf16ToUtf8(const std::wstring& utf16) {
        if (utf16.empty()) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0) return "";
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, utf16.c_str(), -1, &result[0], size, nullptr, nullptr);
        return result;
    }

    FontStyle UltraCanvasWindowsApplication::DetectSystemFontStyleNative() {
        FontStyle result;
        result.fontFamily = "Ubuntu";
        result.fontSize = 12.0f;
        return result;
    }

    FontStyle UltraCanvasWindowsApplication::DetectMonospacedFontStyleNative() {
        FontStyle result;
        result.fontFamily = "Ubuntu Mono";
        result.fontSize = 12.0f;
        return result;
    }

    void UltraCanvasWindowsApplication::LoadBundledFontsNative() {
        const std::string dir = GetBundledFontsDir();

        FcConfig* cfg = FcConfigGetCurrent();

        // Packaged builds ship no fonts.conf, so the current FcConfig usually
        // knows no font directories at all — Pango then only ever sees the
        // bundled fonts added below, and scripts they don't cover (Thai, CJK,
        // Arabic, ...) have no fallback and render as boxes. If the config is
        // empty, teach it the Windows system + per-user font directories and
        // a cache dir so the scan doesn't repeat on every launch.
        // WINDOWSFONTDIR / WINDOWSUSERFONTDIR / LOCAL_APPDATA_FONTCONFIG_CACHE
        // are fontconfig's built-in Windows keywords.
        if (cfg) {
            bool hasFontDirs = false;
            if (FcStrList* dirs = FcConfigGetFontDirs(cfg)) {
                hasFontDirs = (FcStrListNext(dirs) != nullptr);
                FcStrListDone(dirs);
            }
            if (!hasFontDirs) {
                static const char kSystemFontsConfig[] =
                    "<?xml version=\"1.0\"?>"
                    "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">"
                    "<fontconfig>"
                    "<dir>WINDOWSFONTDIR</dir>"
                    "<dir>WINDOWSUSERFONTDIR</dir>"
                    "<cachedir>LOCAL_APPDATA_FONTCONFIG_CACHE</cachedir>"
                    "</fontconfig>";
                if (!FcConfigParseAndLoadFromMemory(cfg,
                        reinterpret_cast<const FcChar8*>(kSystemFontsConfig),
                        FcFalse)) {
                    debugOutput << "UltraCanvas: failed to add Windows system "
                                   "font dirs to fontconfig" << std::endl;
                }
            }
        }

        for (size_t i = 0; i < kEmbeddedAllFontsCount; ++i) {
            std::string path = dir + kEmbeddedAllFonts[i];
            if (!std::filesystem::exists(path)) {
                debugOutput << "UltraCanvas: bundled font missing: " << path << std::endl;
                continue;
            }

            std::wstring wpath = Utf8ToUtf16(path);
            if (AddFontResourceExW(wpath.c_str(), FR_PRIVATE, 0) == 0) {
                debugOutput << "UltraCanvas: AddFontResourceExW failed for " << path << std::endl;
            }

            if (cfg) {
                if (!FcConfigAppFontAddFile(cfg,
                        reinterpret_cast<const FcChar8*>(path.c_str()))) {
                    debugOutput << "UltraCanvas: FcConfigAppFontAddFile failed for " << path << std::endl;
                }
            }
        }

        if (cfg) {
            // Materialise the FontSet so FcMatch (and therefore Pango) can
            // actually find the just-added app fonts. No-op on a system-loaded
            // config; required on a FcConfigCreate'd one.
            FcConfigBuildFonts(cfg);

            // PANGO_BACKEND env var is ignored by MSYS2's Pango, so the
            // default font map stays Win32 — which never reads our FC config.
            // Force an FC-backed map by constructing one explicitly via
            // pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT) and
            // setting it as the default. set_default takes its own ref, so
            // we drop ours afterwards.
            PangoFontMap* fcFm = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
            if (fcFm) {
                pango_cairo_font_map_set_default(PANGO_CAIRO_FONT_MAP(fcFm));
                g_object_unref(fcFm);
            } else {
                debugOutput << "UltraCanvas: pango_cairo_font_map_new_for_font_type(FT) returned null" << std::endl;
                pango_cairo_font_map_set_default(nullptr);
            }

#ifdef ULTRACANVAS_DEBUG
            // Diagnostic: how many fonts does FC know about now? If this is
            // small (<10) on a packaged build, FC likely couldn't find any
            // system fonts and only our bundled DejaVu families are available.
            // Also log a sample of family names so we can tell whether DejaVu
            // Sans was actually picked up by FC's dir scan.
            {
                FcPattern* pat = FcPatternCreate();
                FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, FC_FILE, (char*)nullptr);
                FcFontSet* fs = FcFontList(cfg, pat, os);
                debugOutput << "UltraCanvas: FC font count after init=" << (fs ? fs->nfont : -1) << std::endl;
                if (fs) {
                    int sampleN = std::min(fs->nfont, 500);
                    std::ostringstream fams;
                    for (int i = 0; i < sampleN; ++i) {
                        FcChar8* fam = nullptr;
                        if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &fam) == FcResultMatch && fam) {
                            if (i) fams << ", ";
                            fams << reinterpret_cast<const char*>(fam);
                        }
                    }
                    debugOutput << "UltraCanvas: FC family sample (" << sampleN << "/" << fs->nfont
                                << "): " << fams.str() << std::endl;
                    FcFontSetDestroy(fs);
                }
                FcObjectSetDestroy(os);
                FcPatternDestroy(pat);
            }
#endif
        }
    }
    bool UltraCanvasWindowsApplication::RegisterFontFileNative(const std::string& fontFilePath) {
        // Two registrations, because two consumers look in different places:
        // GDI (FR_PRIVATE - this process only) for anything that goes through
        // the Win32 text stack, and fontconfig for Pango, which the framework
        // pins to its FontConfig backend on Windows. Pango is the one that
        // matters for UltraCanvas text, so a GDI refusal is logged and
        // tolerated while a fontconfig refusal fails the call.
        const std::wstring wpath = Utf8ToUtf16(fontFilePath);
        if (AddFontResourceExW(wpath.c_str(), FR_PRIVATE, 0) == 0) {
            debugOutput << "UltraCanvas: AddFontResourceExW failed for "
                        << fontFilePath << std::endl;
        }

        FcConfig* cfg = FcConfigGetCurrent();
        if (!cfg) {
            debugOutput << "UltraCanvas: RegisterFontFileNative: no current "
                           "fontconfig config" << std::endl;
            return false;
        }
        if (!FcConfigAppFontAddFile(
                cfg, reinterpret_cast<const FcChar8*>(fontFilePath.c_str()))) {
            debugOutput << "UltraCanvas: FcConfigAppFontAddFile failed for "
                        << fontFilePath << std::endl;
            return false;
        }
        return true;
    }

} // namespace UltraCanvas
