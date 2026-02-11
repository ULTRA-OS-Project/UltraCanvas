// OS/MSWindows/UltraCanvasWindowsWindowImpl.cpp
// Implementations of UltraCanvasWindowsWindow methods that require
// the full UltraCanvasWindowsApplication class definition.

#ifdef _WIN32

#include "UltraCanvasWindowsApplication.h"
#include "UltraCanvasWindowsWindow.h"
#include "../../libspecific/Cairo/RenderContextCairo.h"
#include <cairo.h>
#include <iostream>
#include <iomanip>

// Define windowsx.h macros manually to avoid potential conflicts
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

namespace UltraCanvas {

// ===== HELPER: Convert Windows Virtual Key to UCKeys =====
static UCKeys ConvertVKToUCKey(WPARAM vk) {
    switch (vk) {
        case VK_RETURN:    return UCKeys::Return;
        case VK_ESCAPE:    return UCKeys::Escape;
        case VK_SPACE:     return UCKeys::Space;
        case VK_BACK:      return UCKeys::Backspace;
        case VK_TAB:       return UCKeys::Tab;
        case VK_DELETE:    return UCKeys::Delete;
        case VK_INSERT:    return UCKeys::Insert;

        case VK_LEFT:      return UCKeys::Left;
        case VK_RIGHT:     return UCKeys::Right;
        case VK_UP:        return UCKeys::Up;
        case VK_DOWN:      return UCKeys::Down;

        case VK_HOME:      return UCKeys::Home;
        case VK_END:       return UCKeys::End;
        case VK_PRIOR:     return UCKeys::PageUp;
        case VK_NEXT:      return UCKeys::PageDown;

        case VK_F1:  return UCKeys::F1;
        case VK_F2:  return UCKeys::F2;
        case VK_F3:  return UCKeys::F3;
        case VK_F4:  return UCKeys::F4;
        case VK_F5:  return UCKeys::F5;
        case VK_F6:  return UCKeys::F6;
        case VK_F7:  return UCKeys::F7;
        case VK_F8:  return UCKeys::F8;
        case VK_F9:  return UCKeys::F9;
        case VK_F10: return UCKeys::F10;
        case VK_F11: return UCKeys::F11;
        case VK_F12: return UCKeys::F12;

        case VK_SHIFT:     return UCKeys::LeftShift;
        case VK_LSHIFT:    return UCKeys::LeftShift;
        case VK_RSHIFT:    return UCKeys::RightShift;
        case VK_CONTROL:   return UCKeys::LeftCtrl;
        case VK_LCONTROL:  return UCKeys::LeftCtrl;
        case VK_RCONTROL:  return UCKeys::RightCtrl;
        case VK_MENU:      return UCKeys::LeftAlt;
        case VK_LMENU:     return UCKeys::LeftAlt;
        case VK_RMENU:     return UCKeys::RightAlt;
        case VK_LWIN:      return UCKeys::LeftMeta;
        case VK_RWIN:      return UCKeys::RightMeta;

        case VK_CAPITAL:   return UCKeys::CapsLock;
        case VK_SCROLL:    return UCKeys::ScrollLock;
        case VK_NUMLOCK:   return UCKeys::NumLock;
        case VK_PAUSE:     return UCKeys::Pause;
        case VK_SNAPSHOT:  return UCKeys::PrintScreen;
        case VK_APPS:      return UCKeys::Menu;

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
        case VK_DECIMAL:   return UCKeys::NumPadDecimal;
        case VK_ADD:       return UCKeys::NumPadAdd;
        case VK_SUBTRACT:  return UCKeys::NumPadSubtract;
        case VK_MULTIPLY:  return UCKeys::NumPadMultiply;
        case VK_DIVIDE:    return UCKeys::NumPadDivide;

        case VK_OEM_1:     return UCKeys::Semicolon;
        case VK_OEM_PLUS:  return UCKeys::Equal;
        case VK_OEM_COMMA: return UCKeys::Comma;
        case VK_OEM_MINUS: return UCKeys::Minus;
        case VK_OEM_PERIOD:return UCKeys::Period;
        case VK_OEM_2:     return UCKeys::Slash;
        case VK_OEM_3:     return UCKeys::Grave;
        case VK_OEM_4:     return UCKeys::LeftBracket;
        case VK_OEM_5:     return UCKeys::Backslash;
        case VK_OEM_6:     return UCKeys::RightBracket;
        case VK_OEM_7:     return UCKeys::Quote;

        default:
            if (vk >= 'A' && vk <= 'Z')
                return static_cast<UCKeys>(vk);
            if (vk >= '0' && vk <= '9')
                return static_cast<UCKeys>(vk);
            return UCKeys::Unknown;
    }
}

// ===== HELPER: Populate modifier key state =====
static void PopulateModifiers(UCEvent& event) {
    event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    event.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    event.meta = ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0;
}

// ===== HELPER: Populate common mouse event fields =====
static void PopulateMouseEvent(UCEvent& event, HWND hwnd, LPARAM lParam,
                               UltraCanvasWindowsWindow* win) {
    event.x = event.windowX = GET_X_LPARAM(lParam);
    event.y = event.windowY = GET_Y_LPARAM(lParam);
    POINT screenPt = { event.x, event.y };
    ClientToScreen(hwnd, &screenPt);
    event.globalX = screenPt.x;
    event.globalY = screenPt.y;
    PopulateModifiers(event);
    event.targetWindow = static_cast<void*>(win);
    event.nativeWindowHandle = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));
}

// ===== HELPER: Get UCMouseButton from WM message =====
static UCMouseButton GetMouseButtonFromMessage(UINT uMsg) {
    switch (uMsg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            return UCMouseButton::Left;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
            return UCMouseButton::Right;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
            return UCMouseButton::Middle;
        default:
            return UCMouseButton::NoneButton;
    }
}

// ===== WINDOW CREATION =====
bool UltraCanvasWindowsWindow::CreateNativeImpl() {
    auto app = UltraCanvasWindowsApplication::GetInstance();
    if (!app) return false;

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!config_.resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

    RECT rect = {0, 0, config_.width, config_.height};
    AdjustWindowRectEx(&rect, style, FALSE, 0);

    std::wstring wTitle = UltraCanvasWindowsApplication::StringToWString(config_.title);

    hwnd = CreateWindowExW(
        0,
        app->GetWindowClassName().c_str(),
        wTitle.c_str(),
        style,
        (config_.x >= 0) ? config_.x : CW_USEDEFAULT,
        (config_.y >= 0) ? config_.y : CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr, nullptr,
        app->GetHInstance(),
        nullptr
    );

    if (!hwnd) {
        std::cerr << "UltraCanvas Windows: CreateWindowExW failed: " << GetLastError() << std::endl;
        return false;
    }

    // Explicitly set title (CreateWindowExW should have set it, but verify)
    SetWindowTextW(hwnd, wTitle.c_str());

    app->RegisterWindowHandle(hwnd, this);

    // Create Cairo image surface for rendering
    int w = config_.width;
    int h = config_.height;
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;

    cairoSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(cairoSurface) != CAIRO_STATUS_SUCCESS) {
        std::cerr << "UltraCanvas Windows: Failed to create Cairo surface" << std::endl;
        cairoSurface = nullptr;
    } else {
        try {
            renderContext = std::make_unique<RenderContextCairo>(cairoSurface, w, h, false);
            std::cout << "UltraCanvas Windows: Cairo render context created (" << w << "x" << h << ")" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "UltraCanvas Windows: Failed to create RenderContextCairo: " << e.what() << std::endl;
            cairo_surface_destroy(cairoSurface);
            cairoSurface = nullptr;
        }
    }

    std::cout << "UltraCanvas Windows: Window created (HWND: " << hwnd << ")" << std::endl;
    return true;
}

void UltraCanvasWindowsWindow::DestroyNativeImpl() {
    auto app = UltraCanvasWindowsApplication::GetInstance();
    if (app && hwnd) app->UnregisterWindowHandle(hwnd);
    renderContext.reset();
    if (cairoSurface) {
        cairo_surface_destroy(cairoSurface);
        cairoSurface = nullptr;
    }
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
}

void UltraCanvasWindowsWindow::SetWindowTitleImpl(const std::string& title) {
    if (hwnd)
        SetWindowTextW(hwnd, UltraCanvasWindowsApplication::StringToWString(title).c_str());
}

void UltraCanvasWindowsWindow::ResizeCairoSurface(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!renderContext) return;

    cairo_surface_t* newSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(newSurface) != CAIRO_STATUS_SUCCESS) {
        std::cerr << "UltraCanvas Windows: Failed to resize Cairo surface" << std::endl;
        cairo_surface_destroy(newSurface);
        return;
    }

    auto* cairoCtx = dynamic_cast<RenderContextCairo*>(renderContext.get());
    if (cairoCtx) {
        cairoCtx->SetTargetSurface(newSurface, w, h);
    }

    if (cairoSurface) {
        cairo_surface_destroy(cairoSurface);
    }
    cairoSurface = newSurface;
}

// ===== WINDOW PROCEDURE =====
LRESULT CALLBACK UltraCanvasWindowsApplication::WindowProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

    auto app = GetInstance();
    UltraCanvasWindowsWindow* win = app ? app->FindWindowByHandle(hwnd) : nullptr;

    switch (uMsg) {

    // ===== WINDOW MANAGEMENT =====
    case WM_CLOSE:
        if (win) win->HandleCloseRequest();
        return 0;

    case WM_DESTROY:
        if (win) win->HandleDestroyEvent();
        return 0;

    case WM_SIZE:
        if (win && wParam != SIZE_MINIMIZED)
            win->HandleResizeEvent(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_MOVE:
        if (win) win->HandleMoveEvent(
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam)));
        return 0;

    // ===== PAINTING =====
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (win) {
            cairo_surface_t* surface = win->GetCairoSurface();
            if (surface) {
                cairo_surface_flush(surface);
                unsigned char* data = cairo_image_surface_get_data(surface);
                int width = cairo_image_surface_get_width(surface);
                int height = cairo_image_surface_get_height(surface);
                if (data && width > 0 && height > 0) {
                    BITMAPINFO bmi = {};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = width;
                    bmi.bmiHeader.biHeight = -height;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    SetDIBitsToDevice(hdc,
                        0, 0, width, height,
                        0, 0, 0, height,
                        data, &bmi, DIB_RGB_COLORS);
                }
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    // ===== FOCUS =====
    case WM_SETFOCUS:
        if (win && app) {
            UCEvent event;
            event.type = UCEventType::WindowFocus;
            event.targetWindow = static_cast<void*>(win);
            event.nativeWindowHandle = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));
            app->PushEvent(event);
        }
        return 0;

    case WM_KILLFOCUS:
        if (win && app) {
            UCEvent event;
            event.type = UCEventType::WindowBlur;
            event.targetWindow = static_cast<void*>(win);
            event.nativeWindowHandle = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));
            app->PushEvent(event);
        }
        return 0;

    // ===== MOUSE BUTTON DOWN =====
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (win && app) {
            SetCapture(hwnd);
            UCEvent event;
            event.type = UCEventType::MouseDown;
            event.button = GetMouseButtonFromMessage(uMsg);
            PopulateMouseEvent(event, hwnd, lParam, win);
            app->PushEvent(event);
        }
        return 0;

    // ===== MOUSE BUTTON UP =====
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        if (win && app) {
            ReleaseCapture();
            UCEvent event;
            event.type = UCEventType::MouseUp;
            event.button = GetMouseButtonFromMessage(uMsg);
            PopulateMouseEvent(event, hwnd, lParam, win);
            app->PushEvent(event);
        }
        return 0;

    // ===== MOUSE DOUBLE CLICK =====
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
        if (win && app) {
            UCEvent event;
            event.type = UCEventType::MouseDoubleClick;
            event.button = GetMouseButtonFromMessage(uMsg);
            PopulateMouseEvent(event, hwnd, lParam, win);
            app->PushEvent(event);
        }
        return 0;

    // ===== MOUSE MOVE =====
    case WM_MOUSEMOVE:
        if (win && app) {
            // Start tracking for WM_MOUSELEAVE if not already
            if (!win->isTrackingMouse) {
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                win->isTrackingMouse = true;

                // Push MouseEnter event
                UCEvent enterEvent;
                enterEvent.type = UCEventType::MouseEnter;
                PopulateMouseEvent(enterEvent, hwnd, lParam, win);
                app->PushEvent(enterEvent);
            }

            UCEvent event;
            event.type = UCEventType::MouseMove;
            PopulateMouseEvent(event, hwnd, lParam, win);
            // Carry held button info
            if (wParam & MK_LBUTTON) event.button = UCMouseButton::Left;
            else if (wParam & MK_RBUTTON) event.button = UCMouseButton::Right;
            else if (wParam & MK_MBUTTON) event.button = UCMouseButton::Middle;
            app->PushEvent(event);
        }
        return 0;

    // ===== MOUSE LEAVE =====
    case WM_MOUSELEAVE:
        if (win && app) {
            win->isTrackingMouse = false;
            UCEvent event;
            event.type = UCEventType::MouseLeave;
            event.targetWindow = static_cast<void*>(win);
            event.nativeWindowHandle = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));
            PopulateModifiers(event);
            app->PushEvent(event);
        }
        return 0;

    // ===== MOUSE WHEEL =====
    case WM_MOUSEWHEEL:
        if (win && app) {
            UCEvent event;
            event.type = UCEventType::MouseWheel;
            // WM_MOUSEWHEEL coords are screen-relative, convert to client
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            event.globalX = pt.x;
            event.globalY = pt.y;
            ScreenToClient(hwnd, &pt);
            event.x = event.windowX = pt.x;
            event.y = event.windowY = pt.y;
            PopulateModifiers(event);
            event.targetWindow = static_cast<void*>(win);
            event.nativeWindowHandle = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));
            int rawDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            event.wheelDelta = (rawDelta > 0) ? 5 : -5;
            app->PushEvent(event);
        }
        return 0;

    case WM_MOUSEHWHEEL:
        if (win && app) {
            UCEvent event;
            event.type = UCEventType::MouseWheelHorizontal;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            event.globalX = pt.x;
            event.globalY = pt.y;
            ScreenToClient(hwnd, &pt);
            event.x = event.windowX = pt.x;
            event.y = event.windowY = pt.y;
            PopulateModifiers(event);
            event.targetWindow = static_cast<void*>(win);
            event.nativeWindowHandle = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));
            int rawDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            event.wheelDelta = (rawDelta > 0) ? 5 : -5;
            app->PushEvent(event);
        }
        return 0;

    // ===== KEYBOARD DOWN =====
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (win && app) {
            UCEvent event;
            event.type = UCEventType::KeyDown;
            event.nativeKeyCode = static_cast<int>(wParam);
            event.virtualKey = ConvertVKToUCKey(wParam);
            PopulateModifiers(event);
            event.targetWindow = static_cast<void*>(win);
            event.nativeWindowHandle = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));
            app->PushEvent(event);
        }
        // Let DefWindowProc handle system keys (Alt+F4 etc.)
        if (uMsg == WM_SYSKEYDOWN)
            break;
        return 0;

    // ===== KEYBOARD UP =====
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (win && app) {
            UCEvent event;
            event.type = UCEventType::KeyUp;
            event.nativeKeyCode = static_cast<int>(wParam);
            event.virtualKey = ConvertVKToUCKey(wParam);
            PopulateModifiers(event);
            event.targetWindow = static_cast<void*>(win);
            event.nativeWindowHandle = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));
            app->PushEvent(event);
        }
        if (uMsg == WM_SYSKEYUP)
            break;
        return 0;

    // ===== CHARACTER INPUT =====
    case WM_CHAR: {
        if (win && app) {
            UCEvent event;
            event.type = UCEventType::KeyChar;
            PopulateModifiers(event);
            event.targetWindow = static_cast<void*>(win);
            event.nativeWindowHandle = static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));

            // Convert UTF-16 character to UTF-8
            wchar_t wc = static_cast<wchar_t>(wParam);
            char utf8[8] = {};
            int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
            if (len > 0) {
                utf8[len] = '\0';
                event.text = std::string(utf8, len);
                if (len == 1 && static_cast<unsigned char>(utf8[0]) < 128) {
                    event.character = utf8[0];
                }
            }
            app->PushEvent(event);
        }
        return 0;
    }

    } // end switch

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace UltraCanvas

#endif // _WIN32
