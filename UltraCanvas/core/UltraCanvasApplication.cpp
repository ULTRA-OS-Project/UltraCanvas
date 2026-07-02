// UltraCanvasApplication.cpp
// Main UltraCanvas App
// Version: 1.5.0 - focusedWindow/hovered/captured/dragged and UCEvent targets are now weak_ptr
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include <algorithm>
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
            eventQueue.push(event);
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
        eventQueue.pop();
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
        if (focusedWindow.lock().get() == win) {
            focusedWindow.reset();

            // if (win->IsFocused()) {
            //     auto parentWin = win->GetParentWindow();
            //     if (parentWin && parentWin->IsVisible()) {
            //         auto parentWinState = parentWin->GetState();
            //         if ( parentWinState == WindowState::Normal || parentWinState == WindowState::Maximized || parentWinState == WindowState::Fullscreen) {
            //             parentWin->RaiseAndFocus();
            //             focusedWindow = parentWin->GetWeakWindow();
            //         }
            //     }
            // }
        }
        if (auto ce = capturedElement.lock(); ce && ce->GetWindow() == win) {
            capturedElement.reset();
        }
        if (auto he = hoveredElement.lock(); he && he->GetWindow() == win) {
            hoveredElement.reset();
        }
        if (auto de = draggedElement.lock(); de && de->GetWindow() == win) {
            draggedElement.reset();
        }
        debugOutput << "UltraCanvas: window found and unregistered successfully" << std::endl;
    }

    void UltraCanvasApplicationBase::CleanupElementReferences(UltraCanvasUIElement* elem) {
        // Called from ~UltraCanvasUIElement, where the weak_ptrs referencing elem are
        // already expired (auto-handling the dangling case). These comparisons are kept
        // for correctness on any path where elem is still live.
        if (capturedElement.lock().get() == elem) {
            ReleaseMouse();
        }
        if (hoveredElement.lock().get() == elem) {
            hoveredElement.reset();
        }
        if (draggedElement.lock().get() == elem) {
            draggedElement.reset();
        }
        auto win = elem->GetWindow();
        if (win && win->IsCreated() && win->GetState() != WindowState::Closing &&  win->GetState() != WindowState::Closed && win->GetFocusedElement() == elem) {
            win->SetFocusedElement(nullptr);
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
            case UCEventType::Shortcut:
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

    void UltraCanvasApplicationBase::DispatchEvent(const UCEvent& event) {
        // Update modifier states
        if (event.IsKeyboardEvent()) {
            shiftHeld = event.shift;
            ctrlHeld = event.ctrl;
            altHeld = event.alt;
            metaHeld = event.meta;
        }

        // Call global handlers first
        for (auto& handler : globalEventHandlers) {
            if (handler(event)) {
                return; // Event consumed by global handler
            }
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
                event.type == UCEventType::KeyUp ||
                event.type == UCEventType::Shortcut) {
                targetWindow = GetFocusedWindow();
            }
        }

        // block some events if modal window active
        if (HandleModalWindowEvents(event, targetWindow)) {
            return;
        }

       if (event.type == UCEventType::MouseDown) {
           if (targetWindow && event.type == UCEventType::MouseDown && GetFocusedWindow() != targetWindow) {
               debugOutput << "Window clicked but not focused, set focus. target=" << targetWindow << " focused=" << GetFocusedWindow() << std::endl;
               if (auto fw = focusedWindow.lock()) {
                   UCEvent ev{.type = UCEventType::WindowBlur};
                   DispatchEventToElement(fw.get(), event);
               }
               UCEvent ev{.type = UCEventType::WindowFocus};
               DispatchEventToElement(targetWindow, event);
               focusedWindow = targetWindow->GetWindowWeakPtr();
           }
       }

        // Handle different event types
        switch (event.type) {
            case UCEventType::MouseMove:
            case UCEventType::MouseUp:
                if (auto ce = capturedElement.lock()) {
                    if (DispatchEventToElement(ce.get(), event)) {
                        return;
                    }
                }
                break;

            case UCEventType::KeyDown:
            case UCEventType::KeyUp:
                if (event.nativeKeyCode >= 0 && event.nativeKeyCode < 256) {
                    keyStates[event.nativeKeyCode] = (event.type == UCEventType::KeyDown);
                }
                break;
            case UCEventType::WindowFocus:
                if (targetWindow && GetFocusedWindow() != targetWindow) {
                    // Update focused window
                    DispatchEventToElement(targetWindow, event);
                    focusedWindow = targetWindow->GetWindowWeakPtr();
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
                if (auto he = hoveredElement.lock(); he && he.get() != elementUnderPointer) {
                    if (he->GetWindow() == targetWindow && he->IsVisible()) {
                        UCEvent leaveEvent = event;
                        leaveEvent.type = UCEventType::MouseLeave;
                        leaveEvent.pointer = { -1, -1 };
                        DispatchEventToElement(he.get(), leaveEvent);
                    }
                    UltraCanvasTooltipManager::HideTooltip();
                    hoveredElement.reset();
                }
                if (!elementUnderPointer || elementUnderPointer == targetWindow) {
                    UltraCanvasTooltipManager::HideTooltip();
                }
                if (elementUnderPointer) {
                    if (hoveredElement.lock().get() != elementUnderPointer) {
                        auto enterEvent = event.Clone();
                        enterEvent.type = UCEventType::MouseEnter;
                        DispatchEventToElement(elementUnderPointer, enterEvent);

                        hoveredElement = elementUnderPointer->weak_from_this();
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
                HandleEventWithBubbling(event.targetElement.lock().get(), event);
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
        event.targetElement = elem->weak_from_this();
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
        capturedElement = element ? element->weak_from_this() : std::weak_ptr<UltraCanvasUIElement>();
        CaptureMouseNative();
    }

    void UltraCanvasApplicationBase::ReleaseMouse() {
        if (!capturedElement.expired()) {
            ReleaseMouseNative();
        }
        capturedElement.reset();
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

        // Snapshot the current timer count so timers added during callbacks are not processed this round
        size_t count;
        {
            std::lock_guard<std::mutex> lock(timersMutex_);
            count = timers_.size();
        }

        for (size_t i = 0; i < count; ++i) {
            // Re-check bounds in case timers were removed
            if (i >= timers_.size()) break;

            auto& timer = timers_[i];
            if (!timer.active) continue;
            if (timer.nextFire > now) continue;

            // Fire the timer
            if (timer.callback) {
                timer.callback(timer.id);
            } else {
                UCEvent timerEvent;
                timerEvent.type = UCEventType::Timer;
                timerEvent.userDataInt = static_cast<int>(timer.id);
                // Push directly to queue without calling WakeUpEventLoop (we're already on the main thread)
                {
                    std::lock_guard<std::mutex> lock(eventQueueMutex);
                    eventQueue.push(timerEvent);
                }
            }

            // Advance or deactivate
            if (timer.periodic && timer.active) {
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

        // Clean up inactive timers
        {
            std::lock_guard<std::mutex> lock(timersMutex_);
            timers_.erase(
                std::remove_if(timers_.begin(), timers_.end(),
                               [](const UltraCanvasTimer& t) { return !t.active; }),
                timers_.end());
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
