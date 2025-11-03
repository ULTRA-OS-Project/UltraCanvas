// include/UltraCanvasLayout.h
// Abstract base class for layout managers (Qt QLayout-style)
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_LAYOUT_H
#define ULTRACANVAS_LAYOUT_H

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasLayoutItem.h"
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
    SpaceBetween = 4, // Space evenly between items
    SpaceAround = 5,  // Space around items
    SpaceEvenly = 6   // Equal space between and around
};

// ===== ABSTRACT BASE LAYOUT CLASS =====
class UltraCanvasLayout {
protected:
    // Parent container that owns this layout
    UltraCanvasContainer* parentContainer = nullptr;
    
    // Spacing between items
    int spacing = 0;
    
    // Padding around the layout
    int paddingLeft = 0;
    int paddingRight = 0;
    int paddingTop = 0;
    int paddingBottom = 0;
    
    // Margins around the layout
    int marginLeft = 0;
    int marginRight = 0;
    int marginTop = 0;
    int marginBottom = 0;
    
    // Layout state
    bool layoutDirty = true;
    
public:
    UltraCanvasLayout() = default;
    explicit UltraCanvasLayout(UltraCanvasContainer* parent);
    virtual ~UltraCanvasLayout() = default;
    
    // Prevent copying (layouts are owned by containers)
    UltraCanvasLayout(const UltraCanvasLayout&) = delete;
    UltraCanvasLayout& operator=(const UltraCanvasLayout&) = delete;
    
    // ===== PARENT CONTAINER =====
    UltraCanvasContainer* GetParentContainer() const { return parentContainer; }
    void SetParentContainer(UltraCanvasContainer* parent) { parentContainer = parent; }
    
    // ===== SPACING =====
    void SetSpacing(int space) { 
        spacing = space;
        Invalidate();
    }
    int GetSpacing() const { return spacing; }
    
    // ===== PADDING =====
    void SetPadding(int padding) {
        paddingLeft = paddingRight = paddingTop = paddingBottom = padding;
        Invalidate();
    }
    
    void SetPadding(int horizontal, int vertical) {
        paddingLeft = paddingRight = horizontal;
        paddingTop = paddingBottom = vertical;
        Invalidate();
    }
    
    void SetPadding(int left, int top, int right, int bottom) {
        paddingLeft = left;
        paddingTop = top;
        paddingRight = right;
        paddingBottom = bottom;
        Invalidate();
    }
    
    int GetPaddingLeft() const { return paddingLeft; }
    int GetPaddingRight() const { return paddingRight; }
    int GetPaddingTop() const { return paddingTop; }
    int GetPaddingBottom() const { return paddingBottom; }
    
    int GetTotalPaddingHorizontal() const { return paddingLeft + paddingRight; }
    int GetTotalPaddingVertical() const { return paddingTop + paddingBottom; }
    
    // ===== MARGINS =====
    void SetMargin(int margin) {
        marginLeft = marginRight = marginTop = marginBottom = margin;
        Invalidate();
    }
    
    void SetMargin(int horizontal, int vertical) {
        marginLeft = marginRight = horizontal;
        marginTop = marginBottom = vertical;
        Invalidate();
    }
    
    void SetMargin(int left, int top, int right, int bottom) {
        marginLeft = left;
        marginTop = top;
        marginRight = right;
        marginBottom = bottom;
        Invalidate();
    }
    
    int GetMarginLeft() const { return marginLeft; }
    int GetMarginRight() const { return marginRight; }
    int GetMarginTop() const { return marginTop; }
    int GetMarginBottom() const { return marginBottom; }
    
    int GetTotalMarginHorizontal() const { return marginLeft + marginRight; }
    int GetTotalMarginVertical() const { return marginTop + marginBottom; }
    
    // ===== LAYOUT STATE =====
    void Invalidate();
    bool IsLayoutDirty() const { return layoutDirty; }
    
    // ===== PURE VIRTUAL INTERFACE =====
    // These MUST be implemented by derived classes
    
    // Add an item to the layout (derived classes manage their own item collections)
    virtual void AddChildItem(std::shared_ptr<UltraCanvasLayoutItem> item) = 0;
    
    // Add an element directly (creates default layout item and adds to container)
    virtual void AddChildElement(std::shared_ptr<UltraCanvasUIElement> element) = 0;
    
    // Remove item from layout
    virtual void RemoveChildItem(std::shared_ptr<UltraCanvasLayoutItem> item) = 0;
    
    // Remove element from layout
    virtual void RemoveChildElement(std::shared_ptr<UltraCanvasUIElement> element) = 0;
    
    // Get number of items in layout
    virtual int GetItemCount() const = 0;
    
    // Get item at index
    virtual std::shared_ptr<UltraCanvasLayoutItem> GetItemAt(int index) const = 0;
    
    // Clear all items from layout
    virtual void ClearItems() = 0;
    
    // Perform the layout calculation and apply to items
    virtual void PerformLayout(const Rect2Di& containerBounds) = 0;
    
    // Calculate the minimum size required by this layout
    virtual Size2Di CalculateMinimumSize() const = 0;
    
    // Calculate the preferred size for this layout
    virtual Size2Di CalculatePreferredSize() const = 0;
    
    // Calculate the maximum size for this layout
    virtual Size2Di CalculateMaximumSize() const = 0;
    
    // Update layout if dirty
    void Update(const Rect2Di& containerBounds) {
        if (layoutDirty) {
            PerformLayout(containerBounds);
            layoutDirty = false;
        }
    }
    
protected:
    // Helper: Get available space after padding and margins
    Rect2Di GetContentRect(const Rect2Di& containerBounds) const {
        return Rect2Di(
            containerBounds.x + marginLeft + paddingLeft,
            containerBounds.y + marginTop + paddingTop,
            containerBounds.width - GetTotalMarginHorizontal() - GetTotalPaddingHorizontal(),
            containerBounds.height - GetTotalMarginVertical() - GetTotalPaddingVertical()
        );
    }
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_LAYOUT_H
