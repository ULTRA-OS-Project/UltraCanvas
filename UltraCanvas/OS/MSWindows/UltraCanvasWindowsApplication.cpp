// OS/MSWindows/UltraCanvasWindowsApplication.cpp
// Complete Windows application implementation with all methods
// Version: 1.1.1
// Last Modified: 2026-04-17
// Author: UltraCanvas Framework

#include "../../include/UltraCanvasApplication.h"
#include "../../include/UltraCanvasWindow.h"
#include "UltraCanvasWindowsApplication.h"
#include <iostream>
#include <algorithm>
#include "UltraCanvasDebug.h"

// Link against IME library
#pragma comment(lib, "imm32.lib")

namespace UltraCanvas {

    UltraCanvasWindowsApplication* UltraCanvasWindowsApplication::instance = nullptr;

// ===== CONSTRUCTOR =====
    UltraCanvasWindowsApplication::UltraCanvasWindowsApplication()
            : hInstance(nullptr)
            , windowClassAtom(0) {
        instance = this;
        debugOutput << "UltraCanvas: Windows Application created" << std::endl;
    }

// ===== INITIALIZATION =====
    bool UltraCanvasWindowsApplication::InitializeNative() {
        if (initialized) {
            debugOutput << "UltraCanvas: Already initialized" << std::endl;
            return true;
        }

        debugOutput << "UltraCanvas: Initializing Windows Application..." << std::endl;

        try {
            // STEP 1: Get module handle
            hInstance = GetModuleHandle(nullptr);
            if (!hInstance) {
                debugOutput << "UltraCanvas: GetModuleHandle failed" << std::endl;
                return false;
            }

            // STEP 2: Initialize COM/OLE for drag-drop and file dialogs
            HRESULT hr = OleInitialize(nullptr);
            if (FAILED(hr)) {
                debugOutput << "UltraCanvas: OleInitialize failed: 0x" << std::hex << hr << std::endl;
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
                debugOutput << "UltraCanvas: Failed to register window class" << std::endl;
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
            debugOutput << "UltraCanvas: Exception during initialization: " << e.what() << std::endl;
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
            debugOutput << "UltraCanvas: RegisterClassExW failed: " << GetLastError() << std::endl;
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

        // 2. Compute wait timeout from timer system
        auto timeout = GetTimeUntilNextTimer();
        DWORD waitMs = (timeout == std::chrono::milliseconds::max())
                       ? INFINITE
                       : static_cast<DWORD>(timeout.count());

        // 3. Wait for messages, wakeup event, or timer expiry
        if (wakeupEvent) {
            MsgWaitForMultipleObjectsEx(1, &wakeupEvent, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        } else {
            MsgWaitForMultipleObjectsEx(0, nullptr, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        // WAIT_OBJECT_0     = wakeupEvent signaled (auto-reset clears it)
        // WAIT_OBJECT_0 + 1 = Win32 message available
        // WAIT_TIMEOUT      = timer expired
        // All cases: return to main loop which calls ProcessTimers + ProcessEvents
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

        // Let the application convert messages to UCEvents
        if (instance) {
            instance->ProcessWindowMessage(hwnd, msg, wParam, lParam);
        }

        // Let the window handle its own messages
        if (window) {
            return window->HandleMessage(hwnd, msg, wParam, lParam);
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

// ===== MOUSE CAPTURE =====

    void UltraCanvasWindowsApplication::CaptureMouseNative() {
        if (!focusedWindow) {
            debugOutput << "UltraCanvas: Cannot capture mouse - no focused window" << std::endl;
            return;
        }

        HWND hwnd = focusedWindow->GetNativeHandle();
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
        event.targetWindow = FindWindow(hwnd);

        // Common modifier state helper
        auto fillModifiers = [&event]() {
            event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            event.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            event.meta = ((GetKeyState(VK_LWIN) & 0x8000) != 0) ||
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
                PushEvent(event);
                return;
            }

            case WM_KEYUP:
            case WM_SYSKEYUP: {
                event.type = UCEventType::KeyUp;
                event.nativeKeyCode = static_cast<int>(wParam);
                event.virtualKey = ConvertVKToUCKey(wParam);
                event.character = 0;
                fillModifiers();
                PushEvent(event);
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
                PushEvent(event);
                return;
            }

            // ===== MOUSE BUTTON EVENTS =====
            case WM_LBUTTONDOWN: {
                event.type = UCEventType::MouseDown;
                event.button = UCMouseButton::Left;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            case WM_RBUTTONDOWN: {
                event.type = UCEventType::MouseDown;
                event.button = UCMouseButton::Right;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            case WM_MBUTTONDOWN: {
                event.type = UCEventType::MouseDown;
                event.button = UCMouseButton::Middle;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            case WM_LBUTTONUP: {
                event.type = UCEventType::MouseUp;
                event.button = UCMouseButton::Left;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            case WM_RBUTTONUP: {
                event.type = UCEventType::MouseUp;
                event.button = UCMouseButton::Right;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            case WM_MBUTTONUP: {
                event.type = UCEventType::MouseUp;
                event.button = UCMouseButton::Middle;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            // ===== DOUBLE-CLICK EVENTS =====
            case WM_LBUTTONDBLCLK: {
                event.type = UCEventType::MouseDoubleClick;
                event.button = UCMouseButton::Left;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            case WM_RBUTTONDBLCLK: {
                event.type = UCEventType::MouseDoubleClick;
                event.button = UCMouseButton::Right;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            case WM_MBUTTONDBLCLK: {
                event.type = UCEventType::MouseDoubleClick;
                event.button = UCMouseButton::Middle;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            // ===== MOUSE MOVE =====
            case WM_MOUSEMOVE: {
                event.type = UCEventType::MouseMove;
                event.x = event.windowX = GET_X_LPARAM(lParam);
                event.y = event.windowY = GET_Y_LPARAM(lParam);
                POINT pt = {event.x, event.y};
                ClientToScreen(hwnd, &pt);
                event.globalX = pt.x;
                event.globalY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            // ===== MOUSE WHEEL =====
            case WM_MOUSEWHEEL: {
                event.type = UCEventType::MouseWheel;
                // Wheel delta: positive = scroll up, negative = scroll down
                // Normalize to match Linux scale (±5 per notch)
                int rawDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                event.wheelDelta = (rawDelta > 0) ? 5 : -5;

                // WM_MOUSEWHEEL coordinates are in screen space
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                event.globalX = pt.x;
                event.globalY = pt.y;
                ScreenToClient(hwnd, &pt);
                event.x = event.windowX = pt.x;
                event.y = event.windowY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            case WM_MOUSEHWHEEL: {
                event.type = UCEventType::MouseWheelHorizontal;
                int rawDelta = GET_WHEEL_DELTA_WPARAM(wParam);
                event.wheelDelta = (rawDelta > 0) ? 5 : -5;

                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                event.globalX = pt.x;
                event.globalY = pt.y;
                ScreenToClient(hwnd, &pt);
                event.x = event.windowX = pt.x;
                event.y = event.windowY = pt.y;
                fillModifiers();
                PushEvent(event);
                return;
            }

            // ===== MOUSE ENTER/LEAVE =====
            case WM_MOUSELEAVE: {
                event.type = UCEventType::MouseLeave;
                PushEvent(event);
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
//                    PushEvent(event);
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
                PushEvent(event);
                return;
            }

            case WM_SETFOCUS: {
                debugOutput << "focus hwnd=" << hwnd << std::endl;
                event.type = UCEventType::WindowFocus;
                PushEvent(event);
                return;
            }

            case WM_KILLFOCUS: {
                debugOutput << "blur hwnd=" << hwnd << std::endl;
                event.type = UCEventType::WindowBlur;
                PushEvent(event);
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
        result.fontFamily = "Sans";
        result.fontSize = 12.0f;
        return result;
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(NONCLIENTMETRICSW);

        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            std::wstring wfontName(ncm.lfMessageFont.lfFaceName);
            result.fontFamily = Utf16ToUtf8(wfontName);

            // lfHeight is negative for character height in logical units
            int height = ncm.lfMessageFont.lfHeight;
            if (height < 0) height = -height;
            result.fontSize = static_cast<float>(height) * 72.0f / 96.0f;
            if (result.fontSize <= 0) result.fontSize = 12.0f;

//            if (ncm.lfMessageFont.lfWeight >= FW_BOLD) {
//                result.fontWeight = FontWeight::Bold;
//            }
//            if (ncm.lfMessageFont.lfItalic) {
//                result.fontSlant = FontSlant::Italic;
//            }
        } else {
            result.fontFamily = "Segoe UI";
            result.fontSize = 12.0f;
        }

        return result;
    }

    FontStyle UltraCanvasWindowsApplication::DetectMonospacedFontStyleNative() {
        FontStyle result;

        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(NONCLIENTMETRICSW);

        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            // Use the status font size as reference for monospaced font
            int height = ncm.lfStatusFont.lfHeight;
            if (height < 0) height = -height;
            result.fontSize = static_cast<float>(height) * 72.0f / 96.0f;
            if (result.fontSize <= 0) result.fontSize = 12.0f;
        }

        // Try Consolas first (Vista+, universally available)
        // Then Cascadia Mono (Windows 10/11 with Terminal installed)
        // Courier New as last resort — always present, but ugly
        static const char *candidates[] = {
                "Cascadia Mono", "Cascadia Code",
                "Consolas", "Lucida Console",
                "Courier New", nullptr
        };

        PangoFontMap *fontMap = pango_cairo_font_map_get_default();
        if (!fontMap) {
            result.fontFamily = "Courier New";
            return result;
        }

        PangoContext* ctx = pango_font_map_create_context(fontMap);
        if (!ctx) {
            result.fontFamily = "Courier New";
            return result;
        }

        for (int i = 0; candidates[i]; ++i) {
            PangoFontDescription *desc =
                    pango_font_description_from_string(candidates[i]);
            PangoFont *font = pango_font_map_load_font(
                    fontMap, ctx, desc);

            pango_font_description_free(desc);

            if (font) {
                result.fontFamily = candidates[i];
                g_object_unref(font);
                break;
            } else {
                g_object_unref(font);
            }
        }
        g_object_unref(ctx);

        if (result.fontFamily.empty()) {
            result.fontFamily = "Courier New"; // fallback
        }
        return result;
    }
} // namespace UltraCanvas
