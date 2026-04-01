// UltraCanvasApplication.cpp
// Main UltraCanvas App
// Version: 1.0.0
// Last Modified: 2025-01-07
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
                // Check for visible windows, delete/cleanup windows
rescan_windows:
                for (auto it = windows.begin(); it != windows.end(); it++) {
                    auto window = it->get();
                    if (window->GetState() == WindowState::DeleteRequested) {
                        window->Destroy();
                    }
                    if (window->GetState() == WindowState::Deleted) {
                        CleanupWindowReferences(window);
                        windows.erase(it);
                        goto rescan_windows;
                    }
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

                // Update and render all windows
                if (clipbrd) {
                    clipbrd->Update();
                }
                if (eventLoopCallback) {
                    eventLoopCallback();
                }
                UltraCanvasTooltipManager::Update();

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
                window->Destroy();
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
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        eventQueue.push(event);
        eventCondition.notify_one();
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
        }
    }

    void UltraCanvasApplicationBase::CleanupWindowReferences(UltraCanvasWindowBase* win) {
        if (focusedWindow == win) {
            focusedWindow = nullptr;
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
            int dx = event.x - lastMouseEvent.x;
            int dy = event.y - lastMouseEvent.y;
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
        if (UltraCanvasDialogManager::HandleModalEvents(event, targetWindow)) {
            return;
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
                if (targetWindow) {
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

            case UCEventType::Redraw:
                if (event.targetElement) {
                    event.targetElement->RequestRedraw();
                } else if (targetWindow) {
                    targetWindow->RequestRedraw();
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
                elementUnderPointer = targetWindow->FindElementAtPoint(event.windowX, event.windowY);
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
                    && !popupElement->element->Contains(event.windowX, event.windowY)) {
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
                        leaveEvent.x = -1;
                        leaveEvent.y = -1;
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
                                    Point2Di(event.windowX, event.windowY));
                        }
                    }
                    // Update tooltip position as mouse moves
                    if (!elementUnderPointer->GetTooltip().empty() &&
                        (UltraCanvasTooltipManager::IsVisible() || UltraCanvasTooltipManager::IsPending())) {
                        UltraCanvasTooltipManager::UpdateTooltipPosition(
                            Point2Di(event.windowX, event.windowY));
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
            debugOutput << "UltraCanvas: Warning - Event type " << static_cast<int>(event.type)
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
        if (event.IsMouseEvent() || event.IsDragEvent() || event.type == UCEventType::MouseEnter) {
            if (elem->GetParentContainer() != window && elem != window) {
                event.x = event.windowX;
                event.y = event.windowY;
                elem->ConvertWindowToParentContainerCoordinates(event.x, event.y);
            }
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
}
