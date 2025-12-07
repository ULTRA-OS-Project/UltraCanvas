// UltraCanvasApplication.cpp
// Main UltraCanvas App
// Version: 1.0.0
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework

#include <sys/select.h>
#include <algorithm>
#include <iostream>
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasTooltipManager.h"


namespace UltraCanvas {
    bool UltraCanvasBaseApplication::Initialize(const std::string& app) {
        appName = app;
        if (InitializeNative()) {
            if (!InitializeClipboard()) {
                std::cerr << "UltraCanvas: Failed to initialize clipboard" << std::endl;
            }
            return true;
        } else {
            std::cerr << "UltraCanvas: Failed to initialize application" << std::endl;
            return false;
        }
    }

    void UltraCanvasBaseApplication::Shutdown() {
        ShutdownClipboard();
        ShutdownNative();
    }

    void UltraCanvasBaseApplication::Run() {
        RunNative();
        Shutdown();
    }

    void UltraCanvasBaseApplication::PushEvent(const UCEvent& event) {
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        eventQueue.push(event);
        eventCondition.notify_one();
    }

    bool UltraCanvasBaseApplication::PopEvent(UCEvent& event) {
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        if (eventQueue.empty()) {
            return false;
        }

        event = eventQueue.front();
        eventQueue.pop();
        return true;
    }

    void UltraCanvasBaseApplication::ProcessEvents() {
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

    void UltraCanvasBaseApplication::WaitForEvents(int timeoutMs) {
        std::unique_lock<std::mutex> lock(eventQueueMutex);
        if (timeoutMs < 0) {
            eventCondition.wait(lock, [this] { return !eventQueue.empty() || !running; });
        } else {
            eventCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                    [this] { return !eventQueue.empty() || !running; });
        }
    }

    // ===== WINDOW MANAGEMENT =====
    void UltraCanvasBaseApplication::RegisterWindow(UltraCanvasWindowBase *window) {
        if (window && window->GetNativeHandle() != 0) {
            std::cout << "UltraCanvas: Native window created successfully" << std::endl;
            windows.push_back(window);
            std::cout << "UltraCanvas: Window registered with Native ID: " << window->GetNativeHandle() << std::endl;
        }
    }

    void UltraCanvasBaseApplication::UnregisterWindow(UltraCanvasWindowBase *window) {
        if (window) {
            auto it = std::find_if(windows.begin(), windows.end(),
                                   [window](const UltraCanvasWindowBase* ptr) {
                                       return ptr == window;
                                   });

            if (it != windows.end()) {
                if (focusedWindow == window) {
                    focusedWindow = nullptr;
                }
                if (capturedElement && capturedElement->GetWindow() == window) {
                    capturedElement = nullptr;
                }
                if (hoveredElement && hoveredElement->GetWindow() == window) {
                    hoveredElement = nullptr;
                }
                if (draggedElement && draggedElement->GetWindow() == window) {
                    draggedElement = nullptr;
                }

                windows.erase(it);
                std::cout << "UltraCanvas: Linux window destroyed successfully" << std::endl;
            }
            std::cout << "UltraCanvas: Window unregistered" << std::endl;
        }
    }

    UltraCanvasWindowBase* UltraCanvasBaseApplication::FindWindow(unsigned long nativeHandle) {
        auto it = std::find_if(windows.begin(), windows.end(),
                               [nativeHandle](const UltraCanvasWindowBase* ptr) {
                                   return ptr->GetNativeHandle() == nativeHandle;
                               });
        return (it != windows.end()) ? *it : nullptr;
    }

    UltraCanvasUIElement* UltraCanvasBaseApplication::GetFocusedElement() {
        if (focusedWindow) {
            return focusedWindow->GetFocusedElement();
        }
        return nullptr;
    }

    bool UltraCanvasBaseApplication::IsDoubleClick(const UCEvent &event) {
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

    void UltraCanvasBaseApplication::DispatchEvent(const UCEvent &event) {
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
        UltraCanvasWindowBase* targetWindow = nullptr;

        // First priority: Use the window information stored in the event
        if (event.targetWindow != nullptr) {
            targetWindow = static_cast<UltraCanvasWindowBase*>(event.targetWindow);
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

        // Handle different event types
        bool handled = false;
        switch (event.type) {
            case UCEventType::MouseMove:
            case UCEventType::MouseUp:
                if (capturedElement) {
                    auto newEvent = event;
                    newEvent.targetElement = capturedElement;
                    capturedElement->ConvertWindowToParentContainerCoordinates(newEvent.x, newEvent.y);
                    if (DispatchEventToElement(capturedElement, newEvent)) {
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
                    HandleFocusedWindowChange(targetWindow);
                    return;
                }
                break;
            case UCEventType::WindowBlur:
                if (targetWindow == focusedWindow) {
                    HandleFocusedWindowChange(nullptr);
                    return;
                }
                break;
        }
        // Dispatch other events to focused element
        if (targetWindow) {
            if ((event.IsMouseEvent() || event.IsKeyboardEvent()) && targetWindow->HasActivePopups()) {
                std::vector<UltraCanvasUIElement*> activePopupsCopy = targetWindow->GetActivePopups();
                if (event.IsMouseEvent()) {
                    for(auto it = activePopupsCopy.begin(); it != activePopupsCopy.end(); it++) {
                        UltraCanvasUIElement* activePopupElement = *it;
                        UCEvent localEvent = event;
                        localEvent.targetElement = activePopupElement;
//                    activePopupElement->ConvertWindowToParentContainerCoordinates(localEvent.x, localEvent.y);
                        if (DispatchEventToElement(activePopupElement, localEvent)) {
                            goto finish;
                        }
                    }
                } else if (event.IsKeyboardEvent()) { // only last (topmost) popup get keyboard events
                    UltraCanvasUIElement* activePopupElement = activePopupsCopy.back();
//                    activePopupElement->ConvertWindowToParentContainerCoordinates(localEvent.x, localEvent.y);
                    DispatchEventToElement(activePopupElement, event);
                    goto finish;
                }
            }

            if (event.IsKeyboardEvent()) {
                UltraCanvasUIElement* focused =  targetWindow->GetFocusedElement();
                if (focused) {
                    HandleEventWithBubbling(event, focused);
                    goto finish;
                }
            }

            if (event.type == UCEventType::MouseWheel) {
                auto elem = targetWindow->FindElementAtPoint(event.x, event.y);
                if (elem) {
                    HandleEventWithBubbling(event, elem);
                    goto finish;
                }

            }
            if (event.IsMouseEvent()) {
                auto elem = targetWindow->FindElementAtPoint(event.x, event.y);
                if (hoveredElement && hoveredElement != elem) {
                    UCEvent leaveEvent = event;
                    leaveEvent.type = UCEventType::MouseLeave;
                    leaveEvent.x = -1;
                    leaveEvent.y = -1;
                    leaveEvent.targetElement = hoveredElement;
                    DispatchEventToElement(hoveredElement, leaveEvent);
                    hoveredElement = nullptr;
                }
                if (elem) {
                    int localX = event.x;
                    int localY = event.y;
                    elem->ConvertWindowToParentContainerCoordinates(localX, localY);
                    if (hoveredElement != elem) {
                        UCEvent enterEvent = event;
                        enterEvent.targetElement = elem;
                        enterEvent.type = UCEventType::MouseEnter;
                        enterEvent.x = localX;
                        enterEvent.y = localY;
                        DispatchEventToElement(elem, enterEvent);
                        hoveredElement = elem;
                    }
                    auto newEvent = event;
                    newEvent.targetElement = elem;
                    newEvent.x = localX;
                    newEvent.y = localY;
                    if (DispatchEventToElement(elem, newEvent)) {
                        goto finish;
                    }
                }
            }

            if (event.isCommandEvent()) {
                HandleEventWithBubbling(event, event.targetElement);
                goto finish;
            }
            DispatchEventToElement(targetWindow, event);

        finish:
            targetWindow->CleanupRemovedPopupElements();
//            // Debug logging
//            if (event.type != UCEventType::MouseMove) {
//                std::cout << "UltraCanvas: Event type " << static_cast<int>(event.type)
//                          << " dispatched to window " << targetWindow
//                          << " (X11 Window: " << std::hex << event.nativeWindowHandle << std::dec << ")"
//                          << " focused=" << (targetWindow == focusedWindow ? "yes" : "no") << std::endl;
        } else {
            // No target window found - this might be normal for some system events
            std::cout << "UltraCanvas: Warning - Event type " << static_cast<int>(event.type)
                      << " has no target window (X11 Window: " << std::hex << event.nativeWindowHandle << std::dec << ")" << std::endl;
        }
    }

    bool UltraCanvasBaseApplication::HandleEventWithBubbling(const UCEvent &event, UltraCanvasUIElement* elem) {
        if (!event.isCommandEvent()) {
            auto newEvent = event;
            newEvent.targetElement = elem;
            if (event.IsMouseEvent()) {
                elem->ConvertWindowToParentContainerCoordinates(newEvent.x, newEvent.y);
            }
            if (DispatchEventToElement(elem, newEvent)) {
                return true;
            }
        }
        auto parent = elem->GetParentContainer();
        while(parent) {
            auto newParentEvent = event;
            newParentEvent.targetElement = elem;
            if (event.IsMouseEvent()) {
                parent->ConvertWindowToParentContainerCoordinates(newParentEvent.x, newParentEvent.y);
            }
            if (DispatchEventToElement(parent, newParentEvent)) {
                return true;
            }
            parent = parent->GetParentContainer();
        }
        return false;
    }


    bool UltraCanvasBaseApplication::HandleFocusedWindowChange(UltraCanvasWindowBase* window) {
        if (focusedWindow != window) {
            UltraCanvasWindowBase* previousFocusedWindow = focusedWindow;

            // Update focused window first
            focusedWindow = window;

            // Notify old window it lost focus
            if (previousFocusedWindow) {
                UCEvent blurEvent;
                blurEvent.type = UCEventType::WindowBlur;
                blurEvent.timestamp = std::chrono::steady_clock::now();
                blurEvent.targetWindow = static_cast<void*>(previousFocusedWindow);
                //blurEvent.nativeWindowHandle = previousFocusedWindow->GetXWindow();
                DispatchEventToElement(previousFocusedWindow, blurEvent);

                std::cout << "UltraCanvasBaseApplication: Window " << previousFocusedWindow << " lost focus" << std::endl;
            }

            // Notify new window it gained focus
            if (focusedWindow) {
                UCEvent focusEvent;
                focusEvent.type = UCEventType::WindowFocus;
                focusEvent.timestamp = std::chrono::steady_clock::now();
                focusEvent.targetWindow = static_cast<void*>(focusedWindow);
                //focusEvent.nativeWindowHandle = focusedWindow->GetXWindow();
                DispatchEventToElement(focusedWindow, focusEvent);

                std::cout << "UltraCanvasBaseApplication: Window " << focusedWindow << " gained focus" << std::endl;
            }
            return true;
        }
        return false;
    }

    void UltraCanvasBaseApplication::RegisterGlobalEventHandler(std::function<bool(const UCEvent &)> handler) {
        globalEventHandlers.push_back(handler);
    }

    void UltraCanvasBaseApplication::FocusNextElement() {
        if (focusedWindow) {
            focusedWindow->FocusNextElement();
        }
    }

    void UltraCanvasBaseApplication::FocusPreviousElement() {
        if (focusedWindow) {
            focusedWindow->FocusPreviousElement();
        }
    }

    void UltraCanvasBaseApplication::RegisterEventLoopRunCallback(std::function<void()> callback) {
        eventLoopCallback = callback;
    }

    bool UltraCanvasBaseApplication::DispatchEventToElement(UltraCanvasUIElement *elem, const UCEvent &event) {
        currentEvent = event;
        return elem->OnEvent(event);
    }

    void UltraCanvasBaseApplication::RunInEventLoop() {
        auto clipbrd = GetClipboard();
        if (clipbrd) {
            clipbrd->Update();
        }
        if (eventLoopCallback) {
            eventLoopCallback();
        }
        UltraCanvasTooltipManager::Update();
    }
}
