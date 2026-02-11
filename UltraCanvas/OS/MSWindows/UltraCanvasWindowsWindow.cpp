// OS/MSWindows/UltraCanvasWindowsWindow.cpp
// Windows platform window implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2025-12-22
// Author: UltraCanvas Framework

#ifdef _WIN32

#include "UltraCanvasWindowsWindow.h"
#include "UltraCanvasWindowsApplication.h"
#include <iostream>
#include <cstring>

namespace UltraCanvas {

// ===== CONSTRUCTOR & DESTRUCTOR =====
UltraCanvasWindowsWindow::UltraCanvasWindowsWindow()
    : hwnd(nullptr)
    , isTrackingMouse(false)
{
    std::cout << "UltraCanvas Windows: Window constructor completed" << std::endl;
}

UltraCanvasWindowsWindow::~UltraCanvasWindowsWindow() {
    std::cout << "UltraCanvas Windows: Window destructor called" << std::endl;
    DestroyNative();
}

// ===== WINDOW CREATION =====
bool UltraCanvasWindowsWindow::CreateNative() {
    if (_created) {
        std::cout << "UltraCanvas Windows: Window already created" << std::endl;
        return true;
    }

    auto application = UltraCanvasWindowsApplication::GetInstance();
    if (!application || !application->IsInitialized()) {
        std::cerr << "UltraCanvas Windows: Cannot create window - application not ready" << std::endl;
        return false;
    }

    std::cout << "UltraCanvas Windows: Creating window..." << std::endl;

    // Create the HWND
    if (!CreateHWND()) {
        std::cerr << "UltraCanvas Windows: Failed to create HWND" << std::endl;
        return false;
    }

    // Create render target
    if (!CreateRenderTarget()) {
        std::cerr << "UltraCanvas Windows: Failed to create render target" << std::endl;
        DestroyWindow(hwnd);
        hwnd = nullptr;
        return false;
    }

    // Register window with application
    application->RegisterWindowHandle(hwnd, this);

    _created = true;
    std::cout << "UltraCanvas Windows: Window created successfully (HWND: " << hwnd << ")" << std::endl;

    return true;
}

bool UltraCanvasWindowsWindow::CreateHWND() {
    auto application = UltraCanvasWindowsApplication::GetInstance();
    if (!application) {
        return false;
    }

    // Calculate window rectangle including frame
    RECT rect = {0, 0, config_.width, config_.height};
    DWORD style = GetWindowStyle();
    DWORD exStyle = GetWindowExStyle();
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;

    // Convert title to wide string
    std::wstring wTitle = UltraCanvasWindowsApplication::StringToWString(config_.title);

    // Create the window
    hwnd = CreateWindowExW(
        exStyle,
        application->GetWindowClassName(),
        wTitle.c_str(),
        style,
        config_.x != 0 ? config_.x : CW_USEDEFAULT,
        config_.y != 0 ? config_.y : CW_USEDEFAULT,
        windowWidth,
        windowHeight,
        nullptr,    // No parent
        nullptr,    // No menu
        application->GetHInstance(),
        nullptr     // No additional data
    );

    if (!hwnd) {
        DWORD error = GetLastError();
        std::cerr << "UltraCanvas Windows: CreateWindowExW failed with error: " << error << std::endl;
        return false;
    }

    std::cout << "UltraCanvas Windows: HWND created: " << hwnd << std::endl;
    return true;
}

DWORD UltraCanvasWindowsWindow::GetWindowStyle() const {
    DWORD style = WS_OVERLAPPEDWINDOW;
    
    if (!config_.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    
    if (!config_.hasCloseButton) {
        style &= ~WS_SYSMENU;
    }
    
    return style;
}

DWORD UltraCanvasWindowsWindow::GetWindowExStyle() const {
    DWORD exStyle = 0;
    
    if (config_.topMost) {
        exStyle |= WS_EX_TOPMOST;
    }
    
    return exStyle;
}

bool UltraCanvasWindowsWindow::CreateRenderTarget() {
    auto application = UltraCanvasWindowsApplication::GetInstance();
    if (!application || !hwnd) {
        return false;
    }

    try {
        renderContext = std::make_unique<RenderContextDirect2D>(
            hwnd,
            config_.width,
            config_.height,
            application->GetD2DFactory(),
            application->GetDWriteFactory(),
            application->GetWICFactory()
        );
        
        std::cout << "UltraCanvas Windows: Render context created successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "UltraCanvas Windows: Failed to create render context: " << e.what() << std::endl;
        return false;
    }
}

void UltraCanvasWindowsWindow::DestroyRenderTarget() {
    renderContext.reset();
    std::cout << "UltraCanvas Windows: Render target destroyed" << std::endl;
}

void UltraCanvasWindowsWindow::UpdateRenderTarget(int width, int height) {
    std::lock_guard<std::mutex> lock(renderMutex);
    
    if (renderContext) {
        renderContext->Resize(width, height);
        std::cout << "UltraCanvas Windows: Render target resized to " << width << "x" << height << std::endl;
    }
}

void UltraCanvasWindowsWindow::DestroyNative() {
    std::cout << "UltraCanvas Windows: Destroying window..." << std::endl;

    auto application = UltraCanvasWindowsApplication::GetInstance();
    
    if (application && hwnd) {
        application->UnregisterWindowHandle(hwnd);
    }

    DestroyRenderTarget();

    if (hwnd) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }

    _created = false;
    std::cout << "UltraCanvas Windows: Window destroyed" << std::endl;
}

// ===== WINDOW VISIBILITY =====
void UltraCanvasWindowsWindow::Show() {
    if (!_created || visible) {
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    visible = true;
    
    std::cout << "UltraCanvas Windows: Window shown" << std::endl;

    // Trigger onWindowShow callback
    if (onWindowShow) {
        onWindowShow();
    }
}

void UltraCanvasWindowsWindow::Hide() {
    if (!_created || !visible) {
        return;
    }

    ShowWindow(hwnd, SW_HIDE);
    visible = false;
    
    std::cout << "UltraCanvas Windows: Window hidden" << std::endl;

    // Trigger onWindowHide callback
    if (onWindowHide) {
        onWindowHide();
    }
}

// ===== WINDOW PROPERTIES =====
void UltraCanvasWindowsWindow::SetWindowTitle(const std::string& title) {
    config_.title = title;

    if (hwnd) {
        std::wstring wTitle = UltraCanvasWindowsApplication::StringToWString(title);
        SetWindowTextW(hwnd, wTitle.c_str());
        std::cout << "UltraCanvas Windows: Window title set to: \"" << title << "\"" << std::endl;
    }
}

void UltraCanvasWindowsWindow::SetWindowSize(int width, int height) {
    config_.width = width;
    config_.height = height;

    if (_created && hwnd) {
        // Calculate window size including frame
        RECT rect = {0, 0, width, height};
        AdjustWindowRectEx(&rect, GetWindowStyle(), FALSE, GetWindowExStyle());
        
        int windowWidth = rect.right - rect.left;
        int windowHeight = rect.bottom - rect.top;
        
        SetWindowPos(hwnd, nullptr, 0, 0, windowWidth, windowHeight, 
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        
        UpdateRenderTarget(width, height);
    }

    UltraCanvasWindowBase::SetSize(width, height);
}

void UltraCanvasWindowsWindow::SetWindowPosition(int x, int y) {
    config_.x = x;
    config_.y = y;

    if (_created && hwnd) {
        SetWindowPos(hwnd, nullptr, x, y, 0, 0, 
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void UltraCanvasWindowsWindow::SetResizable(bool resizable) {
    config_.resizable = resizable;

    if (_created && hwnd) {
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        
        if (resizable) {
            style |= (WS_THICKFRAME | WS_MAXIMIZEBOX);
        } else {
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }
        
        SetWindowLong(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, 
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

// ===== WINDOW STATE =====
void UltraCanvasWindowsWindow::Minimize() {
    if (_created && hwnd) {
        ShowWindow(hwnd, SW_MINIMIZE);
        _state = WindowState::Minimized;
        
        if (onWindowMinimize) {
            onWindowMinimize();
        }
    }
}

void UltraCanvasWindowsWindow::Maximize() {
    if (_created && hwnd) {
        ShowWindow(hwnd, SW_MAXIMIZE);
        _state = WindowState::Maximized;
        
        if (onWindowMaximize) {
            onWindowMaximize();
        }
    }
}

void UltraCanvasWindowsWindow::Restore() {
    if (_created && hwnd) {
        ShowWindow(hwnd, SW_RESTORE);
        _state = WindowState::Normal;
        
        if (onWindowRestore) {
            onWindowRestore();
        }
    }
}

void UltraCanvasWindowsWindow::SetFullscreen(bool fullscreen) {
    if (!_created || !hwnd) {
        return;
    }

    static WINDOWPLACEMENT prevPlacement = {sizeof(prevPlacement)};
    static LONG prevStyle = 0;

    if (fullscreen) {
        // Save current state
        prevStyle = GetWindowLong(hwnd, GWL_STYLE);
        GetWindowPlacement(hwnd, &prevPlacement);

        // Get monitor info
        HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(hMonitor, &mi);

        // Set fullscreen
        SetWindowLong(hwnd, GWL_STYLE, prevStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd, HWND_TOP,
                     mi.rcMonitor.left,
                     mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        _state = WindowState::Fullscreen;
    } else {
        // Restore previous state
        SetWindowLong(hwnd, GWL_STYLE, prevStyle);
        SetWindowPlacement(hwnd, &prevPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | 
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        _state = WindowState::Normal;
    }
}

void UltraCanvasWindowsWindow::RaiseAndFocus() {
    if (_created && hwnd) {
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }
}

void UltraCanvasWindowsWindow::Focus() {
    if (_created && hwnd) {
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }
}

// ===== RENDERING =====
void UltraCanvasWindowsWindow::Flush() {
    if (renderContext) {
        renderContext->SwapBuffers();
    }
}

void UltraCanvasWindowsWindow::Invalidate() {
    if (hwnd) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void UltraCanvasWindowsWindow::RenderFrame() {
    if (!_created || !visible || !renderContext) {
        return;
    }

    std::lock_guard<std::mutex> lock(renderMutex);

    // Begin drawing
    renderContext->BeginDraw();

    // Clear background
    renderContext->Clear(config_.backgroundColor);

    // Render UI content
    Render(renderContext.get());

    // End drawing and present
    renderContext->EndDraw();
    renderContext->SwapBuffers();

    ClearRequestRedraw();
}

// ===== EVENT HANDLING =====
void UltraCanvasWindowsWindow::HandleResizeEvent(int width, int height) {
    if (config_.width != width || config_.height != height) {
        config_.width = width;
        config_.height = height;
        
        UpdateRenderTarget(width, height);
        
        // Call base class handler
        UltraCanvasWindowBase::HandleResizeEvent(width, height);
        
        // Trigger callback
        if (onWindowResize) {
            onWindowResize(width, height);
        }
        
        // Request redraw
        RequestRedraw();
        Invalidate();
    }
}

void UltraCanvasWindowsWindow::HandleMoveEvent(int x, int y) {
    config_.x = x;
    config_.y = y;
    
    if (onWindowMove) {
        onWindowMove(x, y);
    }
}

void UltraCanvasWindowsWindow::HandleCloseRequest() {
    // Check if close is allowed via callback
    if (onWindowClose) {
        onWindowClose();
    }
    
    // Request deletion
    RequestDelete();
}

void UltraCanvasWindowsWindow::HandleDestroyEvent() {
    if (onWindowDestroy) {
        onWindowDestroy();
    }
}

// ===== GETTERS =====
unsigned long UltraCanvasWindowsWindow::GetNativeHandle() const {
    return reinterpret_cast<unsigned long>(hwnd);
}

} // namespace UltraCanvas

#endif // _WIN32
