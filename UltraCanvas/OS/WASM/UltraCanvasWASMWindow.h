// OS/WASM/UltraCanvasWASMWindow.h
// WebAssembly (Emscripten) platform window implementation
// Version: 2.0.0
// Last Modified: 2026-08-12
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_WASM_WINDOW_H
#define ULTRACANVAS_WASM_WINDOW_H

// ===== CORE INCLUDES =====
// Like the other platform window headers, this file is included from
// include/UltraCanvasWindow.h after UltraCanvasWindowBase is defined.
#include "../../include/UltraCanvasRenderContext.h"

// ===== EMSCRIPTEN / CAIRO INCLUDES =====
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cairo/cairo.h>

#include <string>
#include <vector>
#include <cstdint>

namespace UltraCanvas {

    class UltraCanvasWASMApplication;

// ===== WASM WINDOW CLASS =====
// Each window is an absolutely-positioned HTML <canvas> element. The window
// renders into an offscreen cairo image surface (nativeSurface, like the
// Windows backend); InvalidateWindowNative() presents it by converting the
// surface pixels to RGBA and putImageData()ing them onto the canvas.
    class UltraCanvasWASMWindow : public UltraCanvasWindowBase {
    protected:
        bool CreateNative() override;
        void DestroyNative() override;
        void DoResizeNative() override;
        // devicePixelRatio: the canvas backing store is scaled by it while all
        // CSS/event coordinates stay logical.
        float QueryNativeDeviceScale() const override;
        // Rebuild the offscreen cairo image surface at physical px + device
        // scale, and keep the canvas element's geometry in sync.
        bool RecreateNativeSurface() override;

    public:
        UltraCanvasWASMWindow();
        ~UltraCanvasWASMWindow() override;

        // ===== INHERITED FROM BASE WINDOW =====
        void Show() override;
        void Hide() override;
        void RaiseAndFocus() override;
        void SetWindowTitle(const std::string& title) override;
        void SetWindowIcon(const std::string& iconPath) override;
        void SetWindowSize(int width, int height) override;
        void SetWindowPosition(int x, int y) override;
        void SetResizable(bool resizable) override;
        void Minimize() override;
        void Maximize() override;
        void Restore() override;
        void SetFullscreen(bool fullscreen) override;
        void InvalidateWindowNative() override;
        NativeWindowHandle GetNativeHandle() const override;
        void GetScreenSize(int& width, int& height) const override;
        // Window geometry lives in CSS pixels (= logical units), like macOS —
        // not in physical device pixels as on X11/Windows.
        void GetNativeWindowSize(int& w, int& h) const override;

        // ===== WASM-SPECIFIC METHODS =====
        const std::string& GetCanvasId() const { return canvasId; }

    private:
        std::string canvasId;
        std::string canvasSelector;   // "#" + canvasId, kept alive for emscripten callbacks
        unsigned long windowId = 0;
        static unsigned long lastWindowId;

        bool canvasCreated = false;

        // Wheel notch accumulators (trackpads deliver many sub-notch deltas)
        double wheelAccumX = 0.0;
        double wheelAccumY = 0.0;

        // BGRX -> RGBA staging buffer reused across presents
        std::vector<uint8_t> presentBuffer;

        void CreateCanvasElement();
        void DestroyCanvasElement();
        void UpdateCanvasElementGeometry();
        void RegisterEventCallbacks();
        void UnregisterEventCallbacks();
        void PresentToCanvas();

        UCEvent MakeInputEvent(UCEventType type);
        void PushToApplication(const UCEvent& event);

        static EM_BOOL OnMouseCallback(int eventType, const EmscriptenMouseEvent* mouseEvent, void* userData);
        static EM_BOOL OnWheelCallback(int eventType, const EmscriptenWheelEvent* wheelEvent, void* userData);
        static EM_BOOL OnTouchCallback(int eventType, const EmscriptenTouchEvent* touchEvent, void* userData);
        static EM_BOOL OnFocusCallback(int eventType, const EmscriptenFocusEvent* focusEvent, void* userData);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WASM_WINDOW_H
