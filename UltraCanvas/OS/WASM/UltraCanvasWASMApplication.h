// OS/WASM/UltraCanvasWASMApplication.h
// WebAssembly (Emscripten) platform application implementation
// Version: 2.0.0
// Last Modified: 2026-08-12
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_WASM_APPLICATION_H
#define ULTRACANVAS_WASM_APPLICATION_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

// ===== EMSCRIPTEN INCLUDES =====
#include <emscripten.h>
#include <emscripten/html5.h>

#include <string>

namespace UltraCanvas {

    class UltraCanvasWASMWindow;

// ===== WASM APPLICATION CLASS =====
// The browser owns the event loop: DOM callbacks (registered here and per-
// canvas in UltraCanvasWASMWindow) convert input into UCEvents and PushEvent()
// them; each requestAnimationFrame tick runs one RunOnce() iteration. Apps use
// the exact same bootstrap as on the desktop — Initialize(), CreateWindow(),
// Run() — see RunBeforeMainLoop() for how the blocking Run() loop is bypassed.
    class UltraCanvasWASMApplication : public UltraCanvasApplicationBase {
    private:
        static UltraCanvasWASMApplication* instance;

        // Keyboard focus can only follow DOM focus; key events are registered
        // once on the browser window and routed to the framework's focused
        // window by DispatchEvent()'s KeyDown/KeyUp fallback.
        static EM_BOOL OnKeyCallback(int eventType, const EmscriptenKeyboardEvent* keyEvent, void* userData);
        static EM_BOOL OnVisibilityChange(int eventType, const EmscriptenVisibilityChangeEvent* event, void* userData);
        static const char* OnBeforeUnload(int eventType, const void* reserved, void* userData);

        // One RunOnce() per animation frame; detects loop termination
        // (running == false or no windows left) and performs the same shutdown
        // tail as UltraCanvasApplicationBase::Run().
        static void MainLoopTick();
        void ShutdownAfterMainLoop();

        UCEvent ConvertKeyEvent(int eventType, const EmscriptenKeyboardEvent* keyEvent);
        static UCKeys ConvertDomKey(const char* key, const char* code, unsigned long location);

    public:
        UltraCanvasWASMApplication();
        ~UltraCanvasWASMApplication() override;

        static UltraCanvasWASMApplication* GetInstance() { return instance; }

        bool SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur) override;
        bool SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur,
                                     const char* filename, int hotspotX, int hotspotY) override;

    protected:
        // ===== INHERITED FROM BASE APPLICATION =====
        bool InitializeNative() override;
        void ShutdownNative() override;
        void CaptureMouseNative() override;
        void ReleaseMouseNative() override;
        void CollectAndProcessNativeEvents() override;
        void RunBeforeMainLoop() override;
        void WakeUpEventLoop() override;
        void InitializeWakeUp() override;
        void ShutdownWakeUp() override;
        FontStyle DetectSystemFontStyleNative() override;
        FontStyle DetectMonospacedFontStyleNative() override;
        void LoadBundledFontsNative() override;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WASM_APPLICATION_H
