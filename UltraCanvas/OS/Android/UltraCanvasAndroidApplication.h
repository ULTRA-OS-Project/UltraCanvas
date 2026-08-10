// OS/Android/UltraCanvasAndroidApplication.h
// Android platform implementation for UltraCanvas Framework.
// Runs inside android_native_app_glue's android_main thread: the framework's
// blocking Run() loop is hosted there and CollectAndProcessNativeEvents pumps
// the glue's ALooper (activity commands + input queue).
// Version: 1.0.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_ANDROID_APPLICATION_H
#define ULTRACANVAS_ANDROID_APPLICATION_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

#include <cstdint>

// NDK types are kept out of this header where an opaque forward declaration
// suffices, so including UltraCanvasApplication.h stays cheap.
struct android_app;            // android_native_app_glue
struct AInputEvent;            // <android/input.h>
struct ANativeWindow;          // <android/native_window.h>

namespace UltraCanvas {

    class UltraCanvasAndroidWindow;

    // ===== ANDROID APPLICATION CLASS =====
    class UltraCanvasAndroidApplication : public UltraCanvasApplicationBase {
    private:
        static UltraCanvasAndroidApplication* instance;

        // The android_native_app_glue state. Stored by the android_main glue
        // (SetAndroidApp) BEFORE the application object is constructed; the
        // pointer is owned by the glue and stays valid for the activity's
        // whole native lifetime.
        static android_app* androidApp;

        // Tap → double-click synthesis (same approach as the Linux backend's
        // MouseClickInfo, keyed on event time + distance).
        int64_t lastTapTimeNs = 0;
        int lastTapX = 0;
        int lastTapY = 0;
        int64_t doubleClickTimeMs = 300;
        int doubleClickDistance = 30;   // physical px; generous for fingers

    public:
        UltraCanvasAndroidApplication();

        static UltraCanvasAndroidApplication* GetInstance() {
            return UltraCanvasAndroidApplication::instance;
        }

        // Called by the android_main glue before the app's main() runs.
        static void SetAndroidApp(android_app* app) { androidApp = app; }
        static android_app* GetAndroidApp() { return androidApp; }

        // The activity's current surface, or null between APP_CMD_TERM_WINDOW
        // and the next APP_CMD_INIT_WINDOW.
        ANativeWindow* GetNativeWindow() const;

        void SetDoubleClickTime(unsigned int milliseconds) { doubleClickTimeMs = milliseconds; }
        void SetDoubleClickDistance(int pixels) { doubleClickDistance = pixels; }

        // Android has no persistent pointer cursor; both are accepted no-ops.
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
        void WakeUpEventLoop() override;
        void InitializeWakeUp() override;
        void ShutdownWakeUp() override;
        FontStyle DetectSystemFontStyleNative() override;
        FontStyle DetectMonospacedFontStyleNative() override;
        void LoadBundledFontsNative() override;

    private:
        // The single top-level window the activity surface backs (the first
        // registered window); popups/dialogs render in-process on top of it.
        UltraCanvasAndroidWindow* GetPrimaryWindow();

        // android_native_app_glue callbacks (userData carries `this`).
        static void HandleAppCmdThunk(android_app* app, int32_t cmd);
        static int32_t HandleInputEventThunk(android_app* app, AInputEvent* event);

        void HandleAppCmd(int32_t cmd);
        int32_t HandleInputEvent(AInputEvent* event);
        int32_t HandleMotionEvent(AInputEvent* event);
        int32_t HandleKeyEvent(AInputEvent* event);

        void PushWindowEvent(UCEventType type);

        UCKeys ConvertAndroidKeyToUCKey(int32_t keyCode);
        // US-layout printable-ASCII derivation for physical keyboards; real
        // text entry (soft keyboard / IME) arrives in a later phase.
        char DeriveAsciiCharacter(UCKeys key, bool shift);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_APPLICATION_H
