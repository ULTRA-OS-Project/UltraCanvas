// UltraCanvasBaseWindow.cpp
// Fixed implementation of cross-platform window management system
// Version: 1.2.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework

#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasEventDispatcher.h"
#include "../include/UltraCanvasRenderInterface.h"
#include "../include/UltraCanvasApplication.h"
#include "../include/UltraCanvasZOrderManager.h"
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

        if (event.IsMouseClickEvent() && activePopupElement) {
            auto localCoords = activePopupElement->ConvertWindowToLocalCoordinates(Point2D(event.x, event.y));
            if (!activePopupElement->Contains(localCoords)) {
                activePopupElement->OnEvent(event);
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
        state_ = WindowState::Closing;
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
        if (!visible_ || !created_) return;

        // Clear the window background
        RenderWindowBackground();

        // Render container content (children with scrolling)
        UltraCanvasContainer::Render();

        // Render window-specific overlays
        RenderWindowChrome();
    }

    // Fixed OnEvent method with proper event dispatching
//    bool UltraCanvasBaseWindow::OnEvent(const UCEvent &event) {
//        bool eventHandled = false;
//
//        if (event.type != UCEventType::MouseMove) {
//            std::cout << "Window event: " << static_cast<int>(event.type)
//                      << " at (" << event.x << "," << event.y << ")" << std::endl;
//        }
//
//        // Use the enhanced element event forwarding system
//        eventHandled = DispatchEventToElements(event);
//
//        return eventHandled;
//    }

//    bool UltraCanvasBaseWindow::DispatchEventToElements(const UCEvent& event) {
//        Point2D mousePos(event.x, event.y);
//        bool handled = false;
//
//        // For mouse events, use z-order hit testing
//        if (event.IsMouseEvent()) {
//            // Check elements in reverse order (topmost first)
//            for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
//                UltraCanvasElement* element = *it;
//
//                if (!element || !element->IsVisible() || !element->IsEnabled()) {
//                    continue;
//                }
//
//                // Check various conditions for event handling
//                if (element->Contains(mousePos)) {
//                    // Let the enhanced element handle the event and forward to children
//                    if (element->OnEvent(event)) {
//                        handled = true;
//                        break; // Event consumed
//                    }
//                }
//            }
//            if (event.IsMouseClickEvent()) {
//                for (auto it = outsideClickHandlers.rbegin(); it != outsideClickHandlers.rend(); ++it) {
//                    UltraCanvasElement* element = *it;
//                    element->OnEvent(event);
//                }
//            }
//        } else {
//            // For non-mouse events, forward to all elements
//            // The enhanced OnEvent will handle child forwarding appropriately
//            for (auto* element : elements) {
//                if (element && element->IsVisible() && element->IsEnabled()) {
//                    if (element->OnEvent(event)) {
//                        handled = true;
//                        // Continue - some events may need to reach multiple elements
//                    }
//                }
//            }
//        }
//
//        return handled;
//    }
//
    // UltraCanvasWindow

    bool UltraCanvasWindow::Create(const WindowConfig& config) {
        config_ = config;
        state_ = WindowState::Normal;
        auto application = UltraCanvasApplication::GetInstance();
        SetBounds(0, 0, config_.width, config_.height);
        if (UltraCanvasNativeWindow::CreateNative(config)) {
            application->RegisterWindow(this);
            return true;
        }
        return false;
    }

    void UltraCanvasWindow::Destroy() {
        if (!created_) {
            return;
        }
        auto application = UltraCanvasApplication::GetInstance();
        if (application) {
            application->UnregisterWindow(this);
        }
        UltraCanvasNativeWindow::Destroy();
        created_ = false;
    }


} // namespace UltraCanvas