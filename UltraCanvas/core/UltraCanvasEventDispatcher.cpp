// UltraCanvasEventDispatcher.cpp
// Cross-platform event dispatching system implementation
// Version: 2.1.0
// Last Modified: 2025-07-08
// Author: UltraCanvas Framework

#include "../include/UltraCanvasEventDispatcher.h"
#include <algorithm>
#include <iostream>
#include <cmath>

namespace UltraCanvas {
// ===== STATIC MEMBER DEFINITIONS =====
    UltraCanvasElement* UltraCanvasEventDispatcher::focusedElement = nullptr;
    UltraCanvasElement* UltraCanvasEventDispatcher::hoveredElement = nullptr;
    UltraCanvasElement* UltraCanvasEventDispatcher::capturedElement = nullptr;
    UltraCanvasElement* UltraCanvasEventDispatcher::draggedElement = nullptr;

    UCEvent UltraCanvasEventDispatcher::lastMouseEvent;
    std::chrono::steady_clock::time_point UltraCanvasEventDispatcher::lastClickTime;
    const float UltraCanvasEventDispatcher::DOUBLE_CLICK_TIME = 0.5f;
    const int UltraCanvasEventDispatcher::DOUBLE_CLICK_DISTANCE = 5;

    bool UltraCanvasEventDispatcher::keyStates[256] = {false};
    bool UltraCanvasEventDispatcher::shiftHeld = false;
    bool UltraCanvasEventDispatcher::ctrlHeld = false;
    bool UltraCanvasEventDispatcher::altHeld = false;
    bool UltraCanvasEventDispatcher::metaHeld = false;

    std::vector<std::function<bool(const UCEvent&)>> UltraCanvasEventDispatcher::globalEventHandlers;

    bool UltraCanvas::UltraCanvasEventDispatcher::DispatchEvent(const UCEvent &event,
                                                                std::vector<UltraCanvasElement *> &elements) {
        // Update modifier states
        UpdateModifierStates(event);

        // Call global handlers first
        for (auto& handler : globalEventHandlers) {
            if (handler(event)) {
                return true; // Event consumed by global handler
            }
        }

        // Handle different event types
        bool handled = false;
        switch (event.type) {
            case UCEventType::MouseDown:
                handled = HandleMouseDown(event, elements);
                break;

            case UCEventType::MouseUp:
                handled = HandleMouseUp(event, elements);
                break;

            case UCEventType::MouseMove:
                handled = HandleMouseMove(event, elements);
                break;

            case UCEventType::MouseDoubleClick:
                handled = HandleMouseDoubleClick(event, elements);
                break;

            case UCEventType::MouseWheel:
                handled = HandleMouseWheel(event, elements);
                break;

            case UCEventType::KeyDown:
            case UCEventType::KeyUp:
                handled = HandleKeyboardEvent(event, elements);
                break;

            default:
                // Dispatch other events to focused element
                if (focusedElement) {
                    handled = focusedElement->OnEvent(event);
                }
                break;
        }

        return handled;
    }

    void UltraCanvas::UltraCanvasEventDispatcher::SetFocusedElement(UltraCanvasElement *element) {
        if (focusedElement == element) return;

        // Remove focus from current element
        if (focusedElement) {
            focusedElement->SetFocus(false);
            UCEvent focusLostEvent;
            focusLostEvent.type = UCEventType::FocusLost;
            focusedElement->OnEvent(focusLostEvent);
        }

        // Set new focused element
        focusedElement = element;

        if (focusedElement) {
            focusedElement->SetFocus(true);
            UCEvent focusGainedEvent;
            focusGainedEvent.type = UCEventType::FocusGained;
            focusedElement->OnEvent(focusGainedEvent);
        }
    }

    void
    UltraCanvas::UltraCanvasEventDispatcher::FocusNextElement(std::vector<UltraCanvasElement *> &elements, bool reverse) {
        if (elements.empty()) return;

        // Find focusable elements
        std::vector<UltraCanvasElement*> focusableElements;
        for (UltraCanvasElement* el : elements) {
            if (el && el->IsVisible() && el->IsEnabled()) {
                focusableElements.push_back(el);
            }
        }

        if (focusableElements.empty()) return;

        // Find current index
        int currentIndex = -1;
        for (size_t i = 0; i < focusableElements.size(); ++i) {
            if (focusableElements[i] == focusedElement) {
                currentIndex = static_cast<int>(i);
                break;
            }
        }

        // Calculate next index
        int nextIndex;
        if (reverse) {
            nextIndex = (currentIndex <= 0) ?
                        static_cast<int>(focusableElements.size()) - 1 :
                        currentIndex - 1;
        } else {
            nextIndex = (currentIndex + 1) % static_cast<int>(focusableElements.size());
        }

        // Set focus to next element
        SetFocusedElement(focusableElements[nextIndex]);
    }

    void
    UltraCanvas::UltraCanvasEventDispatcher::RegisterGlobalEventHandler(std::function<bool(const UCEvent &)> handler) {
        globalEventHandlers.push_back(handler);
    }

    bool UltraCanvasEventDispatcher::IsKeyPressed(int keyCode) {
        if (keyCode >= 0 && keyCode < 256) {
            return keyStates[keyCode];
        }
        return false;
    }

    void UltraCanvasEventDispatcher::Reset() {
        focusedElement = nullptr;
        hoveredElement = nullptr;
        capturedElement = nullptr;
        draggedElement = nullptr;

        // Clear keyboard state
        for (int i = 0; i < 256; ++i) {
            keyStates[i] = false;
        }
        shiftHeld = ctrlHeld = altHeld = metaHeld = false;

        // Clear global handlers
        globalEventHandlers.clear();
    }

    bool UltraCanvasEventDispatcher::HandleMouseDown(const UCEvent &event, std::vector<UltraCanvasElement *> &elements) {
        // Check for double-click
        UCEvent processedEvent = event;
        if (IsDoubleClick(event)) {
            processedEvent.type = UCEventType::MouseDoubleClick;
        }

        // Find clicked element (top-most first)
        UltraCanvasElement* clickedElement = FindElementAtPoint(event.x, event.y, elements);

        // Update focus
        SetFocusedElement(clickedElement);

        // Capture mouse for dragging
        if (clickedElement) {
            capturedElement = clickedElement;
            clickedElement->OnEvent(processedEvent);
            return true;
        }

        return false;
    }

    bool UltraCanvasEventDispatcher::HandleMouseUp(const UCEvent &event, std::vector<UltraCanvasElement *> &elements) {
        bool handled = false;

        // Send to captured element first
        if (capturedElement) {
            capturedElement->OnEvent(event);
            capturedElement = nullptr;
            handled = true;
        }

        // Also send to element under mouse if different
        UltraCanvasElement* elementUnderMouse = FindElementAtPoint(event.x, event.y, elements);
        if (elementUnderMouse && elementUnderMouse != capturedElement) {
            elementUnderMouse->OnEvent(event);
            handled = true;
        }

        return handled;
    }

    bool UltraCanvasEventDispatcher::HandleMouseMove(const UCEvent &event, std::vector<UltraCanvasElement *> &elements) {
        // Update hovered element
        UltraCanvasElement* newHovered = FindElementAtPoint(event.x, event.y, elements);

        if (hoveredElement != newHovered) {
            // Send mouse leave to old element
            if (hoveredElement) {
                hoveredElement->SetHovered(false);
                UCEvent leaveEvent = event;
                leaveEvent.type = UCEventType::MouseLeave;
                hoveredElement->OnEvent(leaveEvent);
            }

            // Send mouse enter to new element
            hoveredElement = newHovered;
            if (hoveredElement) {
                hoveredElement->SetHovered(true);
                UCEvent enterEvent = event;
                enterEvent.type = UCEventType::MouseEnter;
                hoveredElement->OnEvent(enterEvent);
            }
        }

        // Send move event to captured element or hovered element
        UltraCanvasElement* targetElement = capturedElement ? capturedElement : hoveredElement;
        if (targetElement) {
            targetElement->OnEvent(event);
            return true;
        }

        return false;
    }

    bool UltraCanvasEventDispatcher::HandleMouseDoubleClick(const UCEvent &event,
                                                            std::vector<UltraCanvasElement *> &elements) {
        UltraCanvasElement* clickedElement = FindElementAtPoint(event.x, event.y, elements);
        if (clickedElement) {
            clickedElement->OnEvent(event);
            return true;
        }
        return false;
    }

    bool UltraCanvasEventDispatcher::HandleMouseWheel(const UCEvent &event, std::vector<UltraCanvasElement *> &elements) {
        UltraCanvasElement* elementUnderMouse = FindElementAtPoint(event.x, event.y, elements);
        if (elementUnderMouse) {
            elementUnderMouse->OnEvent(event);
            return true;
        }
        return false;
    }

    bool
    UltraCanvasEventDispatcher::HandleKeyboardEvent(const UCEvent &event, std::vector<UltraCanvasElement *> &elements) {
        // Update key state
        if (event.keyCode >= 0 && event.keyCode < 256) {
            keyStates[event.keyCode] = (event.type == UCEventType::KeyDown);
        }

        // Handle navigation keys
        if (event.type == UCEventType::KeyDown) {
            switch (event.virtualKey) {
                case UCKeys::Tab: // Tab
                    FocusNextElement(elements, event.shift);
                    return true;

                case UCKeys::Left:
                case UCKeys::Up:
                    FocusNextElement(elements, true);
                    return true;

                case UCKeys::Right:
                case UCKeys::Down:
                    FocusNextElement(elements, false);
                    return true;

                case UCKeys::Escape:
                    SetFocusedElement(nullptr);
                    return true;
            }
        }

        // Send to focused element
        if (focusedElement) {
            focusedElement->OnEvent(event);
            return true;
        }

        return false;
    }

    UltraCanvasElement *
    UltraCanvasEventDispatcher::FindElementAtPoint(int x, int y, std::vector<UltraCanvasElement *> &elements) {
        // Search from back to front (highest z-index first)
        for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
            UltraCanvasElement* element = *it;
            if (element && element->IsVisible() && element->IsEnabled() && element->Contains(x, y)) {
                return element;
            }
        }
        return nullptr;
    }

    bool UltraCanvasEventDispatcher::IsDoubleClick(const UCEvent &event) {
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

    void UltraCanvasEventDispatcher::UpdateModifierStates(const UCEvent &event) {
        shiftHeld = event.shift;
        ctrlHeld = event.ctrl;
        altHeld = event.alt;
        metaHeld = event.meta;
    }
} // namespace UltraCanvas