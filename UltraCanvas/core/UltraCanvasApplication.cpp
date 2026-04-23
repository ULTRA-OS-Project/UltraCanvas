// UltraCanvasApplication.cpp
// Main UltraCanvas App
// Version: 1.1.0
// Last Modified: 2026-04-06
// Author: UltraCanvas Framework

#include <algorithm>
#include <iostream>
#include <filesystem>
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasModalDialog.h"

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


//    void UltraCanvasApplicationBase::MoveWindowEventFilters(UltraCanvasWindowBase* winFrom, UltraCanvasUIElement* elem) {
//        if (!elem) return;
//
//        std::vector<UCEventType> interestedEvents;
//        if (winFrom) {
//            if (!winFrom->eventFilters.empty()) {
//                for(auto &ef : winFrom->eventFilters) {
//                    auto &elems = ef.second;
//                    if (elems.find(elem) != elems.end()) {
//                        interestedEvents.push_back(ef.first);
//                        elems.erase(elem);
//                    }
//                }
//            }
//        } else {
//            if (!pendingUnassignedEventFilters.empty()) {
//                auto found = pendingUnassignedEventFilters.find(elem);
//                if (found != pendingUnassignedEventFilters.end()) {
//                    interestedEvents = found->second;
//                    pendingUnassignedEventFilters.erase(elem);
//                }
//            }
//        }
//        if (!interestedEvents.empty()) {
//            UltraCanvasApplicationBase::InstallWindowEventFilter(elem, interestedEvents);
//        }
//    }

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
            if (!InitializeClipboard()) {
                debugOutput << "UltraCanvas: Failed to initialize clipboard" << std::endl;
            }

            // Auto-set default window icon if available
            std::string iconPath = GetResourcesDir() + UC_DEFAULT_ICON_SUBPATH;
            if (std::filesystem::exists(iconPath)) {
                SetDefaultWindowIcon(iconPath);
                debugOutput << "UltraCanvas: Default window icon set to: " << iconPath << std::endl;
            }
#ifdef UCAPP_ICON_PATH
            else {
                // Fallback to app-specific icon defined at build time
                std::string appIconPath = GetResourcesDir() + UCAPP_ICON_PATH;
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
                        auto ctx = window->GetRenderContext();
                        if (window->IsNeedsResize()) {
                            window->DoResize();
                        }
                        if (ctx) {
                            if (window->IsNeedsUpdateGeometry() || window->IsNeedsRedraw()) {
                                window->UpdateGeometry(ctx);
                            }
                            if (window->IsNeedsRedraw()) {
//                                debugOutput << "Redraw window w=" << window << " nativeh=" << window->GetNativeHandle() << std::endl;
                                window->Render(ctx);
                                window->Flush();
                            }
                        }
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
        if (focusedWindow == win) {
            focusedWindow = nullptr;

            // if (win->IsFocused()) {
            //     auto parentWin = win->GetParentWindow();
            //     if (parentWin && parentWin->IsVisible()) {
            //         auto parentWinState = parentWin->GetState();
            //         if ( parentWinState == WindowState::Normal || parentWinState == WindowState::Maximized || parentWinState == WindowState::Fullscreen) {
            //             parentWin->RaiseAndFocus();
            //             focusedWindow = (UltraCanvasWindow*)parentWin;
            //         }
            //     }
            // }
        }
        if (capturedElement && capturedElement->GetWindow() == win) {
            capturedElement = nullptr;
        }
        if (hoveredElement && hoveredElement->GetWindow() == win) {
            hoveredElement = nullptr;
        }
        if (draggedElement && draggedElement->GetWindow() == win) {
            draggedElement = nullptr;
        }
        debugOutput << "UltraCanvas: window found and unregistered successfully" << std::endl;
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

    UltraCanvasUIElement* UltraCanvasApplicationBase::GetFocusedElement() {
        if (focusedWindow) {
            return focusedWindow->GetFocusedElement();
        }
        return nullptr;
    }

    bool UltraCanvasApplicationBase::IsDoubleClick(const UCEvent &event) {
        auto now = std::chrono::steady_clock::now();
        auto timeDiff = std::chrono::duration<float>(now - lastClickTime).count();

        bool isDoubleClick = false;
        if (timeDiff <= DOUBLE_CLICK_TIME) {
            int dx = event.pointer.x - lastMouseEvent.pointer.x;
            int dy = event.pointer.y - lastMouseEvent.pointer.y;
            int distance = static_cast<int>(std::sqrt(dx * dx + dy * dy));

            if (distance <= DOUBLE_CLICK_DISTANCE) {
                isDoubleClick = true;
            }
        }

        lastMouseEvent = event;
        lastClickTime = now;

        return isDoubleClick;
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

        // First priority: Use the window information stored in the event
        if (event.targetWindow != nullptr) {
            targetWindow = static_cast<UltraCanvasWindow*>(event.targetWindow);
            if (std::find_if(windows.begin(), windows.end(), [targetWindow](auto const &item) {
                return item.get() == targetWindow;
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
                targetWindow = focusedWindow;
            }
        }

        // block some events if modal window active
        if (HandleModalWindowEvents(event, targetWindow)) {
            return;
        }

       if (event.type == UCEventType::MouseDown) {
           if (targetWindow && event.type == UCEventType::MouseDown && focusedWindow != targetWindow) {
               debugOutput << "Window clicked but not focused, set focus. target=" << targetWindow << " focused=" << focusedWindow << std::endl;
               if (focusedWindow) {
                   UCEvent ev{.type = UCEventType::WindowBlur};
                   DispatchEventToElement(focusedWindow, event);
               }
               UCEvent ev{.type = UCEventType::WindowFocus};
               DispatchEventToElement(targetWindow, event);
               focusedWindow = targetWindow;
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

            case UCEventType::KeyDown:
            case UCEventType::KeyUp:
                if (event.nativeKeyCode >= 0 && event.nativeKeyCode < 256) {
                    keyStates[event.nativeKeyCode] = (event.type == UCEventType::KeyDown);
                }
                break;
            case UCEventType::WindowFocus:
                if (targetWindow && focusedWindow != targetWindow) {
                    // Update focused window
                    DispatchEventToElement(targetWindow, event);
                    focusedWindow = targetWindow;
                    debugOutput << "UltraCanvasBaseApplication: Window " << focusedWindow << " (native=" << focusedWindow->GetNativeHandle() << ") gained focus" << std::endl;
                }
                return;
            case UCEventType::WindowBlur:
                if (targetWindow && targetWindow == focusedWindow) {
                    debugOutput << "UltraCanvasBaseApplication: Window " << focusedWindow << " (native=" << focusedWindow->GetNativeHandle() << ") lost focus" << std::endl;
                    DispatchEventToElement(targetWindow, event);
                    focusedWindow = nullptr;
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
        if (focusedWindow) {
            focusedWindow->FocusNextElement();
        }
    }

    void UltraCanvasApplicationBase::FocusPreviousElement() {
        if (focusedWindow) {
            focusedWindow->FocusPreviousElement();
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
        if (event.type != UCEventType::MouseMove) {
            debugOutput << "DispatchEventToElement ev=" << event.ToString() << " target elem=" << elem << " target win=" << elem->GetWindow() << " focused=" << focusedWindow << std::endl;
        }
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
        CaptureMouseNative();
        capturedElement = element;
    }

    void UltraCanvasApplicationBase::ReleaseMouse(UltraCanvasUIElement *element) {
        if (element && element == capturedElement) {
            capturedElement = nullptr;
        }
        ReleaseMouseNative();
    }

    // ===== TIMER SYSTEM =====

    TimerId UltraCanvasApplicationBase::StartTimer(std::chrono::milliseconds interval, bool periodic,
                                                    std::function<void(TimerId)> callback) {
        std::lock_guard<std::mutex> lock(timersMutex_);
        UltraCanvasTimer timer;
        timer.id = nextTimerId_++;
        timer.interval = interval;
        timer.periodic = periodic;
        timer.active = true;
        timer.nextFire = std::chrono::steady_clock::now() + interval;
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
