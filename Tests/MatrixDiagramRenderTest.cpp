// Tests/MatrixDiagramRenderTest.cpp
// The matrix diagram drawn offscreen in every shape, and its hit test swept.
//
// The model has its own unit test (MatrixModelTest); this one covers what the
// element adds: that each shape lays out and paints, and that the cell lattice
// behind every shape is consistent with itself. The sweep moves the pointer
// over the whole element and records which cell each point lands on. Every
// cell of every panel must be reachable - so no panel is lost off the edge,
// hidden under another, or turned the wrong way round - and each must be hit
// over a patch of roughly the same area, which a lattice with a wrong step or
// a wrong inverse would not give.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, each shape is also written there as a
// PNG for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMatrixDiagram.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <tuple>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

const int kWidth = 900;
const int kHeight = 680;

struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;

    Canvas(int w, int h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!ctx) return;
        cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }

    // Pixels differing from the diagram's own background colour.
    int InkPixels(const Color& background) const {
        if (!cr) return -1;
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        const int w = cairo_image_surface_get_width(s);
        const int h = cairo_image_surface_get_height(s);
        const uint32_t bg = (static_cast<uint32_t>(background.r) << 16)
                            | (static_cast<uint32_t>(background.g) << 8) | background.b;
        int ink = 0;
        for (int y = 0; y < h; ++y) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(data + y * stride);
            for (int x = 0; x < w; ++x) {
                if ((row[x] & 0x00FFFFFFu) != bg) ++ink;
            }
        }
        return ink;
    }

    void Save(const std::string& name) const {
        const char* dir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
        if (!dir || !cr) return;
        const std::string path = std::string(dir) + "/" + name + ".png";
        cairo_surface_write_to_png(cairo_get_target(cr), path.c_str());   // path-string-ok: test output under an ASCII directory the caller chose
        std::cout << "  wrote " << path << "\n";
    }
};

// Renders the model, then sweeps the hit test and checks every cell of every
// panel is reachable over a comparable area.
void CheckShape(const std::string& name, const MatrixModel& model,
                MatrixHubMode hub = MatrixHubMode::Blank, bool tint = false) {
    std::cout << "\n" << name << "\n";

    Canvas canvas(kWidth, kHeight);
    Check(canvas.ctx != nullptr, "an offscreen context is available");
    if (!canvas.ctx) return;

    auto diagram = CreateMatrixDiagramElement("matrix", 0, 0, kWidth, kHeight, model);
    diagram->SetEnableTooltips(false);   // no window to show one in
    diagram->SetHubMode(hub);
    diagram->SetTotals(MatrixTotals::Both);
    if (tint) {
        MatrixDiagramStyle style = diagram->GetStyle();
        style.tintCellsByLevel = true;
        diagram->SetStyle(style);
    }

    diagram->Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
    Check(canvas.InkPixels(Color(252, 252, 253, 255)) > kWidth * kHeight / 20,
          "draws a substantial picture");
    canvas.Save("matrix-" + name);

    // ---- Sweep ----
    std::map<std::tuple<int, int, int>, int> hits;
    int armHits = 0;
    for (int y = 0; y < kHeight; y += 2) {
        for (int x = 0; x < kWidth; x += 2) {
            diagram->HandleChartMouseMove(Point2Di(x, y));
            const MatrixRef ref = diagram->GetHoveredCell();
            if (ref.IsCell()) {
                ++hits[std::make_tuple(ref.panel, ref.row, ref.col)];
            } else if (ref.IsValid()) {
                ++armHits;
            }
        }
    }

    bool allReached = true;
    int smallest = 1 << 30, largest = 0;
    for (size_t p = 0; p < model.panels.size(); ++p) {
        const MatrixItemSet* rows = model.RowSet(static_cast<int>(p));
        const MatrixItemSet* cols = model.ColSet(static_cast<int>(p));
        if (!rows || !cols) continue;
        // An L shows panel 0 only.
        if (model.shape == MatrixShape::L && p > 0) continue;
        for (int r = 0; r < rows->Count(); ++r) {
            for (int c = 0; c < cols->Count(); ++c) {
                auto it = hits.find(std::make_tuple(static_cast<int>(p), r, c));
                const int n = (it == hits.end()) ? 0 : it->second;
                if (n == 0) {
                    allReached = false;
                    std::cout << "    unreachable: panel " << p << " (" << r << ", " << c << ")\n";
                }
                smallest = std::min(smallest, n);
                largest = std::max(largest, n);
            }
        }
    }
    Check(allReached, "every cell of every panel can be pointed at");
    // Within one shape the cells share a size (an X's cells can differ in
    // width and height, not between quadrants), so their hit areas should
    // agree to within the sampling error of a 2px sweep.
    Check(smallest > 0 && largest <= smallest * 2,
          "every cell is hit over a comparable area (" + std::to_string(smallest) + " to "
          + std::to_string(largest) + " samples)");
    Check(armHits > 0, "the row and column labels are hit too");

    // Hovering a cell and reading back the tooltip text is the round trip a
    // user makes; the ref must name a real cell of the model.
    bool consistent = true;
    for (const auto& entry : hits) {
        const int p = std::get<0>(entry.first);
        if (p < 0 || p >= static_cast<int>(model.panels.size())) consistent = false;
    }
    Check(consistent, "every hit names a panel the model has");
}

void TestTransposedXPanel() {
    std::cout << "\nX panel named either way round\n";

    // The same quadrant, once with the top set as the row set and once with
    // the left set. The layout turns the second to fit, so the L3 x T2 cell
    // must be found at the same place on screen both times.
    int seenX[2] = {-1, -1}, seenY[2] = {-1, -1};
    for (int turned = 0; turned < 2; ++turned) {
        MatrixModel model;
        model.shape = MatrixShape::X;
        model.AddSet("left", "Left", {"L1", "L2", "L3"});
        model.AddSet("top", "Top", {"T1", "T2"});
        model.AddSet("right", "Right", {"R1", "R2"});
        model.AddSet("bottom", "Bottom", {"B1", "B2", "B3", "B4"});
        if (turned) model.AddPanel("left", "top", MatrixScale::Presence());
        else        model.AddPanel("top", "left", MatrixScale::Presence());
        model.AddPanel("top", "right", MatrixScale::Presence());
        model.AddPanel("bottom", "right", MatrixScale::Presence());
        model.AddPanel("bottom", "left", MatrixScale::Presence());

        auto diagram = CreateMatrixDiagramElement("x", 0, 0, 600, 500, model);
        diagram->SetEnableTooltips(false);
        Canvas canvas(600, 500);
        if (!canvas.ctx) return;
        diagram->Render(canvas.ctx.get(), Rect2Df(0, 0, 600, 500));

        // In the turned panel L3 x T2 is (row 2, col 1), otherwise (row 1,
        // col 2). Record the first point that lands on it.
        int foundX = -1, foundY = -1;
        const MatrixRef want = turned ? MatrixRef{0, 2, 1} : MatrixRef{0, 1, 2};
        for (int y = 0; y < 500 && foundX < 0; y += 2) {
            for (int x = 0; x < 600; x += 2) {
                diagram->HandleChartMouseMove(Point2Di(x, y));
                if (diagram->GetHoveredCell() == want) { foundX = x; foundY = y; break; }
            }
        }
        Check(foundX >= 0, std::string(turned ? "turned" : "unturned")
                           + " panel: the L3 x T2 cell is on screen");
        seenX[turned] = foundX;
        seenY[turned] = foundY;

        // The first cell met scanning from the top left is the quadrant's
        // corner, item 0 of both sets, whichever way the panel is named.
        MatrixRef origin;
        for (int y = 0; y < 500 && !origin.IsCell(); y += 2) {
            for (int x = 0; x < 600; x += 2) {
                diagram->HandleChartMouseMove(Point2Di(x, y));
                const MatrixRef ref = diagram->GetHoveredCell();
                if (ref.IsCell() && ref.panel == 0) { origin = ref; break; }
            }
        }
        Check(origin.IsCell() && origin.row == 0 && origin.col == 0,
              "the quadrant's top-left cell is item 0 of both sets");
    }
    Check(seenX[0] >= 0 && seenX[0] == seenX[1] && seenY[0] == seenY[1],
          "a turned panel puts each cell where the unturned one does");
}

} // namespace

int main() {
    std::cout << "================================\n";
    std::cout << "  Matrix Diagram Render Tests\n";
    std::cout << "================================\n";

    CheckShape("L", MatrixSamples::ImprovementTools());
    CheckShape("T", MatrixSamples::EmployeeAllocation());
    CheckShape("X-blank", MatrixSamples::SupplyChain(), MatrixHubMode::Blank);
    CheckShape("X-captions", MatrixSamples::SupplyChain(), MatrixHubMode::Captions, true);
    CheckShape("Y", MatrixSamples::DesignTraceability());
    TestTransposedXPanel();

    std::cout << "\n================================\n";
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED.\n";
    return 1;
}
