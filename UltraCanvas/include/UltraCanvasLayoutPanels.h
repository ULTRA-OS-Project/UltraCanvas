// UltraCanvasLayoutPanels.h
// Enhanced layout panel components with advanced grid and container functionality
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLayoutEngine.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

namespace UltraCanvas {

// ===== GRID PANEL DEFINITIONS =====
enum class GridResizeMode {
    Fixed,          // Fixed cell sizes
    Proportional,   // Cells resize proportionally
    AutoFit,        // Cells auto-fit content
    Uniform,        // All cells same size
    Custom          // Custom resize behavior
};

enum class GridAlignment {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, MiddleCenter, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
    Stretch
};

struct GridCellInfo {
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
    GridAlignment alignment = GridAlignment::Stretch;
    float minWidth = 0.0f;
    float minHeight = 0.0f;
    float maxWidth = -1.0f;  // -1 = unlimited
    float maxHeight = -1.0f; // -1 = unlimited
    float padding = 4.0f;
    bool isVisible = true;
    
    GridCellInfo(int r = 0, int c = 0, int rs = 1, int cs = 1)
        : row(r), column(c), rowSpan(rs), columnSpan(cs) {}
};

struct GridColumnDefinition {
    float width = 100.0f;     // Fixed width or star weight
    float minWidth = 0.0f;
    float maxWidth = -1.0f;   // -1 = unlimited
    bool isStar = false;      // True for star sizing (proportional)
    bool isAuto = false;      // True for auto sizing (fit content)
    
    static GridColumnDefinition Fixed(float width) {
        GridColumnDefinition def;
        def.width = width;
        def.isStar = false;
        def.isAuto = false;
        return def;
    }
    
    static GridColumnDefinition Star(float weight = 1.0f) {
        GridColumnDefinition def;
        def.width = weight;
        def.isStar = true;
        def.isAuto = false;
        return def;
    }
    
    static GridColumnDefinition Auto() {
        GridColumnDefinition def;
        def.width = 0.0f;
        def.isStar = false;
        def.isAuto = true;
        return def;
    }
};

struct GridRowDefinition {
    float height = 30.0f;     // Fixed height or star weight
    float minHeight = 0.0f;
    float maxHeight = -1.0f;  // -1 = unlimited
    bool isStar = false;      // True for star sizing (proportional)
    bool isAuto = false;      // True for auto sizing (fit content)
    
    static GridRowDefinition Fixed(float height) {
        GridRowDefinition def;
        def.height = height;
        def.isStar = false;
        def.isAuto = false;
        return def;
    }
    
    static GridRowDefinition Star(float weight = 1.0f) {
        GridRowDefinition def;
        def.height = weight;
        def.isStar = true;
        def.isAuto = false;
        return def;
    }
    
    static GridRowDefinition Auto() {
        GridRowDefinition def;
        def.height = 0.0f;
        def.isStar = false;
        def.isAuto = true;
        return def;
    }
};

// ===== ADVANCED GRID PANEL COMPONENT =====
class UltraCanvasGridPanel : public UltraCanvasContainer {
private:
    // ===== GRID DEFINITIONS =====
    std::vector<GridColumnDefinition> columnDefinitions;
    std::vector<GridRowDefinition> rowDefinitions;
    
    // ===== GRID ELEMENTS AND LAYOUT =====
    struct GridElement {
        std::shared_ptr<UltraCanvasElement> element;
        GridCellInfo cellInfo;
        Rect2D calculatedBounds;
    };
    
    std::vector<GridElement> gridElements;
    
    // ===== GRID PROPERTIES =====
    GridResizeMode resizeMode = GridResizeMode::Proportional;
    float cellSpacing = 2.0f;
    float cellPadding = 4.0f;
    bool showGridLines = false;
    Color gridLineColor = Color(200, 200, 200);
    float gridLineWidth = 1.0f;
    
    // ===== LAYOUT CACHE =====
    std::vector<float> calculatedColumnWidths;
    std::vector<float> calculatedRowHeights;
    bool gridLayoutDirty = true;
    
public:
    // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
    UltraCanvasGridPanel(const std::string& identifier = "GridPanel", 
                        long id = 0, long x = 0, long y = 0, long w = 400, long h = 300)
        : UltraCanvasContainer(identifier, id, x, y, w, h) {
        
        // Initialize with default 3x3 grid
        for (int i = 0; i < 3; i++) {
            columnDefinitions.push_back(GridColumnDefinition::Star(1.0f));
            rowDefinitions.push_back(GridRowDefinition::Star(1.0f));
        }
    }
    
    // ===== GRID DEFINITION MANAGEMENT =====
    void SetColumnDefinitions(const std::vector<GridColumnDefinition>& columns) {
        columnDefinitions = columns;
        gridLayoutDirty = true;
    }
    
    void SetRowDefinitions(const std::vector<GridRowDefinition>& rows) {
        rowDefinitions = rows;
        gridLayoutDirty = true;
    }
    
    void AddColumnDefinition(const GridColumnDefinition& column) {
        columnDefinitions.push_back(column);
        gridLayoutDirty = true;
    }
    
    void AddRowDefinition(const GridRowDefinition& row) {
        rowDefinitions.push_back(row);
        gridLayoutDirty = true;
    }
    
    void InsertColumn(int index, const GridColumnDefinition& column) {
        if (index >= 0 && index <= static_cast<int>(columnDefinitions.size())) {
            columnDefinitions.insert(columnDefinitions.begin() + index, column);
            gridLayoutDirty = true;
        }
    }
    
    void InsertRow(int index, const GridRowDefinition& row) {
        if (index >= 0 && index <= static_cast<int>(rowDefinitions.size())) {
            rowDefinitions.insert(rowDefinitions.begin() + index, row);
            gridLayoutDirty = true;
        }
    }
    
    void RemoveColumn(int index) {
        if (index >= 0 && index < static_cast<int>(columnDefinitions.size())) {
            columnDefinitions.erase(columnDefinitions.begin() + index);
            gridLayoutDirty = true;
        }
    }
    
    void RemoveRow(int index) {
        if (index >= 0 && index < static_cast<int>(rowDefinitions.size())) {
            rowDefinitions.erase(rowDefinitions.begin() + index);
            gridLayoutDirty = true;
        }
    }
    
    const std::vector<GridColumnDefinition>& GetColumnDefinitions() const { return columnDefinitions; }
    const std::vector<GridRowDefinition>& GetRowDefinitions() const { return rowDefinitions; }
    
    // ===== ELEMENT MANAGEMENT =====
    void AddElement(std::shared_ptr<UltraCanvasElement> element, int row, int column, 
                   int rowSpan = 1, int columnSpan = 1) {
        if (!element) return;
        
        GridElement gridElem;
        gridElem.element = element;
        gridElem.cellInfo = GridCellInfo(row, column, rowSpan, columnSpan);
        
        gridElements.push_back(gridElem);
        UltraCanvasContainer::AddChild(element);
        
        gridLayoutDirty = true;
    }
    
    void AddElement(std::shared_ptr<UltraCanvasElement> element, const GridCellInfo& cellInfo) {
        if (!element) return;
        
        GridElement gridElem;
        gridElem.element = element;
        gridElem.cellInfo = cellInfo;
        
        gridElements.push_back(gridElem);
        UltraCanvasContainer::AddChild(element);
        
        gridLayoutDirty = true;
    }
    
    void RemoveElement(std::shared_ptr<UltraCanvasElement> element) {
        auto it = std::find_if(gridElements.begin(), gridElements.end(),
                              [&element](const GridElement& ge) { return ge.element == element; });
        if (it != gridElements.end()) {
            gridElements.erase(it);
            UltraCanvasContainer::RemoveChild(element);
            gridLayoutDirty = true;
        }
    }
    
    void SetElementCellInfo(std::shared_ptr<UltraCanvasElement> element, const GridCellInfo& cellInfo) {
        auto it = std::find_if(gridElements.begin(), gridElements.end(),
                              [&element](GridElement& ge) { return ge.element == element; });
        if (it != gridElements.end()) {
            it->cellInfo = cellInfo;
            gridLayoutDirty = true;
        }
    }
    
    GridCellInfo GetElementCellInfo(std::shared_ptr<UltraCanvasElement> element) const {
        auto it = std::find_if(gridElements.begin(), gridElements.end(),
                              [&element](const GridElement& ge) { return ge.element == element; });
        return (it != gridElements.end()) ? it->cellInfo : GridCellInfo();
    }
    
    std::shared_ptr<UltraCanvasElement> GetElementAt(int row, int column) const {
        for (const auto& gridElem : gridElements) {
            const auto& info = gridElem.cellInfo;
            if (row >= info.row && row < info.row + info.rowSpan &&
                column >= info.column && column < info.column + info.columnSpan) {
                return gridElem.element;
            }
        }
        return nullptr;
    }
    
    // ===== GRID PROPERTIES =====
    void SetResizeMode(GridResizeMode mode) {
        resizeMode = mode;
        gridLayoutDirty = true;
    }
    
    GridResizeMode GetResizeMode() const { return resizeMode; }
    
    void SetCellSpacing(float spacing) {
        cellSpacing = spacing;
        gridLayoutDirty = true;
    }
    
    float GetCellSpacing() const { return cellSpacing; }
    
    void SetCellPadding(float padding) {
        cellPadding = padding;
        gridLayoutDirty = true;
    }
    
    float GetCellPadding() const { return cellPadding; }
    
    void SetShowGridLines(bool show) {
        showGridLines = show;
    }
    
    bool GetShowGridLines() const { return showGridLines; }
    
    void SetGridLineColor(const Color& color) {
        gridLineColor = color;
    }
    
    const Color& GetGridLineColor() const { return gridLineColor; }
    
    // ===== GRID INFORMATION =====
    int GetColumnCount() const { return static_cast<int>(columnDefinitions.size()); }
    int GetRowCount() const { return static_cast<int>(rowDefinitions.size()); }
    int GetElementCount() const { return static_cast<int>(gridElements.size()); }
    
    Point2D GetGridSize() const {
        return Point2D(static_cast<float>(GetColumnCount()), static_cast<float>(GetRowCount()));
    }
    
    Rect2D GetCellBounds(int row, int column) const {
        if (row < 0 || row >= GetRowCount() || column < 0 || column >= GetColumnCount()) {
            return Rect2D();
        }
        
        // Calculate cell bounds based on current layout
        float x = GetX() + cellPadding;
        float y = GetY() + cellPadding;
        
        // Add column widths up to the target column
        for (int c = 0; c < column; c++) {
            if (c < static_cast<int>(calculatedColumnWidths.size())) {
                x += calculatedColumnWidths[c] + cellSpacing;
            }
        }
        
        // Add row heights up to the target row
        for (int r = 0; r < row; r++) {
            if (r < static_cast<int>(calculatedRowHeights.size())) {
                y += calculatedRowHeights[r] + cellSpacing;
            }
        }
        
        float width = (column < static_cast<int>(calculatedColumnWidths.size())) ? 
                     calculatedColumnWidths[column] : 100.0f;
        float height = (row < static_cast<int>(calculatedRowHeights.size())) ? 
                      calculatedRowHeights[row] : 30.0f;
        
        return Rect2D(x, y, width, height);
    }
    
    Point2D GetCellAtPosition(const Point2D& position) const {
        Rect2D bounds = GetBounds();
        if (!bounds.Contains(position)) {
            return Point2D(-1, -1); // Outside grid
        }
        
        float relativeX = position.x - bounds.x - cellPadding;
        float relativeY = position.y - bounds.y - cellPadding;
        
        int column = 0;
        float currentX = 0;
        while (column < GetColumnCount() && currentX < relativeX) {
            if (column < static_cast<int>(calculatedColumnWidths.size())) {
                currentX += calculatedColumnWidths[column] + cellSpacing;
            }
            if (currentX <= relativeX) column++;
        }
        
        int row = 0;
        float currentY = 0;
        while (row < GetRowCount() && currentY < relativeY) {
            if (row < static_cast<int>(calculatedRowHeights.size())) {
                currentY += calculatedRowHeights[row] + cellSpacing;
            }
            if (currentY <= relativeY) row++;
        }
        
        return Point2D(static_cast<float>(std::max(0, column - 1)), 
                      static_cast<float>(std::max(0, row - 1)));
    }
    
    // ===== LAYOUT OVERRIDE =====
    void PerformLayout() override {
        if (gridLayoutDirty) {
            CalculateGridLayout();
            gridLayoutDirty = false;
        }
        
        // Position elements according to calculated grid
        PositionGridElements();
    }
    
    // ===== RENDERING OVERRIDE =====
    void Render() override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Perform layout if needed
        PerformLayout();
        
        // Draw background
        DrawBackground();
        
        // Draw grid lines if enabled
        if (showGridLines) {
            DrawGridLines();
        }
        
        // Render children (grid elements)
        RenderChildren();
    }
    
    // ===== EVENT HANDLING OVERRIDE =====
    bool OnEvent(const UCEvent& event) override {
        UltraCanvasContainer::OnEvent(event);
        
        // Add grid-specific event handling
        if (event.type == UCEventType::MouseDown) {
            Point2D cellPos = GetCellAtPosition(Point2D(event.x, event.y));
            if (cellPos.x >= 0 && cellPos.y >= 0) {
                if (onCellClicked) {
                    onCellClicked(static_cast<int>(cellPos.y), static_cast<int>(cellPos.x));
                }
            }
        }
        return false;
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void(int row, int column)> onCellClicked;
    std::function<void(std::shared_ptr<UltraCanvasElement>, int row, int column)> onElementAdded;
    std::function<void(std::shared_ptr<UltraCanvasElement>)> onElementRemoved;
    std::function<void()> onGridLayoutChanged;

private:
    // ===== LAYOUT CALCULATION =====
    void CalculateGridLayout() {
        Rect2D contentArea = GetContentArea();
        
        CalculateColumnWidths(contentArea.width);
        CalculateRowHeights(contentArea.height);
    }
    
    void CalculateColumnWidths(float availableWidth) {
        calculatedColumnWidths.clear();
        
        if (columnDefinitions.empty()) {
            return;
        }
        
        // Subtract spacing
        float totalSpacing = (columnDefinitions.size() - 1) * cellSpacing + cellPadding * 2;
        float usableWidth = availableWidth - totalSpacing;
        
        // Calculate fixed and auto widths first
        float fixedWidth = 0.0f;
        float totalStarWeight = 0.0f;
        std::vector<float> columnWidths(columnDefinitions.size());
        
        for (size_t i = 0; i < columnDefinitions.size(); i++) {
            const auto& colDef = columnDefinitions[i];
            
            if (!colDef.isStar && !colDef.isAuto) {
                // Fixed width
                columnWidths[i] = colDef.width;
                fixedWidth += colDef.width;
            } else if (colDef.isAuto) {
                // Auto width - calculate based on content
                columnWidths[i] = CalculateAutoColumnWidth(static_cast<int>(i));
                fixedWidth += columnWidths[i];
            } else {
                // Star width
                totalStarWeight += colDef.width;
                columnWidths[i] = 0; // Will be calculated later
            }
        }
        
        // Distribute remaining width among star columns
        float remainingWidth = std::max(0.0f, usableWidth - fixedWidth);
        
        for (size_t i = 0; i < columnDefinitions.size(); i++) {
            const auto& colDef = columnDefinitions[i];
            
            if (colDef.isStar && totalStarWeight > 0) {
                columnWidths[i] = (colDef.width / totalStarWeight) * remainingWidth;
            }
            
            // Apply min/max constraints
            if (colDef.minWidth > 0) {
                columnWidths[i] = std::max(columnWidths[i], colDef.minWidth);
            }
            if (colDef.maxWidth > 0) {
                columnWidths[i] = std::min(columnWidths[i], colDef.maxWidth);
            }
        }
        
        calculatedColumnWidths = columnWidths;
    }
    
    void CalculateRowHeights(float availableHeight) {
        calculatedRowHeights.clear();
        
        if (rowDefinitions.empty()) {
            return;
        }
        
        // Subtract spacing
        float totalSpacing = (rowDefinitions.size() - 1) * cellSpacing + cellPadding * 2;
        float usableHeight = availableHeight - totalSpacing;
        
        // Calculate fixed and auto heights first
        float fixedHeight = 0.0f;
        float totalStarWeight = 0.0f;
        std::vector<float> rowHeights(rowDefinitions.size());
        
        for (size_t i = 0; i < rowDefinitions.size(); i++) {
            const auto& rowDef = rowDefinitions[i];
            
            if (!rowDef.isStar && !rowDef.isAuto) {
                // Fixed height
                rowHeights[i] = rowDef.height;
                fixedHeight += rowDef.height;
            } else if (rowDef.isAuto) {
                // Auto height - calculate based on content
                rowHeights[i] = CalculateAutoRowHeight(static_cast<int>(i));
                fixedHeight += rowHeights[i];
            } else {
                // Star height
                totalStarWeight += rowDef.height;
                rowHeights[i] = 0; // Will be calculated later
            }
        }
        
        // Distribute remaining height among star rows
        float remainingHeight = std::max(0.0f, usableHeight - fixedHeight);
        
        for (size_t i = 0; i < rowDefinitions.size(); i++) {
            const auto& rowDef = rowDefinitions[i];
            
            if (rowDef.isStar && totalStarWeight > 0) {
                rowHeights[i] = (rowDef.height / totalStarWeight) * remainingHeight;
            }
            
            // Apply min/max constraints
            if (rowDef.minHeight > 0) {
                rowHeights[i] = std::max(rowHeights[i], rowDef.minHeight);
            }
            if (rowDef.maxHeight > 0) {
                rowHeights[i] = std::min(rowHeights[i], rowDef.maxHeight);
            }
        }
        
        calculatedRowHeights = rowHeights;
    }
    
    float CalculateAutoColumnWidth(int column) {
        float maxWidth = 0.0f;
        
        for (const auto& gridElem : gridElements) {
            const auto& info = gridElem.cellInfo;
            if (info.column == column && info.columnSpan == 1) {
                // Get preferred width of element
                if (gridElem.element) {
                    float elementWidth = static_cast<float>(gridElem.element->GetWidth());
                    maxWidth = std::max(maxWidth, elementWidth + info.padding * 2);
                }
            }
        }
        
        return std::max(maxWidth, 50.0f); // Minimum column width
    }
    
    float CalculateAutoRowHeight(int row) {
        float maxHeight = 0.0f;
        
        for (const auto& gridElem : gridElements) {
            const auto& info = gridElem.cellInfo;
            if (info.row == row && info.rowSpan == 1) {
                // Get preferred height of element
                if (gridElem.element) {
                    float elementHeight = static_cast<float>(gridElem.element->GetHeight());
                    maxHeight = std::max(maxHeight, elementHeight + info.padding * 2);
                }
            }
        }
        
        return std::max(maxHeight, 20.0f); // Minimum row height
    }
    
    void PositionGridElements() {
        for (auto& gridElem : gridElements) {
            if (!gridElem.element) continue;
            
            const auto& info = gridElem.cellInfo;
            
            // Calculate total width and height for spanned cells
            float totalWidth = 0.0f;
            for (int c = info.column; c < info.column + info.columnSpan; c++) {
                if (c < static_cast<int>(calculatedColumnWidths.size())) {
                    totalWidth += calculatedColumnWidths[c];
                    if (c < info.column + info.columnSpan - 1) {
                        totalWidth += cellSpacing;
                    }
                }
            }
            
            float totalHeight = 0.0f;
            for (int r = info.row; r < info.row + info.rowSpan; r++) {
                if (r < static_cast<int>(calculatedRowHeights.size())) {
                    totalHeight += calculatedRowHeights[r];
                    if (r < info.row + info.rowSpan - 1) {
                        totalHeight += cellSpacing;
                    }
                }
            }
            
            // Get cell bounds
            Rect2D cellBounds = GetCellBounds(info.row, info.column);
            cellBounds.width = totalWidth;
            cellBounds.height = totalHeight;
            
            // Apply padding
            cellBounds.x += info.padding;
            cellBounds.y += info.padding;
            cellBounds.width -= info.padding * 2;
            cellBounds.height -= info.padding * 2;
            
            // Apply alignment within cell
            Rect2D elementBounds = CalculateElementBounds(gridElem.element, cellBounds, info.alignment);
            
            // Position the element
            gridElem.element->SetPosition(static_cast<long>(elementBounds.x), 
                                        static_cast<long>(elementBounds.y));
            gridElem.element->SetSize(static_cast<long>(elementBounds.width), 
                                    static_cast<long>(elementBounds.height));
            
            // Cache calculated bounds
            gridElem.calculatedBounds = elementBounds;
        }
    }
    
    Rect2D CalculateElementBounds(std::shared_ptr<UltraCanvasElement> element, 
                                 const Rect2D& cellBounds, GridAlignment alignment) {
        if (!element) return cellBounds;
        
        float elementWidth = static_cast<float>(element->GetWidth());
        float elementHeight = static_cast<float>(element->GetHeight());
        
        switch (alignment) {
            case GridAlignment::Stretch:
                return cellBounds;
                
            case GridAlignment::TopLeft:
                return Rect2D(cellBounds.x, cellBounds.y, elementWidth, elementHeight);
                
            case GridAlignment::TopCenter:
                return Rect2D(cellBounds.x + (cellBounds.width - elementWidth) / 2, 
                             cellBounds.y, elementWidth, elementHeight);
                
            case GridAlignment::TopRight:
                return Rect2D(cellBounds.x + cellBounds.width - elementWidth, 
                             cellBounds.y, elementWidth, elementHeight);
                
            case GridAlignment::MiddleLeft:
                return Rect2D(cellBounds.x, 
                             cellBounds.y + (cellBounds.height - elementHeight) / 2, 
                             elementWidth, elementHeight);
                
            case GridAlignment::MiddleCenter:
                return Rect2D(cellBounds.x + (cellBounds.width - elementWidth) / 2, 
                             cellBounds.y + (cellBounds.height - elementHeight) / 2, 
                             elementWidth, elementHeight);
                
            case GridAlignment::MiddleRight:
                return Rect2D(cellBounds.x + cellBounds.width - elementWidth, 
                             cellBounds.y + (cellBounds.height - elementHeight) / 2, 
                             elementWidth, elementHeight);
                
            case GridAlignment::BottomLeft:
                return Rect2D(cellBounds.x, 
                             cellBounds.y + cellBounds.height - elementHeight, 
                             elementWidth, elementHeight);
                
            case GridAlignment::BottomCenter:
                return Rect2D(cellBounds.x + (cellBounds.width - elementWidth) / 2, 
                             cellBounds.y + cellBounds.height - elementHeight, 
                             elementWidth, elementHeight);
                
            case GridAlignment::BottomRight:
                return Rect2D(cellBounds.x + cellBounds.width - elementWidth, 
                             cellBounds.y + cellBounds.height - elementHeight, 
                             elementWidth, elementHeight);
                
            default:
                return cellBounds;
        }
    }
    
    // ===== RENDERING HELPERS =====
    Rect2D GetContentArea() const {
        Rect2D bounds = GetBounds();
        return Rect2D(bounds.x + cellPadding, bounds.y + cellPadding,
                     bounds.width - cellPadding * 2, bounds.height - cellPadding * 2);
    }
    
    void DrawBackground() {
        ctx->SetFillColor(GetBackgroundColor());
        UltraCanvas::DrawFilledRect(GetBounds(), GetBackgroundColor(), GetBorderColor(), GetBorderWidth());
    }
    
    void DrawGridLines() {
        ctx->SetStrokeColor(gridLineColor);
        ctx->SetStrokeWidth(gridLineWidth);
        
        Rect2D contentArea = GetContentArea();
        
        // Draw vertical lines
        float currentX = contentArea.x;
        for (size_t i = 0; i <= calculatedColumnWidths.size(); i++) {
            ctx->DrawLine(Point2D(currentX, contentArea.y),
                    Point2D(currentX, contentArea.y + contentArea.height));
            
            if (i < calculatedColumnWidths.size()) {
                currentX += calculatedColumnWidths[i] + cellSpacing;
            }
        }
        
        // Draw horizontal lines
        float currentY = contentArea.y;
        for (size_t i = 0; i <= calculatedRowHeights.size(); i++) {
            ctx->DrawLine(Point2D(contentArea.x, currentY),
                    Point2D(contentArea.x + contentArea.width, currentY));
            
            if (i < calculatedRowHeights.size()) {
                currentY += calculatedRowHeights[i] + cellSpacing;
            }
        }
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasGridPanel> CreateGridPanel(
    const std::string& identifier, long id, long x, long y, long w, long h,
    int columns = 3, int rows = 3) {
    auto grid = UltraCanvasElementFactory::CreateWithID<UltraCanvasGridPanel>(
        id, identifier, id, x, y, w, h);
    
    // Initialize with specified grid size
    std::vector<GridColumnDefinition> cols;
    std::vector<GridRowDefinition> rowDefs;
    
    for (int i = 0; i < columns; i++) {
        cols.push_back(GridColumnDefinition::Star(1.0f));
    }
    for (int i = 0; i < rows; i++) {
        rowDefs.push_back(GridRowDefinition::Star(1.0f));
    }
    
    grid->SetColumnDefinitions(cols);
    grid->SetRowDefinitions(rowDefs);
    
    return grid;
}

inline std::shared_ptr<UltraCanvasGridPanel> CreateFixedGridPanel(
    const std::string& identifier, long id, long x, long y, long w, long h,
    const std::vector<float>& columnWidths, const std::vector<float>& rowHeights) {
    auto grid = UltraCanvasElementFactory::CreateWithID<UltraCanvasGridPanel>(
        id, identifier, id, x, y, w, h);
    
    std::vector<GridColumnDefinition> cols;
    std::vector<GridRowDefinition> rows;
    
    for (float width : columnWidths) {
        cols.push_back(GridColumnDefinition::Fixed(width));
    }
    for (float height : rowHeights) {
        rows.push_back(GridRowDefinition::Fixed(height));
    }
    
    grid->SetColumnDefinitions(cols);
    grid->SetRowDefinitions(rows);
    
    return grid;
}

// ===== BUILDER PATTERN =====
class GridPanelBuilder {
private:
    std::string identifier = "GridPanel";
    long id = 0;
    long x = 0, y = 0, w = 400, h = 300;
    std::vector<GridColumnDefinition> columns;
    std::vector<GridRowDefinition> rows;
    float cellSpacing = 2.0f;
    float cellPadding = 4.0f;
    bool showGridLines = false;
    Color gridLineColor = Color(200, 200, 200);
    std::function<void(int, int)> cellClickHandler;
    
public:
    GridPanelBuilder& SetIdentifier(const std::string& id) { identifier = id; return *this; }
    GridPanelBuilder& SetID(long elementId) { id = elementId; return *this; }
    GridPanelBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
    GridPanelBuilder& SetSize(long width, long height) { w = width; h = height; return *this; }
    
    GridPanelBuilder& AddColumn(const GridColumnDefinition& column) { columns.push_back(column); return *this; }
    GridPanelBuilder& AddRow(const GridRowDefinition& row) { rows.push_back(row); return *this; }
    GridPanelBuilder& AddFixedColumn(float width) { columns.push_back(GridColumnDefinition::Fixed(width)); return *this; }
    GridPanelBuilder& AddStarColumn(float weight = 1.0f) { columns.push_back(GridColumnDefinition::Star(weight)); return *this; }
    GridPanelBuilder& AddAutoColumn() { columns.push_back(GridColumnDefinition::Auto()); return *this; }
    GridPanelBuilder& AddFixedRow(float height) { rows.push_back(GridRowDefinition::Fixed(height)); return *this; }
    GridPanelBuilder& AddStarRow(float weight = 1.0f) { rows.push_back(GridRowDefinition::Star(weight)); return *this; }
    GridPanelBuilder& AddAutoRow() { rows.push_back(GridRowDefinition::Auto()); return *this; }
    
    GridPanelBuilder& SetCellSpacing(float spacing) { cellSpacing = spacing; return *this; }
    GridPanelBuilder& SetCellPadding(float padding) { cellPadding = padding; return *this; }
    GridPanelBuilder& ShowGridLines(bool show) { showGridLines = show; return *this; }
    GridPanelBuilder& SetGridLineColor(const Color& color) { gridLineColor = color; return *this; }
    GridPanelBuilder& OnCellClicked(std::function<void(int, int)> handler) { cellClickHandler = handler; return *this; }
    
    std::shared_ptr<UltraCanvasGridPanel> Build() {
        auto grid = UltraCanvasElementFactory::CreateWithID<UltraCanvasGridPanel>(
            id, identifier, id, x, y, w, h);
        
        if (!columns.empty()) grid->SetColumnDefinitions(columns);
        if (!rows.empty()) grid->SetRowDefinitions(rows);
        
        grid->SetCellSpacing(cellSpacing);
        grid->SetCellPadding(cellPadding);
        grid->SetShowGridLines(showGridLines);
        grid->SetGridLineColor(gridLineColor);
        
        if (cellClickHandler) grid->onCellClicked = cellClickHandler;
        
        return grid;
    }
};

// ===== LEGACY C-STYLE API (BACKWARD COMPATIBLE) =====
extern "C" {
    void* CreateGridPanelC(int x, int y, int width, int height, int columns, int rows);
    void AddGridElement(void* gridHandle, void* elementHandle, int row, int column);
    void SetGridCellSpacing(void* gridHandle, float spacing);
    void SetGridShowLines(void* gridHandle, bool show);
    void* GetGridElementAt(void* gridHandle, int row, int column);
    void DestroyGridPanel(void* gridHandle);
}

} // namespace UltraCanvas