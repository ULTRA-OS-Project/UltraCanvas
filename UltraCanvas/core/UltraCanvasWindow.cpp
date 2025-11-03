// UltraCanvasWindowBase.cpp
// Fixed implementation of cross-platform window management system
// Version: 1.2.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework

#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasRenderContext.h"
#include "../include/UltraCanvasApplication.h"
#include "../include/UltraCanvasTooltipManager.h"
//#include "../include/UltraCanvasZOrderManager.h"
#include "../include/UltraCanvasMenu.h"
#include "UltraCanvasApplication.h"

#include <iostream>
#include <algorithm>

namespace UltraCanvas {
    UltraCanvasWindowBase::UltraCanvasWindowBase()
            : UltraCanvasContainer("Window", 0, 0, 0, 0, 0) {
        // Configure container for window behavior
        SetWindow(this);
    }

// ===== FOCUS MANAGEMENT IMPLEMENTATION =====

    void UltraCanvasWindowBase::SetFocusedElement(UltraCanvasUIElement* element) {
        // Don't do anything if already focused
        if (_focusedElement == element) {
            return;
        }

        // Validate element belongs to this window
        if (element && element->GetWindow() != this) {
            std::cerr << "Warning: Trying to focus element from different window" << std::endl;
            return;
        }

        // Remove focus from current element
        if (_focusedElement) {
            SendFocusLostEvent(_focusedElement);
        }

        // Set new focused element
        _focusedElement = element;

        // Set focus on new element
        if (_focusedElement) {
            SendFocusGainedEvent(_focusedElement);
        }

        // Request window redraw to update focus indicators
        _needsRedraw = true;

        std::cout << "Focus changed to: " << (element ? element->GetIdentifier() : "none") << std::endl;
    }

    void UltraCanvasWindowBase::ClearFocus() {
        SetFocusedElement(nullptr);
    }

    bool UltraCanvasWindowBase::RequestElementFocus(UltraCanvasUIElement* element) {
        // Validate element can receive focus
        if (!element || !element->CanReceiveFocus()) {
            return false;
        }

        // Set focus through window's focus management
        SetFocusedElement(element);
        return true;
    }

    void UltraCanvasWindowBase::FocusNextElement() {
        std::vector<UltraCanvasUIElement*> focusableElements = GetFocusableElements();

        if (focusableElements.empty()) {
            return;
        }

        // Find current focus index
        int currentIndex = -1;
        for (size_t i = 0; i < focusableElements.size(); ++i) {
            if (focusableElements[i] == _focusedElement) {
                currentIndex = static_cast<int>(i);
                break;
            }
        }

        // Calculate next index
        int nextIndex = (currentIndex + 1) % static_cast<int>(focusableElements.size());

        // Set focus to next element
        SetFocusedElement(focusableElements[nextIndex]);
    }

    void UltraCanvasWindowBase::FocusPreviousElement() {
        std::vector<UltraCanvasUIElement*> focusableElements = GetFocusableElements();

        if (focusableElements.empty()) {
            return;
        }

        // Find current focus index
        int currentIndex = -1;
        for (size_t i = 0; i < focusableElements.size(); ++i) {
            if (focusableElements[i] == _focusedElement) {
                currentIndex = static_cast<int>(i);
                break;
            }
        }

        // Calculate previous index
        int prevIndex = (currentIndex <= 0) ?
                        static_cast<int>(focusableElements.size()) - 1 :
                        currentIndex - 1;

        // Set focus to previous element
        SetFocusedElement(focusableElements[prevIndex]);
    }

    std::vector<UltraCanvasUIElement*> UltraCanvasWindowBase::GetFocusableElements() {
        std::vector<UltraCanvasUIElement*> focusableElements;

        // Start collecting from the window container itself
        CollectFocusableElements(this, focusableElements);

        return focusableElements;
    }

    void UltraCanvasWindowBase::CollectFocusableElements(UltraCanvasContainer* container,
                                                         std::vector<UltraCanvasUIElement*>& elements) {
        if (!container) return;

        // Get all child elements from the container
        const auto& children = container->GetChildren();

        for (const auto& child : children) {
            UltraCanvasUIElement* element = child.get();

            // Check if element can be focused
            if (element && element->CanReceiveFocus()) {
                elements.push_back(element);
            }

            // If the element is also a container, recursively collect from it
            if (auto childContainer = dynamic_cast<UltraCanvasContainer*>(element)) {
                CollectFocusableElements(childContainer, elements);
            }
        }
    }

    void UltraCanvasWindowBase::SendFocusGainedEvent(UltraCanvasUIElement* element) {
        if (!element) return;

        UCEvent focusEvent;
        focusEvent.type = UCEventType::FocusGained;
        focusEvent.nativeWindowHandle = GetNativeHandle();
        element->OnEvent(focusEvent);
    }

    void UltraCanvasWindowBase::SendFocusLostEvent(UltraCanvasUIElement* element) {
        if (!element) return;

        UCEvent focusEvent;
        focusEvent.type = UCEventType::FocusLost;
        focusEvent.nativeWindowHandle = GetNativeHandle();
        element->OnEvent(focusEvent);
    }

    bool UltraCanvasWindowBase::OnEvent(const UCEvent &event) {
        // Handle window-specific events first
        if (HandleWindowEvent(event)) {
            return true;
        }

        // Handle focus-related keyboard events
        if (event.IsKeyboardEvent()) {
            if (event.type == UCEventType::KeyDown && event.virtualKey == UCKeys::Tab && !event.ctrl && !event.alt && !event.meta) {
                if (event.shift) {
                    FocusPreviousElement();
                } else {
                    FocusNextElement();
                }
                return true;
            }
        }

//        if (event.IsMouseEvent() && !activePopups.empty()) {
//            std::unordered_set<UltraCanvasUIElement*> activePopupsCopy = activePopups;
//            for(auto it = activePopupsCopy.begin(); it != activePopupsCopy.end(); it++) {
//                UltraCanvasUIElement* activePopupElement = *it;
//                auto localCoords = activePopupElement->ConvertWindowToParentContainerCoordinates(Point2Di(event.x, event.y));
//                UCEvent localEvent = event;
//                localEvent.x = localCoords.x;
//                localEvent.y = localCoords.y;
//                if (activePopupElement->OnEvent(localEvent)) {
//                    return true;
//                }
//            }
//        }

        return UltraCanvasContainer::OnEvent(event);
    }

    bool UltraCanvasWindowBase::HandleWindowEvent(const UCEvent &event) {
        switch (event.type) {
            case UCEventType::WindowClose:
                HandleCloseEvent();
                return true;

            case UCEventType::WindowResize:
                HandleResizeEvent(event.width, event.height);
                return true;

            case UCEventType::WindowMove:
                HandleMoveEvent(event.x, event.y);
                return true;

            case UCEventType::WindowFocus:
                HandleFocusEvent(true);
                return true;

            case UCEventType::WindowBlur:
                HandleFocusEvent(false);
                return true;

            default:
                return false;
        }
    }

    void UltraCanvasWindowBase::HandleCloseEvent() {
        Close();
//        _state = WindowState::Closing;
//        if (onWindowClose) onWindowClose();
    }

    void UltraCanvasWindowBase::HandleResizeEvent(int width, int height) {
        config_.width = width;
        config_.height = height;
        UltraCanvasContainer::SetSize(width, height);
        MarkLayoutDirty();
        if (onWindowResize) onWindowResize(width, height);
    }

    void UltraCanvasWindowBase::HandleMoveEvent(int x, int y) {
        config_.x = x;
        config_.y = y;
        // position must be always 0,0
        //UltraCanvasContainer::SetPosition(x, y);
        if (onWindowMove) onWindowMove(x, y);
    }

    void UltraCanvasWindowBase::HandleFocusEvent(bool focused) {
        if (focused) {
            if (!_focused) {
                _focused = true;
                if (onWindowFocus) onWindowFocus();
            }
        } else {
            if (_focused) {
                _focused = false;
                if (onWindowBlur) onWindowBlur();
            }
        }
        _needsRedraw = true;
    }

    void UltraCanvasWindowBase::Render() {
        if (!_visible || !_created) return;
//        if (useSelectiveRendering && selectiveRenderer) {
//            // Use simple selective rendering
//            selectiveRenderer->RenderFrame();
//            // Only clear needs redraw if no dirty regions remain
//            if (!selectiveRenderer->HasDirtyRegions()) {
//                _needsRedraw = false;
//            }
//        } else {
            // Clear the window background
            RenderWindowBackground();

            // Render container content (children with scrolling)
            UltraCanvasContainer::Render();

            RenderActivePopups();

            // Render window-specific overlays
            RenderWindowChrome();

//            useSelectiveRendering = true;
//        }
    }

    void UltraCanvasWindowBase::RenderActivePopups() {
        // Render popups in z-order
        for (UltraCanvasUIElement* popup : activePopups) {
            if (popup) {
                auto ctx = GetRenderContext();
                ctx->PushState();
                popup->RenderPopupContent();
                ctx->PopState();
            }
        }

        UltraCanvasTooltipManager::Render(this);
    }



    void UltraCanvasWindowBase::AddPopupElement(UltraCanvasUIElement *element) {
        if (element) {
            MarkElementDirty(element);
            auto found = std::find(activePopups.begin(), activePopups.end(), element);
            if (found == activePopups.end()) {
                activePopups.emplace_back(element);
            }
            popupsToRemove.erase(element);
        }
    }

    void UltraCanvasWindowBase::RemovePopupElement(UltraCanvasUIElement *element) {
        auto found = std::find(activePopups.begin(), activePopups.end(), element);
        if (found != activePopups.end()) {
            popupsToRemove.insert(element);
        }
    }

    void UltraCanvasWindowBase::CleanupRemovedPopupElements() {
        if (popupsToRemove.empty()) return;

        for(auto it : popupsToRemove) {
            auto found = std::find(activePopups.begin(), activePopups.end(), it);
            if (found != activePopups.end()) {
                activePopups.erase(found);
            }
        }

        popupsToRemove.clear();
//        if (selectiveRenderer) {
//            selectiveRenderer->RestoreBackgroundFromOverlay();
//        } else {
//            _needsRedraw = true;
//        }
        _needsRedraw = true;
    }

    void UltraCanvasWindowBase::MarkElementDirty(UltraCanvasUIElement* element, bool isOverlay) {
//        if (selectiveRenderer) {
////            if (isOverlay) {
////                selectiveRenderer->SaveBackgroundForOverlay(element);
////            }
//            selectiveRenderer->MarkRegionDirty(element->GetActualBoundsInWindow(), isOverlay);
//        }
        _needsRedraw = true;
    }

//    bool UltraCanvasWindowBase::IsSelectiveRenderingActive() {
//        return selectiveRenderer && selectiveRenderer->IsRenderingActive();
//    }

    bool UltraCanvasWindowBase::Create(const WindowConfig& config) {
        config_ = config;
        _state = WindowState::Normal;

        ContainerStyle containerStyle;
        containerStyle.enableVerticalScrolling = config.enableWindowScrolling;
        containerStyle.enableHorizontalScrolling = config.enableWindowScrolling;
        containerStyle.backgroundColor = config.backgroundColor;
        containerStyle.borderWidth = 0.0f; // Windows don't need container borders
        containerStyle.paddingLeft = 0;
        containerStyle.paddingRight = 0;
        containerStyle.paddingTop = 0;
        containerStyle.paddingBottom = 0;
        SetContainerStyle(containerStyle);

        auto application = UltraCanvasApplication::GetInstance();
        SetBounds(Rect2Di(0, 0, config_.width, config_.height));
        if (CreateNative(config)) {
            application->RegisterWindow(this);
            _created = true;
            return true;
        }
        return false;
    }

    void UltraCanvasWindowBase::Destroy() {
        if (!_created) {
            return;
        }
        UltraCanvasApplication::GetInstance()->UnregisterWindow(this);
        _created = false;
    }

//    void UpdateZOrderSort() {
//        sortedElements = elements;
//        UltraCanvasZOrderManager::SortElementsByZOrder(sortedElements);
//        zOrderDirty = false;
//
//        std::cout << "Window z-order updated with " << sortedElements.size() << " elements:" << std::endl;
//        for (size_t i = 0; i < sortedElements.size(); i++) {
//            auto* element = sortedElements[i];
//            if (element) {
//                std::cout << "  [" << i << "] Z=" << element->GetZIndex()
//                          << " " << element->GetIdentifier() << std::endl;
//            }
//        }
//    }
} // namespace UltraCanvas