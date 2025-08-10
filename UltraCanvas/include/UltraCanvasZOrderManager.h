// UltraCanvasZOrderManager.h
// Z-order management system for proper rendering depth
// Version: 1.0.0
// Last Modified: 2025-08-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <algorithm>
#include <functional>

namespace UltraCanvas {

// Forward declarations
    class UltraCanvasElement;
    class UltraCanvasBaseWindow;

// ===== Z-ORDER MANAGEMENT SYSTEM =====
    class UltraCanvasZOrderManager {
    public:
        // Sort elements by z-index (lower values render first, higher values on top)
        static void SortElementsByZOrder(std::vector<UltraCanvasElement*>& elements) {
            std::stable_sort(elements.begin(), elements.end(),
                             [](const UltraCanvasElement* a, const UltraCanvasElement* b) {
                                 if (!a) return true;  // null elements go to beginning
                                 if (!b) return false;
                                 return a->GetZIndex() < b->GetZIndex();
                             });
        }

        // Sort shared_ptr elements by z-index
        static void SortElementsByZOrder(std::vector<std::shared_ptr<UltraCanvasElement>>& elements) {
            std::stable_sort(elements.begin(), elements.end(),
                             [](const std::shared_ptr<UltraCanvasElement>& a, const std::shared_ptr<UltraCanvasElement>& b) {
                                 if (!a) return true;  // null elements go to beginning
                                 if (!b) return false;
                                 return a->GetZIndex() < b->GetZIndex();
                             });
        }

        // Get elements sorted by z-order (creates new vector, original unchanged)
        static std::vector<UltraCanvasElement*> GetElementsSortedByZOrder(
                const std::vector<UltraCanvasElement*>& elements) {
            std::vector<UltraCanvasElement*> sorted = elements;
            SortElementsByZOrder(sorted);
            return sorted;
        }

        // Find the highest z-index in a collection
        static long GetMaxZIndex(const std::vector<UltraCanvasElement*>& elements) {
            long maxZ = 0;
            for (const auto* element : elements) {
                if (element && element->GetZIndex() > maxZ) {
                    maxZ = element->GetZIndex();
                }
            }
            return maxZ;
        }

        // Find the lowest z-index in a collection
        static long GetMinZIndex(const std::vector<UltraCanvasElement*>& elements) {
            long minZ = 0;
            bool first = true;
            for (const auto* element : elements) {
                if (element) {
                    if (first || element->GetZIndex() < minZ) {
                        minZ = element->GetZIndex();
                        first = false;
                    }
                }
            }
            return minZ;
        }

        // Bring element to front (highest z-index + 1)
        static void BringToFront(UltraCanvasElement* element,
                                 const std::vector<UltraCanvasElement*>& allElements) {
            if (!element) return;
            long maxZ = GetMaxZIndex(allElements);
            element->SetZIndex(maxZ + 1);
        }

        // Send element to back (lowest z-index - 1)
        static void SendToBack(UltraCanvasElement* element,
                               const std::vector<UltraCanvasElement*>& allElements) {
            if (!element) return;
            long minZ = GetMinZIndex(allElements);
            element->SetZIndex(minZ - 1);
        }

        // Move element up one layer
        static void MoveUp(UltraCanvasElement* element,
                           const std::vector<UltraCanvasElement*>& allElements) {
            if (!element) return;

            // Find the next highest z-index
            long currentZ = element->GetZIndex();
            long nextZ = currentZ + 1;

            for (const auto* other : allElements) {
                if (other && other != element && other->GetZIndex() > currentZ) {
                    nextZ = std::min(nextZ, other->GetZIndex());
                    break;
                }
            }

            if (nextZ > currentZ) {
                element->SetZIndex(nextZ);
            }
        }

        // Move element down one layer
        static void MoveDown(UltraCanvasElement* element,
                             const std::vector<UltraCanvasElement*>& allElements) {
            if (!element) return;

            // Find the next lowest z-index
            long currentZ = element->GetZIndex();
            long prevZ = currentZ - 1;

            for (const auto* other : allElements) {
                if (other && other != element && other->GetZIndex() < currentZ) {
                    prevZ = std::max(prevZ, other->GetZIndex());
                    break;
                }
            }

            if (prevZ < currentZ) {
                element->SetZIndex(prevZ);
            }
        }

        // Auto-assign z-indexes to ensure proper layering for specific element types
        static void AutoAssignZIndexes(std::vector<UltraCanvasElement*>& elements) {
            // Assign default z-indexes based on element type priority
            // Menus and dropdowns should be on top, followed by dialogs, then regular elements

            long backgroundZ = 0;   // Background elements
            long normalZ = 100;     // Normal UI elements
            long overlayZ = 500;    // Overlays, tooltips
            long menuZ = 1000;      // Menus, dropdowns
            long modalZ = 2000;     // Modal dialogs
            long popupZ = 3000;     // Context menus, popups

            for (auto* element : elements) {
                if (!element) continue;

                // Get element type name for classification
                std::string typeName = typeid(*element).name();

                if (typeName.find("Menu") != std::string::npos) {
                    element->SetZIndex(menuZ++);
                }
                else if (typeName.find("Dropdown") != std::string::npos) {
                    element->SetZIndex(menuZ++);
                }
                else if (typeName.find("Popup") != std::string::npos ||
                         typeName.find("Context") != std::string::npos) {
                    element->SetZIndex(popupZ++);
                }
                else if (typeName.find("Modal") != std::string::npos ||
                         typeName.find("Dialog") != std::string::npos) {
                    element->SetZIndex(modalZ++);
                }
                else if (typeName.find("Tooltip") != std::string::npos ||
                         typeName.find("Overlay") != std::string::npos) {
                    element->SetZIndex(overlayZ++);
                }
                else {
                    // Normal elements keep their current z-index or get default
                    if (element->GetZIndex() == 0) {
                        element->SetZIndex(normalZ++);
                    }
                }
            }
        }

        // Get elements at a specific point, sorted by z-order (highest first)
        static std::vector<UltraCanvasElement*> GetElementsAtPoint(
                const std::vector<UltraCanvasElement*>& elements,
                const Point2D& point) {

            std::vector<UltraCanvasElement*> hitElements;

            for (auto* element : elements) {
                if (element && element->IsVisible() && element->Contains(point)) {
                    hitElements.push_back(element);
                }
            }

            // Sort by z-order, highest first (for hit testing)
            std::stable_sort(hitElements.begin(), hitElements.end(),
                             [](const UltraCanvasElement* a, const UltraCanvasElement* b) {
                                 return a->GetZIndex() > b->GetZIndex();
                             });

            return hitElements;
        }

        // Get the topmost element at a point
        static UltraCanvasElement* GetTopElementAtPoint(
                const std::vector<UltraCanvasElement*>& elements,
                const Point2D& point) {

            auto hitElements = GetElementsAtPoint(elements, point);
            return hitElements.empty() ? nullptr : hitElements[0];
        }
    };

// ===== ENHANCED WINDOW RENDERING WITH Z-ORDER =====
// This should be integrated into UltraCanvasBaseWindow::Render()

    inline void RenderElementsWithZOrder(const std::vector<UltraCanvasElement*>& elements) {
        // Get elements sorted by z-order for proper rendering
        auto sortedElements = UltraCanvasZOrderManager::GetElementsSortedByZOrder(elements);

        // Render from lowest to highest z-index
        for (auto* element : sortedElements) {
            if (element && element->IsVisible()) {
                element->Render();
            }
        }
    }

// ===== Z-ORDER UTILITY FUNCTIONS =====

// Convenience functions for common z-order operations
    inline void BringElementToFront(UltraCanvasElement* element, UltraCanvasBaseWindow* window) {
        if (!element || !window) return;

        // This would need access to window's elements vector
        // Implementation depends on window's element management system
        element->SetZIndex(element->GetZIndex() + 1000); // Simple approach
    }

    inline void SendElementToBack(UltraCanvasElement* element) {
        if (!element) return;
        element->SetZIndex(-1000); // Simple approach to send to back
    }

// Set specific z-index layers for common UI elements
    namespace ZLayers {
        constexpr long Background = -1000;
        constexpr long Content = 0;
        constexpr long Controls = 100;
        constexpr long Overlays = 500;
        constexpr long Menus = 1000;
        constexpr long Dropdowns = 1500;
        constexpr long Modals = 2000;
        constexpr long Popups = 2500;
        constexpr long Tooltips = 3000;
        constexpr long Debug = 9999;
    }

} // namespace UltraCanvas

// ===== WINDOW RENDER METHOD PATCH =====
// Add this to UltraCanvasWindow.cpp to fix the Menu z-order issue

/*
// REPLACE the existing window Render() method with this z-order aware version:

void UltraCanvasBaseWindow::Render() {
    // Set up the render context for this window
    ULTRACANVAS_WINDOW_RENDER_SCOPE(this);

    // Clear the background
    int width, height;
    GetSize(width, height);

    // Draw window background
    SetFillColor(config_.backgroundColor);
    FillRect(Rect2D(0, 0, width, height));

    // FIXED: Sort elements by z-order before rendering
    auto sortedElements = UltraCanvasZOrderManager::GetElementsSortedByZOrder(elements);

    // Render all elements in z-order (lowest to highest)
    for (auto* element : sortedElements) {
        if (element && element->IsVisible()) {
            element->Render();
        }
    }

    needsRedraw_ = false;
}

// ALSO ADD this enhanced debug version:

void UltraCanvasBaseWindow::RenderDebug() {
    // Set up the render context for this window
    ULTRACANVAS_WINDOW_RENDER_SCOPE(this);

    // Clear the background
    int width, height;
    GetSize(width, height);

    // Draw window background
    SetFillColor(config_.backgroundColor);
    FillRect(Rect2D(0, 0, width, height));

    std::cout << "=== RENDERING WITH Z-ORDER ===" << std::endl;
    std::cout << "Window size: " << width << "x" << height
              << ", Background: RGB(" << (int)config_.backgroundColor.r
              << "," << (int)config_.backgroundColor.g
              << "," << (int)config_.backgroundColor.b << ")" << std::endl;

    // FIXED: Sort elements by z-order before rendering
    auto sortedElements = UltraCanvasZOrderManager::GetElementsSortedByZOrder(elements);

    std::cout << "Rendering " << sortedElements.size() << " elements by Z-ORDER:" << std::endl;

    for (size_t i = 0; i < sortedElements.size(); i++) {
        auto* element = sortedElements[i];
        if (element) {
            std::string className = typeid(*element).name();
            if (className.find("UltraCanvas") != std::string::npos) {
                size_t pos = className.find("UltraCanvas");
                className = className.substr(pos);
            }

            std::cout << "  [" << i << "] Z=" << element->GetZIndex()
                      << " Class: " << className
                      << " ID: '" << element->GetIdentifier() << "'"
                      << " Pos: (" << element->GetX() << "," << element->GetY() << ")"
                      << " Size: " << element->GetWidth() << "x" << element->GetHeight()
                      << " Visible: " << (element->IsVisible() ? "true" : "false") << std::endl;

            if (element->IsVisible()) {
                element->Render();
                std::cout << "    ✓ Rendered " << className << " at Z=" << element->GetZIndex() << std::endl;
            } else {
                std::cout << "    ⊗ Skipped (not visible)" << std::endl;
            }
        }
    }

    std::cout << "=== END Z-ORDER RENDERING ===" << std::endl;
    needsRedraw_ = false;
}

// ENHANCED HIT TESTING with z-order support:

UltraCanvasElement* UltraCanvasBaseWindow::GetElementAtPoint(const Point2D& point) {
    return UltraCanvasZOrderManager::GetTopElementAtPoint(elements, point);
}

// EVENT HANDLING with proper z-order hit testing:

void UltraCanvasBaseWindow::OnEvent(const UCEvent& event) {
    if (event.type == UCEventType::MouseMove ||
        event.type == UCEventType::MouseButtonDown ||
        event.type == UCEventType::MouseButtonUp) {

        Point2D mousePos(event.mouseX, event.mouseY);

        // Get elements at mouse position, sorted by z-order (highest first)
        auto hitElements = UltraCanvasZOrderManager::GetElementsAtPoint(elements, mousePos);

        // Send event to the topmost element first
        for (auto* element : hitElements) {
            if (element && element->IsEnabled()) {
                bool handled = element->OnEvent(event);
                if (handled) {
                    break; // Event consumed by topmost element
                }
            }
        }
    } else {
        // For non-mouse events, send to all elements
        for (auto* element : elements) {
            if (element && element->IsVisible() && element->IsEnabled()) {
                element->OnEvent(event);
            }
        }
    }
}
*/