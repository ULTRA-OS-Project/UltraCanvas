// Tests/PerformanceMatrixRenderTest.cpp
// The performance matrix drawn offscreen, in each of its four designs.
//
// The model has its own unit test (PerformanceMatrixModelTest); this one
// covers what the element adds. Every cell must be laid out inside the
// element and hit where it is drawn. The colouring must paint each cell its
// rank or level colour, because the colour is part of the reading. Hover and
// click must select. A matrix too small for its text must fit it by dropping
// content, and the dropped content must still be in the tooltip.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, each design is also written there as a
// PNG for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPerformanceMatrix.h"
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

struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;

    Canvas(int w, int h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!ctx) return;
        cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }

    Color PixelAt(int x, int y) const {
        if (!cr) return Color(0, 0, 0, 0);
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
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

bool Close(const Color& a, const Color& b, int tolerance = 6) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

UCEvent Mouse(UCEventType type, const Point2Di& at) {
    UCEvent e;
    e.type = type;
    e.pointer = at;
    e.button = UCMouseButton::Left;
    return e;
}

Point2Di Centre(const Rect2Dd& r) {
    return Point2Di(static_cast<int>(r.x + r.width / 2), static_cast<int>(r.y + r.height / 2));
}

// Renders `model` in `design`; checks that every cell is inside the element,
// reachable at its centre, and that cells do not overlap.
std::shared_ptr<UltraCanvasPerformanceMatrix> RenderAndCheck(const std::string& name, Canvas& canvas,
                                                             const PerformanceMatrixModel& model,
                                                             PerformanceMatrixDesign design,
                                                             PerformanceCellColoring coloring,
                                                             const PerformanceMatrixPalette& palette,
                                                             int w, int h) {
    std::cout << "\n" << name << "\n";
    auto matrix = CreatePerformanceMatrix("pm", 0, 0, w, h);
    matrix->SetEnableTooltips(false);   // no window to show one in
    matrix->SetModel(model);
    matrix->SetDesign(design);
    matrix->SetCellColoring(coloring);
    matrix->SetPalette(palette);
    matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)));

    bool inside = true, reachable = true, disjoint = true;
    for (int r = 0; r < model.Rows(); ++r) {
        for (int c = 0; c < model.Columns(); ++c) {
            const Rect2Dd rect = matrix->GetCellRect(r, c);
            inside &= rect.width > 20 && rect.height > 20 && rect.x >= 0 && rect.y >= 0 &&
                      rect.x + rect.width <= w && rect.y + rect.height <= h;
            reachable &= matrix->CellAt(Centre(rect)) == PerformanceCellRef(r, c);
            for (int r2 = 0; r2 < model.Rows(); ++r2) {
                for (int c2 = 0; c2 < model.Columns(); ++c2) {
                    if (r2 == r && c2 == c) continue;
                    const Rect2Dd o = matrix->GetCellRect(r2, c2);
                    const bool overlap = rect.x + 0.5 < o.x + o.width && o.x + 0.5 < rect.x + rect.width &&
                                         rect.y + 0.5 < o.y + o.height && o.y + 0.5 < rect.y + rect.height;
                    disjoint &= !overlap;
                }
            }
        }
    }
    Check(inside, "every cell is laid out inside the element");
    Check(reachable, "every cell is hit at its centre");
    Check(disjoint, "no two cells overlap");
    return matrix;
}

void TestTiles() {
    Canvas canvas(820, 600);
    if (!canvas.ctx) { Check(false, "an offscreen context is available"); return; }
    const PerformanceMatrixModel model = PerformanceMatrixSamples::PerformanceVsPotential();
    const PerformanceMatrixPalette palette = PerformanceMatrixPalette::TrafficLight();
    auto matrix = RenderAndCheck("Tiles, performance vs potential", canvas, model, PerformanceMatrixDesign::Tiles,
                                 PerformanceCellColoring::DiagonalBands, palette, 820, 600);

    // The best corner is drawn top right and painted the first rank colour,
    // the worst bottom left in the last.
    const Rect2Dd best = matrix->GetCellRect(2, 2), worst = matrix->GetCellRect(0, 0);
    Check(best.x > worst.x && best.y < worst.y, "high / high is drawn at the top right");
    auto topStrip = [&](const Rect2Dd& r) {   // inside the padding, clear of the rounded corners and the text
        return canvas.PixelAt(static_cast<int>(r.x + r.width / 2), static_cast<int>(r.y + 4));
    };
    Check(Close(topStrip(best), palette.rankColors.front()), "the best tile is painted the best rank colour");
    Check(Close(topStrip(worst), palette.rankColors.back()), "the worst tile is painted the worst rank colour");
    Check(Close(topStrip(matrix->GetCellRect(1, 1)), palette.rankColors[2]), "the centre tile is the middle colour");
    Check(!matrix->AnyCellTruncated(), "every code and description fits");
    canvas.Save("performance_matrix_tiles");
}

void TestHeaderTabs() {
    Canvas canvas(820, 600);
    if (!canvas.ctx) return;
    const PerformanceMatrixModel model = PerformanceMatrixSamples::PerformanceManagement();
    auto matrix = RenderAndCheck("Header tabs, performance management", canvas, model,
                                 PerformanceMatrixDesign::HeaderTabs, PerformanceCellColoring::Plain,
                                 PerformanceMatrixPalette::Vivid(), 820, 600);
    const Rect2Dd cell = matrix->GetCellRect(1, 1);
    Check(Close(canvas.PixelAt(static_cast<int>(cell.x + 4), static_cast<int>(cell.y + 4)), Color(255, 255, 255, 255)),
          "plain cells stay white");
    // The column tab above the middle column - over the top row, the high
    // level - is painted in that level's own accent.
    const Rect2Dd top = matrix->GetCellRect(model.Rows() - 1, 1);
    const Color accent = model.xAxis.levels[1].accent;
    Check(Close(canvas.PixelAt(static_cast<int>(top.x + top.width / 2), static_cast<int>(top.y - 12)), accent),
          "the column tab carries its level's accent");
    canvas.Save("performance_matrix_header_tabs");

    // One hue: the same design, every colour a shade of blue.
    Canvas mono(820, 600);
    if (!mono.ctx) return;
    const PerformanceMatrixPalette blue = PerformanceMatrixPalette::Monochrome(Color(36, 104, 178, 255));
    auto monochrome = RenderAndCheck("Header tabs, monochrome", mono, model, PerformanceMatrixDesign::HeaderTabs,
                                     PerformanceCellColoring::Plain, blue, 820, 600);
    const Rect2Dd mc = monochrome->GetCellRect(model.Rows() - 1, 1);
    const Color tab = mono.PixelAt(static_cast<int>(mc.x + mc.width / 2), static_cast<int>(mc.y - 12));
    Check(!Close(tab, accent) && tab.b > tab.r, "a one-hue palette overrides the levels' own accents");
    mono.Save("performance_matrix_monochrome");
}

void TestCards() {
    Canvas canvas(900, 640);
    if (!canvas.ctx) return;
    const PerformanceMatrixModel model = PerformanceMatrixSamples::TalentReview();
    auto matrix = RenderAndCheck("Cards, talent review", canvas, model, PerformanceMatrixDesign::Cards,
                                 PerformanceCellColoring::ByRow, PerformanceMatrixPalette::Pastel(), 900, 640);
    Check(!matrix->AnyCellTruncated(), "at full size every card keeps its three bullets");
    canvas.Save("performance_matrix_cards");

    // Too small for the text: the font shrinks to the floor, then bullets go,
    // and the tooltip still has all of them.
    Canvas small(460, 330);
    if (!small.ctx) return;
    auto cramped = RenderAndCheck("Cards, too small", small, model, PerformanceMatrixDesign::Cards,
                                  PerformanceCellColoring::ByRow, PerformanceMatrixPalette::Pastel(), 460, 330);
    Check(cramped->GetFittedFontSize() < matrix->GetFittedFontSize(), "a smaller matrix fits a smaller font");
    Check(cramped->AnyCellTruncated(), "and drops content when even the smallest font does not fit");
    const std::string tip = cramped->BuildCellTooltip(PerformanceCellRef(2, 2));
    bool all = true;
    for (const std::string& bullet : model.FindCell(2, 2)->bullets) all &= tip.find(bullet) != std::string::npos;
    Check(all, "the tooltip keeps every bullet a cramped card dropped");
    // The long title shrinks to the width instead of running off both sides:
    // the first and last columns stay background.
    bool clear = true;
    for (int y = 0; y < 40; ++y) {
        clear &= Close(small.PixelAt(1, y), Color(255, 255, 255, 255)) &&
                 Close(small.PixelAt(458, y), Color(255, 255, 255, 255));
    }
    Check(clear, "a title too long for the element is fitted to its width");
    small.Save("performance_matrix_cards_small");
}

void TestGridAndInteraction() {
    Canvas canvas(820, 600);
    if (!canvas.ctx) return;
    const PerformanceMatrixModel model = PerformanceMatrixSamples::NineBox();
    auto matrix = RenderAndCheck("Grid, 9-box", canvas, model, PerformanceMatrixDesign::Grid,
                                 PerformanceCellColoring::DiagonalBands, PerformanceMatrixPalette::TrafficLight(),
                                 820, 600);
    canvas.Save("performance_matrix_grid");

    PerformanceCellRef hovered, clicked;
    matrix->onCellHover = [&hovered](const PerformanceCellRef& ref) { hovered = ref; };
    matrix->onCellClick = [&clicked](const PerformanceCellRef& ref) { clicked = ref; };
    const Point2Di target = Centre(matrix->GetCellRect(0, 2));
    matrix->HandleChartMouseMove(target);
    Check(matrix->GetHoveredCell() == PerformanceCellRef(0, 2) && hovered == PerformanceCellRef(0, 2),
          "hovering a cell reports it");
    matrix->OnEvent(Mouse(UCEventType::MouseDown, target));
    Check(clicked == PerformanceCellRef(0, 2) && matrix->GetSelectedCell() == PerformanceCellRef(0, 2),
          "clicking a cell selects it and reports it");
    matrix->OnEvent(Mouse(UCEventType::MouseDown, target));
    Check(!matrix->GetSelectedCell().IsValid(), "a second click on it clears the selection");
    matrix->HandleChartMouseMove(Point2Di(2, 2));
    Check(!matrix->GetHoveredCell().IsValid() && !hovered.IsValid(), "leaving the cells reports no cell");

    const std::string tip = matrix->BuildCellTooltip(PerformanceCellRef(2, 2));
    Check(tip.find("Future Leader") != std::string::npos && tip.find("Potential: High") != std::string::npos &&
          tip.find("Performance: High") != std::string::npos, "a tooltip names the cell and both of its levels");

    // Reversed axes move the drawing, not the meaning.
    PerformanceMatrixModel flipped = model;
    flipped.yAxis.reversed = true;
    flipped.xAxis.reversed = true;
    matrix->SetModel(flipped);
    matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, 820, 600));
    const Rect2Dd best = matrix->GetCellRect(2, 2), worst = matrix->GetCellRect(0, 0);
    Check(best.x < worst.x && best.y > worst.y, "reversed axes draw high / high at the bottom left");
    Check(matrix->CellAt(Centre(best)) == PerformanceCellRef(2, 2), "and it is still cell (2, 2)");
}

void TestSkillWill() {
    Canvas canvas(640, 480);
    if (!canvas.ctx) return;
    RenderAndCheck("Grid, skill / will (2x2)", canvas, PerformanceMatrixSamples::SkillWill(),
                   PerformanceMatrixDesign::Grid, PerformanceCellColoring::DiagonalBands,
                   PerformanceMatrixPalette::TrafficLight(), 640, 480);
    canvas.Save("performance_matrix_skill_will");
}

} // namespace

int main() {
    std::cout << "====================================\n";
    std::cout << "  Performance Matrix Render Test\n";
    std::cout << "====================================\n";

    TestTiles();
    TestHeaderTabs();
    TestCards();
    TestGridAndInteraction();
    TestSkillWill();

    std::cout << "\n====================================\n";
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED.\n";
    return 1;
}
