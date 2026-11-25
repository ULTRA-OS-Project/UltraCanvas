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
- [Named ranges](#named-ranges)
- [Formula language and functions](#formula-language-and-functions)
- [Column widths](#column-widths)
- [Cell formatting menu](#cell-formatting-menu)
- [Function menu](#function-menu)
- [Sorting from the context menu](#sorting-from-the-context-menu)
- [Fill handle](#fill-handle)
- [Pointing at cells in a formula](#pointing-at-cells-in-a-formula)
- [Sheet tab menu](#sheet-tab-menu)
- [Sorting a selection from the header](#sorting-a-selection-from-the-header)
- [Data validation](#data-validation)
- [Conditional formats](#conditional-formats)
- [Comments and links](#comments-and-links)
- [Best Practices](#best-practices)
- [Printing and PDF](#printing-and-pdf)
- [Charts](#charts)
- [Copy and paste with documents and slides](#copy-and-paste-with-documents-and-slides)
- [Accessibility](#accessibility)
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
- **Charts**: Columns, bars, lines, areas, pies, scatter and radar charts made from the selection, anchored to cells, their series read from the cells whenever they change; moved and resized with handles, kept in `.xlsx` and `.ods`, printed (see [Charts](#charts))
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

void  MoveActiveCell(int rowDelta, int colDelta);     // selects the cell it lands on (the arrows, Enter, Tab)
void  ExtendSelectionBy(int rowDelta, int colDelta);  // Shift+arrow: the far corner moves, the active cell stays
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

A cell's text is drawn centred in its row, on the middle of its capitals,
whatever the font. `VerticalAlignment::Top` draws it at the top of the row
instead. `Bottom` is every cell's default (as in .xlsx and .ods, which store
no alignment for it), so a cell set to it cannot be told from an untouched
one and is drawn centred like `Middle`.

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

Inserting or deleting rows or columns moves the references in formulas with
the cells, on every sheet, as a spreadsheet does. Say two rows are inserted
above row 2:

- `=A2+$B$5` becomes `=A4+$B$7`. `$`-anchored references move too: the `$`
  stops a reference moving when it is copied, not when the sheet changes.
- `=SUM(A1:A5)` becomes `=SUM(A1:A7)`: a range across the insertion grows.
- `=Data.A3` on another sheet moves when the rows were inserted on Data.

Deleting rows moves the references below them up and shrinks the ranges
across them. A reference wholly inside the deleted rows becomes `#REF!`, so
`=A2*2` becomes `=#REF!*2`. Text in quotes, and everything else in the
formula, is kept as it was written. Workbook names covering the sheet move
with it (see [Named ranges](#named-ranges)). Each insert or delete is one undo step: undo puts back the
cells, row and column sizes, merges and the formulas on every sheet as they
were, deleted cells included, and redo does it again. For a formula
string of your own, the same rewrite is
`MoveFormulaReferencesForRowsColumns(formula, sheetName, onThatSheet, columns, at, count)`
(`UltraCanvasSpreadsheetSheet.h`).

### Clipboard, Undo/Redo, Formula Engine

Copy and Cut keep the cells whole for a paste in the same program and put
them on the system clipboard as other programs read them: the shown values
as tab-separated text, beside an HTML table. Paste takes the grid's own copy
while the system clipboard still holds it - a copied formula's references
move with it (`=B2*2` copied from B3 to B8 is `=B7*2`), a cut one's stay -
and otherwise another program's HTML table (spanned cells kept apart) or
tab-, comma- or semicolon-separated text, each value read as if typed.
Every paste is one undo step.

```cpp
void Cut();
void Copy();
void Paste();
void PasteSpecial(const PasteOptions& options);
std::string RangeAsText(const CellRange& range) const;   // what Copy puts on the clipboard
std::string RangeAsHtml(const CellRange& range) const;
bool PasteHtml(const std::string& html);                 // another program's cells, at the active cell
bool PasteText(const std::string& text);

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
// password-protected .xlsx (see UltraCanvasOfficeEncryption.md) or .ods
// (UltraCanvasOdfEncryption.md); other formats ignore it. The templates read
// as their workbooks: .ots as an .ods, .xltx and .xltm as an .xlsx.
bool LoadFromFile(const std::string& filePath, const std::string& password = std::string());
// With a password: a password-protected .xlsx or .xltx; any other format with
// one is refused. An .ots or .xltx is written as a template - the same
// package under the template's media type; an .xltm is refused.
bool SaveToFile(const std::string& filePath, const std::string& password = std::string());
// True for "ots", "xltx", "xltm" (an extension or a path), a free function.
bool IsSpreadsheetTemplateExtension(const std::string& extensionOrPath);

// `password` opens a password-protected .ods (LibreOffice's "Save with
// password", ODF 1.2 and 1.4 - see UltraCanvasOdfEncryption.md).
bool LoadODS(const std::string& filePath, const std::string& password = std::string());
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

The CSV writer's quoting is public in `UltraCanvasCSVImport.h`, for a writer
that streams rows it never holds at once (UltraDBAdmin's table export), so it
quotes exactly as the spreadsheet does:

```cpp
// One record: fields quoted as the options say (all, or those holding the
// separator, the quote or a line break; a quote inside doubled), then the
// options' line ending. UTF-8; CSVEncodeFromUtf8 turns it into the charset.
std::string CSVFormatRecord(const std::vector<std::string>& fields, const CSVExportOptions& opt);
```

#### What a workbook file keeps

Besides the cells, their styles, column widths, row heights and merges,
`.xlsx` and `.ods` both read and write what a sheet holds around them, so a
workbook opened and saved again comes back as it was:

| | `.xlsx` | `.ods` |
|---|---|---|
| Frozen rows and columns | the frozen `pane` of `sheetViews` | the split (mode 2) in `settings.xml` |
| Hidden rows and columns | `hidden="1"` | `table:visibility="collapse"` |
| Named ranges, the workbook's and a sheet's | `definedNames` (a sheet's with `localSheetId`) | `table:named-expressions` of the document and of the table |
| Auto-filter, and columns filtered to values | `autoFilter`, `_xlnm._FilterDatabase` | `table:database-range` with filter buttons |
| Print area | `_xlnm.Print_Area` | `table:print-ranges` |
| Rows / columns repeated on every page | `_xlnm.Print_Titles` | header rows / columns |
| Page setup, header and footer, page breaks | `printOptions`, `pageMargins`, `pageSetup`, `headerFooter`, `rowBreaks` / `colBreaks` | the page layout and master page in `styles.xml`, `fo:break-before` |
| Conditional formats | `conditionalFormatting`, their styles as `dxfs` in `styles.xml` | `calcext:conditional-formats`, their styles as common styles in `styles.xml` |
| Validation rules | `dataValidations` | `table:content-validations`, named by the cells |
| Comments | the comments part, and the legacy VML drawing Excel shows them with | `office:annotation` in the cell |
| Links | `hyperlinks` (an address through the sheet's relationships, a place as `location`) | a `text:a` in the cell's text |

Header and footer texts are kept in Excel's codes (`&P` page, `&N` pages,
`&A` sheet, `&F` file, `&D` date, `&T` time, `&&` an ampersand), which an
`.ods` holds as text fields. A workbook name whose range names no sheet
(it covers the sheet of the formula using it) has no such form in Excel: an
`.xlsx` gives it the first sheet.

```cpp
auto grid = std::make_shared<UltraCanvasSpreadsheet>("Report", 0, 0, 900, 600);
SpreadsheetSheet* report = grid->GetActiveSheet();
report->FreezeRows(1);
report->SetColumnHidden(4, true);                        // column E
report->SetAutoFilter(CellRange(0, 0, 99, 5));           // A1:F100
grid->AddNamedRange("Amounts", CellRange(1, 2, 99, 2), report->GetName());   // scoped to the sheet

PrintSettings& print = report->GetPrintSettings();
print.orientation = PageOrientation::Landscape;
print.paperSize = PaperSize::A4;
print.repeatRowsFirst = 0;                               // row 1 on every page
print.repeatRowsLast = 0;
print.footerCenter = "Page &P of &N";
report->SetPrintArea(CellRange(0, 0, 99, 5));
grid->SaveToFile("report.xlsx");                         // or "report.ods": the same survives
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

`Recalculate()` calculates every formula whose result may have changed since
the last call: formulas set or changed, formulas reading a cell that changed,
directly or through other formulas, on any sheet, the formulas that name a
range, and the volatile ones (`NOW`, `TODAY`, `RAND`, `RANDBETWEEN`). It
finds the changes itself, however a cell was changed: through the grid
(typing, paste, fill, clear, sort, undo) or by writing into a sheet directly
(`GetActiveSheet()->SetCellValue(...)`). A change that has not been
recalculated yet is calculated before the grid draws. Inserting or deleting
rows or columns recalculates everything; so does `RecalculateAll()`.

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

## Named ranges

A workbook name stands for a range in formulas (`=SUM(Amounts)`). It is
visible from every sheet, or, given a `sheetScope`, only from that sheet:

```cpp
CellRange rates(0, 0, 0, 0);                         // A1
rates.start.sheetName = rates.end.sheetName = "Settings";
grid->AddNamedRange("Rate", rates);                  // every sheet: Settings.A1

grid->AddNamedRange("Total", CellRange(9, 1, 9, 1), "Jan");   // on Jan: Jan.B10
grid->AddNamedRange("Total", CellRange(9, 1, 9, 1), "Feb");   // on Feb: Feb.B10

CellRange r = grid->ResolveNamedRange("Total", "Feb");        // what =Total means on Feb
bool both = grid->HasNamedRange("Total", "Jan") && grid->HasNamedRange("Total", "Feb");
grid->RemoveNamedRange("Total", "Jan");                       // that one only
```

- **One name per scope.** The same name can exist once for the workbook and
  once for each sheet, as Excel files often have ("Total" on each month's
  sheet).
- **Lookup from a formula.** A formula looks for the name scoped to its own
  sheet first, then for the workbook's. It never sees a name scoped to
  another sheet; that gives `#NAME?`, as a name that does not exist does.
- **Case.** Names are matched without regard to case.
- **Which sheet a name covers.** It covers the sheet its range names
  (`range.start.sheetName`). A scoped name whose range names none covers its
  own sheet. A workbook name whose range names none covers the sheet of the
  formula using it.
- **Following the sheet.** Renaming a sheet renames the scope and the covered
  sheet of its names. Inserting or deleting rows or columns moves the names
  covering that sheet.
- **Deleting a sheet.** The names scoped to it go with it. Workbook names
  covering it stay, and a formula using one shows `#REF!` (`#NAME?` is for a
  name that does not exist). Undoing the delete brings both back.

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

## Data validation

A rule says what a cell may hold. It is set on a range of the sheet, or on one
cell, and is checked when someone **types** into the cell and commits the
entry (Enter, Tab, a click elsewhere). Values set from code, pasted or filled
are not checked; `SpreadsheetSheet::ValidateCell` checks any cell on demand.

```cpp
DataValidation quantity;
quantity.type = ValidationType::WholeNumber;     // also Decimal, Date, Time, TextLength, List
quantity.op = ValidationOperator::Between;
quantity.formula1 = "1";                         // dot-decimal; a Date bound may be "2026-01-31",
quantity.formula2 = "99";                        // a Time bound "17:30"
quantity.alertStyle = ValidationAlertStyle::Stop;
quantity.errorTitle = "Quantity";
quantity.errorMessage = "Order between 1 and 99 pieces.";
grid->GetActiveSheet()->SetValidation(CellRange(1, 2, 50, 2), quantity);   // C2:C51

DataValidation status;
status.type = ValidationType::List;
status.formula1 = "Open, In progress, Done";     // or ODF's "\"Open\";\"Done\""
grid->GetActiveSheet()->SetValidation(CellRange(1, 3, 50, 3), status);     // D2:D51
```

An entry that breaks its rule is answered as the rule's `alertStyle` says,
in a dialog titled with `errorTitle` that shows `errorMessage` (or a default):

| Alert style | Buttons | What they do |
|---|---|---|
| `Stop` | Retry, Cancel | Retry opens the entry again on its cell, all of it selected; Cancel puts the old value back. A Stop rule never keeps the entry. |
| `Warning` | Yes, No, Cancel | Yes keeps it; No edits it again; Cancel puts the old value back. |
| `Information` | OK, Cancel | OK keeps it; Cancel puts the old value back. |

A refused entry leaves no undo step. A rule with `showErrorAlert = false`
keeps whatever is typed, and blanks pass while `allowBlank` is true (the
default).

A `Custom` rule is a formula that must come out true. It is written for the
rule's top-left cell and moved to each other cell as a copied formula is
(`=K1>A1` on K1:K5 checks `=K3>A3` in K3). A bound of the other rules may be
a formula too (`=$F$1`), worked out when the cell is checked:

```cpp
DataValidation later;
later.type = ValidationType::Custom;
later.formula1 = "=B2>A2";                       // the end after the start, row by row
grid->GetActiveSheet()->SetValidation(CellRange(1, 1, 50, 1), later);      // B2:B51

DataValidation upTo;
upTo.type = ValidationType::Decimal;
upTo.op = ValidationOperator::LessOrEqual;
upTo.formula1 = "=$F$1";                         // no more than F1 holds
grid->GetActiveSheet()->SetValidation(CellRange(1, 4, 50, 4), upTo);       // E2:E51
```

To ask in your own way, set `onValidationFailed`. It replaces the dialog, and
`answer` may be called at once or later, exactly once:

```cpp
grid->onValidationFailed = [statusLabel](const UltraCanvasSpreadsheet::ValidationFailure& failure,
                                         std::function<void(UltraCanvasSpreadsheet::ValidationAnswer)> answer) {
    statusLabel->SetText(failure.message);       // also failure.row, col, enteredText, rule
    answer(UltraCanvasSpreadsheet::ValidationAnswer::Retry);   // or Accept, Cancel
};
```

A `List` rule with `showDropdown` (the default) puts a small arrow just right
of the active cell. A click on it, or Alt+Down, opens the rule's items with
the cell's current value ticked; the one picked is entered as if typed, as one
undo step. From code:

```cpp
std::vector<std::string> items = grid->GetValidationListItems(1, 3);   // {"Open", "In progress", "Done"}
grid->ChooseValidationListItem(1, 3, "Done");                          // false for an item not offered
```

A list rule whose `formula1` starts with `=` names cells instead
(`IsRangeListSource`): `"=$G$1:$G$9"`, `"='Lists'.A1:A20"` or a named range.
Its items are the cells' values, read when the arrow opens or an entry is
checked, so the list follows the cells:

```cpp
DataValidation region;
region.type = ValidationType::List;
region.formula1 = "=$G$1:$G$3";                  // North, South, East in G1:G3
grid->GetActiveSheet()->SetValidation(CellRange(1, 0, 50, 0), region);
std::vector<std::string> regions = grid->GetActiveSheet()->GetValidationListItems(1, 0);
```

`.xlsx` and `.ods` read and write the rules (see *What a workbook file
keeps*), and `ShowDataValidationDialog()` edits the selection's rule:
what is allowed, the condition and its values, blanks, the list arrow, and
how a wrong entry is answered (`SetSelectionValidation` from code; a rule of
type `Any` removes it).

## Conditional formats

A conditional format changes how the cells of a range look by what they
hold. The sheet keeps them (`SpreadsheetSheet::AddConditionalFormat`,
`SetConditionalFormats`), and `UltraCanvasSpreadsheet::PaintCell` draws them,
so the grid, the printed page and the PDF all show them. A format is a range
and its rules:

| Rule (`ConditionalFormatType`) | Holds when | Settings |
|---|---|---|
| `CellValue` | the value compares with `formula1` (and `formula2`) by `op` | a number, `"quoted text"` or a formula (`=$B$1`) |
| `Expression` | the formula in `formula1` is true, moved to each cell from the range's top-left one | `=E1>$F$1` |
| `ContainsText`, `NotContainsText`, `BeginsWithText`, `EndsWithText` | the shown text, ignoring case | the text in `formula1` |
| `ContainsBlanks`, `NotContainsBlanks`, `ContainsErrors`, `NotContainsErrors` | the cell is empty / shows an error | |
| `Top10` | among the highest (`bottom`: lowest) `formula1` values, or percent of them (`percent`) | |
| `AboveAverage` | above the range's average (`aboveAverage` false: below; `equalAverage`: or equal) | |
| `DuplicateValues`, `UniqueValues` | the text occurs more than once / once in the range | |
| `ColorScale` | a number: the cell filled with `colorScale`'s two or three colours, mixed by where the value lies | `thresholds` (default lowest, 50th percentile, highest) |
| `DataBar` | a number: a bar of `dataBarColor` from the left edge, as long as the value is far between the lowest and the highest | `thresholds`, `showValue` |

A rule's `style` lays only what it sets over the cell's own
(`ApplyConditionalStyle`): a fill, a font colour other than black, bold,
italic, underline, strikethrough. Rules are tried by `priority` (lower first;
one without a priority after those with one), then in order; the first
style that holds wins, a colour scale and a data bar draw beside it, and
`stopIfTrue` ends the search.

```cpp
SpreadsheetSheet* sheet = grid->GetActiveSheet();

ConditionalFormatRule over;
over.type = ConditionalFormatType::CellValue;
over.op = ValidationOperator::GreaterThan;
over.formula1 = "1000";
over.style.fill = CellFill::Solid(Color(255, 199, 206));
over.style.font.color = Color(156, 0, 6);

ConditionalFormat amounts;
amounts.range = CellRange(1, 2, 99, 2);                          // C2:C100
amounts.rules.push_back(over);
sheet->AddConditionalFormat(amounts);

ConditionalFormatRule scale;
scale.type = ConditionalFormatType::ColorScale;
scale.colorScale = {Color(248, 105, 107), Color(255, 235, 132), Color(99, 190, 123)};
ConditionalFormat heat;
heat.range = CellRange(1, 3, 99, 3);                             // D2:D100
heat.rules.push_back(scale);
sheet->AddConditionalFormat(heat);

ConditionalCellFormat look = sheet->GetConditionalFormatting(4, 2);   // what C5 is drawn with
if (look.style) { /* the rule's style */ }
if (look.fill) { /* a colour scale's colour */ }
```

The figures a colour scale, a data bar, top values and duplicates need (the
lowest, the highest, the sorted values) are worked out once per change of the
sheet's cells, not per cell drawn. `ShowConditionalFormatDialog()` lists the
rules over the selection, adds one (a value comparison, a formula, text,
blanks, errors, top or bottom values, above or below the average,
duplicates, unique values, a two- or three-colour scale, a data bar) and
changes or deletes the one picked; `AddSelectionConditionalFormat` and
`ClearSelectionConditionalFormats` do the same from code. `.xlsx` and `.ods`
read and write them; an icon set or a date rule is not drawn and not kept.

A change made through the grid - its calls or its dialogs - is one undo step,
as a typed value is; the sheet's own calls above are not. On the grid, a
format is added with `AddConditionalFormat`, and a rule of the active sheet's
format (in `GetConditionalFormats()` order) changed or deleted by its
position, the format going with its last rule:

```cpp
ConditionalFormatRule over;
over.type = ConditionalFormatType::CellValue;
over.op = ValidationOperator::GreaterThan;
over.formula1 = "1000";
over.style.font.bold = true;

ConditionalFormat flagged;
flagged.range = CellRange(1, 4, 99, 4);                          // E2:E100
flagged.rules.push_back(over);
grid->AddConditionalFormat(flagged);                             // one undo step
over.formula1 = "5000";
grid->SetConditionalFormatRule(0, 0, over);                      // false when there is no such rule
grid->RemoveConditionalFormatRule(0, 0);
grid->Undo();                                                    // the rule is back
```

`SetSelectionValidation` is an undo step in the same way, and its undo gives
back the cells' own rules the range's replaced.

## Comments and links

A cell's comment (`CellComment`: text, author) is marked by a red corner
at the cell's top right, and resting the pointer on the cell shows it. A
link (`CellHyperlink`) draws its cell's text blue and underlined (unless the
cell has a colour of its own); the pointer resting on it shows where it goes,
and Ctrl+click opens it - a web or mail address through the system
(`OpenURL`), a place in the workbook (`HyperlinkType::InternalCell`,
`"Sheet2.B4"`) by selecting it. Setting `onOpenHyperlink` takes the opening
over (to ask first, or in a test).

```cpp
grid->SetCellComment(0, 0, "Checked against the March statement", "Ann");   // an undo step
grid->SetCellHyperlink(1, 0, CellHyperlink("https://example.com/report"));
grid->SetCellHyperlink(2, 0, CellHyperlink("Summary.A1", HyperlinkType::InternalCell));
grid->OpenHyperlink(2, 0);                       // goes to Summary!A1; false for a cell without a link
grid->RemoveCellComment(0, 0);
```

A link set on an empty cell gives it the address as its text.
`ShowCommentDialog()` and `ShowHyperlinkDialog()` edit the active cell's
(each with a button that removes it); both are kept by `.xlsx` (with the VML
drawing Excel needs to show a comment) and `.ods`.

## Best Practices

- **Create with the factory.** Use `CreateSpreadsheetElement(...)` so the element is a `shared_ptr` (matching the rest of the framework). Capture that `shared_ptr` in async callbacks (file dialogs) so the grid stays alive, and call `RequestRedraw()` after mutating it from a callback.
- **Set values, then format.** `SetCellValue` / `SetCellFormula` define the value; use `GetCell(row, col)->SetNumberFormat(...)` / `SetBold(...)` to style. Number formats are factories (`NumberFormat::Currency`, `NumberFormat::Percentage`).
- **Call `Recalculate()` after writing into a sheet directly** when you need the results at once (e.g. right after seeding data). The grid's own setters (`SetCellValue`, `SetCellFormula`, ...) recalculate as they go while auto-calculate is enabled.
- **Mind coordinate systems.** The API is 0-based `(row, col)`; formula text and `CellAddress::ToString()` are 1-based A1 notation (`SetCellFormula(9, 1, "=SUM(B2:B8)")`).
- **Prefer the auto-detecting loaders for a quick "Open"** (`LoadFromFile`, single-arg `LoadCSV`); use the `*WithOptions` variants plus the import/export dialogs when the user needs to choose charset, separators, or quoting.
- **Always check the return value and `GetLastError()`** after any `Load*`/`Save*` call to surface a clear reason to the user.
- **Let the grid size the columns after an import.** The loaders already call `RequestAutoFitForUnsizedColumns()`; call `AutoFitUnsizedColumns()` yourself only after filling a sheet from code.
- **Rules live on the sheet, the dialogs on the grid.** Conditional formats and validation rules are `SpreadsheetSheet`'s (`AddConditionalFormat`, `SetValidation`); comments and links are a cell's. The grid's `Show...Dialog` calls edit them for the selection.
- **Use sheet-level objects for bulk work.** `GetActiveSheet()` / `GetSheet(index)` expose `SpreadsheetSheet`, which has range, column/row, merge, freeze, sort, filter, and find/replace operations directly.

## Printing and PDF

`SpreadsheetPrintPages` (`UltraCanvasSpreadsheetPrint.h`) cuts a sheet into
pages and draws them, for a PDF file and for printers. It prints the print
area, or every cell in use, and follows the sheet's `PrintSettings`:

| Setting | What it does on paper |
|---|---|
| `paperSize`, `orientation` | the page; Custom prints on Letter for now |
| `marginTop` / `Bottom` / `Left` / `Right` (inches) | the room for cells |
| `scale` (%), or `fitToWidth` / `fitToHeight` (pages) | the size; fitting never enlarges |
| `rowBreaks`, `colBreaks` | a page starts at each of these rows (columns) |
| `printGridlines`, `printRowColHeaders` | lines between the cells; column letters and row numbers |
| `centerHorizontally`, `centerVertically` | the block of cells centred in the room |
| `headerLeft` ... `footerRight`, `headerMargin`, `footerMargin` | lines in the margins; `&P` the page, `&N` the pages, `&A` the sheet, `&D` the date, `&&` an ampersand |

Pages run down, then across. Hidden rows and columns are left out, a merged
block is drawn once per page it reaches, and every cell is drawn by
`UltraCanvasSpreadsheet::PaintCell`, the grid's own painter, so paper shows
what the screen does: 100 % on paper is the size on screen at 100 %.
`SetTitleRows` / `SetTitleColumns` repeat lines on every page after the ones
they are on.

<!-- doc-check: std::shared_ptr<UltraCanvasSpreadsheet> grid; UltraCanvasWindow* window; -->
```cpp
#include "UltraCanvasSpreadsheetPrint.h"
#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"

grid->GetPrintSettings().orientation = PageOrientation::Landscape;
grid->GetPrintSettings().fitToWidth = 1;          // every column across one page
grid->SetPrintArea(CellRange(0, 0, 49, 7));      // A1:H50

auto pages = CreateSpreadsheetPrintPages(*grid);  // the sheet on show, copied
pages->SetTitleRows(0, 0);                        // row 1 on every page
std::string error;
pages->ExportPdf("Budget.pdf", error);

// Printing: the PDF for printers that take one, the pages for those that draw.
std::vector<uint8_t> pdf;
if (pages->ExportPdf(pdf, error))
    PrintDocumentWithDialog("Budget", pdf, "application/pdf", window, pages);
```

## Charts

A sheet holds charts (`UltraCanvasSpreadsheetChart.h`). A `SpreadsheetChart`
is anchored to cells - its top-left corner some pixels into one cell, its
bottom-right into another, as .xlsx's two-cell anchor keeps it - and holds a
`RichChart`, the chart model documents and decks use, whose series are
ranges of cells: each series' name, its categories (a scatter series' x
values) and its values (`SpreadsheetChartSeries`, ranges as
`SpreadsheetChartRange`: a sheet's name and a `CellRange`). It is drawn by
the chart engine (`DrawRichDocumentChart`) over the cells, moves with them
when the grid scrolls or rows and columns are inserted above or left of it,
and prints where it stands.

| Kind (`RichChart::Kind`) | Offered by *Insert chart* |
|---|---|
| `Column`, `Bar` (side by side or `stacked`) | Columns, Columns stacked, Bars, Bars stacked |
| `Line`, `Area` (or stacked) | Line, Area, Area stacked |
| `Pie`, `Doughnut` | the first series, its values as slices named by the categories |
| `Scatter` | each series' points over the first line of numbers as x |
| `Radar` | the categories as spokes |

A file's bubble and stock charts are read and drawn too, from the values the
file keeps with them.

`InsertChartFromSelection(kind, stacked, seriesIn)` makes one from the
selection as a spreadsheet does (`MakeSpreadsheetChart`): a first row of
words names the series, a first column of words (or an empty corner) holds
the categories, and the series run down the columns when the block is at
least as tall as it is wide (`SpreadsheetChartSeriesIn::Automatic`), else
along the rows. It returns -1, and inserts nothing, when the selection holds
no number. A single selected cell stands for the block of cells in use
around it.

On the grid a click selects a chart, a drag moves it, its eight handles
resize it, Delete (or Backspace) removes it and the arrow keys nudge it (ten
pixels with Shift); a double-click opens `ShowChartPropertiesDialog` - the
kind, the title, the legend and, for a chart made from a block, whether the
series run along the rows - unless `onChartDoubleClick` is set. Inserting,
changing, moving, resizing and deleting are each one undo step
(`UndoActionType::ChartChange`).

The data is read from the cells again whenever they change: the grid marks
its charts stale when a sheet reports a change and reads them before it
draws and before it saves (`RefreshCharts`, which also recalculates the
formulas waiting). A chart whose ranges name a sheet reads it from any sheet;
renaming that sheet renames the ranges.

| Format | How a chart is kept |
|---|---|
| .xlsx | `xl/drawings/drawingN.xml` (an `xdr:twoCellAnchor` holding a `graphicFrame`), its relationships, and `xl/charts/chartN.xml` written by `WriteDrawingMLChart` with the series as `c:f` references (`'Sales Data'!$B$2:$B$7`) and their cached values; a one-cell anchor is read too |
| .ods | a `draw:frame` in the sheet's `table:shapes` naming an object `Object N/` whose `content.xml` is a `chart:chart` (`WriteOdfChartObject`) with `chart:values-cell-range-address` and the categories' and names' ranges into the sheets; a frame LibreOffice anchors in a cell is read too |

<!-- doc-check: std::shared_ptr<UltraCanvasSpreadsheet> grid; -->
```cpp
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetChart.h"

// Months in A2:A7, two regions' sales in B2:C7, their names in B1:C1.
grid->Select(CellRange(0, 0, 6, 2));
int index = grid->InsertChartFromSelection(RichChart::Kind::Column);   // one undo step
if (index >= 0) {
    SpreadsheetChart chart = *grid->GetChart(index);
    chart.chart.title = "Sales";
    chart.chart.legend = true;
    // Six columns wide from G2, ten pixels into it.
    chart.anchor.from = CellAddress(1, 6);
    chart.anchor.fromOffsetX = 10;
    chart.anchor.to = CellAddress(16, 12);
    grid->SetChart(index, chart);                                   // one undo step
}

grid->SetCellValue(3, 1, 120.0);   // the chart shows it the next time it is drawn
grid->RefreshCharts();             // ...or now
const RichChart& data = grid->GetChart(0)->chart;   // series[0].values[2] == 120

grid->onChartSelectionChange = [](int selected) { /* -1: none */ };
```

A chart of any block, placed and kept by hand:

<!-- doc-check: SpreadsheetSheet& sheet; std::shared_ptr<UltraCanvasSpreadsheet> grid; -->
```cpp
SpreadsheetChart pie = MakeSpreadsheetChart(sheet, CellRange(0, 0, 6, 1), RichChart::Kind::Pie);
pie.anchor = SpreadsheetChartAnchorForBox(sheet, Rect2Di(400, 40, 360, 260));   // sheet pixels
grid->InsertChart(pie);

// The ranges' two spellings.
SpreadsheetChartRange range;
SpreadsheetChartRange::FromExcel("'Sales Data'!$B$2:$B$7", range);
std::string odf = range.ToOdf();   // 'Sales Data'.$B$2:.$B$7
```

## Copy and paste with documents and slides

`Copy()` puts the selected cells on the system clipboard as an HTML table and
tab-separated text: a document (`UltraCanvasRichTextEdit`) pastes them as a
table, a slide (`UltraCanvasSlideEdit`) as a table shape. With a chart
selected (`SelectChart`), `Copy()` and `Cut()` take the chart instead: it is
kept on the office clipboard (`CopyChartToClipboard`,
`UltraCanvasOfficeClipboard.h`) and pastes into a document or onto a slide as
a chart holding a static copy of its data, while other programs get a picture
of it (HTML) and its data as text.

```cpp
grid->SelectChart(0);
grid->Copy();                                   // the chart, not the cells
RichChart chart;
int width = 0, height = 0;
bool held = GetClipboardChart(chart, width, height);   // true until something else is copied
```

## Accessibility

The grid is a table to a screen reader, named by its sheet. Its rows and
cells are parts, and it answers as a table (`GetAccessibleTableInterface()`):
a cell is named by its address and the value it shows ("B3 42"), the active
cell is the focused cell and the grid's value text, and a move of it or of the
selection is announced (`SelectionChanged`, with the cell as the event's
`part`). See
[UltraCanvasAccessibility](UltraCanvasAccessibility.md#parts-an-element-draws-itself).

```cpp
IAccessibleTable* table = grid->GetAccessibleTableInterface();
std::string cell = table->GetCell(2, 1).name;   // "B3 42"
```

## See Also

- [`Apps/DemoApp/UltraCanvasSpreadsheetExamples.cpp`](../../Apps/DemoApp/UltraCanvasSpreadsheetExamples.cpp) - Full working demo (sample data, Open/Import/Save flows)
- [`include/UltraCanvasSpreadsheet.h`](../../UltraCanvas/include/UltraCanvasSpreadsheet.h) - Main widget element
- [`include/UltraCanvasSpreadsheetSheet.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetSheet.h) - `SpreadsheetSheet` worksheet API
- [`include/UltraCanvasSpreadsheetCell.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetCell.h) - `SpreadsheetCell` value and formatting API
- [`include/UltraCanvasSpreadsheetTypes.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetTypes.h) - Addresses, ranges, styles, number formats, enums
- [`include/UltraCanvasSpreadsheetFormatMenu.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetFormatMenu.h) - Cell formatting menu and number-format presets
- [`include/UltraCanvasSpreadsheetMetrics.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetMetrics.h) - Document length conversion and content-width estimation
- [`include/UltraCanvasSpreadsheetFormula.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetFormula.h) - Formula parser/evaluator engine
- [`include/UltraCanvasSpreadsheetPrint.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetPrint.h) - A sheet's pages for PDF and printers
- [`include/UltraCanvasSpreadsheetChart.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetChart.h) - Charts on a sheet, anchored to cells, their series ranges of cells
