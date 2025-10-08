// OS/MacOS/UltraCanvasMacOSApplication.h
// Complete macOS platform implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_MACOS_APPLICATION_H
#define ULTRACANVAS_MACOS_APPLICATION_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasBaseApplication.h"
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

// ===== MACOS PLATFORM INCLUDES =====
#ifdef __OBJC__
    #import <Cocoa/Cocoa.h>
    #import <CoreGraphics/CoreGraphics.h>
    #import <QuartzCore/QuartzCore.h>
#else
    // Forward declarations for C++ only files
    typedef struct objc_object NSApplication;
    typedef struct objc_object NSAutoreleasePool;
    typedef struct objc_object NSRunLoop;
    typedef struct objc_object NSEvent;
    typedef struct objc_object NSWindow;
    typedef struct CGContext* CGContextRef;
#endif

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

// ===== MACOS APPLICATION CLASS =====
class UltraCanvasMacOSApplication : public UltraCanvasBaseApplication {
private:
    // ===== COCOA APPLICATION SYSTEM =====
    NSApplication* nsApplication;
    NSAutoreleasePool* autoreleasePool;
    NSRunLoop* mainRunLoop;

    // ===== GRAPHICS SYSTEM =====
    bool coreGraphicsSupported;
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
    std::unordered_map<void*, UltraCanvasMacOSWindow*> windowMap;  // NSWindow* -> UltraCanvasWindow*
    UltraCanvasMacOSWindow* focusedWindow;

    // ===== TIMING AND FRAME RATE =====
    std::chrono::steady_clock::time_point lastFrameTime;
    double deltaTime;
    int targetFPS;
    bool vsyncEnabled;

    // ===== MENU SYSTEM =====
    bool menuBarCreated;
    void* mainMenu;  // NSMenu*
    void* applicationMenu;  // NSMenu*

    // ===== THREAD SAFETY =====
    std::mutex cocoaMutex;
    std::thread::id mainThreadId;

public:
    // ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasMacOSApplication();
    virtual ~UltraCanvasMacOSApplication();

    // ===== INITIALIZATION =====
    bool InitializeNative() override;
    bool InitializeCocoa();
    bool InitializeCoreGraphics();
    bool InitializeMenuBar();
    void InitializeDisplaySettings();
    
    // ===== APPLICATION LIFECYCLE =====
    void Run() override;
    void Exit() override;
    void ProcessEvents() override;
    void Update() override;

    // ===== EVENT PROCESSING =====
    void ProcessCocoaEvent(NSEvent* nsEvent);
    UCEvent ConvertNSEventToUCEvent(const NSEvent* nsEvent);
    void PushEvent(const UCEvent& event);
    UCEvent PopEvent();
    bool HasPendingEvents();

    // ===== WINDOW MANAGEMENT =====
    void RegisterWindow(UltraCanvasMacOSWindow* window, NSWindow* nsWindow);
    void UnregisterWindow(NSWindow* nsWindow);
    UltraCanvasMacOSWindow* FindWindow(NSWindow* nsWindow);
    void SetFocusedWindow(UltraCanvasMacOSWindow* window);

    // ===== DISPLAY MANAGEMENT =====
    float GetDisplayScaleFactor() const { return displayScaleFactor; }
    bool IsRetinaSupported() const { return retinaSupported; }
    bool IsQuartzGLEnabled() const { return quartzGLEnabled; }

    // ===== MENU MANAGEMENT =====
    void CreateApplicationMenu();
    void UpdateMenuBar();

    // ===== THREAD SAFETY =====
    template<typename Func>
    auto SafeExecuteOnMainThread(Func&& func) -> decltype(func());
    bool IsMainThread() const;

    // ===== GETTERS =====
    NSApplication* GetNSApplication() const { return nsApplication; }
    NSRunLoop* GetMainRunLoop() const { return mainRunLoop; }
    double GetDeltaTime() const { return deltaTime; }
    bool IsVSyncEnabled() const { return vsyncEnabled; }

    // ===== ERROR HANDLING (LINUX COMPATIBILITY) =====
    static int XErrorHandler(void* display, void* errorEvent);
    static int XIOErrorHandler(void* display);

private:
    // ===== CLEANUP =====
    void CleanupCocoa();
    void StopEventThread();

    // ===== HELPER METHODS =====
    void UpdateFrameTime();
    void LimitFrameRate();
    void HandleApplicationEvent(NSEvent* nsEvent);
    void HandleWindowEvent(NSEvent* nsEvent);
    void HandleKeyboardEvent(NSEvent* nsEvent);
    void HandleMouseEvent(NSEvent* nsEvent);

    // ===== OBJECTIVE-C BRIDGE METHODS =====
    void* CreateNSApplicationInstance();
    void SetupApplicationDelegate();
    void ConfigureApplicationProperties();
};

// ===== THREAD-SAFE EXECUTION TEMPLATE =====
template<typename Func>
auto UltraCanvasMacOSApplication::SafeExecuteOnMainThread(Func&& func) -> decltype(func()) {
    if (IsMainThread()) {
        return func();
    } else {
        // Dispatch to main thread using GCD
        __block decltype(func()) result;
        __block bool completed = false;
        
        dispatch_sync(dispatch_get_main_queue(), ^{
            result = func();
            completed = true;
        });
        
        return result;
    }
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_MACOS_APPLICATION_H