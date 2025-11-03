// include/UltraCanvasFlexLayout.h
// Flexible layout manager similar to CSS Flexbox
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_FLEX_LAYOUT_H
#define ULTRACANVAS_FLEX_LAYOUT_H

#include "UltraCanvasLayout.h"
#include "UltraCanvasLayoutItem.h"
#include <vector>
#include <memory>

namespace UltraCanvas {

// ===== FLEX DIRECTION =====
enum class FlexDirection {
    Row = 0,            // Left to right
    RowReverse = 1,     // Right to left
    Column = 2,         // Top to bottom
    ColumnReverse = 3   // Bottom to top
};

// ===== FLEX WRAP =====
enum class FlexWrap {
    NoWrap = 0,      // Single line, may overflow
    Wrap = 1,        // Multiple lines, wrap forward
    WrapReverse = 2  // Multiple lines, wrap backward
};

// ===== FLEX JUSTIFY CONTENT (MAIN AXIS) =====
enum class FlexJustifyContent {
    Start = 0,         // Items at start
    End = 1,           // Items at end
    Center = 2,        // Items centered
    SpaceBetween = 3,  // Space between items
    SpaceAround = 4,   // Space around items
    SpaceEvenly = 5    // Equal space between and around
};

// ===== FLEX ALIGN ITEMS (CROSS AXIS) =====
enum class FlexAlignItems {
    Start = 0,      // Items at start of cross axis
    End = 1,        // Items at end of cross axis
    Center = 2,     // Items centered on cross axis
    Stretch = 3,    // Items stretch to fill cross axis
    Baseline = 4    // Items aligned to text baseline
};

// ===== FLEX ALIGN CONTENT (MULTIPLE LINES) =====
enum class FlexAlignContent {
    Start = 0,         // Lines at start
    End = 1,           // Lines at end
    Center = 2,        // Lines centered
    Stretch = 3,       // Lines stretch to fill
    SpaceBetween = 4,  // Space between lines
    SpaceAround = 5    // Space around lines
};

// ===== FLEX LAYOUT CLASS =====
class UltraCanvasFlexLayout : public UltraCanvasLayout {
private:
    // Items in this layout
    std::vector<std::shared_ptr<UltraCanvasFlexLayoutItem>> items;
    
    // Flex properties
    FlexDirection direction = FlexDirection::Row;
    FlexWrap wrap = FlexWrap::NoWrap;
    FlexJustifyContent justifyContent = FlexJustifyContent::Start;
    FlexAlignItems alignItems = FlexAlignItems::Stretch;
    FlexAlignContent alignContent = FlexAlignContent::Stretch;
    
    // Gap between items (CSS gap property)
    int rowGap = 0;
    int columnGap = 0;
    
public:
    UltraCanvasFlexLayout() = default;
    explicit UltraCanvasFlexLayout(FlexDirection dir);
    explicit UltraCanvasFlexLayout(UltraCanvasContainer* parent, FlexDirection dir = FlexDirection::Row);
    virtual ~UltraCanvasFlexLayout() = default;
    
    // ===== FLEX DIRECTION =====
    void SetFlexDirection(FlexDirection dir) {
        direction = dir;
        Invalidate();
    }
    FlexDirection GetFlexDirection() const { return direction; }
    
    // ===== FLEX WRAP =====
    void SetFlexWrap(FlexWrap w) {
        wrap = w;
        Invalidate();
    }
    FlexWrap GetFlexWrap() const { return wrap; }
    
    // ===== JUSTIFY CONTENT =====
    void SetJustifyContent(FlexJustifyContent justify) {
        justifyContent = justify;
        Invalidate();
    }
    FlexJustifyContent GetJustifyContent() const { return justifyContent; }
    
    // ===== ALIGN ITEMS =====
    void SetAlignItems(FlexAlignItems align) {
        alignItems = align;
        Invalidate();
    }
    FlexAlignItems GetAlignItems() const { return alignItems; }
    
    // ===== ALIGN CONTENT =====
    void SetAlignContent(FlexAlignContent align) {
        alignContent = align;
        Invalidate();
    }
    FlexAlignContent GetAlignContent() const { return alignContent; }
    
    // ===== GAP =====
    void SetGap(int gap) {
        rowGap = columnGap = gap;
        Invalidate();
    }
    
    void SetGap(int row, int column) {
        rowGap = row;
        columnGap = column;
        Invalidate();
    }
    
    void SetRowGap(int gap) {
        rowGap = gap;
        Invalidate();
    }
    
    void SetColumnGap(int gap) {
        columnGap = gap;
        Invalidate();
    }
    
    int GetRowGap() const { return rowGap; }
    int GetColumnGap() const { return columnGap; }
    
    // ===== ITEM MANAGEMENT =====
    void AddChildItem(std::shared_ptr<UltraCanvasLayoutItem> item) override;
    void AddChildElement(std::shared_ptr<UltraCanvasUIElement> element) override;
    
    void RemoveChildItem(std::shared_ptr<UltraCanvasLayoutItem> item) override;
    void RemoveChildElement(std::shared_ptr<UltraCanvasUIElement> element) override;
    
    int GetItemCount() const override { return static_cast<int>(items.size()); }
    std::shared_ptr<UltraCanvasLayoutItem> GetItemAt(int index) const override;
    
    void ClearItems() override;
    
    // ===== FLEX LAYOUT SPECIFIC =====
    // Add item with flex properties
    void AddItem(std::shared_ptr<UltraCanvasFlexLayoutItem> item);
    
    // Add element with flex properties
    void AddElement(std::shared_ptr<UltraCanvasUIElement> element, 
                    float flexGrow = 0, float flexShrink = 1, float flexBasis = 0);
    
    // Get all items
    const std::vector<std::shared_ptr<UltraCanvasFlexLayoutItem>>& GetItems() const { return items; }
    
    // ===== LAYOUT CALCULATION =====
    void PerformLayout(const Rect2Di& containerBounds) override;
    Size2Di CalculateMinimumSize() const override;
    Size2Di CalculatePreferredSize() const override;
    Size2Di CalculateMaximumSize() const override;
    
private:
    // Internal layout helpers
    struct FlexLine {
        std::vector<UltraCanvasFlexLayoutItem*> items;
        float mainSize = 0;
        float crossSize = 0;
        float mainStart = 0;
        float crossStart = 0;
    };
    
    // Determine if direction is row-based
    bool IsRowDirection() const {
        return direction == FlexDirection::Row || direction == FlexDirection::RowReverse;
    }
    
    // Determine if direction is reversed
    bool IsReversed() const {
        return direction == FlexDirection::RowReverse || direction == FlexDirection::ColumnReverse;
    }
    
    // Get main axis size of item
    float GetItemMainSize(UltraCanvasFlexLayoutItem* item) const;
    
    // Get cross axis size of item
    float GetItemCrossSize(UltraCanvasFlexLayoutItem* item) const;
    
    // Calculate flex lines (for wrapping)
    std::vector<FlexLine> CalculateFlexLines(float containerMainSize) const;
    
    // Resolve flexible lengths (flex-grow/flex-shrink)
    void ResolveFlexibleLengths(FlexLine& line, float containerMainSize);
    
    // Position items along main axis
    void PositionMainAxis(FlexLine& line, float containerMainSize);
    
    // Position items along cross axis
    void PositionCrossAxis(FlexLine& line, float containerCrossSize);
    
    // Position all lines
    void PositionLines(std::vector<FlexLine>& lines, float containerCrossSize);
};

// ===== CONVENIENCE FACTORY FUNCTION =====
inline std::unique_ptr<UltraCanvasFlexLayout> CreateFlexLayout(
    UltraCanvasContainer* parent = nullptr,
    FlexDirection direction = FlexDirection::Row) {
    return std::make_unique<UltraCanvasFlexLayout>(parent, direction);
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_FLEX_LAYOUT_H
