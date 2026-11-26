// Tests/SpreadsheetChartDrawTest.cpp
// A chart on the grid (UltraCanvasSpreadsheetChart.h), drawn and handled:
//  - the grid draws it at its anchor, off screen, in the series' colour, and
//    nowhere else; scrolled, it moves with the cells;
//  - a click selects it, a drag moves it, the corner handle resizes it, Delete
//    removes it, each an undo step that Undo and Redo take back and forth;
//  - inserting rows above it moves it down with its cells;
//  - printed, it is on the page (and the pages reach the cells under it);
//  - an untitled chart's top value label stays inside its rectangle;
//  - every chart type (SpreadsheetChartTypes) made from its sample cells
//    (Tests/SpreadsheetChartSamples.h) fits its type and draws something in
//    its box, through DrawSpreadsheetChart; a selection that does not fit a
//    type says why. With ULTRACANVAS_SCREENSHOT_DIR set, each type is
//    written there as a PNG.
// The grid is drawn on an off-screen raster (RasterPageTarget): no window,
// no display.
//
// Usage: SpreadsheetChartDrawTest
// Exit code is the number of failed checks.
// Version: 1.2.0 - every chart type drawn from its sample cells
// Version: 1.1.0 - an untitled chart's top value label is not cut off
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasRichDocumentChart.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetChart.h"
#include "UltraCanvasSpreadsheetPrint.h"
#include "SpreadsheetChartSamples.h"

#include <cairo/cairo.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

std::string Box(const Rect2Di& r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.width) + "x" + std::to_string(r.height);
}

// The office palette's first colour (#4472C4), which the first series takes.
bool IsSeriesBlue(const uint8_t* p) {
    return std::abs(p[0] - 0x44) < 24 && std::abs(p[1] - 0x72) < 24 && std::abs(p[2] - 0xC4) < 24;
}

// How many series-blue pixels `target` holds inside `area`, and outside it.
void CountBlue(const RasterPageTarget& target, const Rect2Di& area, int& inside, int& outside) {
    inside = outside = 0;
    int y = 0;
    target.ForEachRow([&](const uint8_t* rgba, int width) {
        for (int x = 0; x < width; ++x) {
            if (!IsSeriesBlue(rgba + 4 * x)) continue;
            const bool in = x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height;
            (in ? inside : outside)++;
        }
        ++y;
        return true;
    });
}

void Draw(UltraCanvasSpreadsheet& grid, RasterPageTarget& target) {
    target.BeginPage();
    grid.Render(target.GetRenderContext(), Rect2Df(0, 0, grid.GetWidth(), grid.GetHeight()));
}

UCEvent Mouse(UCEventType type, int x, int y) {
    UCEvent e;
    e.type = type;
    e.button = UCMouseButton::Left;
    e.pointer = Point2Di(x, y);
    e.pointerWindow = e.pointer;
    return e;
}

void DragFromTo(UltraCanvasSpreadsheet& grid, int x, int y, int toX, int toY) {
    grid.OnEvent(Mouse(UCEventType::MouseDown, x, y));
    grid.OnEvent(Mouse(UCEventType::MouseMove, (x + toX) / 2, (y + toY) / 2));
    grid.OnEvent(Mouse(UCEventType::MouseMove, toX, toY));
    grid.OnEvent(Mouse(UCEventType::MouseUp, toX, toY));
}

void Key(UltraCanvasSpreadsheet& grid, UCKeys key) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    grid.OnEvent(e);
}

} // namespace

int main() {
    std::printf("=== Spreadsheet charts drawn, moved, resized and printed ===\n");
    auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
    SpreadsheetSheet& sheet = *grid->GetActiveSheet();
    sheet.SetCellValue(0, 0, std::string("Month"));
    sheet.SetCellValue(0, 1, std::string("Sales"));
    for (int i = 0; i < 6; ++i) {
        sheet.SetCellValue(i + 1, 0, std::string("M") + std::to_string(i + 1));
        sheet.SetCellValue(i + 1, 1, 10.0 + 5.0 * i);
    }
    grid->Select(CellRange(0, 0, 6, 1));
    const int index = grid->InsertChartFromSelection(RichChart::Kind::Column);
    Check(index == 0 && grid->GetChartCount() == 1, "a chart inserted from A1:B7");
    Check(grid->GetSelectedChart() == 0, "the new chart is selected");
    Check(grid->CanUndo() && grid->GetUndoDescription() == "Insert chart", "inserting it is an undo step");

    RasterPageTarget target(900, 600, 96, 96);
    if (!target.IsValid()) {
        std::printf("  skipped: no off-screen surface in this build\n");
        return failures;
    }

    // 1. Drawn where it stands, and nowhere else.
    Rect2Di box = grid->GetChartBounds(0);
    Draw(*grid, target);
    int inside = 0, outside = 0;
    CountBlue(target, box, inside, outside);
    Check(box.width > 300 && box.height > 200, "the chart's box on screen " + Box(box));
    Check(inside > 500, "the columns are drawn in it (" + std::to_string(inside) + " series pixels)");
    Check(outside < 50, "and not outside it (" + std::to_string(outside) + " pixels; the selection's own blue aside)");

    // 2. Scrolled down five rows: it moves up with the cells.
    sheet.SetScrollPosition(5, 0);
    const Rect2Di scrolled = grid->GetChartBounds(0);
    Check(scrolled.y == box.y - 5 * sheet.GetRowHeight(0) && scrolled.x == box.x,
          "scrolled five rows, it is five rows higher " + Box(scrolled));
    Draw(*grid, target);
    CountBlue(target, scrolled, inside, outside);
    Check(inside > 300 && outside < 50, "and drawn there (" + std::to_string(inside) + " in, " + std::to_string(outside) + " out)");
    sheet.SetScrollPosition(0, 0);

    // 3. A click on a cell lets go of it; a click on it selects it.
    const Rect2Di cell = grid->GetCellBounds(12, 0);
    grid->OnEvent(Mouse(UCEventType::MouseDown, cell.x + 5, cell.y + 5));
    grid->OnEvent(Mouse(UCEventType::MouseUp, cell.x + 5, cell.y + 5));
    Check(grid->GetSelectedChart() == -1, "a click on a cell deselects the chart");
    grid->OnEvent(Mouse(UCEventType::MouseDown, box.x + box.width / 2, box.y + box.height / 2));
    grid->OnEvent(Mouse(UCEventType::MouseUp, box.x + box.width / 2, box.y + box.height / 2));
    Check(grid->GetSelectedChart() == 0, "a click on the chart selects it");
    Check(grid->GetSelection().start.row == 12, "...and leaves the cells' selection as it was");

    // 4. Dragged 64 pixels right and 40 down: a column and two rows on.
    const SpreadsheetChartAnchor before = grid->GetChart(0)->anchor;
    DragFromTo(*grid, box.x + 50, box.y + 50, box.x + 50 + 64, box.y + 50 + 40);
    SpreadsheetChartAnchor moved = grid->GetChart(0)->anchor;
    Check(moved.from.col == before.from.col + 1 && moved.from.row == before.from.row + 2 &&
              moved.to.col == before.to.col + 1 && moved.to.row == before.to.row + 2,
          "dragged, it stands a column right and two rows down");
    Check(grid->GetUndoDescription() == "Move chart", "moving it is an undo step");
    grid->Undo();
    Check(grid->GetChart(0)->anchor.from == before.from, "Undo puts it back");
    grid->Redo();
    Check(grid->GetChart(0)->anchor.from == moved.from, "Redo moves it again");

    // 5. The bottom-right handle makes it bigger.
    const Rect2Di beforeResize = grid->GetChartBounds(0);
    grid->SelectChart(0);
    DragFromTo(*grid, beforeResize.x + beforeResize.width, beforeResize.y + beforeResize.height,
               beforeResize.x + beforeResize.width + 30, beforeResize.y + beforeResize.height + 20);
    const Rect2Di resized = grid->GetChartBounds(0);
    Check(resized.x == beforeResize.x && resized.y == beforeResize.y && resized.width == beforeResize.width + 30 &&
              resized.height == beforeResize.height + 20,
          "the corner handle resizes it " + Box(beforeResize) + " -> " + Box(resized));
    Check(grid->GetUndoDescription() == "Resize chart", "resizing it is an undo step");

    // 6. Rows inserted above it move it down with its cells.
    const int fromRow = grid->GetChart(0)->anchor.from.row;
    grid->InsertRowsAt(0, 2);
    Check(grid->GetChart(0) && grid->GetChart(0)->anchor.from.row == fromRow + 2, "two rows inserted above: two rows down");
    Check(grid->GetChart(0) && grid->GetChart(0)->sources.size() == 1 &&
              grid->GetChart(0)->sources[0].values.range.start.row == 3,
          "...and its data's range moved with the cells (B4:B9)");
    grid->Undo();
    Check(grid->GetChart(0) && grid->GetChart(0)->anchor.from.row == fromRow, "Undo takes the rows and the move back");

    // 7. Delete removes it; Undo brings it back.
    grid->SelectChart(0);
    Key(*grid, UCKeys::Delete);
    Check(grid->GetChartCount() == 0, "Delete removes the selected chart");
    Check(grid->GetCellDisplayValue(1, 1) == "10", "...and not the active cell's value");
    grid->Undo();
    Check(grid->GetChartCount() == 1, "Undo brings it back");

    // 8. Printed: on the page, and the pages reach the cells under it.
    {
        // (Undo of the inserted rows put back the sheet as it was: fetched again.)
        SpreadsheetPrintPages pages(*grid->GetActiveSheet());
        const int count = pages.Paginate();
        int lastCol = -1;
        for (int i = 0; i < count; ++i) lastCol = std::max(lastCol, pages.GetPageCells(i).end.col);
        Check(count >= 1 && lastCol >= grid->GetChart(0)->anchor.to.col,
              "the pages reach the chart's last column (" + std::to_string(lastCol) + ")");
        RasterPageTarget paper(612, 792, 72, 72);   // Letter, a pixel to the point
        paper.BeginPage();
        pages.RenderPage(paper.GetRenderContext(), 0);
        int printed = 0, elsewhere = 0;
        CountBlue(paper, Rect2Di(0, 0, 612, 792), printed, elsewhere);
        Check(printed > 300, "the first page shows the chart's columns (" + std::to_string(printed) + " pixels)");
        std::vector<uint8_t> pdf;
        std::string error;
        Check(pages.ExportPdf(pdf, error) && pdf.size() > 1000, "the pages as a PDF [" + error + "]");
    }

    // 9. An untitled chart's top value label fits inside the chart: the
    //    labels of a left axis are centred on their ticks, and the top one
    //    stood half its height above the chart's rectangle, cut off there.
    {
        RichChart untitled;
        untitled.kind = RichChart::Kind::Column;
        untitled.categories = {"M1", "M2", "M3", "M4", "M5", "M6"};
        RichChartSeries sales;
        sales.name = "Sales";
        sales.values = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0};
        untitled.series.push_back(sales);
        RasterPageTarget page(500, 400, 96, 96);
        page.BeginPage();
        const Rect2Dd at(50.0, 50.0, 400.0, 300.0);
        Check(DrawRichDocumentChart(page.GetRenderContext(), untitled, at, Size2Di(400, 300)), "an untitled chart drawn");
        int above = 0, inBand = 0, y = 0;
        page.ForEachRow([&](const uint8_t* rgba, int width) {
            for (int x = 0; x < width; ++x) {
                const uint8_t* p = rgba + 4 * x;
                const bool ink = p[0] < 160 && p[1] < 160 && p[2] < 160;
                if (!ink) continue;
                if (y < 50) ++above;
                else if (y < 70 && x < 120) ++inBand;
            }
            ++y;
            return true;
        });
        Check(inBand > 5, "its top value label is drawn (" + std::to_string(inBand) + " pixels of it)");
        Check(above == 0, "and nothing above the chart's rectangle (" + std::to_string(above) + " pixels)");
    }

    // Every type, from its sample cells.
    std::printf("\n-- every chart type --\n");
    const auto& types = SpreadsheetChartTypes();
    Check(types.size() >= 50, "the gallery offers " + std::to_string(types.size()) + " types");
    const char* shots = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
    for (size_t t = 0; t < types.size(); ++t) {
        const std::string name = std::string(types[t].id) + " (" + SpreadsheetChartTypeName(t) + ")";
        auto book = std::make_shared<UltraCanvasSpreadsheet>("types", 0, 0, 900, 600);
        SpreadsheetSheet& cells = *book->GetActiveSheet();
        const CellRange block = ChartSamples::WriteSampleCells(cells, types[t].kind, 0, 0);
        const SpreadsheetChart made = MakeSpreadsheetChartOfType(cells, block, t);
        const std::string problem = SpreadsheetChartFitMessage(made);
        Check(problem.empty(), name + ": its sample cells fit" + (problem.empty() ? "" : " - " + problem));
        Check(made.chart.kind == types[t].kind && SpreadsheetChartTypeOf(made.chart) == static_cast<int>(t),
              name + ": made as its type (" + std::to_string(SpreadsheetChartTypeOf(made.chart)) + ")");
        const int width = 480, height = 300;
        auto ctx = CreateRenderContext(Size2Di(width, height), nullptr);
        if (!ctx) {
            Check(false, name + ": an off-screen surface");
            continue;
        }
        cairo_t* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRectangle(Rect2Dd(0, 0, width, height));
        const bool drawn = DrawSpreadsheetChart(ctx.get(), made, Rect2Dd(0, 0, width, height));
        Check(drawn, name + ": drawn");
        cairo_surface_t* surface = cairo_get_target(cr);
        cairo_surface_flush(surface);
        const unsigned char* data = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        int inked = 0;
        for (int y = 6; y < height - 6 && data; ++y) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(data + y * stride);
            for (int x = 6; x < width - 6; ++x) {
                const uint32_t p = row[x];
                const int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
                if (r < 235 || g < 235 || b < 235) ++inked;
            }
        }
        Check(inked > 400, name + ": something drawn in its box (" + std::to_string(inked) + " pixels)");
        if (shots) {
            const std::string path = std::string(shots) + "/chart-" + types[t].id + ".png";
            cairo_surface_write_to_png(surface, path.c_str());   // path-string-ok: test output under an ASCII directory the caller chose
        }
    }
    // Every type over every other type's sample, as the gallery draws them
    // (what does not fit is skipped): nothing crashes, whatever the columns.
    {
        int drawnCount = 0;
        for (size_t s = 0; s < types.size(); ++s) {
            auto book = std::make_shared<UltraCanvasSpreadsheet>("mixed", 0, 0, 900, 600);
            SpreadsheetSheet& cells = *book->GetActiveSheet();
            const CellRange block = ChartSamples::WriteSampleCells(cells, types[s].kind, 0, 0);
            for (size_t t = 0; t < types.size(); ++t) {
                const SpreadsheetChart made = MakeSpreadsheetChartOfType(cells, block, t);
                if (!SpreadsheetChartFitMessage(made).empty()) continue;
                auto ctx = CreateRenderContext(Size2Di(144, 96), nullptr);
                if (ctx && DrawSpreadsheetChart(ctx.get(), made, Rect2Dd(0, 0, 144, 96))) ++drawnCount;
            }
        }
        Check(drawnCount > 1000, "every type over every sample that fits it drawn without a crash (" +
                                     std::to_string(drawnCount) + " pictures)");
    }

    // A selection that does not fit a type says why.
    {
        auto book = std::make_shared<UltraCanvasSpreadsheet>("misfit", 0, 0, 900, 600);
        SpreadsheetSheet& cells = *book->GetActiveSheet();
        cells.SetCellValue(0, 0, std::string("Month"));
        cells.SetCellValue(0, 1, std::string("Sales"));
        for (int i = 0; i < 4; ++i) {
            cells.SetCellValue(i + 1, 0, std::string("M") + std::to_string(i + 1));
            cells.SetCellValue(i + 1, 1, 3.0 + i);
        }
        book->Select(CellRange(0, 0, 4, 1));
        std::string problem;
        const int bubble = SpreadsheetChartTypeById("bubble");
        Check(bubble >= 0 && book->InsertChartOfTypeFromSelection(static_cast<size_t>(bubble), &problem) < 0 &&
                  problem.find("three columns") != std::string::npos,
              "a bubble chart of one column is refused: " + problem);
        const int sankey = SpreadsheetChartTypeById("sankey");
        problem.clear();
        Check(sankey >= 0 && book->InsertChartOfTypeFromSelection(static_cast<size_t>(sankey), &problem) < 0 && !problem.empty(),
              "a Sankey diagram without targets is refused: " + problem);
        const int funnel = SpreadsheetChartTypeById("funnel");
        Check(funnel >= 0 && book->InsertChartOfTypeFromSelection(static_cast<size_t>(funnel)) == 0 &&
                  book->GetChart(0) && book->GetChart(0)->chart.kind == RichChart::Kind::Funnel,
              "a funnel of it is inserted");
        // Changed to a waterfall through its type: one undo step.
        if (const SpreadsheetChart* chart = book->GetChart(0)) {
            SpreadsheetChart changed = *chart;
            SetSpreadsheetChartType(changed, cells, static_cast<size_t>(SpreadsheetChartTypeById("waterfall")));
            Check(book->SetChart(0, changed) && book->GetChart(0)->chart.kind == RichChart::Kind::Waterfall,
                  "its type changed to a waterfall");
            book->Undo();
            Check(book->GetChart(0) && book->GetChart(0)->chart.kind == RichChart::Kind::Funnel, "Undo makes it a funnel again");
        }
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures;
}
