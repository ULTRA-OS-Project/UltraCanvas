// OS/MacOS/UltraCanvasMacOSApplication.h
// Complete macOS platform implementation for UltraCanvas Framework using Cairo
// Version: 2.4.0 - Dropped the unused hand-rolled double-click tracking state
//   (AppKit's NSEvent.clickCount drives MouseDoubleClick instead)
// Version: 2.3.0 - RunInEventLoop() override (CoreAnimation commit per frame)
// Last Modified: 2026-08-11
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
#import <AppKit/AppKit.h>
#else
// Forward declarations for C++ only files
typedef struct objc_object NSApplication;
typedef struct objc_object NSAutoreleasePool;
typedef struct objc_object NSRunLoop;
typedef struct objc_object NSEvent;
typedef struct objc_object NSWindow;
typedef struct objc_object NSMenu;
typedef struct objc_object NSCursor;
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

    // (There is no click-tracking state here on purpose: unlike the X11
    // backend, which times and measures consecutive presses itself, AppKit
    // hands us NSEvent.clickCount already computed from the user's
    // double-click interval — see ConvertNSEventToUCEvent.)

// ===== MACOS APPLICATION CLASS =====
    class UltraCanvasMacOSApplication : public UltraCanvasApplicationBase {
    private:
        static UltraCanvasMacOSApplication* instance;

        // ===== COCOA APPLICATION SYSTEM =====
        NSApplication* nsApplication;
        NSRunLoop* mainRunLoop;

        std::unordered_map<UCMouseCursor, NSCursor*> cursors;

        // ===== GRAPHICS SYSTEM =====
        bool cairoSupported;
        bool quartzGLEnabled;
        bool retinaSupported;
        float displayScaleFactor;

        // ===== EVENT SYSTEM =====
//        std::queue<UCEvent> eventQueue;
//        std::mutex eventQueueMutex;
//        std::condition_variable eventCondition;
//        bool eventThreadRunning;
//        std::thread eventThread;

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

        // ===== THREAD SAFETY =====
        std::mutex cocoaMutex;
        std::thread::id mainThreadId;

        // Wakeup mechanism for cross-thread signaling
        void* wakeupSource = nullptr;  // CFRunLoopSourceRef

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasMacOSApplication();
        ~UltraCanvasMacOSApplication() override;

        static UltraCanvasMacOSApplication* GetInstance() {
            return instance;
        }

        // ===== INHERITED FROM BASE APPLICATION =====
        void RunBeforeMainLoop() override;

        // ===== MACOS-SPECIFIC METHODS =====

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

        // Frame rate and timing
//        void SetTargetFPS(int fps) { targetFPS = fps; }
//        int GetTargetFPS() const { return targetFPS; }
//        void SetVSync(bool enabled) { vsyncEnabled = enabled; }
//        bool GetVSync() const { return vsyncEnabled; }
//        double GetDeltaTime() const { return deltaTime; }

        // Thread safety
        bool IsMainThread() const;

        bool SelectMouseCursorNative(UltraCanvasWindowBase *win, UCMouseCursor cur) override;
        bool SelectMouseCursorNative(UltraCanvasWindowBase *win, UCMouseCursor cur, const char* filename, int hotspotX, int hotspotY) override;

    protected:
        bool InitializeNative() override;
        void ShutdownNative() override;
        void CaptureMouseNative() override;
        void ReleaseMouseNative() override;

        void CollectAndProcessNativeEvents() override;
        // Commits the frame produced by this loop iteration to the screen (the
        // implicit CoreAnimation transaction behind our layer-backed views).
        void RunInEventLoop() override;
        void WakeUpEventLoop() override;
        void InitializeWakeUp() override;
        void ShutdownWakeUp() override;
        FontStyle DetectSystemFontStyleNative() override;
        FontStyle DetectMonospacedFontStyleNative() override;
        void LoadBundledFontsNative() override;
        bool RegisterFontFileNative(const std::string& fontFilePath) override;
        NSCursor* LoadCursorFromImage(const std::string& filename, int hotspotX, int hotspotY);

    private:
        // Internal helper methods
        // Application initialization
        bool InitializeCocoa();
        bool InitializeCairo();
        void InitializeMenuBar();
        void InitializeDisplaySettings();

        // Key and button code conversion
        UCKeys ConvertNSEventKeyCode(unsigned short keyCode);
        UCMouseButton ConvertNSEventMouseButton(int buttonNumber);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_MACOS_APPLICATION_H