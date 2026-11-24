// OS/iOS/UltraCanvasiOSApplication.h
// iOS platform implementation for UltraCanvas Framework.
// UIKit owns the main thread through UIApplicationMain; the framework's
// blocking Run() loop is started on that thread once the application has
// finished launching (OS/iOS/UltraCanvasiOSMain.mm), and
// CollectAndProcessNativeEvents runs the main run loop one turn at a time, so
// UIKit's touch and lifecycle callbacks arrive inside it
// (Docs/UltraCanvas/iOSPortInvestigation.md 3.2, option A).
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_IOS_APPLICATION_H
#define ULTRACANVAS_IOS_APPLICATION_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

#include <cstdint>
#include <string>

namespace UltraCanvas {

    class UltraCanvasiOSWindow;

    // ===== iOS APPLICATION CLASS =====
    class UltraCanvasiOSApplication : public UltraCanvasApplicationBase {
    private:
        static UltraCanvasiOSApplication* instance;

        // CFRunLoopSourceRef: signalled by WakeUpEventLoop (any thread) so the
        // run-loop turn in CollectAndProcessNativeEvents returns at once.
        void* wakeupSource = nullptr;

        // Tap -> double-click synthesis (as the Android backend does it).
        double lastTapTime = 0.0;
        int lastTapX = 0;
        int lastTapY = 0;
        double doubleClickTime = 0.3;     // seconds
        int doubleClickDistance = 20;     // points; generous for fingers

        // Latched once a second finger lands: the rest of the gesture is
        // delivered as touch events only (the Android rule), so a pinch
        // cannot also drag whatever the first finger hit.
        bool multiTouchGesture = false;
        int touchesDown = 0;

    public:
        UltraCanvasiOSApplication();
        ~UltraCanvasiOSApplication() override;

        static UltraCanvasiOSApplication* GetInstance() {
            return UltraCanvasiOSApplication::instance;
        }

        // The single top-level window the screen backs (the first registered
        // window); popups and dialogs render in-process on top of it.
        UltraCanvasiOSWindow* GetPrimaryWindow();

        // ===== INPUT FROM THE VIEW (main thread) =====
        // One finger, in the view's coordinates (points = logical units).
        // `phase`: 0 began, 1 moved, 2 ended, 3 cancelled. `fingerId` is
        // stable for the finger's whole touch; `fingersDown` counts every
        // finger on the screen after this change.
        void HandleTouch(int phase, int fingerId, float x, float y, float pressure,
                         int fingersDown, double timestamp);
        // The view queued events: end the run-loop turn now. (A UIKit
        // callback is a handled source, so the turn returns anyway; this
        // also covers one delivered outside it.)
        void WakeUpEventLoopFromView() { WakeUpEventLoop(); }
        // Application lifecycle (UIApplicationDelegate).
        void HandleDidEnterBackground();
        void HandleWillEnterForeground();
        void HandleDidBecomeActive();
        void HandleWillResignActive();

        // iOS has no pointer cursor (an iPad pointer is a later phase); both
        // are accepted no-ops.
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
        void RunInEventLoop() override;
        void WakeUpEventLoop() override;
        void InitializeWakeUp() override;
        void ShutdownWakeUp() override;
        FontStyle DetectSystemFontStyleNative() override;
        FontStyle DetectMonospacedFontStyleNative() override;
        void LoadBundledFontsNative() override;
        bool RegisterFontFileNative(const std::string& fontFilePath) override;

    private:
        void PushWindowEvent(UCEventType type);
        void PushPointerEvent(UltraCanvasiOSWindow* win, UCEventType type, int x, int y,
                              float pressure, int fingerId, int fingersDown);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_IOS_APPLICATION_H
