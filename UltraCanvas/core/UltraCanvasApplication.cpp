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
#include "UltraCanvasBaseApplication.h"


namespace UltraCanvas {
    UltraCanvasApplication* UltraCanvasApplication::instance = nullptr;

    // ===== WINDOW MANAGEMENT =====
    void UltraCanvasBaseApplication::RegisterWindow(UltraCanvasWindow *window) {
        if (!initialized) {
            std::cerr << "UltraCanvas: Cannot register window - application not initialized" << std::endl;
        }
        if (window && window->GetNativeHandle() != 0) {
            std::cout << "UltraCanvas: Linux window created successfully" << std::endl;
            windows.push_back(window);
            std::cout << "UltraCanvas: Window registered with Native ID: " << window->GetNativeHandle() << std::endl;
        }
    }

    void UltraCanvasBaseApplication::UnregisterWindow(UltraCanvasWindow *window) {
        if (window) {
            auto it = std::find_if(windows.begin(), windows.end(),
                                   [window](const UltraCanvasWindow* ptr) {
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

    UltraCanvasWindow* UltraCanvasBaseApplication::FindWindow(unsigned long nativeHandle) {
        auto it = std::find_if(windows.begin(), windows.end(),
                               [nativeHandle](const UltraCanvasWindow* ptr) {
                                   return ptr->GetNativeHandle() == nativeHandle;
                               });
        return (it != windows.end()) ? *it : nullptr;
    }

    UltraCanvasElement* UltraCanvasBaseApplication::GetFocusedElement() {
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

    bool UltraCanvasBaseApplication::DispatchEvent(const UCEvent &event) {
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
                return true; // Event consumed by global handler
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

        // Handle different event types
        bool handled = false;
        switch (event.type) {
            case UCEventType::MouseUp:
                if (capturedElement && capturedElement->OnEvent(event)) {
                    return true;
                }
                break;

            case UCEventType::MouseMove:
                // Send move event to captured element or hovered element
                if (capturedElement && capturedElement->OnEvent(event)) {
                    return true;
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
                    return HandleWindowFocus(targetWindow);
                }
                break;
            case UCEventType::WindowBlur:
                if (targetWindow == focusedWindow) {
                    return HandleWindowFocus(nullptr);
                }
                break;
        }
        // Dispatch other events to focused element
        if (targetWindow) {
            if ((event.IsMouseEvent() || event.IsKeyboardEvent()) && !targetWindow->GetActivePopups().empty()) {
                std::unordered_set<UltraCanvasElement*> activePopupsCopy = targetWindow->GetActivePopups();
                for(auto it = activePopupsCopy.begin(); it != activePopupsCopy.end(); it++) {
                    UltraCanvasElement* activePopupElement = *it;
                    UCEvent localEvent = event;
                    activePopupElement->ConvertWindowToContainerCoordinates(localEvent.x, localEvent.y);
                    if (activePopupElement->OnEvent(localEvent)) {
                        return true;
                    }
                }
            }

            if (event.IsKeyboardEvent()) {
                auto focused =  targetWindow->GetFocusedElement();
                if (focused) {
                    return HandleEventWithBubbling(event, focused);
                }
            }

            if (event.type == UCEventType::MouseWheel) {
                auto elem = targetWindow->FindElementAtPoint(event.x, event.y);
                if (elem) {
                    return HandleEventWithBubbling(event, elem);
                }

            }
//            if (event.IsMouseClickEvent()) {
//                auto elem = targetWindow->FindElementAtPoint(event.x, event.y);
//                if (elem) {
//                    auto newEvent = event;
//                    std::cout << "Ev x=" << event.x << " y=" << event.y;
//                    elem->ConvertWindowToContainerCoordinates(newEvent.x, newEvent.y);
//                    std::cout << " Evloc x=" << newEvent.x << " y=" << newEvent.y << std::endl;
//                    if (elem->OnEvent(newEvent)) {
//                        return true;
//                    }
//                }
//            }
            if (event.IsMouseEvent()) {
                auto elem = targetWindow->FindElementAtPoint(event.x, event.y);
                if (elem) {
                    auto newEvent = event;
                    elem->ConvertWindowToContainerCoordinates(newEvent.x, newEvent.y);
                    if (elem->OnEvent(newEvent)) {
                        return true;
                    }
                }
            }
            return targetWindow->OnEvent(event);

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

        return false;
    }

    bool UltraCanvasBaseApplication::HandleEventWithBubbling(const UCEvent &event, UltraCanvasElement* elem) {
        auto newEvent = event;
        elem->ConvertWindowToContainerCoordinates(newEvent.x, newEvent.y);
        if (elem->OnEvent(newEvent)) {
            return true;
        }
        auto parent = elem->GetParentContainer();
        while(parent) {
            auto newParentEvent = event;
            parent->ConvertWindowToContainerCoordinates(newParentEvent.x, newParentEvent.y);
            if (parent->OnEvent(newParentEvent)) {
                return true;
            }
            parent = parent->GetParentContainer();
        }
        return false;
    }


    bool UltraCanvasBaseApplication::HandleWindowFocus(UltraCanvasWindow* window) {
        if (focusedWindow != window) {
            UltraCanvasWindow* previousFocusedWindow = focusedWindow;

            // Update focused window first
            focusedWindow = window;

            // Notify old window it lost focus
            if (previousFocusedWindow) {
                UCEvent blurEvent;
                blurEvent.type = UCEventType::WindowBlur;
                blurEvent.timestamp = std::chrono::steady_clock::now();
                blurEvent.targetWindow = static_cast<void*>(previousFocusedWindow);
                blurEvent.nativeWindowHandle = previousFocusedWindow->GetXWindow();
                previousFocusedWindow->OnEvent(blurEvent);

                std::cout << "UltraCanvasBaseApplication: Window " << previousFocusedWindow << " lost focus" << std::endl;
            }

            // Notify new window it gained focus
            if (focusedWindow) {
                UCEvent focusEvent;
                focusEvent.type = UCEventType::WindowFocus;
                focusEvent.timestamp = std::chrono::steady_clock::now();
                focusEvent.targetWindow = static_cast<void*>(focusedWindow);
                focusEvent.nativeWindowHandle = focusedWindow->GetXWindow();
                focusedWindow->OnEvent(focusEvent);

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

    void UltraCanvasApplication::RunInEventLoop() {
        auto clipbrd = GetClipboard();
        if (clipbrd) {
            clipbrd->Update();
        }
    }
}
