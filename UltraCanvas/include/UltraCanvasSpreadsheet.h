// include/UltraCanvasSpreadsheet.h
// Main spreadsheet UI component with multi-sheet support
// Version: 1.3.0
// Last Modified: 2026-09-24
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
    // The sheet the step belongs to (SpreadsheetSheet::GetId); 0 for a step
    // that only knows sheetName.
    uint64_t sheetId = 0;
    // SheetDelete: a copy of the deleted sheet (same id), where it stood, and
    // the formulas on other sheets that deleting it turned to #REF!, so undo
    // can put all of it back.
    struct FormulaText { uint64_t sheetId; int row; int col; std::string formula; };
    std::shared_ptr<SpreadsheetSheet> removedSheet;
    int sheetIndex = -1;
    std::vector<FormulaText> rewrittenFormulas;
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
    // Right-click on the sheet tab strip: (sheet index, or -1 off the tabs;
    // windowX, windowY). Set this to put up your own menu; when it is unset
    // the built-in sheet menu opens (see SetSheetTabMenuEnabled).
    std::function<void(int, int, int)> onSheetTabContextMenu;
    // A header sort button reordered the selection: (range, column, order).
    std::function<void(const CellRange&, int, SortOrder)> onSelectionSorted;
    
private:
    std::vector<std::unique_ptr<SpreadsheetSheet>> sheets_;
    int activeSheetIndex_ = 0;
    int firstVisibleTab_ = 0;   // first sheet tab shown in the tab strip
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
    // The editor's selection, as its onSelectionChanged last reported it
    // (typing does not report, so it is checked against the text before use).
    size_t editorSelStart_ = 0;
    size_t editorSelEnd_ = 0;

    // Point mode: while a formula is edited and its caret sits where a
    // reference may go ("=SUM(|"), a click on a cell types the cell's address
    // there instead of ending the edit, and a drag makes it a range. The
    // reference typed last stays live - the next click replaces it, a drag or
    // Shift+click grows it, Ctrl+click adds another after a separator - until
    // the text or the caret is changed any other way.
    bool pointingReference_ = false;   // a point-mode drag is in progress
    CellAddress pointAnchor_;          // the cell the live reference starts at
    size_t pointRefStart_ = 0;
    size_t pointRefEnd_ = 0;
    std::string pointRefText_;         // the editor's text right after pointing
    bool IsPointedReferenceLive() const;
    // The span of the editor's text a pointed reference would replace (the
    // caret, the selection, or the live reference); false when none fits.
    bool GetPointReferenceSpan(size_t& start, size_t& end) const;
    void PointReference(const CellAddress& anchor, const CellAddress& to,
                        size_t start, size_t end, const std::string& prefix = "");
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
    // The sheet tab strip's menu (see ShowSheetTabMenuAt).
    std::shared_ptr<UltraCanvasMenu> sheetTabMenu_;
    bool sheetTabMenuEnabled_ = true;
    // The formula bar's function menu and its "fx" button.
    std::shared_ptr<UltraCanvasMenu> functionMenu_;
    bool functionMenuEnabled_ = true;
    // The caret the function hint was last drawn for: the editor moves its
    // caret (arrow keys) without telling us, and repaints only itself.
    size_t functionHintCaret_ = static_cast<size_t>(-1);

    // Header sort buttons (see SetHeaderSortEnabled). The last header sort is
    // remembered by sheet, range and column so its header can show the
    // direction for as long as that same range stays selected.
    bool headerSortEnabled_ = true;
    std::string headerSortSheet_;
    CellRange headerSortRange_;
    int headerSortColumn_ = -1;
    bool headerSortAscending_ = true;
    bool sortFormulaWarningEnabled_ = true;

    // Fill-handle drag: the selection it started from and the range the
    // pointer currently extends it to (drawn as a dashed outline).
    CellRange autoFillSource_;
    CellRange autoFillTarget_;
    // Scrolls the grid a step when a drag (fill handle, selection, point
    // mode) is past the edge of the cell area, so it can reach cells that are
    // not on screen.
    void AutoScrollForDrag(int x, int y);
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
    // Deletes the sheet as one undo step: Undo puts it back where it was,
    // with its cells, its own undo history and the references to it that
    // the delete turned to #REF!.
    void RemoveSheet(int index);
    void RemoveSheetByName(const std::string& name);
    // Renames the sheet and rewrites every formula, on every sheet, that
    // refers to it by its old name (=Sales.A1 becomes ='Sales 2016'.A1).
    // False, and nothing changes, when ValidateSheetName rejects the name.
    bool RenameSheet(int index, const std::string& newName);
    // Why `name` cannot be sheet `index`'s name - the CheckSheetName rules,
    // or another sheet already called that, ignoring case - or "" when it can.
    std::string ValidateSheetName(int index, const std::string& name) const;
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
    // Whether a click on a cell now would type its address into the formula
    // being edited (point mode) rather than end the edit: a formula is edited
    // and its caret (or selection) is where a reference may go - after the
    // '=', an opening parenthesis, a separator or an operator - or on the
    // reference the last click typed.
    bool CanPointReference() const { size_t s, e; return GetPointReferenceSpan(s, e); }
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

    // ===== SHEET TAB MENU =====
    // Right-click on a sheet tab opens a menu to insert a sheet before or
    // after it, duplicate, rename, move it left, right, to the start or the
    // end, and delete it (not the last one); a right-click beside the tabs
    // offers inserting one. Double-clicking a tab renames it. Defined in
    // UltraCanvasSpreadsheetFormatMenu.h/.cpp. On by default.
    void ShowSheetTabMenuAt(int sheetIndex, int windowX, int windowY);
    bool IsSheetTabMenuEnabled() const { return sheetTabMenuEnabled_; }
    void SetSheetTabMenuEnabled(bool enabled) { sheetTabMenuEnabled_ = enabled; }
    // Asks for the sheet's new name in a dialog (filled with the current
    // one) and renames it; a name ValidateSheetName rejects is explained and
    // asked for again.
    void PromptRenameSheet(int index);
    // Deletes the sheet - after an OK/Cancel question when it holds any
    // cells. Never the last sheet.
    void RequestRemoveSheet(int index);

    // ===== FUNCTION MENU =====
    // The "fx" button at the right end of the formula bar opens a menu of
    // every function the formula engine knows, by category and A to Z; the
    // name and description of each is shown. Picking one inserts it (see
    // InsertFunction). Defined in UltraCanvasSpreadsheetFormatMenu.h/.cpp.
    void ShowFunctionMenuAt(int windowX, int windowY);
    bool IsFunctionMenuEnabled() const { return functionMenuEnabled_; }
    void SetFunctionMenuEnabled(bool enabled) { functionMenuEnabled_ = enabled; Invalidate(); }
    // Inserts NAME() at the caret of the formula being edited, with the caret
    // between the parentheses. Not editing, it starts editing the active cell
    // in the formula bar: a formula is wrapped (=A1+B1 becomes =NAME(A1+B1),
    // the caret before the closing parenthesis), any other cell becomes
    // =NAME().
    void InsertFunction(const std::string& name);
    
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
    // Sort the selected block by one of its columns. Only the selected rows
    // move, and every selected column moves with them, so each row of the
    // block stays together; cells outside the selection are not touched.
    void SortSelectionByColumn(int column, SortOrder order);
    // SortSelectionByColumn as the header sort buttons and the context
    // menu's Sort entries run it: when the block holds formulas (and the
    // formula warning is on) it first asks, OK/Cancel, whether to go ahead.
    void RequestSortSelectionByColumn(int column, SortOrder order);
    // Whether the selection can be sorted that way: one rectangular block of
    // two or more rows, not whole columns or rows, not over merged cells -
    // the selections that show header sort buttons.
    bool IsSelectionSortable() const { CellRange range; return GetHeaderSortableRange(range); }

    // ===== AUTO-FILL =====
    // Fill `destination` - the selection extended down, up, right or left -
    // from the selection, as dragging the fill handle does: number series
    // continue, "Item 1" counts on, formulas shift their relative references,
    // anything else repeats (see SpreadsheetSheet::AutoFill). One undo step;
    // the destination is selected afterwards. False when the destination is
    // not such an extension or overlaps merged cells. AutoFillMode::Copy
    // repeats the selection as it is (no series), as a drag with Ctrl does.
    bool AutoFillSelection(const CellRange& destination,
                           AutoFillMode mode = AutoFillMode::Series);
    // Double-click on the fill handle: fills the selection down as far as the
    // data in the column next to it goes (see
    // SpreadsheetSheet::GetAutoFillDownEnd). False when there is none.
    bool AutoFillSelectionToDataEnd();
    // Ctrl+D / Ctrl+R: copies the selection's top row down over the rest of
    // it (its left column right across it); a selection one row high (one
    // column wide) copies the row above (the column to the left) into it.
    // Formulas shift their relative references. One undo step; the
    // selection stays. False when there is nothing to copy from.
    bool FillSelectionDown();
    bool FillSelectionRight();

    // Header sort buttons: while a block of two or more rows is selected, the
    // header of every column in it shows a small up/down button. Clicking it
    // sorts the block by that column (see SortSelectionByColumn); clicking the
    // same button again reverses the order. On by default.
    bool IsHeaderSortEnabled() const { return headerSortEnabled_; }
    void SetHeaderSortEnabled(bool enabled);
    // The column the current selection was last sorted by through its header
    // button, or -1 when the selection has not been sorted that way.
    int  GetHeaderSortColumn() const;
    bool GetHeaderSortAscending() const { return headerSortAscending_; }
    // A header sort of a block that holds formulas first asks, in an
    // OK/Cancel warning, whether to go ahead: the formulas move with their
    // rows but keep their references. On by default.
    bool IsSortFormulaWarningEnabled() const { return sortFormulaWarningEnabled_; }
    void SetSortFormulaWarningEnabled(bool enabled) { sortFormulaWarningEnabled_ = enabled; }
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
    // `password` opens a password-protected .xlsx (also one named .xls): the
    // ECMA-376 agile encryption Excel 2010 and later write, see
    // UltraCanvasOfficeEncryption.h. Without it such a file fails with a
    // message saying a password is needed; a wrong one, with "The password is
    // not correct". UltraCanvasFileLoader::RequestDocumentPassword asks the
    // user for it. Other formats ignore it.
    bool LoadFromFile(const std::string& filePath, const std::string& password = std::string());
    bool SaveToFile(const std::string& filePath);
    bool LoadODS(const std::string& filePath);
    bool SaveODS(const std::string& filePath);
    bool LoadXLSX(const std::string& filePath, const std::string& password = std::string());
    bool SaveXLSX(const std::string& filePath);
    // Legacy Excel workbooks (.xls): Excel 97-2003 and 5.0/95 binary files, and
    // what else Excel opens under the name - an .xlsx renamed, an HTML table, an
    // Excel 2003 XML workbook, delimited text. Read only: save as .xlsx or .ods.
    // See UltraCanvasSpreadsheetXls.h.
    bool LoadXLS(const std::string& filePath, const std::string& password = std::string());
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
    // The tab strip: the scroll buttons, the tabs that fit from the first
    // scrolled-to tab on, and the "+" button. Shared by drawing and
    // hit-testing so a click lands on what is drawn.
    struct SheetTabLayout {
        Rect2Di prevButton, nextButton, addButton;
        std::vector<std::pair<int, Rect2Di>> tabs;   // sheet index, rect
        bool canScrollPrev = false, canScrollNext = false;
    };
    SheetTabLayout LayoutSheetTabs(int firstTab) const;
    int SheetTabWidth(const std::string& name) const;
    void EnsureSheetTabVisible(int index);
    void RenderScrollbars(IRenderContext* ctx);
    void RenderCellEditor(IRenderContext* ctx);
    // While a formula is typed, the signature of the function call the caret
    // is in, under the editor, with the argument being typed in bold.
    void RenderFunctionHint(IRenderContext* ctx);   // draws the editor child
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
                         HorizontalScrollbar, VerticalScrollbar, AutoFillHandle,
                         ColumnSortButton };
    struct HitTestResult { HitArea area = HitArea::None; int row = -1; int col = -1; int tabIndex = -1;
                           bool addSheetButton = false; int tabScroll = 0;
                           bool functionMenuButton = false; };
    // The "fx" button at the right end of the formula bar (empty when off).
    Rect2Di FunctionMenuButtonRect() const;
    HitTestResult HitTest(int x, int y) const;

    // Header sort buttons: the block they sort (false when the selection is
    // not one), and where a column's button sits in its header cell (an empty
    // rect when the column is too narrow to carry one).
    bool GetHeaderSortableRange(CellRange& range) const;
    Rect2Di GetHeaderSortButtonRect(int colX, int colWidth) const;
    void RenderHeaderSortButton(IRenderContext* ctx, const Rect2Di& button, bool sorted,
                                bool ascending, const Color& color);
    // The fill-handle drag's destination for a pointer over (row, col): the
    // source extended along the axis the pointer has moved furthest out on.
    CellRange AutoFillTargetFor(int row, int col) const;
    bool FillSelectionAlong(bool down);   // FillSelectionDown / FillSelectionRight
    
    void BeginUndoGroup(UndoActionType type, const std::string& description);
    void RecordCellChange(int row, int col);
    void RecordRangeChange(const CellRange& range);
    void RecordStructureChange();
    void CommitUndoGroup();
    void CancelUndoGroup();
    void PushUndo(const SpreadsheetUndoAction& action);
    void ApplyUndo(const SpreadsheetUndoAction& action);
    // Keeps the undo and redo history in step with a renamed or deleted
    // sheet: `keep` may edit each step and returns false to drop it.
    void RewriteUndoHistory(const std::function<bool(SpreadsheetUndoAction&)>& keep);
    // The sheet a step belongs to: by id, or by name for a step without one.
    SpreadsheetSheet* FindUndoSheet(const SpreadsheetUndoAction& action);
    // Deletes sheet `index`; with `record`, fills it in as the SheetDelete
    // step that puts it back (RestoreDeletedSheet).
    void RemoveSheetAt(int index, SpreadsheetUndoAction* record);
    void RestoreDeletedSheet(const SpreadsheetUndoAction& action);
    static void RewriteSnapshotFormulas(SpreadsheetUndoAction& action,
                                        const std::function<std::string(const std::string&)>& rewrite);
    
    void InitializeDefaultSheet();
    // Clear/select the target sheet for a CSV import (creating Sheet1 when the
    // index is out of range). Shared by LoadCSV and LoadCSVWithOptions.
    SpreadsheetSheet* PrepareCSVSheet(int sheetIndex);
    void SetupSheetCallbacks(SpreadsheetSheet* sheet);
    std::string GenerateUniqueSheetName() const;
    void UpdateFormulaBar();
    void CommitEdit();
    Point2Di CellToScreen(int row, int col) const;
    Rect2Df CellAreaRect() const;   // the scrolling cell area, below/right of the headers
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
