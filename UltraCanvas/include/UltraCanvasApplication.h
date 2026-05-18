// include/UltraCanvasBaseApplication.h
// Main UltraCanvas Framework Entry Point - Unified System
// Version: 1.4.0
// Last Modified: 2026-05-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BASE_APPLICATION_H
#define ULTRACANVAS_BASE_APPLICATION_H

#include "UltraCanvasEvent.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasTimer.h"
#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <chrono>
#include <queue>
#include <optional>

namespace UltraCanvas {
    class UltraCanvasWindowBase;

    // Bundled DejaVu font registration tables. Defined in UltraCanvasApplication.cpp.
    extern const char* const kEmbeddedAllFonts[];
    extern const size_t kEmbeddedAllFontsCount;
    extern const char* const kEmbeddedMonoFonts[];
    extern const size_t kEmbeddedMonoFontsCount;

    // Returns absolute path to media/fonts/dejavu/ in the resources dir.
    std::string GetBundledFontsDir();

    // Pins hinting / antialias / autohint / lcdfilter defaults for the bundled
    // DejaVu families so system fontconfig (which differs between MSYS2 and
    // Linux distros) cannot change how the framework's text looks. Pass the
    // current FcConfig* as void* to keep this header free of fontconfig.h;
    // implemented on Linux + Windows, no-op elsewhere. Returns true on success.
//    bool LoadDejaVuFcRules(void* fcConfig);

    // Writes a runtime fonts.conf (with the absolute path to the bundled
    // DejaVu directory baked in) and sets the FONTCONFIG_FILE env variable so
    // the next FcInit() call has a valid config regardless of how broken the
    // system default is (notably on packaged MSYS2 Windows builds run outside
    // the mingw shell). On Linux the generated config <include>s the system
    // fonts.conf so apps still see non-DejaVu system fonts via FC. No-op on
    // macOS. Honours an externally-set FONTCONFIG_FILE — does not override.
    void SetupBundledFontconfig();

    class UltraCanvasApplicationBase {
    friend UltraCanvasWindowBase;
    protected:
        bool volatile running = false;
        bool volatile initialized = false;
        std::string appName;
        std::string defaultWindowIconPath;

        std::queue<UCEvent> eventQueue;
        std::mutex eventQueueMutex;
        std::condition_variable eventCondition;

        // Timer system
        std::vector<UltraCanvasTimer> timers_;
        mutable std::mutex timersMutex_;
        TimerId nextTimerId_ = 1;

        std::vector<std::shared_ptr<UltraCanvasWindowBase>> windows;
        std::vector<std::weak_ptr<UltraCanvasWindowBase>> activeModalWindows;

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

        // Cached system font styles
        std::optional<FontStyle> cachedSystemFontStyle_;
        std::optional<FontStyle> cachedMonospacedFontStyle_;

        // Keyboard state
        bool keyStates[256];
        bool shiftHeld = false;
        bool ctrlHeld = false;
        bool altHeld = false;
        bool metaHeld = false;

    public:        
        UltraCanvasApplicationBase() = default;

        void RegisterWindow(const std::shared_ptr<UltraCanvasWindowBase>& window);
        bool IsWindowRegistered(UltraCanvasWindowBase* window);
        void UnregisterWindow(UltraCanvasWindowBase* window);

        // Modal window management
        bool HandleModalWindowEvents(const UCEvent& event, UltraCanvasWindow* targetWindow);
        bool HasActiveModalWindow();
        UltraCanvasWindowBase* GetCurrentModalWindow();
        void RegisterModalWindow(const std::shared_ptr<UltraCanvasWindowBase>& window);
        void UnregisterModalWindow(UltraCanvasWindowBase* window);

        void ProcessEvents();
        bool PopEvent(UCEvent& event);
        void PushEvent(const UCEvent& event);
        void WaitForEvents(int timeoutMs);

        void DispatchEvent(const UCEvent& event);
        bool DispatchEventToElement(UltraCanvasUIElement* elem, UCEvent event);

        bool HandleEventWithBubbling(UltraCanvasUIElement* elem, const UCEvent &event);
        void RegisterEventLoopRunCallback(std::function<void()> callback);

        // Timer API - timers fire on the main thread
        TimerId StartTimer(unsigned int milliseconds_interval, bool periodic,
                           std::function<void(TimerId)> callback = nullptr);
        void StopTimer(TimerId id);

        static void InstallWindowEventFilter(UltraCanvasUIElement* elem, const std::vector<UCEventType>& interestedEvents);
        static void UnInstallWindowEventFilter(UltraCanvasUIElement* elem);
        static void MoveWindowEventFilters(UltraCanvasWindowBase* winFrom, UltraCanvasUIElement* elem);

        bool IsKeyPressed(int keyCode);

        bool IsShiftHeld() { return shiftHeld; }
        bool IsCtrlHeld() { return ctrlHeld; }
        bool IsAltHeld() { return altHeld; }
        bool IsMetaHeld() { return metaHeld; }

        UltraCanvasWindow* GetFocusedWindow() { return focusedWindow; }
        UltraCanvasUIElement* GetFocusedElement();
        UltraCanvasUIElement* GetHoveredElement() { return hoveredElement; }
        UltraCanvasUIElement* GetCapturedElement() { return capturedElement; }

        UltraCanvasWindow* FindWindow(NativeWindowHandle nativeHandle);

        const UCEvent& GetCurrentEvent() { return currentEvent; }

        virtual void FocusNextElement();
        virtual void FocusPreviousElement();

        // ===== MOUSE CAPTURE =====
        void CaptureMouse(UltraCanvasUIElement* element);
        void ReleaseMouse(UltraCanvasUIElement* element);

        // System font detection
        FontStyle GetSystemFontStyle();
        FontStyle GetDefaultMonospacedFontStyle();

        // Application icon
        void SetDefaultWindowIcon(const std::string& iconPath) { defaultWindowIconPath = iconPath; }
        std::string GetDefaultWindowIcon() const { return defaultWindowIconPath; }

        void Run();
        bool Initialize(const std::string& app);
        bool RequestExit();
        virtual void Exit();

        bool IsInitialized() const { return initialized; }
        bool IsRunning() const { return running; }

        void CleanupElementReferences(UltraCanvasUIElement* elem);


        //        bool HandleFocusedWindowChange(UltraCanvasWindow* window);
        virtual bool SelectMouseCursorNative(UltraCanvasWindowBase *win, UCMouseCursor ptr) = 0;
        virtual bool SelectMouseCursorNative(UltraCanvasWindowBase *win, UCMouseCursor ptr, const char* filename, int hotspotX, int hotspotY) = 0;
        
        std::function<bool()> onApplicationExitRequest;
        std::function<void()> onApplicationExit;
    protected:
        virtual bool InitializeNative() = 0;
        virtual void ShutdownNative() = 0;
        virtual void RunInEventLoop() {};
        virtual void RunBeforeMainLoop() {};
        virtual void CaptureMouseNative() = 0;
        virtual void ReleaseMouseNative() = 0;


        bool IsDoubleClick(const UCEvent &event);
        void CleanupWindowReferences(UltraCanvasWindowBase* window);
        virtual void CollectAndProcessNativeEvents() = 0;

        // Timer processing - called from Run() each iteration
        void ProcessTimers();
        std::chrono::milliseconds GetTimeUntilNextTimer() const;

        // Platform-specific system font detection
        virtual FontStyle DetectSystemFontStyleNative() = 0;
        virtual FontStyle DetectMonospacedFontStyleNative() = 0;

        // Register the bundled DejaVu fonts (process-private) so they are
        // available as the framework defaults on every platform. Implemented
        // per-platform via FontConfig + GDI (Windows) / FontConfig (Linux) /
        // CoreText (macOS, monospace only).
        virtual void LoadBundledFontsNative() = 0;

        // Platform-specific wakeup mechanism for cross-thread signaling
        virtual void WakeUpEventLoop() = 0;
        virtual void InitializeWakeUp() = 0;
        virtual void ShutdownWakeUp() = 0;

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