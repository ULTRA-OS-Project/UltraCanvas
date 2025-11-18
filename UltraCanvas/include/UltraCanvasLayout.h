// include/UltraCanvasLayout.h
// Abstract base class for layout managers (Qt QLayout-style)
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_LAYOUT_H
#define ULTRACANVAS_LAYOUT_H

#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <memory>
#include <algorithm>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasContainer;
class UltraCanvasUIElement;

// ===== LAYOUT ALIGNMENT ENUMS =====
enum class LayoutAlignment {
    Start = 0,        // Left/Top
    Center = 1,       // Center
    End = 2,          // Right/Bottom
    Fill = 3,         // Fill available space
    Auto = 4         // Auto (Flex)
};

// Forward declarations
class UltraCanvasUIElement;

// ===== SIZE MODE ENUMS =====
// Note: SizeMode is already defined in UltraCanvasCommonTypes.h
// enum class SizeMode { Fixed, Auto, Fill, Percent }

// ===== BASE LAYOUT ITEM CLASS =====
class UltraCanvasLayoutItem {
protected:
    // Element reference
    std::shared_ptr<UltraCanvasUIElement> element;

    // Computed position and size (set by layout manager)
    int computedX = 0;
    int computedY = 0;
    int computedWidth = 0;
    int computedHeight = 0;

public:
    UltraCanvasLayoutItem() = default;
    explicit UltraCanvasLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem);

    // ===== ELEMENT ACCESS =====
    std::shared_ptr<UltraCanvasUIElement> GetElement() const { return element; }

    // ===== COMPUTED GEOMETRY =====
    int GetComputedX() const { return computedX; }
    int GetComputedY() const { return computedY; }
    int GetComputedWidth() const { return computedWidth; }
    int GetComputedHeight() const { return computedHeight; }

    void SetComputedGeometry(int x, int y, int width, int height) {
        computedX = x;
        computedY = y;
        computedWidth = width;
        computedHeight = height;
    }

    Rect2Di GetComputedBounds() const {
        return Rect2Di(computedX, computedY, computedWidth, computedHeight);
    }

    int GetTotalMarginHorizontal() const;
    int GetTotalMarginVertical() const;

    // ===== VIRTUAL INTERFACE =====
    // Derived classes can override to provide layout-specific behavior
    virtual SizeMode GetWidthMode() const { return SizeMode::Auto; }
    virtual SizeMode GetHeightMode() const { return SizeMode::Auto; }
    virtual int GetPreferredWidth() const;
    virtual int GetPreferredHeight() const;
    virtual int GetMinimumWidth() const;
    virtual int GetMinimumHeight() const;
    virtual int GetMaximumWidth() const;
    virtual int GetMaximumHeight() const;

    bool IsVisible() const;
    // Apply computed geometry to the actual element

    int GetMarginTop() const;
    int GetMarginRight() const;
    int GetMarginBottom() const;
    int GetMarginLeft() const;

    virtual void ApplyToElement();
};

// ===== ABSTRACT BASE LAYOUT CLASS =====
class UltraCanvasLayout {
protected:
    // Parent container that owns this layout
    UltraCanvasContainer* parentContainer = nullptr;

    // Spacing between items
    int spacing = 0;

public:
    UltraCanvasLayout() = delete;
    explicit UltraCanvasLayout(UltraCanvasContainer* container);
    virtual ~UltraCanvasLayout() = default;
    
    // Prevent copying (layouts are owned by containers)
    UltraCanvasLayout(const UltraCanvasLayout&) = delete;
    UltraCanvasLayout& operator=(const UltraCanvasLayout&) = delete;
    
    // ===== PARENT CONTAINER =====
    UltraCanvasContainer* GetParentContainer() const { return parentContainer; }
    void SetParentContainer(UltraCanvasContainer* parent);

    // ===== SPACING =====
    void SetSpacing(int space);
    int GetSpacing() const { return spacing; }
    
    virtual UltraCanvasLayoutItem* InsertUIElement(std::shared_ptr<UltraCanvasUIElement> element, int index) = 0;
    // Remove element from layout
    virtual void RemoveUIElement(std::shared_ptr<UltraCanvasUIElement> element) = 0;

    // Get number of items in layout
    virtual int GetItemCount() const = 0;
    
//     Clear all items from layout
    virtual void ClearItems() = 0;
    
    // Perform the layout calculation and apply to items
    virtual void PerformLayout() = 0;
    
    // Calculate the minimum size required by this layout
    virtual Size2Di CalculateMinimumSize() const = 0;
    
    // Calculate the preferred size for this layout
    virtual Size2Di CalculatePreferredSize() const = 0;
    
    // Calculate the maximum size for this layout
    virtual Size2Di CalculateMaximumSize() const = 0;

    void InvalidateContainerLayout();
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_LAYOUT_H
