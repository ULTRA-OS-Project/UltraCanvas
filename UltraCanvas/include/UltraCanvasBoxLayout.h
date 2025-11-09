// include/UltraCanvasBoxLayout.h
// Box layout manager (horizontal/vertical) similar to Qt QBoxLayout
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BOX_LAYOUT_H
#define ULTRACANVAS_BOX_LAYOUT_H

#include "UltraCanvasLayout.h"
#include <vector>
#include <memory>

namespace UltraCanvas {

// ===== BOX LAYOUT DIRECTION =====
enum class BoxLayoutDirection {
    Horizontal = 0,  // Left to right
    Vertical = 1     // Top to bottom
};

class UltraCanvasBoxLayoutItem : public UltraCanvasLayoutItem {
private:
    // Size constraints
    SizeMode widthMode = SizeMode::Auto;
    SizeMode heightMode = SizeMode::Auto;
    int fixedWidth = 0;
    int fixedHeight = 0;

    // Size limits
    int minWidth = -1;
    int minHeight = -1;
    int maxWidth = -1;
    int maxHeight = -1;

    // Flex properties (for flexible sizing)
    float stretch = 0;  // How much to stretch relative to other items (0 = no stretch)

    // Alignment within allocated space
    LayoutItemAlignment alignment = LayoutItemAlignment::Start;

public:
    UltraCanvasBoxLayoutItem() = default;
    explicit UltraCanvasBoxLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem);

    // ===== SIZE MODE =====
    SizeMode GetWidthMode() const override { return widthMode; }
    SizeMode GetHeightMode() const override { return heightMode; }

    UltraCanvasBoxLayoutItem* SetWidthMode(SizeMode mode) { widthMode = mode; return this; }
    UltraCanvasBoxLayoutItem* SetHeightMode(SizeMode mode) { heightMode = mode; return this; }
    UltraCanvasBoxLayoutItem* SetSizeMode(SizeMode width, SizeMode height) {
        widthMode = width;
        heightMode = height;
        return this;
    }

    // ===== FIXED SIZES =====
    UltraCanvasBoxLayoutItem* SetFixedWidth(int width) {
        widthMode = SizeMode::Fixed;
        fixedWidth = width;
        return this;
    }

    UltraCanvasBoxLayoutItem* SetFixedHeight(int height) {
        heightMode = SizeMode::Fixed;
        fixedHeight = height;
        return this;
    }

    UltraCanvasBoxLayoutItem* SetFixedSize(int width, int height) {
        SetFixedWidth(width);
        SetFixedHeight(height);
        return this;
    }

    int GetFixedWidth() const { return fixedWidth; }
    int GetFixedHeight() const { return fixedHeight; }

    // ===== SIZE LIMITS =====
    UltraCanvasBoxLayoutItem* SetMinimumWidth(int width) { minWidth = width; return this; }
    UltraCanvasBoxLayoutItem* SetMinimumHeight(int height) { minHeight = height; return this; }
    UltraCanvasBoxLayoutItem* SetMinimumSize(int width, int height) {
        minWidth = width;
        minHeight = height;
        return this;
    }

    UltraCanvasBoxLayoutItem* SetMaximumWidth(int width) { maxWidth = width; return this; }
    UltraCanvasBoxLayoutItem* SetMaximumHeight(int height) { maxHeight = height; return this; }
    UltraCanvasBoxLayoutItem* SetMaximumSize(int width, int height) {
        maxWidth = width;
        maxHeight = height;
        return this;
    }

    int GetMinimumWidth() const override { return minWidth != -1 ? minWidth : UltraCanvasLayoutItem::GetMinimumWidth(); }
    int GetMinimumHeight() const override { return minHeight!= -1 ? minHeight : UltraCanvasLayoutItem::GetMinimumHeight();  }
    int GetMaximumWidth() const override { return maxWidth != -1 ? maxWidth : UltraCanvasLayoutItem::GetMaximumWidth();  }
    int GetMaximumHeight() const override { return maxHeight != -1 ? maxHeight : UltraCanvasLayoutItem::GetMaximumHeight();  }

    // ===== STRETCH (FLEX GROW) =====
    UltraCanvasBoxLayoutItem* SetStretch(float stretchFactor) { stretch = stretchFactor; return this; }
    float GetStretch() const { return stretch; }

    // ===== ALIGNMENT =====
    UltraCanvasBoxLayoutItem* SetAlignment(LayoutItemAlignment align) { alignment = align; return this; }
    LayoutItemAlignment GetAlignment() const { return alignment; }

    // ===== PREFERRED SIZE =====
    int GetPreferredWidth() const override;
    int GetPreferredHeight() const override;
};

// ===== BOX LAYOUT CLASS =====
class UltraCanvasBoxLayout : public UltraCanvasLayout {
private:
    // Layout direction
    BoxLayoutDirection direction = BoxLayoutDirection::Vertical;
    
    // Items in this layout
    std::vector<std::unique_ptr<UltraCanvasBoxLayoutItem>> items;
    
    // Alignment of items along the cross axis
    LayoutAlignment crossAxisAlignment = LayoutAlignment::Start;
    
    // Alignment along main axis
    LayoutAlignment mainAxisAlignment = LayoutAlignment::Start;
    
public:
    UltraCanvasBoxLayout() = delete;
    explicit UltraCanvasBoxLayout(UltraCanvasContainer* parent, BoxLayoutDirection dir = BoxLayoutDirection::Vertical);
    virtual ~UltraCanvasBoxLayout() = default;
    
    // ===== DIRECTION =====
    void SetDirection(BoxLayoutDirection dir) {
        direction = dir;
        Invalidate();
    }
    BoxLayoutDirection GetDirection() const { return direction; }
    
    // ===== ALIGNMENT =====
    void SetCrossAxisAlignment(LayoutAlignment align) {
        crossAxisAlignment = align;
        Invalidate();
    }
    LayoutAlignment GetCrossAxisAlignment() const { return crossAxisAlignment; }
    
    void SetMainAxisAlignment(LayoutAlignment align) {
        mainAxisAlignment = align;
        Invalidate();
    }
    LayoutAlignment GetMainAxisAlignment() const { return mainAxisAlignment; }
    
    // ===== ITEM MANAGEMENT =====
    UltraCanvasLayoutItem* InsertUIElement(std::shared_ptr<UltraCanvasUIElement> element, int index) override;
    void RemoveUIElement(std::shared_ptr<UltraCanvasUIElement> element) override;
    int GetItemCount() const override { return static_cast<int>(items.size()); }
    void ClearItems() override;

    // ===== BOX LAYOUT SPECIFIC =====
    // Add item with stretch factor
    UltraCanvasBoxLayoutItem* GetItemAt(int index) const;
    UltraCanvasBoxLayoutItem* GetItemForUIElement(std::shared_ptr<UltraCanvasUIElement> elem) const;

    // Add element with stretch factor
    UltraCanvasBoxLayoutItem* AddUIElement(std::shared_ptr<UltraCanvasUIElement> element, float stretch = 0);

    // Add spacing (non-stretchable space)
    void AddSpacing(int size);
    
    // Add stretch (stretchable space)
    void AddStretch(int stretch = 1);
    
    // Get all items
    const std::vector<std::unique_ptr<UltraCanvasBoxLayoutItem>>& GetItems() const { return items; }
    
    // ===== LAYOUT CALCULATION =====
    void PerformLayout() override;
    Size2Di CalculateMinimumSize() const override;
    Size2Di CalculatePreferredSize() const override;
    Size2Di CalculateMaximumSize() const override;
    
private:
    // Internal layout helpers
    void LayoutHorizontal(const Rect2Di& contentRect);
    void LayoutVertical(const Rect2Di& contentRect);
    
    // Calculate total stretch factor
    float CalculateTotalStretch() const;
    
    // Calculate total fixed size along main axis
    int CalculateTotalFixedSize() const;
    
    // Calculate total spacing
    int CalculateTotalSpacing() const;
    
    // Apply cross-axis alignment to item
    void ApplyCrossAxisAlignment(UltraCanvasBoxLayoutItem* item, int crossStart, int crossSize);
};

// ===== CONVENIENCE FACTORY FUNCTIONS =====
inline UltraCanvasBoxLayout* CreateHBoxLayout(UltraCanvasContainer* parent = nullptr) {
    return new UltraCanvasBoxLayout(parent, BoxLayoutDirection::Horizontal);
}

inline UltraCanvasBoxLayout* CreateVBoxLayout(UltraCanvasContainer* parent = nullptr) {
    return new UltraCanvasBoxLayout(parent, BoxLayoutDirection::Vertical);
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_BOX_LAYOUT_H
