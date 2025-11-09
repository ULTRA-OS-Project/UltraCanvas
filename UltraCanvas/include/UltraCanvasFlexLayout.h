// include/UltraCanvasFlexLayout.h
// Flexible layout manager similar to CSS Flexbox
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_FLEX_LAYOUT_H
#define ULTRACANVAS_FLEX_LAYOUT_H

#include "UltraCanvasLayout.h"
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

class UltraCanvasFlexLayoutItem : public UltraCanvasLayoutItem {
private:
    // Flex properties
    float flexGrow = 0;      // How much to grow (0 = don't grow)
    float flexShrink = 1;    // How much to shrink (1 = normal shrinking)
    int flexBasis = 0;     // Base size before growing/shrinking (0 = auto)

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

    // Alignment
    LayoutItemAlignment alignSelf = LayoutItemAlignment::Auto;  // Override container alignment for this item

public:
    UltraCanvasFlexLayoutItem() = default;
    explicit UltraCanvasFlexLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem);

    // ===== FLEX PROPERTIES =====
    UltraCanvasFlexLayoutItem* SetFlexGrow(float grow) { flexGrow = grow; return this; }
    UltraCanvasFlexLayoutItem* SetFlexShrink(float shrink) { flexShrink = shrink; return this; }
    UltraCanvasFlexLayoutItem* SetFlexBasis(int basis) { flexBasis = basis; return this; }
    UltraCanvasFlexLayoutItem* SetFlex(float grow, float shrink, int basis) {
        flexGrow = grow;
        flexShrink = shrink;
        flexBasis = basis;
        return this;
    }

    float GetFlexGrow() const { return flexGrow; }
    float GetFlexShrink() const { return flexShrink; }
    int GetFlexBasis() const { return flexBasis; }

    // ===== SIZE MODE =====
    SizeMode GetWidthMode() const override { return widthMode; }
    SizeMode GetHeightMode() const override { return heightMode; }

    UltraCanvasFlexLayoutItem* SetWidthMode(SizeMode mode) { widthMode = mode; return this; }
    UltraCanvasFlexLayoutItem* SetHeightMode(SizeMode mode) { heightMode = mode; return this; }
    UltraCanvasFlexLayoutItem* SetSizeMode(SizeMode width, SizeMode height) {
        widthMode = width;
        heightMode = height;
        return this;
    }

    // ===== FIXED SIZES =====
    UltraCanvasFlexLayoutItem* SetFixedWidth(int width) {
        widthMode = SizeMode::Fixed;
        fixedWidth = width;
        return this;
    }

    UltraCanvasFlexLayoutItem* SetFixedHeight(int height) {
        heightMode = SizeMode::Fixed;
        fixedHeight = height;
        return this;
    }

    UltraCanvasFlexLayoutItem* SetFixedSize(int width, int height) {
        SetFixedWidth(width);
        SetFixedHeight(height);
        return this;
    }

    int GetFixedWidth() const { return fixedWidth; }
    int GetFixedHeight() const { return fixedHeight; }

    // ===== SIZE LIMITS =====
    UltraCanvasFlexLayoutItem* SetMinimumWidth(int width) { minWidth = width; return this; }
    UltraCanvasFlexLayoutItem* SetMinimumHeight(int height) { minHeight = height; return this; }
    UltraCanvasFlexLayoutItem* SetMinimumSize(int width, int height) {
        minWidth = width;
        minHeight = height;
        return this;
    }

    UltraCanvasFlexLayoutItem* SetMaximumWidth(int width) { maxWidth = width; return this; }
    UltraCanvasFlexLayoutItem* SetMaximumHeight(int height) { maxHeight = height; return this; }
    UltraCanvasFlexLayoutItem* SetMaximumSize(int width, int height) {
        maxWidth = width;
        maxHeight = height;
        return this;
    }

    int GetMinimumWidth() const override { return minWidth != -1 ? minWidth : UltraCanvasLayoutItem::GetMinimumWidth(); }
    int GetMinimumHeight() const override { return minHeight!= -1 ? minHeight : UltraCanvasLayoutItem::GetMinimumHeight();  }
    int GetMaximumWidth() const override { return maxWidth != -1 ? maxWidth : UltraCanvasLayoutItem::GetMaximumWidth();  }
    int GetMaximumHeight() const override { return maxHeight != -1 ? maxHeight : UltraCanvasLayoutItem::GetMaximumHeight();  }

    // ===== ALIGNMENT =====
    UltraCanvasFlexLayoutItem* SetAlignSelf(LayoutItemAlignment align) { alignSelf = align; return this; }
    LayoutItemAlignment GetAlignSelf() const { return alignSelf; }

    // ===== PREFERRED SIZE =====
    int GetPreferredWidth() const override;
    int GetPreferredHeight() const override;
};

// ===== FLEX LAYOUT CLASS =====
class UltraCanvasFlexLayout : public UltraCanvasLayout {
private:
    // Items in this layout
    std::vector<std::unique_ptr<UltraCanvasFlexLayoutItem>> items;
    
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
    UltraCanvasFlexLayout() = delete;
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
    UltraCanvasLayoutItem* InsertUIElement(std::shared_ptr<UltraCanvasUIElement> element, int index) override;
    void RemoveUIElement(std::shared_ptr<UltraCanvasUIElement> element) override;
    int GetItemCount() const override { return static_cast<int>(items.size()); }
    void ClearItems() override;

    // ===== BOX LAYOUT SPECIFIC =====
    // Add item with stretch factor
    UltraCanvasFlexLayoutItem* GetItemAt(int index) const;
    UltraCanvasFlexLayoutItem* GetItemForUIElement(std::shared_ptr<UltraCanvasUIElement> elem) const;

    // Add element with stretch factor
    UltraCanvasFlexLayoutItem* AddUIElement(std::shared_ptr<UltraCanvasUIElement> element, float flexGrow = 0, float flexShrink = 0, float flexBasis = 0);

    // Get all items
    const std::vector<std::unique_ptr<UltraCanvasFlexLayoutItem>>& GetItems() const { return items; }
    
    // ===== LAYOUT CALCULATION =====
    void PerformLayout() override;
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
    int GetItemMainSize(UltraCanvasFlexLayoutItem* item) const;
    
    // Get cross axis size of item
    int GetItemCrossSize(UltraCanvasFlexLayoutItem* item) const;
    
    // Calculate flex lines (for wrapping)
    std::vector<FlexLine> CalculateFlexLines(int containerMainSize) const;
    
    // Resolve flexible lengths (flex-grow/flex-shrink)
    void ResolveFlexibleLengths(FlexLine& line, int containerMainSize);
    
    // Position items along main axis
    void PositionMainAxis(FlexLine& line, int containerMainSize);
    
    // Position items along cross axis
    void PositionCrossAxis(FlexLine& line, int containerCrossSize);
    
    // Position all lines
    void PositionLines(std::vector<FlexLine>& lines, int containerCrossSize);
};

// ===== CONVENIENCE FACTORY FUNCTION =====
inline UltraCanvasFlexLayout* CreateFlexLayout(
    UltraCanvasContainer* parent = nullptr,
    FlexDirection direction = FlexDirection::Row) {
    return new UltraCanvasFlexLayout(parent, direction);
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_FLEX_LAYOUT_H
