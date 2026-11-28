// Tests/SpreadsheetChartRoundTripTest.cpp
// Charts on a spreadsheet (UltraCanvasSpreadsheetChart.h) through a save and
// a load in both writable workbook formats.
//
// A workbook of two sheets is built: "Sales Data" (a name that has to be
// quoted) with months down A2:A7 and two regions' sales in B and C, and
// "Points" with x and y values. It gets four charts - columns made from
// A1:C7 (two series named by row 1, the months as categories), stacked bars
// over A1:B7 moved a few pixels into their cells, a scatter chart of the
// points, and on "Points" a pie of the North column of "Sales Data". Checked:
//  - what a chart made from a block reads: its series' names, categories and
//    values, and the ranges they come from;
//  - an edited cell changes the chart's values once the charts are read
//    again (RefreshCharts), and a formula's result does too;
//  - each chart's kind, stacking, title, legend, anchor (cells and offsets)
//    and ranges survive .xlsx and .ods, and a chart read from a file reads
//    its cells again when they change;
//  - .xlsx -> .ods -> load and .ods -> .xlsx -> load keep them;
//  - where LibreOffice is installed (soffice), its conversion of our .xlsx
//    to .ods and of our .ods to .xlsx still holds the charts with their
//    kinds and ranges; without it that part is skipped;
//  - the framework's other charts (a Sankey diagram, a Gantt chart, a
//    bubble chart, a waterfall, a ternary plot, a histogram, a surface, a
//    treemap) on a sheet of their sample cells keep their kind, title, every
//    range and what they read through .xlsx and .ods: the file holds a
//    column chart of their numbers (a surface and a bubble chart their own)
//    with the UltraCanvas extension beside it, and reading puts the kind
//    back. LibreOffice, which ignores the extension, shows the column chart.
//
// Usage: SpreadsheetChartRoundTripTest
// Exit code is the number of failed checks.
// Version: 1.1.0 - the framework's other charts through the UltraCanvas extension
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetChart.h"
#include "UltraCanvasZipPackage.h"
#include "SpreadsheetChartSamples.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

const char* kSales = "Sales Data";
const char* kPoints = "Points";
const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun"};

std::string Describe(const SpreadsheetChartRange& part) {
    return part.IsSet() ? part.ToExcel() : std::string("(none)");
}

bool SameRange(const SpreadsheetChartRange& part, const std::string& sheet, int r1, int c1, int r2, int c2) {
    return part.sheet == sheet && part.range.start.row == r1 && part.range.start.col == c1 &&
           part.range.end.row == r2 && part.range.end.col == c2;
}

std::shared_ptr<UltraCanvasSpreadsheet> BuildWorkbook() {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("book", 0, 0, 1000, 700);
    book->RenameSheet(0, kSales);
    book->AddSheet(kPoints);
    SpreadsheetSheet* sales = book->GetSheet(0);
    SpreadsheetSheet* points = book->GetSheet(1);
    sales->SetCellValue(0, 0, std::string("Month"));
    sales->SetCellValue(0, 1, std::string("North"));
    sales->SetCellValue(0, 2, std::string("South"));
    for (int i = 0; i < 6; ++i) {
        sales->SetCellValue(i + 1, 0, std::string(kMonths[i]));
        sales->SetCellValue(i + 1, 1, 10.0 * (i + 1));
        sales->SetCellValue(i + 1, 2, 5.0 + i);
    }
    points->SetCellValue(0, 0, std::string("x"));
    points->SetCellValue(0, 1, std::string("y"));
    for (int i = 0; i < 5; ++i) {
        points->SetCellValue(i + 1, 0, 1.5 * i);
        points->SetCellValue(i + 1, 1, 2.0 * i * i);
    }

    // 1. Columns from A1:C7 of the sales.
    book->SetActiveSheet(0);
    book->Select(CellRange(0, 0, 6, 2));
    Check(book->InsertChartFromSelection(RichChart::Kind::Column) == 0, "columns inserted from A1:C7");
    // 2. Stacked bars over A1:B7, titled, a few pixels into their cells.
    book->Select(CellRange(0, 0, 6, 1));
    const int bars = book->InsertChartFromSelection(RichChart::Kind::Bar, true);
    Check(bars == 1, "stacked bars inserted from A1:B7");
    if (const SpreadsheetChart* made = book->GetChart(bars)) {
        SpreadsheetChart changed = *made;
        changed.chart.title = "North by month";
        changed.chart.legend = true;
        changed.anchor.from = CellAddress(9, 1);
        changed.anchor.fromOffsetX = 7;
        changed.anchor.fromOffsetY = 3;
        changed.anchor.to = CellAddress(20, 6);
        changed.anchor.toOffsetX = 12;
        changed.anchor.toOffsetY = 5;
        Check(book->SetChart(bars, changed), "the bars' title, legend and place changed");
    }
    // 3. A scatter chart of the points; 4. a pie on Points of the North column.
    book->SetActiveSheet(1);
    book->Select(CellRange(0, 0, 5, 1));
    Check(book->InsertChartFromSelection(RichChart::Kind::Scatter) == 0, "scatter inserted from Points A1:B6");
    SpreadsheetChart pie = MakeSpreadsheetChart(*sales, CellRange(0, 0, 6, 1), RichChart::Kind::Pie);
    pie.anchor.from = CellAddress(20, 0);
    pie.anchor.to = CellAddress(34, 5);
    pie.anchor.fromOffsetX = pie.anchor.fromOffsetY = pie.anchor.toOffsetX = pie.anchor.toOffsetY = 0;
    Check(book->InsertChart(pie) == 1, "a pie of the sales placed on Points");
    book->SetActiveSheet(0);
    return book;
}

const SpreadsheetChart* ChartOf(UltraCanvasSpreadsheet& book, int sheet, int index) {
    const SpreadsheetSheet* on = book.GetSheet(sheet);
    if (!on || index >= static_cast<int>(on->GetCharts().size())) return nullptr;
    return &on->GetCharts()[static_cast<size_t>(index)];
}

// `exactOffsets`: whether the file keeps the offsets to the pixel; an .ods
// keeps the box in centimetres, which reads back within a pixel.
void CheckCharts(UltraCanvasSpreadsheet& book, const std::string& label, bool exactOffsets = true,
                 bool checkNames = true) {
    std::printf("\n-- %s --\n", label.c_str());
    book.RefreshCharts();
    Check(book.GetSheetCount() == 2, label + ": two sheets");
    if (book.GetSheetCount() != 2) return;
    Check(book.GetSheet(0)->GetCharts().size() == 2, label + ": two charts on Sales Data");
    Check(book.GetSheet(1)->GetCharts().size() == 2, label + ": two charts on Points");
    auto close = [&](int a, int b) { return exactOffsets ? a == b : std::abs(a - b) <= 1; };

    // 1. Columns
    if (const SpreadsheetChart* columns = ChartOf(book, 0, 0)) {
        Check(columns->chart.kind == RichChart::Kind::Column && !columns->chart.stacked, label + ": chart 1 is columns, side by side");
        Check(columns->sources.size() == 2 && columns->chart.series.size() == 2, label + ": chart 1 has two series");
        if (columns->sources.size() == 2) {
            Check(SameRange(columns->sources[0].values, kSales, 1, 1, 6, 1), label + ": series 1 values " + Describe(columns->sources[0].values));
            Check(SameRange(columns->sources[1].values, kSales, 1, 2, 6, 2), label + ": series 2 values " + Describe(columns->sources[1].values));
            Check(SameRange(columns->sources[0].categories, kSales, 1, 0, 6, 0), label + ": categories " + Describe(columns->sources[0].categories));
            Check(SameRange(columns->sources[1].name, kSales, 0, 2, 0, 2), label + ": series 2 named by " + Describe(columns->sources[1].name));
        }
        if (columns->chart.series.size() == 2) {
            Check(columns->chart.series[0].name == "North" && columns->chart.series[1].name == "South", label + ": series named North and South");
            const std::vector<double> north = {10, 20, 30, 40, 50, 60};
            Check(columns->chart.series[0].values == north, label + ": North's values read from the cells");
        }
        Check(columns->chart.categories.size() == 6 && columns->chart.categories.front() == "Jan" &&
                  columns->chart.categories.back() == "Jun",
              label + ": the months as categories");
        Check(columns->chart.legend, label + ": chart 1 has a legend");
        Check(columns->anchor.from.row == 0 && columns->anchor.from.col == 4, label + ": chart 1 stands from E1");
        if (checkNames) Check(columns->name == "Chart 1", label + ": chart 1 is called Chart 1 (" + columns->name + ")");
    } else {
        Check(false, label + ": chart 1 there");
    }
    // 2. Stacked bars, placed by hand
    if (const SpreadsheetChart* bars = ChartOf(book, 0, 1)) {
        Check(bars->chart.kind == RichChart::Kind::Bar && bars->chart.stacked, label + ": chart 2 is bars, stacked");
        Check(bars->chart.title == "North by month", label + ": chart 2's title (" + bars->chart.title + ")");
        Check(bars->chart.legend, label + ": chart 2 has a legend");
        const SpreadsheetChartAnchor& a = bars->anchor;
        Check(a.from.row == 9 && a.from.col == 1 && a.to.row == 20 && a.to.col == 6, label + ": chart 2 from B10 to G21");
        Check(close(a.fromOffsetX, 7) && close(a.fromOffsetY, 3) && close(a.toOffsetX, 12) && close(a.toOffsetY, 5),
              label + ": chart 2's offsets into its cells (" + std::to_string(a.fromOffsetX) + "," + std::to_string(a.fromOffsetY) +
                  " / " + std::to_string(a.toOffsetX) + "," + std::to_string(a.toOffsetY) + ")");
        Check(bars->sources.size() == 1 && SameRange(bars->sources[0].values, kSales, 1, 1, 6, 1), label + ": chart 2 reads B2:B7");
    } else {
        Check(false, label + ": chart 2 there");
    }
    // 3. Scatter
    if (const SpreadsheetChart* scatter = ChartOf(book, 1, 0)) {
        Check(scatter->chart.kind == RichChart::Kind::Scatter, label + ": chart 3 is a scatter chart");
        Check(scatter->sources.size() == 1, label + ": chart 3 has one series");
        if (!scatter->sources.empty()) {
            Check(SameRange(scatter->sources[0].categories, kPoints, 1, 0, 5, 0), label + ": its x values " + Describe(scatter->sources[0].categories));
            Check(SameRange(scatter->sources[0].values, kPoints, 1, 1, 5, 1), label + ": its y values " + Describe(scatter->sources[0].values));
        }
        if (!scatter->chart.series.empty()) {
            const std::vector<double> xs = {0, 1.5, 3, 4.5, 6}, ys = {0, 2, 8, 18, 32};
            Check(scatter->chart.series[0].xValues == xs && scatter->chart.series[0].values == ys, label + ": its points read from the cells");
        }
    } else {
        Check(false, label + ": chart 3 there");
    }
    // 4. A pie of the other sheet's cells
    if (const SpreadsheetChart* pie = ChartOf(book, 1, 1)) {
        Check(pie->chart.kind == RichChart::Kind::Pie, label + ": chart 4 is a pie");
        Check(pie->sources.size() == 1 && SameRange(pie->sources[0].values, kSales, 1, 1, 6, 1),
              label + ": chart 4 reads Sales Data's B2:B7 from Points");
        Check(pie->anchor.from.row == 20 && pie->anchor.from.col == 0 && pie->anchor.to.row == 34 && pie->anchor.to.col == 5,
              label + ": chart 4 from A21 to F35");
        Check(!pie->chart.series.empty() && pie->chart.series[0].values.size() == 6 && pie->chart.series[0].values[2] == 30,
              label + ": the pie's slices from the cells");
    } else {
        Check(false, label + ": chart 4 there");
    }

    // The cells change; the charts follow.
    book.GetSheet(0)->SetCellValue(3, 1, 333.0);
    book.RefreshCharts();
    const SpreadsheetChart* columns = ChartOf(book, 0, 0);
    const SpreadsheetChart* pie = ChartOf(book, 1, 1);
    Check(columns && !columns->chart.series.empty() && columns->chart.series[0].values.size() == 6 &&
              columns->chart.series[0].values[2] == 333.0,
          label + ": an edited cell changes chart 1");
    Check(pie && !pie->chart.series.empty() && pie->chart.series[0].values.size() == 6 && pie->chart.series[0].values[2] == 333.0,
          label + ": ...and the pie on the other sheet");
    book.GetSheet(0)->SetCellValue(3, 1, 30.0);
    book.RefreshCharts();
}

std::shared_ptr<UltraCanvasSpreadsheet> Load(const std::string& path) {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("loaded", 0, 0, 1000, 700);
    if (!book->LoadFromFile(path)) {
        std::printf("      cannot load %s: %s\n", path.c_str(), book->GetLastError().c_str());
        return nullptr;
    }
    return book;
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

// What LibreOffice keeps of a chart: its kind and its ranges.
void CheckConverted(UltraCanvasSpreadsheet& book, const std::string& label) {
    std::printf("\n-- %s --\n", label.c_str());
    book.RefreshCharts();
    const SpreadsheetSheet* sales = book.GetSheetByName(kSales);
    const SpreadsheetSheet* points = book.GetSheetByName(kPoints);
    Check(sales && sales->GetCharts().size() == 2, label + ": two charts on Sales Data");
    Check(points && points->GetCharts().size() == 2, label + ": two charts on Points");
    if (!sales || !points || sales->GetCharts().size() != 2 || points->GetCharts().size() != 2) return;
    const SpreadsheetChart& columns = sales->GetCharts()[0];
    Check(columns.chart.kind == RichChart::Kind::Column && columns.sources.size() == 2 &&
              SameRange(columns.sources[1].values, kSales, 1, 2, 6, 2),
          label + ": the columns and their second series' range");
    Check(columns.chart.series.size() == 2 && columns.chart.series[0].values.size() == 6 &&
              columns.chart.series[0].values[5] == 60.0,
          label + ": the columns read from the cells");
    const SpreadsheetChart& bars = sales->GetCharts()[1];
    Check(bars.chart.kind == RichChart::Kind::Bar && bars.chart.stacked && bars.chart.title == "North by month",
          label + ": the stacked bars and their title");
    Check(bars.anchor.from.row == 9 && bars.anchor.from.col == 1, label + ": the bars still stand from B10");
    Check(points->GetCharts()[0].chart.kind == RichChart::Kind::Scatter, label + ": the scatter chart");
    Check(points->GetCharts()[1].chart.kind == RichChart::Kind::Pie && points->GetCharts()[1].sources.size() == 1 &&
              SameRange(points->GetCharts()[1].sources[0].values, kSales, 1, 1, 6, 1),
          label + ": the pie of the other sheet's cells");
}

// ----- the framework's other charts ---------------------------------------------

const char* kKinds = "Kinds";
const char* const kKindTypes[] = {"sankey", "gantt", "bubble", "waterfall", "ternary", "histogram", "surface", "treemap"};

// A sheet of sample blocks, side by side, and a chart of each.
std::shared_ptr<UltraCanvasSpreadsheet> BuildKindsWorkbook() {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("kinds", 0, 0, 1000, 700);
    book->RenameSheet(0, kKinds);
    SpreadsheetSheet* sheet = book->GetSheet(0);
    int column = 0, row = 40;
    for (const char* id : kKindTypes) {
        const int type = SpreadsheetChartTypeById(id);
        Check(type >= 0, std::string("the type ") + id);
        if (type < 0) continue;
        const CellRange block =
            ChartSamples::WriteSampleCells(*sheet, SpreadsheetChartTypes()[static_cast<size_t>(type)].kind, 0, column);
        SpreadsheetChart chart = MakeSpreadsheetChartOfType(*sheet, block, static_cast<size_t>(type));
        chart.chart.title = std::string("A ") + id;
        chart.anchor.from = CellAddress(row, 0);
        chart.anchor.to = CellAddress(row + 12, 6);
        row += 14;
        Check(SpreadsheetChartFitMessage(chart).empty(), std::string(id) + ": its sample fits");
        Check(book->InsertChart(chart) >= 0, std::string(id) + ": inserted");
        column = block.end.col + 2;
    }
    book->RefreshCharts();
    return book;
}

// The charts of `book` match those of `built`: kind, title, ranges and what
// they read from the cells.
void CheckKinds(UltraCanvasSpreadsheet& book, const UltraCanvasSpreadsheet& built, const std::string& label) {
    std::printf("\n-- %s --\n", label.c_str());
    book.RefreshCharts();
    const SpreadsheetSheet* sheet = book.GetSheetByName(kKinds);
    const SpreadsheetSheet* original = built.GetSheet(0);
    Check(sheet && original && sheet->GetCharts().size() == original->GetCharts().size(), label + ": every chart is there");
    if (!sheet || !original || sheet->GetCharts().size() != original->GetCharts().size()) return;
    for (size_t i = 0; i < original->GetCharts().size(); ++i) {
        const SpreadsheetChart& want = original->GetCharts()[i];
        const SpreadsheetChart& have = sheet->GetCharts()[i];
        const std::string what = label + ": " + SpreadsheetChartKindId(want.chart.kind);
        Check(have.chart.kind == want.chart.kind,
              what + " keeps its kind (read " + SpreadsheetChartKindId(have.chart.kind) + ")");
        Check(have.chart.title == want.chart.title, what + " keeps its title");
        bool sameRanges = have.sources.size() == want.sources.size();
        for (size_t s = 0; sameRanges && s < want.sources.size(); ++s) {
            sameRanges = have.sources[s].values == want.sources[s].values && have.sources[s].name == want.sources[s].name &&
                         have.sources[s].categories == want.sources[s].categories &&
                         have.sources[s].sizes == want.sources[s].sizes;
        }
        Check(sameRanges, what + " keeps its " + std::to_string(want.sources.size()) + " series' ranges");
        bool sameData = have.chart.series.size() == want.chart.series.size() && have.chart.categories == want.chart.categories;
        for (size_t s = 0; sameData && s < want.chart.series.size(); ++s) {
            sameData = have.chart.series[s].values == want.chart.series[s].values &&
                       have.chart.series[s].labels == want.chart.series[s].labels &&
                       have.chart.series[s].sizes == want.chart.series[s].sizes;
        }
        Check(sameData, what + " reads the same data from its cells");
    }
}

// What LibreOffice keeps of the kinds' file: a chart for each, showing
// numbers - the kinds it has a form for as themselves, the others as the
// column chart beside the extension.
void CheckKindsConverted(UltraCanvasSpreadsheet& book, const UltraCanvasSpreadsheet& built, const std::string& label) {
    std::printf("\n-- %s --\n", label.c_str());
    book.RefreshCharts();
    const SpreadsheetSheet* sheet = book.GetSheetByName(kKinds);
    const SpreadsheetSheet* original = built.GetSheet(0);
    Check(sheet && original && sheet->GetCharts().size() == original->GetCharts().size(),
          label + ": a chart for each (" + std::to_string(sheet ? sheet->GetCharts().size() : 0) + ")");
    if (!sheet || !original || sheet->GetCharts().size() != original->GetCharts().size()) return;
    for (size_t i = 0; i < original->GetCharts().size(); ++i) {
        const SpreadsheetChart& want = original->GetCharts()[i];
        const SpreadsheetChart& have = sheet->GetCharts()[i];
        const std::string what = label + ": " + SpreadsheetChartKindId(want.chart.kind);
        bool numbers = false;
        for (const RichChartSeries& one : have.chart.series) numbers = numbers || !one.values.empty();
        Check(numbers, what + " shows as a chart of numbers (" + SpreadsheetChartKindId(have.chart.kind) + ")");
    }
}

// The chart parts of the kinds' .xlsx: a column chart with the extension
// for each kind .xlsx has no form for, a chart of its own kind for the rest.
void CheckXlsxParts(const std::string& path) {
    UCZipPackageReader package;
    Check(package.Open(path), "the kinds' .xlsx opens as a package");
    int fallbacks = 0, natives = 0;
    for (int n = 1; n <= 8; ++n) {
        std::string xml;
        if (!package.ReadEntry("xl/charts/chart" + std::to_string(n) + ".xml", xml)) continue;
        const bool extension = xml.find("<c:extLst><c:ext uri=") != std::string::npos &&
                               xml.find("ucc:chart kind=") != std::string::npos;
        if (extension && xml.find("<c:barChart>") != std::string::npos) ++fallbacks;
        if (!extension) ++natives;
    }
    Check(fallbacks == 6 && natives == 2,
          "the .xlsx holds 6 column charts with the extension and 2 of their own kind (bubble, surface): " +
              std::to_string(fallbacks) + " and " + std::to_string(natives));
}

} // namespace

int main() {
    std::printf("=== Spreadsheet charts through .xlsx and .ods ===\n");
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / PathFromUtf8("uc-chart-roundtrip-" + std::to_string(stamp));
    fs::create_directories(dir, ec);

    // The ranges' two spellings.
    SpreadsheetChartRange range;
    Check(SpreadsheetChartRange::FromExcel("'Sales Data'!$B$2:$B$7", range) && SameRange(range, kSales, 1, 1, 6, 1),
          "Excel's 'Sales Data'!$B$2:$B$7");
    Check(range.ToExcel() == "'Sales Data'!$B$2:$B$7" && range.ToOdf() == "'Sales Data'.$B$2:.$B$7",
          "written back as Excel and ODF spell it (" + range.ToExcel() + ", " + range.ToOdf() + ")");
    Check(SpreadsheetChartRange::FromOdf("$Sheet1.$A$2:$Sheet1.$A$5", range) && SameRange(range, "Sheet1", 1, 0, 4, 0),
          "ODF's $Sheet1.$A$2:$Sheet1.$A$5");
    Check(SpreadsheetChartRange::FromOdf("'It''s'.C3", range) && SameRange(range, "It's", 2, 2, 2, 2), "ODF's 'It''s'.C3");

    auto book = BuildWorkbook();
    CheckCharts(*book, "the workbook as built");

    // A formula's result is a chart's value too.
    {
        SpreadsheetSheet* sales = book->GetSheet(0);
        sales->SetCellFormula(6, 2, "=B7*2");
        book->RefreshCharts();
        const SpreadsheetChart* columns = ChartOf(*book, 0, 0);
        Check(columns && columns->chart.series.size() == 2 && columns->chart.series[1].values.size() == 6 &&
                  columns->chart.series[1].values[5] == 120.0,
              "a formula's result (=B7*2) is the South series' last value");
        sales->SetCellValue(6, 2, 10.0);
        book->RefreshCharts();
    }

    const fs::path xlsx = dir / "charts.xlsx";
    const fs::path ods = dir / "charts.ods";
    Check(book->SaveToFile(PathToUtf8(xlsx)), "saved as .xlsx " + book->GetLastError());
    Check(book->SaveToFile(PathToUtf8(ods)), "saved as .ods " + book->GetLastError());

    if (auto loaded = Load(PathToUtf8(xlsx))) CheckCharts(*loaded, ".xlsx loaded");
    else Check(false, ".xlsx loads");
    if (auto loaded = Load(PathToUtf8(ods))) CheckCharts(*loaded, ".ods loaded", false);
    else Check(false, ".ods loads");

    if (auto loaded = Load(PathToUtf8(xlsx))) {
        const fs::path again = dir / "again.ods";
        Check(loaded->SaveToFile(PathToUtf8(again)), ".xlsx loaded, saved as .ods");
        if (auto twice = Load(PathToUtf8(again))) CheckCharts(*twice, ".xlsx -> .ods loaded", false);
        else Check(false, ".xlsx -> .ods loads");
    }
    if (auto loaded = Load(PathToUtf8(ods))) {
        const fs::path again = dir / "again.xlsx";
        Check(loaded->SaveToFile(PathToUtf8(again)), ".ods loaded, saved as .xlsx");
        if (auto twice = Load(PathToUtf8(again))) CheckCharts(*twice, ".ods -> .xlsx loaded", false);
        else Check(false, ".ods -> .xlsx loads");
    }

    // The framework's other charts.
    std::printf("\n=== The framework's other charts ===\n");
    auto kinds = BuildKindsWorkbook();
    CheckKinds(*kinds, *kinds, "the kinds as built");
    const fs::path kindsXlsx = dir / "kinds.xlsx";
    const fs::path kindsOds = dir / "kinds.ods";
    Check(kinds->SaveToFile(PathToUtf8(kindsXlsx)), "the kinds saved as .xlsx " + kinds->GetLastError());
    Check(kinds->SaveToFile(PathToUtf8(kindsOds)), "the kinds saved as .ods " + kinds->GetLastError());
    CheckXlsxParts(PathToUtf8(kindsXlsx));
    if (auto loaded = Load(PathToUtf8(kindsXlsx))) CheckKinds(*loaded, *kinds, "the kinds' .xlsx loaded");
    else Check(false, "the kinds' .xlsx loads");
    if (auto loaded = Load(PathToUtf8(kindsOds))) {
        CheckKinds(*loaded, *kinds, "the kinds' .ods loaded");
        const fs::path again = dir / "kinds-again.xlsx";
        Check(loaded->SaveToFile(PathToUtf8(again)), "the kinds' .ods loaded, saved as .xlsx");
        if (auto twice = Load(PathToUtf8(again))) CheckKinds(*twice, *kinds, "the kinds' .ods -> .xlsx loaded");
        else Check(false, "the kinds' .ods -> .xlsx loads");
    } else {
        Check(false, "the kinds' .ods loads");
    }

    std::printf("\n=== LibreOffice reads them too ===\n");
    if (!HaveSoffice()) {
        std::printf("  skipped: soffice is not installed\n");
    } else {
        const fs::path profile = dir / "lo-profile";
        const std::string fromXlsx = Convert(xlsx, "ods", dir / "lo-from-xlsx", profile);
        const std::string fromOds = Convert(ods, "xlsx", dir / "lo-from-ods", profile);
        if (fromXlsx.empty()) std::printf("  skipped: soffice did not convert the .xlsx\n");
        else if (auto loaded = Load(fromXlsx)) CheckConverted(*loaded, "our .xlsx as LibreOffice's .ods");
        else Check(false, "LibreOffice's .ods loads");
        if (fromOds.empty()) std::printf("  skipped: soffice did not convert the .ods\n");
        else if (auto loaded = Load(fromOds)) CheckConverted(*loaded, "our .ods as LibreOffice's .xlsx");
        else Check(false, "LibreOffice's .xlsx loads");
        // The kinds: LibreOffice ignores the extension and shows the column
        // chart beside it.
        const std::string kindsFromXlsx = Convert(kindsXlsx, "ods", dir / "lo-kinds-xlsx", profile);
        const std::string kindsFromOds = Convert(kindsOds, "xlsx", dir / "lo-kinds-ods", profile);
        if (kindsFromXlsx.empty()) std::printf("  skipped: soffice did not convert the kinds' .xlsx\n");
        else if (auto loaded = Load(kindsFromXlsx)) CheckKindsConverted(*loaded, *kinds, "the kinds' .xlsx as LibreOffice's .ods");
        else Check(false, "LibreOffice's .ods of the kinds loads");
        if (kindsFromOds.empty()) std::printf("  skipped: soffice did not convert the kinds' .ods\n");
        else if (auto loaded = Load(kindsFromOds)) CheckKindsConverted(*loaded, *kinds, "the kinds' .ods as LibreOffice's .xlsx");
        else Check(false, "LibreOffice's .xlsx of the kinds loads");
    }

    if (!GetEnvUtf8("UC_KEEP_ROUNDTRIP_FILES").empty()) {
        std::printf("\nfiles kept in %s\n", PathToUtf8(dir).c_str());
    } else {
        fs::remove_all(dir, ec);
    }
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
