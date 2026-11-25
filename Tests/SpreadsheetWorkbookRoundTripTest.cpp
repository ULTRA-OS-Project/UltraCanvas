// Tests/SpreadsheetWorkbookRoundTripTest.cpp
// What a sheet holds besides its cells, through a save and a load in both
// writable workbook formats. Until 2026-11 the .xlsx and .ods writers wrote
// cells, sizes and merges only, and a workbook opened and saved again lost:
//  - its frozen rows and columns;
//  - its hidden rows and columns (they came back shown);
//  - its named ranges, of the workbook and of each sheet;
//  - its auto-filter and the values it filtered on;
//  - its print area and print settings (orientation, paper, margins, scale or
//    fit to pages, gridlines and headings, centring, header and footer,
//    print titles, manual page breaks).
// The .ods reader also lost a merge whose first cell was empty, and took the
// cells after a covered cell one column too far left.
//
// A workbook of two sheets holding all of it is saved as .xlsx and .ods into a
// temporary folder, each loaded into a fresh spreadsheet and checked. Where
// LibreOffice is installed (soffice), the .xlsx is also converted to .ods and
// to .xlsx and the .ods to .xlsx by it, and those are loaded and checked for
// the frozen panes, hidden rows and columns, names, filter and print areas;
// without it that part is skipped. (An .ods that `soffice --convert-to`
// writes holds no view settings, so no frozen panes, whatever it was made
// from: that one check is left out there.)
//
// Usage: SpreadsheetWorkbookRoundTripTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSpreadsheet.h"

#include <algorithm>
#include <chrono>
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

bool Near(float a, float b) { return a - b < 0.011f && b - a < 0.011f; }

bool SameRange(const CellRange& range, int r1, int c1, int r2, int c2) {
    return range.start.row == r1 && range.start.col == c1 && range.end.row == r2 &&
           range.end.col == c2;
}

std::string Describe(const CellRange& range) {
    return range.ToString() + (range.start.sheetName.empty() ? "" : " on " + range.start.sheetName);
}

const char* kData = "Data Two";   // a sheet name that has to be quoted

// Sheet1: a 20 x 4 block with a header row, Data Two: three amounts.
std::shared_ptr<UltraCanvasSpreadsheet> BuildWorkbook() {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("book", 0, 0, 900, 600);
    book->AddSheet(kData);
    SpreadsheetSheet* first = book->GetSheet(0);
    SpreadsheetSheet* data = book->GetSheet(1);

    first->SetCellValue(0, 0, std::string("Name"));
    first->SetCellValue(0, 1, std::string("Group"));
    first->SetCellValue(0, 2, std::string("Amount"));
    first->SetCellValue(0, 3, std::string("Note"));
    for (int row = 1; row < 20; ++row) {
        first->SetCellValue(row, 0, std::string("Item ") + std::to_string(row));
        first->SetCellValue(row, 1, std::string(row % 3 == 0 ? "North" : row % 3 == 1 ? "South" : "East"));
        first->SetCellValue(row, 2, static_cast<double>(row * 10));
        first->SetCellValue(row, 3, std::string("n") + std::to_string(row));
    }
    first->SetCellValue(0, 5, std::string("Merged"));
    first->MergeCells(0, 5, 1, 6);                       // F1:G2

    data->SetCellValue(0, 0, 3.0);
    data->SetCellValue(1, 0, 4.0);
    data->SetCellValue(2, 0, 5.0);
    data->SetCellValue(7, 6, std::string("far"));        // G8
    data->MergeCells(4, 3, 4, 4);                        // D5:E5, its first cell empty

    // 1. Frozen panes
    first->SetFreezePanes(2, 1);
    data->FreezeRows(1);

    // 2. Hidden rows and columns
    first->SetRowHidden(14, true);                       // row 15, outside the filter
    first->SetColumnHidden(3, true);                     // column D
    data->SetRowHidden(30, true);                        // beyond the used range
    data->SetColumnHidden(9, true);                      // column J, beyond it too

    // 3. Named ranges: the workbook's, one scoped to Data Two, and one name
    // standing for each.
    CellRange total(1, 2, 4, 2);                         // Sheet1.C2:C5
    total.start.sheetName = total.end.sheetName = "Sheet1";
    book->AddNamedRange("Total", total);
    book->AddNamedRange("Local", CellRange(0, 0, 2, 0), kData);   // no sheet: its own
    CellRange rateWorkbook(1, 2, 1, 2);                  // Sheet1.C2 = 10
    rateWorkbook.start.sheetName = rateWorkbook.end.sheetName = "Sheet1";
    book->AddNamedRange("Rate", rateWorkbook);
    book->AddNamedRange("Rate", CellRange(1, 0, 1, 0), kData);    // Data Two.A2 = 4
    CellRange ahead(0, 0, 1, 0);                         // scoped to Sheet1, covering the
    ahead.start.sheetName = ahead.end.sheetName = kData; // sheet after it: Data Two.A1:A2
    book->AddNamedRange("Ahead", ahead, "Sheet1");
    book->SetActiveSheet(1);
    book->SetCellFormula(5, 0, "=SUM(Local)");           // Data Two.A6 = 12
    book->SetCellFormula(6, 0, "=Rate*2");               // Data Two.A7 = 8
    book->SetActiveSheet(0);
    book->SetCellFormula(21, 2, "=SUM(Total)+Rate");     // Sheet1.C22 = 100 + 10

    // 4. Auto-filter, its column B filtered to two values
    first->SetAutoFilter(CellRange(0, 0, 9, 3));         // A1:D10
    ColumnFilter groups;
    groups.op = FilterOperator::Equals;
    groups.values = {"North", "South"};
    first->ApplyFilter(1, groups);

    // 5. Print area and print settings
    PrintSettings& print = first->GetPrintSettings();
    print.orientation = PageOrientation::Landscape;
    print.paperSize = PaperSize::A4;
    print.marginLeft = 0.5f;
    print.marginRight = 0.6f;
    print.marginTop = 1.0f;
    print.marginBottom = 0.9f;
    print.headerMargin = 0.4f;
    print.footerMargin = 0.35f;
    print.printGridlines = true;
    print.printRowColHeaders = true;
    print.centerHorizontally = true;
    print.scale = 80;
    print.headerLeft = "Left &P";
    print.footerCenter = "Page &P of &N";
    print.repeatRowsFirst = 0;
    print.repeatRowsLast = 1;
    print.repeatColumnsFirst = 0;
    print.repeatColumnsLast = 0;
    print.rowBreaks = {12};
    print.colBreaks = {3};
    first->SetPrintArea(CellRange(0, 0, 9, 3));          // A1:D10

    PrintSettings& dataPrint = data->GetPrintSettings();
    dataPrint.paperSize = PaperSize::Legal;
    dataPrint.fitToWidth = 1;
    dataPrint.fitToHeight = 0;
    dataPrint.centerVertically = true;
    data->SetPrintArea(CellRange(0, 0, 2, 1));           // A1:B3

    book->Recalculate();
    return book;
}

// `full`: our own round trip; otherwise a file converted by LibreOffice, of
// which only what the task asks of another program is checked. `freeze`:
// whether the file can hold the frozen panes - `soffice --convert-to` writes
// an .ods without view settings at all (it has no view to take them from),
// so an .ods it wrote has none, from any source.
void CheckWorkbook(UltraCanvasSpreadsheet& book, const std::string& label, bool full,
                   bool freeze = true) {
    std::printf("\n-- %s --\n", label.c_str());
    Check(book.GetSheetCount() == 2, label + ": two sheets");
    if (book.GetSheetCount() != 2) return;
    SpreadsheetSheet* first = book.GetSheet(0);
    SpreadsheetSheet* data = book.GetSheet(1);
    Check(first->GetName() == "Sheet1" && data->GetName() == kData,
          label + ": the sheet names [" + first->GetName() + ", " + data->GetName() + "]");
    Check(first->GetCellDisplayValue(3, 0) == "Item 3", label + ": a cell's text");
    Check(first->GetCellNumber(5, 2) == 50.0, label + ": a cell's number");

    // 1. Frozen panes
    if (freeze) {
        Check(first->GetFrozenRowCount() == 2 && first->GetFrozenColumnCount() == 1,
              label + ": Sheet1 frozen at 2 rows, 1 column [" +
                  std::to_string(first->GetFrozenRowCount()) + ", " +
                  std::to_string(first->GetFrozenColumnCount()) + "]");
        Check(data->GetFrozenRowCount() == 1 && data->GetFrozenColumnCount() == 0,
              label + ": Data Two frozen at 1 row");
    } else {
        std::printf("  not checked: frozen panes (soffice --convert-to writes no view settings)\n");
    }

    // 2. Hidden rows and columns
    Check(first->IsRowHidden(14) && !first->IsRowHidden(13) && !first->IsRowHidden(15),
          label + ": Sheet1 row 15 hidden, its neighbours not");
    Check(first->IsColumnHidden(3) && !first->IsColumnHidden(2) && !first->IsColumnHidden(4),
          label + ": Sheet1 column D hidden, its neighbours not");
    Check(data->IsRowHidden(30), label + ": Data Two row 31 hidden (beyond the used range)");
    Check(data->IsColumnHidden(9), label + ": Data Two column J hidden (beyond the used range)");

    // 3. Named ranges
    CellRange total = book.GetNamedRange("Total", "");
    Check(SameRange(total, 1, 2, 4, 2) && total.start.sheetName == "Sheet1",
          label + ": workbook name Total = Sheet1.C2:C5 [" + Describe(total) + "]");
    CellRange local = book.ResolveNamedRange("Local", kData);
    Check(SameRange(local, 0, 0, 2, 0) && local.start.sheetName == kData,
          label + ": Local, scoped to Data Two, covers Data Two.A1:A3 [" + Describe(local) + "]");
    Check(!book.IsNamedRangeVisible("Local", "Sheet1"),
          label + ": Local stays scoped to its sheet (not seen from Sheet1)");
    Check(!book.HasNamedRange("Local", ""), label + ": Local is not a workbook name");
    CellRange rateHere = book.ResolveNamedRange("Rate", kData);
    CellRange rateThere = book.ResolveNamedRange("Rate", "Sheet1");
    Check(SameRange(rateHere, 1, 0, 1, 0) && rateHere.start.sheetName == kData,
          label + ": Rate on Data Two is its own [" + Describe(rateHere) + "]");
    Check(SameRange(rateThere, 1, 2, 1, 2) && rateThere.start.sheetName == "Sheet1",
          label + ": Rate elsewhere is the workbook's [" + Describe(rateThere) + "]");
    CellRange ahead = book.ResolveNamedRange("Ahead", "Sheet1");
    Check(SameRange(ahead, 0, 0, 1, 0) && ahead.start.sheetName == kData &&
              !book.IsNamedRangeVisible("Ahead", kData),
          label + ": Ahead, scoped to Sheet1, covers the next sheet's A1:A2 [" + Describe(ahead) + "]");
    // ... and the formulas using them. (Through LibreOffice too: an .ods
    // formula's "of:" prefix was written without its namespace until
    // 2026-11, and LibreOffice opened every formula as an error.)
    Check(data->GetCellNumber(5, 0) == 12.0, label + ": =SUM(Local) on Data Two = 12 [" +
                                                 data->GetCellDisplayValue(5, 0) + "]");
    Check(data->GetCellNumber(6, 0) == 8.0, label + ": =Rate*2 on Data Two = 8 [" +
                                                data->GetCellDisplayValue(6, 0) + "]");
    Check(first->GetCellNumber(21, 2) == 110.0, label + ": =SUM(Total)+Rate on Sheet1 = 110 [" +
                                                    first->GetCellDisplayValue(21, 2) + "]");

    // 4. Auto-filter
    Check(first->HasAutoFilter() && SameRange(first->GetAutoFilter()->range, 0, 0, 9, 3),
          label + ": Sheet1 auto-filter on A1:D10" +
              (first->HasAutoFilter() ? " [" + Describe(first->GetAutoFilter()->range) + "]" : ""));
    Check(!data->HasAutoFilter(), label + ": Data Two has none");
    if (first->HasAutoFilter()) {
        const auto& filters = first->GetAutoFilter()->columnFilters;
        bool found = false;
        for (const ColumnFilter& f : filters) {
            std::vector<std::string> values = f.values;
            std::sort(values.begin(), values.end());
            if (f.column == 1 && f.op == FilterOperator::Equals &&
                values == std::vector<std::string>{"North", "South"}) {
                found = true;
            }
        }
        Check(found, label + ": column B filtered to North and South");
        // Row 3 (Item 2) is East: the filter hides it; row 2 (Item 1) is South.
        Check(first->IsRowHidden(2) && !first->IsRowHidden(1),
              label + ": the filter hides the East rows and shows the others");
    }

    // 5. Print area and settings
    const PrintSettings& print = first->GetPrintSettings();
    Check(print.HasPrintArea() && SameRange(print.printArea, 0, 0, 9, 3),
          label + ": Sheet1 print area A1:D10 [" + Describe(print.printArea) + "]");
    const PrintSettings& dataPrint = data->GetPrintSettings();
    Check(dataPrint.HasPrintArea() && SameRange(dataPrint.printArea, 0, 0, 2, 1),
          label + ": Data Two print area A1:B3 [" + Describe(dataPrint.printArea) + "]");
    if (!full) return;
    Check(print.orientation == PageOrientation::Landscape, label + ": landscape");
    Check(print.paperSize == PaperSize::A4, label + ": A4");
    Check(Near(print.marginLeft, 0.5f) && Near(print.marginRight, 0.6f) &&
              Near(print.marginTop, 1.0f) && Near(print.marginBottom, 0.9f),
          label + ": the page margins [" + std::to_string(print.marginLeft) + " " +
              std::to_string(print.marginRight) + " " + std::to_string(print.marginTop) + " " +
              std::to_string(print.marginBottom) + "]");
    Check(Near(print.headerMargin, 0.4f) && Near(print.footerMargin, 0.35f),
          label + ": the header and footer margins [" + std::to_string(print.headerMargin) + " " +
              std::to_string(print.footerMargin) + "]");
    Check(print.printGridlines && print.printRowColHeaders, label + ": gridlines and headings printed");
    Check(print.centerHorizontally && !print.centerVertically, label + ": centred across the page");
    Check(print.scale == 80 && print.fitToWidth == 0 && print.fitToHeight == 0,
          label + ": scaled to 80% [" + std::to_string(print.scale) + "]");
    Check(print.headerLeft == "Left &P" && print.headerCenter.empty() && print.headerRight.empty(),
          label + ": the header [" + print.headerLeft + "|" + print.headerCenter + "|" +
              print.headerRight + "]");
    Check(print.footerCenter == "Page &P of &N" && print.footerLeft.empty(),
          label + ": the footer [" + print.footerLeft + "|" + print.footerCenter + "|" +
              print.footerRight + "]");
    Check(print.repeatRowsFirst == 0 && print.repeatRowsLast == 1,
          label + ": rows 1-2 repeated on every page");
    Check(print.repeatColumnsFirst == 0 && print.repeatColumnsLast == 0,
          label + ": column A repeated on every page");
    Check(print.rowBreaks == std::vector<int>{12} && print.colBreaks == std::vector<int>{3},
          label + ": the page breaks before row 13 and column D");
    Check(dataPrint.paperSize == PaperSize::Legal && dataPrint.orientation == PageOrientation::Portrait,
          label + ": Data Two on Legal, portrait");
    Check(dataPrint.fitToWidth == 1 && dataPrint.fitToHeight == 0,
          label + ": Data Two fitted to one page wide [" + std::to_string(dataPrint.fitToWidth) + "x" +
              std::to_string(dataPrint.fitToHeight) + "]");
    Check(dataPrint.centerVertically && !dataPrint.centerHorizontally,
          label + ": Data Two centred down the page");
    Check(!dataPrint.printGridlines && dataPrint.headerLeft.empty() && dataPrint.footerCenter.empty(),
          label + ": Data Two prints no gridlines, header or footer");

    // Merged cells
    Check(first->IsCellMerged(1, 6) && SameRange(first->GetMergedRange(0, 5), 0, 5, 1, 6),
          label + ": Sheet1 F1:G2 merged");
    Check(first->GetCellDisplayValue(0, 5) == "Merged", label + ": the merge keeps its text");
    Check(data->IsCellMerged(4, 4) && SameRange(data->GetMergedRange(4, 3), 4, 3, 4, 4),
          label + ": Data Two D5:E5 merged, its first cell empty");
    Check(data->GetCellDisplayValue(7, 6) == "far", label + ": a cell after the merge in its column");
}

std::shared_ptr<UltraCanvasSpreadsheet> Load(const std::string& path) {
    auto book = std::make_shared<UltraCanvasSpreadsheet>("loaded", 0, 0, 900, 600);
    if (!book->LoadFromFile(path)) {
        std::printf("      cannot load %s: %s\n", path.c_str(), book->GetLastError().c_str());
        return nullptr;
    }
    return book;
}

bool HaveSoffice() {
#ifdef _WIN32
    return false;   // the interoperability part runs where LibreOffice is on the PATH
#else
    return std::system("command -v soffice > /dev/null 2>&1") == 0;
#endif
}

// soffice --headless --convert-to <ext>, into its own folder, with a profile
// of its own so a LibreOffice already running does not take the job.
std::string Convert(const fs::path& source, const std::string& ext, const fs::path& outDir,
                    const fs::path& profile) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    const std::string command = "soffice -env:UserInstallation=file://" + PathToUtf8(profile) +
                                " --headless --convert-to " + ext + " --outdir '" +
                                PathToUtf8(outDir) + "' '" + PathToUtf8(source) +
                                "' > /dev/null 2>&1";
    if (std::system(command.c_str()) != 0) return "";
    const fs::path result = outDir / PathFromUtf8(PathToUtf8(source.stem()) + "." + ext);
    return fs::exists(result, ec) ? PathToUtf8(result) : std::string();
}

} // namespace

int main() {
    std::printf("=== Spreadsheet: what a workbook holds, through .xlsx and .ods ===\n");
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() /
                         PathFromUtf8("uc-workbook-roundtrip-" + std::to_string(stamp));
    fs::create_directories(dir, ec);

    auto book = BuildWorkbook();
    CheckWorkbook(*book, "the workbook as built", true);

    const fs::path xlsx = dir / "book.xlsx";
    const fs::path ods = dir / "book.ods";
    Check(book->SaveToFile(PathToUtf8(xlsx)), "saved as .xlsx " + book->GetLastError());
    Check(book->SaveToFile(PathToUtf8(ods)), "saved as .ods " + book->GetLastError());

    if (auto loaded = Load(PathToUtf8(xlsx))) CheckWorkbook(*loaded, ".xlsx loaded", true);
    else Check(false, ".xlsx loads");
    if (auto loaded = Load(PathToUtf8(ods))) CheckWorkbook(*loaded, ".ods loaded", true);
    else Check(false, ".ods loads");

    // Saved twice: what was read is written again the same way.
    if (auto loaded = Load(PathToUtf8(xlsx))) {
        const fs::path again = dir / "again.ods";
        Check(loaded->SaveToFile(PathToUtf8(again)), ".xlsx loaded, saved as .ods");
        if (auto twice = Load(PathToUtf8(again))) CheckWorkbook(*twice, ".xlsx -> .ods loaded", true);
        else Check(false, ".xlsx -> .ods loads");
    }
    if (auto loaded = Load(PathToUtf8(ods))) {
        const fs::path again = dir / "again.xlsx";
        Check(loaded->SaveToFile(PathToUtf8(again)), ".ods loaded, saved as .xlsx");
        if (auto twice = Load(PathToUtf8(again))) CheckWorkbook(*twice, ".ods -> .xlsx loaded", true);
        else Check(false, ".ods -> .xlsx loads");
    }

    std::printf("\n=== LibreOffice reads and writes them too ===\n");
    if (!HaveSoffice()) {
        std::printf("  skipped: soffice is not installed\n");
    } else {
        const fs::path profile = dir / "lo-profile";
        const std::string fromXlsx = Convert(xlsx, "ods", dir / "lo-from-xlsx", profile);
        const std::string fromOds = Convert(ods, "xlsx", dir / "lo-from-ods", profile);
        if (fromXlsx.empty()) std::printf("  skipped: soffice did not convert the .xlsx\n");
        else if (auto loaded = Load(fromXlsx)) {
            CheckWorkbook(*loaded, "our .xlsx as LibreOffice's .ods", false, false);
        }
        else Check(false, "LibreOffice's .ods loads");
        if (fromOds.empty()) std::printf("  skipped: soffice did not convert the .ods\n");
        else if (auto loaded = Load(fromOds)) CheckWorkbook(*loaded, "our .ods as LibreOffice's .xlsx", false);
        else Check(false, "LibreOffice's .xlsx loads");
        // .xlsx to .xlsx: LibreOffice read our frozen panes (its .xlsx writer
        // keeps them where its .ods writer cannot, see above).
        const std::string xlsxAgain = Convert(xlsx, "xlsx", dir / "lo-xlsx-again", profile);
        if (xlsxAgain.empty()) std::printf("  skipped: soffice did not convert the .xlsx to .xlsx\n");
        else if (auto loaded = Load(xlsxAgain)) CheckWorkbook(*loaded, "our .xlsx as LibreOffice's .xlsx", false);
        else Check(false, "LibreOffice's .xlsx of our .xlsx loads");
    }

    if (!GetEnvUtf8("UC_KEEP_ROUNDTRIP_FILES").empty()) {
        std::printf("\nfiles kept in %s\n", PathToUtf8(dir).c_str());
    } else {
        fs::remove_all(dir, ec);
    }
    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
