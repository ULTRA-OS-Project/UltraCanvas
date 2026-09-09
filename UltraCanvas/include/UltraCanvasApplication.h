// include/UltraCanvasBaseApplication.h
// Main UltraCanvas Framework Entry Point - Unified System
// Version: 1.6.0
// Last Modified: 2026-08-03
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
#include <condition_variable>
#include <deque>
#include <mutex>
#include <queue>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <cstdint>

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

    // Makes sure fontconfig has a config file to load before it is first used.
    // Packaged builds ship no fonts.conf (notably the MSYS2-produced Windows
    // ZIP), and fontconfig then prints
    //     Fontconfig error: Cannot load default config file: No such file: (null)
    // on stderr and initialises with no font directory at all. If no config is
    // reachable (FONTCONFIG_FILE, FONTCONFIG_PATH, or the platform's built-in
    // location), this writes a minimal one — bundled fonts directory, system
    // font directories, a cache directory and the generic family aliases — into
    // the user's cache/appdata directory and points FONTCONFIG_FILE at it. It
    // carries no rendering rules, so text looks the same as on a system that
    // has its own fonts.conf. No-op on macOS (CoreText) and whenever a usable
    // config already exists, so a normal Linux desktop is unaffected. Called
    // first thing by UltraCanvasApplicationBase::Initialize().
    void SetupBundledFontconfig();

    // Tells the text stack that the set of available fonts just changed.
    // A font file added after start-up is invisible to Pango until this is
    // called: on the fontconfig platforms the FontSet has to be rebuilt and
    // the default font map told its configuration moved, and on macOS the
    // CoreText-backed map enumerates the installed families once when it is
    // created, so the default is dropped and rebuilt on next use. Called for
    // you by UltraCanvasApplicationBase::RegisterFontFile(); call it directly
    // only after registering a font behind the framework's back.
    void RefreshFontConfiguration();

    // Watches a file descriptor from within the UltraCanvas event loop. Added so a
    // host integration (e.g. Ladybird's IPC to its WebContent process) can have its
    // sockets serviced by the same wait the toolkit already runs, instead of needing
    // a second event loop. Registered via UltraCanvasApplicationBase::AddFdWatch().
    enum class FdWatchType { Read, Write };
    using FdWatchId = std::uint64_t;

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

        // File-descriptor watches (see FdWatchType). Registered via AddFdWatch();
        // the platform event loop folds these fds into its native wait primitive
        // (Linux: the select() in CollectAndProcessNativeEvents) and calls
        // FireFdWatch() for each fd that became ready.
        struct FdWatch { FdWatchId id; int fd; FdWatchType type; std::function<void()> callback; };
        std::vector<FdWatch> fdWatches_;
        mutable std::mutex   fdWatchesMutex_;
        FdWatchId            nextFdWatchId_ = 1;
        // (fd,type,id) view used by platform loops to build the wait set without
        // copying callbacks; FireFdWatch() runs the callback for a ready watch.
        struct FdWatchKey { FdWatchId id; int fd; FdWatchType type; };
        std::vector<FdWatchKey> SnapshotFdWatchKeys() const;
        void FireFdWatch(FdWatchId id);

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

        // ===== TOUCH GESTURE RECOGNITION =====
        // Backends deliver raw per-finger touches; turning two of them into a
        // pinch/rotate is pure geometry, so it lives here rather than in each
        // backend - every touch platform gets it from one implementation.
        struct TouchPoint {
            int pointerId = 0;
            Point2Di position;
        };
        std::vector<TouchPoint> activeTouches;
        bool gestureActive = false;          // two fingers down and moving
        double gestureBaseDistance = 0.0;    // finger separation when it began
        double gestureBaseAngle = 0.0;       // finger angle when it began
        std::weak_ptr<UltraCanvasWindowBase> gestureWindow;

        // Fold a raw touch event into the recogniser and, once two fingers are
        // moving, dispatch the resulting PinchZoom. Called for TouchStart /
        // TouchMove / TouchEnd only; PinchZoom itself never re-enters.
        void UpdateTouchGesture(const UCEvent& touchEvent);
        void ResetTouchGesture();

        UCMouseButton capturedMouseButtonDown = UCMouseButton::NoneButton;
        UCEvent currentEvent;
        std::chrono::steady_clock::time_point lastClickTime;
        const float DOUBLE_CLICK_TIME = 0;
        const int DOUBLE_CLICK_DISTANCE = 0;

        // Cached system font styles
        std::optional<FontStyle> cachedSystemFontStyle_;
        std::optional<FontStyle> cachedMonospacedFontStyle_;

        // Font files handed to RegisterFontFile(), by resolved path, so a
        // second call for the same file does not register it twice.
        mutable std::mutex registeredFontsMutex_;
        std::vector<std::string> registeredFontFiles_;

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

        // Watch `fd` for readability/writability from within the event loop. The
        // callback runs on the UI thread on each iteration the fd is ready. Returns
        // an id for RemoveFdWatch(). Used by host integrations that must service
        // their own sockets (e.g. IPC) on the toolkit's loop. Thread-safe.
        FdWatchId AddFdWatch(int fd, FdWatchType type, std::function<void()> callback);
        void RemoveFdWatch(FdWatchId id);

        static void InstallWindowEventFilter(UltraCanvasUIElement* elem, const std::vector<UCEventType>& interestedEvents);
        static void UnInstallWindowEventFilter(UltraCanvasUIElement* elem);
        static void MoveWindowEventFilters(UltraCanvasWindowBase* winFrom, UltraCanvasUIElement* elem);

        bool IsKeyPressed(UCKeys keyCode) { return keyStates[keyCode]; }
        // Keys the application currently believes are held down. Modal dialogs
        // snapshot this on open so a key that was already down when they
        // appeared cannot act as input to them.
        std::vector<UCKeys> GetPressedKeys() const;
        // Forget every held key and modifier. Called when a modal dialog closes
        // so the key that dismissed it is not still considered down by whatever
        // regains the focus.
        void ClearKeyboardState();

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

        // ===== RUNTIME FONT REGISTRATION =====
        // Makes the faces in a font file usable by name for the rest of this
        // process - after this returns true, a FontStyle naming one of the
        // file's families renders with it. The registration is private to the
        // process: nothing is installed for the user or for other
        // applications, and it lasts until the process exits.
        //
        // Use it for fonts an application ships or downloads rather than
        // requires to be installed - a document that embeds its fonts, a
        // theme that comes with one, a font manager previewing a candidate
        // before it is installed. Read the family names to ask for out of the
        // file itself with ReadFontFileInfo() (UltraCanvasFontFile.h), which
        // needs no registration and answers before this is called.
        //
        // Registering the same file twice is a no-op that returns true.
        // There is no unregister: neither fontconfig nor the framework can
        // withdraw one file's faces from a running text stack without
        // discarding every application font, so a registration is for the
        // life of the process. Call from the UI thread - it changes global
        // font state that layout reads.
        //
        // Returns false when the file does not exist or the platform's font
        // system rejects it (a corrupt file, or a format it does not accept).
        bool RegisterFontFile(const std::string& fontFilePath);

        // True when this path was registered by RegisterFontFile() already.
        bool IsFontFileRegistered(const std::string& fontFilePath) const;

        // Every font file registered so far, in registration order.
        std::vector<std::string> GetRegisteredFontFiles() const;

        // Application icon
        void SetDefaultWindowIcon(const std::string& iconPath) { defaultWindowIconPath = iconPath; }
        std::string GetDefaultWindowIcon() const { return defaultWindowIconPath; }

        void Run();
        // One iteration of the main loop (native events + fd watches, queued UI
        // events, timers, posted tasks, then render). Exposed so a host embedding
        // UltraCanvas under its own event loop can pump a single iteration without
        // calling Run(). Assumes Initialize() succeeded and the app is running.
        void RunOnce();
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

        // Hand one font file to the platform's font system, process-privately
        // (FontConfig on Linux/Android/WASM, GDI + FontConfig on Windows,
        // CoreText on macOS). Called by RegisterFontFile(), which does the
        // existence check, the duplicate check and the cache refresh around
        // it. Returns false when the platform rejects the file.
        virtual bool RegisterFontFileNative(const std::string& fontFilePath) = 0;

        // Platform-specific wakeup mechanism for cross-thread signaling
        virtual void WakeUpEventLoop() = 0;
        virtual void InitializeWakeUp() = 0;
        virtual void ShutdownWakeUp() = 0;

    };
}

// __ANDROID__ must be tested before __linux__: bionic defines __linux__, so the
// Linux/X11 branch would otherwise shadow the Android one on every NDK build.
#if defined(__ANDROID__)
#include "../OS/Android/UltraCanvasAndroidApplication.h"
namespace UltraCanvas { using UltraCanvasApplication = UltraCanvasAndroidApplication; }
#elif defined(__EMSCRIPTEN__)
// Web/WASM (checked before __unix__, which Emscripten also defines)
#include "../OS/WASM/UltraCanvasWASMApplication.h"
namespace UltraCanvas { using UltraCanvasApplication = UltraCanvasWASMApplication; }
#elif defined(__linux__) || defined(__unix__) || defined(__unix)
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
#else
    #error "No supported platform defined. Supported platforms: Linux, Windows, macOS, iOS, Android, Web/WASM, Unix"
#endif

#endif