// include/UltraCanvasSpreadsheet.h
// Main spreadsheet UI component with multi-sheet support
// Version: 1.2.0
// Last Modified: 2026-08-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSpreadsheetTypes.h"
#include "UltraCanvasSpreadsheetCell.h"
#include "UltraCanvasSpreadsheetSheet.h"
#include "UltraCanvasSpreadsheetFormula.h"
#include "UltraCanvasCSVImport.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <stack>

namespace UltraCanvas {
class UltraCanvasTextInput;
class UltraCanvasMenu;

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

// A text measurer backed by a live render context: it selects each cell's own
// font before measuring, so a bold 14pt heading is not sized with 11pt body
// metrics. Returns nullptr for a null context, which the sheet's auto-fit takes
// as "estimate from the font size instead".
CellTextMeasureFn MakeSpreadsheetTextMeasurer(IRenderContext* ctx);

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

// A container, not a bare element: the cell / formula-bar editor is a real
// UltraCanvasTextInput child (caret, selection, clipboard, undo, IME) rather
// than a private buffer painted by hand.
class UltraCanvasSpreadsheet : public UltraCanvasContainer {
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
    // Right-click on the grid: (row, col, windowX, windowY). Set this to put up
    // your own menu; when it is unset the built-in cell-formatting menu opens
    // (see SetFormatMenuEnabled).
    std::function<void(int, int, int, int)> onCellContextMenu;
    
private:
    std::vector<std::unique_ptr<SpreadsheetSheet>> sheets_;
    int activeSheetIndex_ = 0;
    std::unique_ptr<SpreadsheetFormulaEngine> formulaEngine_;
    std::map<std::string, NamedRange> namedRanges_;
    
    SpreadsheetEditMode editMode_ = SpreadsheetEditMode::Normal;
    // The live text, mirrored from the editor child so the formula preview and
    // the formula bar can read it without reaching into the editor.
    std::string editBuffer_;
    CellAddress editingCell_;
    // The editor itself: created when an edit starts, moved onto the cell or
    // the formula bar depending on the mode, destroyed when the edit ends.
    std::shared_ptr<UltraCanvasTextInput> cellEditor;
    void BuildCellEditor(const std::string& initialText, bool selectAll);
    void DestroyCellEditor();
    void PositionCellEditor();
    Rect2Df EditorRect() const;   // cell rect or formula-bar text rect
    
    ClipboardOperation clipboardOp_ = ClipboardOperation::None;
    CellRange clipboardRange_;
    std::string clipboardSheetName_;
    std::vector<std::vector<SpreadsheetCell>> clipboardData_;
    
    std::stack<SpreadsheetUndoAction> undoStack_;
    std::stack<SpreadsheetUndoAction> redoStack_;
    bool recordingUndo_ = true;

    // Reason for the most recent failed Load*/Save* (see GetLastError()).
    std::string lastError_;

    bool mouseDown_ = false;
    Point2Di mouseDownPos_;
    CellAddress mouseDownCell_;
    int resizingColumn_ = -1;
    int resizingRow_ = -1;
    int resizeStartSize_ = 0;
    int resizeStartPos_ = 0;
    bool draggingHScrollbar_ = false;
    bool draggingVScrollbar_ = false;
    
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

    // The built-in formatting context menu, created lazily on first use.
    std::shared_ptr<UltraCanvasMenu> formatMenu_;
    bool formatMenuEnabled_ = true;
    // Set by a file load: columns the document did not size are fitted to their
    // content on the next render, when a render context exists to measure text
    // with. Doing it at load time would have to guess the font metrics.
    bool autoFitPending_ = false;

    float formulaBarHeight_ = 28;
    float sheetTabHeight_ = 24;
    float scrollbarSize_ = 16;
    
    Rect2Df formulaBarBounds_;
    Rect2Df gridBounds_;
    Rect2Df sheetTabsBounds_;
    Rect2Df horizontalScrollBounds_;
    Rect2Df verticalScrollBounds_;
    
public:
    // ===== CONSTRUCTORS =====
    UltraCanvasSpreadsheet();
    UltraCanvasSpreadsheet(const std::string& id, float x, float y, float width, float height);

    UltraCanvasSpreadsheet(const UltraCanvasSpreadsheet&) = delete;
    UltraCanvasSpreadsheet& operator=(const UltraCanvasSpreadsheet&) = delete;

    // ===== UIELEMENT INTERFACE =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    void Arrange(const UltraCanvas::Rect2Df &newFinalRect, const CSSLayout::LayoutContext &ctx) override;
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
    // The range the SetSelection* / AdjustSelection* calls actually write to.
    // Identical to GetSelection() for an ordinary range; a whole-column or
    // whole-row selection (from a header click, or SelectAll) is clipped to the
    // cells that exist, because walking its full 1,048,576 rows would allocate
    // a cell for every empty one.
    CellRange GetFormattingRange() const;
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
    
    // Add `delta` to the decimal places of every selected cell's number format
    // (the "increase/decrease decimals" commands). A General cell becomes a
    // plain Number format first, as in Excel and LibreOffice.
    void AdjustSelectionDecimalPlaces(int delta);

    CellFont GetActiveCellFont() const;
    Color GetActiveCellBackgroundColor() const;
    HorizontalAlignment GetActiveCellHAlign() const;
    VerticalAlignment GetActiveCellVAlign() const;
    NumberFormat GetActiveCellNumberFormat() const;
    bool GetActiveCellWrapText() const;

    // ===== FORMATTING MENU =====
    // The cell-formatting menu (alignment, number format presets, font style,
    // column width) that the grid opens on a right-click. Defined in
    // UltraCanvasSpreadsheetFormatMenu.h/.cpp.
    void ShowFormatMenuAt(int windowX, int windowY);
    bool IsFormatMenuEnabled() const { return formatMenuEnabled_; }
    void SetFormatMenuEnabled(bool enabled) { formatMenuEnabled_ = enabled; }
    
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
    // Fit every used column the loaded document did not size. Called
    // automatically after a file load; also useful after filling a sheet from
    // code. Measures with the live render context when there is one.
    void AutoFitUnsizedColumns();
    // Queue that pass for the next render, when text can be measured properly.
    void RequestAutoFitForUnsizedColumns() { autoFitPending_ = true; }
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
    // Load a CSV/TSV file. The single-argument form auto-detects the encoding,
    // field separator and decimal separator (see CSVDetectOptions); use the
    // WithOptions form to apply settings chosen in the import dialog.
    bool LoadCSV(const std::string& filePath, int sheetIndex = 0);
    bool LoadCSVWithOptions(const std::string& filePath, const CSVImportOptions& options,
                            int sheetIndex = 0);
    bool SaveCSV(const std::string& filePath, int sheetIndex = -1);
    // Save a sheet as CSV/TSV using explicit export settings (separator, quote
    // character, quoting policy, encoding and line ending). sheetIndex < 0 saves
    // the active sheet.
    bool SaveCSVWithOptions(const std::string& filePath, const CSVExportOptions& options,
                            int sheetIndex = -1);
    // Build the CSV/TSV text for a sheet (UTF-8, before charset encoding) using
    // the given options. Used by the export dialog for a live preview.
    std::string ExportCSVToString(const CSVExportOptions& options, int sheetIndex = -1) const;

    // After any Load*/Save* call that returns false, this holds a human-readable
    // reason (e.g. "file locked by another application", "unsupported format").
    // Empty after a successful operation.
    const std::string& GetLastError() const { return lastError_; }

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
    void RenderFormulaRangeHighlights(IRenderContext* ctx);
    void RenderFreezeLines(IRenderContext* ctx);
    void RenderSheetTabs(IRenderContext* ctx);
    void RenderScrollbars(IRenderContext* ctx);
    void RenderCellEditor(IRenderContext* ctx);   // draws the editor child
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
    // Clear/select the target sheet for a CSV import (creating Sheet1 when the
    // index is out of range). Shared by LoadCSV and LoadCSVWithOptions.
    SpreadsheetSheet* PrepareCSVSheet(int sheetIndex);
    void SetupSheetCallbacks(SpreadsheetSheet* sheet);
    std::string GenerateUniqueSheetName() const;
    void UpdateFormulaBar();
    void CommitEdit();
    Point2Di CellToScreen(int row, int col) const;
    CellAddress ScreenToCell(int x, int y) const;
    CellRange GetVisibleRange() const;
    bool IsCellVisible(int row, int col) const;

    // Scrollbar geometry/mapping (shared by RenderScrollbars and drag handling)
    int GetMaxScrollRow() const;
    int GetMaxScrollColumn() const;
    int ScrollRowFromTrackY(int y) const;
    int ScrollColumnFromTrackX(int x) const;
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

// UI elements derive from std::enable_shared_from_this, so create them as
// shared_ptr (matches CreateButton/CreateLabel and other framework factories).
inline std::shared_ptr<UltraCanvasSpreadsheet> CreateSpreadsheetElement(
    const std::string& id, float x, float y, float width, float height) {
    return std::make_shared<UltraCanvasSpreadsheet>(id, x, y, width, height);
}
inline std::shared_ptr<UltraCanvasSpreadsheet> CreateSpreadsheetElement(
        const std::string& id, float width, float height) {
    return std::make_shared<UltraCanvasSpreadsheet>(id, -1, -1, width, height);
}

} // namespace UltraCanvas
