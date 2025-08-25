// UltraCanvasBaseWindow.cpp
// Fixed implementation of cross-platform window management system
// Version: 1.2.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework

#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasEventDispatcher.h"
#include "../include/UltraCanvasRenderInterface.h"
#include "../include/UltraCanvasApplication.h"
//#include "../include/UltraCanvasZOrderManager.h"
#include "../include/UltraCanvasMenu.h"

#include <iostream>
#include <algorithm>

namespace UltraCanvas {
    UltraCanvasBaseWindow::UltraCanvasBaseWindow(const WindowConfig &config)
            : UltraCanvasContainer("Window", 0, 0, 0, config.width, config.height),
              config_(config) {
        // Configure container for window behavior
        ContainerStyle containerStyle = GetContainerStyle();
        containerStyle.enableVerticalScrolling = config.enableWindowScrolling;
        containerStyle.enableHorizontalScrolling = config.enableWindowScrolling;
        containerStyle.backgroundColor = config.backgroundColor;
        containerStyle.borderWidth = 0.0f; // Windows don't need container borders
        SetContainerStyle(containerStyle);
    }

    bool UltraCanvasBaseWindow::OnEvent(const UCEvent &event) {
        // Handle window-specific events first
        if (HandleWindowEvent(event)) {
            return true;
        }

        if (event.IsMouseClickEvent() && !activePopups.empty()) {
            std::unordered_set<UltraCanvasElement*> activePopupsCopy = activePopups;
            for(auto it = activePopupsCopy.begin(); it != activePopupsCopy.end(); it++) {
                UltraCanvasElement* activePopupElement = *it;
                auto localCoords = activePopupElement->ConvertWindowToLocalCoordinates(Point2D(event.x, event.y));
                UCEvent localEvent = event;
                localEvent.x = localCoords.x;
                localEvent.y = localCoords.y;
                if (activePopupElement->OnEvent(localEvent)) {
                    return true;
                }
            }
        }
        // Forward to container for child handling
        return UltraCanvasContainer::OnEvent(event);
    }

    bool UltraCanvasBaseWindow::HandleWindowEvent(const UCEvent &event) {
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
    void UltraCanvasBaseWindow::HandleCloseEvent() {
        _state = WindowState::Closing;
        if (onWindowClose) onWindowClose();
    }

    void UltraCanvasBaseWindow::HandleResizeEvent(int width, int height) {
        config_.width = width;
        config_.height = height;
        UltraCanvasContainer::SetSize(width, height);
        MarkLayoutDirty();
        if (onWindowResize) onWindowResize(width, height);
    }

    void UltraCanvasBaseWindow::HandleMoveEvent(int x, int y) {
        config_.x = x;
        config_.y = y;
        // position must be always 0,0
        //UltraCanvasContainer::SetPosition(x, y);
        if (onWindowMove) onWindowMove(x, y);
    }

    void UltraCanvasBaseWindow::HandleFocusEvent(bool focused) {
        if (focused) {
            if (onWindowFocus) onWindowFocus();
        } else {
            if (onWindowBlur) onWindowBlur();
        }
    }

    void UltraCanvasBaseWindow::Render() {
        if (!_visible || !_created) return;

        // Clear the window background
        RenderWindowBackground();

        // Render container content (children with scrolling)
        UltraCanvasContainer::Render();

        RenderActivePopups();

        // Render window-specific overlays
        RenderWindowChrome();
    }

    void UltraCanvasBaseWindow::RenderActivePopups() {
        // Render popups in z-order
        for (UltraCanvasElement* popup : activePopups) {
            if (popup) {
                PushRenderState();
                popup->RenderPopupContent();
                PopRenderState();
            }
        }
    }

    bool UltraCanvasWindow::Create(const WindowConfig& config) {
        config_ = config;
        _state = WindowState::Normal;
        auto application = UltraCanvasApplication::GetInstance();
        SetBounds(0, 0, config_.width, config_.height);
        if (UltraCanvasNativeWindow::CreateNative(config)) {
            application->RegisterWindow(this);
            return true;
        }
        return false;
    }

    void UltraCanvasWindow::Destroy() {
        if (!_created) {
            return;
        }
        auto application = UltraCanvasApplication::GetInstance();
        if (application) {
            application->UnregisterWindow(this);
        }
        UltraCanvasNativeWindow::Destroy();
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