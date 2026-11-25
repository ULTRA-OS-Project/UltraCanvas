// include/UltraCanvasSpreadsheetPrint.h
// A sheet's pages, for PDF and for printers: the print area (or the cells in
// use) cut into pages by the sheet's PrintSettings - paper, orientation,
// margins, scale or fit to pages, manual breaks, gridlines and headings,
// centring, header and footer lines - each page drawn with the grid's own
// cell painter (UltraCanvasSpreadsheet::PaintCell), so paper shows what the
// screen does. Title rows and columns repeat on every page.
//
// It is an IPrintPageSource, like RichDocumentPrintPages for documents: a
// printer that draws gets each page drawn on its own context, one that cannot
// (a Windows printer DC) an image of it. The PDF is the same pages.
//
// doc-check: std::shared_ptr<UltraCanvasSpreadsheet> grid;
// doc-check: example
//     auto pages = CreateSpreadsheetPrintPages(*grid);
//     std::string error;
//     if (pages && pages->ExportPdf("Budget.pdf", error)) { /* written */ }
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "IODeviceManager/UltraCanvasIODevicePrinterPage.h"
#include "UltraCanvasSpreadsheetTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class IRenderContext;
class RasterPageTarget;
class SpreadsheetSheet;
class UltraCanvasSpreadsheet;

class SpreadsheetPrintPages : public IPrintPageSource {
public:
    // The sheet is copied, so editing it while a job spools cannot change
    // what prints.
    explicit SpreadsheetPrintPages(const SpreadsheetSheet& sheet);
    SpreadsheetPrintPages(const SpreadsheetSheet& sheet, const PrintSettings& settings);
    ~SpreadsheetPrintPages() override;

    SpreadsheetPrintPages(const SpreadsheetPrintPages&) = delete;
    SpreadsheetPrintPages& operator=(const SpreadsheetPrintPages&) = delete;

    // Rows (columns) printed at the top (left) of every page; -1: none.
    // Set before the pages are counted.
    void SetTitleRows(int first, int last);
    void SetTitleColumns(int first, int last);

    // The paper PrintSettings names, turned as it says, in points.
    static void PaperSizePoints(const PrintSettings& settings, double& widthPt, double& heightPt);

    // Cuts the sheet into pages; the page count. Prepare and the exports
    // call it.
    int Paginate();
    int GetPageCount() const override { return static_cast<int>(pages_.size()); }

    // The cells page `index` shows (without the title rows and columns), for
    // tests and a preview.
    CellRange GetPageCells(int index) const;
    // How much the sheet is scaled on paper: the PrintSettings scale, or
    // what fits it to the pages asked for.
    double GetScale() const { return scale_; }

    // Page `index` drawn on `ctx` in points, from the paper's top-left.
    void RenderPage(IRenderContext* ctx, int index);

    // The pages as a PDF file (UTF-8 path) or in memory.
    bool ExportPdf(const std::string& utf8Path, std::string& error);
    bool ExportPdf(std::vector<uint8_t>& out, std::string& error);

    // ===== IPrintPageSource =====
    IODeviceResult Prepare(IPrintPageTarget& target) override;
    IODeviceResult DrawPage(int pageIndex, IPrintPageTarget& target) override;

private:
    struct Page {
        int firstRow = 0, lastRow = -1;
        int firstCol = 0, lastCol = -1;
    };

    std::unique_ptr<SpreadsheetSheet> sheet_;
    PrintSettings settings_;
    int titleFirstRow_ = -1, titleLastRow_ = -1;
    int titleFirstCol_ = -1, titleLastCol_ = -1;
    std::vector<Page> pages_;
    double scale_ = 1.0;          // sheet pixels to points, times the print scale
    double paperWidthPt_ = 0.0, paperHeightPt_ = 0.0;

    // Where the pages go on the target Prepare met.
    double placeX_ = 0.0, placeY_ = 0.0, dotsPerPoint_ = 1.0;
    std::unique_ptr<RasterPageTarget> offscreen_;
    double offscreenScale_ = 1.0;

    CellRange PrintedRange() const;
    std::string FieldText(const std::string& field, int pageIndex) const;
};

// The pages of the sheet `grid` shows, with that sheet's print settings, or
// null when it shows none.
std::shared_ptr<SpreadsheetPrintPages> CreateSpreadsheetPrintPages(const UltraCanvasSpreadsheet& grid);

} // namespace UltraCanvas
