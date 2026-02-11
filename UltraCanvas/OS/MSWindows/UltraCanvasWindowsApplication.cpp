// OS/MSWindows/UltraCanvasWindowsApplication.cpp
// Windows platform application implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2025-12-22
// Author: UltraCanvas Framework

#ifdef _WIN32

#include "UltraCanvasWindowsApplication.h"
#include "UltraCanvasWindowsWindow.h"
#include <iostream>
#include <cstring>

// Link required libraries
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace UltraCanvas {

// ===== STATIC INSTANCE =====
UltraCanvasWindowsApplication* UltraCanvasWindowsApplication::instance = nullptr;

// Window class name
static const wchar_t* ULTRACANVAS_WINDOW_CLASS = L"UltraCanvasWindowClass";

// ===== CONSTRUCTOR & DESTRUCTOR =====
UltraCanvasWindowsApplication::UltraCanvasWindowsApplication()
    : hInstance(nullptr)
    , windowClassAtom(0)
    , windowClassRegistered(false)
    , d2dFactory(nullptr)
    , dwriteFactory(nullptr)
    , wicFactory(nullptr)
    , deltaTime(0.0)
    , targetFPS(60)
    , vsyncEnabled(true)
{
    instance = this;
    
    // Initialize performance counter
    QueryPerformanceFrequency(&performanceFrequency);
    QueryPerformanceCounter(&lastFrameTime);
    
    std::cout << "UltraCanvas: Windows Application created" << std::endl;
}

UltraCanvasWindowsApplication::~UltraCanvasWindowsApplication() {
    std::cout << "UltraCanvas: Windows Application destroying..." << std::endl;
    
    // Cleanup windows
    {
        std::lock_guard<std::mutex> lock(windowMapMutex);
        windowMap.clear();
    }
    
    // Shutdown systems
    ShutdownDirect2D();
    UnregisterWindowClass();
    ShutdownCOM();
    
    instance = nullptr;
    std::cout << "UltraCanvas: Windows Application destroyed" << std::endl;
}

// ===== PLATFORM INITIALIZATION =====
bool UltraCanvasWindowsApplication::InitializeNative() {
    if (initialized) {
        std::cout << "UltraCanvas: Already initialized" << std::endl;
        return true;
    }

    std::cout << "UltraCanvas: Initializing Windows Application..." << std::endl;

    // Get application instance
    hInstance = GetModuleHandle(nullptr);
    if (!hInstance) {
        std::cerr << "UltraCanvas: Failed to get module handle" << std::endl;
        return false;
    }

    // Initialize COM
    if (!InitializeCOM()) {
        std::cerr << "UltraCanvas: Failed to initialize COM" << std::endl;
        return false;
    }

    // Initialize Direct2D
    if (!InitializeDirect2D()) {
        std::cerr << "UltraCanvas: Failed to initialize Direct2D" << std::endl;
        ShutdownCOM();
        return false;
    }

    // Register window class
    if (!RegisterWindowClass()) {
        std::cerr << "UltraCanvas: Failed to register window class" << std::endl;
        ShutdownDirect2D();
        ShutdownCOM();
        return false;
    }

    // Initialize mouse click info for double-click detection
    mouseClickInfo.doubleClickTime = GetDoubleClickTime();
    mouseClickInfo.doubleClickDistance = GetSystemMetrics(SM_CXDOUBLECLK);

    initialized = true;
    std::cout << "UltraCanvas: Windows Application initialized successfully" << std::endl;
    
    return true;
}

bool UltraCanvasWindowsApplication::InitializeCOM() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::cerr << "UltraCanvas: CoInitializeEx failed with HRESULT: " << hr << std::endl;
        return false;
    }
    std::cout << "UltraCanvas: COM initialized" << std::endl;
    return true;
}

void UltraCanvasWindowsApplication::ShutdownCOM() {
    CoUninitialize();
    std::cout << "UltraCanvas: COM shutdown" << std::endl;
}

bool UltraCanvasWindowsApplication::InitializeDirect2D() {
    HRESULT hr;

    // Create Direct2D factory
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory);
    if (FAILED(hr)) {
        std::cerr << "UltraCanvas: D2D1CreateFactory failed with HRESULT: " << hr << std::endl;
        return false;
    }
    std::cout << "UltraCanvas: Direct2D factory created" << std::endl;

    // Create DirectWrite factory
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&dwriteFactory)
    );
    if (FAILED(hr)) {
        std::cerr << "UltraCanvas: DWriteCreateFactory failed with HRESULT: " << hr << std::endl;
        d2dFactory->Release();
        d2dFactory = nullptr;
        return false;
    }
    std::cout << "UltraCanvas: DirectWrite factory created" << std::endl;

    // Create WIC factory
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&wicFactory)
    );
    if (FAILED(hr)) {
        std::cerr << "UltraCanvas: WIC factory creation failed with HRESULT: " << hr << std::endl;
        dwriteFactory->Release();
        dwriteFactory = nullptr;
        d2dFactory->Release();
        d2dFactory = nullptr;
        return false;
    }
    std::cout << "UltraCanvas: WIC factory created" << std::endl;

    return true;
}

void UltraCanvasWindowsApplication::ShutdownDirect2D() {
    if (wicFactory) {
        wicFactory->Release();
        wicFactory = nullptr;
    }
    if (dwriteFactory) {
        dwriteFactory->Release();
        dwriteFactory = nullptr;
    }
    if (d2dFactory) {
        d2dFactory->Release();
        d2dFactory = nullptr;
    }
    std::cout << "UltraCanvas: Direct2D shutdown" << std::endl;
}

bool UltraCanvasWindowsApplication::RegisterWindowClass() {
    if (windowClassRegistered) {
        return true;
    }

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(LONG_PTR);
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;  // No background brush - we paint everything
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = ULTRACANVAS_WINDOW_CLASS;
    wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    windowClassAtom = RegisterClassExW(&wcex);
    if (!windowClassAtom) {
        DWORD error = GetLastError();
        std::cerr << "UltraCanvas: RegisterClassExW failed with error: " << error << std::endl;
        return false;
    }

    windowClassRegistered = true;
    std::cout << "UltraCanvas: Window class registered" << std::endl;
    return true;
}

void UltraCanvasWindowsApplication::UnregisterWindowClass() {
    if (windowClassRegistered && hInstance) {
        UnregisterClassW(ULTRACANVAS_WINDOW_CLASS, hInstance);
        windowClassRegistered = false;
        windowClassAtom = 0;
        std::cout << "UltraCanvas: Window class unregistered" << std::endl;
    }
}

const wchar_t* UltraCanvasWindowsApplication::GetWindowClassName() const {
    return ULTRACANVAS_WINDOW_CLASS;
}

// ===== MAIN LOOP =====
void UltraCanvasWindowsApplication::RunNative() {
    if (!initialized) {
        std::cerr << "UltraCanvas: Application not initialized" << std::endl;
        return;
    }

    running = true;
    std::cout << "UltraCanvas: Entering main loop..." << std::endl;

    double targetFrameTime = 1.0 / targetFPS;

    while (running) {
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        deltaTime = static_cast<double>(currentTime.QuadPart - lastFrameTime.QuadPart) / 
                    static_cast<double>(performanceFrequency.QuadPart);

        // Process Windows messages
        ProcessWindowsMessages();

        // Process event queue
        CollectAndProcessNativeEvents();

        // Run user callbacks
        RunInEventLoop();

        // Render all windows
        {
            std::lock_guard<std::mutex> lock(windowMapMutex);
            for (auto& pair : windowMap) {
                if (pair.second && pair.second->IsVisible()) {
                    pair.second->RenderFrame();
                }
            }
        }

        // Frame rate limiting
        if (!vsyncEnabled && targetFPS > 0) {
            LARGE_INTEGER endTime;
            QueryPerformanceCounter(&endTime);
            double frameTime = static_cast<double>(endTime.QuadPart - currentTime.QuadPart) / 
                               static_cast<double>(performanceFrequency.QuadPart);
            
            if (frameTime < targetFrameTime) {
                DWORD sleepMs = static_cast<DWORD>((targetFrameTime - frameTime) * 1000.0);
                if (sleepMs > 0) {
                    Sleep(sleepMs);
                }
            }
        }

        QueryPerformanceCounter(&lastFrameTime);
    }

    std::cout << "UltraCanvas: Exited main loop" << std::endl;
}

void UltraCanvasWindowsApplication::ProcessWindowsMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            running = false;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void UltraCanvasWindowsApplication::ExitNative() {
    running = false;
    PostQuitMessage(0);
    std::cout << "UltraCanvas: Exit requested" << std::endl;
}

// ===== WINDOW MANAGEMENT =====
void UltraCanvasWindowsApplication::RegisterWindow(HWND hwnd, UltraCanvasWindowsWindow* window) {
    std::lock_guard<std::mutex> lock(windowMapMutex);
    windowMap[hwnd] = window;
    std::cout << "UltraCanvas: Window registered (HWND: " << hwnd << ")" << std::endl;
}

void UltraCanvasWindowsApplication::UnregisterWindow(HWND hwnd) {
    std::lock_guard<std::mutex> lock(windowMapMutex);
    windowMap.erase(hwnd);
    std::cout << "UltraCanvas: Window unregistered (HWND: " << hwnd << ")" << std::endl;
}

UltraCanvasWindowsWindow* UltraCanvasWindowsApplication::GetWindowFromHWND(HWND hwnd) {
    std::lock_guard<std::mutex> lock(windowMapMutex);
    auto it = windowMap.find(hwnd);
    return (it != windowMap.end()) ? it->second : nullptr;
}

// ===== EVENT HANDLING =====
void UltraCanvasWindowsApplication::PostEvent(const UCEvent& event) {
    std::lock_guard<std::mutex> lock(eventQueueMutex);
    eventQueue.push(event);
}

void UltraCanvasWindowsApplication::CollectAndProcessNativeEvents() {
    std::queue<UCEvent> eventsToProcess;
    
    {
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        std::swap(eventsToProcess, eventQueue);
    }

    while (!eventsToProcess.empty()) {
        UCEvent event = eventsToProcess.front();
        eventsToProcess.pop();
        
        // Find window and dispatch event
        HWND hwnd = reinterpret_cast<HWND>(event.windowId);
        UltraCanvasWindowsWindow* window = GetWindowFromHWND(hwnd);
        if (window) {
            window->OnEvent(event);
        }
    }
}

// ===== WINDOW PROCEDURE =====
LRESULT CALLBACK UltraCanvasWindowsApplication::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    UltraCanvasWindowsApplication* app = GetInstance();
    if (!app) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    UltraCanvasWindowsWindow* window = app->GetWindowFromHWND(hwnd);

    switch (message) {
        case WM_CREATE:
            return 0;

        case WM_SIZE: {
            if (window) {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                window->HandleResizeEvent(width, height);
            }
            return 0;
        }

        case WM_MOVE: {
            if (window) {
                int x = static_cast<short>(LOWORD(lParam));
                int y = static_cast<short>(HIWORD(lParam));
                window->HandleMoveEvent(x, y);
            }
            return 0;
        }

        case WM_PAINT: {
            if (window) {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                window->RenderFrame();
                EndPaint(hwnd, &ps);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;  // We handle background painting

        case WM_CLOSE: {
            if (window) {
                window->HandleCloseRequest();
            }
            return 0;
        }

        case WM_DESTROY: {
            if (window) {
                window->HandleDestroyEvent();
            }
            
            // Check if this is the last window
            if (app) {
                std::lock_guard<std::mutex> lock(app->windowMapMutex);
                if (app->windowMap.size() <= 1) {
                    PostQuitMessage(0);
                }
            }
            return 0;
        }

        case WM_SETFOCUS: {
            if (window) {
                UCEvent event;
                event.type = UCEventType::WindowFocus;
                event.windowId = reinterpret_cast<long>(hwnd);
                event.timestamp = GetTickCount64();
                window->OnEvent(event);
            }
            return 0;
        }

        case WM_KILLFOCUS: {
            if (window) {
                UCEvent event;
                event.type = UCEventType::WindowBlur;
                event.windowId = reinterpret_cast<long>(hwnd);
                event.timestamp = GetTickCount64();
                window->OnEvent(event);
            }
            return 0;
        }

        // Mouse events
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            SetCapture(hwnd);
            UCEvent event = app->ConvertWindowsMessage(hwnd, message, wParam, lParam);
            if (window) window->OnEvent(event);
            return 0;
        }

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            ReleaseCapture();
            UCEvent event = app->ConvertWindowsMessage(hwnd, message, wParam, lParam);
            if (window) window->OnEvent(event);
            return 0;
        }

        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK: {
            UCEvent event = app->ConvertWindowsMessage(hwnd, message, wParam, lParam);
            event.type = UCEventType::MouseDoubleClick;
            if (window) window->OnEvent(event);
            return 0;
        }

        case WM_MOUSEMOVE: {
            UCEvent event = app->ConvertWindowsMessage(hwnd, message, wParam, lParam);
            if (window) window->OnEvent(event);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            UCEvent event;
            event.type = UCEventType::MouseWheel;
            event.windowId = reinterpret_cast<long>(hwnd);
            event.timestamp = GetTickCount64();
            
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &pt);
            event.x = pt.x;
            event.y = pt.y;
            
            event.wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            event.wheelDirection = (event.wheelDelta > 0) ? 1 : -1;
            
            if (window) window->OnEvent(event);
            return 0;
        }

        case WM_MOUSEHWHEEL: {
            UCEvent event;
            event.type = UCEventType::MouseWheel;
            event.windowId = reinterpret_cast<long>(hwnd);
            event.timestamp = GetTickCount64();
            
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &pt);
            event.x = pt.x;
            event.y = pt.y;
            
            event.wheelDeltaX = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            
            if (window) window->OnEvent(event);
            return 0;
        }

        // Keyboard events
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            UCEvent event;
            event.type = UCEventType::KeyDown;
            event.windowId = reinterpret_cast<long>(hwnd);
            event.timestamp = GetTickCount64();
            event.key = app->ConvertVirtualKeyCode(wParam);
            event.keyRepeat = (lParam & 0x40000000) != 0;
            event.modifiers = 0;
            if (GetKeyState(VK_SHIFT) & 0x8000) event.modifiers |= UCModifier::Shift;
            if (GetKeyState(VK_CONTROL) & 0x8000) event.modifiers |= UCModifier::Control;
            if (GetKeyState(VK_MENU) & 0x8000) event.modifiers |= UCModifier::Alt;
            
            if (window) window->OnEvent(event);
            return 0;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            UCEvent event;
            event.type = UCEventType::KeyUp;
            event.windowId = reinterpret_cast<long>(hwnd);
            event.timestamp = GetTickCount64();
            event.key = app->ConvertVirtualKeyCode(wParam);
            
            if (window) window->OnEvent(event);
            return 0;
        }

        case WM_CHAR: {
            UCEvent event;
            event.type = UCEventType::TextInput;
            event.windowId = reinterpret_cast<long>(hwnd);
            event.timestamp = GetTickCount64();
            
            // Convert UTF-16 to UTF-8
            wchar_t wc = static_cast<wchar_t>(wParam);
            if (wc >= 0x20 || wc == '\t' || wc == '\n' || wc == '\r') {
                char utf8[4] = {0};
                WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, sizeof(utf8), nullptr, nullptr);
                event.text = utf8;
                if (window) window->OnEvent(event);
            }
            return 0;
        }

        case WM_GETMINMAXINFO: {
            if (window) {
                MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
                const WindowConfig& config = window->GetConfig();
                
                if (config.minWidth > 0 || config.minHeight > 0) {
                    RECT rect = {0, 0, config.minWidth, config.minHeight};
                    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
                    mmi->ptMinTrackSize.x = rect.right - rect.left;
                    mmi->ptMinTrackSize.y = rect.bottom - rect.top;
                }
                
                if (config.maxWidth > 0 || config.maxHeight > 0) {
                    RECT rect = {0, 0, config.maxWidth, config.maxHeight};
                    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
                    mmi->ptMaxTrackSize.x = rect.right - rect.left;
                    mmi->ptMaxTrackSize.y = rect.bottom - rect.top;
                }
            }
            return 0;
        }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ===== EVENT CONVERSION =====
UCEvent UltraCanvasWindowsApplication::ConvertWindowsMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    UCEvent event;
    event.windowId = reinterpret_cast<long>(hwnd);
    event.timestamp = GetTickCount64();
    event.x = GET_X_LPARAM(lParam);
    event.y = GET_Y_LPARAM(lParam);

    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            event.type = UCEventType::MouseDown;
            event.button = ConvertMouseButton(message, wParam);
            break;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            event.type = UCEventType::MouseUp;
            event.button = ConvertMouseButton(message, wParam);
            break;

        case WM_MOUSEMOVE:
            event.type = UCEventType::MouseMove;
            break;

        default:
            event.type = UCEventType::Unknown;
            break;
    }

    // Check modifiers
    event.modifiers = 0;
    if (wParam & MK_SHIFT) event.modifiers |= UCModifier::Shift;
    if (wParam & MK_CONTROL) event.modifiers |= UCModifier::Control;
    if (GetKeyState(VK_MENU) & 0x8000) event.modifiers |= UCModifier::Alt;

    return event;
}

UCMouseButton UltraCanvasWindowsApplication::ConvertMouseButton(UINT message, WPARAM wParam) {
    switch (message) {
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
            return UCMouseButton::None;
    }
}

UCKey UltraCanvasWindowsApplication::ConvertVirtualKeyCode(WPARAM vkCode) {
    switch (vkCode) {
        case VK_BACK: return UCKey::Backspace;
        case VK_TAB: return UCKey::Tab;
        case VK_RETURN: return UCKey::Enter;
        case VK_ESCAPE: return UCKey::Escape;
        case VK_SPACE: return UCKey::Space;
        case VK_DELETE: return UCKey::Delete;
        case VK_INSERT: return UCKey::Insert;
        case VK_HOME: return UCKey::Home;
        case VK_END: return UCKey::End;
        case VK_PRIOR: return UCKey::PageUp;
        case VK_NEXT: return UCKey::PageDown;
        case VK_LEFT: return UCKey::Left;
        case VK_RIGHT: return UCKey::Right;
        case VK_UP: return UCKey::Up;
        case VK_DOWN: return UCKey::Down;
        case VK_SHIFT: return UCKey::Shift;
        case VK_CONTROL: return UCKey::Control;
        case VK_MENU: return UCKey::Alt;
        case VK_F1: return UCKey::F1;
        case VK_F2: return UCKey::F2;
        case VK_F3: return UCKey::F3;
        case VK_F4: return UCKey::F4;
        case VK_F5: return UCKey::F5;
        case VK_F6: return UCKey::F6;
        case VK_F7: return UCKey::F7;
        case VK_F8: return UCKey::F8;
        case VK_F9: return UCKey::F9;
        case VK_F10: return UCKey::F10;
        case VK_F11: return UCKey::F11;
        case VK_F12: return UCKey::F12;
        
        // Alphanumeric keys
        default:
            if (vkCode >= 'A' && vkCode <= 'Z') {
                return static_cast<UCKey>(static_cast<int>(UCKey::A) + (vkCode - 'A'));
            }
            if (vkCode >= '0' && vkCode <= '9') {
                return static_cast<UCKey>(static_cast<int>(UCKey::Num0) + (vkCode - '0'));
            }
            return UCKey::Unknown;
    }
}

// ===== CLIPBOARD SUPPORT =====
std::string UltraCanvasWindowsApplication::GetClipboardText() {
    std::string result;
    
    if (!OpenClipboard(nullptr)) {
        return result;
    }
    
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
        if (pszText) {
            result = WStringToString(pszText);
            GlobalUnlock(hData);
        }
    }
    
    CloseClipboard();
    return result;
}

void UltraCanvasWindowsApplication::SetClipboardText(const std::string& text) {
    if (!OpenClipboard(nullptr)) {
        return;
    }
    
    EmptyClipboard();
    
    std::wstring wtext = StringToWString(text);
    size_t size = (wtext.length() + 1) * sizeof(wchar_t);
    
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hMem) {
        wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
        if (pMem) {
            memcpy(pMem, wtext.c_str(), size);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    
    CloseClipboard();
}

// ===== UTILITY FUNCTIONS =====
std::wstring UltraCanvasWindowsApplication::StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &result[0], size);
    return result;
}

std::string UltraCanvasWindowsApplication::WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.length()), &result[0], size, nullptr, nullptr);
    return result;
}

} // namespace UltraCanvas

#endif // _WIN32
