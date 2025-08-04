// UltraCanvasTableView.h
// Interactive table view component with sorting, filtering, and selection capabilities
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <sstream>

namespace UltraCanvas {

// ===== TABLE CELL DATA =====
struct TableCell {
    std::string text;
    Color textColor = Colors::Black;
    Color backgroundColor = Colors::Transparent;
    bool editable = false;
    void* userData = nullptr;
    
    TableCell() = default;
    TableCell(const std::string& cellText) : text(cellText) {}
    TableCell(const std::string& cellText, const Color& textCol, const Color& bgCol = Colors::Transparent)
        : text(cellText), textColor(textCol), backgroundColor(bgCol) {}
};

// ===== TABLE COLUMN DEFINITION =====
struct TableColumn {
    std::string title;
    int width = 100;
    int minWidth = 50;
    int maxWidth = 500;
    bool resizable = true;
    bool sortable = true;
    bool visible = true;
    enum class Alignment { Left, Center, Right } alignment = Alignment::Left;
    
    TableColumn() = default;
    TableColumn(const std::string& columnTitle, int columnWidth = 100)
        : title(columnTitle), width(columnWidth) {}
};

// ===== TABLE SORT INFO =====
struct SortInfo {
    int columnIndex = -1;
    bool ascending = true;
    
    bool IsValid() const { return columnIndex >= 0; }
};

// ===== TABLE SELECTION INFO =====
struct SelectionInfo {
    int startRow = -1;
    int startCol = -1;
    int endRow = -1;
    int endCol = -1;
    
    bool IsValid() const { return startRow >= 0 && startCol >= 0; }
    bool IsSingleCell() const { return startRow == endRow && startCol == endCol; }
    
    void Clear() {
        startRow = startCol = endRow = endCol = -1;
    }
    
    bool Contains(int row, int col) const {
        if (!IsValid()) return false;
        
        int minRow = std::min(startRow, endRow);
        int maxRow = std::max(startRow, endRow);
        int minCol = std::min(startCol, endCol);
        int maxCol = std::max(startCol, endCol);
        
        return row >= minRow && row <= maxRow && col >= minCol && col <= maxCol;
    }
};

// ===== MAIN TABLE VIEW COMPONENT =====
class UltraCanvasTableView : public UltraCanvasElement {
private:
    StandardProperties properties;
    
    // Data storage
    std::vector<TableColumn> columns;
    std::vector<std::vector<TableCell>> rows;
    std::vector<int> sortedRowIndices; // For maintaining original data while showing sorted view
    
    // Visual properties
    int headerHeight = 30;
    int rowHeight = 25;
    int cellPadding = 5;
    int gridLineWidth = 1;
    
    // Colors
    Color headerBackgroundColor = Color(240, 240, 240);
    Color headerTextColor = Colors::Black;
    Color alternateRowColor = Color(248, 248, 248);
    Color selectedCellColor = Color(173, 216, 230);
    Color selectedRowColor = Color(220, 235, 255);
    Color gridLineColor = Color(200, 200, 200);
    Color focusColor = Color(100, 150, 255);
    
    // Interaction state
    SelectionInfo selection;
    SortInfo currentSort;
    bool allowMultipleSelection = true;
    bool allowRowSelection = true;
    bool allowCellSelection = true;
    bool showGridLines = true;
    bool showHeader = true;
    bool alternateRowColors = true;
    
    // Scrolling
    int scrollOffsetX = 0;
    int scrollOffsetY = 0;
    int maxScrollX = 0;
    int maxScrollY = 0;
    bool needsScrollUpdate = true;
    
    // Column resizing
    int resizingColumn = -1;
    int resizeStartX = 0;
    int resizeStartWidth = 0;
    
    // Editing
    int editingRow = -1;
    int editingCol = -1;
    std::string editingText;
    bool isEditing = false;
    
    // Filtering
    std::string filterText;
    std::vector<int> filteredRowIndices;
    bool hasFilter = false;
    
public:
    // ===== EVENTS =====
    std::function<void(int, int)> onCellClicked;                    // (row, col)
    std::function<void(int, int)> onCellDoubleClicked;              // (row, col)
    std::function<void(int)> onRowSelected;                         // (row)
    std::function<void(int)> onColumnHeaderClicked;                 // (col)
    std::function<void(int, bool)> onColumnSorted;                  // (col, ascending)
    std::function<void(int, int, const std::string&)> onCellEdited; // (row, col, newValue)
    std::function<void(int)> onRowAdded;                            // (rowIndex)
    std::function<void(int)> onRowRemoved;                          // (rowIndex)
    
    // ===== CONSTRUCTOR =====
    UltraCanvasTableView(const std::string& identifier = "TableView", long id = 0,
                        long x = 0, long y = 0, long w = 400, long h = 300)
        : UltraCanvasElement(identifier, id, x, y, w, h), properties(identifier, id, x, y, w, h) {
        
        properties.MousePtr = MousePointer::Default;
        properties.MouseCtrl = MouseControls::Object2D;
        
        UpdateScrollBounds();
    }

    // ===== COLUMN MANAGEMENT =====
    void AddColumn(const TableColumn& column) {
        columns.push_back(column);
        UpdateScrollBounds();
    }
    
    void AddColumn(const std::string& title, int width = 100) {
        AddColumn(TableColumn(title, width));
    }
    
    void InsertColumn(int index, const TableColumn& column) {
        if (index >= 0 && index <= static_cast<int>(columns.size())) {
            columns.insert(columns.begin() + index, column);
            
            // Insert empty cells in all rows
            for (auto& row : rows) {
                if (index < static_cast<int>(row.size())) {
                    row.insert(row.begin() + index, TableCell());
                }
            }
            
            UpdateScrollBounds();
        }
    }
    
    void RemoveColumn(int index) {
        if (index >= 0 && index < static_cast<int>(columns.size())) {
            columns.erase(columns.begin() + index);
            
            // Remove cells from all rows
            for (auto& row : rows) {
                if (index < static_cast<int>(row.size())) {
                    row.erase(row.begin() + index);
                }
            }
            
            UpdateScrollBounds();
        }
    }
    
    void SetColumnWidth(int index, int width) {
        if (index >= 0 && index < static_cast<int>(columns.size())) {
            columns[index].width = std::max(columns[index].minWidth, 
                                          std::min(columns[index].maxWidth, width));
            UpdateScrollBounds();
        }
    }
    
    void SetColumnTitle(int index, const std::string& title) {
        if (index >= 0 && index < static_cast<int>(columns.size())) {
            columns[index].title = title;
        }
    }
    
    // ===== ROW MANAGEMENT =====
    int AddRow() {
        std::vector<TableCell> newRow(columns.size());
        rows.push_back(newRow);
        
        int newIndex = static_cast<int>(rows.size()) - 1;
        sortedRowIndices.push_back(newIndex);
        
        UpdateScrollBounds();
        
        if (onRowAdded) onRowAdded(newIndex);
        
        return newIndex;
    }
    
    int AddRow(const std::vector<std::string>& rowData) {
        int rowIndex = AddRow();
        SetRowData(rowIndex, rowData);
        return rowIndex;
    }
    
    void InsertRow(int index, const std::vector<std::string>& rowData = {}) {
        if (index >= 0 && index <= static_cast<int>(rows.size())) {
            std::vector<TableCell> newRow(columns.size());
            
            // Fill with provided data
            for (size_t i = 0; i < rowData.size() && i < newRow.size(); i++) {
                newRow[i].text = rowData[i];
            }
            
            rows.insert(rows.begin() + index, newRow);
            
            // Update sorted indices
            for (int& idx : sortedRowIndices) {
                if (idx >= index) idx++;
            }
            sortedRowIndices.insert(sortedRowIndices.begin() + index, index);
            
            UpdateScrollBounds();
        }
    }
    
    void RemoveRow(int index) {
        if (index >= 0 && index < static_cast<int>(rows.size())) {
            rows.erase(rows.begin() + index);
            
            // Update sorted indices
            auto it = std::find(sortedRowIndices.begin(), sortedRowIndices.end(), index);
            if (it != sortedRowIndices.end()) {
                sortedRowIndices.erase(it);
            }
            
            for (int& idx : sortedRowIndices) {
                if (idx > index) idx--;
            }
            
            UpdateScrollBounds();
            
            if (onRowRemoved) onRowRemoved(index);
        }
    }
    
    void ClearRows() {
        rows.clear();
        sortedRowIndices.clear();
        selection.Clear();
        UpdateScrollBounds();
    }
    
    void SetRowData(int rowIndex, const std::vector<std::string>& rowData) {
        if (rowIndex >= 0 && rowIndex < static_cast<int>(rows.size())) {
            for (size_t col = 0; col < rowData.size() && col < rows[rowIndex].size(); col++) {
                rows[rowIndex][col].text = rowData[col];
            }
        }
    }
    
    std::vector<std::string> GetRowData(int rowIndex) const {
        std::vector<std::string> rowData;
        if (rowIndex >= 0 && rowIndex < static_cast<int>(rows.size())) {
            for (const auto& cell : rows[rowIndex]) {
                rowData.push_back(cell.text);
            }
        }
        return rowData;
    }
    
    // ===== CELL MANAGEMENT =====
    void SetCellValue(int row, int col, const std::string& value) {
        if (IsValidCell(row, col)) {
            rows[row][col].text = value;
        }
    }
    
    void SetCellValue(int row, int col, const TableCell& cell) {
        if (IsValidCell(row, col)) {
            rows[row][col] = cell;
        }
    }
    
    std::string GetCellValue(int row, int col) const {
        if (IsValidCell(row, col)) {
            return rows[row][col].text;
        }
        return "";
    }
    
    const TableCell& GetCell(int row, int col) const {
        static TableCell emptyCell;
        if (IsValidCell(row, col)) {
            return rows[row][col];
        }
        return emptyCell;
    }
    
    // ===== SELECTION MANAGEMENT =====
    void SetSelection(int startRow, int startCol, int endRow = -1, int endCol = -1) {
        selection.startRow = startRow;
        selection.startCol = startCol;
        selection.endRow = (endRow == -1) ? startRow : endRow;
        selection.endCol = (endCol == -1) ? startCol : endCol;
    }
    
    void ClearSelection() {
        selection.Clear();
    }
    
    SelectionInfo GetSelection() const {
        return selection;
    }
    
    void SelectRow(int row) {
        if (row >= 0 && row < static_cast<int>(GetDisplayRowCount())) {
            SetSelection(row, 0, row, static_cast<int>(columns.size()) - 1);
            
            if (onRowSelected) onRowSelected(GetActualRowIndex(row));
        }
    }
    
    void SelectColumn(int col) {
        if (col >= 0 && col < static_cast<int>(columns.size())) {
            SetSelection(0, col, static_cast<int>(GetDisplayRowCount()) - 1, col);
        }
    }
    
    // ===== SORTING =====
    void SortByColumn(int columnIndex, bool ascending = true) {
        if (columnIndex < 0 || columnIndex >= static_cast<int>(columns.size()) || 
            !columns[columnIndex].sortable) {
            return;
        }
        
        currentSort.columnIndex = columnIndex;
        currentSort.ascending = ascending;
        
        // Get indices to sort (either all rows or filtered rows)
        std::vector<int>& indicesToSort = hasFilter ? filteredRowIndices : sortedRowIndices;
        
        std::sort(indicesToSort.begin(), indicesToSort.end(),
            [this, columnIndex, ascending](int a, int b) {
                const std::string& valueA = rows[a][columnIndex].text;
                const std::string& valueB = rows[b][columnIndex].text;
                
                // Try numeric comparison first
                try {
                    double numA = std::stod(valueA);
                    double numB = std::stod(valueB);
                    return ascending ? (numA < numB) : (numA > numB);
                } catch (...) {
                    // Fall back to string comparison
                    return ascending ? (valueA < valueB) : (valueA > valueB);
                }
            });
        
        if (onColumnSorted) onColumnSorted(columnIndex, ascending);
    }
    
    void ClearSort() {
        currentSort.columnIndex = -1;
        
        // Reset to original order
        sortedRowIndices.clear();
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            sortedRowIndices.push_back(i);
        }
        
        ApplyFilter(); // Reapply filter if active
    }
    
    // ===== FILTERING =====
    void SetFilter(const std::string& filter) {
        filterText = filter;
        hasFilter = !filter.empty();
        ApplyFilter();
    }
    
    void ClearFilter() {
        filterText.clear();
        hasFilter = false;
        filteredRowIndices.clear();
    }
    
    // ===== SCROLLING =====
    void ScrollTo(int offsetX, int offsetY) {
        scrollOffsetX = std::max(0, std::min(offsetX, maxScrollX));
        scrollOffsetY = std::max(0, std::min(offsetY, maxScrollY));
    }
    
    void ScrollToRow(int row) {
        int targetY = row * rowHeight;
        int visibleHeight = GetHeight() - (showHeader ? headerHeight : 0);
        
        if (targetY < scrollOffsetY) {
            scrollOffsetY = targetY;
        } else if (targetY + rowHeight > scrollOffsetY + visibleHeight) {
            scrollOffsetY = targetY + rowHeight - visibleHeight;
        }
        
        scrollOffsetY = std::max(0, std::min(scrollOffsetY, maxScrollY));
    }
    
    void ScrollToColumn(int col) {
        int targetX = GetColumnOffset(col);
        int visibleWidth = GetWidth();
        
        if (targetX < scrollOffsetX) {
            scrollOffsetX = targetX;
        } else if (targetX + columns[col].width > scrollOffsetX + visibleWidth) {
            scrollOffsetX = targetX + columns[col].width - visibleWidth;
        }
        
        scrollOffsetX = std::max(0, std::min(scrollOffsetX, maxScrollX));
    }
    
    // ===== EDITING =====
    void StartEditing(int row, int col) {
        if (IsValidCell(row, col) && rows[row][col].editable) {
            editingRow = row;
            editingCol = col;
            editingText = rows[row][col].text;
            isEditing = true;
        }
    }
    
    void StopEditing(bool saveChanges = true) {
        if (isEditing) {
            if (saveChanges && IsValidCell(editingRow, editingCol)) {
                rows[editingRow][editingCol].text = editingText;
                
                if (onCellEdited) {
                    onCellEdited(editingRow, editingCol, editingText);
                }
            }
            
            isEditing = false;
            editingRow = editingCol = -1;
            editingText.clear();
        }
    }
    
    bool IsEditing() const {
        return isEditing;
    }
    
    // ===== APPEARANCE =====
    void SetColors(const Color& headerBg, const Color& alternateRow, const Color& selectedCell) {
        headerBackgroundColor = headerBg;
        alternateRowColor = alternateRow;
        selectedCellColor = selectedCell;
    }
    
    void SetRowHeight(int height) {
        rowHeight = std::max(15, height);
        UpdateScrollBounds();
    }
    
    void SetHeaderHeight(int height) {
        headerHeight = std::max(20, height);
        UpdateScrollBounds();
    }
    
    void SetShowGridLines(bool show) {
        showGridLines = show;
    }
    
    void SetShowHeader(bool show) {
        showHeader = show;
        UpdateScrollBounds();
    }
    
    void SetAlternateRowColors(bool alternate) {
        alternateRowColors = alternate;
    }
    
    // ===== BULK DATA OPERATIONS =====
    void SetTableData(const std::vector<std::string>& headers, 
                     const std::vector<std::vector<std::string>>& data) {
        // Clear existing data
        columns.clear();
        rows.clear();
        sortedRowIndices.clear();
        
        // Add columns
        for (const auto& header : headers) {
            AddColumn(header);
        }
        
        // Add rows
        for (const auto& rowData : data) {
            AddRow(rowData);
        }
        
        UpdateScrollBounds();
    }
    
    std::vector<std::vector<std::string>> GetTableData() const {
        std::vector<std::vector<std::string>> data;
        
        for (const auto& row : rows) {
            std::vector<std::string> rowData;
            for (const auto& cell : row) {
                rowData.push_back(cell.text);
            }
            data.push_back(rowData);
        }
        
        return data;
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        // Update scroll bounds if needed
        if (needsScrollUpdate) {
            UpdateScrollBounds();
            needsScrollUpdate = false;
        }
        
        // Draw background
        DrawFilledRect(GetBounds(), Colors::White, gridLineColor, 1.0f);
        
        // Set clipping to table bounds
        SetClipRect(GetBounds());
        
        // Draw header
        if (showHeader) {
            DrawHeader();
        }
        
        // Draw rows
        DrawRows();
        
        // Draw selection
        DrawSelection();
        
        // Draw resize indicator
        DrawResizeIndicator();
    }
    
    // ===== EVENT HANDLING =====
    void OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
                
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
                
            case UCEventType::MouseDoubleClick:
                HandleDoubleClick(event);
                break;
                
            case UCEventType::MouseWheel:
                HandleMouseWheel(event);
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
                
            case UCEventType::KeyChar:
                HandleKeyChar(event);
                break;
        }
    }
    
    // ===== UTILITY FUNCTIONS =====
    int GetRowCount() const {
        return static_cast<int>(rows.size());
    }
    
    int GetColumnCount() const {
        return static_cast<int>(columns.size());
    }
    
    int GetDisplayRowCount() const {
        return hasFilter ? static_cast<int>(filteredRowIndices.size()) : static_cast<int>(rows.size());
    }
    
private:
    bool IsValidCell(int row, int col) const {
        return row >= 0 && row < static_cast<int>(rows.size()) && 
               col >= 0 && col < static_cast<int>(columns.size());
    }
    
    int GetActualRowIndex(int displayRow) const {
        if (hasFilter) {
            return (displayRow >= 0 && displayRow < static_cast<int>(filteredRowIndices.size())) ?
                   filteredRowIndices[displayRow] : -1;
        } else {
            return (displayRow >= 0 && displayRow < static_cast<int>(sortedRowIndices.size())) ?
                   sortedRowIndices[displayRow] : -1;
        }
    }
    
    int GetColumnOffset(int col) const {
        int offset = 0;
        for (int i = 0; i < col && i < static_cast<int>(columns.size()); i++) {
            if (columns[i].visible) {
                offset += columns[i].width;
            }
        }
        return offset;
    }
    
    void UpdateScrollBounds() {
        // Calculate total content size
        int totalWidth = 0;
        for (const auto& col : columns) {
            if (col.visible) {
                totalWidth += col.width;
            }
        }
        
        int totalHeight = GetDisplayRowCount() * rowHeight;
        
        // Calculate scroll bounds
        maxScrollX = std::max(0, totalWidth - GetWidth());
        maxScrollY = std::max(0, totalHeight - (GetHeight() - (showHeader ? headerHeight : 0)));
        
        // Clamp current scroll offsets
        scrollOffsetX = std::max(0, std::min(scrollOffsetX, maxScrollX));
        scrollOffsetY = std::max(0, std::min(scrollOffsetY, maxScrollY));
    }
    
    void ApplyFilter() {
        filteredRowIndices.clear();
        
        if (!hasFilter) return;
        
        for (int i = 0; i < static_cast<int>(rows.size()); i++) {
            bool matches = false;
            
            // Check if any cell in the row contains the filter text
            for (const auto& cell : rows[i]) {
                if (cell.text.find(filterText) != std::string::npos) {
                    matches = true;
                    break;
                }
            }
            
            if (matches) {
                filteredRowIndices.push_back(i);
            }
        }
        
        // Apply current sort to filtered results
        if (currentSort.IsValid()) {
            SortByColumn(currentSort.columnIndex, currentSort.ascending);
        }
    }
    
    void DrawHeader() {
        if (!showHeader) return;
        
        Rect2D headerRect(GetX(), GetY(), GetWidth(), headerHeight);
        DrawFilledRect(headerRect, headerBackgroundColor, gridLineColor, 1.0f);
        
        SetTextColor(headerTextColor);
        SetFont("Arial", 11.0f);
        
        int x = GetX() - scrollOffsetX;
        
        for (int col = 0; col < static_cast<int>(columns.size()); col++) {
            if (!columns[col].visible) continue;
            
            int colWidth = columns[col].width;
            Rect2D colRect(x, GetY(), colWidth, headerHeight);
            
            // Draw column background
            if (currentSort.columnIndex == col) {
                Color sortedColor = headerBackgroundColor;
                sortedColor.r = std::max(0, static_cast<int>(sortedColor.r) - 20);
                sortedColor.g = std::max(0, static_cast<int>(sortedColor.g) - 20);
                sortedColor.b = std::max(0, static_cast<int>(sortedColor.b) - 20);
                DrawFilledRect(colRect, sortedColor);
            }
            
            // Draw column text
            Point2D textPos(x + cellPadding, GetY() + (headerHeight + 11) / 2);
            DrawText(columns[col].title, textPos);
            
            // Draw sort indicator
            if (currentSort.columnIndex == col) {
                DrawSortIndicator(x + colWidth - 15, GetY() + headerHeight / 2, currentSort.ascending);
            }
            
            // Draw column separator
            if (showGridLines) {
                SetStrokeColor(gridLineColor);
                DrawLine(Point2D(x + colWidth, GetY()), Point2D(x + colWidth, GetY() + headerHeight));
            }
            
            x += colWidth;
        }
    }
    
    void DrawRows() {
        int startY = GetY() + (showHeader ? headerHeight : 0);
        int visibleHeight = GetHeight() - (showHeader ? headerHeight : 0);
        
        // Calculate visible row range
        int firstVisibleRow = scrollOffsetY / rowHeight;
        int lastVisibleRow = std::min(static_cast<int>(GetDisplayRowCount()),
                                     firstVisibleRow + (visibleHeight / rowHeight) + 2);
        
        SetFont("Arial", 10.0f);
        
        for (int displayRow = firstVisibleRow; displayRow < lastVisibleRow; displayRow++) {
            int actualRow = GetActualRowIndex(displayRow);
            if (actualRow < 0) continue;
            
            int y = startY + displayRow * rowHeight - scrollOffsetY;
            
            // Draw row background
            Color rowColor = Colors::White;
            if (alternateRowColors && displayRow % 2 == 1) {
                rowColor = alternateRowColor;
            }
            
            Rect2D rowRect(GetX(), y, GetWidth(), rowHeight);
            DrawFilledRect(rowRect, rowColor);
            
            // Draw cells
            DrawRowCells(actualRow, y);
            
            // Draw horizontal grid line
            if (showGridLines) {
                SetStrokeColor(gridLineColor);
                DrawLine(Point2D(GetX(), y + rowHeight), Point2D(GetX() + GetWidth(), y + rowHeight));
            }
        }
    }
    
    void DrawRowCells(int row, int y) {
        int x = GetX() - scrollOffsetX;
        
        for (int col = 0; col < static_cast<int>(columns.size()); col++) {
            if (!columns[col].visible) continue;
            
            int colWidth = columns[col].width;
            const TableCell& cell = rows[row][col];
            
            // Draw cell background
            if (cell.backgroundColor.a > 0) {
                Rect2D cellRect(x, y, colWidth, rowHeight);
                DrawFilledRect(cellRect, cell.backgroundColor);
            }
            
            // Draw cell text
            SetTextColor(cell.textColor);
            
            Rect2D textRect(x + cellPadding, y, colWidth - cellPadding * 2, rowHeight);
            Point2D textPos(x + cellPadding, y + (rowHeight + 10) / 2);
            
            // Clip text to cell bounds
            SetClipRect(textRect);
            
            if (isEditing && editingRow == row && editingCol == col) {
                // Draw editing text
                DrawText(editingText + "|", textPos); // Simple cursor
            } else {
                DrawText(cell.text, textPos);
            }
            
            // Reset clipping
            SetClipRect(GetBounds());
            
            // Draw vertical grid line
            if (showGridLines) {
                SetStrokeColor(gridLineColor);
                DrawLine(Point2D(x + colWidth, y), Point2D(x + colWidth, y + rowHeight));
            }
            
            x += colWidth;
        }
    }
    
    void DrawSelection() {
        if (!selection.IsValid()) return;
        
        int startY = GetY() + (showHeader ? headerHeight : 0);
        
        int minRow = std::min(selection.startRow, selection.endRow);
        int maxRow = std::max(selection.startRow, selection.endRow);
        int minCol = std::min(selection.startCol, selection.endCol);
        int maxCol = std::max(selection.startCol, selection.endCol);
        
        for (int row = minRow; row <= maxRow; row++) {
            for (int col = minCol; col <= maxCol; col++) {
                if (!IsValidCell(row, col) || !columns[col].visible) continue;
                
                int x = GetX() + GetColumnOffset(col) - scrollOffsetX;
                int y = startY + row * rowHeight - scrollOffsetY;
                
                Rect2D cellRect(x, y, columns[col].width, rowHeight);
                
                // Only draw if visible
                if (cellRect.x + cellRect.width > GetX() && cellRect.x < GetX() + GetWidth() &&
                    cellRect.y + cellRect.height > startY && cellRect.y < GetY() + GetHeight()) {
                    
                    DrawFilledRect(cellRect, selectedCellColor, focusColor, 2.0f);
                }
            }
        }
    }
    
    void DrawSortIndicator(int x, int y, bool ascending) {
        SetStrokeColor(headerTextColor);
        SetStrokeWidth(1.0f);
        
        int size = 4;
        if (ascending) {
            // Up arrow
            DrawLine(Point2D(x - size, y + size), Point2D(x, y - size));
            DrawLine(Point2D(x, y - size), Point2D(x + size, y + size));
        } else {
            // Down arrow
            DrawLine(Point2D(x - size, y - size), Point2D(x, y + size));
            DrawLine(Point2D(x, y + size), Point2D(x + size, y - size));
        }
    }
    
    void DrawResizeIndicator() {
        if (resizingColumn >= 0) {
            SetStrokeColor(focusColor);
            SetStrokeWidth(2.0f);
            
            int x = GetX() + GetColumnOffset(resizingColumn + 1) - scrollOffsetX;
            DrawLine(Point2D(x, GetY()), Point2D(x, GetY() + GetHeight()));
        }
    }
    
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        // Check for column resize
        if (CheckColumnResize(event.x, event.y)) return;
        
        // Check header click
        if (showHeader && event.y < GetY() + headerHeight) {
            HandleHeaderClick(event.x);
            return;
        }
        
        // Check cell click
        HandleCellClick(event.x, event.y);
    }
    
    void HandleMouseMove(const UCEvent& event) {
        if (resizingColumn >= 0) {
            // Handle column resize
            int deltaX = event.x - resizeStartX;
            int newWidth = resizeStartWidth + deltaX;
            SetColumnWidth(resizingColumn, newWidth);
        }
    }
    
    void HandleMouseUp(const UCEvent& event) {
        resizingColumn = -1;
    }
    
    void HandleDoubleClick(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        auto cellPos = GetCellFromPosition(event.x, event.y);
        if (cellPos.first >= 0 && cellPos.second >= 0) {
            StartEditing(cellPos.first, cellPos.second);
            
            if (onCellDoubleClicked) {
                onCellDoubleClicked(cellPos.first, cellPos.second);
            }
        }
    }
    
    void HandleMouseWheel(const UCEvent& event) {
        int scrollAmount = event.delta * 3; // 3 rows per wheel notch
        ScrollTo(scrollOffsetX, scrollOffsetY - scrollAmount * rowHeight);
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (isEditing) {
            switch (event.virtualKey) {
                case UCKeys::Return:
                    StopEditing(true);
                    break;
                case UCKeys::Escape:
                    StopEditing(false);
                    break;
                case UCKeys::Backspace:
                    if (!editingText.empty()) {
                        editingText.pop_back();
                    }
                    break;
            }
        } else {
            // Navigation
            HandleNavigationKeys(event);
        }
    }
    
    void HandleKeyChar(const UCEvent& event) {
        if (isEditing && event.character >= 32 && event.character < 127) {
            editingText += static_cast<char>(event.character);
        }
    }
    
    void HandleNavigationKeys(const UCEvent& event) {
        if (!selection.IsValid()) return;
        
        int newRow = selection.startRow;
        int newCol = selection.startCol;
        
        switch (event.virtualKey) {
            case UCKeys::Up:
                newRow = std::max(0, newRow - 1);
                break;
            case UCKeys::Down:
                newRow = std::min(static_cast<int>(GetDisplayRowCount()) - 1, newRow + 1);
                break;
            case UCKeys::Left:
                newCol = std::max(0, newCol - 1);
                break;
            case UCKeys::Right:
                newCol = std::min(static_cast<int>(columns.size()) - 1, newCol + 1);
                break;
            case UCKeys::Home:
                newCol = 0;
                break;
            case UCKeys::End:
                newCol = static_cast<int>(columns.size()) - 1;
                break;
        }
        
        if (newRow != selection.startRow || newCol != selection.startCol) {
            SetSelection(newRow, newCol);
            ScrollToRow(newRow);
            ScrollToColumn(newCol);
        }
    }
    
    bool CheckColumnResize(int x, int y) {
        if (!showHeader || y > GetY() + headerHeight) return false;
        
        int currentX = GetX() - scrollOffsetX;
        
        for (int col = 0; col < static_cast<int>(columns.size()); col++) {
            if (!columns[col].visible) continue;
            
            currentX += columns[col].width;
            
            // Check if mouse is near column boundary
            if (std::abs(x - currentX) <= 3 && columns[col].resizable) {
                resizingColumn = col;
                resizeStartX = x;
                resizeStartWidth = columns[col].width;
                return true;
            }
        }
        
        return false;
    }
    
    void HandleHeaderClick(int x) {
        int currentX = GetX() - scrollOffsetX;
        
        for (int col = 0; col < static_cast<int>(columns.size()); col++) {
            if (!columns[col].visible) continue;
            
            if (x >= currentX && x < currentX + columns[col].width) {
                if (onColumnHeaderClicked) {
                    onColumnHeaderClicked(col);
                }
                
                // Toggle sort
                if (columns[col].sortable) {
                    bool ascending = (currentSort.columnIndex != col) || !currentSort.ascending;
                    SortByColumn(col, ascending);
                }
                
                break;
            }
            
            currentX += columns[col].width;
        }
    }
    
    void HandleCellClick(int x, int y) {
        auto cellPos = GetCellFromPosition(x, y);
        if (cellPos.first >= 0 && cellPos.second >= 0) {
            SetSelection(cellPos.first, cellPos.second);
            
            if (onCellClicked) {
                onCellClicked(GetActualRowIndex(cellPos.first), cellPos.second);
            }
        }
    }
    
    std::pair<int, int> GetCellFromPosition(int x, int y) {
        // Convert to display row/col
        int startY = GetY() + (showHeader ? headerHeight : 0);
        int row = (y - startY + scrollOffsetY) / rowHeight;
        
        int currentX = GetX() - scrollOffsetX;
        int col = -1;
        
        for (int c = 0; c < static_cast<int>(columns.size()); c++) {
            if (!columns[c].visible) continue;
            
            if (x >= currentX && x < currentX + columns[c].width) {
                col = c;
                break;
            }
            
            currentX += columns[c].width;
        }
        
        if (row >= 0 && row < static_cast<int>(GetDisplayRowCount()) && col >= 0) {
            return std::make_pair(row, col);
        }
        
        return std::make_pair(-1, -1);
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasTableView> CreateTableView(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasTableView>(id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasTableView> CreateTableView(
    const std::string& identifier, long id, const Rect2D& bounds) {
    return CreateTableView(identifier, id, static_cast<long>(bounds.x), static_cast<long>(bounds.y),
                          static_cast<long>(bounds.width), static_cast<long>(bounds.height));
}

// ===== CONVENIENCE FUNCTIONS =====
inline std::shared_ptr<UltraCanvasTableView> CreateTableWithData(
    const std::string& identifier, long id, long x, long y, long w, long h,
    const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& data) {
    auto table = CreateTableView(identifier, id, x, y, w, h);
    table->SetTableData(headers, data);
    return table;
}

// ===== LEGACY C-STYLE INTERFACE =====
extern "C" {
    static UltraCanvasTableView* g_currentTableView = nullptr;
    
    void CreateTableView(int x, int y, int width, int height, const char** headers, int columnCount) {
        g_currentTableView = new UltraCanvasTableView("legacy_table", 9997, x, y, width, height);
        
        if (headers && columnCount > 0) {
            for (int i = 0; i < columnCount; i++) {
                if (headers[i]) {
                    g_currentTableView->AddColumn(headers[i]);
                }
            }
        }
    }
    
    void CreateTableView(int x, int y, int width, int height) {
        CreateTableView(x, y, width, height, nullptr, 0);
    }
    
    void AddTableRow(const char** rowData, int columnCount) {
        if (g_currentTableView && rowData && columnCount > 0) {
            std::vector<std::string> data;
            for (int i = 0; i < columnCount; i++) {
                data.push_back(rowData[i] ? rowData[i] : "");
            }
            g_currentTableView->AddRow(data);
        }
    }
    
    void SetTableCell(int row, int col, const char* value) {
        if (g_currentTableView && value) {
            g_currentTableView->SetCellValue(row, col, value);
        }
    }
    
    const char* GetTableCell(int row, int col) {
        static std::string cellValue;
        if (g_currentTableView) {
            cellValue = g_currentTableView->GetCellValue(row, col);
            return cellValue.c_str();
        }
        return "";
    }
    
    void ClearTable() {
        if (g_currentTableView) {
            g_currentTableView->ClearRows();
        }
    }
    
    int GetTableRowCount() {
        return g_currentTableView ? g_currentTableView->GetRowCount() : 0;
    }
    
    int GetTableColumnCount() {
        return g_currentTableView ? g_currentTableView->GetColumnCount() : 0;
    }
    
    void SortTableByColumn(int column, int ascending) {
        if (g_currentTableView) {
            g_currentTableView->SortByColumn(column, ascending != 0);
        }
    }
}

} // namespace UltraCanvas