// include/UltraCanvasSpreadsheet.h
// Main spreadsheet UI component with multi-sheet support
// Version: 1.0.0
// Last Modified: 2026-01-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasSpreadsheetTypes.h"
#include "UltraCanvasSpreadsheetCell.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasSpreadsheetFormula.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <stack>

namespace UltraCanvas {

// Forward declarations
class IRenderContext;
struct UCEvent;

// ============================================================================
// UNDO ACTION
// ============================================================================

struct SpreadsheetUndoAction {
    UndoActionType type;
    std::string description;
    std::string sheetName;
    CellRange range;
    std::vector<std::vector<SpreadsheetCell>> cellData;
    std::vector<ColumnDefinition> columnData;
    std::vector<RowDefinition> rowData;
    std::vector<MergedCell> mergeData;
    FreezePanes freezeData;
    int rowIndex = 0;
    int colIndex = 0;
    int count = 0;
    std::string oldName;
    std::string newName;
};

// ============================================================================
// SPREADSHEET EDIT MODE
// ============================================================================

enum class SpreadsheetEditMode {
    Normal,
    Editing,
    InCellEditing,
    Selecting,
    Resizing,
    AutoFilling
};

// ============================================================================
// SPREADSHEET COMPONENT
// ============================================================================

class UltraCanvasSpreadsheet : public UltraCanvasUIElement {
public:
    // ===== CALLBACKS (Base verb form per guidelines) =====
    std::function<void(int, int)> onCellClick;
    std::function<void(int, int)> onCellDoubleClick;
    std::function<void(int, int)> onCellChange;
    std::function<void(int, int)> onCellFormatChange;
    std::function<void()> onSelectionChange;
    std::function<void(int)> onSheetChange;
    std::function<void(int)> onSheetAdd;
    std::function<void(int)> onSheetRemove;
    std::function<void(int, const std::string&)> onSheetRename;
    std::function<void()> onFormulaBarChange;
    std::function<void(const std::string&)> onFormulaError;
    std::function<void()> onStructureChange;
    std::function<void()> onUndoStackChange;
    std::function<void(const std::string&)> onStatusChange;
    
private:
    std::vector<std::unique_ptr<SpreadsheetSheet>> sheets_;
    int activeSheetIndex_ = 0;
    std::unique_ptr<SpreadsheetFormulaEngine> formulaEngine_;
    std::map<std::string, NamedRange> namedRanges_;
    
    SpreadsheetEditMode editMode_ = SpreadsheetEditMode::Normal;
    std::string editBuffer_;
    int editCursorPos_ = 0;
    int editSelectionStart_ = -1;
    CellAddress editingCell_;
    
    ClipboardOperation clipboardOp_ = ClipboardOperation::None;
    CellRange clipboardRange_;
    std::string clipboardSheetName_;
    std::vector<std::vector<SpreadsheetCell>> clipboardData_;
    
    std::stack<SpreadsheetUndoAction> undoStack_;
    std::stack<SpreadsheetUndoAction> redoStack_;
    bool recordingUndo_ = true;
    
    bool mouseDown_ = false;
    Point2Di mouseDownPos_;
    CellAddress mouseDownCell_;
    int resizingColumn_ = -1;
    int resizingRow_ = -1;
    int resizeStartSize_ = 0;
    int resizeStartPos_ = 0;
    
    bool showGridlines_ = true;
    bool showRowHeaders_ = true;
    bool showColumnHeaders_ = true;
    bool showFormulaBar_ = true;
    bool showSheetTabs_ = true;
    bool showScrollbars_ = true;
    
    Color gridlineColor_ = Color(200, 200, 200);
    Color headerBackgroundColor_ = Color(240, 240, 240);
    Color headerTextColor_ = Colors::Black;
    Color selectionColor_ = Color(0, 120, 215, 40);
    Color selectionBorderColor_ = Color(0, 120, 215);
    Color activeHeaderColor_ = Color(0, 120, 215);
    Color frozenPaneLineColor_ = Color(100, 100, 100);
    Color sheetTabActiveColor_ = Colors::White;
    Color sheetTabInactiveColor_ = Color(230, 230, 230);
    Color formulaBarBackgroundColor_ = Colors::White;
    
    int formulaBarHeight_ = 28;
    int sheetTabHeight_ = 24;
    int scrollbarSize_ = 16;
    
    Rect2Di formulaBarBounds_;
    Rect2Di gridBounds_;
    Rect2Di sheetTabsBounds_;
    Rect2Di horizontalScrollBounds_;
    Rect2Di verticalScrollBounds_;
    
public:
    // ===== CONSTRUCTORS =====
    UltraCanvasSpreadsheet();
    UltraCanvasSpreadsheet(const std::string& id, int x, int y, int width, int height);
    virtual ~UltraCanvasSpreadsheet();

    UltraCanvasSpreadsheet(const UltraCanvasSpreadsheet&) = delete;
    UltraCanvasSpreadsheet& operator=(const UltraCanvasSpreadsheet&) = delete;

    // ===== UIELEMENT INTERFACE =====
    void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    void SetBounds(const Rect2Di& b) override;   // recompute layout on resize
    bool AcceptsFocus() const override { return true; }
    bool SetFocus(bool focus = true) override;   // commit edit on focus loss
    
    // ===== SHEET MANAGEMENT =====
    int GetSheetCount() const { return static_cast<int>(sheets_.size()); }
    SpreadsheetSheet* GetActiveSheet();
    const SpreadsheetSheet* GetActiveSheet() const;
    int GetActiveSheetIndex() const { return activeSheetIndex_; }
    void SetActiveSheet(int index);
    void SetActiveSheetByName(const std::string& name);
    SpreadsheetSheet* GetSheet(int index);
    const SpreadsheetSheet* GetSheet(int index) const;
    SpreadsheetSheet* GetSheetByName(const std::string& name);
    const SpreadsheetSheet* GetSheetByName(const std::string& name) const;
    SpreadsheetSheet* AddSheet(const std::string& name = "");
    SpreadsheetSheet* InsertSheet(int index, const std::string& name = "");
    void RemoveSheet(int index);
    void RemoveSheetByName(const std::string& name);
    void RenameSheet(int index, const std::string& newName);
    void MoveSheet(int fromIndex, int toIndex);
    SpreadsheetSheet* CopySheet(int index, const std::string& newName = "");
    std::vector<std::string> GetSheetNames() const;
    
    // ===== CELL ACCESS =====
    SpreadsheetCell* GetCell(int row, int col);
    SpreadsheetCell* GetCell(const CellAddress& addr);
    const SpreadsheetCell* GetCellIfExists(int row, int col) const;
    void SetCellValue(int row, int col, const std::string& text);
    void SetCellValue(int row, int col, double number);
    void SetCellValue(int row, int col, bool boolean);
    void SetCellFormula(int row, int col, const std::string& formula);
    std::string GetCellText(int row, int col) const;
    double GetCellNumber(int row, int col) const;
    std::string GetCellDisplayValue(int row, int col) const;
    std::string GetCellFormula(int row, int col) const;
    void ClearCell(int row, int col);
    void ClearSelection();
    void ClearRange(const CellRange& range);
    
    // ===== SELECTION =====
    CellAddress GetActiveCell() const;
    void SetActiveCell(int row, int col);
    void SetActiveCell(const CellAddress& addr);
    CellRange GetSelection() const;
    void Select(const CellRange& range);
    void SelectCell(int row, int col);
    void SelectRow(int row);
    void SelectColumn(int col);
    void SelectAll();
    void ExtendSelection(const CellAddress& toCell);
    void AddToSelection(const CellRange& range);
    bool IsCellSelected(int row, int col) const;
    
    // ===== NAVIGATION =====
    void MoveActiveCell(int rowDelta, int colDelta);
    void MoveToCellBegin();
    void MoveToCellEnd();
    void MoveToRowBegin();
    void MoveToRowEnd();
    void MoveToDataRegionEdge(int rowDir, int colDir);
    void PageUp();
    void PageDown();
    void ScrollToCell(int row, int col);
    void EnsureCellVisible(int row, int col);
    
    // ===== EDITING =====
    void StartEditing();
    void StartEditingAt(int row, int col);
    void StartInCellEditing();
    void StopEditing(bool confirm = true);
    void CancelEditing();
    bool IsEditing() const { return editMode_ == SpreadsheetEditMode::Editing || 
                                   editMode_ == SpreadsheetEditMode::InCellEditing; }
    SpreadsheetEditMode GetEditMode() const { return editMode_; }
    std::string GetEditBuffer() const { return editBuffer_; }
    void SetEditBuffer(const std::string& text);
    void InsertTextAtCursor(const std::string& text);
    
    // ===== FORMATTING =====
    void SetSelectionFont(const CellFont& font);
    void SetSelectionFontFamily(const std::string& family);
    void SetSelectionFontSize(float size);
    void SetSelectionBold(bool bold);
    void SetSelectionItalic(bool italic);
    void SetSelectionUnderline(UnderlineStyle style = UnderlineStyle::Single);
    void SetSelectionFontColor(const Color& color);
    void SetSelectionBackgroundColor(const Color& color);
    void SetSelectionBorders(const CellBorders& borders);
    void SetSelectionAlignment(HorizontalAlignment h, VerticalAlignment v);
    void SetSelectionNumberFormat(const NumberFormat& format);
    void SetSelectionWrapText(bool wrap);
    
    CellFont GetActiveCellFont() const;
    Color GetActiveCellBackgroundColor() const;
    HorizontalAlignment GetActiveCellHAlign() const;
    VerticalAlignment GetActiveCellVAlign() const;
    
    // ===== MERGE CELLS =====
    bool MergeSelection();
    void UnmergeSelection();
    void MergeCells(const CellRange& range);
    void UnmergeCells(const CellRange& range);
    bool IsCellMerged(int row, int col) const;
    
    // ===== FREEZE PANES =====
    void FreezeRows(int count);
    void FreezeColumns(int count);
    void FreezePanes(int rows, int cols);
    void FreezePanesAtSelection();
    void UnfreezePanes();
    bool HasFrozenPanes() const;
    int GetFrozenRowCount() const;
    int GetFrozenColumnCount() const;
    
    // ===== ROWS AND COLUMNS =====
    int GetColumnWidth(int col) const;
    void SetColumnWidth(int col, int width);
    void AutoFitColumnWidth(int col);
    void AutoFitSelectedColumns();
    void SetColumnHidden(int col, bool hidden);
    bool IsColumnHidden(int col) const;
    void InsertColumns(int count = 1);
    void InsertColumnsAt(int col, int count = 1);
    void DeleteColumns(int count = 1);
    void DeleteColumnsAt(int col, int count = 1);
    
    int GetRowHeight(int row) const;
    void SetRowHeight(int row, int height);
    void AutoFitRowHeight(int row);
    void AutoFitSelectedRows();
    void SetRowHidden(int row, bool hidden);
    bool IsRowHidden(int row) const;
    void InsertRows(int count = 1);
    void InsertRowsAt(int row, int count = 1);
    void DeleteRows(int count = 1);
    void DeleteRowsAt(int row, int count = 1);
    
    // ===== CLIPBOARD =====
    void Cut();
    void Copy();
    void Paste();
    void PasteSpecial(const PasteOptions& options);
    bool HasClipboardData() const { return clipboardOp_ != ClipboardOperation::None; }
    void ClearClipboard();
    
    // ===== UNDO/REDO =====
    void Undo();
    void Redo();
    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }
    void ClearUndoHistory();
    std::string GetUndoDescription() const;
    std::string GetRedoDescription() const;
    
    // ===== FIND/REPLACE =====
    CellAddress FindNext(const std::string& searchText, bool matchCase = false, 
                         bool matchEntireCell = false, bool searchFormulas = false);
    std::vector<CellAddress> FindAll(const std::string& searchText, bool matchCase = false,
                                     bool matchEntireCell = false, bool searchFormulas = false);
    int ReplaceAll(const std::string& searchText, const std::string& replaceText,
                   bool matchCase = false, bool matchEntireCell = false);
    
    // ===== SORTING & FILTERING =====
    void SortSelection(const std::vector<SortCriteria>& criteria);
    void SortSelectionAscending();
    void SortSelectionDescending();
    void SetAutoFilter();
    void RemoveAutoFilter();
    void ApplyFilter(int column, const ColumnFilter& filter);
    void ClearAllFilters();
    
    // ===== NAMED RANGES =====
    void AddNamedRange(const std::string& name, const CellRange& range, 
                       const std::string& sheetScope = "");
    void RemoveNamedRange(const std::string& name);
    bool HasNamedRange(const std::string& name) const;
    CellRange GetNamedRange(const std::string& name) const;
    std::vector<std::string> GetNamedRangeNames() const;
    
    // ===== FORMULA ENGINE =====
    SpreadsheetFormulaEngine* GetFormulaEngine() { return formulaEngine_.get(); }
    void Recalculate();
    void RecalculateAll();
    bool IsAutoCalculateEnabled() const;
    void SetAutoCalculate(bool enabled);
    
    // ===== FILE OPERATIONS =====
    bool LoadFromFile(const std::string& filePath);
    bool SaveToFile(const std::string& filePath);
    bool LoadODS(const std::string& filePath);
    bool SaveODS(const std::string& filePath);
    bool LoadXLSX(const std::string& filePath);
    bool SaveXLSX(const std::string& filePath);
    bool LoadCSV(const std::string& filePath, int sheetIndex = 0);
    bool SaveCSV(const std::string& filePath, int sheetIndex = -1);
    
    // ===== PRINT =====
    PrintSettings& GetPrintSettings();
    const PrintSettings& GetPrintSettings() const;
    void SetPrintArea(const CellRange& range);
    void ClearPrintArea();
    
    // ===== VIEW SETTINGS =====
    bool GetShowGridlines() const { return showGridlines_; }
    void SetShowGridlines(bool show) { showGridlines_ = show; Invalidate(); }
    bool GetShowRowHeaders() const { return showRowHeaders_; }
    void SetShowRowHeaders(bool show);
    bool GetShowColumnHeaders() const { return showColumnHeaders_; }
    void SetShowColumnHeaders(bool show);
    bool GetShowFormulaBar() const { return showFormulaBar_; }
    void SetShowFormulaBar(bool show);
    bool GetShowSheetTabs() const { return showSheetTabs_; }
    void SetShowSheetTabs(bool show);
    int GetZoomLevel() const;
    void SetZoomLevel(int percent);
    
    // ===== COLORS =====
    void SetGridlineColor(const Color& color) { gridlineColor_ = color; Invalidate(); }
    void SetHeaderBackgroundColor(const Color& color) { headerBackgroundColor_ = color; Invalidate(); }
    void SetSelectionColor(const Color& color) { selectionColor_ = color; Invalidate(); }
    void SetSelectionBorderColor(const Color& color) { selectionBorderColor_ = color; Invalidate(); }
    
    // ===== UTILITY =====
    CellAddress GetCellAtPosition(int x, int y) const;
    Rect2Di GetCellBounds(int row, int col) const;
    void Invalidate();
    size_t GetMemorySize() const;
    
private:
    void RenderFormulaBar(IRenderContext* ctx);
    void RenderColumnHeaders(IRenderContext* ctx);
    void RenderRowHeaders(IRenderContext* ctx);
    void RenderCornerHeader(IRenderContext* ctx);
    void RenderCells(IRenderContext* ctx);
    void RenderCell(IRenderContext* ctx, int row, int col, const Rect2Di& bounds);
    void RenderSelection(IRenderContext* ctx);
    void RenderFreezeLines(IRenderContext* ctx);
    void RenderSheetTabs(IRenderContext* ctx);
    void RenderScrollbars(IRenderContext* ctx);
    void RenderEditCursor(IRenderContext* ctx);
    void RenderClipboardIndicator(IRenderContext* ctx);
    void RenderAutoFillHandle(IRenderContext* ctx);
    
    void UpdateLayout();
    void CalculateBounds();
    
    void HandleMouseDown(const UCEvent& event);
    void HandleMouseUp(const UCEvent& event);
    void HandleMouseMove(const UCEvent& event);
    void HandleMouseDoubleClick(const UCEvent& event);
    void HandleMouseWheel(const UCEvent& event);
    void HandleKeyDown(const UCEvent& event);
    void HandleKeyPress(const UCEvent& event);
    
    enum class HitArea { None, FormulaBar, ColumnHeader, RowHeader, CornerHeader, 
                         Cell, ColumnResizer, RowResizer, SheetTab, 
                         HorizontalScrollbar, VerticalScrollbar, AutoFillHandle };
    struct HitTestResult { HitArea area = HitArea::None; int row = -1; int col = -1; int tabIndex = -1; };
    HitTestResult HitTest(int x, int y) const;
    
    void BeginUndoGroup(UndoActionType type, const std::string& description);
    void RecordCellChange(int row, int col);
    void RecordRangeChange(const CellRange& range);
    void RecordStructureChange();
    void CommitUndoGroup();
    void CancelUndoGroup();
    void PushUndo(const SpreadsheetUndoAction& action);
    void ApplyUndo(const SpreadsheetUndoAction& action);
    
    void InitializeDefaultSheet();
    void SetupSheetCallbacks(SpreadsheetSheet* sheet);
    std::string GenerateUniqueSheetName() const;
    void UpdateFormulaBar();
    void CommitEdit();
    Point2Di CellToScreen(int row, int col) const;
    CellAddress ScreenToCell(int x, int y) const;
    CellRange GetVisibleRange() const;
    bool IsCellVisible(int row, int col) const;
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

// UI elements derive from std::enable_shared_from_this, so create them as
// shared_ptr (matches CreateButton/CreateLabel and other framework factories).
inline std::shared_ptr<UltraCanvasSpreadsheet> CreateSpreadsheetElement(
    const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasSpreadsheet>(id, x, y, width, height);
}

} // namespace UltraCanvas
