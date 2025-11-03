// include/UltraCanvasBoxLayout.h
// Box layout manager (horizontal/vertical) similar to Qt QBoxLayout
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BOX_LAYOUT_H
#define ULTRACANVAS_BOX_LAYOUT_H

#include "UltraCanvasLayout.h"
#include "UltraCanvasLayoutItem.h"
#include <vector>
#include <memory>

namespace UltraCanvas {

// ===== BOX LAYOUT DIRECTION =====
enum class BoxLayoutDirection {
    Horizontal = 0,  // Left to right
    Vertical = 1     // Top to bottom
};

// ===== BOX LAYOUT CLASS =====
class UltraCanvasBoxLayout : public UltraCanvasLayout {
private:
    // Layout direction
    BoxLayoutDirection direction = BoxLayoutDirection::Vertical;
    
    // Items in this layout
    std::vector<std::shared_ptr<UltraCanvasBoxLayoutItem>> items;
    
    // Alignment of items along the cross axis
    LayoutAlignment crossAxisAlignment = LayoutAlignment::Start;
    
    // Alignment along main axis
    LayoutAlignment mainAxisAlignment = LayoutAlignment::Start;
    
public:
    UltraCanvasBoxLayout() = default;
    explicit UltraCanvasBoxLayout(BoxLayoutDirection dir);
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
    void AddChildItem(std::shared_ptr<UltraCanvasLayoutItem> item) override;
    void AddChildElement(std::shared_ptr<UltraCanvasUIElement> element) override;
    
    void RemoveChildItem(std::shared_ptr<UltraCanvasLayoutItem> item) override;
    void RemoveChildElement(std::shared_ptr<UltraCanvasUIElement> element) override;
    
    int GetItemCount() const override { return static_cast<int>(items.size()); }
    std::shared_ptr<UltraCanvasLayoutItem> GetItemAt(int index) const override;
    
    void ClearItems() override;
    
    // ===== BOX LAYOUT SPECIFIC =====
    // Add item with stretch factor
    void AddItem(std::shared_ptr<UltraCanvasBoxLayoutItem> item, int stretch = 0);
    
    // Add element with stretch factor
    void AddElement(std::shared_ptr<UltraCanvasUIElement> element, int stretch = 0);
    
    // Add spacing (non-stretchable space)
    void AddSpacing(int size);
    
    // Add stretch (stretchable space)
    void AddStretch(int stretch = 1);
    
    // Insert item at index
    void InsertItem(int index, std::shared_ptr<UltraCanvasBoxLayoutItem> item, int stretch = 0);
    
    // Get all items
    const std::vector<std::shared_ptr<UltraCanvasBoxLayoutItem>>& GetItems() const { return items; }
    
    // ===== LAYOUT CALCULATION =====
    void PerformLayout(const Rect2Di& containerBounds) override;
    Size2Di CalculateMinimumSize() const override;
    Size2Di CalculatePreferredSize() const override;
    Size2Di CalculateMaximumSize() const override;
    
private:
    // Internal layout helpers
    void LayoutHorizontal(const Rect2Di& contentRect);
    void LayoutVertical(const Rect2Di& contentRect);
    
    // Calculate total stretch factor
    int CalculateTotalStretch() const;
    
    // Calculate total fixed size along main axis
    float CalculateTotalFixedSize() const;
    
    // Calculate total spacing
    int CalculateTotalSpacing() const;
    
    // Apply cross-axis alignment to item
    void ApplyCrossAxisAlignment(UltraCanvasBoxLayoutItem* item, float crossStart, float crossSize);
};

// ===== CONVENIENCE FACTORY FUNCTIONS =====
inline std::unique_ptr<UltraCanvasBoxLayout> CreateHBoxLayout(UltraCanvasContainer* parent = nullptr) {
    return std::make_unique<UltraCanvasBoxLayout>(parent, BoxLayoutDirection::Horizontal);
}

inline std::unique_ptr<UltraCanvasBoxLayout> CreateVBoxLayout(UltraCanvasContainer* parent = nullptr) {
    return std::make_unique<UltraCanvasBoxLayout>(parent, BoxLayoutDirection::Vertical);
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_BOX_LAYOUT_H
