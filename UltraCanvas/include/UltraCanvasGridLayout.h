// include/UltraCanvasGridLayout.h
// Grid layout manager similar to Qt QGridLayout
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_GRID_LAYOUT_H
#define ULTRACANVAS_GRID_LAYOUT_H

#include "UltraCanvasLayout.h"
#include <vector>
#include <memory>
#include <map>

namespace UltraCanvas {

// ===== GRID SIZE MODE =====
enum class GridSizeMode {
    Fixed = 0,      // Fixed size in pixels
    Auto = 1,       // Size based on content
    Percent = 2,    // Percentage of available space
    Star = 3        // Proportional sizing (remaining space distributed by weight)
};

// ===== ROW/COLUMN DEFINITION =====
struct GridRowColumnDefinition {
    GridSizeMode sizeMode = GridSizeMode::Auto;
    int size = 0;           // Value depends on sizeMode (pixels, percent, or weight)
    int minSize = 0;        // Minimum size in pixels
    int maxSize = 10000;    // Maximum size in pixels
    
    GridRowColumnDefinition() = default;
    GridRowColumnDefinition(GridSizeMode mode, int value = 0)
        : sizeMode(mode), size(value) {}
    
    static GridRowColumnDefinition Fixed(int pixels) {
        return GridRowColumnDefinition(GridSizeMode::Fixed, pixels);
    }
    
    static GridRowColumnDefinition Auto() {
        return GridRowColumnDefinition(GridSizeMode::Auto, 0);
    }
    
    static GridRowColumnDefinition Percent(int percent) {
        return GridRowColumnDefinition(GridSizeMode::Percent, percent);
    }
    
    static GridRowColumnDefinition Star(int weight = 1) {
        return GridRowColumnDefinition(GridSizeMode::Star, weight);
    }
};


// ===== GRID LAYOUT ITEM =====
class UltraCanvasGridLayoutItem : public UltraCanvasLayoutItem {
private:
    // Grid position
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;

    // Size constraints
    SizeMode widthMode = SizeMode::Fill;  // Grid items default to Fill
    SizeMode heightMode = SizeMode::Fill;
    int fixedWidth = 0;
    int fixedHeight = 0;

    // Size limits
    int minWidth = -1;
    int minHeight = -1;
    int maxWidth = -1;
    int maxHeight = -1;

    // Alignment within cell
    LayoutAlignment horizontalAlignment = LayoutAlignment::Fill;
    LayoutAlignment verticalAlignment = LayoutAlignment::Fill;

public:
    UltraCanvasGridLayoutItem() = default;
    explicit UltraCanvasGridLayoutItem(std::shared_ptr<UltraCanvasUIElement> elem);

    // ===== GRID POSITION =====
    UltraCanvasGridLayoutItem* SetRow(int r) { row = r; return this;}
    UltraCanvasGridLayoutItem* SetColumn(int c) { column = c; return this;}
    UltraCanvasGridLayoutItem* SetPosition(int r, int c) {
        row = r;
        column = c;
        return this;
    }

    int GetRow() const { return row; }
    int GetColumn() const { return column; }

    // ===== SPANNING =====
    UltraCanvasGridLayoutItem* SetRowSpan(int span) { rowSpan = span; return this;}
    UltraCanvasGridLayoutItem* SetColumnSpan(int span) { columnSpan = span; return this;}
    UltraCanvasGridLayoutItem* SetSpan(int rowSpan, int colSpan) {
        this->rowSpan = rowSpan;
        this->columnSpan = colSpan;
        return this;
    }

    int GetRowSpan() const { return rowSpan; }
    int GetColumnSpan() const { return columnSpan; }

    // ===== SIZE MODE =====
    SizeMode GetWidthMode() const override { return widthMode; }
    SizeMode GetHeightMode() const override { return heightMode; }

    UltraCanvasGridLayoutItem* SetWidthMode(SizeMode mode) { widthMode = mode; return this; }
    UltraCanvasGridLayoutItem* SetHeightMode(SizeMode mode) { heightMode = mode; return this; }
    UltraCanvasGridLayoutItem* SetSizeMode(SizeMode width, SizeMode height) {
        widthMode = width;
        heightMode = height;
        return this;
    }

    // ===== FIXED SIZES =====
    UltraCanvasGridLayoutItem* SetFixedWidth(int width) {
        widthMode = SizeMode::Fixed;
        fixedWidth = width;
        return this;
    }

    UltraCanvasGridLayoutItem* SetFixedHeight(int height) {
        heightMode = SizeMode::Fixed;
        fixedHeight = height;
        return this;
    }

    UltraCanvasGridLayoutItem* SetFixedSize(int width, int height) {
        SetFixedWidth(width);
        SetFixedHeight(height);
        return this;
    }

    int GetFixedWidth() const { return fixedWidth; }
    int GetFixedHeight() const { return fixedHeight; }

    // ===== SIZE LIMITS =====
    UltraCanvasGridLayoutItem* SetMinimumWidth(int width) { minWidth = width; return this;}
    UltraCanvasGridLayoutItem* SetMinimumHeight(int height) { minHeight = height; return this;}
    UltraCanvasGridLayoutItem* SetMinimumSize(int width, int height) {
        minWidth = width;
        minHeight = height;
        return this;
    }

    UltraCanvasGridLayoutItem* SetMaximumWidth(int width) { maxWidth = width; return this; }
    UltraCanvasGridLayoutItem* SetMaximumHeight(int height) { maxHeight = height; return this; }
    UltraCanvasGridLayoutItem* SetMaximumSize(int width, int height) {
        maxWidth = width;
        maxHeight = height;
        return this;
    }

    int GetMinimumWidth() const override { return minWidth != -1 ? minWidth : UltraCanvasLayoutItem::GetMinimumWidth(); }
    int GetMinimumHeight() const override { return minHeight!= -1 ? minHeight : UltraCanvasLayoutItem::GetMinimumHeight();  }
    int GetMaximumWidth() const override { return maxWidth != -1 ? maxWidth : UltraCanvasLayoutItem::GetMaximumWidth();  }
    int GetMaximumHeight() const override { return maxHeight != -1 ? maxHeight : UltraCanvasLayoutItem::GetMaximumHeight();  }

    // ===== ALIGNMENT =====
    void SetHorizontalAlignment(LayoutAlignment align) { horizontalAlignment = align; }
    void SetVerticalAlignment(LayoutAlignment align) { verticalAlignment = align; }
    void SetAlignment(LayoutAlignment horizontal, LayoutAlignment vertical) {
        horizontalAlignment = horizontal;
        verticalAlignment = vertical;
    }

    LayoutAlignment GetHorizontalAlignment() const { return horizontalAlignment; }
    LayoutAlignment GetVerticalAlignment() const { return verticalAlignment; }

    // ===== PREFERRED SIZE =====
    int GetPreferredWidth() const override;
    int GetPreferredHeight() const override;
};

// ===== GRID LAYOUT CLASS =====
class UltraCanvasGridLayout : public UltraCanvasLayout {
private:
    // Items in this layout
    std::vector<std::unique_ptr<UltraCanvasGridLayoutItem>> items;
    
    // Row and column definitions
    std::vector<GridRowColumnDefinition> rowDefinitions;
    std::vector<GridRowColumnDefinition> columnDefinitions;
    
    // Computed row/column sizes (calculated during layout)
    mutable std::vector<int> computedRowHeights;
    mutable std::vector<int> computedColumnWidths;
    
    // Default alignment for items
    LayoutAlignment defaultHorizontalAlignment = LayoutAlignment::Fill;
    LayoutAlignment defaultVerticalAlignment = LayoutAlignment::Fill;
    
public:
    UltraCanvasGridLayout() = delete;
    explicit UltraCanvasGridLayout(UltraCanvasContainer* parent, int rows = 1, int columns = 1);
    virtual ~UltraCanvasGridLayout() = default;
    
    // ===== ROW/COLUMN DEFINITIONS =====
    void AddRowDefinition(const GridRowColumnDefinition& def) {
        rowDefinitions.push_back(def);
        InvalidateContainerLayout();
    }
    
    void AddColumnDefinition(const GridRowColumnDefinition& def) {
        columnDefinitions.push_back(def);
        InvalidateContainerLayout();
    }
    
    void SetRowDefinition(int row, const GridRowColumnDefinition& def) {
        if (row >= 0 && row < static_cast<int>(rowDefinitions.size())) {
            rowDefinitions[row] = def;
            InvalidateContainerLayout();
        }
    }
    
    void SetColumnDefinition(int column, const GridRowColumnDefinition& def) {
        if (column >= 0 && column < static_cast<int>(columnDefinitions.size())) {
            columnDefinitions[column] = def;
            InvalidateContainerLayout();
        }
    }
    
    const std::vector<GridRowColumnDefinition>& GetRowDefinitions() const { return rowDefinitions; }
    const std::vector<GridRowColumnDefinition>& GetColumnDefinitions() const { return columnDefinitions; }
    
    // ===== GRID SIZE =====
    void SetGridSize(int rows, int columns);
    int GetRowCount() const { return static_cast<int>(rowDefinitions.size()); }
    int GetColumnCount() const { return static_cast<int>(columnDefinitions.size()); }
    
    // ===== DEFAULT ALIGNMENT =====
    void SetDefaultHorizontalAlignment(LayoutAlignment align) {
        defaultHorizontalAlignment = align;
        InvalidateContainerLayout();
    }
    
    void SetDefaultVerticalAlignment(LayoutAlignment align) {
        defaultVerticalAlignment = align;
        InvalidateContainerLayout();
    }
    
    LayoutAlignment GetDefaultHorizontalAlignment() const { return defaultHorizontalAlignment; }
    LayoutAlignment GetDefaultVerticalAlignment() const { return defaultVerticalAlignment; }

    // ===== GRID ITEMS =====
    UltraCanvasLayoutItem* InsertUIElement(std::shared_ptr<UltraCanvasUIElement> element, int index) override;
    void RemoveUIElement(std::shared_ptr<UltraCanvasUIElement> element) override;
    int GetItemCount() const override { return static_cast<int>(items.size()); }
    void ClearItems() override;

    UltraCanvasGridLayoutItem* GetItemForUIElement(std::shared_ptr<UltraCanvasUIElement> element) const;
    UltraCanvasGridLayoutItem* GetItemAt(int index) const;
    // Get item at grid position
    UltraCanvasGridLayoutItem* GetItemAt(int row, int column) const;


    // Add element at specific grid position
    UltraCanvasGridLayoutItem* AddUIElement(std::shared_ptr<UltraCanvasUIElement> element, int row, int column,
                    int rowSpan = 1, int columnSpan = 1);

    // Get all items
    const std::vector<std::unique_ptr<UltraCanvasGridLayoutItem>>& GetItems() const { return items; }
    
    // ===== LAYOUT CALCULATION =====
    void PerformLayout() override;
    Size2Di CalculateMinimumSize() const override;
    Size2Di CalculatePreferredSize() const override;
    Size2Di CalculateMaximumSize() const override;
    
private:
    // Internal layout helpers
    void CalculateRowHeights(int availableHeight);
    void CalculateColumnWidths(int availableWidth);
    
    // Position items in grid
    void PositionItems();
    
    // Get cell bounds
    Rect2Di GetCellBounds(int row, int column, int rowSpan, int columnSpan) const;
    
    // Ensure grid is large enough for item
    void EnsureGridSize(int row, int column, int rowSpan, int columnSpan);
    
    // Calculate size based on mode
    int CalculateSize(const GridRowColumnDefinition& def, int availableSpace,
                       int contentSize) const;
    
    // Get total size of fixed and percent sized rows/columns
    int GetFixedAndPercentSize(const std::vector<GridRowColumnDefinition>& definitions,
                                 int availableSpace) const;
    
    // Get total weight of star-sized rows/columns
    int GetTotalStarWeight(const std::vector<GridRowColumnDefinition>& definitions) const;
};

// ===== CONVENIENCE FACTORY FUNCTION =====
inline UltraCanvasGridLayout* CreateGridLayout(
    UltraCanvasContainer* parent = nullptr, 
    int rows = 0, 
    int columns = 0) {
    return new UltraCanvasGridLayout(parent, rows, columns);
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_GRID_LAYOUT_H
