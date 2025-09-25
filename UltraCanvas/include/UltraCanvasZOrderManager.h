// include/UltraCanvasZOrderManager.h
// Enhanced Z-order management system with container and dropdown support
// Version: 1.1.0
// Last Modified: 2025-08-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <memory>

namespace UltraCanvas {
    // Forward declarations
    class UltraCanvasUIElement;
    class UltraCanvasBaseWindow;
    class UltraCanvasContainer;
    class UltraCanvasDropdown;

    namespace ZLayers {
        constexpr long Background = -1000;     // Background elements
        constexpr long Content = 0;            // Main content areas
        constexpr long Controls = 100;         // Standard UI controls
        constexpr long Overlays = 500;         // Overlays, tooltips
        constexpr long Menus = 1000;          // Menu bars
        constexpr long Dropdowns = 1500;      // Dropdown lists (when open)
        constexpr long Modals = 2000;         // Modal dialogs
        constexpr long Popups = 2500;         // Context menus, popups
        constexpr long Tooltips = 3000;       // Tooltips
        constexpr long Debug = 9999;          // Debug overlays
    }


// ===== ENHANCED Z-ORDER MANAGEMENT SYSTEM =====
    class UltraCanvasZOrderManager {
    public:
        // ===== CORE SORTING METHODS =====

        // Sort elements by z-index (lower values render first, higher values on top)
        static void SortElementsByZOrder(std::vector<UltraCanvasUIElement*>& elements) {
            std::stable_sort(elements.begin(), elements.end(),
                             [](const UltraCanvasUIElement* a, const UltraCanvasUIElement* b) {
                                 if (!a) return true;  // null elements go to beginning
                                 if (!b) return false;
                                 return a->GetZIndex() < b->GetZIndex();
                             });
        }

        // Sort shared_ptr elements by z-index
        static void SortElementsByZOrder(std::vector<std::shared_ptr<UltraCanvasUIElement>>& elements) {
            std::stable_sort(elements.begin(), elements.end(),
                             [](const std::shared_ptr<UltraCanvasUIElement>& a, const std::shared_ptr<UltraCanvasUIElement>& b) {
                                 if (!a) return true;  // null elements go to beginning
                                 if (!b) return false;
                                 return a->GetZIndex() < b->GetZIndex();
                             });
        }

        // Get elements sorted by z-order (creates new vector, original unchanged)
        static std::vector<UltraCanvasUIElement*> GetElementsSortedByZOrder(
                const std::vector<UltraCanvasUIElement*>& elements) {
            std::vector<UltraCanvasUIElement*> sorted = elements;
            SortElementsByZOrder(sorted);
            return sorted;
        }

        // ===== HIERARCHICAL Z-ORDER METHODS =====

        // Collect all elements from containers recursively, maintaining hierarchy
        static std::vector<UltraCanvasUIElement*> GetAllElementsFlattened(
                const std::vector<UltraCanvasUIElement*>& topLevelElements) {

            std::vector<UltraCanvasUIElement*> allElements;
            std::unordered_set<UltraCanvasUIElement*> visited;

            for (auto* element : topLevelElements) {
                CollectElementsRecursively(element, allElements, visited);
            }

            return allElements;
        }

        // Get flattened elements sorted by z-order
        static std::vector<UltraCanvasUIElement*> GetAllElementsSortedByZOrder(
                const std::vector<UltraCanvasUIElement*>& topLevelElements) {

            auto allElements = GetAllElementsFlattened(topLevelElements);
            SortElementsByZOrder(allElements);
            return allElements;
        }

        // ===== Z-INDEX UTILITY METHODS =====

        // Find the highest z-index in a collection
        static long GetMaxZIndex(const std::vector<UltraCanvasUIElement*>& elements) {
            long maxZ = UltraCanvas::ZLayers::Background;
            for (const auto* element : elements) {
                if (element && element->GetZIndex() > maxZ) {
                    maxZ = element->GetZIndex();
                }
            }
            return maxZ;
        }

        // Find the lowest z-index in a collection
        static long GetMinZIndex(const std::vector<UltraCanvasUIElement*>& elements) {
            long minZ = UltraCanvas::ZLayers::Debug;
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

        // ===== ELEMENT MANIPULATION METHODS =====

        // Bring element to front (highest z-index + 1)
        static void BringToFront(UltraCanvasUIElement* element,
                                 const std::vector<UltraCanvasUIElement*>& allElements) {
            if (!element) return;
            long maxZ = GetMaxZIndex(allElements);
            element->SetZIndex(maxZ + 1);
        }

        // Send element to back (lowest z-index - 1)
        static void SendToBack(UltraCanvasUIElement* element,
                               const std::vector<UltraCanvasUIElement*>& allElements) {
            if (!element) return;
            long minZ = GetMinZIndex(allElements);
            element->SetZIndex(minZ - 1);
        }

        // Move element up one layer
        static void MoveUp(UltraCanvasUIElement* element,
                           const std::vector<UltraCanvasUIElement*>& allElements) {
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
        static void MoveDown(UltraCanvasUIElement* element,
                             const std::vector<UltraCanvasUIElement*>& allElements) {
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

        // ===== AUTO Z-INDEX ASSIGNMENT =====

        // Auto-assign z-indexes to ensure proper layering for specific element types
        static void AutoAssignZIndexes(std::vector<UltraCanvasUIElement*>& elements) {
            // Assign default z-indexes based on element type priority
            long backgroundZ = UltraCanvas::ZLayers::Background;
            long contentZ = UltraCanvas::ZLayers::Content;
            long controlZ = UltraCanvas::ZLayers::Controls;
            long overlayZ = UltraCanvas::ZLayers::Overlays;
            long menuZ = UltraCanvas::ZLayers::Menus;
            long dropdownZ = UltraCanvas::ZLayers::Dropdowns;
            long modalZ = UltraCanvas::ZLayers::Modals;
            long popupZ = UltraCanvas::ZLayers::Popups;
            long tooltipZ = UltraCanvas::ZLayers::Tooltips;

            for (auto* element : elements) {
                if (!element) continue;

                // Get element type name for classification
                std::string typeName = typeid(*element).name();

                if (typeName.find("Menu") != std::string::npos) {
                    element->SetZIndex(menuZ++);
                }
                else if (typeName.find("Dropdown") != std::string::npos) {
                    element->SetZIndex(controlZ++);  // Base z-index for dropdowns
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
                else if (typeName.find("Background") != std::string::npos) {
                    element->SetZIndex(backgroundZ++);
                }
                else if (typeName.find("Container") != std::string::npos) {
                    element->SetZIndex(contentZ++);
                }
                else {
                    // Regular UI controls
                    if (element->GetZIndex() == 0) {
                        element->SetZIndex(controlZ++);
                    }
                }
            }
        }

        // ===== HIT TESTING METHODS =====

        // Get elements at a specific point, sorted by z-order (highest first for hit testing)
        static std::vector<UltraCanvasUIElement*> GetElementsAtPoint(
                const std::vector<UltraCanvasUIElement*>& elements,
                const Point2D& point) {

            std::vector<UltraCanvasUIElement*> hitElements;

            // Collect all elements that contain the point (including nested containers)
            auto allElements = GetAllElementsFlattened(elements);

            for (auto* element : allElements) {
                if (element && element->IsVisible() && element->Contains(point)) {
                    hitElements.push_back(element);
                }
            }

            // Sort by z-order, highest first (for hit testing)
            std::stable_sort(hitElements.begin(), hitElements.end(),
                             [](const UltraCanvasUIElement* a, const UltraCanvasUIElement* b) {
                                 return a->GetZIndex() > b->GetZIndex();
                             });

            return hitElements;
        }

        // Get the topmost element at a point
        static UltraCanvasUIElement* GetTopElementAtPoint(
                const std::vector<UltraCanvasUIElement*>& elements,
                const Point2D& point) {

            auto hitElements = GetElementsAtPoint(elements, point);
            return hitElements.empty() ? nullptr : hitElements[0];
        }

        // ===== DEBUGGING AND DIAGNOSTIC METHODS =====

        // Print z-order hierarchy for debugging
        static void PrintZOrderHierarchy(const std::vector<UltraCanvasUIElement*>& elements,
                                         const std::string& title = "Z-Order Hierarchy") {
            std::cout << "=== " << title << " ===" << std::endl;

            auto sorted = GetElementsSortedByZOrder(elements);

            for (size_t i = 0; i < sorted.size(); i++) {
                auto* element = sorted[i];
                if (element) {
                    std::string typeName = typeid(*element).name();
                    if (typeName.find("UltraCanvas") != std::string::npos) {
                        size_t pos = typeName.find("UltraCanvas");
                        typeName = typeName.substr(pos);
                    }

                    std::cout << "  [" << i << "] Z=" << element->GetZIndex()
                              << " " << typeName
                              << " '" << element->GetIdentifier() << "'"
                              << " Visible=" << (element->IsVisible() ? "true" : "false")
                              << std::endl;
                }
            }
            std::cout << "=========================" << std::endl;
        }

        // Validate z-order consistency
        static bool ValidateZOrder(const std::vector<UltraCanvasUIElement*>& elements) {
            bool valid = true;
            auto sorted = GetElementsSortedByZOrder(elements);

            for (size_t i = 1; i < sorted.size(); i++) {
                if (sorted[i]->GetZIndex() < sorted[i-1]->GetZIndex()) {
                    std::cout << "Z-Order validation failed at index " << i << std::endl;
                    valid = false;
                }
            }

            return valid;
        }

    private:
        // ===== PRIVATE HELPER METHODS =====

        // Recursively collect all elements from containers
        static void CollectElementsRecursively(UltraCanvasUIElement* element,
                                               std::vector<UltraCanvasUIElement*>& collection,
                                               std::unordered_set<UltraCanvasUIElement*>& visited) {
            if (!element || visited.find(element) != visited.end()) {
                return;  // Already visited or null
            }

            visited.insert(element);
            collection.push_back(element);

            // Check if element is a container and collect its children
            UltraCanvasContainer* container = dynamic_cast<UltraCanvasContainer*>(element);
            if (container) {
                auto children = container->GetChildren();
                for (auto& child : children) {
                    CollectElementsRecursively(child.get(), collection, visited);
                }
            }
        }
    };

// ===== ENHANCED WINDOW RENDERING INTEGRATION =====

// Template function for z-order aware rendering
template<typename ElementContainer>
inline void RenderElementsWithZOrder(const ElementContainer& elements) {
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
inline void BringElementToFront(UltraCanvasUIElement* element, UltraCanvasBaseWindow* window) {
    if (!element || !window) return;

    // Get all elements from window and bring this one to front
    auto allElements = window->GetAllElements();
    UltraCanvasZOrderManager::BringToFront(element, allElements);
    window->RequestZOrderUpdate();
}

inline void SendElementToBack(UltraCanvasUIElement* element, UltraCanvasBaseWindow* window) {
    if (!element || !window) return;

    auto allElements = window->GetAllElements();
    UltraCanvasZOrderManager::SendToBack(element, allElements);
    window->RequestZOrderUpdate();
}

// Special handling for dropdown z-order management
inline void HandleDropdownOpened(UltraCanvasDropdown* dropdown, UltraCanvasBaseWindow* window) {
    if (!dropdown || !window) return;

    auto allElements = window->GetAllElements();
    UltraCanvasZOrderManager::HandleDropdownOpened(dropdown, allElements);
    window->RequestZOrderUpdate();
}

// ===== CONTAINER Z-ORDER INTEGRATION =====

// Enhanced container base class with z-order support
class UltraCanvasZOrderAwareContainer {
protected:
    bool _zOrderDirty;
    std::vector<UltraCanvasUIElement*> sortedChildren;

    void UpdateChildrenZOrder() {
        if (!_zOrderDirty) return;

        sortedChildren.clear();

        // Collect all direct children
        auto children = GetDirectChildren();
        for (auto& child : children) {
            if (child) {
                sortedChildren.push_back(child.get());
            }
        }

        // Sort by z-index
        UltraCanvasZOrderManager::SortElementsByZOrder(sortedChildren);
        _zOrderDirty = false;

        std::cout << "Container z-order updated with " << sortedChildren.size() << " children" << std::endl;
    }

    virtual std::vector<std::shared_ptr<UltraCanvasUIElement>> GetDirectChildren() = 0;

public:
    UltraCanvasZOrderAwareContainer() : _zOrderDirty(true) {}

    virtual void RequestZOrderUpdate() {
        _zOrderDirty = true;
    }

    void RenderChildrenInZOrder() {
        if (_zOrderDirty) {
            UpdateChildrenZOrder();
        }

        // Render children in correct z-order
        for (auto* child : sortedChildren) {
            if (child && child->IsVisible()) {
                child->Render();
            }
        }
    }

    void PrintChildrenZOrder(const std::string& containerName) {
        std::cout << "=== " << containerName << " Children Z-Order ===" << std::endl;

        if (_zOrderDirty) {
            UpdateChildrenZOrder();
        }

        for (size_t i = 0; i < sortedChildren.size(); i++) {
            auto* child = sortedChildren[i];
            if (child) {
                std::string typeName = typeid(*child).name();
                if (typeName.find("UltraCanvas") != std::string::npos) {
                    size_t pos = typeName.find("UltraCanvas");
                    typeName = typeName.substr(pos);
                }

                std::cout << "  [" << i << "] Z=" << child->GetZIndex()
                          << " " << typeName
                          << " '" << child->GetIdentifier() << "'"
                          << std::endl;
            }
        }
        std::cout << "===========================================" << std::endl;
    }
};

} // namespace UltraCanvas

// ===== INTEGRATION MACROS =====

// Macro to easily add z-order support to existing windows
#define ULTRACANVAS_ENABLE_ZORDER_RENDERING() \
    void Render() override { \
        ULTRACANVAS_WINDOW_RENDER_SCOPE(this); \
        int width, height; \
        GetSize(width, height); \
        ctx->SetFillColor(config_.backgroundColor); \
        ctx->FillRectangle(Rect2D(0, 0, width, height)); \
        auto sortedElements = UltraCanvasZOrderManager::GetElementsSortedByZOrder(elements); \
        for (auto* element : sortedElements) { \
            if (element && element->IsVisible()) { \
                element->Render(); \
            } \
        } \
        _needsRedraw = false; \
    }

// Macro for z-order aware event handling
#define ULTRACANVAS_ENABLE_ZORDER_EVENTS() \
    void OnEvent(const UCEvent& event) override { \
        if (event.type == UCEventType::MouseMove || \
            event.type == UCEventType::MouseButtonDown || \
            event.type == UCEventType::MouseButtonUp) { \
            Point2D mousePos(event.mouseX, event.mouseY); \
            auto hitElements = UltraCanvasZOrderManager::GetElementsAtPoint(elements, mousePos); \
            for (auto* element : hitElements) { \
                if (element && element->IsEnabled()) { \
                    bool handled = element->OnEvent(event); \
                    if (handled) break; \
                } \
            } \
        } else { \
            for (auto* element : elements) { \
                if (element && element->IsVisible() && element->IsEnabled()) { \
                    element->OnEvent(event); \
                } \
            } \
        } \
    }

// ===== USAGE EXAMPLES =====

/*
// Example 1: Basic dropdown z-order fix
auto dropdown = std::make_shared<UltraCanvasDropdown>("MyDropdown", 1, 10, 10, 200, 30);
dropdown->SetZIndex(UltraCanvas::ZLayers::Controls);

dropdown->onDropdownOpened = [this]() {
    this->BringElementToFront(dropdown.get());
    this->RequestZOrderUpdate();
};

dropdown->onDropdownClosed = [this]() {
    this->RequestZOrderUpdate();
};

// Example 2: Container with z-order support
auto container = std::make_shared<UltraCanvasZOrderContainer>("MyContainer", 2, 0, 0, 400, 300);
container->AddChild(dropdown);
container->AddChild(otherElement);

// Example 3: Window with enhanced z-order rendering
class MyWindow : public UltraCanvasWindow {
public:
    ULTRACANVAS_ENABLE_ZORDER_RENDERING()
    ULTRACANVAS_ENABLE_ZORDER_EVENTS()
};

// Example 4: Manual z-order debugging
UltraCanvasZOrderManager::PrintZOrderHierarchy(window->GetAllElements(), "My Window Elements");
UltraCanvasZOrderManager::ValidateZOrder(window->GetAllElements());
*/

