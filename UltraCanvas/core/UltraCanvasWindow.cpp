// UltraCanvasBaseWindow.cpp
// Fixed implementation of cross-platform window management system
// Version: 1.2.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework

#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasEventDispatcher.h"
#include "../include/UltraCanvasRenderInterface.h"
#include "../include/UltraCanvasApplication.h"

#include <iostream>
#include <algorithm>

namespace UltraCanvas {
    void UltraCanvasBaseWindow::SetApplication(UltraCanvasApplication* application_) {
        if (!application_) {
            std::cerr << "CRITICAL: No UltraCanvasLinuxApplication instance found!" << std::endl;
            throw std::runtime_error("Application not initialized");
        }
        application = application_;
    }

    void UltraCanvasBaseWindow::AddElement(std::shared_ptr<UltraCanvasElement> element) {
        if (element) {
            elements.push_back(element.get());
            sharedElements.push_back(element); // Keep shared_ptr alive
            needsRedraw_ = true;
            if (onElementAdded) onElementAdded(element.get());
        }
    }

    void UltraCanvasBaseWindow::AddElement(UltraCanvasElement* element) {
        if (element) {
            elements.push_back(element);
            needsRedraw_ = true;
            if (onElementAdded) onElementAdded(element);
        }
    }

    void UltraCanvasBaseWindow::RemoveElement(std::shared_ptr<UltraCanvasElement> element) {
        if (element) {
            RemoveElement(element.get());
            // Remove from shared_ptr list
            sharedElements.erase(
                    std::remove(sharedElements.begin(), sharedElements.end(), element),
                    sharedElements.end()
            );
        }
    }

    void UltraCanvasBaseWindow::RemoveElement(UltraCanvasElement* element) {
        if (element) {
            auto it = std::find(elements.begin(), elements.end(), element);
            if (it != elements.end()) {
                elements.erase(it);
                needsRedraw_ = true;
                if (onElementRemoved) onElementRemoved(element);
            }
        }
    }

    void UltraCanvasBaseWindow::ClearElements() {
        elements.clear();
        sharedElements.clear();
        needsRedraw_ = true;
    }

    UltraCanvasElement* UltraCanvasBaseWindow::FindElementById(const std::string& id) {
        for (auto* element : elements) {
            if (element->GetIdentifier() == id) {
                return element;
            }

            // Search children recursively
            auto* found = element->FindChildById(id);
            if (found) return found;
        }
        return nullptr;
    }

    // Fixed Render method with proper context management
    void UltraCanvasBaseWindow::Render() {
//        if (!IsVisible()) return;

        // Set up the render context for this window
        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);

        // Clear the background
        int width, height;
        GetSize(width, height);

        // Draw window background
        SetFillColor(config_.backgroundColor);
        FillRect(Rect2D(0, 0, width, height));

        // Render all elements
        for (auto* element : elements) {
            if (element && element->IsVisible()) {
                element->Render();
            }
        }

        needsRedraw_ = false;
    }

    // Fixed OnEvent method with proper event dispatching
    void UltraCanvasBaseWindow::OnEvent(const UCEvent& event) {
        // Use the UltraCanvasEventDispatcher to handle events properly
        bool handled = UltraCanvasEventDispatcher::DispatchEvent(event, elements);

        // Handle window-specific events if not handled by elements
        if (!handled) {
            switch (event.type) {
                case UCEventType::WindowClose:
                    if (onWindowClosing) {
                        onWindowClosing();
                    }
                    break;

                case UCEventType::WindowResize:
                    if (onWindowResized) {
                        onWindowResized(event.width, event.height);
                    }
                    SetNeedsRedraw(true);
                    break;

                case UCEventType::WindowRepaint:
                    SetNeedsRedraw(true);
                    break;

                case UCEventType::WindowFocus:
                    if (onWindowFocused) {
                        onWindowFocused();
                    }
                    break;

                case UCEventType::WindowBlur:
                    if (onWindowBlurred) {
                        onWindowBlurred();
                    }
                    break;
            }
        }

        // Trigger redraw if needed
        if (needsRedraw_) {
            Render();
        }
    }


    // UltraCanvasWindow

    bool UltraCanvasWindow::Create(const WindowConfig& config) {
        config_ = config;
        SetApplication(UltraCanvasApplication::GetInstance());

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
        if (application) {
            application->UnregisterWindow(this);
        }
        UltraCanvasNativeWindow::Destroy();
        created_ = false;
    }


} // namespace UltraCanvas