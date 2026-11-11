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
// Version: 1.2.0 - calibration: dragging a person to another box, the veto,
//   click versus drag, the keyboard, Ctrl+arrows, the "+N more" list
// Version: 1.1.0 - the population: counts, dots and names hit where drawn, "+N"
//   accounting, the legend hiding a group, anonymised, ByCount, level bars
// Version: 1.0.0
// Last Modified: 2026-11-03
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
#include <vector>

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

UCEvent Key(UCKeys key, bool ctrl = false) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    e.ctrl = ctrl;
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

PerformanceMatrixModel Calibration() {
    PerformanceMatrixModel model = PerformanceMatrixSamples::NineBox();
    model.title = "Calibration";
    model.items = PerformanceMatrixSamples::SampleTeam();
    model.targetDistribution = PerformanceMatrixSamples::SampleTargetDistribution();
    return model;
}

double Luma(const Color& c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; }

void TestPopulation() {
    const int W = 980, H = 680;
    const PerformanceMatrixModel model = Calibration();

    // ---- Count, the default --------------------------------------------------
    {
        Canvas canvas(W, H);
        if (!canvas.ctx) return;
        auto matrix = RenderAndCheck("Population, counts", canvas, model, PerformanceMatrixDesign::Grid,
                                     PerformanceCellColoring::DiagonalBands, PerformanceMatrixPalette::TrafficLight(), W, H);
        Check(matrix->GetPopulationDisplay() == PerformancePopulationDisplay::Count, "counts are the default");
        int sum = 0;
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) sum += matrix->GetCellCount(PerformanceCellRef(r, c));
        Check(sum == 36, "the cells hold all 36 people");
        Check(matrix->GetItemRect(0).width == 0.0, "counts draw no individual marks");
        Check(matrix->ItemAt(Centre(matrix->GetCellRect(1, 1))) == -1, "so no item answers the pointer");
        const std::string tip = matrix->BuildCellTooltip(PerformanceCellRef(1, 1));
        Check(tip.find("items") != std::string::npos && tip.find("target 30%") != std::string::npos,
              "a cell's tooltip gives its count, share and target");
        Check(matrix->LegendEntryAt(Point2Di(5, 5)) == SIZE_MAX, "the legend answers only over itself");
        canvas.Save("performance_matrix_counts");
    }

    // ---- Dots: each person is hit where drawn ---------------------------------
    {
        Canvas canvas(W, H);
        if (!canvas.ctx) return;
        auto matrix = CreatePerformanceMatrix("pm", 0, 0, W, H);
        matrix->SetEnableTooltips(false);
        matrix->SetModel(model);
        matrix->SetPopulationDisplay(PerformancePopulationDisplay::Dots);
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        std::cout << "\nPopulation, dots\n";
        bool allHit = true, inside = true;
        for (size_t i = 0; i < model.items.size(); ++i) {
            const Rect2Dd r = matrix->GetItemRect(i);
            if (r.width == 0.0) { allHit = false; continue; }
            allHit &= matrix->ItemAt(Centre(r)) == static_cast<int>(i);
            const PerformanceCellRef cell = model.CellOf(model.items[i]);
            const Rect2Dd box = matrix->GetCellRect(cell.row, cell.col);
            inside &= r.x >= box.x && r.y >= box.y && r.x + r.width <= box.x + box.width && r.y + r.height <= box.y + box.height;
        }
        Check(allHit, "every person is drawn as a dot and hit at it");
        Check(inside, "every dot is inside its person's cell");

        const PerformanceMatrixItem* clicked = nullptr;
        matrix->onItemClick = [&clicked](const PerformanceMatrixItem& item) { clicked = &item; };
        const Point2Di at = Centre(matrix->GetItemRect(5));
        matrix->HandleChartMouseMove(at);
        Check(matrix->GetHoveredItem() == 5, "hovering a dot hovers its person");
        matrix->OnEvent(Mouse(UCEventType::MouseDown, at));
        Check(clicked && clicked->label == model.items[5].label, "clicking a dot reports its person");
        Check(!matrix->GetSelectedCell().IsValid(), "and does not select the cell under it");
        const std::string tip = matrix->BuildItemTooltip(5);
        Check(tip.find(model.items[5].label) != std::string::npos && tip.find("Scores:") != std::string::npos,
              "a person's tooltip gives the name and scores");

        // The legend hides a group: its dots go, and the counts drop.
        const std::string group = model.items[5].group;
        const int before = matrix->GetCellCount(model.CellOf(model.items[5]));
        bool clickedLegend = false;
        for (int y = 0; y < H && !clickedLegend; y += 2) {
            for (int x = W - 200; x < W && !clickedLegend; x += 2) {
                if (matrix->LegendEntryAt(Point2Di(x, y)) == 0) {
                    matrix->OnEvent(Mouse(UCEventType::MouseDown, Point2Di(x, y)));
                    clickedLegend = true;
                }
            }
        }
        Check(clickedLegend, "the legend sits at the right");
        Check(!matrix->IsGroupVisible(model.Groups()[0]), "a click on a legend entry hides its group");
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        bool gone = true;
        for (size_t i = 0; i < model.items.size(); ++i) {
            if (model.items[i].group == model.Groups()[0]) gone &= matrix->GetItemRect(i).width == 0.0;
        }
        Check(gone, "a hidden group's dots are not drawn");
        Check(group != model.Groups()[0] || matrix->GetCellCount(model.CellOf(model.items[5])) < before,
              "and its people leave the counts");
        canvas.Save("performance_matrix_dots_hidden_group");
        matrix->SetGroupVisible(model.Groups()[0], true);
        Check(matrix->IsGroupVisible(model.Groups()[0]), "a group can be shown again");
    }

    // ---- Names: what does not fit is counted, not lost -------------------------
    {
        Canvas canvas(W, H);
        if (!canvas.ctx) return;
        auto matrix = CreatePerformanceMatrix("pm", 0, 0, W, H);
        matrix->SetEnableTooltips(false);
        matrix->SetModel(model);
        matrix->SetDesign(PerformanceMatrixDesign::Cards);
        matrix->SetCellColoring(PerformanceCellColoring::ByCount);
        matrix->SetPopulationDisplay(PerformancePopulationDisplay::Names);
        matrix->SetShowTotals(true);
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        std::cout << "\nPopulation, names\n";
        int drawn = 0;
        bool hit = true;
        for (size_t i = 0; i < model.items.size(); ++i) {
            const Rect2Dd r = matrix->GetItemRect(i);
            if (r.width == 0.0) continue;
            ++drawn;
            hit &= matrix->ItemAt(Centre(r)) == static_cast<int>(i);
        }
        Check(drawn > 0 && hit, "names are hit where they are drawn");
        Check(drawn <= 36, "no one is drawn twice");
        canvas.Save("performance_matrix_names");

        matrix->SetAnonymised(true);
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        bool none = true;
        for (size_t i = 0; i < model.items.size(); ++i) none &= matrix->GetItemRect(i).width == 0.0;
        Check(none, "anonymised, no name is drawn");
        Check(matrix->ItemAt(Centre(matrix->GetCellRect(1, 1))) == -1, "and no person answers the pointer");
        canvas.Save("performance_matrix_anonymised");
    }

    // ---- ByCount and level bars ---------------------------------------------
    {
        Canvas canvas(W, H);
        if (!canvas.ctx) return;
        auto matrix = CreatePerformanceMatrix("pm", 0, 0, W, H);
        matrix->SetEnableTooltips(false);
        matrix->SetModel(model);
        matrix->SetCellColoring(PerformanceCellColoring::ByCount);
        matrix->SetPopulationDisplay(PerformancePopulationDisplay::Percent);
        matrix->SetShowLevelBars(true);
        matrix->SetShowLegend(false);
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        std::cout << "\nByCount and level bars\n";
        // The core box holds the most, the top-left box the fewest.
        const Rect2Dd most = matrix->GetCellRect(1, 1), fewest = matrix->GetCellRect(2, 0);
        const Color a = canvas.PixelAt(static_cast<int>(most.x + 4), static_cast<int>(most.y + most.height - 4));
        const Color b = canvas.PixelAt(static_cast<int>(fewest.x + 4), static_cast<int>(fewest.y + fewest.height - 4));
        Check(Luma(a) < Luma(b), "the fuller cell is the darker one");
        const Rect2Dd low = matrix->GetCellRect(0, 0);
        const Color bar = canvas.PixelAt(static_cast<int>(low.x + low.width / 2), static_cast<int>(low.y + low.height + 5));
        Check(Close(bar, PerformanceMatrixPalette::TrafficLight().levelColors.front()),
              "the bar under the lowest column is the lowest level's colour");
        Check(matrix->LegendEntryAt(Point2Di(W - 40, 80)) == SIZE_MAX, "a hidden legend answers nothing");
        canvas.Save("performance_matrix_percent_bycount");
    }
}

void TestCalibration() {
    const int W = 980, H = 680;
    PerformanceMatrixModel model = Calibration();

    // ---- Dragging a person to another box -----------------------------------
    {
        Canvas canvas(W, H);
        if (!canvas.ctx) return;
        auto matrix = CreatePerformanceMatrix("pm", 0, 0, W, H);
        matrix->SetEnableTooltips(false);
        matrix->SetModel(model);
        matrix->SetPopulationDisplay(PerformancePopulationDisplay::Dots);
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        std::cout << "\nCalibration, drag\n";

        // A person placed by scores, so the move must keep them.
        size_t who = 0;
        while (who < model.items.size() && !std::isfinite(model.items[who].xScore)) ++who;
        const PerformanceCellRef from = model.CellOf(model.items[who]);
        const PerformanceCellRef to = from == PerformanceCellRef(2, 2) ? PerformanceCellRef(0, 0) : PerformanceCellRef(2, 2);
        const Point2Di start = Centre(matrix->GetItemRect(who));
        const Point2Di end = Centre(matrix->GetCellRect(to.row, to.col));

        int asked = 0, clicks = 0;
        bool veto = false;
        PerformanceCellRef askedFrom, askedTo;
        matrix->onItemMove = [&](const PerformanceMatrixItem&, const PerformanceCellRef& f, const PerformanceCellRef& t) {
            ++asked;
            askedFrom = f;
            askedTo = t;
            return !veto;
        };
        matrix->onItemClick = [&clicks](const PerformanceMatrixItem&) { ++clicks; };

        Check(!matrix->GetAllowItemDrag(), "moving people is off by default");
        matrix->OnEvent(Mouse(UCEventType::MouseDown, start));
        matrix->OnEvent(Mouse(UCEventType::MouseMove, end));
        matrix->OnEvent(Mouse(UCEventType::MouseUp, end));
        Check(!matrix->IsDraggingItem() && asked == 0 && model.CellOf(matrix->GetModel().items[who]) == from,
              "so a drag moves no one");
        Check(clicks == 1, "and the press is a click, reported at once");

        matrix->SetAllowItemDrag(true);
        clicks = 0;
        matrix->OnEvent(Mouse(UCEventType::MouseDown, start));
        Check(clicks == 0, "with dragging on, a press waits to see if it is a click");
        matrix->OnEvent(Mouse(UCEventType::MouseMove, Point2Di(start.x + 2, start.y + 1)));
        Check(!matrix->IsDraggingItem(), "a few pixels of wobble are not a drag");
        matrix->OnEvent(Mouse(UCEventType::MouseMove, end));
        Check(matrix->IsDraggingItem() && matrix->GetDropTarget() == to, "past them, the drag follows the pointer");
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        canvas.Save("performance_matrix_drag");
        matrix->OnEvent(Mouse(UCEventType::MouseUp, end));
        const PerformanceMatrixItem& moved = matrix->GetModel().items[who];
        Check(asked == 1 && askedFrom == from && askedTo == to, "onItemMove is asked, with where from and where to");
        Check(moved.row == to.row && moved.col == to.col, "the person lands in the box dropped on");
        Check(std::isfinite(moved.xScore) && moved.xScore == model.items[who].xScore,
              "as an explicit cell, with the scores kept beside it");
        Check(clicks == 0 && !matrix->IsDraggingItem(), "a drag is not a click");
        Check(matrix->GetSelectedCell() == to && matrix->GetSelectedItem() == static_cast<int>(who),
              "the person and the box stay selected");

        // The veto: the application says no, and nothing changes.
        veto = true;
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        const Point2Di again = Centre(matrix->GetItemRect(who));
        const Point2Di back = Centre(matrix->GetCellRect(from.row, from.col));
        matrix->OnEvent(Mouse(UCEventType::MouseDown, again));
        matrix->OnEvent(Mouse(UCEventType::MouseMove, back));
        matrix->OnEvent(Mouse(UCEventType::MouseUp, back));
        Check(asked == 2 && model.CellOf(matrix->GetModel().items[who]) == to, "a vetoed move changes nothing");

        // A click with dragging on is reported on release.
        veto = false;
        matrix->OnEvent(Mouse(UCEventType::MouseDown, again));
        matrix->OnEvent(Mouse(UCEventType::MouseUp, again));
        Check(clicks == 1 && asked == 2, "press and release in place is one click and no move");

        // Escape drops a drag in progress.
        matrix->OnEvent(Mouse(UCEventType::MouseDown, again));
        matrix->OnEvent(Mouse(UCEventType::MouseMove, back));
        matrix->OnEvent(Key(UCKeys::Escape));
        matrix->OnEvent(Mouse(UCEventType::MouseUp, back));
        Check(!matrix->IsDraggingItem() && asked == 2, "Escape cancels a drag");

        Check(!matrix->MoveItem(who, to), "moving a person to the box they are in does nothing");
        Check(!matrix->MoveItem(who, PerformanceCellRef(3, 0)) && !matrix->MoveItem(999, from),
              "nor does a box or person that does not exist");
    }

    // ---- The keyboard -------------------------------------------------------
    {
        Canvas canvas(W, H);
        if (!canvas.ctx) return;
        auto matrix = CreatePerformanceMatrix("pm", 0, 0, W, H);
        matrix->SetEnableTooltips(false);
        matrix->SetModel(model);
        matrix->SetPopulationDisplay(PerformancePopulationDisplay::Names);
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        std::cout << "\nCalibration, keyboard\n";
        Check(matrix->AcceptsFocus(), "the matrix takes the focus");
        PerformanceCellRef activated;
        matrix->onCellClick = [&activated](const PerformanceCellRef& ref) { activated = ref; };

        matrix->OnEvent(Key(UCKeys::Right));
        Check(matrix->GetSelectedCell() == PerformanceCellRef(2, 0), "the first arrow selects the top-left box");
        matrix->OnEvent(Key(UCKeys::Right));
        matrix->OnEvent(Key(UCKeys::Down));
        Check(matrix->GetSelectedCell() == PerformanceCellRef(1, 1), "arrows move on screen: right, then down");
        for (int i = 0; i < 4; ++i) matrix->OnEvent(Key(UCKeys::Left));
        Check(matrix->GetSelectedCell() == PerformanceCellRef(1, 0), "and stop at the edge");
        matrix->OnEvent(Key(UCKeys::Return));
        Check(activated == PerformanceCellRef(1, 0), "Enter activates the selected box");
        matrix->OnEvent(Key(UCKeys::Escape));
        Check(!matrix->GetSelectedCell().IsValid(), "Escape clears the selection");

        // Ctrl+arrows move the selected person, only when moving is allowed.
        size_t who = 0;
        while (who < model.items.size() && model.CellOf(model.items[who]).col == 2) ++who;
        const PerformanceCellRef from = model.CellOf(model.items[who]);
        matrix->SetSelectedItem(static_cast<int>(who));
        matrix->OnEvent(Key(UCKeys::Right, true));
        Check(model.CellOf(matrix->GetModel().items[who]) == from, "Ctrl+Right moves no one while moving is off");
        matrix->SetAllowItemDrag(true);
        matrix->OnEvent(Key(UCKeys::Right, true));
        const PerformanceCellRef now = model.CellOf(matrix->GetModel().items[who]);
        Check(now == PerformanceCellRef(from.row, from.col + 1), "with it on, Ctrl+Right moves the person one box right");
        Check(matrix->GetSelectedCell() == now, "and the selection follows them");
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
        canvas.Save("performance_matrix_keyboard_move");
        matrix->OnEvent(Key(UCKeys::Escape));
        Check(matrix->GetSelectedItem() == -1 && matrix->GetSelectedCell().IsValid(),
              "Escape lets go of the person first, then the box");

        matrix->SetAnonymised(true);
        matrix->SetSelectedItem(static_cast<int>(who));
        Check(matrix->GetSelectedItem() == -1, "anonymised, no one can be singled out");
    }

    // ---- "+N more" lists everyone in the box ---------------------------------
    {
        const int w = 720, h = 470;
        Canvas canvas(w, h);
        if (!canvas.ctx) return;
        auto matrix = CreatePerformanceMatrix("pm", 0, 0, w, h);
        matrix->SetEnableTooltips(false);
        matrix->SetModel(model);
        matrix->SetPopulationDisplay(PerformancePopulationDisplay::Names);
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)));
        std::cout << "\nCalibration, overflow list\n";
        PerformanceCellRef listed;
        std::vector<size_t> people;
        matrix->onOverflowClick = [&](const PerformanceCellRef& ref, const std::vector<size_t>& items) {
            listed = ref;
            people = items;
        };
        Point2Di mark(-1, -1);
        PerformanceCellRef overflowing;
        for (int y = 0; y < h && mark.x < 0; ++y) {
            for (int x = 0; x < w && mark.x < 0; ++x) {
                overflowing = matrix->OverflowAt(Point2Di(x, y));
                if (overflowing.IsValid()) mark = Point2Di(x, y);
            }
        }
        Check(mark.x >= 0, "a small matrix has a box with \"+N more\"");
        matrix->OnEvent(Mouse(UCEventType::MouseDown, mark));
        bool allThere = listed == overflowing && static_cast<int>(people.size()) == matrix->GetCellCount(overflowing);
        for (size_t i : people) allThere &= model.CellOf(model.items[i]) == overflowing;
        Check(allThere, "clicking it hands over everyone in that box");
        Check(!matrix->GetSelectedCell().IsValid(), "and does not select the box");
        canvas.Save("performance_matrix_overflow");
        matrix->SetAnonymised(true);
        matrix->Render(canvas.ctx.get(), Rect2Df(0, 0, static_cast<float>(w), static_cast<float>(h)));
        Check(!matrix->OverflowAt(mark).IsValid(), "anonymised, there is no list to open");
    }
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
    TestPopulation();
    TestCalibration();

    std::cout << "\n====================================\n";
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED.\n";
    return 1;
}
