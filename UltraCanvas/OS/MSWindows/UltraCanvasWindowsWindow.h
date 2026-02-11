// OS/MSWindows/UltraCanvasWindowsWindow.h
// Windows platform window implementation for UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_WINDOWS_WINDOW_H
#define ULTRACANVAS_WINDOWS_WINDOW_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasRenderContext.h"
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"

// ===== WINDOWS PLATFORM INCLUDES =====
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Undefine Windows macros that conflict with UltraCanvas method names
#ifdef RGB
#undef RGB
#endif
#ifdef DrawText
#undef DrawText
#endif
#ifdef CreateWindow
#undef CreateWindow
#endif
#ifdef CreateDialog
#undef CreateDialog
#endif

// Cairo forward declaration (full header included in impl .cpp)
typedef struct _cairo_surface cairo_surface_t;

// ===== STANDARD INCLUDES =====
#include <memory>
#include <string>
#include <mutex>
#include <cstdint>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasWindowsApplication;

// ===== WINDOWS WINDOW CLASS =====
class UltraCanvasWindowsWindow : public UltraCanvasWindowBase {
    friend class UltraCanvasWindowsApplication; // WindowProc needs access to internals
protected:
    HWND hwnd = nullptr;
    std::unique_ptr<IRenderContext> renderContext;
    cairo_surface_t* cairoSurface = nullptr;
    std::mutex renderMutex;
    bool isTrackingMouse = false;

    // Impl methods defined in UltraCanvasWindowsWindowImpl.cpp (needs Application class)
    bool CreateNativeImpl();
    void DestroyNativeImpl();
    void SetWindowTitleImpl(const std::string& title);
    void ResizeCairoSurface(int w, int h);

    bool CreateNative() override {
        if (_created) return true;
        if (!CreateNativeImpl()) return false;
        _created = true;
        return true;
    }

    void DestroyNative() override {
        DestroyNativeImpl();
        _created = false;
    }

public:
    UltraCanvasWindowsWindow() = default;
    ~UltraCanvasWindowsWindow() override { if (hwnd) DestroyNative(); }

    // ===== INHERITED FROM BASE WINDOW =====
    void Show() override {
        if (!_created || visible) return;
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        visible = true;
        if (onWindowShow) onWindowShow();
    }
    void Hide() override {
        if (!_created || !visible) return;
        ShowWindow(hwnd, SW_HIDE);
        visible = false;
        if (onWindowHide) onWindowHide();
    }
    void SetWindowTitle(const std::string& title) override {
        config_.title = title;
        SetWindowTitleImpl(title);
    }
    void SetWindowSize(int w, int h) override {
        config_.width = w;
        config_.height = h;
        if (_created && hwnd) {
            DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
            RECT rect = {0, 0, w, h};
            AdjustWindowRectEx(&rect, style, FALSE, 0);
            SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        UltraCanvasWindowBase::SetSize(w, h);
    }
    void SetWindowPosition(int x, int y) override {
        config_.x = x; config_.y = y;
        if (_created && hwnd)
            SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void SetResizable(bool r) override {
        config_.resizable = r;
        if (_created && hwnd) {
            LONG style = GetWindowLongW(hwnd, GWL_STYLE);
            if (r) style |= (WS_THICKFRAME | WS_MAXIMIZEBOX);
            else   style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
            SetWindowLongW(hwnd, GWL_STYLE, style);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
    void Minimize() override {
        if (_created && hwnd) { ShowWindow(hwnd, SW_MINIMIZE); _state = WindowState::Minimized; }
    }
    void Maximize() override {
        if (_created && hwnd) { ShowWindow(hwnd, SW_MAXIMIZE); _state = WindowState::Maximized; }
    }
    void Restore() override {
        if (_created && hwnd) { ShowWindow(hwnd, SW_RESTORE); _state = WindowState::Normal; }
    }
    void SetFullscreen(bool) override {}
    void Flush() override {
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }
    unsigned long GetNativeHandle() const override {
        return static_cast<unsigned long>(reinterpret_cast<uintptr_t>(hwnd));
    }
    IRenderContext* GetRenderContext() const override { return renderContext.get(); }

    // ===== FOCUS MANAGEMENT =====
    void RaiseAndFocus() override {
        if (_created && hwnd) { BringWindowToTop(hwnd); SetForegroundWindow(hwnd); SetFocus(hwnd); }
    }
    void Focus() {
        if (_created && hwnd) { SetForegroundWindow(hwnd); SetFocus(hwnd); }
    }

    // ===== RENDERING =====
    void Invalidate() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }
    void RenderFrame() {}
    cairo_surface_t* GetCairoSurface() const { return cairoSurface; }

    // ===== EVENT HANDLING (called from WindowProc) =====
    void HandleResizeEvent(int w, int h) {
        if (config_.width != w || config_.height != h) {
            config_.width = w; config_.height = h;
            UltraCanvasWindowBase::SetSize(w, h);
            ResizeCairoSurface(w, h);
            if (onWindowResize) onWindowResize(w, h);
            RequestRedraw();
        }
    }
    void HandleMoveEvent(int x, int y) {
        config_.x = x; config_.y = y;
        if (onWindowMove) onWindowMove(x, y);
    }
    void HandleCloseRequest() {
        if (onWindowClose) onWindowClose();
        RequestDelete();
    }
    void HandleDestroyEvent() {}

    // ===== GETTERS =====
    HWND GetHWND() const { return hwnd; }
    bool IsVisible() const { return visible; }
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_WINDOWS_WINDOW_H
