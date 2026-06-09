// include/UltraCanvasSpreadsheetSheet.h
// Individual worksheet within a spreadsheet workbook
// Version: 1.0.0
// Last Modified: 2026-01-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSpreadsheetTypes.h"
#include "UltraCanvasSpreadsheetCell.h"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <algorithm>
#include <climits>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasSpreadsheet;
class SpreadsheetFormulaEngine;

// ============================================================================
// SPARSE CELL STORAGE
// ============================================================================

// Hash function for cell coordinates
struct CellCoordHash {
    size_t operator()(const std::pair<int, int>& coord) const {
        return std::hash<int>()(coord.first) ^ (std::hash<int>()(coord.second) << 16);
    }
};

// ============================================================================
// FIND/REPLACE OPTIONS
// ============================================================================

// Declared at namespace scope (not nested) so it can be used as a default
// argument with its NSDMIs inside SpreadsheetSheet's still-incomplete class body.
struct SheetFindOptions {
    bool matchCase = false;
    bool matchEntireCell = false;
    bool searchFormulas = false;
    bool useRegex = false;
    bool searchByRows = true;  // false = by columns
};

// ============================================================================
// SPREADSHEET SHEET CLASS
// ============================================================================

class SpreadsheetSheet {
public:
    // ===== CALLBACKS =====
    std::function<void(int, int)> onCellChange;             // (row, col) - cell value changed
    std::function<void(int, int)> onCellFormatChange;       // (row, col) - cell format changed
    std::function<void(const CellRange&)> onRangeChange;    // Range of cells changed
    std::function<void()> onStructureChange;                // Rows/columns inserted/deleted
    std::function<void()> onSelectionChange;                // Selection changed
    std::function<void(const std::string&)> onNameChange;   // Sheet name changed
    
private:
    // Parent workbook
    UltraCanvasSpreadsheet* parent_ = nullptr;
    
    // Sheet identification
    std::string name_ = "Sheet1";
    int index_ = 0;
    Color tabColor_ = Colors::Transparent;
    bool visible_ = true;
    bool selected_ = false;
    
    // Cell storage (sparse - only non-empty cells)
    std::unordered_map<std::pair<int, int>, std::unique_ptr<SpreadsheetCell>, CellCoordHash> cells_;
    
    // Used range tracking
    int usedRowMin_ = -1;
    int usedRowMax_ = -1;
    int usedColMin_ = -1;
    int usedColMax_ = -1;
    bool usedRangeDirty_ = true;
    
    // Column definitions (sparse - only customized columns)
    std::map<int, ColumnDefinition> columns_;
    int defaultColumnWidth_ = SpreadsheetLimits::DefaultColumnWidth;
    
    // Row definitions (sparse - only customized rows)
    std::map<int, RowDefinition> rows_;
    int defaultRowHeight_ = SpreadsheetLimits::DefaultRowHeight;
    
    // Merged cells
    std::vector<MergedCell> mergedCells_;
    
    // Freeze panes
    FreezePanes freezePanes_;
    
    // Selection state
    SelectionState selection_;
    
    // Auto filter
    std::unique_ptr<AutoFilter> autoFilter_;
    
    // Conditional formatting rules
    std::vector<ConditionalFormat> conditionalFormats_;
    
    // Data validations (range -> validation)
    std::vector<std::pair<CellRange, DataValidation>> validations_;
    
    // Named ranges (sheet-scoped)
    std::map<std::string, NamedRange> namedRanges_;
    
    // Print settings
    PrintSettings printSettings_;
    
    // Protection
    SheetProtection protection_;
    
    // Default styles
    CellStyle defaultCellStyle_;
    
    // Scroll position (for UI)
    int scrollRow_ = 0;
    int scrollCol_ = 0;
    
    // Zoom level (percentage)
    int zoomLevel_ = 100;
    
public:
    // ===== CONSTRUCTOR =====
    SpreadsheetSheet(const std::string& name = "Sheet1", int index = 0);
    ~SpreadsheetSheet() = default;
    
    // No copy (use Clone for explicit copies)
    SpreadsheetSheet(const SpreadsheetSheet&) = delete;
    SpreadsheetSheet& operator=(const SpreadsheetSheet&) = delete;
    
    // Move allowed
    SpreadsheetSheet(SpreadsheetSheet&&) = default;
    SpreadsheetSheet& operator=(SpreadsheetSheet&&) = default;
    
    // ===== PARENT ACCESS =====
    void SetParent(UltraCanvasSpreadsheet* parent) { parent_ = parent; }
    UltraCanvasSpreadsheet* GetParent() const { return parent_; }
    
    // ===== SHEET PROPERTIES =====
    const std::string& GetName() const { return name_; }
    void SetName(const std::string& name);
    
    int GetIndex() const { return index_; }
    void SetIndex(int index) { index_ = index; }
    
    const Color& GetTabColor() const { return tabColor_; }
    void SetTabColor(const Color& color) { tabColor_ = color; }
    
    bool IsVisible() const { return visible_; }
    void SetVisible(bool visible) { visible_ = visible; }
    
    bool IsSelected() const { return selected_; }
    void SetSelected(bool selected) { selected_ = selected; }
    
    // ===== CELL ACCESS =====
    
    // Get cell (creates if doesn't exist)
    SpreadsheetCell* GetCell(int row, int col);
    SpreadsheetCell* GetCell(const CellAddress& addr);
    
    // Get cell (returns nullptr if doesn't exist)
    const SpreadsheetCell* GetCellIfExists(int row, int col) const;
    SpreadsheetCell* GetCellIfExists(int row, int col);
    const SpreadsheetCell* GetCellIfExists(const CellAddress& addr) const;
    
    // Check if cell exists
    bool HasCell(int row, int col) const;
    bool HasCell(const CellAddress& addr) const { return HasCell(addr.row, addr.col); }
    
    // Get or create cell
    SpreadsheetCell& GetOrCreateCell(int row, int col);
    
    // Delete cell (remove from storage)
    void DeleteCell(int row, int col);
    void DeleteCell(const CellAddress& addr) { DeleteCell(addr.row, addr.col); }
    
    // Get all cells in range
    std::vector<SpreadsheetCell*> GetCellsInRange(const CellRange& range);
    std::vector<const SpreadsheetCell*> GetCellsInRange(const CellRange& range) const;
    
    // ===== CELL VALUES (CONVENIENCE) =====
    
    void SetCellValue(int row, int col, const std::string& text);
    void SetCellValue(int row, int col, double number);
    void SetCellValue(int row, int col, bool boolean);
    void SetCellFormula(int row, int col, const std::string& formula);
    
    std::string GetCellText(int row, int col) const;
    double GetCellNumber(int row, int col) const;
    std::string GetCellDisplayValue(int row, int col) const;
    std::string GetCellFormula(int row, int col) const;
    
    void ClearCell(int row, int col);
    void ClearRange(const CellRange& range);
    void ClearRangeContents(const CellRange& range);
    void ClearRangeFormats(const CellRange& range);
    void ClearAll();
    
    // ===== USED RANGE =====
    
    CellRange GetUsedRange() const;
    bool IsWithinUsedRange(int row, int col) const;
    int GetUsedRowCount() const;
    int GetUsedColumnCount() const;
    int GetTotalCellCount() const { return static_cast<int>(cells_.size()); }
    void RecalculateUsedRange();
    
    // ===== COLUMN OPERATIONS =====
    
    int GetColumnWidth(int col) const;
    void SetColumnWidth(int col, int width);
    void SetColumnWidthRange(int startCol, int endCol, int width);
    void AutoFitColumnWidth(int col);
    void AutoFitColumnWidthRange(int startCol, int endCol);
    
    int GetDefaultColumnWidth() const { return defaultColumnWidth_; }
    void SetDefaultColumnWidth(int width) { defaultColumnWidth_ = width; }
    
    bool IsColumnHidden(int col) const;
    void SetColumnHidden(int col, bool hidden);
    void SetColumnsHidden(int startCol, int endCol, bool hidden);
    
    const ColumnDefinition* GetColumnDefinition(int col) const;
    ColumnDefinition& GetOrCreateColumnDefinition(int col);
    
    void InsertColumns(int col, int count = 1);
    void DeleteColumns(int col, int count = 1);
    
    // ===== ROW OPERATIONS =====
    
    int GetRowHeight(int row) const;
    void SetRowHeight(int row, int height);
    void SetRowHeightRange(int startRow, int endRow, int height);
    void AutoFitRowHeight(int row);
    void AutoFitRowHeightRange(int startRow, int endRow);
    
    int GetDefaultRowHeight() const { return defaultRowHeight_; }
    void SetDefaultRowHeight(int height) { defaultRowHeight_ = height; }
    
    bool IsRowHidden(int row) const;
    void SetRowHidden(int row, bool hidden);
    void SetRowsHidden(int startRow, int endRow, bool hidden);
    
    const RowDefinition* GetRowDefinition(int row) const;
    RowDefinition& GetOrCreateRowDefinition(int row);
    
    void InsertRows(int row, int count = 1);
    void DeleteRows(int row, int count = 1);
    
    // ===== MERGED CELLS =====
    
    bool MergeCells(const CellRange& range);
    bool MergeCells(int startRow, int startCol, int endRow, int endCol);
    void UnmergeCells(const CellRange& range);
    void UnmergeCell(int row, int col);
    void UnmergeAll();
    
    bool IsCellMerged(int row, int col) const;
    const MergedCell* GetMergedCell(int row, int col) const;
    CellRange GetMergedRange(int row, int col) const;
    const std::vector<MergedCell>& GetMergedCells() const { return mergedCells_; }
    
    // ===== FREEZE PANES =====
    
    const FreezePanes& GetFreezePanes() const { return freezePanes_; }
    void FreezeRows(int count);
    void FreezeColumns(int count);
    void SetFreezePanes(int rows, int cols);
    void FreezePanesAt(const CellAddress& addr);
    void UnfreezePanes();
    
    bool HasFrozenPanes() const { return freezePanes_.HasFreeze(); }
    int GetFrozenRowCount() const { return freezePanes_.frozenRows; }
    int GetFrozenColumnCount() const { return freezePanes_.frozenColumns; }
    
    void SplitPanes(int horizontalPixels, int verticalPixels);
    void RemoveSplit();
    
    // ===== SELECTION =====
    
    const SelectionState& GetSelection() const { return selection_; }
    SelectionState& GetSelectionMutable() { return selection_; }
    
    CellAddress GetActiveCell() const { return selection_.activeCell; }
    void SetActiveCell(int row, int col);
    void SetActiveCell(const CellAddress& addr);
    
    void SelectCell(int row, int col);
    void SelectRange(const CellRange& range);
    void ExtendSelection(const CellRange& range);
    void AddToSelection(const CellRange& range);
    void ClearSelection();
    
    void SelectRow(int row);
    void SelectRows(int startRow, int endRow);
    void SelectColumn(int col);
    void SelectColumns(int startCol, int endCol);
    void SelectAll();
    
    bool IsCellSelected(int row, int col) const;
    
    // ===== AUTO FILTER =====
    
    bool HasAutoFilter() const { return autoFilter_ != nullptr && autoFilter_->enabled; }
    const AutoFilter* GetAutoFilter() const { return autoFilter_.get(); }
    
    void SetAutoFilter(const CellRange& range);
    void RemoveAutoFilter();
    void ApplyFilter(int column, const ColumnFilter& filter);
    void ClearFilter(int column);
    void ClearAllFilters();
    void RefreshFilter();
    
    // ===== SORTING =====
    
    void Sort(const CellRange& range, const std::vector<SortCriteria>& criteria);
    void SortByColumn(const CellRange& range, int column, SortOrder order = SortOrder::Ascending);
    
    // ===== CONDITIONAL FORMATTING =====
    
    void AddConditionalFormat(const ConditionalFormat& format);
    void RemoveConditionalFormat(int index);
    void ClearConditionalFormats();
    const std::vector<ConditionalFormat>& GetConditionalFormats() const { return conditionalFormats_; }
    std::optional<CellStyle> GetConditionalStyle(int row, int col) const;
    
    // ===== DATA VALIDATION =====
    
    void SetValidation(const CellRange& range, const DataValidation& validation);
    void RemoveValidation(const CellRange& range);
    void ClearAllValidations();
    const DataValidation* GetValidation(int row, int col) const;
    bool ValidateCell(int row, int col, std::string* errorMessage = nullptr) const;
    
    // ===== NAMED RANGES =====
    
    void AddNamedRange(const std::string& name, const CellRange& range);
    void RemoveNamedRange(const std::string& name);
    bool HasNamedRange(const std::string& name) const;
    const NamedRange* GetNamedRange(const std::string& name) const;
    const std::map<std::string, NamedRange>& GetNamedRanges() const { return namedRanges_; }
    
    // ===== PRINT SETTINGS =====
    
    PrintSettings& GetPrintSettings() { return printSettings_; }
    const PrintSettings& GetPrintSettings() const { return printSettings_; }
    void SetPrintSettings(const PrintSettings& settings) { printSettings_ = settings; }
    
    void SetPrintArea(const CellRange& range);
    void ClearPrintArea();
    void AddPageBreak(int row, bool horizontal = true);
    void RemovePageBreak(int row, bool horizontal = true);
    
    // ===== PROTECTION =====
    
    SheetProtection& GetProtection() { return protection_; }
    const SheetProtection& GetProtection() const { return protection_; }
    
    void Protect(const std::string& password = "");
    void Unprotect(const std::string& password = "");
    bool IsProtected() const { return protection_.isProtected; }
    bool CanEdit(int row, int col) const;
    
    // ===== VIEW SETTINGS =====
    
    int GetScrollRow() const { return scrollRow_; }
    int GetScrollColumn() const { return scrollCol_; }
    void SetScrollPosition(int row, int col);
    void ScrollToCell(int row, int col);
    
    int GetZoomLevel() const { return zoomLevel_; }
    void SetZoomLevel(int percent);
    
    // ===== COORDINATE CONVERSION =====
    
    // Convert pixel position to cell coordinates
    CellAddress PixelToCell(int x, int y) const;
    
    // Convert cell to pixel position (top-left corner)
    Point2Di CellToPixel(int row, int col) const;
    
    // Get cell bounds in pixels
    Rect2Di GetCellBounds(int row, int col) const;
    
    // Get visible range based on viewport
    CellRange GetVisibleRange(int viewportWidth, int viewportHeight) const;
    
    // Get column X position (accumulated widths)
    int GetColumnXPosition(int col) const;
    
    // Get row Y position (accumulated heights)
    int GetRowYPosition(int row) const;
    
    // ===== COPY/PASTE SUPPORT =====
    
    // Copy cell data for clipboard
    std::vector<std::vector<SpreadsheetCell>> CopyCells(const CellRange& range) const;
    
    // Paste cell data
    void PasteCells(const CellAddress& destination,
                    const std::vector<std::vector<SpreadsheetCell>>& cells,
                    const PasteOptions& options = PasteOptions());
    
    // ===== FILL OPERATIONS =====
    
    // Auto-fill (extend series)
    void AutoFill(const CellRange& source, const CellRange& destination);
    
    // Fill with value
    void FillRange(const CellRange& range, const SpreadsheetCell& value);
    
    // ===== FIND/REPLACE =====
    
    using FindOptions = SheetFindOptions;  // backward-compatible alias

    // Find cell containing text
    CellAddress FindNext(const std::string& searchText,
                         const CellAddress& startFrom,
                         const SheetFindOptions& options = SheetFindOptions()) const;

    // Find all cells
    std::vector<CellAddress> FindAll(const std::string& searchText,
                                     const SheetFindOptions& options = SheetFindOptions()) const;

    // Replace text
    int ReplaceAll(const std::string& searchText,
                   const std::string& replaceText,
                   const SheetFindOptions& options = SheetFindOptions());
    
    // ===== UTILITY =====
    
    // Clone sheet (deep copy)
    std::unique_ptr<SpreadsheetSheet> Clone(const std::string& newName = "") const;
    
    // Get memory usage
    size_t GetMemorySize() const;
    
    // Iterate over all cells
    template<typename Func>
    void ForEachCell(Func&& func) {
        for (auto& [coord, cell] : cells_) {
            func(coord.first, coord.second, *cell);
        }
    }
    
    template<typename Func>
    void ForEachCell(Func&& func) const {
        for (const auto& [coord, cell] : cells_) {
            func(coord.first, coord.second, *cell);
        }
    }
    
    // Iterate over range
    template<typename Func>
    void ForEachCellInRange(const CellRange& range, Func&& func) {
        for (int row = range.start.row; row <= range.end.row; ++row) {
            for (int col = range.start.col; col <= range.end.col; ++col) {
                if (auto* cell = GetCellIfExists(row, col)) {
                    func(row, col, *cell);
                }
            }
        }
    }
    
private:
    // Mark used range as needing recalculation
    void InvalidateUsedRange() { usedRangeDirty_ = true; }
    
    // Update cell references after insert/delete
    void ShiftCellReferences(int startRow, int startCol, int rowDelta, int colDelta);
    
    // Notify callbacks
    void NotifyCellChange(int row, int col);
    void NotifyRangeChange(const CellRange& range);
    void NotifyStructureChange();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

inline SpreadsheetSheet::SpreadsheetSheet(const std::string& name, int index)
    : name_(name)
    , index_(index)
{
    selection_.activeCell = CellAddress(0, 0);
    selection_.selections.push_back(CellRange(CellAddress(0, 0)));
}

inline void SpreadsheetSheet::SetName(const std::string& name) {
    if (name_ != name) {
        name_ = name;
        if (onNameChange) onNameChange(name);
    }
}

// ===== CELL ACCESS =====

inline SpreadsheetCell* SpreadsheetSheet::GetCell(int row, int col) {
    return &GetOrCreateCell(row, col);
}

inline SpreadsheetCell* SpreadsheetSheet::GetCell(const CellAddress& addr) {
    return GetCell(addr.row, addr.col);
}

inline const SpreadsheetCell* SpreadsheetSheet::GetCellIfExists(int row, int col) const {
    auto it = cells_.find({row, col});
    return (it != cells_.end()) ? it->second.get() : nullptr;
}

inline SpreadsheetCell* SpreadsheetSheet::GetCellIfExists(int row, int col) {
    auto it = cells_.find({row, col});
    return (it != cells_.end()) ? it->second.get() : nullptr;
}

inline const SpreadsheetCell* SpreadsheetSheet::GetCellIfExists(const CellAddress& addr) const {
    return GetCellIfExists(addr.row, addr.col);
}

inline bool SpreadsheetSheet::HasCell(int row, int col) const {
    return cells_.find({row, col}) != cells_.end();
}

inline SpreadsheetCell& SpreadsheetSheet::GetOrCreateCell(int row, int col) {
    auto key = std::make_pair(row, col);
    auto it = cells_.find(key);
    if (it == cells_.end()) {
        auto cell = std::make_unique<SpreadsheetCell>(row, col);
        auto* ptr = cell.get();
        cells_[key] = std::move(cell);
        InvalidateUsedRange();
        return *ptr;
    }
    return *it->second;
}

inline void SpreadsheetSheet::DeleteCell(int row, int col) {
    cells_.erase({row, col});
    InvalidateUsedRange();
}

inline std::vector<SpreadsheetCell*> SpreadsheetSheet::GetCellsInRange(const CellRange& range) {
    std::vector<SpreadsheetCell*> result;
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            if (auto* cell = GetCellIfExists(row, col)) {
                result.push_back(cell);
            }
        }
    }
    return result;
}

inline std::vector<const SpreadsheetCell*> SpreadsheetSheet::GetCellsInRange(const CellRange& range) const {
    std::vector<const SpreadsheetCell*> result;
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            if (auto* cell = GetCellIfExists(row, col)) {
                result.push_back(cell);
            }
        }
    }
    return result;
}

// ===== CELL VALUES =====

inline void SpreadsheetSheet::SetCellValue(int row, int col, const std::string& text) {
    GetOrCreateCell(row, col).SetText(text);
    NotifyCellChange(row, col);
}

inline void SpreadsheetSheet::SetCellValue(int row, int col, double number) {
    GetOrCreateCell(row, col).SetNumber(number);
    NotifyCellChange(row, col);
}

inline void SpreadsheetSheet::SetCellValue(int row, int col, bool boolean) {
    GetOrCreateCell(row, col).SetBoolean(boolean);
    NotifyCellChange(row, col);
}

inline void SpreadsheetSheet::SetCellFormula(int row, int col, const std::string& formula) {
    GetOrCreateCell(row, col).SetFormula(formula);
    NotifyCellChange(row, col);
}

inline std::string SpreadsheetSheet::GetCellText(int row, int col) const {
    if (auto* cell = GetCellIfExists(row, col)) {
        return cell->GetText();
    }
    return "";
}

inline double SpreadsheetSheet::GetCellNumber(int row, int col) const {
    if (auto* cell = GetCellIfExists(row, col)) {
        return cell->GetNumber();
    }
    return 0.0;
}

inline std::string SpreadsheetSheet::GetCellDisplayValue(int row, int col) const {
    if (auto* cell = GetCellIfExists(row, col)) {
        return cell->GetDisplayValue();
    }
    return "";
}

inline std::string SpreadsheetSheet::GetCellFormula(int row, int col) const {
    if (auto* cell = GetCellIfExists(row, col)) {
        return cell->GetFormulaText();
    }
    return "";
}

inline void SpreadsheetSheet::ClearCell(int row, int col) {
    DeleteCell(row, col);
    NotifyCellChange(row, col);
}

inline void SpreadsheetSheet::ClearRange(const CellRange& range) {
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            DeleteCell(row, col);
        }
    }
    NotifyRangeChange(range);
}

inline void SpreadsheetSheet::ClearRangeContents(const CellRange& range) {
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            if (auto* cell = GetCellIfExists(row, col)) {
                cell->ClearContents();
            }
        }
    }
    NotifyRangeChange(range);
}

inline void SpreadsheetSheet::ClearRangeFormats(const CellRange& range) {
    for (int row = range.start.row; row <= range.end.row; ++row) {
        for (int col = range.start.col; col <= range.end.col; ++col) {
            if (auto* cell = GetCellIfExists(row, col)) {
                cell->ClearFormats();
            }
        }
    }
    NotifyRangeChange(range);
}

inline void SpreadsheetSheet::ClearAll() {
    cells_.clear();
    columns_.clear();
    rows_.clear();
    mergedCells_.clear();
    conditionalFormats_.clear();
    validations_.clear();
    namedRanges_.clear();
    autoFilter_.reset();
    InvalidateUsedRange();
    NotifyStructureChange();
}

// ===== USED RANGE =====

inline void SpreadsheetSheet::RecalculateUsedRange() {
    usedRowMin_ = usedColMin_ = INT_MAX;
    usedRowMax_ = usedColMax_ = -1;
    
    for (const auto& [coord, cell] : cells_) {
        if (!cell->IsEmpty()) {
            usedRowMin_ = std::min(usedRowMin_, coord.first);
            usedRowMax_ = std::max(usedRowMax_, coord.first);
            usedColMin_ = std::min(usedColMin_, coord.second);
            usedColMax_ = std::max(usedColMax_, coord.second);
        }
    }
    
    if (usedRowMax_ < 0) {
        usedRowMin_ = usedRowMax_ = 0;
        usedColMin_ = usedColMax_ = 0;
    }
    
    usedRangeDirty_ = false;
}

inline CellRange SpreadsheetSheet::GetUsedRange() const {
    if (usedRangeDirty_) {
        const_cast<SpreadsheetSheet*>(this)->RecalculateUsedRange();
    }
    return CellRange(usedRowMin_, usedColMin_, usedRowMax_, usedColMax_);
}

inline bool SpreadsheetSheet::IsWithinUsedRange(int row, int col) const {
    if (usedRangeDirty_) {
        const_cast<SpreadsheetSheet*>(this)->RecalculateUsedRange();
    }
    return row >= usedRowMin_ && row <= usedRowMax_ &&
           col >= usedColMin_ && col <= usedColMax_;
}

inline int SpreadsheetSheet::GetUsedRowCount() const {
    if (usedRangeDirty_) {
        const_cast<SpreadsheetSheet*>(this)->RecalculateUsedRange();
    }
    return (usedRowMax_ >= usedRowMin_) ? (usedRowMax_ - usedRowMin_ + 1) : 0;
}

inline int SpreadsheetSheet::GetUsedColumnCount() const {
    if (usedRangeDirty_) {
        const_cast<SpreadsheetSheet*>(this)->RecalculateUsedRange();
    }
    return (usedColMax_ >= usedColMin_) ? (usedColMax_ - usedColMin_ + 1) : 0;
}

// ===== COLUMNS =====

inline int SpreadsheetSheet::GetColumnWidth(int col) const {
    auto it = columns_.find(col);
    return (it != columns_.end()) ? it->second.width : defaultColumnWidth_;
}

inline void SpreadsheetSheet::SetColumnWidth(int col, int width) {
    GetOrCreateColumnDefinition(col).width = std::max(0, std::min(width, SpreadsheetLimits::MaxColumnWidth * 7));
}

inline void SpreadsheetSheet::SetColumnWidthRange(int startCol, int endCol, int width) {
    for (int col = startCol; col <= endCol; ++col) {
        SetColumnWidth(col, width);
    }
}

inline bool SpreadsheetSheet::IsColumnHidden(int col) const {
    auto it = columns_.find(col);
    return (it != columns_.end()) ? it->second.hidden : false;
}

inline void SpreadsheetSheet::SetColumnHidden(int col, bool hidden) {
    GetOrCreateColumnDefinition(col).hidden = hidden;
}

inline void SpreadsheetSheet::SetColumnsHidden(int startCol, int endCol, bool hidden) {
    for (int col = startCol; col <= endCol; ++col) {
        SetColumnHidden(col, hidden);
    }
}

inline const ColumnDefinition* SpreadsheetSheet::GetColumnDefinition(int col) const {
    auto it = columns_.find(col);
    return (it != columns_.end()) ? &it->second : nullptr;
}

inline ColumnDefinition& SpreadsheetSheet::GetOrCreateColumnDefinition(int col) {
    auto it = columns_.find(col);
    if (it == columns_.end()) {
        columns_[col] = ColumnDefinition(col, defaultColumnWidth_);
    }
    return columns_[col];
}

// ===== ROWS =====

inline int SpreadsheetSheet::GetRowHeight(int row) const {
    auto it = rows_.find(row);
    return (it != rows_.end()) ? it->second.height : defaultRowHeight_;
}

inline void SpreadsheetSheet::SetRowHeight(int row, int height) {
    GetOrCreateRowDefinition(row).height = std::max(0, std::min(height, SpreadsheetLimits::MaxRowHeight * 4));
}

inline void SpreadsheetSheet::SetRowHeightRange(int startRow, int endRow, int height) {
    for (int row = startRow; row <= endRow; ++row) {
        SetRowHeight(row, height);
    }
}

inline bool SpreadsheetSheet::IsRowHidden(int row) const {
    auto it = rows_.find(row);
    return (it != rows_.end()) ? it->second.hidden : false;
}

inline void SpreadsheetSheet::SetRowHidden(int row, bool hidden) {
    GetOrCreateRowDefinition(row).hidden = hidden;
}

inline void SpreadsheetSheet::SetRowsHidden(int startRow, int endRow, bool hidden) {
    for (int row = startRow; row <= endRow; ++row) {
        SetRowHidden(row, hidden);
    }
}

inline const RowDefinition* SpreadsheetSheet::GetRowDefinition(int row) const {
    auto it = rows_.find(row);
    return (it != rows_.end()) ? &it->second : nullptr;
}

inline RowDefinition& SpreadsheetSheet::GetOrCreateRowDefinition(int row) {
    auto it = rows_.find(row);
    if (it == rows_.end()) {
        rows_[row] = RowDefinition(row, defaultRowHeight_);
    }
    return rows_[row];
}

// ===== MERGED CELLS =====

inline bool SpreadsheetSheet::MergeCells(const CellRange& range) {
    return MergeCells(range.start.row, range.start.col, range.end.row, range.end.col);
}

inline bool SpreadsheetSheet::MergeCells(int startRow, int startCol, int endRow, int endCol) {
    CellRange range(startRow, startCol, endRow, endCol);
    
    // Check for overlapping merges
    for (const auto& merge : mergedCells_) {
        if (merge.range.Intersects(range)) {
            return false;  // Cannot merge overlapping ranges
        }
    }
    
    // Create merge
    mergedCells_.push_back(MergedCell(range));
    
    // Mark cells as part of merge
    CellAddress origin(startRow, startCol);
    for (int row = startRow; row <= endRow; ++row) {
        for (int col = startCol; col <= endCol; ++col) {
            if (row != startRow || col != startCol) {
                GetOrCreateCell(row, col).SetMergeInfo(true, origin);
            }
        }
    }
    
    NotifyRangeChange(range);
    return true;
}

inline void SpreadsheetSheet::UnmergeCells(const CellRange& range) {
    auto it = std::remove_if(mergedCells_.begin(), mergedCells_.end(),
        [&range](const MergedCell& merge) {
            return merge.range.Intersects(range);
        });
    
    // Clear merge info from cells
    for (auto i = it; i != mergedCells_.end(); ++i) {
        for (int row = i->range.start.row; row <= i->range.end.row; ++row) {
            for (int col = i->range.start.col; col <= i->range.end.col; ++col) {
                if (auto* cell = GetCellIfExists(row, col)) {
                    cell->ClearMergeInfo();
                }
            }
        }
    }
    
    mergedCells_.erase(it, mergedCells_.end());
    NotifyRangeChange(range);
}

inline void SpreadsheetSheet::UnmergeCell(int row, int col) {
    if (auto* merge = GetMergedCell(row, col)) {
        UnmergeCells(merge->range);
    }
}

inline void SpreadsheetSheet::UnmergeAll() {
    for (const auto& merge : mergedCells_) {
        for (int row = merge.range.start.row; row <= merge.range.end.row; ++row) {
            for (int col = merge.range.start.col; col <= merge.range.end.col; ++col) {
                if (auto* cell = GetCellIfExists(row, col)) {
                    cell->ClearMergeInfo();
                }
            }
        }
    }
    mergedCells_.clear();
    NotifyStructureChange();
}

inline bool SpreadsheetSheet::IsCellMerged(int row, int col) const {
    return GetMergedCell(row, col) != nullptr;
}

inline const MergedCell* SpreadsheetSheet::GetMergedCell(int row, int col) const {
    for (const auto& merge : mergedCells_) {
        if (merge.Contains(row, col)) {
            return &merge;
        }
    }
    return nullptr;
}

inline CellRange SpreadsheetSheet::GetMergedRange(int row, int col) const {
    if (auto* merge = GetMergedCell(row, col)) {
        return merge->range;
    }
    return CellRange(row, col, row, col);
}

// ===== FREEZE PANES =====

inline void SpreadsheetSheet::FreezeRows(int count) {
    freezePanes_.frozenRows = std::max(0, count);
    freezePanes_.topLeftCell = CellAddress(count, freezePanes_.frozenColumns);
}

inline void SpreadsheetSheet::FreezeColumns(int count) {
    freezePanes_.frozenColumns = std::max(0, count);
    freezePanes_.topLeftCell = CellAddress(freezePanes_.frozenRows, count);
}

inline void SpreadsheetSheet::SetFreezePanes(int rows, int cols) {
    freezePanes_.frozenRows = std::max(0, rows);
    freezePanes_.frozenColumns = std::max(0, cols);
    freezePanes_.topLeftCell = CellAddress(rows, cols);
}

inline void SpreadsheetSheet::FreezePanesAt(const CellAddress& addr) {
    SetFreezePanes(addr.row, addr.col);
}

inline void SpreadsheetSheet::UnfreezePanes() {
    freezePanes_ = FreezePanes();
}

inline void SpreadsheetSheet::SplitPanes(int horizontalPixels, int verticalPixels) {
    freezePanes_.splitHorizontal = horizontalPixels > 0;
    freezePanes_.splitVertical = verticalPixels > 0;
    freezePanes_.horizontalSplitPosition = horizontalPixels;
    freezePanes_.verticalSplitPosition = verticalPixels;
}

inline void SpreadsheetSheet::RemoveSplit() {
    freezePanes_.splitHorizontal = false;
    freezePanes_.splitVertical = false;
    freezePanes_.horizontalSplitPosition = 0;
    freezePanes_.verticalSplitPosition = 0;
}

// ===== SELECTION =====

inline void SpreadsheetSheet::SetActiveCell(int row, int col) {
    selection_.activeCell = CellAddress(row, col);
    if (onSelectionChange) onSelectionChange();
}

inline void SpreadsheetSheet::SetActiveCell(const CellAddress& addr) {
    SetActiveCell(addr.row, addr.col);
}

inline void SpreadsheetSheet::SelectCell(int row, int col) {
    selection_.SetSingle(CellAddress(row, col));
    if (onSelectionChange) onSelectionChange();
}

inline void SpreadsheetSheet::SelectRange(const CellRange& range) {
    selection_.SetRange(range);
    if (onSelectionChange) onSelectionChange();
}

inline void SpreadsheetSheet::ExtendSelection(const CellRange& range) {
    if (!selection_.selections.empty()) {
        selection_.selections[0] = CellRange(selection_.activeCell, range.end);
    }
    if (onSelectionChange) onSelectionChange();
}

inline void SpreadsheetSheet::AddToSelection(const CellRange& range) {
    selection_.AddRange(range);
    if (onSelectionChange) onSelectionChange();
}

inline void SpreadsheetSheet::ClearSelection() {
    selection_.Clear();
    if (onSelectionChange) onSelectionChange();
}

inline void SpreadsheetSheet::SelectRow(int row) {
    SelectRange(CellRange(row, 0, row, SpreadsheetLimits::MaxColumns - 1));
}

inline void SpreadsheetSheet::SelectRows(int startRow, int endRow) {
    SelectRange(CellRange(startRow, 0, endRow, SpreadsheetLimits::MaxColumns - 1));
}

inline void SpreadsheetSheet::SelectColumn(int col) {
    SelectRange(CellRange(0, col, SpreadsheetLimits::MaxRows - 1, col));
}

inline void SpreadsheetSheet::SelectColumns(int startCol, int endCol) {
    SelectRange(CellRange(0, startCol, SpreadsheetLimits::MaxRows - 1, endCol));
}

inline void SpreadsheetSheet::SelectAll() {
    SelectRange(CellRange(0, 0, SpreadsheetLimits::MaxRows - 1, SpreadsheetLimits::MaxColumns - 1));
}

inline bool SpreadsheetSheet::IsCellSelected(int row, int col) const {
    return selection_.IsSelected(row, col);
}

// ===== COORDINATE CONVERSION =====

inline int SpreadsheetSheet::GetColumnXPosition(int col) const {
    int x = SpreadsheetLimits::RowHeaderWidth;
    for (int c = 0; c < col; ++c) {
        if (!IsColumnHidden(c)) {
            x += GetColumnWidth(c);
        }
    }
    return x;
}

inline int SpreadsheetSheet::GetRowYPosition(int row) const {
    int y = SpreadsheetLimits::HeaderRowHeight;
    for (int r = 0; r < row; ++r) {
        if (!IsRowHidden(r)) {
            y += GetRowHeight(r);
        }
    }
    return y;
}

inline Point2Di SpreadsheetSheet::CellToPixel(int row, int col) const {
    return Point2Di(GetColumnXPosition(col), GetRowYPosition(row));
}

inline Rect2Di SpreadsheetSheet::GetCellBounds(int row, int col) const {
    Point2Di pos = CellToPixel(row, col);
    int width = GetColumnWidth(col);
    int height = GetRowHeight(row);
    
    // Account for merged cells
    if (auto* merge = GetMergedCell(row, col)) {
        if (merge->IsTopLeft(row, col)) {
            for (int c = merge->range.start.col; c <= merge->range.end.col; ++c) {
                if (c != col) width += GetColumnWidth(c);
            }
            for (int r = merge->range.start.row; r <= merge->range.end.row; ++r) {
                if (r != row) height += GetRowHeight(r);
            }
        }
    }
    
    return Rect2Di(pos.x, pos.y, width, height);
}

inline CellAddress SpreadsheetSheet::PixelToCell(int x, int y) const {
    // Find column
    int col = 0;
    int accX = SpreadsheetLimits::RowHeaderWidth;
    while (col < SpreadsheetLimits::MaxColumns && accX < x) {
        if (!IsColumnHidden(col)) {
            accX += GetColumnWidth(col);
        }
        if (accX > x) break;
        ++col;
    }
    
    // Find row
    int row = 0;
    int accY = SpreadsheetLimits::HeaderRowHeight;
    while (row < SpreadsheetLimits::MaxRows && accY < y) {
        if (!IsRowHidden(row)) {
            accY += GetRowHeight(row);
        }
        if (accY > y) break;
        ++row;
    }
    
    return CellAddress(row, col);
}

inline CellRange SpreadsheetSheet::GetVisibleRange(int viewportWidth, int viewportHeight) const {
    int startRow = scrollRow_;
    int startCol = scrollCol_;
    
    // Find end row
    int endRow = startRow;
    int accHeight = 0;
    while (endRow < SpreadsheetLimits::MaxRows && accHeight < viewportHeight) {
        if (!IsRowHidden(endRow)) {
            accHeight += GetRowHeight(endRow);
        }
        ++endRow;
    }
    
    // Find end column
    int endCol = startCol;
    int accWidth = 0;
    while (endCol < SpreadsheetLimits::MaxColumns && accWidth < viewportWidth) {
        if (!IsColumnHidden(endCol)) {
            accWidth += GetColumnWidth(endCol);
        }
        ++endCol;
    }
    
    return CellRange(startRow, startCol, endRow, endCol);
}

// ===== VIEW =====

inline void SpreadsheetSheet::SetScrollPosition(int row, int col) {
    scrollRow_ = std::max(0, std::min(row, SpreadsheetLimits::MaxRows - 1));
    scrollCol_ = std::max(0, std::min(col, SpreadsheetLimits::MaxColumns - 1));
}

inline void SpreadsheetSheet::ScrollToCell(int row, int col) {
    SetScrollPosition(row, col);
}

inline void SpreadsheetSheet::SetZoomLevel(int percent) {
    zoomLevel_ = std::max(10, std::min(400, percent));
}

// ===== NOTIFICATIONS =====

inline void SpreadsheetSheet::NotifyCellChange(int row, int col) {
    InvalidateUsedRange();
    if (onCellChange) onCellChange(row, col);
}

inline void SpreadsheetSheet::NotifyRangeChange(const CellRange& range) {
    InvalidateUsedRange();
    if (onRangeChange) onRangeChange(range);
}

inline void SpreadsheetSheet::NotifyStructureChange() {
    InvalidateUsedRange();
    if (onStructureChange) onStructureChange();
}

// ===== MEMORY =====

inline size_t SpreadsheetSheet::GetMemorySize() const {
    size_t size = sizeof(SpreadsheetSheet);
    size += name_.capacity();
    
    for (const auto& [coord, cell] : cells_) {
        size += sizeof(coord) + cell->GetMemorySize();
    }
    
    size += columns_.size() * sizeof(ColumnDefinition);
    size += rows_.size() * sizeof(RowDefinition);
    size += mergedCells_.size() * sizeof(MergedCell);
    size += conditionalFormats_.size() * sizeof(ConditionalFormat);
    size += validations_.size() * sizeof(std::pair<CellRange, DataValidation>);
    
    return size;
}

} // namespace UltraCanvas
