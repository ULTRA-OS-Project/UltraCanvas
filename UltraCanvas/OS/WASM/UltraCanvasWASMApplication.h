// OS/WASM/UltraCanvasWASMApplication.h
// WebAssembly (Emscripten) platform application implementation
// Version: 2.1.0 - input method composition through a hidden <textarea>
//   while the focused element draws compositions (TextComposition events)
// Version: 2.0.0
// Last Modified: 2026-10-14
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

#include <memory>
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

        // ===== INPUT METHOD COMPOSITION =====
        // A canvas gets no input method: while the focused element draws
        // compositions (DrawsTextComposition()), keyboard focus moves to a
        // transparent <textarea> at its caret, whose composition events
        // become TextComposition events and whose committed text a KeyDown
        // with `text`. Every other element keeps the canvas and the plain
        // key path.
        std::weak_ptr<UltraCanvasWindowBase> imeWindow;   // the window the textarea types into
        std::string imeCanvasId;                          // its canvas, for handing focus back
        bool imeActive = false;                           // the textarea has (or should have) focus
        bool imeComposing = false;                        // between compositionstart and compositionend
        bool imeSwitchingFocus = false;                   // our own focus moves: no WindowFocus/Blur
        void UpdateImeTarget();                           // once per frame
        void SetImeFocus(bool toInput, const std::string& canvasId);
        void PositionImeInput();
        void PushImeEvent(UCEvent& event);

    public:
        UltraCanvasWASMApplication();
        ~UltraCanvasWASMApplication() override;

        static UltraCanvasWASMApplication* GetInstance() { return instance; }

        // ===== INPUT METHOD COMPOSITION =====
        // True while the window's own focus moves (canvas <-> textarea): the
        // canvas focus callbacks do not report it as the window activating.
        bool IsSwitchingImeFocus() const { return imeSwitchingFocus; }
        // A click in `window`: ends a composition in progress (keeping its
        // text) and gives the keyboard to the textarea when `window` types
        // through it. False when the canvas should take focus as before.
        bool PrepareImeForClick(UltraCanvasWindowBase* window);
        // From the textarea's DOM events (the exported C entry points).
        void OnImeCompositionStart();
        void OnImeComposition(const std::string& text, int cursorUtf16);
        void OnImeCompositionEnd(const std::string& text);
        void OnImeText(const std::string& text);
        void OnImeFocus(bool focused);

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
        bool RegisterFontFileNative(const std::string& fontFilePath) override;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WASM_APPLICATION_H
