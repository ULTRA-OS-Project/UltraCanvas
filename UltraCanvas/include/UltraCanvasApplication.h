// include/UltraCanvasBaseApplication.h
// Main UltraCanvas Framework Entry Point - Unified System
// Version: 1.0.0
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BASE_APPLICATION_H
#define ULTRACANVAS_BASE_APPLICATION_H

#include "UltraCanvasEvent.h"
#include "UltraCanvasWindow.h"
#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <chrono>
#include <queue>

namespace UltraCanvas {
    class UltraCanvasBaseApplication {
    protected:
        bool volatile running = false;
        bool volatile initialized = false;
        std::string appName;
        std::queue<UCEvent> eventQueue;
        std::mutex eventQueueMutex;
        std::condition_variable eventCondition;

        std::vector<std::shared_ptr<UltraCanvasWindowBase>> windows;

        UltraCanvasWindow* focusedWindow = nullptr;
        UltraCanvasUIElement* hoveredElement = nullptr;
        UltraCanvasUIElement* capturedElement = nullptr;
        UltraCanvasUIElement* draggedElement = nullptr;

        std::vector<std::function<bool(const UCEvent&)>> globalEventHandlers;
        std::function<void()> eventLoopCallback;

        UCEvent lastMouseEvent;
        UCEvent currentEvent;
        std::chrono::steady_clock::time_point lastClickTime;
        const float DOUBLE_CLICK_TIME = 0;
        const int DOUBLE_CLICK_DISTANCE = 0;

        // Keyboard state
        bool keyStates[256];
        bool shiftHeld = false;
        bool ctrlHeld = false;
        bool altHeld = false;
        bool metaHeld = false;

    public:
        UltraCanvasBaseApplication() = default;

        void RegisterWindow(const std::shared_ptr<UltraCanvasWindowBase>& window);

        void ProcessEvents();
        bool PopEvent(UCEvent& event);
        void PushEvent(const UCEvent& event);
        void WaitForEvents(int timeoutMs);

        void DispatchEvent(const UCEvent &event);
        bool DispatchEventToElement(UltraCanvasUIElement* elem, const UCEvent &event);

        bool HandleEventWithBubbling(const UCEvent &event, UltraCanvasUIElement* elem);
        void RegisterGlobalEventHandler(std::function<bool(const UCEvent&)> handler);
        void ClearGlobalEventHandlers() { globalEventHandlers.clear(); }
        void RegisterEventLoopRunCallback(std::function<void()> callback);

        bool IsKeyPressed(int keyCode);

        bool IsShiftHeld() { return shiftHeld; }
        bool IsCtrlHeld() { return ctrlHeld; }
        bool IsAltHeld() { return altHeld; }
        bool IsMetaHeld() { return metaHeld; }

        UltraCanvasWindow* GetFocusedWindow() { return focusedWindow; }
        UltraCanvasUIElement* GetFocusedElement();
        UltraCanvasUIElement* GetHoveredElement() { return hoveredElement; }
        UltraCanvasUIElement* GetCapturedElement() { return capturedElement; }

        UltraCanvasWindow* FindWindow(unsigned long nativeHandle);

        const UCEvent& GetCurrentEvent() { return currentEvent; }

        virtual void FocusNextElement();
        virtual void FocusPreviousElement();

        // ===== MOUSE CAPTURE =====
        void CaptureMouse(UltraCanvasUIElement* element);
        void ReleaseMouse(UltraCanvasUIElement* element);

        void Run();
        bool Initialize(const std::string& app);
        void Shutdown();
        void RequestExit();
        virtual bool InitializeNative() = 0;
        virtual void ShutdownNative() = 0;
        virtual void RunInEventLoop() {};
        virtual void RunBeforeMainLoop() {};
        virtual void CaptureMouseNative() = 0;
        virtual void ReleaseMouseNative() = 0;

        bool IsInitialized() const { return initialized; }
        bool IsRunning() const { return running; }

        bool HandleFocusedWindowChange(UltraCanvasWindow* window);

    protected:
        bool IsDoubleClick(const UCEvent &event);
        void CleanupWindowReferences(UltraCanvasWindowBase* window);
        virtual void CollectAndProcessNativeEvents() = 0;
    };
}

#if defined(__linux__) || defined(__unix__) || defined(__unix)
#include "../OS/Linux/UltraCanvasLinuxApplication.h"
namespace UltraCanvas { using UltraCanvasApplication = UltraCanvasLinuxApplication; }
#elif defined(_WIN32) || defined(_WIN64)
#include "../OS/MSWindows/UltraCanvasWindowsApplication.h"
namespace UltraCanvas { using UltraCanvasApplication = UltraCanvasWindowsApplication; }
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC && !TARGET_OS_IPHONE
        // macOS
        #include "../OS/MacOS/UltraCanvasMacOSApplication.h"
        namespace UltraCanvas { using UltraCanvasApplication = UltraCanvasMacOSApplication; }
    #elif TARGET_OS_IPHONE
        // iOS
        #include "../OS/iOS/UltraCanvasiOSApplication.h"
        namespace UltraCanvas { using UltraCanvasApplication = UltraCanvasiOSApplication; }
    #else
        #error "Unsupported Apple platform"
    #endif
#elif defined(__ANDROID__)
    #include "../OS/Android/UltraCanvasAndroidApplication.h"
    namespace UltraCanvas { using UltraCanvasApplication = UltraCanvasAndroidApplication; }
#elif defined(__WASM__)
    // Web/WASM
    #include "../OS/Web/UltraCanvasWebApplication.h"
    namespace UltraCanvas { using UltraCanvasApplication = UltraCanvasWebApplication; }
#else
    #error "No supported platform defined. Supported platforms: Linux, Windows, macOS, iOS, Android, Web/WASM, Unix"
#endif

#endif