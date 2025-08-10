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
    void UltraCanvasBaseWindow::SetApplication(UltraCanvasApplication* application_) {
        if (!application_) {
            std::cerr << "CRITICAL: No UltraCanvasLinuxApplication instance found!" << std::endl;
            throw std::runtime_error("Application not initialized");
        }
        application = application_;
    }

    void UltraCanvasBaseWindow::AddElement(UltraCanvasElement* element) {
        if (!element) return;

        // Set default z-index if not already set
        if (element->GetZIndex() == 0) {
            // Auto-assign appropriate z-index based on element type
            std::string typeName = typeid(*element).name();

            if (typeName.find("Menu") != std::string::npos) {
                element->SetZIndex(UltraCanvas::ZLayers::Menus);
            }
            else if (typeName.find("Dropdown") != std::string::npos) {
                element->SetZIndex(UltraCanvas::ZLayers::Dropdowns);
            }
            else if (typeName.find("Tooltip") != std::string::npos) {
                element->SetZIndex(UltraCanvas::ZLayers::Tooltips);
            }
            else {
                element->SetZIndex(UltraCanvas::ZLayers::Controls);
            }
        }

        elements.push_back(element);
        element->SetWindow(this);

        std::cout << "Added element: " << typeid(*element).name()
                  << " '" << element->GetIdentifier()
                  << "' with Z=" << element->GetZIndex() << std::endl;
        if (onElementAdded) onElementAdded(element);
        needsRedraw_ = true;
    }

    void UltraCanvasBaseWindow::AddElement(std::shared_ptr<UltraCanvasElement> element) {
        if (element) {
            AddElement(element.get());
            sharedElements.push_back(element); // Keep shared_ptr alive
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

    void UltraCanvasBaseWindow::BringElementToFront(UltraCanvasElement* element) {
        if (!element) return;

        long maxZ = UltraCanvasZOrderManager::GetMaxZIndex(elements);
        element->SetZIndex(maxZ + 1);

        std::cout << "Brought to front: " << element->GetIdentifier()
                  << " new Z=" << element->GetZIndex() << std::endl;
    }

    void UltraCanvasBaseWindow::SendElementToBack(UltraCanvasElement* element) {
        if (!element) return;

        long minZ = UltraCanvasZOrderManager::GetMinZIndex(elements);
        element->SetZIndex(minZ - 1);

        std::cout << "Sent to back: " << element->GetIdentifier()
                  << " new Z=" << element->GetZIndex() << std::endl;
    }

    // Fixed Render method with proper context management
    void UltraCanvasBaseWindow::Render() {
        // Set up the render context for this window
        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);

        // Clear the background
        int width, height;
        GetSize(width, height);

        // Draw window background
        SetFillColor(config_.backgroundColor);
        FillRect(Rect2D(0, 0, width, height));

        RenderCustomContent();

        // CRITICAL FIX: Auto-assign proper z-indexes for menu controls
        UltraCanvasZOrderManager::AutoAssignZIndexes(elements);

        // CRITICAL FIX: Sort elements by z-order before rendering
        auto sortedElements = UltraCanvasZOrderManager::GetElementsSortedByZOrder(elements);

        // Render all elements in correct depth order (back to front)
        for (auto* element : sortedElements) {
            if (element && element->IsVisible()) {
                element->Render();
            }
        }

        needsRedraw_ = false;
    }

//    void UltraCanvasBaseWindow::RenderDebug() {
//        // Set up the render context for this window
//        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);
//
//        // Clear the background
//        int width, height;
//        GetSize(width, height);
//
//        if (width <= 0 || height <= 0) {
//            std::cout << "⚠ Invalid window size: " << width << "x" << height << std::endl;
//            return;
//        }
//
//        // Draw window background
//        SetFillColor(config_.backgroundColor);
//        FillRect(Rect2D(0, 0, width, height));
//
//        std::cout << "=== Z-ORDER RENDERING DEBUG ===" << std::endl;
//        std::cout << "Window: " << width << "x" << height
//                  << " Background: RGB(" << (int)config_.backgroundColor.r
//                  << "," << (int)config_.backgroundColor.g
//                  << "," << (int)config_.backgroundColor.b << ")" << std::endl;
//
//        // Auto-assign z-indexes first
//        UltraCanvasZOrderManager::AutoAssignZIndexes(elements);
//
//        // Sort elements by z-order
//        auto sortedElements = UltraCanvasZOrderManager::GetElementsSortedByZOrder(elements);
//
//        std::cout << "Total elements: " << elements.size()
//                  << " → Sorted for rendering: " << sortedElements.size() << std::endl;
//
//        // Display z-order information
//        for (size_t i = 0; i < sortedElements.size(); i++) {
//            auto* element = sortedElements[i];
//            if (element) {
//                std::string className = typeid(*element).name();
//
//                // Clean up mangled class name
//                if (className.find("UltraCanvas") != std::string::npos) {
//                    size_t pos = className.find("UltraCanvas");
//                    className = className.substr(pos);
//                }
//
//                Rect2D bounds = element->GetBounds();
//
//                std::cout << "  [" << i << "] Z=" << element->GetZIndex()
//                          << " Type: " << className
//                          << " ID: '" << element->GetIdentifier() << "'"
//                          << " Bounds: (" << bounds.x << "," << bounds.y
//                          << " " << bounds.width << "x" << bounds.height << ")"
//                          << " Visible: " << (element->IsVisible() ? "YES" : "NO")
//                          << " Enabled: " << (element->IsEnabled() ? "YES" : "NO") << std::endl;
//
//                if (element->IsVisible()) {
//                    auto startTime = std::chrono::high_resolution_clock::now();
//
//                    element->Render();
//
//                    auto endTime = std::chrono::high_resolution_clock::now();
//                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
//
//                    std::cout << "    ✓ Rendered in " << duration.count() << "μs" << std::endl;
//                } else {
//                    std::cout << "    ⊗ Skipped (not visible)" << std::endl;
//                }
//            } else {
//                std::cout << "  [" << i << "] ⚠ NULL ELEMENT POINTER!" << std::endl;
//            }
//        }
//
//        std::cout << "=== END Z-ORDER RENDERING ===" << std::endl;
//        needsRedraw_ = false;
//    }

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


    // Fixed OnEvent method with proper event dispatching
    bool UltraCanvasBaseWindow::OnEvent(const UCEvent &event) {
        bool eventHandled = false;

        if (event.type != UCEventType::MouseMove) {
            std::cout << "Window event: " << static_cast<int>(event.type)
                      << " at (" << event.x << "," << event.y << ")" << std::endl;
        }

        // Handle mouse events with proper z-order hit testing
        if (event.type == UCEventType::MouseMove ||
            event.type == UCEventType::MouseDown ||
            event.type == UCEventType::MouseUp) {

            Point2D mousePos(event.x, event.y);

            // Send event to elements in z-order (topmost first)
            for (auto* element : elements) {
                if (element && element->IsVisible() && element->IsEnabled()) {
                    if ((element->IsHandleOutsideClicks() && (event.type == UCEventType::MouseDown || event.type == UCEventType::MouseUp))
                    || element->Contains(mousePos)) {
                        element->OnEvent(event);
                        if (event.type != UCEventType::MouseMove) {
                            std::cout << "Event handled by: " << typeid(*element).name()
                                      << " Z=" << element->GetZIndex() << std::endl;
                        }
                    }
                }
            }

            // If no element handled the event, it goes to the window
//            if (!eventHandled) {
//                onEvent(event);
//            }

        } else {
            // For non-mouse events, send to all visible/enabled elements
            for (auto* element : elements) {
                if (element && element->IsVisible() && element->IsEnabled()) {
                    eventHandled = element->OnEvent(event);
                    if (eventHandled) break;
                }
            }

            // Also send to window event handler
//            if (onEvent) {
//                onEvent(event);
//            }
        }

        // Force redraw if this was an interactive event
        if (event.type == UCEventType::MouseDown ||
            event.type == UCEventType::MouseUp ||
            event.type == UCEventType::KeyDown) {
            SetNeedsRedraw(true);
        }
        return eventHandled;
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