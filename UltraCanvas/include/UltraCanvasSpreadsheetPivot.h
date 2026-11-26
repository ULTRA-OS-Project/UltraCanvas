// include/UltraCanvasSpreadsheetPivot.h
// Pivot tables on a spreadsheet: a summary of a block of cells whose first
// row names its columns (the fields). Row fields and column fields put the
// distinct items of a column down the side and across the top, filter
// (page) fields leave records out, value fields summarise a column - sum,
// count, average, min, max, count of numbers, product, standard deviation,
// variance - for every combination, optionally shown as a share of the
// grand total, of its row or of its column. Subtotals, grand totals, the
// order of each field's items, which items show, and the grouping of dates
// (by year, quarter or month) and of numbers (by a step) are the pivot's
// own.
//
// The model is display-free: ComputeSpreadsheetPivot lays the summary out
// as cells, RefreshSpreadsheetPivot writes them onto the sheet the pivot
// stands on (and clears where it stood before). The grid keeps those cells
// read-only, draws them with a header style, refreshes a pivot when its
// source changes, and opens its field list (ShowSpreadsheetPivotDialog,
// UltraCanvasSpreadsheetPivotPanel) - see UltraCanvasSpreadsheet's PIVOT
// TABLES calls.
//
// A sheet keeps the pivots that stand on it (SpreadsheetSheet::GetPivotTables).
// .xlsx keeps a pivot as a pivot cache definition (its source and fields)
// with its records, and a pivot table part (its fields' places, items and
// value fields) related to the worksheet it stands on; .ods as a
// table:data-pilot-table naming its source range and fields. A pivot read
// from a file shows the cells the file holds; one the model cannot
// represent (a calculated field, a source in another workbook ...) keeps
// them and says why in `unsupportedReason`.
//
// doc-check: SpreadsheetSheet& data; SpreadsheetSheet& report;
// doc-check: example
//     // Region in A, product in B, sales in C, their names in row 1.
//     SpreadsheetPivotTable pivot = MakeSpreadsheetPivot(data, CellRange(0, 0, 40, 2));
//     pivot.rowFields.push_back(PivotField{0});          // Region down the side
//     pivot.columnFields.push_back(PivotField{1});       // Product across the top
//     pivot.valueFields.push_back(PivotValueField{2});   // Sum of Sales
//     pivot.target = CellAddress(0, 0);
//     RefreshSpreadsheetPivot(pivot, report, [&](const std::string&) { return &data; });
//
// Version: 1.0.0 - SpreadsheetPivotTable: row, column, filter and value fields, summaries, show-as, totals,
//                  sort, item filters, date and number grouping; computed into cells, drilled into
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSpreadsheetTypes.h"
#include "UltraCanvasSpreadsheetChart.h"   // SpreadsheetSheetLookup

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class SpreadsheetSheet;
class UltraCanvasSpreadsheet;
class UltraCanvasWindowBase;

// What a value field computes over the records of a cell.
enum class PivotSummary {
    Sum, Count, Average, Max, Min, Product, CountNumbers, StdDev, StdDevP, Var, VarP
};

// How a value field's results are shown.
enum class PivotShowAs { Value, PercentOfTotal, PercentOfRow, PercentOfColumn };

// How a row, column or filter field's values become items.
enum class PivotGroupBy { NoGrouping, Years, Quarters, Months, NumberRange };

struct PivotGrouping {
    PivotGroupBy by = PivotGroupBy::NoGrouping;
    // NumberRange: items of `step` from `start` (below it one "<start"
    // item) up to `end` (above it one ">end" item); automatic ends follow
    // the smallest and largest number.
    double start = 0.0;
    double end = 0.0;
    double step = 10.0;
    bool autoStart = true;
    bool autoEnd = true;
};

// A row, column or filter field.
struct PivotField {
    int column = 0;                        // in the source block, 0 = its first column
    bool descending = false;               // the items' order (by number, then by text)
    bool subtotals = true;                 // a total after each item's group (not the innermost field)
    std::vector<std::string> hiddenItems;  // the items left out, by key (PivotItem::key)
    PivotGrouping grouping;
};

// A value field.
struct PivotValueField {
    int column = 0;
    PivotSummary summary = PivotSummary::Sum;
    PivotShowAs showAs = PivotShowAs::Value;
    std::string name;                      // empty: "Sum of Sales"
};

struct SpreadsheetPivotTable {
    std::string name;                      // "PivotTable1"
    // The source: a block whose first row names the fields. `sourceName`,
    // where set, is a named range the block is taken from on refresh.
    std::string sourceSheet;
    CellRange sourceRange;
    std::string sourceName;
    // The top-left cell of the pivot on the sheet that holds it.
    CellAddress target;
    std::vector<PivotField> rowFields;
    std::vector<PivotField> columnFields;
    std::vector<PivotField> filterFields;
    std::vector<PivotValueField> valueFields;
    bool valuesInRows = false;             // several value fields: one under the other, not side by side
    bool grandTotalRow = true;             // a row of totals at the bottom (each column's)
    bool grandTotalColumn = true;          // a column of totals at the right (each row's)
    // As written last: the cells it fills on its sheet, and the names of
    // the source's columns.
    CellRange outputRange = CellRange(-1, -1, -1, -1);
    std::vector<std::string> fieldNames;
    // Read from a file with something the model cannot represent: its cells
    // are kept as the file had them, and refreshing it is refused.
    std::string unsupportedReason;

    bool HasOutput() const { return outputRange.start.row >= 0 && outputRange.end.row >= outputRange.start.row; }
    bool Covers(int row, int col) const {
        return HasOutput() && row >= outputRange.start.row && row <= outputRange.end.row &&
               col >= outputRange.start.col && col <= outputRange.end.col;
    }
};

// One distinct value of a field, as an item: `key` stays the same for the
// same value however it is shown ("East", "45292" for a date, "3" for the
// third month), `label` is what the cell shows ("East", "2024-01-01",
// "Mar"), `sortNumber` orders numbers, dates and groups.
struct PivotItem {
    std::string key;
    std::string label;
    bool isNumber = false;
    double sortNumber = 0.0;
    bool blank = false;
};

// What a cell of the pivot is.
enum class PivotCellKind {
    Caption,          // the value field's name over the table, a filter's selection
    FieldName,        // a row, column or filter field's name
    RowItem, ColumnItem,
    Value,
    SubtotalLabel, SubtotalValue,
    GrandTotalLabel, GrandTotalValue
};

struct PivotOutputCell {
    int row = 0;                           // from the pivot's target
    int col = 0;
    PivotCellKind kind = PivotCellKind::Value;
    bool isNumber = false;
    double number = 0.0;
    std::string text;
    bool percent = false;                  // a share, shown as a percentage
    int rowEntry = -1;                     // a value cell: which row and column of the table
    int colEntry = -1;
    int valueField = -1;
};

// The pivot laid out as cells. A row or column of the table (an entry) is
// a path of item indexes into the row (column) fields' items - shorter for
// a subtotal, empty for the grand total.
struct PivotOutput {
    int rows = 0;
    int cols = 0;
    std::vector<PivotOutputCell> cells;
    int tableTop = 0;                      // the first row of the table, under the filters
    int firstDataRow = 0;
    int firstDataCol = 0;
    int columnLevels = 0;                  // rows of column items
    struct Entry {
        std::vector<int> items;
        int valueField = -1;               // the value field the entry shows, -1: the table's
        bool subtotal = false;
        bool grandTotal = false;
    };
    std::vector<Entry> rowEntries;
    std::vector<Entry> colEntries;
    std::vector<std::vector<PivotItem>> rowItems;      // per row field, in its order
    std::vector<std::vector<PivotItem>> columnItems;   // per column field
    std::vector<std::string> fieldNames;
    int sourceRecords = 0;                 // the source's rows
    int shownRecords = 0;                  // ...left after the filters
};

// The names of the source block's columns: its first row's texts ("Column
// C" for an empty one).
std::vector<std::string> SpreadsheetPivotFieldNames(const SpreadsheetSheet& source, const CellRange& range);

// A pivot over `range` of `source` (a block of one cell is widened to the
// cells in use around it), with no fields yet, named "PivotTable1".
SpreadsheetPivotTable MakeSpreadsheetPivot(const SpreadsheetSheet& source, const CellRange& range);

// A value field over `column` as a spreadsheet adds one: Sum where the
// column holds numbers, Count where it holds text.
PivotValueField MakePivotValueField(const SpreadsheetSheet& source, const CellRange& range, int column);

// The items `field` has over the source, in its order, hidden ones too.
std::vector<PivotItem> SpreadsheetPivotFieldItems(const SpreadsheetSheet& source, const CellRange& range,
                                                  const PivotField& field);
// The item a cell of the source is under `grouping`.
PivotItem SpreadsheetPivotCellItem(const SpreadsheetSheet& source, int row, int col, const PivotGrouping& grouping);

// The pivot over its source: false (and why in `problem`) when the source
// has no data row or a field names a column outside it.
bool ComputeSpreadsheetPivot(const SpreadsheetPivotTable& pivot, const SpreadsheetSheet& source, PivotOutput& out,
                             std::string* problem = nullptr);

// Computes `pivot` and writes it onto `target` at pivot.target: the cells
// it filled before are cleared, the new ones written with a header style
// (field names, items, totals) and the value field's number format.
// `sheets` finds the source sheet. False, and the old cells kept, when the
// pivot is unsupported or cannot be computed.
bool RefreshSpreadsheetPivot(SpreadsheetPivotTable& pivot, SpreadsheetSheet& target,
                             const SpreadsheetSheetLookup& sheets, std::string* problem = nullptr);
// Clears the cells `pivot` fills on `target`.
void ClearSpreadsheetPivotCells(SpreadsheetPivotTable& pivot, SpreadsheetSheet& target);

// The source rows (0-based rows of `source`) behind cell (row, col) of the
// pivot's output (sheet coordinates): a value, a subtotal or a grand total.
// Empty for any other cell.
std::vector<int> SpreadsheetPivotDrillDownRows(const SpreadsheetPivotTable& pivot, const SpreadsheetSheet& source,
                                               int row, int col);

// Names in the reader's language.
std::string PivotSummaryName(PivotSummary summary);             // "Sum", "Count", ...
std::string PivotShowAsName(PivotShowAs showAs);                // "Value", "% of grand total" ...
std::string PivotGroupByName(PivotGroupBy by);                  // "No grouping", "Years" ...
// A value field's caption: its name, or "Sum of Sales".
std::string PivotValueFieldCaption(const PivotValueField& field, const std::vector<std::string>& fieldNames);

// Whether `pivot` names its fields within the source's columns.
bool SpreadsheetPivotFieldsFit(const SpreadsheetPivotTable& pivot);

// A sheet was renamed: the pivots' sources naming it by `oldName` name it `newName`.
void RenameSpreadsheetPivotSheet(std::vector<SpreadsheetPivotTable>& pivots, const std::string& oldName,
                                 const std::string& newName);

// The name for a new pivot of `workbook`: "PivotTable1", "PivotTable2" ...
std::string NextSpreadsheetPivotName(const UltraCanvasSpreadsheet& workbook);

// ===== IN FILES (the .xlsx and .ods readers and writers call these) =====

class UCZipPackageReader;

// .xlsx: every sheet's pivots as package parts (path, XML): a pivot cache
// definition with its records and relationships per pivot, a pivot table
// part with its relationships, and each worksheet's relationship to its
// pivots (xl/worksheets/_rels/sheetN.xml.rels, merged with the sheet's
// others). `workbookPivotCaches` gets the workbook's <pivotCaches>,
// `workbookRelationships` its <Relationship>s to the caches,
// `contentTypesXml` the new parts' types.
std::vector<std::pair<std::string, std::string>> WriteXlsxPivots(const UltraCanvasSpreadsheet& workbook,
                                                                  std::string& workbookPivotCaches,
                                                                  std::string& workbookRelationships,
                                                                  std::string& contentTypesXml);
// .xlsx: the pivots the worksheet part `sheetPart` relates to, added to
// `sheet`; `workbookRelationships` is the workbook's rId -> part path.
// Call once every sheet is read (a source may stand on a later sheet).
void ReadXlsxSheetPivots(const UCZipPackageReader& package, const std::string& sheetPart, SpreadsheetSheet& sheet,
                         const UltraCanvasSpreadsheet& workbook);
// .ods: the workbook's <table:data-pilot-tables> (empty without pivots).
std::string WriteOdsPivots(const UltraCanvasSpreadsheet& workbook);
// .ods: the data pilots of content.xml, each added to the sheet of its
// target range.
void ReadOdsPivots(UltraCanvasSpreadsheet& workbook, const std::string& contentXml);

// ===== THE FIELD LIST =====

// The pivot's field list in a dialog: the source's fields, the pivot's
// rows, columns, values and filters, each value field's summary and
// show-as, totals, each field's order, items and grouping; for a new pivot
// (`isNew`) also its source and where it goes (a new sheet or a cell).
// `onApply` gets the pivot as set, and whether it goes onto a new sheet.
void ShowSpreadsheetPivotDialog(const UltraCanvasSpreadsheet& workbook, const SpreadsheetPivotTable& pivot,
                                bool isNew, std::function<void(const SpreadsheetPivotTable&, bool newSheet)> onApply,
                                UltraCanvasWindowBase* parent);

} // namespace UltraCanvas
