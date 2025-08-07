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
            element->SetWindow(this);
            elements.push_back(element.get());
            sharedElements.push_back(element); // Keep shared_ptr alive
            needsRedraw_ = true;
            if (onElementAdded) onElementAdded(element.get());
        }
    }

    void UltraCanvasBaseWindow::AddElement(UltraCanvasElement* element) {
        if (element) {
            element->SetWindow(this);
            elements.push_back(element);
            needsRedraw_ = true;
            if (onElementAdded) onElementAdded(element);
        }
    }

    void UltraCanvasBaseWindow::RemoveElement(std::shared_ptr<UltraCanvasElement> element) {
        if (element) {
            element->SetWindow(nullptr);
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
            element->SetWindow(nullptr);
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
        std::cout << "=== UltraCanvasBaseWindow::Render() START ===" << std::endl;

        // Set up the render context for this window
        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);

        // Clear the background
        int width, height;
        GetSize(width, height);
        std::cout << "Window size: " << width << "x" << height << std::endl;

        // Draw window background
        if (config_.backgroundColor != Colors::Transparent) {
            SetFillColor(config_.backgroundColor);
            FillRect(Rect2D(0, 0, width, height));
            std::cout << "Background cleared with color: R=" << (int)config_.backgroundColor.r
                      << " G=" << (int)config_.backgroundColor.g
                      << " B=" << (int)config_.backgroundColor.b << std::endl;
        }

        // Render all elements with detailed debug output
        std::cout << "Rendering " << elements.size() << " UI elements:" << std::endl;

        int elementIndex = 0;
        for (auto* element : elements) {
            if (element) {
                // Get element information
                std::string elementId = element->GetIdentifier();
                long elementIdNum = element->GetIdentifierID();
                Rect2D bounds = element->GetBounds();
                bool isVisible = element->IsVisible();
                bool isEnabled = element->IsEnabled();

                // Get the actual class name using typeid
                std::string className = typeid(*element).name();

                // Clean up the mangled class name (basic demangling for common cases)
                if (className.find("UltraCanvas") != std::string::npos) {
                    size_t pos = className.find("UltraCanvas");
                    if (pos != std::string::npos) {
                        className = className.substr(pos);
                    }
                }

                std::cout << "  [" << elementIndex << "] Class: " << className
                          << ", ID: '" << elementId << "' (" << elementIdNum << ")"
                          << ", Position: (" << bounds.x << "," << bounds.y << ")"
                          << ", Size: " << bounds.width << "x" << bounds.height
                          << ", Visible: " << (isVisible ? "true" : "false")
                          << ", Enabled: " << (isEnabled ? "true" : "false") << std::endl;

                if (isVisible) {
                    std::cout << "    → Calling Render() on " << className << std::endl;

                    // Track render time for performance debugging
                    auto startTime = std::chrono::high_resolution_clock::now();

                    element->Render();

                    auto endTime = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

                    std::cout << "    ✓ Rendered " << className << " in " << duration.count() << "μs" << std::endl;
                } else {
                    std::cout << "    ⊗ Skipped (not visible): " << className << std::endl;
                }
            } else {
                std::cout << "  [" << elementIndex << "] ⚠ NULL ELEMENT POINTER!" << std::endl;
            }
            elementIndex++;
        }

        std::cout << "Total elements processed: " << elementIndex << std::endl;

        needsRedraw_ = false;
        std::cout << "=== UltraCanvasBaseWindow::Render() COMPLETE ===" << std::endl;
    }

// Additional helper method for runtime type inspection (add to UltraCanvasBaseWindow class)
    std::string UltraCanvasBaseWindow::GetElementTypeName(UltraCanvasElement* element) {
        if (!element) return "NULL";

        std::string typeName = typeid(*element).name();

        // Basic demangling for common UltraCanvas types
        if (typeName.find("UltraCanvasDropdown") != std::string::npos) {
            return "UltraCanvasDropdown";
        }
        else if (typeName.find("UltraCanvasButton") != std::string::npos) {
            return "UltraCanvasButton";
        }
        else if (typeName.find("UltraCanvasTextInput") != std::string::npos) {
            return "UltraCanvasTextInput";
        }
        else if (typeName.find("UltraCanvasTreeView") != std::string::npos) {
            return "UltraCanvasTreeView";
        }
        else if (typeName.find("UltraCanvasContainer") != std::string::npos) {
            return "UltraCanvasContainer";
        }
        else if (typeName.find("UltraCanvasElement") != std::string::npos) {
            return "UltraCanvasElement";
        }

        // For unknown types, try to extract class name from mangled name
        size_t pos = typeName.find_last_of(":");
        if (pos != std::string::npos && pos + 1 < typeName.length()) {
            return typeName.substr(pos + 1);
        }

        return typeName; // Return mangled name if can't demangle
    }

// Enhanced element debugging method (add to UltraCanvasBaseWindow class)
    void UltraCanvasBaseWindow::DebugPrintElements() {
        std::cout << "\n=== WINDOW DEBUG: Element Hierarchy ===" << std::endl;
        std::cout << "Window: '" << GetTitle() << "'" << std::endl;
        int width, height;
        GetSize(width, height);
        std::cout << "Window Size: " << width << "x" << height << std::endl;
        std::cout << "Total Elements: " << elements.size() << std::endl;

        for (size_t i = 0; i < elements.size(); i++) {
            auto* element = elements[i];
            if (element) {
                Rect2D bounds = element->GetBounds();
                std::cout << "  Element[" << i << "]: " << GetElementTypeName(element)
                          << " '" << element->GetIdentifier() << "'"
                          << " at (" << bounds.x << "," << bounds.y << ")"
                          << " size " << bounds.width << "x" << bounds.height;

                if (!element->IsVisible()) std::cout << " [HIDDEN]";
                if (!element->IsEnabled()) std::cout << " [DISABLED]";

                std::cout << std::endl;

                // Print children if any
                const auto& children = element->GetChildren();
                if (!children.empty()) {
                    for (size_t j = 0; j < children.size(); j++) {
                        auto* child = children[j];
                        if (child) {
                            Rect2D childBounds = child->GetBounds();
                            std::cout << "    Child[" << j << "]: " << GetElementTypeName(child)
                                      << " '" << child->GetIdentifier() << "'"
                                      << " at (" << childBounds.x << "," << childBounds.y << ")"
                                      << " size " << childBounds.width << "x" << childBounds.height;

                            if (!child->IsVisible()) std::cout << " [HIDDEN]";
                            if (!child->IsEnabled()) std::cout << " [DISABLED]";

                            std::cout << std::endl;
                        }
                    }
                }
            } else {
                std::cout << "  Element[" << i << "]: NULL POINTER!" << std::endl;
            }
        }
        std::cout << "=== END Element Hierarchy ===" << std::endl;
    }

// Minimal debug version (if you want less verbose output)
    void UltraCanvasBaseWindow::RenderMinimalDebug() {
        // Set up the render context for this window
        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);

        // Clear the background
        int width, height;
        GetSize(width, height);

        // Draw window background
        SetFillColor(config_.backgroundColor);
        FillRect(Rect2D(0, 0, width, height));

        // Render all elements with minimal debug
        std::cout << "Rendering " << elements.size() << " elements: ";

        for (size_t i = 0; i < elements.size(); i++) {
            auto* element = elements[i];
            if (element && element->IsVisible()) {
                std::cout << GetElementTypeName(element) << "(" << element->GetIdentifier() << ") ";
                element->Render();
            }
        }
        std::cout << std::endl;

        needsRedraw_ = false;
    }

//    void UltraCanvasBaseWindow::Render() {
////        if (!IsVisible()) return;
//
//        // Set up the render context for this window
//        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);
//
//        // Clear the background
//        int width, height;
//        GetSize(width, height);
//
//        // Draw window background
//        SetFillColor(config_.backgroundColor);
//        FillRect(Rect2D(0, 0, width, height));
//
//        // Render all elements
//        for (auto* element : elements) {
//            if (element && element->IsVisible()) {
//                element->Render();
//            }
//        }
//
//        needsRedraw_ = false;
//    }

    // Fixed OnEvent method with proper event dispatching
    void UltraCanvasBaseWindow::OnEvent(const UCEvent &event) {
        if (event.type != UCEventType::MouseMove) {
            std::cout << "UltraCanvasBaseWindow::OnEvent - type: " << (int) event.type
                      << " elements: " << elements.size() << std::endl;
        }

        // Use the event dispatcher to handle events properly
        bool handled = false;

        // Try to dispatch to UI elements first
        for (auto *element: elements) {
            if (element && element->IsVisible() && element->IsActive()) {
                // Check if element contains the point (for mouse events)
                bool elementShouldReceive = false;

                if (event.type == UCEventType::MouseDown ||
                    event.type == UCEventType::MouseUp ||
                    event.type == UCEventType::MouseWheel ||
                    event.type == UCEventType::MouseWheelHorizontal ||
                    event.type == UCEventType::MouseMove) {

                    // For mouse events, check if the element contains the point
                    elementShouldReceive = element->Contains(event.x, event.y);
                    if (event.type != UCEventType::MouseMove) {
                        std::cout << "  Element '" << element->GetIdentifier()
                                  << "' contains point (" << event.x << "," << event.y << "): "
                                  << elementShouldReceive << std::endl;
                    }
                } else if (event.type == UCEventType::KeyDown || event.type == UCEventType::KeyUp) {
// For keyboard events, send to focused element
                    elementShouldReceive = element->IsFocused();
                    if (event.type != UCEventType::MouseMove) {
                        std::cout << "  Element '" << element->GetIdentifier()
                                  << "' is focused: " << elementShouldReceive << std::endl;
                    }
                }

                if (elementShouldReceive) {
                    if (event.type != UCEventType::MouseMove) {
                        std::cout << "  → Forwarding event to element '"
                                  << element->GetIdentifier() << "'" << std::endl;
                    }
                    element->OnEvent(event);
                    handled = true;

// For mouse down events, we might want to stop after first hit
                    if (event.type == UCEventType::MouseDown) {
                        break;
                    }
                }
            }
        }

// Handle window-specific events if not handled by elements
        if (!handled) {
            std::cout << "Event not handled by any element, processing as window event" << std::endl;
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

                default:
                    break;
            }
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