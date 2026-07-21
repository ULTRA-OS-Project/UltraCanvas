// include/UltraCanvasBaseApplication.h
// Main UltraCanvas Framework Entry Point - Unified System
// Version: 1.5.1
// Last Modified: 2026-07-21
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BASE_APPLICATION_H
#define ULTRACANVAS_BASE_APPLICATION_H

#include "UltraCanvasNativeHandle.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasFocusHistory.h"
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

        std::deque<UCEvent> eventQueue;
        std::mutex eventQueueMutex;
        std::condition_variable eventCondition;

        // Timer system
        std::vector<UltraCanvasTimer> timers_;
        mutable std::mutex timersMutex_;
        TimerId nextTimerId_ = 1;

        // PostToUIThread queue. Background threads push functions here and
        // call WakeUpEventLoop(); the main loop drains them via
        // ProcessPostedTasks() each iteration.
        std::vector<std::function<void()>> postedTasks_;
        std::mutex                         postedTasksMutex_;

        std::vector<std::shared_ptr<UltraCanvasWindowBase>> windows;
        std::vector<std::weak_ptr<UltraCanvasWindowBase>> activeModalWindows;

        // Non-owning references to transient UI state. Stored as weak_ptr so a
        // destroyed window/element simply lock()s to nullptr instead of dangling.
        std::weak_ptr<UltraCanvasWindowBase> focusedWindow;

        // Window focus history in most-recently-used order (front = current).
        // Maintained by SetFocusedWindowInternal(); drives JumpToLastWindow().
        UCWeakMRUList<UltraCanvasWindowBase> windowFocusHistory;

        // "Jump to last window" trigger bindings. Disabled until an app calls
        // SetJumpToLastWindowKey() / SetJumpToLastWindowMouseButton().
        bool jumpLastWindowKeyEnabled = false;
        UCKeys jumpLastWindowKey = UCKeys::Unknown;
        bool jumpLastWindowKeyCtrl = false;
        bool jumpLastWindowKeyShift = false;
        bool jumpLastWindowKeyAlt = false;
        bool jumpLastWindowKeyMeta = false;
        UCMouseButton jumpLastWindowMouseButton = UCMouseButton::NoneButton;

        // raw pointers used instead weak_ptr because when element created without using make_shared()
        // (make_unique() or raw new()) then weak_ptr is null
        UltraCanvasUIElement* hoveredElement = nullptr;
        UltraCanvasUIElement* capturedElement = nullptr;

        std::vector<std::function<bool(const UCEvent&)>> globalEventHandlers;
        std::function<void()> eventLoopCallback;

        UCMouseButton capturedMouseButtonDown = UCMouseButton::NoneButton;
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
        UltraCanvasApplicationBase();
        virtual ~UltraCanvasApplicationBase();

        // Returns the currently-running application instance (the one most
        // recently constructed). UltraCanvas assumes a single-application
        // process; this accessor lets callbacks running off the main thread
        // (e.g. libcurl workers, std::thread, plug-in pumps) reach
        // PostToUIThread without threading the pointer through every layer.
        static UltraCanvasApplicationBase* GetCurrent();

        // Schedules `task` to run on the main / UI thread the next time the
        // event loop iterates. Safe to call from any thread, including a
        // libcurl async worker (UltraNet_HttpRequestAsync callback) or any
        // std::thread the app spawns. The call is non-blocking; the task
        // runs after the loop wakes (WakeUpEventLoop is signalled here too).
        // A null `task` is silently ignored.
        void PostToUIThread(std::function<void()> task);

        void RegisterWindow(const std::shared_ptr<UltraCanvasWindowBase>& window);
        bool IsWindowRegistered(UltraCanvasWindowBase* window);
        void UnregisterWindow(UltraCanvasWindowBase* window);

        // Modal window management
        bool HandleModalWindowEvents(const UCEvent& event, UltraCanvasWindow* targetWindow);
        bool HasActiveModalWindow();
        UltraCanvasWindowBase* GetCurrentModalWindow();
        void RegisterModalWindow(const std::shared_ptr<UltraCanvasWindowBase>& window);
        void UnregisterModalWindow(UltraCanvasWindowBase* window);

        // Close every window whose config parentWindow is `parent` (recursively,
        // via each child's own PerformClose). Called when a window closes so its
        // transient children (dialogs, modal popups) cannot outlive it — an
        // orphaned modal child would keep swallowing the whole application's
        // input while its parent is already gone.
        void CloseChildWindows(UltraCanvasWindowBase* parent);

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

        bool IsKeyPressed(UCKeys keyCode) { return keyStates[keyCode]; }

        bool IsShiftHeld() { return shiftHeld; }
        bool IsCtrlHeld() { return ctrlHeld; }
        bool IsAltHeld() { return altHeld; }
        bool IsMetaHeld() { return metaHeld; }

        UltraCanvasWindow* GetFocusedWindow();  // downcast from weak_ptr, defined in .cpp
        // All windows registered with the application (main windows and dialogs).
        const std::vector<std::shared_ptr<UltraCanvasWindowBase>>& GetWindows() const { return windows; }
        UltraCanvasUIElement* GetFocusedElement();
        UltraCanvasUIElement* GetHoveredElement() { return hoveredElement; }
        UltraCanvasUIElement* GetCapturedElement() { return capturedElement; }

        UltraCanvasWindow* FindWindow(NativeWindowHandle nativeHandle);

        const UCEvent& GetCurrentEvent() { return currentEvent; }

        virtual void FocusNextElement();
        virtual void FocusPreviousElement();

        // ===== JUMP TO LAST WINDOW =====
        // Raises + focuses the window that was used before the currently
        // focused one (repeated calls toggle between the two most recent
        // windows). The target window's focused element is preserved across
        // the switch, so keyboard input resumes in the same input field.
        // Returns false when there is no other eligible window or a modal
        // window is active (modality must not be bypassed by the shortcut).
        bool JumpToLastWindow();

        // Binds a keyboard shortcut that triggers JumpToLastWindow() on
        // KeyDown, before the event reaches any window. Example:
        //   app->SetJumpToLastWindowKey(UCKeys::F6);
        //   app->SetJumpToLastWindowKey(UCKeys::Grave, /*ctrl=*/true);
        void SetJumpToLastWindowKey(UCKeys key, bool ctrl = false, bool shift = false,
                                    bool alt = false, bool meta = false);
        void ClearJumpToLastWindowKey();

        // Binds a mouse button (typically UCMouseButton::Back or ::Forward,
        // the side/thumb buttons) that triggers JumpToLastWindow() on
        // MouseDown. Pass UCMouseButton::NoneButton to disable.
        void SetJumpToLastWindowMouseButton(UCMouseButton button);

        // ===== MOUSE CAPTURE =====
        void CaptureMouse(UltraCanvasUIElement* element);
        void ReleaseMouse();

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


        void CleanupWindowReferences(UltraCanvasWindowBase* window);
        virtual void CollectAndProcessNativeEvents() = 0;

        // Applies a window focus change synchronously: sends WindowBlur to the
        // previously focused window, WindowFocus to `window`, then updates
        // focusedWindow and the MRU focus history. Passing nullptr only blurs.
        void SetFocusedWindowInternal(UltraCanvasWindowBase* window);

        // True when `event` matches the configured jump-to-last-window
        // keyboard or mouse trigger.
        bool MatchesJumpToLastWindowTrigger(const UCEvent& event) const;

        // Timer processing - called from Run() each iteration
        void ProcessTimers();
        std::chrono::milliseconds GetTimeUntilNextTimer() const;

        // Drains and runs anything PostToUIThread enqueued. Called from
        // Run() right after ProcessTimers().
        void ProcessPostedTasks();

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