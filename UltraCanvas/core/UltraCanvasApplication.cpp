// UltraCanvasApplication.cpp
// Main UltraCanvas App
// Version: 1.5.1 - ProcessTimers() now runs callbacks outside timersMutex_ (fixes data race + reentrant dangling access)
// Last Modified: 2026-07-12
// Author: UltraCanvas Framework

#include <algorithm>
#include <atomic>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"

#if !defined(__APPLE__)
#include <fontconfig/fontconfig.h>
#endif

#if defined(__linux__) || defined(__unix__)
#include <unistd.h>
#include <linux/limits.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include "UltraCanvasDebug.h"
#endif


namespace UltraCanvas {

    // ===== Singleton accessor for cross-thread PostToUIThread =====
    namespace {
        std::atomic<UltraCanvasApplicationBase*> g_currentApplication{nullptr};
    }

    UltraCanvasApplicationBase::UltraCanvasApplicationBase() {
        // Latch the most-recently-constructed app as the "current" one;
        // UltraCanvas assumes one app per process.
        g_currentApplication.store(this, std::memory_order_release);
        memset(keyStates, 0, sizeof(keyStates));
    }

    UltraCanvasApplicationBase::~UltraCanvasApplicationBase() {
        UltraCanvasApplicationBase* expected = this;
        g_currentApplication.compare_exchange_strong(
            expected, nullptr, std::memory_order_release);
    }

    UltraCanvasApplicationBase* UltraCanvasApplicationBase::GetCurrent() {
        return g_currentApplication.load(std::memory_order_acquire);
    }

    void UltraCanvasApplicationBase::PostToUIThread(std::function<void()> task) {
        if (!task) return;
        {
            std::lock_guard<std::mutex> lk(postedTasksMutex_);
            postedTasks_.push_back(std::move(task));
        }
        // Wake the loop so the task runs promptly instead of waiting for
        // the next OS event.
        WakeUpEventLoop();
    }

    void UltraCanvasApplicationBase::ProcessPostedTasks() {
        // Swap under lock so concurrent posts can keep enqueueing while we
        // run tasks lock-free. Tasks running here might re-enter
        // PostToUIThread to chain follow-up work; that lands in the next
        // iteration, never the current vector.
        std::vector<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lk(postedTasksMutex_);
            local.swap(postedTasks_);
        }
        for (auto& fn : local) {
            try {
                fn();
            } catch (const std::exception& e) {
                std::cerr << "UltraCanvas PostToUIThread task threw: "
                          << e.what() << std::endl;
            } catch (...) {
                std::cerr << "UltraCanvas PostToUIThread task threw "
                             "non-std exception" << std::endl;
            }
        }
    }

    const char* const kEmbeddedAllFonts[] = {
        "Ubuntu-R.ttf", "Ubuntu-B.ttf",
        "Ubuntu-RI.ttf", "Ubuntu-BI.ttf",
        "Ubuntu-C.ttf", "Ubuntu-L.ttf",
        "Ubuntu-M.ttf", "Ubuntu-LI.ttf",
        "Ubuntu-MI.ttf", "Ubuntu-Th.ttf",
        "UbuntuMono-R.ttf", "UbuntuMono-B.ttf",
        "UbuntuMono-RI.ttf", "UbuntuMono-BI.ttf",
        "OpenSans-Bold.ttf", "OpenSans-BoldItalic.ttf",
//        "OpenSans-Italic.ttf", "OpenSans-Regular.ttf",
//        "OpenSans-Bold.ttf", "OpenSans-BoldItalic.ttf",
//        "OpenSans-CondBold.ttf", "OpenSans-CondLight.ttf",
//        "OpenSans-CondLightItalic.ttf", "OpenSans-ExtraBold.ttf",
//        "OpenSans-Light.ttf", "OpenSans-LightItalic.ttf",
//        "OpenSans-Semibold.ttf", "OpenSans-SemiboldItalic.ttf",
    };
    const size_t kEmbeddedAllFontsCount = sizeof(kEmbeddedAllFonts) / sizeof(kEmbeddedAllFonts[0]);

    const char* const kEmbeddedMonoFonts[] = {
            "UbuntuMono-R.ttf", "UbuntuMono-B.ttf",
            "UbuntuMono-RI.ttf", "UbuntuMono-BI.ttf",
    };
    const size_t kEmbeddedMonoFontsCount = sizeof(kEmbeddedMonoFonts) / sizeof(kEmbeddedMonoFonts[0]);

    std::string GetBundledFontsDir() {
        std::string p = NormalizePath(GetResourcesDir() + "media/fonts/");
        return p;
    }


    FontStyle UltraCanvasApplicationBase::GetSystemFontStyle() {
        if (!cachedSystemFontStyle_.has_value()) {
            cachedSystemFontStyle_ = DetectSystemFontStyleNative();
        }
        return cachedSystemFontStyle_.value();
    }

    FontStyle UltraCanvasApplicationBase::GetDefaultMonospacedFontStyle() {
        if (!cachedMonospacedFontStyle_.has_value()) {
            cachedMonospacedFontStyle_ = DetectMonospacedFontStyleNative();
        }
        return cachedMonospacedFontStyle_.value();
    }

    bool UltraCanvasApplicationBase::Initialize(const std::string& app) {
        appName = app;

        UCImage::InitializeImageSubsysterm(appName.c_str());

        if (InitializeNative()) {
            // Register bundled DejaVu fonts before any text rendering / default
            // detection runs, so platform Detect*FontStyleNative() can return
            // the just-registered families.
            LoadBundledFontsNative();

            if (!InitializeClipboard()) {
                debugOutput << "UltraCanvas: Failed to initialize clipboard" << std::endl;
            }

            // Auto-set default window icon if available
            std::string iconPath = GetDefaultIcon();
            if (std::filesystem::exists(iconPath)) {
                SetDefaultWindowIcon(iconPath);
                debugOutput << "UltraCanvas: Default window icon set to: " << iconPath << std::endl;
            }
#ifdef UCAPP_ICON_PATH
            else {
                // Fallback to app-specific icon defined at build time
                std::string appIconPath = NormalizePath(GetResourcesDir() + UCAPP_ICON_PATH);
                if (std::filesystem::exists(appIconPath)) {
                    SetDefaultWindowIcon(appIconPath);
                    debugOutput << "UltraCanvas: App icon set to: " << appIconPath << std::endl;
                } else {
                    debugOutput << "UltraCanvas: App icon not found at: " << appIconPath << std::endl;
                }
            }
#else
            else {
                debugOutput << "UltraCanvas: Default icon not found at: " << iconPath << std::endl;
            }
#endif

            return true;
        } else {
            debugOutput << "UltraCanvas: Failed to initialize application" << std::endl;
            return false;
        }
    }

    void UltraCanvasApplicationBase::Run() {
        debugOutput << "UltraCanvasBaseApplication::Run Starting app" << std::endl;
        if (!initialized) {
            debugOutput << "UltraCanvas: Cannot run - application not initialized" << std::endl;
            return;
        }

        running = true;

        // Start the event processing thread
        RunBeforeMainLoop();

        auto clipbrd = GetClipboard();

        debugOutput << "UltraCanvas: Starting main loop..." << std::endl;
        try {
            while (running && !windows.empty()) {
                CollectAndProcessNativeEvents();

                // Process all pending events
                ProcessEvents();

                // Fire expired timers
                ProcessTimers();

                // Run anything PostToUIThread enqueued from a background
                // thread (async HTTP callbacks, std::thread, plug-ins).
                ProcessPostedTasks();

                std::erase_if(windows, [](const auto &w) {
                    return (w->GetState() == WindowState::Closed && w->GetConfig().deleteOnClose);
                });
                // Check for visible windows, delete/cleanup windows

                for (auto it = windows.begin(); it != windows.end(); it++) {
                    auto window = it->get();
//                    debugOutput << "window w=" << window << " nativeh=" << window->GetNativeHandle() << " visible=" << window->IsVisible() << " needredraw=" << window->IsNeedsRedraw() << " ctx=" << window->GetRenderContext() << std::endl;
                    if (window->IsVisible()) {
                        window->UpdateAndRender();
                    }

                }

                if (windows.empty()) {
                    debugOutput << "UltraCanvas: No windows, exiting..." << std::endl;
                    break;
                }
                
                // Clean up stale modal windows (expired weak_ptrs)
                activeModalWindows.erase(
                    std::remove_if(activeModalWindows.begin(), activeModalWindows.end(),
                        [](const std::weak_ptr<UltraCanvasWindowBase>& w) {
                            return w.expired();
                        }),
                    activeModalWindows.end()
                );

                // Update and render all windows
                if (clipbrd) {
                    clipbrd->Update();
                }
                if (eventLoopCallback) {
                    eventLoopCallback();
                }

                RunInEventLoop();
            }

        } catch (const std::exception& e) {
            debugOutput << "UltraCanvas: Exception in main loop: " << e.what() << std::endl;
        }

        // Clean shutdown
        debugOutput << "UltraCanvas: Main loop ended, performing cleanup..." << std::endl;
        //StopEventThread();

        debugOutput << "UltraCanvas: Destroying all windows..." << std::endl;
        while (!windows.empty()) {
            try {
                auto window = windows.back();
                window->PerformClose();
                windows.pop_back();
            } catch (const std::exception& e) {
                debugOutput << "UltraCanvas: Exception destroying window: " << e.what() << std::endl;
            }
        }

        if (onApplicationExit) {
            onApplicationExit();
        }

        initialized = false;
        debugOutput << "UltraCanvas: main loop completed, shutting down.." << std::endl;
        
        ShutdownClipboard();
        ShutdownNative();

        UCImage::ShutdownImageSubsysterm();
    }

    bool UltraCanvasApplicationBase::RequestExit() {
        debugOutput << "UltraCanvas: application exit requested" << std::endl;
        if (onApplicationExitRequest) {
            if (onApplicationExitRequest()) {
                Exit();
                return true;
            } else {
                debugOutput << "UltraCanvas: application exit requested denied" << std::endl;
                return false;
            }
        } else {
            Exit();
            return true;
        }
    }

    void UltraCanvasApplicationBase::Exit() {
        debugOutput << "UltraCanvas: application exit (set running=false)" << std::endl;
        running = false;
    }

    void UltraCanvasApplicationBase::PushEvent(const UCEvent& event) {
        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            eventQueue.push_back(event);
        }
        eventCondition.notify_one();
        WakeUpEventLoop();
    }

    bool UltraCanvasApplicationBase::PopEvent(UCEvent& event) {
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        if (eventQueue.empty()) {
            return false;
        }

        event = eventQueue.front();
        eventQueue.pop_front();
        return true;
    }

    void UltraCanvasApplicationBase::ProcessEvents() {
        UCEvent event;
        int processedEvents = 0;

        while (PopEvent(event) && processedEvents < 100) {
            processedEvents++;
            if (!running) {
                break;
            }
            DispatchEvent(event);

            if (event.type == UCEventType::MouseUp && capturedMouseButtonDown == event.button) {
                ReleaseMouse();
            }
        }
    }

    void UltraCanvasApplicationBase::WaitForEvents(int timeoutMs) {
        std::unique_lock<std::mutex> lock(eventQueueMutex);
        if (timeoutMs < 0) {
            eventCondition.wait(lock, [this] { return !eventQueue.empty() || !running; });
        } else {
            eventCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                    [this] { return !eventQueue.empty() || !running; });
        }
    }

    // ===== WINDOW MANAGEMENT =====
    void UltraCanvasApplicationBase::RegisterWindow(const std::shared_ptr<UltraCanvasWindowBase>& window) {
        if (window && window->GetNativeHandle() != 0) {
            windows.push_back(window);
            debugOutput << "UltraCanvas: Window registered with Native ID: " << window->GetNativeHandle() << std::endl;

            // Auto-register modal windows
            if (window->GetConfig().modal) {
                RegisterModalWindow(window);
            }
        }
    }

    void UltraCanvasApplicationBase::CleanupWindowReferences(UltraCanvasWindowBase* win) {
        UnregisterModalWindow(win);
        // Called from PerformClose() while the window is still alive, so the weak_ptrs
        // can still be locked and compared here.
        windowFocusHistory.Remove(win);
        if (focusedWindow.lock().get() == win) {
            focusedWindow.reset();

            // if (win->IsFocused()) {
            //     auto parentWin = win->GetParentWindow();
            //     if (parentWin && parentWin->IsVisible()) {
            //         auto parentWinState = parentWin->GetState();
            //         if ( parentWinState == WindowState::Normal || parentWinState == WindowState::Maximized || parentWinState == WindowState::Fullscreen) {
            //             parentWin->RaiseAndFocus();
            //             focusedWindow = parentWin->GetWindowWeakPtr();
            //         }
            //     }
            // }
        }
        if (capturedElement && capturedElement->GetWindow() == win) {
            ReleaseMouse();
        }
        if (hoveredElement && hoveredElement->GetWindow() == win) {
            hoveredElement = nullptr;
        }
        debugOutput << "UltraCanvas: window found and unregistered successfully" << std::endl;
    }

    void UltraCanvasApplicationBase::CleanupElementReferences(UltraCanvasUIElement* elem) {
        // Called from ~UltraCanvasUIElement

        {
            std::lock_guard<std::mutex> lock(eventQueueMutex);
            for (auto &eventsIt: eventQueue) {
                if (eventsIt.targetElement == elem) {
                    eventsIt.targetElement = nullptr;
                }
            }
        }

        if (currentEvent.targetElement == elem) {
            currentEvent.targetElement = nullptr;
        }

        if (capturedElement == elem) {
            ReleaseMouse();
        }
        if (hoveredElement == elem) {
            hoveredElement = nullptr;
        }
        auto win = elem->GetWindow();
        if (win && win->_focusedElement == elem) {
            win->_focusedElement = nullptr;
        }
    }

    // ===== MODAL WINDOW MANAGEMENT =====
    UltraCanvasWindowBase* UltraCanvasApplicationBase::GetCurrentModalWindow() {
        for (auto it = activeModalWindows.rbegin(); it != activeModalWindows.rend(); ++it) {
            auto locked = it->lock();
            if (!locked) continue;
            if (!locked->IsVisible()) continue;
            auto state = locked->GetState();
            if (state == WindowState::Closing || state == WindowState::Closed) continue;
            return locked.get();
        }
        return nullptr;
    }

    bool UltraCanvasApplicationBase::HasActiveModalWindow() {
        return GetCurrentModalWindow() != nullptr;
    }

    bool UltraCanvasApplicationBase::HandleModalWindowEvents(const UCEvent& event, UltraCanvasWindow* targetWindow) {
        auto* modalWindow = GetCurrentModalWindow();
        if (!modalWindow) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
            case UCEventType::MouseUp:
            case UCEventType::MouseMove:
            case UCEventType::MouseWheel:
            case UCEventType::MouseDoubleClick:
            case UCEventType::MouseEnter:
            case UCEventType::MouseLeave:
            case UCEventType::KeyDown:
            case UCEventType::KeyUp:
            case UCEventType::TextInput:
                if (targetWindow != modalWindow) return true;
                break;
            case UCEventType::WindowFocus:
                if (targetWindow && targetWindow != modalWindow) {
                    modalWindow->RaiseAndFocus();
                    return true;
                }
                break;
            default:
                return false;
        }
        return false;
    }

    void UltraCanvasApplicationBase::RegisterModalWindow(const std::shared_ptr<UltraCanvasWindowBase>& window) {
        if (window) {
            activeModalWindows.push_back(window);
        }
    }

    void UltraCanvasApplicationBase::UnregisterModalWindow(UltraCanvasWindowBase* window) {
        std::erase_if(activeModalWindows, 
            [window](const std::weak_ptr<UltraCanvasWindowBase>& w) {
                auto locked = w.lock();
                return !locked || locked.get() == window;
            }
        );
    }

    void UltraCanvasApplicationBase::UnregisterWindow(UltraCanvasWindowBase* window) {
        std::erase_if(windows, 
            [window](const std::shared_ptr<UltraCanvasWindowBase>& w) {
                return w.get() == window;
            }
        );
    }
    
    bool UltraCanvasApplicationBase::IsWindowRegistered(UltraCanvasWindowBase* window) {
        return (std::find_if(windows.begin(), windows.end(), 
            [window](const std::shared_ptr<UltraCanvasWindowBase>& w) {
                return w.get() == window;
            }
        ) != windows.end());
    }

    UltraCanvasWindow* UltraCanvasApplicationBase::FindWindow(NativeWindowHandle nativeHandle) {
        auto it = std::find_if(windows.begin(), windows.end(),
                               [nativeHandle](const std::shared_ptr<UltraCanvasWindowBase>& ptr) {
                                   return ptr->GetNativeHandle() == nativeHandle;
                               });

        if (it != windows.end()) {
           return (UltraCanvasWindow*)(it->get());
        } else {
            return nullptr;
        }
    }

    UltraCanvasWindow* UltraCanvasApplicationBase::GetFocusedWindow() {
        return static_cast<UltraCanvasWindow*>(focusedWindow.lock().get());
    }

    UltraCanvasUIElement* UltraCanvasApplicationBase::GetFocusedElement() {
        if (auto fw = focusedWindow.lock()) {
            return fw->GetFocusedElement();
        }
        return nullptr;
    }

    void UltraCanvasApplicationBase::SetFocusedWindowInternal(UltraCanvasWindowBase* window) {
        auto current = focusedWindow.lock();
        if (current.get() == window) {
            // No focus change, but make sure the window is at the front of the
            // history (covers the very first focus after window creation).
            if (current) windowFocusHistory.Touch(current);
            return;
        }
        if (current) {
            UCEvent blurEvent;
            blurEvent.type = UCEventType::WindowBlur;
            DispatchEventToElement(current.get(), blurEvent);
        }
        if (window) {
            UCEvent focusEvent;
            focusEvent.type = UCEventType::WindowFocus;
            DispatchEventToElement(window, focusEvent);
            focusedWindow = window->GetWindowWeakPtr();
            if (auto shared = focusedWindow.lock()) {
                windowFocusHistory.Touch(shared);
            }
        } else {
            focusedWindow.reset();
        }
    }

    // ===== JUMP TO LAST WINDOW =====
    bool UltraCanvasApplicationBase::JumpToLastWindow() {
        // A modal window owns the interaction; switching away would bypass it.
        if (HasActiveModalWindow()) {
            return false;
        }

        auto* current = focusedWindow.lock().get();
        auto eligible = [](const std::shared_ptr<UltraCanvasWindowBase>& w) {
            if (!w->IsCreated()) return false;
            auto state = w->GetState();
            if (state == WindowState::Closing || state == WindowState::Closed ||
                state == WindowState::Hidden) {
                return false;
            }
            // Popup windows (menus, dropdowns) are transient — never jump targets.
            if (w->GetConfig().type == WindowType::Popup) return false;
            return w->IsWindowVisible() || state == WindowState::Minimized;
        };

        auto target = windowFocusHistory.FindMostRecent(current, eligible);
        if (!target) {
            // No usable history yet (e.g. shortcut pressed right after startup):
            // fall back to the first other eligible registered window.
            for (const auto& w : windows) {
                if (w.get() != current && eligible(w)) {
                    target = w;
                    break;
                }
            }
        }
        if (!target) {
            return false;
        }

        if (target->GetState() == WindowState::Minimized) {
            target->Restore();
        }
        target->RaiseAndFocus();

        // Update focus bookkeeping immediately instead of waiting for the
        // asynchronous native focus notification, so repeated triggers toggle
        // between the two most recent windows predictably. The native
        // WindowFocus event that arrives later becomes a no-op. The target
        // window keeps its _focusedElement, so keyboard input resumes in the
        // element (input field) that was focused when the window was left.
        SetFocusedWindowInternal(target.get());
        return true;
    }

    void UltraCanvasApplicationBase::SetJumpToLastWindowKey(UCKeys key, bool ctrl, bool shift,
                                                            bool alt, bool meta) {
        jumpLastWindowKeyEnabled = (key != UCKeys::Unknown);
        jumpLastWindowKey = key;
        jumpLastWindowKeyCtrl = ctrl;
        jumpLastWindowKeyShift = shift;
        jumpLastWindowKeyAlt = alt;
        jumpLastWindowKeyMeta = meta;
    }

    void UltraCanvasApplicationBase::ClearJumpToLastWindowKey() {
        jumpLastWindowKeyEnabled = false;
        jumpLastWindowKey = UCKeys::Unknown;
    }

    void UltraCanvasApplicationBase::SetJumpToLastWindowMouseButton(UCMouseButton button) {
        jumpLastWindowMouseButton = button;
    }

    bool UltraCanvasApplicationBase::MatchesJumpToLastWindowTrigger(const UCEvent& event) const {
        if (jumpLastWindowKeyEnabled && event.type == UCEventType::KeyDown) {
            return event.virtualKey == jumpLastWindowKey &&
                   event.ctrl == jumpLastWindowKeyCtrl &&
                   event.shift == jumpLastWindowKeyShift &&
                   event.alt == jumpLastWindowKeyAlt &&
                   event.meta == jumpLastWindowKeyMeta;
        }
        if (jumpLastWindowMouseButton != UCMouseButton::NoneButton &&
            event.type == UCEventType::MouseDown) {
            return event.button == jumpLastWindowMouseButton;
        }
        return false;
    }

    void UltraCanvasApplicationBase::DispatchEvent(const UCEvent& event) {
        // Update modifier states and key pressed status
        if (event.type == UCEventType::KeyDown || event.type == UCEventType::KeyUp) {
            shiftHeld = event.shift;
            ctrlHeld = event.ctrl;
            altHeld = event.alt;
            metaHeld = event.meta;
            if (event.virtualKey >= 0 && event.virtualKey < 256) {
                keyStates[event.virtualKey] = (event.type == UCEventType::KeyDown);
            }
        }

        // Call global handlers first
        for (auto& handler : globalEventHandlers) {
            if (handler(event)) {
                return; // Event consumed by global handler
            }
        }

        // Jump-to-last-window trigger is an application-level action; consume
        // the event before any window/element sees it so a bound key or mouse
        // button never leaks into the UI as input.
        if (MatchesJumpToLastWindowTrigger(event)) {
            JumpToLastWindow();
            return;
        }

        // ===== NEW: IMPROVED TARGET WINDOW DETECTION =====
        UltraCanvasWindow* targetWindow = nullptr;

        // First priority: Use the window information stored in the event. An expired
        // weak_ptr (window already destroyed) naturally falls through to the fallbacks.
        if (auto tw = event.targetWindow.lock()) {
            targetWindow = static_cast<UltraCanvasWindow*>(tw.get());
            if (std::find_if(windows.begin(), windows.end(), [&tw](auto const &item) {
                return item == tw;
            }) == windows.end()) {
                debugOutput << "UltraCanvasApplicationBase::DispatchEvent stale event for already deleted window ev=" << event.ToString() << " win="<<targetWindow << std::endl;
                return;
            }
        }
            // Fallback: Try to find window by native handle
        else if (event.nativeWindowHandle != 0) {
            targetWindow = FindWindow(event.nativeWindowHandle);
        }
            // Last resort: Use focused window for certain event types
        else {
            // Only use focused window for keyboard events when no target is found
            if (event.type == UCEventType::KeyDown ||
                event.type == UCEventType::KeyUp) {
                targetWindow = GetFocusedWindow();
            }
        }

        // block some events if modal window active
        if (HandleModalWindowEvents(event, targetWindow)) {
            return;
        }

       if (event.type == UCEventType::MouseDown) {
           if (targetWindow && GetFocusedWindow() != targetWindow) {
               debugOutput << "Window clicked but not focused, set focus. target=" << targetWindow << " focused=" << GetFocusedWindow() << std::endl;
               SetFocusedWindowInternal(targetWindow);
           }
       }

        // Handle different event types
        switch (event.type) {
            case UCEventType::MouseMove:
            case UCEventType::MouseUp:
                if (capturedElement) {
                    if (DispatchEventToElement(capturedElement, event)) {
                        return;
                    }
                }
                break;
            case UCEventType::WindowFocus:
                if (targetWindow && GetFocusedWindow() != targetWindow) {
                    // Update focused window + MRU focus history
                    SetFocusedWindowInternal(targetWindow);
                    debugOutput << "UltraCanvasBaseApplication: Window " << targetWindow << " (native=" << targetWindow->GetNativeHandle() << ") gained focus" << std::endl;
                }
                return;
            case UCEventType::WindowBlur:
                if (targetWindow && targetWindow == GetFocusedWindow()) {
                    debugOutput << "UltraCanvasBaseApplication: Window " << targetWindow << " (native=" << targetWindow->GetNativeHandle() << ") lost focus" << std::endl;
                    DispatchEventToElement(targetWindow, event);
                    focusedWindow.reset();
                }
                return;
        }
        // Dispatch other events to focused element
        if (targetWindow) {
            UltraCanvasUIElement* elementUnderPointer = nullptr;
//            UltraCanvasUIElement* originalElementUnderPointer = nullptr;

            PopupElement* popupElement = targetWindow->GetActivePopupElement();
//            std::shared_ptr<UltraCanvasUIElement> popupOwner;
            bool isPointerOutsidePopupElement = false;

//            if (popupElement) {
//                popupOwner = popupElement->settings.popUpOwner.lock();
//            }

            // Check if an element is the popup owner or a child of it
//            auto isPopupOwnerOrChild = [&popupElement](UltraCanvasUIElement* elem) -> bool {
//                if (!popupElement || !elem) return false;
//                auto owner = popupElement->settings.popUpOwner.lock();
//                if (!owner) return false;
//                if (elem == owner.get()) return true;
//                auto* ownerContainer = dynamic_cast<UltraCanvasContainer*>(owner.get());
//                return ownerContainer && ownerContainer->HasChild(elem);
//            };

            if (event.IsMouseEvent() || event.IsDragEvent()) { // change mouse cursor first
                elementUnderPointer = targetWindow->FindElementAtPoint(event.pointerWindow);
  //              originalElementUnderPointer = elementUnderPointer;
                // set pointerElem to popup element if it points outside
                if (popupElement) {
                    if (elementUnderPointer) {
                        if (elementUnderPointer != popupElement->element) {
                            auto *popupContainer = dynamic_cast<UltraCanvasContainer *>(popupElement->element);
                            if (!popupContainer || !popupContainer->HasChild(elementUnderPointer)) {
                                auto popupOwner = popupElement->settings.popupOwner.lock();
                                if (!popupOwner) {
                                    isPointerOutsidePopupElement = true;
                                } else {
                                    if (popupOwner.get() != elementUnderPointer) {
                                        auto* ownerContainer = dynamic_cast<UltraCanvasContainer*>(popupOwner.get());
                                        if (!ownerContainer || !ownerContainer->HasChild(elementUnderPointer)) {
                                            isPointerOutsidePopupElement = true;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        isPointerOutsidePopupElement = true;
                    }

                    if (isPointerOutsidePopupElement) {
                        elementUnderPointer = popupElement->element;
                    }
                }
                // change mouse cursor
                if (elementUnderPointer) {
                    if (targetWindow->GetCurrentMouseCursor() != elementUnderPointer->GetMouseCursor()) {
                        targetWindow->SelectMouseCursor(elementUnderPointer->GetMouseCursor());
                    }
                } else {
                    // if no element pointed then select window's cursor
                    if (targetWindow->GetCurrentMouseCursor() != targetWindow->GetMouseCursor()) {
                        targetWindow->SelectMouseCursor(targetWindow->GetMouseCursor());
                    }
                }
            }

            // close popup element handling first
            if (popupElement) {
                // THERE element->Contains() WHICH IS NOT THE SAME AS element->GetBounds().Contains(),
                // THE element->Contains() MAY CHECK CHILD ELEMENTS TOO (SUBMENUS OR PARENT MENUS FOR EXAMPLE)
                if (popupElement->settings.closeByClickOutside
                    && event.type == UCEventType::MouseDown
                    && event.button != UCMouseButton::NoneButton
                    && isPointerOutsidePopupElement
                    && !popupElement->element->ContainsInWindow(event.pointerWindow)) {
                    targetWindow->ClosePopup(*popupElement->element, ClosePopupReason::ClickOutside);
                    goto finish;
                }
                if (popupElement->settings.closeByEscapeKey
                    && event.IsKeyboardEvent()
                    && event.virtualKey == UCKeys::Escape) {
                    targetWindow->ClosePopup(*popupElement->element, ClosePopupReason::EscapeKey);
                    goto finish;
                }
            }

            if (event.IsKeyboardEvent()) {
                UltraCanvasUIElement* focused =  targetWindow->GetFocusedElement();
                // sent event to popup directly if real focused element outside popup
                if (popupElement && focused != popupElement->element) {
                    UltraCanvasContainer* popupContainer = dynamic_cast<UltraCanvasContainer*>(popupElement->element);
                    if (!popupContainer || !popupContainer->HasChild(focused)) {
                        auto popupOwner = popupElement->settings.popupOwner.lock();
                        if (!popupOwner) {
                            focused = popupElement->element;
                        } else if (popupOwner.get() != focused) {
                            auto* ownerContainer = dynamic_cast<UltraCanvasContainer*>(popupOwner.get());
                            if (!ownerContainer || !ownerContainer->HasChild(focused)) {
                                focused = popupElement->element;
                            }
                        }
                    }
                }
                if (focused) {
                    HandleEventWithBubbling(focused, event);
                    goto finish;
                }
            }

            if (event.type == UCEventType::MouseWheel && elementUnderPointer) {
                HandleEventWithBubbling(elementUnderPointer, event);
                goto finish;

            }
            if (event.IsDragEvent()) {
                if (elementUnderPointer) {
                    HandleEventWithBubbling(elementUnderPointer, event);
                } else {
                    DispatchEventToElement(targetWindow, event);
                }
                goto finish;
            }

            if (event.IsMouseEvent()) {
                if (hoveredElement && hoveredElement != elementUnderPointer) {
                    if (hoveredElement->GetWindow() == targetWindow && hoveredElement->IsVisible()) {
                        UCEvent leaveEvent = event;
                        leaveEvent.type = UCEventType::MouseLeave;
                        leaveEvent.pointer = { -1, -1 };
                        DispatchEventToElement(hoveredElement, leaveEvent);
                    }
                    UltraCanvasTooltipManager::HideTooltip();
                    hoveredElement = nullptr;
                }
                if (!elementUnderPointer || elementUnderPointer == targetWindow) {
                    UltraCanvasTooltipManager::HideTooltip();
                }
                if (elementUnderPointer) {
                    if (hoveredElement != elementUnderPointer) {
                        auto enterEvent = event.Clone();
                        enterEvent.type = UCEventType::MouseEnter;
                        DispatchEventToElement(elementUnderPointer, enterEvent);

                        hoveredElement = elementUnderPointer;
                        // Show tooltip if element has one
                        if (!elementUnderPointer->GetTooltip().empty()) {
                            UltraCanvasTooltipManager::UpdateAndShowTooltip(
                                    targetWindow, elementUnderPointer->GetTooltip(),
                                    event.pointerWindow);
                        }
                    }
                    // Update tooltip position as mouse moves
                    if (!elementUnderPointer->GetTooltip().empty() &&
                        (UltraCanvasTooltipManager::IsVisible() || UltraCanvasTooltipManager::IsPending())) {
                        UltraCanvasTooltipManager::UpdateTooltipPosition(
                            event.pointerWindow);
                    }

                    if (DispatchEventToElement(elementUnderPointer, event)) {
                        goto finish;
                    }
                }
            }

            if (event.isCommandEvent()) {
                HandleEventWithBubbling(event.targetElement, event);
                goto finish;
            }
            DispatchEventToElement(targetWindow, event);

    finish:
            return;
//            // Debug logging
//            if (event.type != UCEventType::MouseMove) {
//                debugOutput << "UltraCanvas: Event type " << static_cast<int>(event.type)
//                          << " dispatched to window " << targetWindow
//                          << " (X11 Window: " << std::hex << event.nativeWindowHandle << std::dec << ")"
//                          << " focused=" << (targetWindow == focusedWindow ? "yes" : "no") << std::endl;
        } else {
            // No target window found - this might be normal for some system events
            debugOutput << "UltraCanvas: Warning - Event " << event.ToString()
                      << " has no target window (Native Window: " << std::hex << event.nativeWindowHandle << std::dec << ")" << std::endl;
        }
    }

    bool UltraCanvasApplicationBase::HandleEventWithBubbling(UltraCanvasUIElement *elem, const UCEvent &event) {
        if (!elem) {
            return false;  // target element was destroyed before the event was processed
        }
        if (!event.isCommandEvent()) {
            if (DispatchEventToElement(elem, event)) {
                return true;
            }
        }
        auto parent = elem->GetParentContainer();
        while(parent) {
            if (DispatchEventToElement(parent, event)) {
                return true;
            }
            parent = parent->GetParentContainer();
        }
        return false;
    }


    void UltraCanvasApplicationBase::FocusNextElement() {
        if (auto fw = focusedWindow.lock()) {
            fw->FocusNextElement();
        }
    }

    void UltraCanvasApplicationBase::FocusPreviousElement() {
        if (auto fw = focusedWindow.lock()) {
            fw->FocusPreviousElement();
        }
    }

    void UltraCanvasApplicationBase::RegisterEventLoopRunCallback(std::function<void()> callback) {
        eventLoopCallback = callback;
    }

    bool UltraCanvasApplicationBase::DispatchEventToElement(UltraCanvasUIElement* elem, UCEvent event) {
        event.targetElement = elem;
        auto window = elem->GetWindow();
        if (!window) {
            debugOutput << "UltraCanvasApplicationBase::DispatchEventToElement window == null for elem=" << elem << std::ends;
            return false;
        }
//        if (event.type != UCEventType::MouseMove) {
//            debugOutput << "DispatchEventToElement ev=" << event.ToString() << " target elem=" << elem << " target win=" << elem->GetWindow() << " focused=" << focusedWindow << std::endl;
//        }
        if (event.IsMouseEvent() || event.IsDragEvent() || event.type == UCEventType::MouseEnter) {
            event.pointer = elem->MapToLocal(event.pointerWindow, nullptr);
        }

        currentEvent = event;

        if (window->HandleEventFilters(event)) {
            return true;
        }

        return elem->OnEvent(event);
    }

    void UltraCanvasApplicationBase::CaptureMouse(UltraCanvasUIElement *element) {
        capturedMouseButtonDown = currentEvent.button;
        capturedElement = element;
        CaptureMouseNative();
    }

    void UltraCanvasApplicationBase::ReleaseMouse() {
        if (capturedElement) {
            ReleaseMouseNative();
        }
        capturedElement = nullptr;
        capturedMouseButtonDown = UCMouseButton::NoneButton;
    }

    // ===== TIMER SYSTEM =====

    TimerId UltraCanvasApplicationBase::StartTimer(unsigned int ms_interval, bool periodic,
                                                    std::function<void(TimerId)> callback) {
        std::lock_guard<std::mutex> lock(timersMutex_);
        UltraCanvasTimer timer;
        timer.id = nextTimerId_++;
        timer.interval = std::chrono::milliseconds(ms_interval);
        timer.periodic = periodic;
        timer.active = true;
        timer.nextFire = std::chrono::steady_clock::now() + timer.interval;
        timer.callback = std::move(callback);
        timers_.push_back(std::move(timer));
        WakeUpEventLoop();
        return timers_.back().id;
    }

    void UltraCanvasApplicationBase::StopTimer(TimerId id) {
        std::lock_guard<std::mutex> lock(timersMutex_);
        for (auto& timer : timers_) {
            if (timer.id == id) {
                timer.active = false;
                return;
            }
        }
    }

    void UltraCanvasApplicationBase::ProcessTimers() {
        auto now = std::chrono::steady_clock::now();

        // Collect the work to run for timers that are due. We must NOT invoke
        // callbacks (or touch a timers_ element) while iterating: callbacks are
        // documented to call StartTimer()/StopTimer() from any thread, and
        // StartTimer() does timers_.push_back() which can reallocate the vector.
        // So we snapshot each due timer's id + callback under the lock, advance
        // or deactivate the timer in place (still under the lock), and only run
        // the callbacks after the lock is released. A null callback means push a
        // Timer UCEvent instead. Timers added during callbacks appear next round.
        std::vector<std::pair<TimerId, std::function<void(TimerId)>>> due;
        {
            std::lock_guard<std::mutex> lock(timersMutex_);
            for (auto& timer : timers_) {
                if (!timer.active) continue;
                if (timer.nextFire > now) continue;

                due.emplace_back(timer.id, timer.callback);

                // Advance (periodic) or deactivate (one-shot). If a collected
                // callback later calls StopTimer() on its own periodic timer, it
                // marks active=false and the timer is cleaned up next round, so a
                // self-stopped timer never fires again.
                if (timer.periodic) {
                    timer.nextFire += timer.interval;
                    // If we fell behind, skip to next future fire time
                    if (timer.nextFire <= now) {
                        auto elapsed = now - timer.nextFire;
                        auto periods = elapsed / timer.interval + 1;
                        timer.nextFire += timer.interval * periods;
                    }
                } else {
                    timer.active = false;
                }
            }

            // Clean up inactive timers (safe: everything needed is already in `due`)
            timers_.erase(
                std::remove_if(timers_.begin(), timers_.end(),
                               [](const UltraCanvasTimer& t) { return !t.active; }),
                timers_.end());
        }

        // Run callbacks / push events without holding timersMutex_, so a callback
        // is free to call StartTimer()/StopTimer() without deadlocking, and no
        // timers_ element is accessed across a callback.
        for (auto& [id, callback] : due) {
            if (callback) {
                callback(id);
            } else {
                UCEvent timerEvent;
                timerEvent.type = UCEventType::Timer;
                timerEvent.userDataInt = static_cast<int>(id);
                // Push directly to queue without calling WakeUpEventLoop (we're already on the main thread)
                std::lock_guard<std::mutex> lock(eventQueueMutex);
                eventQueue.push_back(timerEvent);
            }
        }
    }

    std::chrono::milliseconds UltraCanvasApplicationBase::GetTimeUntilNextTimer() const {
        std::lock_guard<std::mutex> lock(timersMutex_);
        auto earliest = std::chrono::steady_clock::time_point::max();
        for (const auto& timer : timers_) {
            if (timer.active && timer.nextFire < earliest) {
                earliest = timer.nextFire;
            }
        }
        if (earliest == std::chrono::steady_clock::time_point::max()) {
            return std::chrono::milliseconds::max(); // No active timers
        }
        auto now = std::chrono::steady_clock::now();
        if (earliest <= now) {
            return std::chrono::milliseconds(0);
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(earliest - now);
    }
}
