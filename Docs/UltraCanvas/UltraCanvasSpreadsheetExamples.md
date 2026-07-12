# UltraCanvasSpreadsheet Documentation

## Overview

**UltraCanvasSpreadsheet** is a full-featured, editable spreadsheet grid component. It supports multiple worksheets, sparse cell storage, an OpenFormula-compatible formula engine, rich per-cell formatting (fonts, fills, borders, number formats), merged cells, freeze panes, sorting/filtering, find/replace, undo/redo, and file import/export for OpenDocument (`.ods`) and CSV/TSV. Each cell is strongly typed (text, number, boolean, date/time, currency, percentage, error, or formula) and rendered with a built-in formula bar, sheet tabs, scrollbars, and row/column headers.

**Version:** 1.0.0
**Header:** `include/UltraCanvasSpreadsheet.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement`

## Features

- **Editable Grid**: Click-to-edit cells with an in-cell editor and a formula bar
- **Multiple Worksheets**: Add, insert, remove, rename, move, and copy sheets with tabs
- **Typed Cells**: Text, Number, Boolean, Date/Time, Currency, Percentage, Error, and Formula values
- **Formula Engine**: OpenFormula-compatible parser/evaluator (`=SUM(C2:C8)`, `=SUM(D2:D8)/SUM(C2:C8)`, etc.) with automatic recalculation and circular-reference detection
- **Cell Formatting**: Fonts (bold/italic/underline), fills, borders, alignment, and number formats (currency, percentage, scientific, date/time)
- **Layout Control**: Per-column widths, per-row heights, hidden rows/columns, merged cells, and freeze panes
- **Data Tools**: Sorting, auto-filter, find/replace, named ranges, and data validation
- **Clipboard & Undo**: Cut/Copy/Paste (including Paste Special) and multi-level Undo/Redo
- **File I/O**: Load/Save OpenDocument (`.ods`) and CSV/TSV, with auto-detection or explicit import/export options
- **Bundled Demo File + Open Flow**: On entry the demo opens the bundled `media/docs/spreadsheet.ods` document (a monthly sales / chargeback report with live `SUM` totals), falling back to a formatted sample sheet if the file is missing, and provides "Open Spreadsheet File…", "Import CSV…", and "Save…" buttons driven by `UltraCanvasFileLoader`

## Header Include

```cpp
#include "UltraCanvasSpreadsheet.h"
```

## Class Reference

### Constructors and Factory

```cpp
UltraCanvasSpreadsheet();
UltraCanvasSpreadsheet(const std::string& id, float x, float y, float width, float height);

// Preferred: UI elements are created as shared_ptr.
std::shared_ptr<UltraCanvasSpreadsheet> CreateSpreadsheetElement(
    const std::string& id, float x, float y, float width, float height);

std::shared_ptr<UltraCanvasSpreadsheet> CreateSpreadsheetElement(
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
void  RenameSheet(int index, const std::string& newName);
void  MoveSheet(int fromIndex, int toIndex);
SpreadsheetSheet* CopySheet(int index, const std::string& newName = "");
std::vector<std::string> GetSheetNames() const;
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
```

### Rows, Columns, Merge, and Freeze

```cpp
int  GetColumnWidth(int col) const;
void SetColumnWidth(int col, int width);
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

### File Operations

```cpp
// Format chosen automatically from the file extension.
bool LoadFromFile(const std::string& filePath);
bool SaveToFile(const std::string& filePath);

bool LoadODS(const std::string& filePath);
bool SaveODS(const std::string& filePath);

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
```

### SpreadsheetCell

`GetCell(row, col)` returns a `SpreadsheetCell*` exposing typed setters, formatting, and getters.

```cpp
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
```

### NumberFormat factories (`UltraCanvasSpreadsheetTypes.h`)

```cpp
NumberFormat::General();
NumberFormat::Number(int decimals = 2, bool thousands = false);
NumberFormat::Currency(const std::string& symbol = "$", int decimals = 2,
                       bool symbolAfter = false);   // symbolAfter => "1,234.00 €"
NumberFormat::Percentage(int decimals = 0);
NumberFormat::Date(const std::string& format = "YYYY-MM-DD");
NumberFormat::Time(const std::string& format = "HH:MM:SS");
NumberFormat::Scientific(int decimals = 2);
NumberFormat::Text();
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

Wire an "Open" button to a file dialog and load via `LoadFromFile` (format auto-detected from the extension). On failure, report `GetLastError()`. Capture `shared_ptr`s so the grid and label stay valid for the async callback.

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

## Best Practices

- **Create with the factory.** Use `CreateSpreadsheetElement(...)` so the element is a `shared_ptr` (matching the rest of the framework). Capture that `shared_ptr` in async callbacks (file dialogs) so the grid stays alive, and call `RequestRedraw()` after mutating it from a callback.
- **Set values, then format.** `SetCellValue` / `SetCellFormula` define the value; use `GetCell(row, col)->SetNumberFormat(...)` / `SetBold(...)` to style. Number formats are factories (`NumberFormat::Currency`, `NumberFormat::Percentage`).
- **Call `Recalculate()` after setting formulas** when you need results immediately (e.g. right after seeding data); the engine recalculates dirty cells automatically during normal editing when auto-calculate is enabled.
- **Mind coordinate systems.** The API is 0-based `(row, col)`; formula text and `CellAddress::ToString()` are 1-based A1 notation (`SetCellFormula(9, 1, "=SUM(B2:B8)")`).
- **Prefer the auto-detecting loaders for a quick "Open"** (`LoadFromFile`, single-arg `LoadCSV`); use the `*WithOptions` variants plus the import/export dialogs when the user needs to choose charset, separators, or quoting.
- **Always check the return value and `GetLastError()`** after any `Load*`/`Save*` call to surface a clear reason to the user.
- **Use sheet-level objects for bulk work.** `GetActiveSheet()` / `GetSheet(index)` expose `SpreadsheetSheet`, which has range, column/row, merge, freeze, sort, filter, and find/replace operations directly.

## See Also

- [`Apps/DemoApp/UltraCanvasSpreadsheetExamples.cpp`](../../Apps/DemoApp/UltraCanvasSpreadsheetExamples.cpp) - Full working demo (sample data, Open/Import/Save flows)
- [`include/UltraCanvasSpreadsheet.h`](../../UltraCanvas/include/UltraCanvasSpreadsheet.h) - Main widget element
- [`include/UltraCanvasSpreadsheetSheet.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetSheet.h) - `SpreadsheetSheet` worksheet API
- [`include/UltraCanvasSpreadsheetCell.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetCell.h) - `SpreadsheetCell` value and formatting API
- [`include/UltraCanvasSpreadsheetTypes.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetTypes.h) - Addresses, ranges, styles, number formats, enums
- [`include/UltraCanvasSpreadsheetFormula.h`](../../UltraCanvas/include/UltraCanvasSpreadsheetFormula.h) - Formula parser/evaluator engine
