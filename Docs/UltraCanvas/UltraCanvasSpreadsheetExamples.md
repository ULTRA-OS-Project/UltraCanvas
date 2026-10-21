# UltraCanvasSpreadsheet Documentation

<!-- doc-check: int MyTextMetrics(const std::string& text, const CellFont& font); -->

## Contents

- [Overview](#overview)
- [Features](#features)
- [Header Include](#header-include)
- [Class Reference](#class-reference)
  - [Constructors and Factory](#constructors-and-factory)
  - [Cell Access (workbook-level convenience)](#cell-access-workbook-level-convenience)
  - [Sheet Management](#sheet-management)
  - [Selection and Navigation](#selection-and-navigation)
  - [Formatting (applies to current selection)](#formatting-applies-to-current-selection)
  - [Rows, Columns, Merge, and Freeze](#rows-columns-merge-and-freeze)
  - [Clipboard, Undo/Redo, Formula Engine](#clipboard-undoredo-formula-engine)
  - [Sorting](#sorting)
  - [Auto-Fill](#auto-fill)
  - [File Operations](#file-operations)
  - [View Settings](#view-settings)
  - [Callbacks](#callbacks)
  - [SpreadsheetCell](#spreadsheetcell)
  - [NumberFormat factories (`UltraCanvasSpreadsheetTypes.h`)](#numberformat-factories-ultracanvasspreadsheettypesh)
  - [Addresses and Ranges](#addresses-and-ranges)
- [Examples](#examples)
  - [1. Create a Spreadsheet Element](#1-create-a-spreadsheet-element)
  - [2. Seed Sample Data with Formatting](#2-seed-sample-data-with-formatting)
  - [3. Formulas and Recalculation](#3-formulas-and-recalculation)
  - [4. Open a Spreadsheet File (.ods / .csv)](#4-open-a-spreadsheet-file-ods--csv)
  - [5. Import CSV with Explicit Options](#5-import-csv-with-explicit-options)
  - [6. Save: ODS Directly, CSV via Export Dialog](#6-save-ods-directly-csv-via-export-dialog)
  - [7. Working with Multiple Sheets](#7-working-with-multiple-sheets)
  - [8. Reading Cell Values Back](#8-reading-cell-values-back)
- [Formula language and functions](#formula-language-and-functions)
- [Column widths](#column-widths)
- [Cell formatting menu](#cell-formatting-menu)
- [Function menu](#function-menu)
- [Sorting from the context menu](#sorting-from-the-context-menu)
- [Fill handle](#fill-handle)
- [Pointing at cells in a formula](#pointing-at-cells-in-a-formula)
- [Sheet tab menu](#sheet-tab-menu)
- [Sorting a selection from the header](#sorting-a-selection-from-the-header)
- [Best Practices](#best-practices)
- [See Also](#see-also)

## Overview

**UltraCanvasSpreadsheet** is a full-featured, editable spreadsheet grid component. It supports multiple worksheets, sparse cell storage, an OpenFormula-compatible formula engine, rich per-cell formatting (fonts, fills, borders, number formats), merged cells, freeze panes, sorting/filtering, find/replace, undo/redo, and file import/export for OpenDocument (`.ods`), Excel (`.xlsx`) and CSV/TSV, plus import of legacy Excel 97-2003 and 5.0/95 workbooks (`.xls`, see [`UltraCanvasSpreadsheetXls`](UltraCanvasSpreadsheetXls.md)). Each cell is strongly typed (text, number, boolean, date/time, currency, percentage, error, or formula) and rendered with a built-in formula bar, sheet tabs, scrollbars, and row/column headers.

**Version:** 1.0.1
**Last Modified:** 2026-10-07
**Header:** `include/UltraCanvasSpreadsheet.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasContainer`

## Features

- **Editable Grid**: Click-to-edit cells with an in-cell editor and a formula bar
- **Multiple Worksheets**: Add, insert, remove, rename, move, and copy sheets with tabs; the tab strip's "+" button adds a sheet, its ◀ ▶ buttons scroll through tabs that do not fit, and a right-click on a tab opens a menu to insert, duplicate, rename, move and delete sheets (see [Sheet tab menu](#sheet-tab-menu))
- **Pointing in Formulas**: While a formula is typed, clicking a cell types its address at the caret and dragging types a range — `=SUM(` then a drag over `C2:C8` (see [Pointing at cells in a formula](#pointing-at-cells-in-a-formula))
- **Typed Cells**: Text, Number, Boolean, Date/Time, Currency, Percentage, Error, and Formula values
- **Formula Engine**: OpenFormula-compatible parser/evaluator (`=SUM(C2:C8)`, `=SUM(D2:D8)/SUM(C2:C8)`, etc.) with automatic recalculation and circular-reference detection, and some 150 Excel-compatible functions — `VLOOKUP`, `INDEX` and `MATCH`, `SUMIF` and `SUMIFS`, `COUNTIF` and `COUNTIFS`, `SUMPRODUCT`, text, date, statistical and financial functions (see [Formula language and functions](#formula-language-and-functions))
- **Cell Formatting**: Fonts (bold/italic/underline), fills, borders, alignment, and number formats (currency, percentage, scientific, date/time)
- **Layout Control**: Per-column widths, per-row heights, hidden rows/columns, merged cells, and freeze panes
- **Column Widths From the Document**: Widths and row heights stored in an imported `.ods` / `.xlsx` are applied to the grid; any column the file does not size is auto-fitted to its content, measured with the font actually in use (see [Column widths](#column-widths))
- **Function Menu**: an **fx** button at the end of the formula bar lists every formula function by category and A to Z, and inserts the one picked (see [Function menu](#function-menu))
- **Cell Formatting Menu**: Alignment, number-format presets with live samples, font style and size, text/background colour, sorting, and column/row sizing — on a right-click in the grid, or from your own toolbar button (see [Cell formatting menu](#cell-formatting-menu))
- **Data Tools**: Sorting, auto-filter, find/replace, named ranges, and data validation
- **Fill Handle**: Drag the small square at the selection's bottom-right corner down, up, right or left to fill: number series continue, "Item 1" counts on, formulas shift their references; Ctrl-drag copies instead, a double-click fills down beside the neighbouring data, Ctrl+D / Ctrl+R fill down / right (see [Fill handle](#fill-handle))
- **Header Sort Buttons**: Select a block of rows and each of its column headers shows an up/down button; clicking one sorts only that block by that column, with the block's other columns moving along (see [Sorting a selection from the header](#sorting-a-selection-from-the-header))
- **Clipboard & Undo**: Cut/Copy/Paste (including Paste Special) and multi-level Undo/Redo
- **File I/O**: Load/Save OpenDocument (`.ods`), Excel (`.xlsx`) and CSV/TSV, with auto-detection or explicit import/export options; load legacy Excel (`.xls`) — read only, save it as `.xlsx` or `.ods`
- **Bundled Demo File + Open Flow**: On entry the demo opens the bundled `media/docs/spreadsheet.ods` document (a monthly sales / chargeback report with live `SUM` totals), falling back to a formatted sample sheet if the file is missing, and provides "Open Spreadsheet File…", "Import CSV…", and "Save…" buttons driven by `UltraCanvasFileLoader`

## Header Include

```cpp
#include "UltraCanvasSpreadsheet.h"
```

## Class Reference

### Constructors and Factory

```cpp
class UltraCanvasSpreadsheet : public UltraCanvasContainer {
public:
    UltraCanvasSpreadsheet();
    UltraCanvasSpreadsheet(const std::string& id, float x, float y, float width, float height);
};
```

```cpp
// Preferred: UI elements are created as shared_ptr.
inline std::shared_ptr<UltraCanvasSpreadsheet> CreateSpreadsheetElement(
    const std::string& id, float x, float y, float width, float height);

inline std::shared_ptr<UltraCanvasSpreadsheet> CreateSpreadsheetElement(
    const std::string& id, float width, float height);   // auto-position (x = y = -1)
```

### Cell Access (workbook-level convenience)

These operate on the active sheet.

```cpp
SpreadsheetCell* GetCell(int row, int col);
SpreadsheetCell* GetCell(const CellAddress& addr);
const SpreadsheetCell* GetCellIfExists(int row, int col) const;

void SetCellValue(int row, int col, const std::string& text);
void SetCellValue(int row, int col, double number);
void SetCellValue(int row, int col, bool boolean);
void SetCellFormula(int row, int col, const std::string& formula);

std::string GetCellText(int row, int col) const;
double      GetCellNumber(int row, int col) const;
std::string GetCellDisplayValue(int row, int col) const;
std::string GetCellFormula(int row, int col) const;

void ClearCell(int row, int col);
void ClearRange(const CellRange& range);
```

### Sheet Management

```cpp
int   GetSheetCount() const;
SpreadsheetSheet* GetActiveSheet();
int   GetActiveSheetIndex() const;
void  SetActiveSheet(int index);
void  SetActiveSheetByName(const std::string& name);
SpreadsheetSheet* GetSheet(int index);
SpreadsheetSheet* GetSheetByName(const std::string& name);
SpreadsheetSheet* AddSheet(const std::string& name = "");
SpreadsheetSheet* InsertSheet(int index, const std::string& name = "");
void  RemoveSheet(int index);
// Renames, and rewrites the formulas on every sheet that name the old one.
// False (nothing changes) for a name ValidateSheetName rejects.
bool  RenameSheet(int index, const std::string& newName);
std::string ValidateSheetName(int index, const std::string& name) const;   // "" = fine
void  MoveSheet(int fromIndex, int toIndex);
SpreadsheetSheet* CopySheet(int index, const std::string& newName = "");
std::vector<std::string> GetSheetNames() const;

// The sheet tab menu (see "Sheet tab menu")
void  ShowSheetTabMenuAt(int sheetIndex, int windowX, int windowY);
bool  IsSheetTabMenuEnabled() const;
void  SetSheetTabMenuEnabled(bool enabled);
void  PromptRenameSheet(int index);    // name dialog, re-asks on a bad name
void  RequestRemoveSheet(int index);   // asks first when the sheet has cells

// Sheet level (UltraCanvasSpreadsheetSheet.h)
std::string CheckSheetName(const std::string& name);   // why not, or ""
std::string RenameSheetInFormula(const std::string& formula,
                                 const std::string& oldName, const std::string& newName);
std::string RemoveSheetFromFormula(const std::string& formula,
                                   const std::string& sheetName);   // its references -> #REF!
```

### Selection and Navigation

```cpp
CellAddress GetActiveCell() const;
void  SetActiveCell(int row, int col);
CellRange GetSelection() const;
void  Select(const CellRange& range);
void  SelectCell(int row, int col);
void  SelectRow(int row);
void  SelectColumn(int col);
void  SelectAll();
bool  IsCellSelected(int row, int col) const;

void  MoveActiveCell(int rowDelta, int colDelta);
void  ScrollToCell(int row, int col);
void  EnsureCellVisible(int row, int col);
```

### Formatting (applies to current selection)

```cpp
void SetSelectionBold(bool bold);
void SetSelectionItalic(bool italic);
void SetSelectionUnderline(UnderlineStyle style = UnderlineStyle::Single);
void SetSelectionFontFamily(const std::string& family);
void SetSelectionFontSize(float size);
void SetSelectionFontColor(const Color& color);
void SetSelectionBackgroundColor(const Color& color);
void SetSelectionBorders(const CellBorders& borders);
void SetSelectionAlignment(HorizontalAlignment h, VerticalAlignment v);
void SetSelectionNumberFormat(const NumberFormat& format);
void SetSelectionWrapText(bool wrap);
void AdjustSelectionDecimalPlaces(int delta);   // +1 / -1: the "increase/decrease decimals" commands

// Read the active cell's formatting back (used to tick the formatting menu)
CellFont             GetActiveCellFont() const;
Color                GetActiveCellBackgroundColor() const;
HorizontalAlignment  GetActiveCellHAlign() const;
VerticalAlignment    GetActiveCellVAlign() const;
NumberFormat         GetActiveCellNumberFormat() const;
bool                 GetActiveCellWrapText() const;
```

### Rows, Columns, Merge, and Freeze

```cpp
int  GetColumnWidth(int col) const;
void SetColumnWidth(int col, int width);
void AutoFitColumnWidth(int col);          // fit one column to its content
void AutoFitSelectedColumns();
void AutoFitUnsizedColumns();              // fit every column no document/user sized
void RequestAutoFitForUnsizedColumns();    // ...on the next render, when text can be measured
void SetColumnHidden(int col, bool hidden);
int  GetRowHeight(int row) const;
void SetRowHeight(int row, int height);
void SetRowHidden(int row, bool hidden);
void InsertRowsAt(int row, int count = 1);
void DeleteRowsAt(int row, int count = 1);
void InsertColumnsAt(int col, int count = 1);
void DeleteColumnsAt(int col, int count = 1);

bool MergeSelection();
void UnmergeSelection();
void MergeCells(const CellRange& range);
bool IsCellMerged(int row, int col) const;

void FreezeRows(int count);
void FreezeColumns(int count);
void FreezePanes(int rows, int cols);
void UnfreezePanes();
```

### Clipboard, Undo/Redo, Formula Engine

```cpp
void Cut();
void Copy();
void Paste();
void PasteSpecial(const PasteOptions& options);

void Undo();
void Redo();
bool CanUndo() const;
bool CanRedo() const;

SpreadsheetFormulaEngine* GetFormulaEngine();
void Recalculate();
void RecalculateAll();
bool IsAutoCalculateEnabled() const;
void SetAutoCalculate(bool enabled);
```

### Sorting

```cpp
void SortSelection(const std::vector<SortCriteria>& criteria);
void SortSelectionAscending();                 // by the selection's first column
void SortSelectionDescending();
void SortSelectionByColumn(int column, SortOrder order);   // by any column of the selection

// Header sort buttons (on by default)
bool IsHeaderSortEnabled() const;
void SetHeaderSortEnabled(bool enabled);
int  GetHeaderSortColumn() const;      // -1 unless the current selection was header-sorted
bool GetHeaderSortAscending() const;
bool IsSortFormulaWarningEnabled() const;   // OK/Cancel warning before sorting formulas
void SetSortFormulaWarningEnabled(bool enabled);
```

### Auto-Fill

```cpp
// Fill the selection's extension, as the fill handle does. False when the
// destination does not grow the selection along one axis, or hits merged cells.
// AutoFillMode::Copy repeats the selection as it is, as a Ctrl-drag does.
bool AutoFillSelection(const CellRange& destination,
                       AutoFillMode mode = AutoFillMode::Series);
bool AutoFillSelectionToDataEnd();   // double-click on the fill handle
bool FillSelectionDown();            // Ctrl+D
bool FillSelectionRight();           // Ctrl+R

// Sheet level (UltraCanvasSpreadsheetSheet.h)
void SpreadsheetSheet::AutoFill(const CellRange& source, const CellRange& destination,
                                AutoFillMode mode = AutoFillMode::Series);
int  SpreadsheetSheet::GetAutoFillDownEnd(const CellRange& source) const;   // -1 = none
std::string ShiftFormulaReferences(const std::string& formula, int rowDelta, int colDelta);
```

### File Operations

```cpp
// Format chosen automatically from the file extension. `password` opens a
// password-protected .xlsx (see UltraCanvasOfficeEncryption.md); other
// formats ignore it.
bool LoadFromFile(const std::string& filePath, const std::string& password = std::string());
// With a password: a password-protected .xlsx; any other format with one is refused.
bool SaveToFile(const std::string& filePath, const std::string& password = std::string());

bool LoadODS(const std::string& filePath);
bool SaveODS(const std::string& filePath);
bool LoadXLSX(const std::string& filePath, const std::string& password = std::string());
bool SaveXLSX(const std::string& filePath, const std::string& password = std::string());
// Legacy Excel (.xls): BIFF8 / BIFF5, and an .xlsx (password-protected or
// not), HTML table, Excel 2003 XML file or delimited text saved under the
// name. Read only.
bool LoadXLS(const std::string& filePath, const std::string& password = std::string());

// CSV/TSV: single-argument form auto-detects encoding/separators.
bool LoadCSV(const std::string& filePath, int sheetIndex = 0);
bool LoadCSVWithOptions(const std::string& filePath, const CSVImportOptions& options,
                        int sheetIndex = 0);
bool SaveCSV(const std::string& filePath, int sheetIndex = -1);   // sheetIndex < 0 = active sheet
bool SaveCSVWithOptions(const std::string& filePath, const CSVExportOptions& options,
                        int sheetIndex = -1);
std::string ExportCSVToString(const CSVExportOptions& options, int sheetIndex = -1) const;

// After any Load*/Save* that returns false, this holds a human-readable reason.
const std::string& GetLastError() const;
```

### View Settings

```cpp
void SetShowGridlines(bool show);
void SetShowRowHeaders(bool show);
void SetShowColumnHeaders(bool show);
void SetShowFormulaBar(bool show);
void SetShowSheetTabs(bool show);
int  GetZoomLevel() const;
void SetZoomLevel(int percent);
```

### Callbacks

```cpp
std::function<void(int, int)> onCellClick;        // (row, col)
std::function<void(int, int)> onCellDoubleClick;  // (row, col)
std::function<void(int, int)> onCellChange;       // (row, col)
std::function<void()>         onSelectionChange;
std::function<void(int)>      onSheetChange;       // active sheet index
std::function<void(const std::string&)> onFormulaError;
std::function<void()>         onStructureChange;
std::function<void(const std::string&)> onStatusChange;
std::function<void(const CellRange&, int, SortOrder)> onSelectionSorted;  // header sort: (range, column, order)
std::function<void(int, int, int)> onSheetTabContextMenu;  // (sheet index or -1, windowX, windowY)
```

### SpreadsheetCell

`GetCell(row, col)` returns a `SpreadsheetCell*` exposing typed setters, formatting, and getters.

```cpp
class SpreadsheetCell {
public:
    // Typed value setters
    void SetText(const std::string& text);
    void SetNumber(double value);
    void SetBoolean(bool value);
    void SetFormula(const std::string& formula);
    void SetDate(int year, int month, int day);
    void SetTime(int hour, int minute, int second);
    void SetCurrency(double amount, const std::string& currencyCode = "USD");
    void SetPercentage(double value);                 // 0.5 = 50%
    void SetValueFromString(const std::string& input); // auto-detects type

    // Formatting
    void SetBold(bool bold = true);
    void SetItalic(bool italic = true);
    void SetUnderline(UnderlineStyle style = UnderlineStyle::Single);
    void SetFontFamily(const std::string& family);
    void SetFontSize(float size);
    void SetFontColor(const Color& color);
    void SetBackgroundColor(const Color& color);
    void SetNumberFormat(const NumberFormat& format);
    void SetAlignment(HorizontalAlignment h, VerticalAlignment v);
    void SetTextWrap(bool wrap = true);

    // Getters
    CellValueType GetValueType() const;
    std::string GetText() const;
    double      GetNumber() const;
    std::string GetDisplayValue() const;
    const std::string& GetFormulaText() const;
    bool IsEmpty() const;
    bool HasFormula() const;
};
```

### NumberFormat factories (`UltraCanvasSpreadsheetTypes.h`)

```cpp
struct NumberFormat {
    static NumberFormat General();
    static NumberFormat Number(int decimals = 2, bool thousands = false);
    static NumberFormat Currency(const std::string& symbol = "$", int decimals = 2,
                                 bool symbolAfter = false);   // symbolAfter => "1,234.00 €"
    static NumberFormat Percentage(int decimals = 0);
    static NumberFormat Date(const std::string& format = "YYYY-MM-DD");
    static NumberFormat Time(const std::string& format = "HH:MM:SS");
    static NumberFormat Scientific(int decimals = 2);
    static NumberFormat Text();
};
```

### Addresses and Ranges

```cpp
struct CellAddress {
    int row = 0;   // 0-based
    int col = 0;   // 0-based
    CellAddress(int r, int c);
    std::string ToString() const;                    // e.g. "B2"
    static std::string ColumnToLetter(int col);      // 0 -> "A"
    static int LetterToColumn(const std::string& s); // "A" -> 0
    static CellAddress FromString(const std::string& str);
};

struct CellRange {
    CellAddress start, end;
    CellRange(int startRow, int startCol, int endRow, int endCol);
    std::string ToString() const;                    // e.g. "A1:B2"
    static CellRange FromString(const std::string& str);
};
```

## Examples

### 1. Create a Spreadsheet Element

Create the grid with the factory and add it to a container. Cell coordinates are 0-based `(row, col)`.

```cpp
const int kWidth = 1020, kHeight = 720;

auto root = std::make_shared<UltraCanvasContainer>("SpreadsheetExamples", 0, 0, kWidth, kHeight);

auto sheet = CreateSpreadsheetElement("ssDemoGrid", 20, 92, kWidth - 40, kHeight - 104);
root->AddChild(sheet);
```

### 2. Seed Sample Data with Formatting

Set column widths, a taller header row, bold headers, then fill numeric rows with currency and percentage number formats. `GetCell(row, col)` returns the cell for per-cell styling.

```cpp
void SeedSampleData(UltraCanvasSpreadsheet* sheet) {
    if (!sheet) return;

    // Currency formatted as "28,869.80 €"; percentage with 2 decimals.
    const NumberFormat euro = NumberFormat::Currency("€", 2, /*symbolAfter*/true);
    const NumberFormat pct  = NumberFormat::Percentage(2);

    // Column widths and a taller header row.
    const int widths[] = { 120, 90, 130, 110, 90 };
    for (int c = 0; c < 5; ++c) sheet->SetColumnWidth(c, widths[c]);
    sheet->SetRowHeight(0, 28);

    // Bold header row.
    const char* headers[] = { "Month", "Refunds", "Sales", "Chargebacks", "CBK %" };
    for (int c = 0; c < 5; ++c) {
        sheet->SetCellValue(0, c, std::string(headers[c]));
        sheet->GetCell(0, c)->SetBold(true);
    }

    struct Row { const char* month; int refunds; double sales; double chargebacks; double cbkPct; };
    const Row rows[] = {
        { "06.2016", 22, 28869.80, 278.80, 0.0097 },
        { "07.2016", 21, 32788.00, 327.00, 0.0100 },
        { "08.2016", 42, 30913.90, 444.80, 0.0144 },
    };

    int r = 1;
    for (const auto& row : rows) {
        sheet->SetCellValue(r, 0, std::string(row.month));
        sheet->GetCell(r, 1)->SetNumber(row.refunds);
        sheet->GetCell(r, 2)->SetNumber(row.sales);
        sheet->GetCell(r, 2)->SetNumberFormat(euro);
        sheet->GetCell(r, 3)->SetNumber(row.chargebacks);
        sheet->GetCell(r, 3)->SetNumberFormat(euro);
        sheet->GetCell(r, 4)->SetNumber(row.cbkPct);
        sheet->GetCell(r, 4)->SetNumberFormat(pct);
        ++r;
    }
}

SeedSampleData(sheet.get());
```

### 3. Formulas and Recalculation

Set formula cells with `SetCellFormula`, then call `Recalculate()` so the engine evaluates dependencies. Formulas use A1 notation and are 1-based in text (`B2:B8` is rows 1..7).

```cpp
const int totalRow = 9;
sheet->SetCellValue(totalRow, 0, std::string("07 to 12/2016"));
sheet->GetCell(totalRow, 0)->SetBold(true);

sheet->SetCellFormula(totalRow, 1, "=SUM(B2:B8)");
sheet->SetCellFormula(totalRow, 2, "=SUM(C2:C8)");
sheet->GetCell(totalRow, 2)->SetNumberFormat(NumberFormat::Currency("€", 2, true));
sheet->SetCellFormula(totalRow, 3, "=SUM(D2:D8)");

// Overall CBK %: chargebacks total over sales total.
sheet->SetCellFormula(totalRow, 4, "=SUM(D2:D8)/SUM(C2:C8)");
sheet->GetCell(totalRow, 4)->SetNumberFormat(NumberFormat::Percentage(2));

sheet->Recalculate();
```

### 4. Open a Spreadsheet File (.ods / .csv)

Wire an "Open" button to a file dialog and load via `LoadFromFile` (format auto-detected from the extension). On failure, report `GetLastError()`. Capture `shared_ptr`s so the grid and label stay valid for the async callback. A password-protected `.xlsx` needs its password: `UltraCanvasFileLoader::RequestDocumentPassword` asks for it (and calls straight back for any other file), then pass it as `LoadFromFile(path, password)` - the demo's Open button does this, see [UltraCanvasOfficeEncryption.md](UltraCanvasOfficeEncryption.md).

```cpp
auto openBtn = std::make_shared<UltraCanvasButton>("ssOpenBtn", 20, 50, 190, 30,
                                                   "Open Spreadsheet File…");
auto status  = std::make_shared<UltraCanvasLabel>("ssStatus", 496, 56, 510, 22);
root->AddChild(openBtn);
root->AddChild(status);

openBtn->onClick = [sheet, status]() {
    FileDialogOptions opts;
    opts.SetTitle("Open Spreadsheet File")
        .AddFilter("Spreadsheet files", std::vector<std::string>{ "ods", "csv", "tsv" })
        .AddFilter("OpenDocument Spreadsheet", "ods")
        .AddFilter("CSV / TSV", std::vector<std::string>{ "csv", "tsv" })
        .AddFilter("All files", "*");

    UltraCanvasFileLoader::OpenFileDialog(opts,
        [sheet, status](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) {
                status->SetText("Open cancelled.");
                status->RequestRedraw();
                return;
            }
            if (sheet->LoadFromFile(path)) {
                status->SetText("Loaded: " + path);
            } else {
                status->SetText("Could not open file: " + sheet->GetLastError());
            }
            sheet->RequestRedraw();
            status->RequestRedraw();
        });
};
```

### 5. Import CSV with Explicit Options

For full control over charset, separators, start row, and number recognition, pick a file, surface any open error, then run the import options dialog and call `LoadCSVWithOptions`.

```cpp
importBtn->onClick = [sheet, status]() {
    FileDialogOptions opts;
    opts.SetTitle("Select CSV / TSV File")
        .AddFilter("CSV / TSV", std::vector<std::string>{ "csv", "tsv", "txt" })
        .AddFilter("All files", "*");

    UltraCanvasFileLoader::OpenFileDialog(opts,
        [sheet, status](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) {
                status->SetText("Import cancelled.");
                status->RequestRedraw();
                return;
            }
            std::string openError = CSVDescribeOpenError(path);
            if (!openError.empty()) {
                status->SetText("Could not open file: " + openError);
                status->RequestRedraw();
                return;
            }
            ShowCSVImportDialog(path,
                [sheet, status, path](const CSVImportOptions& options) {
                    if (sheet->LoadCSVWithOptions(path, options)) {
                        status->SetText("Imported: " + path);
                    } else {
                        status->SetText("Could not import file: " + sheet->GetLastError());
                    }
                    sheet->RequestRedraw();
                    status->RequestRedraw();
                });
        });
};
```

### 6. Save: ODS Directly, CSV via Export Dialog

Choose the format from the chosen file name. `.ods` saves directly with `SaveToFile`; for `.csv` / `.tsv` open the export options dialog and call `SaveCSVWithOptions`.

```cpp
saveBtn->onClick = [sheet, status]() {
    FileDialogOptions opts;
    opts.SetTitle("Save Spreadsheet As")
        .SetDefaultFileName("spreadsheet.ods")
        .AddFilter("OpenDocument Spreadsheet", "ods")
        .AddFilter("CSV (comma separated)", "csv")
        .AddFilter("TSV (tab separated)", "tsv")
        .AddFilter("All files", "*");

    UltraCanvasFileLoader::SaveFileDialog(opts,
        [sheet, status](DialogResult result, const std::string& path) {
            if (result != DialogResult::OK || path.empty()) {
                status->SetText("Save cancelled.");
                status->RequestRedraw();
                return;
            }

            std::string ext;
            size_t dot = path.find_last_of('.');
            size_t sep = path.find_last_of("/\\");
            if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
                ext = path.substr(dot);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            }

            if (ext == ".csv" || ext == ".tsv") {
                CSVExportOptions initial;
                if (ext == ".tsv") initial.fieldSeparator = '\t';
                ShowCSVExportDialog(sheet.get(), initial,
                    [sheet, status, path](const CSVExportOptions& options) {
                        if (sheet->SaveCSVWithOptions(path, options)) {
                            status->SetText("Saved: " + path);
                        } else {
                            status->SetText("Could not save file: " + sheet->GetLastError());
                        }
                        status->RequestRedraw();
                    });
                return;
            }

            std::string savePath = ext.empty() ? path + ".ods" : path;
            if (sheet->SaveToFile(savePath)) {
                status->SetText("Saved: " + savePath);
            } else {
                status->SetText("Could not save file: " + sheet->GetLastError());
            }
            status->RequestRedraw();
        });
};
```

### 7. Working with Multiple Sheets

```cpp
SpreadsheetSheet* summary = sheet->AddSheet("Summary");
SpreadsheetSheet* detail  = sheet->AddSheet("Detail");

// Operate directly on a SpreadsheetSheet.
summary->SetCellValue(0, 0, std::string("Quarterly Summary"));
summary->GetCell(0, 0)->SetBold(true);
summary->SetColumnWidth(0, 200);

sheet->SetActiveSheetByName("Summary");
for (const auto& name : sheet->GetSheetNames()) {
    // ... list tabs ...
}
```

### 8. Reading Cell Values Back

```cpp
double sales      = sheet->GetCellNumber(2, 2);       // raw numeric value
std::string shown = sheet->GetCellDisplayValue(2, 2); // formatted, e.g. "30,913.90 €"
std::string text  = sheet->GetCellText(0, 0);         // "Month"
std::string fx    = sheet->GetCellFormula(9, 2);      // "=SUM(C2:C8)"
```

## Formula language and functions

Formulas are written as in Excel and LibreOffice, with one difference: a
reference to another sheet is written `'Sheet Name'.A1` (the OpenDocument
form), not Excel's `Sheet Name!A1`. Loading an `.xlsx` translates Excel's form
(and drops the `_xlfn.` prefix Excel stores before its newer functions), and
saving one writes it back, so a workbook round-trips through Excel; loading an
`.ods` translates OpenDocument's bracketed form the same way. A sheet name may
hold spaces, dots and quotes (`'It''s'.A1`). A reference to a sheet that does
not exist is `#REF!`.

Error values can be written as literals — `=IF(ISNA(A1),#N/A,A1*2)`,
`=IFERROR(#DIV/0!,0)` — and a number turned into text reads the way the cell
shows it: `=A1&" pie"` with 12.5 in A1 is `12.5 pie`.

**Calculation order.** A formula reading another formula cell gets that cell's
current result with its type — a number stays a number. A cell not yet
calculated in a pass (a total above the rows it adds up, a sheet reading a later
sheet) is calculated first; a circular reference reads the value it has.

**Functions**, by category (`GetFormulaEngine()->GetFunctionLibrary()
.GetFunctionNames()` lists them):

| Category | Functions |
|---|---|
| Lookup | `VLOOKUP`, `HLOOKUP`, `LOOKUP`, `XLOOKUP`, `INDEX`, `MATCH`, `CHOOSE`, `ROWS`, `COLUMNS` |
| Conditional | `SUMIF`, `SUMIFS`, `COUNTIF`, `COUNTIFS`, `AVERAGEIF`, `AVERAGEIFS`, `MAXIFS`, `MINIFS`, `COUNTBLANK`, `SUMPRODUCT`, `SUBTOTAL` |
| Math | `SUM`, `PRODUCT`, `SUMSQ`, `ABS`, `SQRT`, `POWER`, `EXP`, `LN`, `LOG`, `LOG10`, `MOD`, `QUOTIENT`, `INT`, `ROUND`, `ROUNDUP`, `ROUNDDOWN`, `TRUNC`, `CEILING`, `FLOOR`, `MROUND`, `EVEN`, `ODD`, `SIGN`, `FACT`, `COMBIN`, `GCD`, `LCM`, `PI`, `RAND`, `RANDBETWEEN`, `SIN`, `COS`, `TAN`, `ASIN`, `ACOS`, `ATAN`, `ATAN2`, `SINH`, `COSH`, `TANH`, `DEGREES`, `RADIANS` |
| Statistical | `AVERAGE`, `MEDIAN`, `MODE`, `MIN`, `MAX`, `COUNT`, `COUNTA`, `LARGE`, `SMALL`, `RANK`, `PERCENTILE`, `QUARTILE`, `STDEV`, `STDEVP`, `VAR`, `VARP` (and the `STDEV.S`, `STDEV.P`, `VAR.S`, `VAR.P`, `MODE.SNGL`, `RANK.EQ` names) |
| Text | `LEN`, `LEFT`, `RIGHT`, `MID`, `FIND`, `SEARCH`, `REPLACE`, `SUBSTITUTE`, `CONCATENATE`, `CONCAT`, `TEXTJOIN`, `UPPER`, `LOWER`, `PROPER`, `TRIM`, `CLEAN`, `REPT`, `EXACT`, `CHAR`, `CODE`, `VALUE`, `TEXT`, `T` |
| Date and time | `DATE`, `TIME`, `TODAY`, `NOW`, `YEAR`, `MONTH`, `DAY`, `HOUR`, `MINUTE`, `SECOND`, `WEEKDAY`, `EDATE`, `EOMONTH`, `DAYS`, `DATEDIF`, `DATEVALUE` |
| Logical | `IF`, `IFS`, `IFERROR`, `IFNA`, `SWITCH`, `AND`, `OR`, `NOT`, `XOR`, `TRUE`, `FALSE` |
| Information | `ISBLANK`, `ISNUMBER`, `ISTEXT`, `ISNONTEXT`, `ISLOGICAL`, `ISERROR`, `ISERR`, `ISNA`, `ISEVEN`, `ISODD`, `NA`, `N` |
| Financial | `PMT`, `FV`, `PV`, `NPV` |

Text functions count characters, not bytes: `=LEN("ขาย")` is 3. The
criteria of `SUMIF`, `COUNTIF` and their kin are Excel's: a value (`30`,
`"apple"`), a comparison (`">25"`, `"<>x"`, `"<=2024-01-01"` is text), and the
wildcards `*`, `?` and `~` (`"a*"`); `""` matches empty cells.

A function an application registers itself can read its arguments in the
order they were written, with each range's rows and columns, through the
evaluator:

```cpp
SpreadsheetFormulaEngine* engine = sheet->GetFormulaEngine();
FunctionDefinition rowCount;
rowCount.name = "ROWSOF";
rowCount.minArgs = 1;
rowCount.maxArgs = 1;
rowCount.implementation = [](const std::vector<FormulaValue>&,
                             const std::vector<std::vector<FormulaValue>>&,
                             FormulaEvaluator* evaluator) -> FormulaValue {
    const auto& args = evaluator->GetCallArguments();
    if (args.empty() || !args[0].isRange) return FormulaValue::Error(CellErrorType::ValueError);
    return FormulaValue::Number(args[0].range.end.row - args[0].range.start.row + 1);
};
engine->GetFunctionLibrary().RegisterFunction(rowCount);
```

## Column widths

A spreadsheet document may or may not say how wide its columns are, and the grid
handles both cases without the application doing anything.

**Widths stored in the document are applied.** OpenDocument keeps the width in a
`style:style` of family `table-column`, referenced from `<table:table-column>`
through `table:style-name` (and the row height likewise in a `table-row` style);
`.xlsx` keeps it in `<cols><col width="…"/></cols>` plus the sheet-wide
`sheetFormatPr/@defaultColWidth`. Both are read on import and converted to grid
pixels at 96 dpi, so `5.08cm`, `2in` and `144pt` all arrive as 192 px. Saving
writes the same conformant structures back, so a document round-trips through
UltraCanvas and reopens in LibreOffice or Excel with its layout intact.

**Anything the document did not size is auto-fitted to its content.** Every CSV,
and any `.ods`/`.xlsx` that leaves a column at the default (or asks for
`style:use-optimal-column-width` / `bestFit`), gets that column measured against
its widest displayed value. The measurement uses the render context's real glyph
advances in the font each cell actually uses, so a bold heading or a `1.234,00 €`
value is sized correctly rather than by character count. Because that needs a
render context, a file loaded before the grid is on a window is fitted on the
first render — `LoadFromFile` and friends arrange this for you.

A width becomes "explicit" — and is left alone by the auto-fit pass — when the
document supplied it, when the user drags a column divider or double-clicks it,
or when the application calls `SetColumnWidth`. To re-fit deliberately:

```cpp
grid->AutoFitColumnWidth(2);      // one column (also: double-click its divider)
grid->AutoFitSelectedColumns();   // the selection
grid->AutoFitUnsizedColumns();    // everything nothing has sized yet
```

At sheet level the same operations take an optional text measurer, which is what
lets them be used off-screen or in tests:

```cpp
SpreadsheetSheet* sheet = grid->GetActiveSheet();
sheet->AutoFitUnsizedColumns([](const std::string& text, const CellFont& font) {
    return MyTextMetrics(text, font);   // omit the argument to use the built-in estimate
});
bool authored = sheet->HasExplicitColumnWidth(0);
sheet->SetColumnWidth(0, 120, /*explicitWidth*/ true);
```

## Cell formatting menu

`UltraCanvasSpreadsheetFormatMenu.h` builds the standard formatting menu for the
current selection: alignment (horizontal, vertical, wrap), number-format presets,
font style and size, text and background colour, sorting the selected rows
(see [Sorting from the context menu](#sorting-from-the-context-menu)),
column/row sizing, and merge/unmerge. Each number preset shows a live sample rendered through the same
formatter the cells use, so the menu shows exactly what the cell will show.

The grid opens it on a right-click, with no application code at all — the click
selects the cell under the cursor (or keeps a multi-cell selection, or the whole
column/row when the click lands on a header) and pops the menu up there. Turn it
off with `SetFormatMenuEnabled(false)`, or replace it by setting
`onCellContextMenu`.

To put the same menu behind a toolbar button:

```cpp
#include "UltraCanvasSpreadsheetFormatMenu.h"

formatButton->onClick = [grid, formatButton]() {
    grid->ShowFormatMenuAt(static_cast<int>(formatButton->GetXInWindow()),
                           static_cast<int>(formatButton->GetYInWindow() +
                                            formatButton->GetHeight()));
};
```

Or build the items into a menu of your own:

```cpp
auto menu = CreateSpreadsheetFormatMenu("MyFormatMenu", grid.get());
// ...or merge them into an existing menu:
for (const auto& item : BuildSpreadsheetFormatMenuItems(grid.get())) {
    myMenu->AddItem(item);
}
```

Rebuild the items each time the menu opens (`PopulateSpreadsheetFormatMenu`)
so the ticks and radio marks reflect the cell that was just clicked.
`SpreadsheetNumberFormatPresets()` returns the preset list on its own — label,
`NumberFormat` and sample — if you want to drive a dropdown or a dialog with it.

## Function menu

The **fx** button at the right end of the formula bar opens a menu of every
function the formula engine knows - a submenu per category (Date and time,
Financial, Information, Logical, Lookup, Math, Statistical, Text) and one with
all of them A to Z. Each entry shows the call with its argument names -
`ROUND(number, num_digits)`, `IF(logical_test, value_if_true, [value_if_false])`,
`SUM(number1, [number2], ...)` (square brackets: optional; `...`: more of the
same) - and the function's description.
Picking one inserts the call:

| When | The formula bar gets |
|---|---|
| editing a formula | `NAME()` at the caret |
| on a formula cell, not editing | the formula wrapped: `=A1/B1` becomes `=ROUND(A1/B1)` |
| on any other cell | `=NAME()` |

The caret is left inside the call - between the parentheses, or before the
closing one of a wrapped formula - ready for the arguments.

While a formula is typed - in the formula bar or in the cell - a hint under
the editor shows the signature of the function call the caret is in, with
the argument being typed in **bold**, and the description below it. It
follows the caret: into a nested call, and on to the next argument after each
`,` (or `;`); text in quotes and array constants `{...}` are skipped.

The argument names are the ones Excel uses. From code:
`GetFunctionLibrary().GetSignature("ROUND")` gives the signature,
`GetSignatureArguments()` the arguments one by one, and
`FindFormulaCallAtCaret(formula, caret, name, argument)` the call and argument
at a position in a formula. Functions an
application registers on the engine appear too; the menu is rebuilt each time
it opens. From code: `InsertFunction("ROUND")` does what a pick does,
`BuildSpreadsheetFunctionMenuItems()` returns the items for a menu of your
own, and `SetFunctionMenuEnabled(false)` removes the button.

## Sorting from the context menu

The right-click menu has a **Sort** submenu that sorts the selected block the
way the header sort buttons do (see
[Sorting a selection from the header](#sorting-a-selection-from-the-header)):
only the selected rows move, each keeping its cells together.

- **Ascending by column X** / **Descending by column X** - by the column of
  the active cell (the cell that was right-clicked).
- **Ascending by** / **Descending by** - a submenu listing every column of the
  selection, to sort by another one.

It is offered for the same selections as the header buttons - one block of
two or more rows, not whole columns or rows, not over merged cells - and
otherwise shows one disabled hint. A block that holds formulas asks first, as
the buttons do. From code: `RequestSortSelectionByColumn(column, order)` sorts
with that warning, `SortSelectionByColumn` without it, and
`IsSelectionSortable()` says whether the selection can be sorted.

## Fill handle

The small square at the bottom-right corner of the selection is the fill
handle (the pointer turns into a crosshair over it). Drag it down, up, right
or left: a dashed outline shows the range the release will fill, following
whichever direction the pointer has moved furthest from the selection. On
release the new cells are filled from the selection, repeating its pattern:

| Selection holds | The new cells get |
|---|---|
| two or more numbers in a column (or row) | the series continued: `1, 2` → `3, 4, 5`; `10, 20` → `30, 40`; also backwards when dragged up or left |
| one text ending in a number | the number counted on: `Item 1` → `Item 2`, `Item 3` |
| a formula | the formula with its relative references shifted by the distance moved: `=C2*$D$1` one row down is `=C3*$D$1` |
| anything else (one number, plain text) | a copy |

Formatting comes along with each value. The filled range is selected
afterwards, the fill is one undo step (Ctrl+Z), and every formula is
recalculated, so totals that read the filled cells update. A drag that would
cover merged cells does nothing. Dragged past the edge of the grid, the grid
scrolls a row or column with each pointer move, so the fill can reach cells
that were off screen.

More ways to fill:

| Gesture | What it does |
|---|---|
| **Ctrl** held when the handle is released | copies the selection as it is: `1, 2` repeats as `1, 2, 1, 2`, `Item 1` stays `Item 1`; formulas still shift their references |
| **double-click** on the handle | fills the selection down as far as the data in the column beside it goes (the left one, or the right one when the left is empty) — select the `=B2*C2` in D2 and double-click to give every row of the table its own |
| **Ctrl+D** | fill down: copies the selection's top row over the rest of it; with a one-row selection, copies the row above into it |
| **Ctrl+R** | fill right: the same with the left column; with a one-column selection, copies the column to the left |

Each is one undo step. From code: `AutoFillSelection(destination,
AutoFillMode::Copy)`, `AutoFillSelectionToDataEnd()`, `FillSelectionDown()`
and `FillSelectionRight()`; `SpreadsheetSheet::GetAutoFillDownEnd` gives the
row a double-click fills down to.

From code, select the source and call `AutoFillSelection(destination)`; the
sheet-level `SpreadsheetSheet::AutoFill` does the same without selection or
undo, and `ShiftFormulaReferences` shifts a formula's text on its own.

## Pointing at cells in a formula

While a cell (or the formula bar) is being edited and its text is a formula,
the grid is a way to type references — as in Excel and LibreOffice Calc:

1. Type `=SUM(` in C10.
2. Drag over C2:C8. `C2:C8` appears at the caret as the drag goes, and the
   range is outlined in colour on the grid.
3. Type `)` and press Enter: `=SUM(C2:C8)`.

A click types a single cell's address. The pointer is a crosshair over the
cells whenever a click would do this, which is when the caret (or the
selected text) follows the `=`, an opening parenthesis, a separator (`,`
`;`), an operator (`+ - * / ^ & < > =`) or the `:` of a range — and is not
inside "quotes". Anywhere else — `=A1|` — a click on a cell ends the edit
and selects that cell, as before.

The reference typed last stays live until the formula is changed another way
(a key typed, the caret moved):

| While it is live | Does |
|---|---|
| click another cell | replaces it — try a few cells before settling |
| drag | makes it the range from where the press was |
| **Shift**+click | grows it into the range to the clicked cell |
| **Ctrl**+click | adds another reference after a `,`: `=SUM(C2,C5,C7` |

Selected text in a formula is replaced by the pointed reference, so select
`B2` in `=A1+B2` and click D4 to get `=A1+D4`. Every reference of the formula
being typed is outlined in the grid, in the cell and in the formula bar alike.
The second click of a double-click while pointing does not start editing the
clicked cell. `CanPointReference()` says whether a click would point, for an
application that wants to show it.

## Sheet tab menu

Right-click a sheet tab: the sheet opens, and its menu offers

- **Insert sheet before** / **Insert sheet after** — a new, empty sheet
  next to it, which opens;
- **Duplicate sheet** — a copy of it, values, formulas and formatting,
  named "Sales (2)", right after it;
- **Rename sheet...** — a dialog with the name to edit (a double-click on
  the tab does the same);
- **Move left**, **Move right**, **Move to start**, **Move to end** — each
  greyed out where the sheet already is;
- **Delete sheet** — asks first when the sheet holds any cells, and is
  greyed out for the only sheet; Ctrl+Z brings it back.

A right-click on the empty part of the strip offers **Insert sheet**, which
adds one at the end.

A sheet name is at most 31 characters, has none of `[ ] : * ? / \`, does not
start or end with an apostrophe, and is not another sheet's name (ignoring
case) — the rules `.xlsx` and `.ods` files share. A rejected name is
explained and the dialog asked again. Renaming rewrites every formula, on any
sheet, that names the sheet: `=Sales.C2` becomes `='Sales 2016'.C2`.
Deleting a sheet turns every reference to it into `#REF!` in the formula
itself, as Excel does: `=Gone.E5*2` becomes `=#REF!*2` and
`=SUM(Gone.A1:B3)` becomes `=SUM(#REF!)`, so the cell shows `#REF!` and the
formula says why.

Every change to the sheets is one undo step: inserting, duplicating,
renaming, moving and deleting. **Ctrl+Z** takes an inserted or duplicated
sheet away again (and **Ctrl+Y** brings it back, cells and all), gives a
renamed sheet its old name with the formulas that name it following, and
moves a moved sheet back. Loading a file starts a fresh undo history.

Undoing a delete puts the sheet back where it was,
with its cells, its own undo history, and the references the delete turned to
`#REF!` naming it again; **Ctrl+Y** deletes it again. If another sheet has
taken its name in the meantime, it comes back as "Gone (2)".

Each step finds its sheet
by a fixed id (`SpreadsheetSheet::GetId()`), not by name, so it never writes
into whichever sheet is open, or into another sheet that has since taken the
name.

From code: `RenameSheet` (returns false for a name `ValidateSheetName`
rejects), `PromptRenameSheet`, `RequestRemoveSheet`, and
`ShowSheetTabMenuAt(sheetIndex, windowX, windowY)`.
`BuildSpreadsheetSheetTabMenuItems(sheet, index)` in
`UltraCanvasSpreadsheetFormatMenu.h` gives the items for a menu of your own;
set `onSheetTabContextMenu` to open your own instead, or
`SetSheetTabMenuEnabled(false)` for none.

```cpp
// doc-check: example
auto sheet = CreateSpreadsheetElement("Book", 0, 0, 800, 600);
sheet->AddSheet("Sales");
sheet->SetCellFormula(0, 0, "=Sales.A1*2");
if (!sheet->RenameSheet(1, "Sales 2016")) {
    std::string why = sheet->ValidateSheetName(1, "Sales 2016");
}
sheet->onSheetTabContextMenu = [](int sheetIndex, int windowX, int windowY) {
    // open your own menu for sheetIndex (-1: beside the tabs)
};
```

## Sorting a selection from the header

Select a block of two or more rows — say `A2:E8`, the months of the demo's
sales report without its title and totals rows — and every column header over
the block shows a small button with an up and a down triangle. Clicking one:

- sorts **only the selected rows**, by the column whose button was clicked,
  ascending (numbers by value, text case-insensitively, empty cells last);
- moves the **other selected columns with it**, so each row of the block stays
  together — sort by Sales and the month, the refunds and the percentage of
  each row travel with its sales figure;
- leaves **everything outside the block alone**: the title row above, the
  totals row below and the columns beside it do not move.

The clicked button then shows a single filled triangle (apex up ascending,
apex down descending), and clicking it again reverses the order. The block
stays selected, so you can sort it by another column straight away. The
direction is shown for as long as that block remains the selection. A sort is
one undo step (Ctrl+Z).

The buttons appear only for a single rectangular selection of two rows or
more. Whole-column and whole-row selections get none (sorting a full column
would take its title along and run over the entire sheet), a block that
overlaps merged cells gets none (moving rows would tear the merge apart), and
a column narrower than about 44 px has no room for one.

```cpp
grid->onSelectionSorted = [statusLabel](const CellRange& range, int column, SortOrder order) {
    statusLabel->SetText("Sorted " + range.ToString() + " by column " +
                         CellAddress::ColumnToLetter(column));
};

// The same sort without the button:
grid->Select(CellRange(1, 0, 7, 4));                  // A2:E8
grid->SortSelectionByColumn(2, SortOrder::Descending); // by column C

grid->SetHeaderSortEnabled(false);                     // no header buttons
```

Formulas inside a sorted block move as they are: their references are not
rewritten, so a row formula such as `=D3/C3` keeps pointing at row 3 after
it moves. Formulas that cover the whole block (a `SUM(C2:C8)` in the totals
row) are unaffected as long as they sit outside it.

So when the block holds formulas, the header button asks first: an OK/Cancel
warning names the block and how many formula cells it contains, and explains
that they will keep their references. OK sorts, Cancel leaves the block as it
is. In the demo, select `A2:E10` (the months *and* the totals row) to see it.
Turn the warning off with `SetSortFormulaWarningEnabled(false)`.
`SortSelectionByColumn` itself never asks — code that calls it has decided —
and `SpreadsheetSheet::CountFormulaCells(range)` gives the count if you want
to warn in your own way.

## Best Practices

- **Create with the factory.** Use `CreateSpreadsheetElement(...)` so the element is a `shared_ptr` (matching the rest of the framework). Capture that `shared_ptr` in async callbacks (file dialogs) so the grid stays alive, and call `RequestRedraw()` after mutating it from a callback.
- **Set values, then format.** `SetCellValue` / `SetCellFormula` define the value; use `GetCell(row, col)->SetNumberFormat(...)` / `SetBold(...)` to style. Number formats are factories (`NumberFormat::Currency`, `NumberFormat::Percentage`).
- **Call `Recalculate()` after setting formulas** when you need results immediately (e.g. right after seeding data); the engine recalculates dirty cells automatically during normal editing when auto-calculate is enabled.
- **Mind coordinate systems.** The API is 0-based `(row, col)`; formula text and `CellAddress::ToString()` are 1-based A1 notation (`SetCellFormula(9, 1, "=SUM(B2:B8)")`).
- **Prefer the auto-detecting loaders for a quick "Open"** (`LoadFromFile`, single-arg `LoadCSV`); use the `*WithOptions` variants plus the import/export dialogs when the user needs to choose charset, separators, or quoting.
- **Always check the return value and `GetLastError()`** after any `Load*`/`Save*` call to surface a clear reason to the user.
- **Let the grid size the columns after an import.** The loaders already call `RequestAutoFitForUnsizedColumns()`; call `AutoFitUnsizedColumns()` yourself only after filling a sheet from code.
- **Use sheet-level objects for bulk work.** `GetActiveSheet()` / `GetSheet(index)` expose `SpreadsheetSheet`, which has range, column/row, merge, freeze, sort, filter, and find/replace operations directly.

## See Also

- [`Apps/DemoApp/UltraCanvasSpreadsheetExamples.cpp`](../../Apps/DemoApp/UltraCanvasSpreadsheetExamples.cpp) - Full working demo (sample data, Open/Import/Save flows)
- [`include/UltraCanvasSpreadsheet.h`](../../UltraCanvas/include/UltraCanvasSpreadsheet.h) - Main widget element
- [`include/UltraCanvasSpreadsheetSheet.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetSheet.h) - `SpreadsheetSheet` worksheet API
- [`include/UltraCanvasSpreadsheetCell.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetCell.h) - `SpreadsheetCell` value and formatting API
- [`include/UltraCanvasSpreadsheetTypes.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetTypes.h) - Addresses, ranges, styles, number formats, enums
- [`include/UltraCanvasSpreadsheetFormatMenu.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetFormatMenu.h) - Cell formatting menu and number-format presets
- [`include/UltraCanvasSpreadsheetMetrics.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetMetrics.h) - Document length conversion and content-width estimation
- [`include/UltraCanvasSpreadsheetFormula.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetFormula.h) - Formula parser/evaluator engine
