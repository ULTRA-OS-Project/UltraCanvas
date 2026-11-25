// Tests/SpreadsheetPivotTest.cpp
// Pivot tables (UltraCanvasSpreadsheetPivot.h), without a display.
//
// A sheet "Sales Data" holds seven sales - region, product, date, units,
// sales - and pivots over it are checked against sums worked out by hand:
//  - sums, counts, averages, min, max, the standard deviation, by region and
//    product, with the grand totals;
//  - two row fields and a column field, with subtotals and grand totals,
//    and without them;
//  - a filter field leaving a product out, items hidden in a row field, a
//    descending order;
//  - dates grouped by years, quarters and months, numbers by a step;
//  - % of the grand total, of the row and of the column;
//  - the grid keeps a pivot's cells read-only, refreshes the pivot when a
//    source cell changes, and drills into a value (a sheet of its rows);
//  - .xlsx and .ods: saved and read back, the model and the cells the same;
//    the .xlsx holds the cache definition, records and table parts;
//  - LibreOffice (where soffice is installed): our .xlsx and .ods converted
//    by it and read back, and pivots LibreOffice made itself over a sheet
//    we wrote (through its UNO bridge, when python3 has it), whose cells -
//    computed by LibreOffice - match ours.
//
// Usage: SpreadsheetPivotTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetPivot.h"
#include "UltraCanvasZipPackage.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-6 * std::max(1.0, std::fabs(b)); }

const char* kData = "Sales Data";
enum Column { Region = 0, Product = 1, Date = 2, Units = 3, Sales = 4 };

struct Sale {
    const char* region;
    const char* product;
    int year, month, day;
    double units, sales;
};
const Sale kSales[] = {
    {"East", "Apples", 2024, 1, 5, 10, 100},  {"East", "Pears", 2024, 1, 20, 5, 40},
    {"West", "Apples", 2024, 2, 3, 7, 70},    {"West", "Pears", 2024, 4, 9, 3, 30},
    {"East", "Apples", 2024, 5, 15, 8, 80},   {"North", "Plums", 2025, 1, 15, 2, 25},
    {"West", "Apples", 2025, 2, 10, 4, 45},
};
constexpr int kRows = 7;

std::shared_ptr<UltraCanvasSpreadsheet> BuildWorkbook() {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("book", 0, 0, 1000, 700);
    book->RenameSheet(0, kData);
    SpreadsheetSheet* data = book->GetSheet(0);
    const char* names[] = {"Region", "Product", "Date", "Units", "Sales"};
    for (int c = 0; c < 5; ++c) data->SetCellValue(0, c, std::string(names[c]));
    for (int r = 0; r < kRows; ++r) {
        const Sale& s = kSales[r];
        data->SetCellValue(r + 1, Region, std::string(s.region));
        data->SetCellValue(r + 1, Product, std::string(s.product));
        data->GetOrCreateCell(r + 1, Date).SetDate(s.year, s.month, s.day);
        data->GetOrCreateCell(r + 1, Date).SetNumberFormat(NumberFormat::Date());
        data->SetCellValue(r + 1, Units, s.units);
        data->SetCellValue(r + 1, Sales, s.sales);
    }
    book->AddSheet("Report");
    return book;
}

CellRange SourceRange() { return CellRange(0, 0, kRows, 4); }

// The text a cell of `sheet` shows ("" for none).
std::string TextAt(const SpreadsheetSheet& sheet, int row, int col) {
    const SpreadsheetCell* cell = sheet.GetCellIfExists(row, col);
    return cell ? cell->GetDisplayValue() : std::string();
}

// The row of the pivot whose label columns read `path` (each label in the
// column after the one before, under the first's group).
int RowOfPath(const SpreadsheetSheet& sheet, const SpreadsheetPivotTable& pivot, const std::vector<std::string>& path) {
    const CellRange out = pivot.outputRange;
    int from = out.start.row;
    for (size_t k = 0; k < path.size(); ++k) {
        const int col = out.start.col + static_cast<int>(k);
        int found = -1;
        for (int r = from; r <= out.end.row; ++r) {
            if (k > 0 && r > from) {
                // Left the group: a label in an outer column.
                bool outer = false;
                for (size_t j = 0; j < k; ++j) outer = outer || !TextAt(sheet, r, out.start.col + static_cast<int>(j)).empty();
                if (outer) break;
            }
            if (TextAt(sheet, r, col) == path[k]) { found = r; break; }
        }
        if (found < 0) return -1;
        from = found;
    }
    return from;
}

// The column of the pivot whose header reads `label` (searched in the
// header rows, right of the labels).
int ColumnOf(const SpreadsheetSheet& sheet, const SpreadsheetPivotTable& pivot, const std::string& label, int startCol = -1) {
    const CellRange out = pivot.outputRange;
    for (int r = out.start.row; r <= out.end.row; ++r) {
        for (int c = std::max(out.start.col + 1, startCol); c <= out.end.col; ++c) {
            if (TextAt(sheet, r, c) == label) return c;
        }
    }
    return -1;
}

// The number at a row path and a column label; NaN where there is none.
double ValueAt(const SpreadsheetSheet& sheet, const SpreadsheetPivotTable& pivot, const std::vector<std::string>& path,
               const std::string& column) {
    const int r = RowOfPath(sheet, pivot, path);
    const int c = ColumnOf(sheet, pivot, column);
    if (r < 0 || c < 0) return std::nan("");
    const SpreadsheetCell* cell = sheet.GetCellIfExists(r, c);
    if (!cell) return std::nan("");
    const auto number = cell->TryGetNumber();
    return number ? *number : std::nan("");
}

std::string Num(double v) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.6g", v);
    return text;
}

void CheckValue(const SpreadsheetSheet& sheet, const SpreadsheetPivotTable& pivot, const std::vector<std::string>& path,
                const std::string& column, double expected, const std::string& what) {
    const double got = ValueAt(sheet, pivot, path, column);
    std::string label;
    for (const std::string& p : path) label += (label.empty() ? "" : "/") + p;
    Check(!std::isnan(got) && Near(got, expected),
          what + ": " + label + " x " + column + " = " + Num(expected) + " (got " + Num(got) + ")");
}

SpreadsheetSheetLookup LookupOf(UltraCanvasSpreadsheet& book) {
    UltraCanvasSpreadsheet* raw = &book;
    return [raw](const std::string& name) -> const SpreadsheetSheet* { return raw->GetSheetByName(name); };
}

// A pivot written onto "Report" at A1 (straight through the model).
SpreadsheetPivotTable Write(UltraCanvasSpreadsheet& book, SpreadsheetPivotTable pivot) {
    SpreadsheetSheet* report = book.GetSheetByName("Report");
    report->ClearAll();
    pivot.outputRange = CellRange(-1, -1, -1, -1);
    std::string problem;
    if (!RefreshSpreadsheetPivot(pivot, *report, LookupOf(book), &problem)) {
        std::printf("      cannot write the pivot: %s\n", problem.c_str());
    }
    return pivot;
}

SpreadsheetPivotTable BasePivot(UltraCanvasSpreadsheet& book) {
    SpreadsheetPivotTable pivot = MakeSpreadsheetPivot(*book.GetSheetByName(kData), SourceRange());
    pivot.target = CellAddress(0, 0);
    return pivot;
}

PivotValueField ValueField(int column, PivotSummary summary, PivotShowAs showAs = PivotShowAs::Value) {
    PivotValueField v;
    v.column = column;
    v.summary = summary;
    v.showAs = showAs;
    return v;
}

PivotField AxisField(int column) {
    PivotField f;
    f.column = column;
    return f;
}

void CheckSummaries(UltraCanvasSpreadsheet& book) {
    std::printf("\n=== Sums, counts, averages ===\n");
    const SpreadsheetSheet& report = *book.GetSheetByName("Report");
    SpreadsheetPivotTable pivot = BasePivot(book);
    Check(pivot.fieldNames.size() == 5 && pivot.fieldNames[4] == "Sales", "the fields are the header row's names");
    Check(pivot.sourceRange.RowCount() == kRows + 1 && pivot.sourceRange.ColCount() == 5, "the source block");
    {
        SpreadsheetPivotTable one = MakeSpreadsheetPivot(*book.GetSheetByName(kData), CellRange(3, 2, 3, 2));
        Check(one.sourceRange.start.row == 0 && one.sourceRange.end.row == kRows && one.sourceRange.end.col == 4,
              "a single cell picked grows to the block around it");
    }
    pivot.rowFields.push_back(AxisField(Region));
    pivot.columnFields.push_back(AxisField(Product));
    pivot.valueFields.push_back(ValueField(Sales, PivotSummary::Sum));
    pivot = Write(book, pivot);
    Check(TextAt(report, 0, 0) == "Sum of Sales" && TextAt(report, 0, 1) == "Product" && TextAt(report, 1, 0) == "Region",
          "the caption, the column field's name over its items, the row field's name");
    Check(TextAt(report, 1, 1) == "Apples" && TextAt(report, 1, 2) == "Pears" && TextAt(report, 1, 3) == "Plums" &&
              TextAt(report, 1, 4) == "Grand Total",
          "the products across the top in order, then the grand total");
    Check(TextAt(report, 2, 0) == "East" && TextAt(report, 3, 0) == "North" && TextAt(report, 4, 0) == "West" &&
              TextAt(report, 5, 0) == "Grand Total",
          "the regions down the side in order, then the grand total");
    CheckValue(report, pivot, {"East"}, "Apples", 180, "sum");
    CheckValue(report, pivot, {"East"}, "Pears", 40, "sum");
    CheckValue(report, pivot, {"West"}, "Apples", 115, "sum");
    CheckValue(report, pivot, {"North"}, "Plums", 25, "sum");
    CheckValue(report, pivot, {"East"}, "Grand Total", 220, "row total");
    CheckValue(report, pivot, {"Grand Total"}, "Apples", 295, "column total");
    CheckValue(report, pivot, {"Grand Total"}, "Grand Total", 390, "grand total");
    Check(report.GetCellIfExists(3, 1) == nullptr || report.GetCellIfExists(3, 1)->IsEmpty(),
          "North has no apples: the cell is empty");
    Check(pivot.outputRange.start.row == 0 && pivot.outputRange.end.row == 5 && pivot.outputRange.end.col == 4,
          "the output range: A1:E6");
    {
        const SpreadsheetCell* header = report.GetCellIfExists(1, 1);
        const SpreadsheetCell* value = report.GetCellIfExists(2, 1);
        Check(header && header->GetStyle().font.bold && header->GetStyle().fill.HasFill() && value &&
                  !value->GetStyle().font.bold,
              "headers are drawn with the header style, values plain");
    }

    // Count, average, max, min, StdDev by region (no column field).
    SpreadsheetPivotTable stats = BasePivot(book);
    stats.rowFields.push_back(AxisField(Region));
    stats.valueFields.push_back(ValueField(Sales, PivotSummary::Count));
    stats.valueFields.push_back(ValueField(Sales, PivotSummary::Average));
    stats.valueFields.push_back(ValueField(Units, PivotSummary::Max));
    stats.valueFields.push_back(ValueField(Units, PivotSummary::Min));
    stats.valueFields.push_back(ValueField(Sales, PivotSummary::StdDev));
    stats.valueFields.push_back(ValueField(Units, PivotSummary::CountNumbers));
    stats.valueFields.push_back(ValueField(Units, PivotSummary::Product));
    stats.valueFields.push_back(ValueField(Sales, PivotSummary::VarP));
    stats = Write(book, stats);
    Check(TextAt(report, 0, 1) == "Values" && TextAt(report, 1, 1) == "Count of Sales" &&
              TextAt(report, 1, 2) == "Average of Sales",
          "several values side by side under a Values level");
    CheckValue(report, stats, {"East"}, "Count of Sales", 3, "count");
    CheckValue(report, stats, {"North"}, "Count of Sales", 1, "count");
    CheckValue(report, stats, {"Grand Total"}, "Count of Sales", 7, "count, total");
    CheckValue(report, stats, {"East"}, "Average of Sales", 220.0 / 3.0, "average");
    CheckValue(report, stats, {"West"}, "Average of Sales", 145.0 / 3.0, "average");
    CheckValue(report, stats, {"Grand Total"}, "Average of Sales", 390.0 / 7.0, "average, total");
    CheckValue(report, stats, {"East"}, "Max of Units", 10, "max");
    CheckValue(report, stats, {"West"}, "Min of Units", 3, "min");
    const double mean = 220.0 / 3.0;
    const double sd = std::sqrt(((100 - mean) * (100 - mean) + (40 - mean) * (40 - mean) + (80 - mean) * (80 - mean)) / 2.0);
    CheckValue(report, stats, {"East"}, "StdDev of Sales", sd, "standard deviation");
    Check(std::isnan(ValueAt(report, stats, {"North"}, "StdDev of Sales")), "a standard deviation of one sale is empty");
    CheckValue(report, stats, {"West"}, "Count numbers of Units", 3, "count of numbers");
    CheckValue(report, stats, {"West"}, "Product of Units", 7.0 * 3.0 * 4.0, "product");
    const double westMean = 145.0 / 3.0;
    CheckValue(report, stats, {"West"}, "VarP of Sales",
               ((70 - westMean) * (70 - westMean) + (30 - westMean) * (30 - westMean) + (45 - westMean) * (45 - westMean)) / 3.0,
               "population variance");
    // A count of a text column counts its cells.
    SpreadsheetPivotTable texts = BasePivot(book);
    texts.rowFields.push_back(AxisField(Product));
    texts.valueFields.push_back(MakePivotValueField(*book.GetSheetByName(kData), SourceRange(), Region));
    Check(texts.valueFields[0].summary == PivotSummary::Count, "a text column added to the values is counted");
    texts = Write(book, texts);
    CheckValue(report, texts, {"Apples"}, "Count of Region", 4, "count of texts");
    // Values one under the other.
    stats.valuesInRows = true;
    stats.valueFields.resize(2);
    stats = Write(book, stats);
    Check(TextAt(report, 0, 0) == "Region" && TextAt(report, 0, 1) == "Values" && TextAt(report, 1, 1) == "Count of Sales" &&
              TextAt(report, 2, 1) == "Average of Sales",
          "several values one under the other");
    Check(report.GetCellIfExists(1, 2) && Near(*report.GetCellIfExists(1, 2)->TryGetNumber(), 3) &&
              report.GetCellIfExists(2, 2) && Near(*report.GetCellIfExists(2, 2)->TryGetNumber(), 220.0 / 3.0),
          "East's count and average, one under the other");
}

void CheckSeveralFields(UltraCanvasSpreadsheet& book) {
    std::printf("\n=== Several row and column fields, subtotals, grand totals ===\n");
    const SpreadsheetSheet& report = *book.GetSheetByName("Report");
    SpreadsheetPivotTable pivot = BasePivot(book);
    pivot.rowFields.push_back(AxisField(Region));
    pivot.rowFields.push_back(AxisField(Product));
    PivotField years = AxisField(Date);
    years.grouping.by = PivotGroupBy::Years;
    pivot.columnFields.push_back(years);
    pivot.valueFields.push_back(ValueField(Sales, PivotSummary::Sum));
    pivot = Write(book, pivot);
    CheckValue(report, pivot, {"East", "Apples"}, "2024", 180, "by year");
    CheckValue(report, pivot, {"East", "Pears"}, "2024", 40, "by year");
    CheckValue(report, pivot, {"West", "Apples"}, "2025", 45, "by year");
    CheckValue(report, pivot, {"West", "Apples"}, "Grand Total", 115, "a row's total");
    CheckValue(report, pivot, {"East Total"}, "2024", 220, "East's subtotal");
    CheckValue(report, pivot, {"West Total"}, "2024", 100, "West's subtotal");
    CheckValue(report, pivot, {"West Total"}, "Grand Total", 145, "West's subtotal, total");
    CheckValue(report, pivot, {"Grand Total"}, "2025", 70, "the grand total of 2025");
    CheckValue(report, pivot, {"Grand Total"}, "Grand Total", 390, "the grand total");
    Check(RowOfPath(report, pivot, {"East", "Apples"}) + 1 == RowOfPath(report, pivot, {"East", "Pears"}) &&
              RowOfPath(report, pivot, {"East", "Pears"}) + 1 == RowOfPath(report, pivot, {"East Total"}),
          "a region's products, then its subtotal");
    // Two column fields: product, then year.
    SpreadsheetPivotTable wide = pivot;
    wide.rowFields.resize(1);
    wide.columnFields.clear();
    wide.columnFields.push_back(AxisField(Product));
    wide.columnFields.push_back(years);
    wide = Write(book, wide);
    const int applesTotal = ColumnOf(report, wide, "Apples Total");
    const int apples2025 = ColumnOf(report, wide, "2025");
    Check(applesTotal > 0 && apples2025 > 0 && apples2025 < applesTotal, "the apples' years, then the apples' total");
    if (applesTotal > 0) {
        const int east = RowOfPath(report, wide, {"East"});
        const SpreadsheetCell* cell = report.GetCellIfExists(east, applesTotal);
        Check(cell && cell->TryGetNumber() && Near(*cell->TryGetNumber(), 180), "East's apples, all years: 180");
        const int west = RowOfPath(report, wide, {"West"});
        const SpreadsheetCell* w2025 = report.GetCellIfExists(west, apples2025);
        Check(w2025 && w2025->TryGetNumber() && Near(*w2025->TryGetNumber(), 45), "West's apples in 2025: 45");
    }
    // Without subtotals and grand totals.
    pivot.rowFields[0].subtotals = false;
    pivot.grandTotalRow = false;
    pivot.grandTotalColumn = false;
    pivot = Write(book, pivot);
    Check(RowOfPath(report, pivot, {"East Total"}) < 0 && RowOfPath(report, pivot, {"Grand Total"}) < 0 &&
              ColumnOf(report, pivot, "Grand Total") < 0,
          "subtotals and grand totals turned off");
    CheckValue(report, pivot, {"West", "Pears"}, "2024", 30, "without totals");
}

void CheckFiltersAndGroups(UltraCanvasSpreadsheet& book) {
    std::printf("\n=== Filters, items, order, grouping ===\n");
    const SpreadsheetSheet& report = *book.GetSheetByName("Report");
    const SpreadsheetSheet& data = *book.GetSheetByName(kData);
    SpreadsheetPivotTable pivot = BasePivot(book);
    pivot.rowFields.push_back(AxisField(Region));
    PivotField product = AxisField(Product);
    product.subtotals = false;
    product.hiddenItems.push_back("Pears");
    pivot.filterFields.push_back(product);
    pivot.valueFields.push_back(ValueField(Sales, PivotSummary::Sum));
    pivot = Write(book, pivot);
    Check(TextAt(report, 0, 0) == "Product" && TextAt(report, 0, 1) == "(Multiple items)",
          "the filter above the table: Product, several items");
    CheckValue(report, pivot, {"East"}, "Sum of Sales", 180, "pears filtered out");
    CheckValue(report, pivot, {"Grand Total"}, "Sum of Sales", 320, "pears filtered out, total");
    pivot.filterFields[0].hiddenItems = {"Pears", "Plums"};
    pivot = Write(book, pivot);
    Check(TextAt(report, 0, 1) == "Apples", "one item left: the filter names it");
    CheckValue(report, pivot, {"Grand Total"}, "Sum of Sales", 295, "apples only");
    Check(RowOfPath(report, pivot, {"North"}) < 0, "North sold no apples: it is not there");

    // Items of a row field hidden, and the order turned round.
    SpreadsheetPivotTable rows = BasePivot(book);
    PivotField region = AxisField(Region);
    region.descending = true;
    region.hiddenItems.push_back("North");
    rows.rowFields.push_back(region);
    rows.valueFields.push_back(ValueField(Sales, PivotSummary::Sum));
    rows = Write(book, rows);
    Check(TextAt(report, 1, 0) == "West" && TextAt(report, 2, 0) == "East" && TextAt(report, 3, 0) == "Grand Total",
          "descending, North hidden");
    CheckValue(report, rows, {"Grand Total"}, "Sum of Sales", 365, "the total without North");
    const std::vector<PivotItem> items = SpreadsheetPivotFieldItems(data, SourceRange(), region);
    Check(items.size() == 3 && items[0].label == "West" && items[2].label == "East",
          "the field's items, hidden ones too, in its order");

    // Dates by months and quarters; numbers by a step.
    SpreadsheetPivotTable months = BasePivot(book);
    PivotField month = AxisField(Date);
    month.grouping.by = PivotGroupBy::Months;
    months.rowFields.push_back(month);
    months.valueFields.push_back(ValueField(Sales, PivotSummary::Sum));
    months = Write(book, months);
    CheckValue(report, months, {"Jan"}, "Sum of Sales", 165, "January of both years");
    CheckValue(report, months, {"Feb"}, "Sum of Sales", 115, "February");
    CheckValue(report, months, {"May"}, "Sum of Sales", 80, "May");
    Check(TextAt(report, 1, 0) == "Jan" && TextAt(report, 2, 0) == "Feb" && TextAt(report, 3, 0) == "Apr" &&
              TextAt(report, 4, 0) == "May",
          "the months in calendar order");
    months.rowFields[0].grouping.by = PivotGroupBy::Quarters;
    months = Write(book, months);
    CheckValue(report, months, {"Q1"}, "Sum of Sales", 280, "the first quarters");
    CheckValue(report, months, {"Q2"}, "Sum of Sales", 110, "the second quarters");
    SpreadsheetPivotTable steps = BasePivot(book);
    PivotField units = AxisField(Units);
    units.grouping.by = PivotGroupBy::NumberRange;
    units.grouping.step = 5;
    steps.rowFields.push_back(units);
    steps.valueFields.push_back(ValueField(Sales, PivotSummary::Sum));
    steps = Write(book, steps);
    CheckValue(report, steps, {"2-6"}, "Sum of Sales", 140, "units 2 to 6");
    CheckValue(report, steps, {"7-11"}, "Sum of Sales", 250, "units 7 to 11");

    // Shown as shares.
    SpreadsheetPivotTable shares = BasePivot(book);
    shares.rowFields.push_back(AxisField(Region));
    shares.columnFields.push_back(AxisField(Product));
    shares.valueFields.push_back(ValueField(Sales, PivotSummary::Sum, PivotShowAs::PercentOfTotal));
    shares = Write(book, shares);
    CheckValue(report, shares, {"East"}, "Grand Total", 220.0 / 390.0, "% of the grand total");
    CheckValue(report, shares, {"Grand Total"}, "Grand Total", 1.0, "% of the grand total, total");
    {
        const int r = RowOfPath(report, shares, {"East"});
        const int c = ColumnOf(report, shares, "Apples");
        const SpreadsheetCell* cell = report.GetCellIfExists(r, c);
        Check(cell && cell->GetStyle().numberFormat.category == NumberFormatCategory::Percentage, "a share shows as a percentage");
    }
    shares.valueFields[0].showAs = PivotShowAs::PercentOfRow;
    shares = Write(book, shares);
    CheckValue(report, shares, {"East"}, "Apples", 180.0 / 220.0, "% of the row");
    shares.valueFields[0].showAs = PivotShowAs::PercentOfColumn;
    shares = Write(book, shares);
    CheckValue(report, shares, {"East"}, "Apples", 180.0 / 295.0, "% of the column");

    // Drilling into a value.
    SpreadsheetPivotTable drill = BasePivot(book);
    drill.rowFields.push_back(AxisField(Region));
    drill.columnFields.push_back(AxisField(Product));
    drill.valueFields.push_back(ValueField(Sales, PivotSummary::Sum));
    drill = Write(book, drill);
    const int east = RowOfPath(report, drill, {"East"});
    const int apples = ColumnOf(report, drill, "Apples");
    const std::vector<int> behind = SpreadsheetPivotDrillDownRows(drill, data, east, apples);
    Check(behind.size() == 2 && behind[0] == 1 && behind[1] == 5, "East's apples are source rows 2 and 6");
    Check(SpreadsheetPivotDrillDownRows(drill, data, 0, 0).empty(), "a caption has no rows behind it");
}

void CheckGrid(std::shared_ptr<UltraCanvasSpreadsheet> book) {
    std::printf("\n=== The grid: read-only cells, refresh, drill-down ===\n");
    SpreadsheetPivotTable pivot = BasePivot(*book);
    pivot.rowFields.push_back(AxisField(Region));
    pivot.columnFields.push_back(AxisField(Product));
    pivot.valueFields.push_back(ValueField(Sales, PivotSummary::Sum));
    std::string problem;
    const int index = book->InsertPivotTable(pivot, -1, &problem);
    Check(index == 0, "a pivot on a new sheet " + problem);
    SpreadsheetSheet* sheet = book->GetActiveSheet();
    Check(sheet && sheet->GetName() == "Pivot" && sheet->GetPivotTables().size() == 1, "the new sheet is Pivot, active");
    if (!sheet || sheet->GetPivotTables().empty()) return;
    const SpreadsheetPivotTable* placed = &sheet->GetPivotTables()[0];
    Check(placed->name == "PivotTable1", "named PivotTable1");
    CheckValue(*sheet, *placed, {"East"}, "Apples", 180, "on the grid");
    // Read-only.
    const int r = RowOfPath(*sheet, *placed, {"East"});
    const int c = ColumnOf(*sheet, *placed, "Apples");
    book->SelectCell(r, c);
    Check(book->GetActivePivotTable() == 0, "the active cell is in the pivot");
    std::string status;
    book->onStatusChange = [&status](const std::string& text) { status = text; };
    book->StartEditingAt(r, c);
    Check(!book->IsEditing() && !status.empty(), "typing into a pivot's cell is refused, with a message");
    book->ClearSelection();
    Check(sheet->GetCellIfExists(r, c) && sheet->GetCellIfExists(r, c)->TryGetNumber(), "Delete leaves the pivot's cell");
    book->onStatusChange = nullptr;
    // A source cell changes: the pivot follows.
    SpreadsheetSheet* data = book->GetSheetByName(kData);
    data->SetCellValue(1, Sales, 200.0);   // East apples 100 -> 200
    Check(book->RefreshChangedPivotTables(), "the changed source refreshes the pivot");
    placed = &sheet->GetPivotTables()[0];
    CheckValue(*sheet, *placed, {"East"}, "Apples", 280, "after the source changed");
    CheckValue(*sheet, *placed, {"Grand Total"}, "Grand Total", 490, "the grand total after the change");
    Check(!book->RefreshChangedPivotTables(), "nothing more to refresh");
    data->SetCellValue(20, 9, 1.0);   // outside the source
    Check(!book->RefreshChangedPivotTables(), "a cell outside the source refreshes nothing");
    data->SetCellValue(1, Sales, 100.0);
    book->RefreshChangedPivotTables();
    // Changing the fields: one step on the grid.
    SpreadsheetPivotTable changed = sheet->GetPivotTables()[0];
    changed.columnFields.clear();
    Check(book->SetPivotTable(book->GetActiveSheetIndex(), 0, changed), "the fields changed");
    placed = &sheet->GetPivotTables()[0];
    Check(placed->outputRange.end.col == 1 && TextAt(*sheet, 1, 4).empty(), "the old cells are cleared");
    CheckValue(*sheet, *placed, {"East"}, "Sum of Sales", 220, "without the column field");
    // Drill-down: a new sheet of the rows.
    const int rr = RowOfPath(*sheet, *placed, {"West"});
    const int sheetsBefore = book->GetSheetCount();
    const int details = book->DrillDownPivotCell(rr, 1);
    const SpreadsheetSheet* rowsSheet = book->GetSheet(details);
    Check(details >= 0 && book->GetSheetCount() == sheetsBefore + 1 && rowsSheet &&
              TextAt(*rowsSheet, 0, 0) == "Region" && TextAt(*rowsSheet, 1, 0) == "West" &&
              TextAt(*rowsSheet, 3, 0) == "West" && TextAt(*rowsSheet, 4, 0).empty(),
          "drilling into West: a sheet of its three rows under the field names");
    book->SetActiveSheet(book->GetSheetByName("Pivot")->GetIndex());
    // Removing it clears its cells.
    Check(book->RemovePivotTable(book->GetActiveSheetIndex(), 0) && sheet->GetPivotTables().empty() &&
              TextAt(*sheet, 0, 0).empty(),
          "removed, its cells cleared");
}

// The workbook the files carry: three pivots on "Report" and "Pivot".
std::shared_ptr<UltraCanvasSpreadsheet> BuildFileWorkbook() {
    auto book = BuildWorkbook();
    SpreadsheetPivotTable a = BasePivot(*book);
    a.rowFields.push_back(AxisField(Region));
    a.columnFields.push_back(AxisField(Product));
    a.valueFields.push_back(ValueField(Sales, PivotSummary::Sum));
    book->InsertPivotTable(a, 1);
    SpreadsheetPivotTable b = BasePivot(*book);
    b.name = "Monthly";
    PivotField month = AxisField(Date);
    month.grouping.by = PivotGroupBy::Months;
    b.rowFields.push_back(month);
    PivotField product = AxisField(Product);
    product.subtotals = false;
    product.hiddenItems.push_back("Pears");
    b.filterFields.push_back(product);
    b.valueFields.push_back(ValueField(Sales, PivotSummary::Average));
    b.valueFields.push_back(ValueField(Units, PivotSummary::Count));
    b.target = CellAddress(0, 8);   // I1, beside the first
    book->InsertPivotTable(b, 1);
    SpreadsheetPivotTable c = BasePivot(*book);
    PivotField region = AxisField(Region);
    region.descending = true;
    region.hiddenItems.push_back("North");
    c.rowFields.push_back(region);
    c.rowFields.push_back(AxisField(Product));
    c.valueFields.push_back(ValueField(Sales, PivotSummary::Sum, PivotShowAs::PercentOfTotal));
    c.grandTotalColumn = false;
    book->InsertPivotTable(c, -1);
    return book;
}

std::shared_ptr<UltraCanvasSpreadsheet> Load(const std::string& path) {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("loaded", 0, 0, 1000, 700);
    if (!book->LoadFromFile(path)) {
        std::printf("      cannot load %s: %s\n", path.c_str(), book->GetLastError().c_str());
        return nullptr;
    }
    return book;
}

bool SameFields(const std::vector<PivotField>& a, const std::vector<PivotField>& b, std::string& why, bool order = true) {
    if (a.size() != b.size()) { why = "field count"; return false; }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].column != b[i].column) { why = "column"; return false; }
        if (order && a[i].descending != b[i].descending) { why = "order"; return false; }
        if (a[i].grouping.by != b[i].grouping.by) { why = "grouping"; return false; }
        std::vector<std::string> ha = a[i].hiddenItems, hb = b[i].hiddenItems;
        std::sort(ha.begin(), ha.end());
        std::sort(hb.begin(), hb.end());
        if (ha != hb) { why = "hidden items"; return false; }
    }
    return true;
}

// The loaded pivots against the built ones: their model, their cells, and
// the same cells after a refresh. `exact` checks what LibreOffice may write
// its own way (names, subtotal flags, the cells it rendered) too; `lossy`
// leaves out what LibreOffice's .xlsx writer drops (a field's sort order, a
// value shown as a share, the grand totals of a pivot without column fields).
void CheckFiles(UltraCanvasSpreadsheet& loaded, UltraCanvasSpreadsheet& built, const std::string& label, bool exact,
                bool lossy = false) {
    std::printf("\n-- %s --\n", label.c_str());
    for (const char* sheetName : {"Report", "Pivot"}) {
        const SpreadsheetSheet* want = built.GetSheetByName(sheetName);
        const SpreadsheetSheet* got = loaded.GetSheetByName(sheetName);
        Check(want && got && got->GetPivotTables().size() == want->GetPivotTables().size(),
              label + ": " + sheetName + " has " + std::to_string(want ? want->GetPivotTables().size() : 0) + " pivot(s)" +
                  (got ? " (read " + std::to_string(got->GetPivotTables().size()) + ")" : ""));
        if (!want || !got || got->GetPivotTables().size() != want->GetPivotTables().size()) continue;
        for (size_t i = 0; i < want->GetPivotTables().size(); ++i) {
            const SpreadsheetPivotTable& w = want->GetPivotTables()[i];
            const SpreadsheetPivotTable& g = got->GetPivotTables()[i];
            const std::string what = label + ": " + w.name;
            Check(g.unsupportedReason.empty(), what + " is read into the model " + g.unsupportedReason);
            Check(g.sourceSheet == w.sourceSheet && g.sourceRange == w.sourceRange, what + ": its source");
            std::string why;
            Check(SameFields(g.rowFields, w.rowFields, why, !lossy), what + ": its row fields " + why);
            Check(SameFields(g.columnFields, w.columnFields, why, !lossy), what + ": its column fields " + why);
            Check(SameFields(g.filterFields, w.filterFields, why, !lossy), what + ": its filter fields " + why);
            bool values = g.valueFields.size() == w.valueFields.size();
            for (size_t v = 0; values && v < w.valueFields.size(); ++v) {
                values = g.valueFields[v].column == w.valueFields[v].column &&
                         g.valueFields[v].summary == w.valueFields[v].summary &&
                         (lossy || g.valueFields[v].showAs == w.valueFields[v].showAs);
            }
            Check(values, what + ": its value fields, summaries and show-as");
            if (!lossy) {
                Check(g.grandTotalRow == w.grandTotalRow && g.grandTotalColumn == w.grandTotalColumn,
                      what + ": its grand totals");
            }
            if (exact) {
                Check(g.name == w.name, what + ": its name");
                Check(g.target == w.target && g.outputRange == w.outputRange, what + ": where it stands");
                bool subtotals = g.rowFields.size() == w.rowFields.size();
                for (size_t k = 0; subtotals && k < w.rowFields.size(); ++k) subtotals = g.rowFields[k].subtotals == w.rowFields[k].subtotals;
                Check(subtotals, what + ": its subtotals");
            }
        }
    }
    // The cells as read, then refreshed: the hand-worked values.
    auto checkCells = [&](const std::string& when) {
        const SpreadsheetSheet* report = loaded.GetSheetByName("Report");
        const SpreadsheetSheet* pivotSheet = loaded.GetSheetByName("Pivot");
        if (!report || report->GetPivotTables().size() < 2 || !pivotSheet || pivotSheet->GetPivotTables().empty()) return;
        const SpreadsheetPivotTable& a = report->GetPivotTables()[0];
        CheckValue(*report, a, {"East"}, "Apples", 180, label + " " + when);
        CheckValue(*report, a, {"Grand Total"}, "Grand Total", 390, label + " " + when);
        const SpreadsheetPivotTable& b = report->GetPivotTables()[1];
        // Average of the sales but pears, by month: Jan (100 + 25) / 2.
        const int jan = RowOfPath(*report, b, {"Jan"}) >= 0 ? RowOfPath(*report, b, {"Jan"}) : -1;
        const int col = ColumnOf(*report, b, PivotValueFieldCaption(b.valueFields[0], b.fieldNames),
                                 b.outputRange.start.col + 1);
        const SpreadsheetCell* cell = jan >= 0 && col >= 0 ? report->GetCellIfExists(jan, col) : nullptr;
        Check(cell && cell->TryGetNumber() && Near(*cell->TryGetNumber(), 62.5),
              label + " " + when + ": January's average without the pears, 62.5");
        const SpreadsheetPivotTable& c = pivotSheet->GetPivotTables()[0];
        if (lossy) return;   // the share came back as a sum
        CheckValue(*pivotSheet, c, {"West", "Apples"}, PivotValueFieldCaption(c.valueFields[0], c.fieldNames),
                   115.0 / 365.0, label + " " + when + ": a share");
    };
    if (exact) checkCells("as read");
    loaded.RefreshPivotTables();
    checkCells("refreshed");
}

void CheckXlsxParts(const std::string& path) {
    UCZipPackageReader zip;
    Check(zip.Open(path), "the .xlsx opens as a package");
    std::string types, workbook, table, cache, rels;
    zip.ReadEntry("[Content_Types].xml", types);
    zip.ReadEntry("xl/workbook.xml", workbook);
    zip.ReadEntry("xl/pivotTables/pivotTable1.xml", table);
    zip.ReadEntry("xl/pivotCache/pivotCacheDefinition1.xml", cache);
    zip.ReadEntry("xl/worksheets/_rels/sheet2.xml.rels", rels);
    Check(zip.HasEntry("xl/pivotCache/pivotCacheRecords1.xml") && zip.HasEntry("xl/pivotTables/pivotTable3.xml") &&
              zip.HasEntry("xl/pivotTables/_rels/pivotTable1.xml.rels") &&
              zip.HasEntry("xl/pivotCache/_rels/pivotCacheDefinition1.xml.rels"),
          "the .xlsx holds the caches, their records and the tables, with their relationships");
    Check(types.find("pivotTable+xml") != std::string::npos && types.find("pivotCacheDefinition+xml") != std::string::npos &&
              types.find("pivotCacheRecords+xml") != std::string::npos,
          "their content types");
    Check(workbook.find("<pivotCaches><pivotCache cacheId=\"1\"") != std::string::npos, "the workbook lists the caches");
    Check(rels.find("pivotTables/pivotTable1.xml") != std::string::npos && rels.find("pivotTables/pivotTable2.xml") != std::string::npos,
          "the Report sheet relates to its two tables");
    Check(cache.find("worksheetSource ref=\"A1:E8\" sheet=\"Sales Data\"") != std::string::npos &&
              cache.find("refreshOnLoad=\"1\"") != std::string::npos,
          "the cache names its source and is refreshed on load");
    Check(table.find("axis=\"axisRow\"") != std::string::npos && table.find("<dataField name=\"Sum of Sales\"") != std::string::npos,
          "the table's fields");
}

bool HaveSoffice() {
#ifdef _WIN32
    return false;
#else
    return std::system("command -v soffice > /dev/null 2>&1") == 0;
#endif
}

std::string Convert(const fs::path& source, const std::string& ext, const fs::path& outDir, const fs::path& profile) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    const std::string command = "soffice -env:UserInstallation=file://" + PathToUtf8(profile) +
                                " --headless --convert-to " + ext + " --outdir '" + PathToUtf8(outDir) + "' '" +
                                PathToUtf8(source) + "' > /dev/null 2>&1";
    if (std::system(command.c_str()) != 0) return "";
    const fs::path result = outDir / PathFromUtf8(PathToUtf8(source.stem()) + "." + ext);
    return fs::exists(result, ec) ? PathToUtf8(result) : std::string();
}

// A script LibreOffice runs through its UNO bridge: it opens `data.ods`
// (the sales, written by us), makes two pivots of its own - regions by
// products summed, with a filter on the product; the sales averaged by
// month - and saves the workbook as .ods and .xlsx.
const char* kUnoScript = R"PY(
import os, subprocess, sys, time, uno
from com.sun.star.beans import PropertyValue
from com.sun.star.table import CellAddress, CellRangeAddress
src, out, profile = sys.argv[1], sys.argv[2], sys.argv[3]
pipe = "ucpivot%d" % os.getpid()
proc = subprocess.Popen(["soffice", "-env:UserInstallation=file://" + profile, "--headless", "--invisible",
                         "--norestore", "--accept=pipe,name=%s;urp;" % pipe],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
local = uno.getComponentContext()
resolver = local.ServiceManager.createInstanceWithContext("com.sun.star.bridge.UnoUrlResolver", local)
ctx = None
for _ in range(120):
    try:
        ctx = resolver.resolve("uno:pipe,name=%s;urp;StarOffice.ComponentContext" % pipe)
        break
    except Exception:
        time.sleep(0.5)
if ctx is None:
    proc.kill(); sys.exit(2)
desktop = ctx.ServiceManager.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
hidden = PropertyValue(); hidden.Name = "Hidden"; hidden.Value = True
doc = desktop.loadComponentFromURL(uno.systemPathToFileUrl(src), "_blank", 0, (hidden,))
from com.sun.star.sheet.DataPilotFieldOrientation import ROW, COLUMN, DATA, PAGE
from com.sun.star.sheet.GeneralFunction import SUM, AVERAGE
from com.sun.star.sheet.DataPilotFieldGroupBy import MONTHS
sheets = doc.Sheets
data = sheets.getByIndex(0)
if not sheets.hasByName("LO Pivot"):
    sheets.insertNewByName("LO Pivot", sheets.Count)
target = sheets.getByName("LO Pivot")
tables = target.getDataPilotTables()
def source():
    r = CellRangeAddress(); r.Sheet = 0; r.StartColumn = 0; r.StartRow = 0; r.EndColumn = 4; r.EndRow = 7
    return r
desc = tables.createDataPilotDescriptor()
desc.SourceRange = source()
f = desc.DataPilotFields
f.getByIndex(0).Orientation = ROW
f.getByIndex(1).Orientation = COLUMN
s = f.getByIndex(4); s.Orientation = DATA; s.Function = SUM
at = CellAddress(); at.Sheet = target.RangeAddress.Sheet; at.Column = 0; at.Row = 0
tables.insertNewByName("LOSums", at, desc)
desc2 = tables.createDataPilotDescriptor()
desc2.SourceRange = source()
d = desc2.DataPilotFields.getByIndex(2)
d.Orientation = ROW
a = desc2.DataPilotFields.getByIndex(4); a.Orientation = DATA; a.Function = AVERAGE
at2 = CellAddress(); at2.Sheet = target.RangeAddress.Sheet; at2.Column = 8; at2.Row = 0
tables.insertNewByName("LOMonths", at2, desc2)
# The dates grouped by month: on the inserted table's field (on the descriptor's it is not kept).
info = uno.createUnoStruct("com.sun.star.sheet.DataPilotFieldGroupInfo")
info.HasAutoStart = True; info.HasAutoEnd = True; info.GroupBy = MONTHS; info.HasDateValues = True
made = tables.getByName("LOMonths")
for i in range(made.DataPilotFields.Count):
    if made.DataPilotFields.getByIndex(i).Name == "Date":
        made.DataPilotFields.getByIndex(i).createDateGroup(info)
        break
doc.calculateAll()
def save(name, flt):
    p = PropertyValue(); p.Name = "FilterName"; p.Value = flt
    doc.storeToURL(uno.systemPathToFileUrl(os.path.join(out, name)), (p,))
save("lo-made.ods", "calc8")
save("lo-made.xlsx", "Calc Office Open XML")
doc.close(True)
try:
    desktop.terminate()
except Exception:
    pass
try:
    proc.wait(timeout=60)
except Exception:
    proc.kill()
)PY";

// The pivots LibreOffice made: read into the model, LibreOffice's own cells
// agreeing with ours.
void CheckLibreOfficeMade(const std::string& path, const std::string& label) {
    std::printf("\n-- %s --\n", label.c_str());
    auto book = Load(path);
    Check(book != nullptr, label + " loads");
    if (!book) return;
    const SpreadsheetSheet* sheet = book->GetSheetByName("LO Pivot");
    Check(sheet && sheet->GetPivotTables().size() == 2, label + ": two pivots on LO Pivot");
    if (!sheet || sheet->GetPivotTables().size() != 2) return;
    const SpreadsheetPivotTable& sums = sheet->GetPivotTables()[0];
    const SpreadsheetPivotTable& months = sheet->GetPivotTables()[1];
    Check(sums.unsupportedReason.empty() && months.unsupportedReason.empty(),
          label + ": both read into the model " + sums.unsupportedReason + months.unsupportedReason);
    Check(sums.rowFields.size() == 1 && sums.rowFields[0].column == Region && sums.columnFields.size() == 1 &&
              sums.columnFields[0].column == Product && sums.valueFields.size() == 1 &&
              sums.valueFields[0].column == Sales && sums.valueFields[0].summary == PivotSummary::Sum,
          label + ": regions by products, the sales summed");
    Check(months.rowFields.size() == 1 && months.rowFields[0].column == Date &&
              months.rowFields[0].grouping.by == PivotGroupBy::Months && months.valueFields.size() == 1 &&
              months.valueFields[0].summary == PivotSummary::Average,
          label + ": the dates by month, the sales averaged");
    // LibreOffice's cells: its numbers where ours are.
    auto findNumber = [&](const SpreadsheetPivotTable& pivot, const std::string& rowLabel, const std::string& colLabel) {
        const CellRange out = pivot.outputRange;
        int row = -1, col = -1;
        for (int r = out.start.row; r <= out.end.row && row < 0; ++r) {
            if (TextAt(*sheet, r, out.start.col) == rowLabel) row = r;
        }
        for (int r = out.start.row; r <= out.end.row && col < 0; ++r) {
            for (int c = out.start.col + 1; c <= out.end.col; ++c) {
                if (TextAt(*sheet, r, c) == colLabel) { col = c; break; }
            }
        }
        const SpreadsheetCell* cell = row >= 0 && col >= 0 ? sheet->GetCellIfExists(row, col) : nullptr;
        return cell && cell->TryGetNumber() ? *cell->TryGetNumber() : std::nan("");
    };
    Check(Near(findNumber(sums, "East", "Apples"), 180) && Near(findNumber(sums, "West", "Apples"), 115) &&
              Near(findNumber(sums, "North", "Plums"), 25),
          label + ": LibreOffice's sums are the hand-worked ones");
    std::string jan;
    for (int r = months.outputRange.start.row; r <= months.outputRange.end.row; ++r) {
        const std::string t = TextAt(*sheet, r, months.outputRange.start.col);
        if (t == "Jan") jan = t;
    }
    // Ours, computed into a copy of the same place, agree with LibreOffice's.
    auto copy = Load(path);
    SpreadsheetSheet* mine = copy ? copy->GetSheetByName("LO Pivot") : nullptr;
    if (!mine) return;
    copy->RefreshPivotTables();
    const SpreadsheetPivotTable& ourSums = mine->GetPivotTables()[0];
    CheckValue(*mine, ourSums, {"East"}, "Apples", 180, label + ", refreshed by us");
    CheckValue(*mine, ourSums, {"Grand Total"}, "Grand Total", 390, label + ", refreshed by us");
    const SpreadsheetPivotTable& ourMonths = mine->GetPivotTables()[1];
    CheckValue(*mine, ourMonths, {"Jan"}, PivotValueFieldCaption(ourMonths.valueFields[0], ourMonths.fieldNames), 55,
               label + ", refreshed by us: January's average");
    Check(jan == "Jan", label + ": LibreOffice groups the dates by month (Jan)");
    const double loJan = findNumber(months, "Jan", months.valueFields[0].name.empty() ? "Average - Sales" : months.valueFields[0].name);
    Check(Near(loJan, 55), label + ": LibreOffice's January average is ours, 55 (got " + Num(loJan) + ")");
}

} // namespace

int main() {
    std::printf("=== Spreadsheet pivot tables ===\n");
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / PathFromUtf8("uc-pivot-test-" + std::to_string(stamp));
    fs::create_directories(dir, ec);

    {
        auto book = BuildWorkbook();
        CheckSummaries(*book);
        CheckSeveralFields(*book);
        CheckFiltersAndGroups(*book);
        CheckGrid(book);
    }

    std::printf("\n=== .xlsx and .ods ===\n");
    auto built = BuildFileWorkbook();
    Check(built->GetSheetByName("Report")->GetPivotTables().size() == 2 && built->GetSheetByName("Pivot") &&
              built->GetSheetByName("Pivot")->GetPivotTables().size() == 1,
          "three pivots built: two on Report, one on a new sheet");
    const fs::path xlsx = dir / "pivots.xlsx";
    const fs::path ods = dir / "pivots.ods";
    Check(built->SaveToFile(PathToUtf8(xlsx)), "saved as .xlsx " + built->GetLastError());
    Check(built->SaveToFile(PathToUtf8(ods)), "saved as .ods " + built->GetLastError());
    CheckXlsxParts(PathToUtf8(xlsx));
    if (auto loaded = Load(PathToUtf8(xlsx))) {
        CheckFiles(*loaded, *built, ".xlsx read back", true);
        // A source cell changed after reading: the pivot read from the file follows.
        loaded->GetSheetByName(kData)->SetCellValue(1, Sales, 200.0);
        loaded->RefreshChangedPivotTables();
        const SpreadsheetSheet* report = loaded->GetSheetByName("Report");
        if (report && !report->GetPivotTables().empty()) {
            CheckValue(*report, report->GetPivotTables()[0], {"East"}, "Apples", 280, ".xlsx read back, its source changed");
        }
    } else {
        Check(false, ".xlsx loads");
    }
    if (auto loaded = Load(PathToUtf8(ods))) {
        CheckFiles(*loaded, *built, ".ods read back", true);
        const fs::path again = dir / "pivots-again.xlsx";
        Check(loaded->SaveToFile(PathToUtf8(again)), ".ods read back, saved as .xlsx");
        if (auto twice = Load(PathToUtf8(again))) CheckFiles(*twice, *built, ".ods -> .xlsx read back", true);
        else Check(false, ".ods -> .xlsx loads");
    } else {
        Check(false, ".ods loads");
    }

    std::printf("\n=== LibreOffice ===\n");
    if (!HaveSoffice()) {
        std::printf("  skipped: soffice is not installed\n");
    } else {
        const fs::path profile = dir / "lo-profile";
        const std::string fromXlsx = Convert(xlsx, "ods", dir / "lo-from-xlsx", profile);
        const std::string fromOds = Convert(ods, "xlsx", dir / "lo-from-ods", profile);
        if (fromXlsx.empty()) std::printf("  skipped: soffice did not convert the .xlsx\n");
        else if (auto loaded = Load(fromXlsx)) CheckFiles(*loaded, *built, "our .xlsx as LibreOffice's .ods", false);
        else Check(false, "LibreOffice's .ods loads");
        if (fromOds.empty()) std::printf("  skipped: soffice did not convert the .ods\n");
        else if (auto loaded = Load(fromOds)) CheckFiles(*loaded, *built, "our .ods as LibreOffice's .xlsx", false, true);
        else Check(false, "LibreOffice's .xlsx loads");

        // Pivots LibreOffice makes itself, over the sales we wrote.
        if (std::system("python3 -c 'import uno' > /dev/null 2>&1") != 0) {
            std::printf("  skipped: python3 has no LibreOffice UNO bridge\n");
        } else {
            auto plain = BuildWorkbook();
            const fs::path data = dir / "data.ods";
            Check(plain->SaveToFile(PathToUtf8(data)), "the sales written for LibreOffice");
            const fs::path script = dir / "make_pivots.py";
            std::ofstream(script) << kUnoScript;
            const fs::path made = dir / "lo-made";
            fs::create_directories(made, ec);
            const std::string command = "python3 '" + PathToUtf8(script) + "' '" + PathToUtf8(data) + "' '" +
                                        PathToUtf8(made) + "' '" + PathToUtf8(dir / "lo-uno-profile") + "' > /dev/null 2>&1";
            const int ran = std::system(command.c_str());
            if (ran != 0 || !fs::exists(made / "lo-made.ods", ec)) {
                std::printf("  skipped: LibreOffice did not make the pivots (%d)\n", ran);
            } else {
                CheckLibreOfficeMade(PathToUtf8(made / "lo-made.ods"), "LibreOffice's own pivots, .ods");
                CheckLibreOfficeMade(PathToUtf8(made / "lo-made.xlsx"), "LibreOffice's own pivots, .xlsx");
            }
        }
    }

    if (!GetEnvUtf8("UC_KEEP_ROUNDTRIP_FILES").empty()) {
        std::printf("\nfiles kept in %s\n", PathToUtf8(dir).c_str());
    } else {
        fs::remove_all(dir, ec);
    }
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
