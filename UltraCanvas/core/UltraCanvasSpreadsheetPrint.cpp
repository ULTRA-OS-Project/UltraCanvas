// core/UltraCanvasSpreadsheetPrint.cpp
// A sheet's pages for PDF and printers (include/UltraCanvasSpreadsheetPrint.h).
//
// Units: the sheet measures rows and columns in screen pixels (96 to the
// inch); paper is in points. A page is drawn in sheet pixels under a scale of
// 0.75 (points per pixel) times the print scale, so a printed sheet at 100 %
// is the size it is on screen at 100 %.
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasSpreadsheetPrint.h"

#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"
#include "UltraCanvasPdfSurface.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetSheet.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>

namespace UltraCanvas {

namespace {

constexpr double kPointsPerPixel = 72.0 / 96.0;
constexpr double kPointsPerInch = 72.0;
constexpr double kMaxOffscreenDpi = 300.0;
constexpr double kFieldFontSize = 9.0;   // header and footer lines, in points
const Color kGridlineColor(200, 200, 200);
const Color kHeadingFill(240, 240, 240);
const Color kHeadingLine(160, 160, 160);
const Color kHeadingText(60, 60, 60);

bool HasPrintArea(const PrintSettings& settings) {
    // No print area is the default range, A1:A1.
    const CellRange& area = settings.printArea;
    return area.IsValid() && !(area.start.row == 0 && area.start.col == 0 && area.end.row == 0 && area.end.col == 0);
}

std::string Number(int value) {
    char text[16];
    const auto result = std::to_chars(text, text + sizeof text, value);
    return std::string(text, result.ptr);
}

std::string TwoDigits(unsigned value) {
    return value < 10 ? "0" + Number(static_cast<int>(value)) : Number(static_cast<int>(value));
}

} // namespace

SpreadsheetPrintPages::SpreadsheetPrintPages(const SpreadsheetSheet& sheet)
    : SpreadsheetPrintPages(sheet, sheet.GetPrintSettings()) {}

SpreadsheetPrintPages::SpreadsheetPrintPages(const SpreadsheetSheet& sheet, const PrintSettings& settings)
    : sheet_(sheet.CloneKeepingId()), settings_(settings) {
    PaperSizePoints(settings_, paperWidthPt_, paperHeightPt_);
}

SpreadsheetPrintPages::~SpreadsheetPrintPages() = default;

void SpreadsheetPrintPages::SetTitleRows(int first, int last) {
    titleFirstRow_ = first < 0 || last < first ? -1 : first;
    titleLastRow_ = titleFirstRow_ < 0 ? -1 : last;
}

void SpreadsheetPrintPages::SetTitleColumns(int first, int last) {
    titleFirstCol_ = first < 0 || last < first ? -1 : first;
    titleLastCol_ = titleFirstCol_ < 0 ? -1 : last;
}

void SpreadsheetPrintPages::PaperSizePoints(const PrintSettings& settings, double& widthPt, double& heightPt) {
    switch (settings.paperSize) {
        case PaperSize::Legal: widthPt = 612.0;  heightPt = 1008.0; break;
        case PaperSize::A3:    widthPt = 841.89; heightPt = 1190.55; break;
        case PaperSize::A4:    widthPt = 595.28; heightPt = 841.89; break;
        case PaperSize::A5:    widthPt = 419.53; heightPt = 595.28; break;
        case PaperSize::B4:    widthPt = 708.66; heightPt = 1000.63; break;
        case PaperSize::B5:    widthPt = 498.90; heightPt = 708.66; break;
        default:               widthPt = 612.0;  heightPt = 792.0; break;   // Letter, and Custom until it has a size
    }
    if (settings.orientation == PageOrientation::Landscape) std::swap(widthPt, heightPt);
}

CellRange SpreadsheetPrintPages::PrintedRange() const {
    if (HasPrintArea(settings_)) return settings_.printArea;
    return sheet_->GetUsedRange();
}

int SpreadsheetPrintPages::Paginate() {
    pages_.clear();
    PaperSizePoints(settings_, paperWidthPt_, paperHeightPt_);
    const CellRange range = PrintedRange();
    const SpreadsheetSheet& sheet = *sheet_;
    const bool headings = settings_.printRowColHeaders;

    auto rowsHeight = [&](int first, int last) {
        double height = 0;
        for (int r = first; r >= 0 && r <= last; ++r) if (!sheet.IsRowHidden(r)) height += sheet.GetRowHeight(r);
        return height;
    };
    auto colsWidth = [&](int first, int last) {
        double width = 0;
        for (int c = first; c >= 0 && c <= last; ++c) if (!sheet.IsColumnHidden(c)) width += sheet.GetColumnWidth(c);
        return width;
    };

    // The room on paper for cells, in points.
    const double roomWidthPt = std::max(36.0, paperWidthPt_ - (settings_.marginLeft + settings_.marginRight) * kPointsPerInch);
    const double roomHeightPt = std::max(36.0, paperHeightPt_ - (settings_.marginTop + settings_.marginBottom) * kPointsPerInch);
    const double headingWidth = headings ? SpreadsheetLimits::RowHeaderWidth : 0.0;
    const double headingHeight = headings ? SpreadsheetLimits::HeaderRowHeight : 0.0;

    // Scale: as set, or what fits the sheet on the pages asked for (never
    // larger than 100 %).
    double printScale = std::clamp(settings_.scale, 10, 400) / 100.0;
    if (settings_.fitToWidth > 0 || settings_.fitToHeight > 0) {
        printScale = 1.0;
        const double totalWidthPt = (colsWidth(range.start.col, range.end.col) + headingWidth) * kPointsPerPixel;
        const double totalHeightPt = (rowsHeight(range.start.row, range.end.row) + headingHeight) * kPointsPerPixel;
        if (settings_.fitToWidth > 0 && totalWidthPt > 0)
            printScale = std::min(printScale, settings_.fitToWidth * roomWidthPt / totalWidthPt);
        if (settings_.fitToHeight > 0 && totalHeightPt > 0)
            printScale = std::min(printScale, settings_.fitToHeight * roomHeightPt / totalHeightPt);
        printScale = std::max(printScale, 0.1);
    }
    scale_ = kPointsPerPixel * printScale;
    const double roomWidth = roomWidthPt / scale_ - headingWidth;     // sheet pixels
    const double roomHeight = roomHeightPt / scale_ - headingHeight;

    const double titleHeight = titleFirstRow_ >= 0 ? rowsHeight(titleFirstRow_, titleLastRow_) : 0.0;
    const double titleWidth = titleFirstCol_ >= 0 ? colsWidth(titleFirstCol_, titleLastCol_) : 0.0;

    // Cuts [first, last] into runs that fit `room`, starting a run at each
    // manual break; a run after the title lines has their size less room.
    auto cut = [](int first, int last, double room, double titleSize, int titleLast,
                  const std::vector<int>& breaks, auto&& sizeOf, auto&& hidden) {
        std::vector<std::pair<int, int>> runs;
        int start = first;
        while (start <= last) {
            const double space = std::max(1.0, room - (start > titleLast && titleLast >= 0 ? titleSize : 0.0));
            double used = 0;
            int end = start;
            for (int i = start; i <= last; ++i) {
                if (i > start && std::find(breaks.begin(), breaks.end(), i) != breaks.end()) break;
                const double size = hidden(i) ? 0.0 : sizeOf(i);
                if (i > start && used + size > space + 0.5) break;   // half a pixel: a sheet that just fits, fits
                used += size;
                end = i;
            }
            runs.emplace_back(start, end);
            start = end + 1;
        }
        return runs;
    };
    const auto rowRuns = cut(range.start.row, range.end.row, roomHeight, titleHeight, titleLastRow_, settings_.rowBreaks,
                             [&](int r) { return static_cast<double>(sheet.GetRowHeight(r)); },
                             [&](int r) { return sheet.IsRowHidden(r); });
    const auto colRuns = cut(range.start.col, range.end.col, roomWidth, titleWidth, titleLastCol_, settings_.colBreaks,
                             [&](int c) { return static_cast<double>(sheet.GetColumnWidth(c)); },
                             [&](int c) { return sheet.IsColumnHidden(c); });
    // Down, then across, as spreadsheets print.
    for (const auto& cols : colRuns) {
        for (const auto& rows : rowRuns) {
            Page page;
            page.firstRow = rows.first;
            page.lastRow = rows.second;
            page.firstCol = cols.first;
            page.lastCol = cols.second;
            pages_.push_back(page);
        }
    }
    return GetPageCount();
}

CellRange SpreadsheetPrintPages::GetPageCells(int index) const {
    if (index < 0 || index >= GetPageCount()) return CellRange();
    const Page& page = pages_[static_cast<size_t>(index)];
    return CellRange(page.firstRow, page.firstCol, page.lastRow, page.lastCol);
}

std::string SpreadsheetPrintPages::FieldText(const std::string& field, int pageIndex) const {
    // The spreadsheet formats' codes: &P the page, &N the pages, &A the
    // sheet, &D the date, && an ampersand; other codes (fonts, sections)
    // are left out.
    std::string out;
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '&' || i + 1 >= field.size()) { out += field[i]; continue; }
        const char code = field[++i];
        switch (code) {
            case 'P': case 'p': out += Number(pageIndex + 1); break;
            case 'N': case 'n': out += Number(GetPageCount()); break;
            case 'A': case 'a': out += sheet_->GetName(); break;
            case 'D': case 'd': {
                const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
                const std::chrono::year_month_day date(today);
                out += Number(static_cast<int>(date.year())) + "-" + TwoDigits(static_cast<unsigned>(date.month())) + "-" +
                       TwoDigits(static_cast<unsigned>(date.day()));
                break;
            }
            case '&': out += '&'; break;
            case '"': {   // &"Font,Style"
                const size_t close = field.find('"', i + 1);
                i = close == std::string::npos ? field.size() : close;
                break;
            }
            default:
                while (i + 1 < field.size() && std::isdigit(static_cast<unsigned char>(field[i + 1]))) ++i;   // &12 (a size)
                break;
        }
    }
    return out;
}

void SpreadsheetPrintPages::RenderPage(IRenderContext* ctx, int index) {
    if (!ctx || index < 0 || index >= GetPageCount()) return;
    const SpreadsheetSheet& sheet = *sheet_;
    const Page& page = pages_[static_cast<size_t>(index)];
    const bool headings = settings_.printRowColHeaders;

    // The lines and columns this page shows: the titles first where the page
    // is past them, then its own.
    std::vector<int> rows, cols;
    if (titleFirstRow_ >= 0 && page.firstRow > titleLastRow_)
        for (int r = titleFirstRow_; r <= titleLastRow_; ++r) if (!sheet.IsRowHidden(r)) rows.push_back(r);
    for (int r = page.firstRow; r <= page.lastRow; ++r) if (!sheet.IsRowHidden(r)) rows.push_back(r);
    if (titleFirstCol_ >= 0 && page.firstCol > titleLastCol_)
        for (int c = titleFirstCol_; c <= titleLastCol_; ++c) if (!sheet.IsColumnHidden(c)) cols.push_back(c);
    for (int c = page.firstCol; c <= page.lastCol; ++c) if (!sheet.IsColumnHidden(c)) cols.push_back(c);

    const double headingWidth = headings ? SpreadsheetLimits::RowHeaderWidth : 0.0;
    const double headingHeight = headings ? SpreadsheetLimits::HeaderRowHeight : 0.0;
    std::vector<int> rowTop(rows.size()), colLeft(cols.size());
    int height = static_cast<int>(headingHeight), width = static_cast<int>(headingWidth);
    for (size_t i = 0; i < rows.size(); ++i) { rowTop[i] = height; height += sheet.GetRowHeight(rows[i]); }
    for (size_t i = 0; i < cols.size(); ++i) { colLeft[i] = width; width += sheet.GetColumnWidth(cols[i]); }

    // The cells' block on paper, centred where the settings say.
    const double roomLeft = settings_.marginLeft * kPointsPerInch;
    const double roomTop = settings_.marginTop * kPointsPerInch;
    const double roomWidth = paperWidthPt_ - (settings_.marginLeft + settings_.marginRight) * kPointsPerInch;
    const double roomHeight = paperHeightPt_ - (settings_.marginTop + settings_.marginBottom) * kPointsPerInch;
    double left = roomLeft, top = roomTop;
    if (settings_.centerHorizontally) left += std::max(0.0, (roomWidth - width * scale_) / 2.0);
    if (settings_.centerVertically) top += std::max(0.0, (roomHeight - height * scale_) / 2.0);

    ctx->PushState();
    ctx->ClipRect(Rect2Dd(roomLeft, roomTop, roomWidth, roomHeight));
    ctx->Translate(left, top);
    ctx->Scale(scale_, scale_);

    auto indexOf = [](const std::vector<int>& list, int value) {
        const auto it = std::find(list.begin(), list.end(), value);
        return it == list.end() ? -1 : static_cast<int>(it - list.begin());
    };
    for (size_t ri = 0; ri < rows.size(); ++ri) {
        for (size_t ci = 0; ci < cols.size(); ++ci) {
            const int row = rows[ri], col = cols[ci];
            Rect2Di bounds(colLeft[ci], rowTop[ri], sheet.GetColumnWidth(col), sheet.GetRowHeight(row));
            if (const MergedCell* merge = sheet.IsCellMerged(row, col) ? sheet.GetMergedCell(row, col) : nullptr) {
                // A merged block is drawn once, from its first cell on this
                // page, over the part of it the page shows.
                int firstRi = -1, firstCi = -1, lastRi = -1, lastCi = -1;
                for (size_t k = 0; k < rows.size(); ++k)
                    if (merge->range.Contains(rows[k], col)) { if (firstRi < 0) firstRi = static_cast<int>(k); lastRi = static_cast<int>(k); }
                for (size_t k = 0; k < cols.size(); ++k)
                    if (merge->range.Contains(row, cols[k])) { if (firstCi < 0) firstCi = static_cast<int>(k); lastCi = static_cast<int>(k); }
                if (firstRi != static_cast<int>(ri) || firstCi != static_cast<int>(ci)) continue;
                const int right = colLeft[static_cast<size_t>(lastCi)] + sheet.GetColumnWidth(cols[static_cast<size_t>(lastCi)]);
                const int bottom = rowTop[static_cast<size_t>(lastRi)] + sheet.GetRowHeight(rows[static_cast<size_t>(lastRi)]);
                bounds = Rect2Di(colLeft[ci], rowTop[ri], right - colLeft[ci], bottom - rowTop[ri]);
                // The value sits in the block's top-left cell.
                const int anchorRow = merge->range.start.row, anchorCol = merge->range.start.col;
                if (indexOf(rows, anchorRow) >= 0 && indexOf(cols, anchorCol) >= 0) {
                    UltraCanvasSpreadsheet::PaintCell(ctx, sheet, anchorRow, anchorCol, bounds);
                }
            } else {
                UltraCanvasSpreadsheet::PaintCell(ctx, sheet, row, col, bounds);
            }
            if (settings_.printGridlines) {
                ctx->SetStrokePaint(kGridlineColor);
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawRectangle(Rect2Dd(bounds.x, bounds.y, bounds.width, bounds.height));
            }
        }
    }

    if (headings) {
        FontStyle font;
        font.fontFamily = "Arial";
        font.fontSize = 10;
        ctx->SetFontStyle(font);
        ctx->SetStrokeWidth(1.0f);
        auto heading = [&](const Rect2Dd& box, const std::string& label) {
            ctx->SetFillPaint(kHeadingFill);
            ctx->FillRectangle(box);
            ctx->SetStrokePaint(kHeadingLine);
            ctx->DrawRectangle(box);
            ctx->SetTextPaint(kHeadingText);
            const int textWidth = ctx->GetTextLineWidth(label);
            const int textTop = ctx->TextTopCentredOnCaps(box, font);
            ctx->DrawText(label, Point2Df(static_cast<float>(box.x + (box.width - textWidth) / 2.0), static_cast<float>(textTop)));
        };
        heading(Rect2Dd(0, 0, headingWidth, headingHeight), "");
        for (size_t ci = 0; ci < cols.size(); ++ci)
            heading(Rect2Dd(colLeft[ci], 0, sheet.GetColumnWidth(cols[ci]), headingHeight), CellAddress::ColumnToLetter(cols[ci]));
        for (size_t ri = 0; ri < rows.size(); ++ri)
            heading(Rect2Dd(0, rowTop[ri], headingWidth, sheet.GetRowHeight(rows[ri])), Number(rows[ri] + 1));
    }
    ctx->PopState();

    // Header and footer lines, in the margins, at their own size.
    FontStyle fieldFont;
    fieldFont.fontFamily = "Arial";
    fieldFont.fontSize = kFieldFontSize;
    ctx->SetFontStyle(fieldFont);
    ctx->SetTextPaint(Colors::Black);
    auto line = [&](const std::string& leftText, const std::string& centreText, const std::string& rightText, double baselineTop) {
        const std::string l = FieldText(leftText, index), c = FieldText(centreText, index), r = FieldText(rightText, index);
        if (!l.empty()) ctx->DrawText(l, Point2Df(static_cast<float>(roomLeft), static_cast<float>(baselineTop)));
        if (!c.empty())
            ctx->DrawText(c, Point2Df(static_cast<float>((paperWidthPt_ - ctx->GetTextLineWidth(c)) / 2.0), static_cast<float>(baselineTop)));
        if (!r.empty())
            ctx->DrawText(r, Point2Df(static_cast<float>(paperWidthPt_ - settings_.marginRight * kPointsPerInch - ctx->GetTextLineWidth(r)),
                                      static_cast<float>(baselineTop)));
    };
    line(settings_.headerLeft, settings_.headerCenter, settings_.headerRight, settings_.headerMargin * kPointsPerInch);
    line(settings_.footerLeft, settings_.footerCenter, settings_.footerRight,
         paperHeightPt_ - settings_.footerMargin * kPointsPerInch - kFieldFontSize * 1.3);
}

bool SpreadsheetPrintPages::ExportPdf(const std::string& utf8Path, std::string& error) {
    if (Paginate() <= 0) { error = "The sheet has nothing to print."; return false; }
    auto pdf = UltraCanvasPdfSurface::CreateForFile(utf8Path, paperWidthPt_, paperHeightPt_, error);
    if (!pdf) return false;
    pdf->SetMetadata(sheet_->GetName(), "");
    for (int i = 0; i < GetPageCount(); ++i) {
        if (i > 0) pdf->NextPage();
        RenderPage(pdf->GetContext(), i);
    }
    return pdf->Finish(error);
}

bool SpreadsheetPrintPages::ExportPdf(std::vector<uint8_t>& out, std::string& error) {
    if (Paginate() <= 0) { error = "The sheet has nothing to print."; return false; }
    auto pdf = UltraCanvasPdfSurface::CreateInMemory(paperWidthPt_, paperHeightPt_, error);
    if (!pdf) return false;
    pdf->SetMetadata(sheet_->GetName(), "");
    for (int i = 0; i < GetPageCount(); ++i) {
        if (i > 0) pdf->NextPage();
        RenderPage(pdf->GetContext(), i);
    }
    if (!pdf->Finish(error)) return false;
    out = pdf->GetBytes();
    return true;
}

// ===== PRINTERS =====

IODeviceResult SpreadsheetPrintPages::Prepare(IPrintPageTarget& target) {
    offscreen_.reset();
    const IOPrintPageMetrics metrics = target.GetMetrics();
    if (!metrics.IsValid()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument, "The printer reported no usable page size");
    }
    if (Paginate() <= 0) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument, "The sheet has nothing to print");
    }
    // The page lined up with the sheet of paper's edges (the printable area
    // starts the unprintable margin in), scaled down if the paper is smaller.
    const double sheetWidth = metrics.widthDots + 2.0 * metrics.offsetXDots;
    const double sheetHeight = metrics.heightDots + 2.0 * metrics.offsetYDots;
    const double dotsX = metrics.dpiX / kPointsPerInch, dotsY = metrics.dpiY / kPointsPerInch;
    dotsPerPoint_ = std::min({dotsX, dotsY, sheetWidth / paperWidthPt_, sheetHeight / paperHeightPt_});
    placeX_ = -metrics.offsetXDots + std::max(0.0, (sheetWidth - paperWidthPt_ * dotsPerPoint_) / 2.0);
    placeY_ = -metrics.offsetYDots + std::max(0.0, (sheetHeight - paperHeightPt_ * dotsPerPoint_) / 2.0);
    if (!target.GetRenderContext()) {
        // A printer the rendering stack cannot draw on gets each page as an image.
        const double dpi = std::min(std::min(metrics.dpiX, metrics.dpiY) * 1.0, kMaxOffscreenDpi);
        offscreenScale_ = dpi / kPointsPerInch;
        const int width = std::max(1, static_cast<int>(std::ceil(paperWidthPt_ * offscreenScale_)));
        const int height = std::max(1, static_cast<int>(std::ceil(paperHeightPt_ * offscreenScale_)));
        offscreen_ = std::make_unique<RasterPageTarget>(width, height, static_cast<int>(dpi), static_cast<int>(dpi));
        if (!offscreen_->IsValid()) {
            offscreen_.reset();
            return IODeviceResult::Error(IODeviceResultCode::BackendError, "Could not create a page to draw the sheet on");
        }
    }
    return IODeviceResult::Ok();
}

IODeviceResult SpreadsheetPrintPages::DrawPage(int pageIndex, IPrintPageTarget& target) {
    if (pageIndex < 0 || pageIndex >= GetPageCount()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "There is no page " + Number(pageIndex + 1) + " in this sheet");
    }
    if (offscreen_) {
        offscreen_->BeginPage();
        IRenderContext* ctx = offscreen_->GetRenderContext();
        ctx->PushState();
        ctx->Scale(offscreenScale_, offscreenScale_);
        RenderPage(ctx, pageIndex);
        ctx->PopState();
        const IOPrintPageMetrics drawn = offscreen_->GetMetrics();
        std::vector<uint8_t> pixels;
        pixels.reserve(static_cast<size_t>(drawn.widthDots) * static_cast<size_t>(drawn.heightDots) * 4u);
        offscreen_->ForEachRow([&pixels](const uint8_t* rgba, int width) {
            pixels.insert(pixels.end(), rgba, rgba + static_cast<size_t>(width) * 4u);
            return true;
        });
        IOPrintRect dest;
        dest.x = static_cast<int>(std::lround(placeX_));
        dest.y = static_cast<int>(std::lround(placeY_));
        dest.width = static_cast<int>(std::lround(paperWidthPt_ * dotsPerPoint_));
        dest.height = static_cast<int>(std::lround(paperHeightPt_ * dotsPerPoint_));
        if (!target.DrawImage(pixels.data(), drawn.widthDots, drawn.heightDots, dest)) {
            return IODeviceResult::Error(IODeviceResultCode::IOError, "The printer did not take page " + Number(pageIndex + 1));
        }
        return IODeviceResult::Ok();
    }
    IRenderContext* ctx = target.GetRenderContext();
    if (!ctx) return IODeviceResult::Error(IODeviceResultCode::InvalidArgument, "The page was prepared for another target");
    ctx->PushState();
    ctx->Translate(placeX_, placeY_);
    ctx->Scale(dotsPerPoint_, dotsPerPoint_);
    RenderPage(ctx, pageIndex);
    ctx->PopState();
    return IODeviceResult::Ok();
}

std::shared_ptr<SpreadsheetPrintPages> CreateSpreadsheetPrintPages(const UltraCanvasSpreadsheet& grid) {
    const SpreadsheetSheet* sheet = grid.GetActiveSheet();
    if (!sheet) return nullptr;
    return std::make_shared<SpreadsheetPrintPages>(*sheet);
}

} // namespace UltraCanvas
