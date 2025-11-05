// include/UltraCanvasGridLayout.h
// Grid layout manager similar to Qt QGridLayout
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_GRID_LAYOUT_H
#define ULTRACANVAS_GRID_LAYOUT_H

#include "UltraCanvasLayout.h"
#include "UltraCanvasLayoutItem.h"
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
    float size = 0;           // Value depends on sizeMode (pixels, percent, or weight)
    float minSize = 0;        // Minimum size in pixels
    float maxSize = 10000;    // Maximum size in pixels
    
    GridRowColumnDefinition() = default;
    GridRowColumnDefinition(GridSizeMode mode, float value = 0)
        : sizeMode(mode), size(value) {}
    
    static GridRowColumnDefinition Fixed(float pixels) {
        return GridRowColumnDefinition(GridSizeMode::Fixed, pixels);
    }
    
    static GridRowColumnDefinition Auto() {
        return GridRowColumnDefinition(GridSizeMode::Auto, 0);
    }
    
    static GridRowColumnDefinition Percent(float percent) {
        return GridRowColumnDefinition(GridSizeMode::Percent, percent);
    }
    
    static GridRowColumnDefinition Star(float weight = 1.0f) {
        return GridRowColumnDefinition(GridSizeMode::Star, weight);
    }
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
    mutable std::vector<float> computedRowHeights;
    mutable std::vector<float> computedColumnWidths;
    
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
        Invalidate();
    }
    
    void AddColumnDefinition(const GridRowColumnDefinition& def) {
        columnDefinitions.push_back(def);
        Invalidate();
    }
    
    void SetRowDefinition(int row, const GridRowColumnDefinition& def) {
        if (row >= 0 && row < static_cast<int>(rowDefinitions.size())) {
            rowDefinitions[row] = def;
            Invalidate();
        }
    }
    
    void SetColumnDefinition(int column, const GridRowColumnDefinition& def) {
        if (column >= 0 && column < static_cast<int>(columnDefinitions.size())) {
            columnDefinitions[column] = def;
            Invalidate();
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
        Invalidate();
    }
    
    void SetDefaultVerticalAlignment(LayoutAlignment align) {
        defaultVerticalAlignment = align;
        Invalidate();
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
    void PerformLayout(const Rect2Di& containerBounds) override;
    Size2Di CalculateMinimumSize() const override;
    Size2Di CalculatePreferredSize() const override;
    Size2Di CalculateMaximumSize() const override;
    
private:
    // Internal layout helpers
    void CalculateRowHeights(float availableHeight);
    void CalculateColumnWidths(float availableWidth);
    
    // Position items in grid
    void PositionItems();
    
    // Get cell bounds
    Rect2Df GetCellBounds(int row, int column, int rowSpan, int columnSpan) const;
    
    // Ensure grid is large enough for item
    void EnsureGridSize(int row, int column, int rowSpan, int columnSpan);
    
    // Calculate size based on mode
    float CalculateSize(const GridRowColumnDefinition& def, float availableSpace, 
                       float contentSize) const;
    
    // Get total size of fixed and percent sized rows/columns
    float GetFixedAndPercentSize(const std::vector<GridRowColumnDefinition>& definitions,
                                 float availableSpace) const;
    
    // Get total weight of star-sized rows/columns
    float GetTotalStarWeight(const std::vector<GridRowColumnDefinition>& definitions) const;
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
