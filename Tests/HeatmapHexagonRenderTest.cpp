// Tests/HeatmapHexagonRenderTest.cpp
// The Hexagon cell shape of both heatmaps (UltraCanvasHeatmapChartElement and
// UltraCanvasEngineHeatmapChart) drawn offscreen: the cells are regular
// pointy-top hexagons whatever the plot's shape (the grid's extent has the hex
// grid's proportions, not the plot's), odd rows are shifted right by half a
// cell, the corners between hexagons stay empty, and on the engine heatmap
// the cell under the pointer is the one drawn there.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, the cases are also written there as
// PNGs for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasHeatmapChart.h"
#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineHeatmapChart.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

const int W = 640, H = 360;
const int kCols = 6, kRows = 4;
const Color kInk(200, 30, 30, 255);

struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;
    Canvas() {
        ctx = CreateRenderContext(Size2Di(W, H), nullptr);
        if (ctx) cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }
    Color PixelAt(double px, double py) const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const int x = static_cast<int>(std::lround(px)), y = static_cast<int>(std::lround(py));
        if (x < 0 || y < 0 || x >= W || y >= H) return Color(0, 0, 0, 0);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const uint32_t p = reinterpret_cast<const uint32_t*>(data + y * cairo_image_surface_get_stride(s))[x];
        return Color((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF, 255);
    }
    void Save(const std::string& name) const {
        const char* dir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
        if (!dir || !cr) return;
        const std::string path = std::string(dir) + "/" + name + ".png";
        cairo_surface_write_to_png(cairo_get_target(cr), path.c_str());   // path-string-ok: test output under an ASCII directory the caller chose
        std::cout << "  wrote " << path << "\n";
    }
};

bool IsInk(const Color& c) { return c.r > 150 && c.g < 90 && c.b < 90; }

// Every cell the same red, so all ink is cells.
std::vector<double> Ones() { return std::vector<double>(kCols * kRows, 1.0); }

struct Box { double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9; };

Box InkBox(const Canvas& canvas) {
    Box b;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (!IsInk(canvas.PixelAt(x, y))) continue;
            b.x0 = std::min<double>(b.x0, x);
            b.y0 = std::min<double>(b.y0, y);
            b.x1 = std::max<double>(b.x1, x + 1);
            b.y1 = std::max<double>(b.y1, y + 1);
        }
    }
    return b;
}

// The geometry checks shared by both heatmaps. Returns the circumradius.
double CheckHexGrid(const Canvas& canvas, const std::string& who) {
    const Box b = InkBox(canvas);
    const double width = b.x1 - b.x0, height = b.y1 - b.y0;
    const double expected = (kCols + 0.5) * std::sqrt(3.0) / (1.5 * kRows + 0.5);
    Check(width > 50.0 && std::abs(width / height - expected) < 0.04 * expected,
          who + ": the grid has a hex grid's proportions (regular hexagons), not the plot's");
    const double s = height / (1.5 * kRows + 0.5);
    const double w = std::sqrt(3.0) * s;
    // Row 0, column 0: its centre is ink, the box's corner beside its point is not.
    Check(IsInk(canvas.PixelAt(b.x0 + w * 0.5, b.y0 + s)), who + ": the first hexagon's centre is filled");
    Check(!IsInk(canvas.PixelAt(b.x0 + 2.0, b.y0 + 2.0)), who + ": the corner beside a hexagon's point is empty");
    // Row 1 is shifted right by half a cell: its left edge is empty, its
    // first centre half a cell in.
    const double row1 = b.y0 + s + 1.5 * s;
    Check(!IsInk(canvas.PixelAt(b.x0 + 2.0, row1)) && IsInk(canvas.PixelAt(b.x0 + w, row1)),
          who + ": an odd row is shifted right by half a cell");
    // Between two neighbours in a row, along the top: the notch is empty.
    Check(!IsInk(canvas.PixelAt(b.x0 + w, b.y0 + 2.0)), who + ": the notch between two hexagons is empty");
    return s;
}

void TestLegacy() {
    std::cout << "\n[UltraCanvasHeatmapChartElement]\n";
    Canvas canvas;
    auto hm = CreateHeatmapChartElement("hex", 0, 0, W, H);
    hm->SetData(Ones(), kCols, kRows);
    hm->SetCustomColormap({kInk, kInk});
    hm->SetShowColorBar(false);
    hm->SetCellShape(HeatmapCellShape::Hexagon);
    hm->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
    canvas.Save("heatmap_hexagon_legacy");
    CheckHexGrid(canvas, "legacy");
}

void TestEngine() {
    std::cout << "\n[UltraCanvasEngineHeatmapChart]\n";
    Canvas canvas;
    auto hm = CreateEngineHeatmapChart("hex", 0, 0, W, H);
    hm->SetAnimateOnDataChange(false);
    std::vector<double> values = Ones();
    hm->SetData(values, kCols, kRows);
    hm->SetCustomColormap({kInk, kInk});
    hm->SetShowColorBar(false);
    hm->SetCellShape(ChartHeatmapCellShape::Hexagon);
    hm->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
    canvas.Save("heatmap_hexagon_engine");
    const double s = CheckHexGrid(canvas, "engine");

    // The pointer over a hexagon finds that cell: row 1 (shifted), column 2.
    const Box b = InkBox(canvas);
    const double w = std::sqrt(3.0) * s;
    const Point2Dd centre(b.x0 + w * 0.5 + 2 * w + w * 0.5, b.y0 + s + 1.5 * s);
    std::string tooltip;
    Check(hm->HitRegionAt(centre, &tooltip) == 1 * kCols + 2, "engine: the pointer finds the hexagon drawn there");
    Check(tooltip.find("1 / 2") != std::string::npos, "engine: its tooltip names row 1, column 2");

    // Bottom-up rows: data row 0 is the bottom row.
    hm->SetRowOrder(ChartHeatmapRowOrder::BottomUp);
    Canvas flipped;
    hm->Render(flipped.ctx.get(), Rect2Df(0, 0, W, H));
    const Box fb = InkBox(flipped);
    const Point2Dd bottomLeft(fb.x0 + w * 0.5 + ((kRows - 1) % 2 ? w * 0.5 : 0.0), fb.y1 - s);
    std::string bottom;
    hm->HitRegionAt(bottomLeft, &bottom);
    Check(bottom.find("0 / 0") == 0, "engine: bottom-up puts data row 0 at the bottom");
}

} // namespace

int main() {
    std::cout << "====================================\n";
    std::cout << "  UltraCanvas Heatmap - hexagon cells\n";
    std::cout << "====================================\n";
    TestLegacy();
    TestEngine();
    std::cout << "\n====================================\n";
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED.\n";
    return 1;
}
