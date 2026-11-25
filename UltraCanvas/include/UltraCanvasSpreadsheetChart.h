// include/UltraCanvasSpreadsheetChart.h
// A chart on a spreadsheet: an object anchored to cells - its corners a few
// pixels into two cells, as .xlsx's two-cell anchor keeps it - holding a
// RichChart (the chart model documents and decks use) whose series are
// ranges of cells: each series' name, its categories (a scatter series' x
// values) and its values. The data is read from the cells again whenever
// they change (RefreshSpreadsheetChart), and the chart is drawn by the chart
// engine through DrawRichDocumentChart (UltraCanvasRichDocumentChart.h) - on
// the grid, in print and in a PDF.
//
// A sheet keeps its charts (SpreadsheetSheet::GetCharts). The grid
// (UltraCanvasSpreadsheet) inserts one from the selection
// (InsertChartFromSelection), selects one with a click, moves and resizes
// it with handles, deletes it with Delete, and each change is an undo step.
// .xlsx keeps a chart as a drawing part (xl/drawings/drawingN.xml, a
// twoCellAnchor holding a graphicFrame) and a chart part (xl/charts/chartN.xml)
// with c:f references to the cells and their cached values
// (UltraCanvasDrawingML.h); .ods as a draw:frame in the sheet's table:shapes
// naming an Object N/ chart document whose series point at the cells
// (UltraCanvasOdfChart.h).
//
// doc-check: SpreadsheetSheet& sheet; std::shared_ptr<UltraCanvasSpreadsheet> grid;
// doc-check: example
//     // Months down A2:A7, sales in B2:B7 and costs in C2:C7, names in row 1.
//     SpreadsheetChart chart = MakeSpreadsheetChart(sheet, CellRange(0, 0, 6, 2), RichChart::Kind::Column);
//     chart.chart.title = "Sales";
//     grid->InsertChart(chart);             // on the active sheet, one undo step
//
// Version: 1.0.0 - SpreadsheetChart, its anchor and its ranges; made from a block of cells, read again from them
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSpreadsheetTypes.h"
#include "UltraCanvasRichDocument.h"   // RichChart

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

class IRenderContext;
class SpreadsheetSheet;
class UCZipPackageReader;
class UltraCanvasChartEngineElement;
class UltraCanvasSpreadsheet;

// A block of cells on a sheet named by its name. Unset (no sheet) where a
// series has no such part: a series without a name cell, a chart without
// categories.
struct SpreadsheetChartRange {
    std::string sheet;
    CellRange range;

    bool IsSet() const { return !sheet.empty(); }
    bool operator==(const SpreadsheetChartRange& other) const {
        return sheet == other.sheet && range.start.row == other.range.start.row &&
               range.start.col == other.range.start.col && range.end.row == other.range.end.row &&
               range.end.col == other.range.end.col;
    }
    bool operator!=(const SpreadsheetChartRange& other) const { return !(*this == other); }

    // As Excel writes it in a formula: "Sheet1!$A$2:$A$5", "'My data'!$B$1".
    std::string ToExcel() const;
    // As OpenDocument writes a cell range address: "Sheet1.$A$2:.$A$5",
    // "'My data'.$B$1".
    std::string ToOdf() const;
    // Reads either form back ("Sheet1!A2:A5" and "$Sheet1.$A$2:$Sheet1.$A$5"
    // too); false when `text` names no block of cells on a sheet.
    static bool FromExcel(const std::string& text, SpreadsheetChartRange& out);
    static bool FromOdf(const std::string& text, SpreadsheetChartRange& out);
};

// Where one series' data stands.
struct SpreadsheetChartSeries {
    SpreadsheetChartRange name;         // one cell; unset: the series keeps the name it has
    SpreadsheetChartRange categories;   // the category names (a scatter series: its x values)
    SpreadsheetChartRange values;
};

// Where a chart sits: its top-left corner `fromOffsetX`, `fromOffsetY`
// pixels into cell `from`, its bottom-right `toOffsetX`, `toOffsetY` pixels
// into cell `to` (the grid's pixels at 100 %). It moves and stretches with
// the cells under it.
struct SpreadsheetChartAnchor {
    CellAddress from;
    CellAddress to;
    int fromOffsetX = 0, fromOffsetY = 0;
    int toOffsetX = 0, toOffsetY = 0;
};

// Which way the series run through the block a chart is made from.
enum class SpreadsheetChartSeriesIn { Automatic, Columns, Rows };

struct SpreadsheetChart {
    std::string name;                         // "Chart 1"
    SpreadsheetChartAnchor anchor;
    // Kind, title, legend, colours - and the series' names, categories and
    // values, as RefreshSpreadsheetChart last read them from the cells.
    RichChart chart;
    std::vector<SpreadsheetChartSeries> sources;   // one per chart.series
    // The block the chart was made from and the way its series run (unset
    // for a chart read from a file): changing the way rebuilds `sources`.
    SpreadsheetChartRange dataRange;
    bool seriesInRows = false;
    // Changes whenever the chart does (its data, kind, title ...); never the
    // same for two different states, so a drawing can be cached by it.
    uint64_t revision = 0;
};

// A new revision for `chart` (after changing it by hand).
void TouchSpreadsheetChart(SpreadsheetChart& chart);

// Whether a cell's value is a number a chart plots (a number, a date, a
// currency, a formula's numeric result), and that number.
bool SpreadsheetChartCellNumber(const SpreadsheetSheet& sheet, int row, int col, double& value);

// A chart of `kind` over `range` of `sheet`, as a spreadsheet makes one from
// a selection: the first row holds the series' names where it holds words
// over numbers, the first column the categories where it holds words (or
// the corner is empty); the series run down the columns when the block is
// at least as tall as it is wide (Automatic), else along the rows. A scatter
// chart takes the first column (row) as every series' x values. `stacked`
// stacks columns, bars and areas. It is anchored right of the block, its
// data read already. A block of one cell is widened to the cells in use
// around it.
SpreadsheetChart MakeSpreadsheetChart(const SpreadsheetSheet& sheet, const CellRange& range, RichChart::Kind kind,
                                      bool stacked = false,
                                      SpreadsheetChartSeriesIn seriesIn = SpreadsheetChartSeriesIn::Automatic);

// Rebuilds `chart`'s sources from its dataRange with the series along the
// rows (or down the columns), keeping its kind, title and look; false when
// it has no dataRange. Read its data again afterwards.
bool SetSpreadsheetChartSeriesInRows(SpreadsheetChart& chart, const SpreadsheetSheet& sheet, bool rows);

// The sheet called `name`, or null.
using SpreadsheetSheetLookup = std::function<const SpreadsheetSheet*(const std::string& name)>;

// Reads `chart`'s series from the cells their ranges name (a cell that is
// no number reads as 0; a range on a sheet that is gone, as nothing). True,
// and a new revision, when anything changed.
bool RefreshSpreadsheetChart(SpreadsheetChart& chart, const SpreadsheetSheetLookup& sheets);

// The chart's box in sheet pixels, from the top-left of cell A1 (hidden
// rows and columns take no room), and the anchor of a box.
Rect2Di SpreadsheetChartBox(const SpreadsheetSheet& sheet, const SpreadsheetChartAnchor& anchor);
SpreadsheetChartAnchor SpreadsheetChartAnchorForBox(const SpreadsheetSheet& sheet, const Rect2Di& box);

// Draws `chart` into `target` of `ctx` (the grid, a printed page): a white
// chart area with a thin border, the chart engine's chart over it. `cache`
// (optional) keeps the engine's element between calls.
bool DrawSpreadsheetChart(IRenderContext* ctx, const SpreadsheetChart& chart, const Rect2Dd& target,
                          std::shared_ptr<UltraCanvasChartEngineElement>* cache = nullptr);

// A sheet was renamed: the ranges naming it by `oldName` name it `newName`.
void RenameSpreadsheetChartSheet(std::vector<SpreadsheetChart>& charts, const std::string& oldName,
                                 const std::string& newName);
// `count` rows (columns) were inserted at `at` of sheet `sheetName`
// (removed where negative): the ranges on it move with their cells.
void MoveSpreadsheetChartRanges(std::vector<SpreadsheetChart>& charts, const std::string& sheetName, bool columns,
                                int at, int count);
// ...and the charts standing on that sheet move with the cells under them.
void MoveSpreadsheetChartAnchors(std::vector<SpreadsheetChart>& charts, bool columns, int at, int count);

// ===== IN FILES (the .xlsx and .ods readers and writers call these) =====

// .xlsx: the charts the worksheet part `sheetPart` ("xl/worksheets/sheet1.xml")
// draws - its drawing's two-cell (and one-cell) anchors and their chart
// parts, found through the relationships - added to `sheet`.
void ReadXlsxSheetCharts(const UCZipPackageReader& package, const std::string& sheetPart, SpreadsheetSheet& sheet);
// .xlsx: the parts of every sheet's charts as (path in the package, XML): a
// drawing per sheet with charts, its relationships, a chart part per chart,
// and the worksheet's relationships. `sheetXmls` (one per sheet, in order)
// gets each such sheet's <drawing>, `contentTypesXml` ([Content_Types].xml)
// the new parts' types.
std::vector<std::pair<std::string, std::string>> WriteXlsxCharts(const UltraCanvasSpreadsheet& workbook,
                                                                  std::vector<std::string>& sheetXmls,
                                                                  std::string& contentTypesXml);
// .ods: the charts of content.xml - a draw:frame in a table's table:shapes
// or in one of its cells, naming an object ("./Object 1") whose
// content.xml is a chart - added to the sheet of the table's name.
// `readPart` reads an entry of the package ("" when it has none).
void ReadOdsCharts(UltraCanvasSpreadsheet& workbook, const std::string& contentXml,
                   const std::function<std::string(const std::string&)>& readPart);
// .ods: `sheet`'s charts as its <table:shapes> (empty when it has none);
// each chart's object is added to `objects` as ("Object N/content.xml",
// XML), N counting on from the objects already there.
std::string WriteOdsSheetCharts(const SpreadsheetSheet& sheet, std::vector<std::pair<std::string, std::string>>& objects);
// .ods: the manifest's entries for `objects`.
std::string OdsChartManifestEntries(const std::vector<std::pair<std::string, std::string>>& objects);

// The kinds the Insert chart command offers, in order, with the stacking
// each implies (Column, Column stacked, Bar, Bar stacked, Line, Area, Area
// stacked, Pie, Doughnut, Scatter, Radar).
struct SpreadsheetChartChoice {
    RichChart::Kind kind;
    bool stacked;
};
const std::vector<SpreadsheetChartChoice>& SpreadsheetChartChoices();
// The name of choice `index` in the reader's language ("Columns", "Bars,
// stacked" ...).
std::string SpreadsheetChartChoiceName(size_t index);
// The choice a chart is (its kind and stacking); -1 for one not offered.
int SpreadsheetChartChoiceOf(const RichChart& chart);

} // namespace UltraCanvas
