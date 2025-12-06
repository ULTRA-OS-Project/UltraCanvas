// OS/MacOS/UltraCanvasMacOSApplication.h
// Complete macOS platform implementation for UltraCanvas Framework using Cairo
// Version: 2.0.0
// Last Modified: 2025-01-18
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_MACOS_APPLICATION_H
#define ULTRACANVAS_MACOS_APPLICATION_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasApplication.h"
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

// ===== MACOS PLATFORM INCLUDES =====
#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
    #import <QuartzCore/QuartzCore.h>
#else
// Forward declarations for C++ only files
typedef struct objc_object NSApplication;
typedef struct objc_object NSAutoreleasePool;
typedef struct objc_object NSRunLoop;
typedef struct objc_object NSEvent;
typedef struct objc_object NSWindow;
typedef struct objc_object NSMenu;
#endif

// ===== CAIRO INCLUDES =====
#include <cairo/cairo.h>
#include <cairo/cairo-quartz.h>
#include <pango/pangocairo.h>

// ===== STANDARD INCLUDES =====
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasMacOSWindow;

    struct MouseClickInfo {
        void* window = nullptr;  // NSWindow*
        std::chrono::steady_clock::time_point lastClickTime;
        int lastClickX = 0;
        int lastClickY = 0;
        unsigned int lastButton = 0;
        int doubleClickTime = 250; // milliseconds
        int doubleClickDistance = 5; // pixels
    };

// ===== MACOS APPLICATION CLASS =====
    class UltraCanvasMacOSApplication : public UltraCanvasBaseApplication {
    private:
        static UltraCanvasMacOSApplication* instance;

        // ===== COCOA APPLICATION SYSTEM =====
        NSApplication* nsApplication;
        NSRunLoop* mainRunLoop;

        // ===== GRAPHICS SYSTEM =====
        bool cairoSupported;
        bool quartzGLEnabled;
        bool retinaSupported;
        float displayScaleFactor;

        // ===== EVENT SYSTEM =====
        std::queue<UCEvent> eventQueue;
        std::mutex eventQueueMutex;
        std::condition_variable eventCondition;
        bool eventThreadRunning;
        std::thread eventThread;

        // ===== WINDOW MANAGEMENT =====
//        std::unordered_map<void*, UltraCanvasMacOSWindow*> windowMap;  // NSWindow* -> UltraCanvasWindow*
//        UltraCanvasMacOSWindow* focusedWindow;

        // ===== TIMING AND FRAME RATE =====
//        std::chrono::steady_clock::time_point lastFrameTime;
//        double deltaTime;
//        int targetFPS;
//        bool vsyncEnabled;

        // ===== MENU SYSTEM =====
        bool menuBarCreated;
        void* mainMenu;  // NSMenu*
        void* applicationMenu;  // NSMenu*

        // ===== MOUSE TRACKING =====
        MouseClickInfo mouseClickInfo;

        // ===== THREAD SAFETY =====
        std::mutex cocoaMutex;
        std::thread::id mainThreadId;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasMacOSApplication();
        virtual ~UltraCanvasMacOSApplication();

        static UltraCanvasMacOSApplication* GetInstance() {
            return instance;
        }

        // ===== INHERITED FROM BASE APPLICATION =====
        bool InitializeNative() override;
        bool ShutdownNative() override;
        void RunNative() override;
        void Exit() override;

        // ===== MACOS-SPECIFIC METHODS =====

        // Application initialization
        bool InitializeCocoa();
        bool InitializeCairo();
        void InitializeMenuBar();
        void InitializeDisplaySettings();

        // Application information
        NSApplication* GetNSApplication() const { return nsApplication; }
        float GetDisplayScaleFactor() const { return displayScaleFactor; }
        bool IsRetinaSupported() const { return retinaSupported; }
        bool IsQuartzGLEnabled() const { return quartzGLEnabled; }
        bool IsCairoSupported() const { return cairoSupported; }

        // Event processing
        void ProcessCocoaEvent(NSEvent* nsEvent);
        UCEvent ConvertNSEventToUCEvent(NSEvent* nsEvent);
        bool HasPendingEvents();

        // Window management
//        void RegisterWindow(UltraCanvasMacOSWindow* window, NSWindow* nsWindow);
//        void UnregisterWindow(NSWindow* nsWindow);
//        UltraCanvasMacOSWindow* FindWindow(NSWindow* nsWindow);
//        void SetFocusedWindow(UltraCanvasMacOSWindow* window);
//        UltraCanvasMacOSWindow* GetFocusedWindow() const { return focusedWindow; }

        // Frame rate and timing
//        void SetTargetFPS(int fps) { targetFPS = fps; }
//        int GetTargetFPS() const { return targetFPS; }
//        void SetVSync(bool enabled) { vsyncEnabled = enabled; }
//        bool GetVSync() const { return vsyncEnabled; }
//        double GetDeltaTime() const { return deltaTime; }

        // Mouse tracking for double-click detection
        bool IsDoubleClick(NSWindow* window, int x, int y, unsigned int button);
        void UpdateLastClick(NSWindow* window, int x, int y, unsigned int button);

        // Thread safety
        bool IsMainThread() const;
        void ExecuteOnMainThread(std::function<void()> func);

    private:
        // Internal helper methods
        void EventLoop();
        void Update();
        void CleanupCocoa();

        // Key and button code conversion
        UCKeys ConvertNSEventKeyCode(unsigned short keyCode);
        UCMouseButton ConvertNSEventMouseButton(int buttonNumber);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_MACOS_APPLICATION_H