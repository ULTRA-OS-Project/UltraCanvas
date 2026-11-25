// Tests/SpreadsheetPrintTest.cpp
// A sheet cut into pages for print and PDF (UltraCanvasSpreadsheetPrint.h):
// how many pages, which cells each shows, for the default paper, landscape,
// a print area, a manual break, fit to one page wide and title rows; and that
// the PDF holds those pages (pdfinfo counts them in a file the test writes).
//
// A 100 x 15 sheet of default cells (64 x 20 px) on Letter with the default
// margins has 511.2 x 684 pt of room: 10 columns and 45 rows at 100 %.
//
// Usage: SpreadsheetPrintTest [pdf-output-path]
// Exit code is the number of failed checks.

#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetPrint.h"
#include "UltraCanvasSpreadsheetSheet.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

std::string Pages(SpreadsheetPrintPages& pages) {
    std::string out;
    for (int i = 0; i < pages.GetPageCount(); ++i) out += (i ? " " : "") + pages.GetPageCells(i).ToString();
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("=== Spreadsheet print and PDF pages ===\n");
    auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
    SpreadsheetSheet& sheet = *grid->GetActiveSheet();
    for (int r = 0; r < 100; ++r)
        for (int c = 0; c < 15; ++c) sheet.SetCellValue(r, c, static_cast<double>(r * 100 + c));
    sheet.GetCell(0, 0)->SetBold(true);

    {
        SpreadsheetPrintPages pages(sheet);
        Check(pages.Paginate() == 6, "Letter, portrait: 3 pages down, 2 across [" + Pages(pages) + "]");
        Check(pages.GetPageCells(0).ToString() == "A1:J45" && pages.GetPageCells(1).ToString() == "A46:J90"
                  && pages.GetPageCells(3).ToString() == "K1:O45",
              "down, then across");
        std::vector<uint8_t> pdf;
        std::string error;
        Check(pages.ExportPdf(pdf, error) && pdf.size() > 4 && std::string(pdf.begin(), pdf.begin() + 4) == "%PDF",
              "a PDF [" + error + "]");
        PrintSettings oneSettings = sheet.GetPrintSettings();
        oneSettings.printArea = CellRange(0, 0, 9, 4);
        SpreadsheetPrintPages one(sheet, oneSettings);
        std::vector<uint8_t> onePdf;
        Check(one.ExportPdf(onePdf, error) && pdf.size() > onePdf.size(),
              "six full pages are a larger file than one small page [" + std::to_string(pdf.size()) + " / " +
                  std::to_string(onePdf.size()) + " bytes]");
    }
    {
        PrintSettings settings = sheet.GetPrintSettings();
        settings.orientation = PageOrientation::Landscape;
        SpreadsheetPrintPages pages(sheet, settings);
        Check(pages.Paginate() == 8, "landscape: 14 columns and 33 rows a page [" + Pages(pages) + "]");
    }
    {
        PrintSettings settings = sheet.GetPrintSettings();
        settings.printArea = CellRange(0, 0, 19, 4);
        SpreadsheetPrintPages pages(sheet, settings);
        Check(pages.Paginate() == 1 && pages.GetPageCells(0).ToString() == "A1:E20", "a print area A1:E20: one page");
    }
    {
        PrintSettings settings = sheet.GetPrintSettings();
        settings.rowBreaks = {10};
        SpreadsheetPrintPages pages(sheet, settings);
        Check(pages.Paginate() == 6 && pages.GetPageCells(0).ToString() == "A1:J10" && pages.GetPageCells(1).ToString() == "A11:J55",
              "a break before row 11 [" + Pages(pages) + "]");
    }
    {
        PrintSettings settings = sheet.GetPrintSettings();
        settings.fitToWidth = 1;
        SpreadsheetPrintPages pages(sheet, settings);
        Check(pages.Paginate() == 2 && pages.GetPageCells(0).end.col == 14,
              "fit to one page wide: every column on each page [" + Pages(pages) + "]");
        Check(std::fabs(pages.GetScale() - 0.75 * 511.2 / 720.0) < 1e-6, "at 71 % [" + std::to_string(pages.GetScale()) + "]");
    }
    {
        SpreadsheetPrintPages pages(sheet);
        pages.SetTitleRows(0, 0);
        Check(pages.Paginate() == 6 && pages.GetPageCells(1).ToString() == "A46:J89" && pages.GetPageCells(2).ToString() == "A90:J100",
              "a title row takes its height from the pages after the first [" + Pages(pages) + "]");
    }
    {
        PrintSettings settings = sheet.GetPrintSettings();
        settings.repeatRowsFirst = 0;
        settings.repeatRowsLast = 0;
        SpreadsheetPrintPages pages(sheet, settings);
        Check(pages.Paginate() == 6 && pages.GetPageCells(1).ToString() == "A46:J89",
              "the sheet's own print titles repeat too [" + Pages(pages) + "]");
    }
    {
        PrintSettings settings = sheet.GetPrintSettings();
        settings.printGridlines = true;
        settings.printRowColHeaders = true;
        settings.headerCenter = "&A";
        settings.footerRight = "Page &P of &N";
        settings.paperSize = PaperSize::A4;
        SpreadsheetPrintPages pages(sheet, settings);
        pages.SetTitleRows(0, 0);
        std::string error;
        const std::string path = argc > 1 ? argv[1] : "spreadsheet-print-test.pdf";
        Check(pages.ExportPdf(path, error), "A4 with gridlines, headings, header and footer to " + path + " [" + error + "]");
    }
    {
        auto empty = std::make_shared<UltraCanvasSpreadsheet>("empty", 0, 0, 900, 600);
        auto pages = CreateSpreadsheetPrintPages(*empty);
        Check(pages && pages->Paginate() == 1, "an empty sheet is one empty page");
    }

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
