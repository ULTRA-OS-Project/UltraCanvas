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
// Version: 1.4.0 - the competitive-assessment block: its rows are hit as the needs, and it gives way when hidden
// Version: 1.3.0 - pan and zoom keep the pointed-at cell put; keyboard navigation follows the screen; editing cycles levels
// Version: 1.2.0 - the House: roof cells only above the diagonal, each centred between its two columns; the bar strip
// Version: 1.1.0 - rotated headers stay inside the element; a theme switch keeps non-colour settings
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMatrixDiagram.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSmoothScroll.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool NearlyEqualScore(double a, double b) { return std::fabs(a - b) < 1e-9; }

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
                MatrixHubMode hub = MatrixHubMode::Blank, bool tint = false,
                MatrixRollUp rollUp = MatrixRollUp::Numbers) {
    std::cout << "\n" << name << "\n";

    Canvas canvas(kWidth, kHeight);
    Check(canvas.ctx != nullptr, "an offscreen context is available");
    if (!canvas.ctx) return;

    auto diagram = CreateMatrixDiagramElement("matrix", 0, 0, kWidth, kHeight, model);
    diagram->SetEnableTooltips(false);   // no window to show one in
    diagram->SetHubMode(hub);
    diagram->SetTotals(MatrixTotals::Both);
    diagram->SetRollUp(rollUp);
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
    std::map<std::tuple<int, int, int>, double> sumX;
    int armHits = 0;
    for (int y = 0; y < kHeight; y += 2) {
        for (int x = 0; x < kWidth; x += 2) {
            diagram->HandleChartMouseMove(Point2Di(x, y));
            const MatrixRef ref = diagram->GetHoveredCell();
            if (ref.IsCell()) {
                ++hits[std::make_tuple(ref.panel, ref.row, ref.col)];
                sumX[std::make_tuple(ref.panel, ref.row, ref.col)] += x;
            } else if (ref.IsValid()) {
                ++armHits;
            }
        }
    }

    bool allReached = true;
    bool comparable = true;
    std::string spread;
    for (size_t p = 0; p < model.panels.size(); ++p) {
        const MatrixItemSet* rows = model.RowSet(static_cast<int>(p));
        const MatrixItemSet* cols = model.ColSet(static_cast<int>(p));
        if (!rows || !cols) continue;
        // An L shows panel 0 only.
        if (model.shape == MatrixShape::L && p > 0) continue;
        const bool triangle = model.panels[p].symmetric;
        int smallest = 1 << 30, largest = 0;
        for (int r = 0; r < rows->Count(); ++r) {
            // A symmetric panel has cells above its diagonal only.
            for (int c = triangle ? r + 1 : 0; c < cols->Count(); ++c) {
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
        // Within one panel the cells share a size, so their hit areas should
        // agree to within the sampling error of a 2px sweep. (Panels may
        // differ: a roof diamond is half the area of a body cell.)
        if (smallest == 0 || largest > smallest * 2) comparable = false;
        spread += (spread.empty() ? "" : ", ") + std::to_string(smallest) + "-" + std::to_string(largest);
    }
    Check(allReached, "every cell of every panel can be pointed at");
    Check(comparable, "within each panel the cells are hit over comparable areas ("
                      + spread + " samples)");

    // Nothing may land below a symmetric panel's diagonal.
    bool belowDiagonal = false;
    for (const auto& entry : hits) {
        const int p = std::get<0>(entry.first);
        if (p >= 0 && p < static_cast<int>(model.panels.size()) && model.panels[p].symmetric &&
            std::get<2>(entry.first) <= std::get<1>(entry.first)) {
            belowDiagonal = true;
        }
    }
    Check(!belowDiagonal, "no point lands on or below a symmetric panel's diagonal");

    // A House's roof cell (r, c) correlates columns r and c, so it must stand
    // centred between them: its mean x is the mean of the two columns'.
    if (model.shape == MatrixShape::House && model.panels.size() > 1) {
        const MatrixItemSet* cols = model.ColSet(0);
        std::vector<double> colX(static_cast<size_t>(cols->Count()), 0.0);
        std::vector<int> colN(colX.size(), 0);
        for (const auto& entry : hits) {
            if (std::get<0>(entry.first) != 0) continue;
            const size_t c = static_cast<size_t>(std::get<2>(entry.first));
            colX[c] += sumX[entry.first];
            colN[c] += entry.second;
        }
        double worst = 0.0;
        for (const auto& entry : hits) {
            if (std::get<0>(entry.first) != 1) continue;
            const size_t r = static_cast<size_t>(std::get<1>(entry.first));
            const size_t c = static_cast<size_t>(std::get<2>(entry.first));
            if (colN[r] == 0 || colN[c] == 0) continue;
            const double expected = (colX[r] / colN[r] + colX[c] / colN[c]) / 2.0;
            worst = std::max(worst, std::fabs(sumX[entry.first] / entry.second - expected));
        }
        Check(worst < 2.0, "every roof cell stands centred between its two columns (worst "
                           + std::to_string(worst) + " px off)");
    }
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

// Rotated column headers lean right from their column's centre, so the last
// one reaches past the grid. The layout must leave it room: nothing may be
// drawn on the content area's right edge, where the clip would cut it off.
void TestRotatedHeadersFit() {
    std::cout << "\nRotated headers stay inside the element\n";

    for (MatrixShape shape : {MatrixShape::L, MatrixShape::T}) {
        const MatrixModel model = (shape == MatrixShape::T)
                ? MatrixSamples::EmployeeAllocation() : MatrixSamples::ImprovementTools();
        for (MatrixHeaderFit fit : {MatrixHeaderFit::Rotated45, MatrixHeaderFit::Rotated90}) {
            Canvas canvas(kWidth, kHeight);
            if (!canvas.ctx) return;
            auto diagram = CreateMatrixDiagramElement("rot", 0, 0, kWidth, kHeight, model);
            diagram->SetEnableTooltips(false);
            diagram->SetHeaderFit(fit);
            diagram->SetShowLegend(false);
            diagram->Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));

            // The element keeps a 12 px margin; its content ends there.
            const int edge = kWidth - 12 - 1;
            cairo_surface_t* surface = cairo_get_target(canvas.cr);
            cairo_surface_flush(surface);
            const unsigned char* data = cairo_image_surface_get_data(surface);
            const int stride = cairo_image_surface_get_stride(surface);
            int ink = 0;
            for (int y = 0; y < kHeight; ++y) {
                const uint32_t px = reinterpret_cast<const uint32_t*>(data + y * stride)[edge];
                if ((px & 0x00FFFFFFu) != 0x00FCFCFDu) ++ink;
            }
            Check(ink == 0, std::string(shape == MatrixShape::T ? "T" : "L")
                            + (fit == MatrixHeaderFit::Rotated45 ? ", 45 degrees" : ", 90 degrees")
                            + ": nothing reaches the clipped edge (" + std::to_string(ink)
                            + " pixels did)");
            canvas.Save(std::string("matrix-rotated-") + (shape == MatrixShape::T ? "T" : "L")
                        + (fit == MatrixHeaderFit::Rotated45 ? "45" : "90"));
        }
    }
}

// Switching the theme changes colours, and only colours: a layout or
// behaviour setting made before must survive a round trip through dark.
void TestThemeKeepsSettings() {
    std::cout << "\nTheme switch keeps non-colour settings\n";

    auto diagram = CreateMatrixDiagramElement("theme", 0, 0, 400, 300);
    MatrixDiagramStyle style = diagram->GetStyle();
    const MatrixDiagramStyle defaults = style;
    style.tintCellsByLevel = true;
    style.alternateRowBands = true;
    style.markSize = 0.6;
    style.minCellSize = 22.0;
    diagram->SetStyle(style);

    diagram->SetDarkTheme(true);
    Check(diagram->GetStyle().tintCellsByLevel && diagram->GetStyle().alternateRowBands,
          "going dark keeps the switches");
    Check(diagram->GetStyle().cellBackground != defaults.cellBackground,
          "going dark changes the colours");

    diagram->SetDarkTheme(false);
    const MatrixDiagramStyle& back = diagram->GetStyle();
    Check(back.tintCellsByLevel && back.alternateRowBands,
          "coming back keeps tintCellsByLevel and alternateRowBands");
    Check(back.markSize == 0.6 && back.minCellSize == 22.0,
          "coming back keeps the sizes");
    Check(back.cellBackground == defaults.cellBackground &&
          back.labelColor == defaults.labelColor &&
          back.hubBackground == defaults.hubBackground,
          "coming back restores the light colours");
}

// ---- Interaction helpers ----

UCEvent MouseEvent(UCEventType type, const Point2Di& at,
                   UCMouseButton button = UCMouseButton::Left, int wheel = 0) {
    UCEvent e;
    e.type = type;
    e.pointer = at;
    e.button = button;
    e.wheelDelta = wheel;
    return e;
}

UCEvent KeyEvent(UCKeys key, bool shift = false, bool ctrl = false) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    e.shift = shift;
    e.ctrl = ctrl;
    return e;
}

MatrixRef HoverAt(UltraCanvasMatrixDiagram& diagram, const Point2Di& at) {
    diagram.HandleChartMouseMove(at);
    return diagram.GetHoveredCell();
}

// The centre of a cell, found by sweeping: the mean of the points that hit it.
bool FindCellCentre(UltraCanvasMatrixDiagram& diagram, const MatrixRef& want, int w, int h,
                    Point2Di& centre, int& widthOut) {
    long sx = 0, sy = 0, n = 0;
    int minX = 1 << 30, maxX = -1;
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            if (HoverAt(diagram, Point2Di(x, y)) == want) {
                sx += x; sy += y; ++n;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
            }
        }
    }
    if (n == 0) return false;
    centre = Point2Di(static_cast<int>(sx / n), static_cast<int>(sy / n));
    widthOut = maxX - minX;
    return true;
}

MatrixRef Ref(int panel, int row, int col) {
    MatrixRef r;
    r.panel = panel;
    r.row = row;
    r.col = col;
    return r;
}

std::shared_ptr<UltraCanvasMatrixDiagram> LaidOut(const MatrixModel& model, Canvas& canvas) {
    auto diagram = CreateMatrixDiagramElement("interact", 0, 0, kWidth, kHeight, model);
    diagram->SetEnableTooltips(false);
    if (canvas.ctx) diagram->Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
    return diagram;
}

void TestPanZoom() {
    std::cout << "\nPan and zoom\n";
    SetSmoothScrollingEnabled(false);   // zoom in one step, not a glide

    Canvas canvas(kWidth, kHeight);
    if (!canvas.ctx) return;
    auto diagram = LaidOut(MatrixSamples::ImprovementTools(), canvas);
    const MatrixRef target = Ref(0, 4, 3);

    Point2Di at;
    int cellW = 0;
    Check(FindCellCentre(*diagram, target, kWidth, kHeight, at, cellW), "the target cell is on screen");

    diagram->OnEvent(MouseEvent(UCEventType::MouseWheel, at, UCMouseButton::NoneButton, 1));
    Check(diagram->GetViewZoom() == 1.0, "the wheel does nothing while pan and zoom is off");

    diagram->SetPanZoomEnabled(true);
    for (int i = 0; i < 4; ++i) {
        diagram->OnEvent(MouseEvent(UCEventType::MouseWheel, at, UCMouseButton::NoneButton, 1));
    }
    Check(diagram->GetViewZoom() > 1.3, "the wheel zooms in");
    Check(HoverAt(*diagram, at) == target, "the cell under the pointer stays under it");
    const Point2Di sideways(at.x + static_cast<int>(cellW * 0.7), at.y);
    Check(HoverAt(*diagram, sideways) == target,
          "and has grown: a point that was in the next column is still in it");

    // A middle-button drag pans by exactly the drag.
    const Point2Di moved(at.x + 40, at.y + 25);
    diagram->OnEvent(MouseEvent(UCEventType::MouseDown, at, UCMouseButton::Middle));
    diagram->OnEvent(MouseEvent(UCEventType::MouseMove, moved, UCMouseButton::Middle));
    diagram->OnEvent(MouseEvent(UCEventType::MouseUp, moved, UCMouseButton::Middle));
    Check(HoverAt(*diagram, moved) == target, "a drag carries the cell with the pointer");

    diagram->OnEvent(KeyEvent(UCKeys::Key0, false, true));
    Check(diagram->GetViewZoom() == 1.0 && HoverAt(*diagram, at) == target,
          "Ctrl+0 puts the diagram back where it was laid out");
    diagram->OnEvent(KeyEvent(UCKeys::Minus, false, true));
    Check(diagram->GetViewZoom() < 1.0, "Ctrl+- zooms out");

    // Pointing outside the matrix area never hits a cell, however far the
    // zoom has pushed cells under the title.
    diagram->ResetView();
    for (int i = 0; i < 8; ++i) diagram->ZoomViewIn();
    Check(!HoverAt(*diagram, Point2Di(kWidth / 2, 4)).IsValid(),
          "a point on the title is not a cell, even zoomed in");

    diagram->SetPanZoomEnabled(false);
    Check(diagram->GetViewZoom() == 1.0, "turning pan and zoom off resets the view");
    SetSmoothScrollingEnabled(true);
}

void TestKeyboard() {
    std::cout << "\nKeyboard navigation\n";
    Canvas canvas(kWidth, kHeight);
    if (!canvas.ctx) return;

    int selections = 0;
    auto l = LaidOut(MatrixSamples::ImprovementTools(), canvas);
    l->onSelectionChanged = [&selections](const MatrixRef&) { ++selections; };
    l->OnEvent(KeyEvent(UCKeys::Right));
    Check(l->GetSelectedCell() == Ref(0, 0, 0), "the first arrow selects the first cell");
    l->OnEvent(KeyEvent(UCKeys::Right));
    l->OnEvent(KeyEvent(UCKeys::Down));
    Check(l->GetSelectedCell() == Ref(0, 1, 1), "Right then Down moves a column and a row");
    l->OnEvent(KeyEvent(UCKeys::Left));
    l->OnEvent(KeyEvent(UCKeys::Left));
    Check(l->GetSelectedCell() == Ref(0, 1, 0), "Left stops at the panel's edge");
    Check(selections == 4, "every move reports the new selection, the blocked one does not");
    l->OnEvent(KeyEvent(UCKeys::Escape));
    Check(!l->GetSelectedCell().IsValid(), "Escape clears the selection");

    // The X sample's first panel is turned: its model rows (suppliers) run
    // across the screen, so Right steps the ROW and Down the column.
    auto x = LaidOut(MatrixSamples::SupplyChain(), canvas);
    x->SelectCell(Ref(0, 0, 0));
    x->OnEvent(KeyEvent(UCKeys::Right));
    Check(x->GetSelectedCell() == Ref(0, 1, 0), "on a turned X panel Right moves along the model row");
    x->OnEvent(KeyEvent(UCKeys::Down));
    Check(x->GetSelectedCell() == Ref(0, 1, 1), "and Down along the model column");
    x->OnEvent(KeyEvent(UCKeys::Tab));
    Check(x->GetSelectedCell().panel == 1, "Tab moves to the next panel");
    x->OnEvent(KeyEvent(UCKeys::Tab, true));
    Check(x->GetSelectedCell().panel == 0, "Shift+Tab back");

    // The House's roof: Tab to it lands on its first cell, (0, 1). Right can
    // only go up-right, to (0, 2): down-right, (1, 1), is the diagonal. Below
    // (0, 2) lie two diamonds, down-left (0, 1) and down-right (1, 2), equally
    // "down"; the tie goes to the row step, so Down lands on (1, 2) - every
    // time. Down again from there stays on the roof's lowest row: (1, 2) has
    // no diamond below it but (1, 1), which does not exist, and (0, 2).
    auto house = LaidOut(MatrixSamples::HouseOfQuality(), canvas);
    house->SelectCell(Ref(0, 0, 0));
    house->OnEvent(KeyEvent(UCKeys::Tab));
    Check(house->GetSelectedCell() == Ref(1, 0, 1), "Tab reaches the roof's first cell");
    house->OnEvent(KeyEvent(UCKeys::Right));
    Check(house->GetSelectedCell() == Ref(1, 0, 2), "Right on the roof goes to the next diamond");
    house->OnEvent(KeyEvent(UCKeys::Down));
    Check(house->GetSelectedCell() == Ref(1, 1, 2), "Down on a tie takes the row step, the same way every time");
    for (int i = 0; i < 4; ++i) house->OnEvent(KeyEvent(UCKeys::Down));
    const MatrixRef low = house->GetSelectedCell();
    Check(low.panel == 1 && low.col == low.row + 1,
          "repeated Down settles on the roof's lowest row and never leaves the roof");
}

void TestEditing() {
    std::cout << "\nEditing\n";
    Canvas canvas(kWidth, kHeight);
    if (!canvas.ctx) return;
    auto d = LaidOut(MatrixSamples::ImprovementTools(), canvas);
    std::vector<std::string> edits;
    d->onCellEdited = [&edits](const MatrixRef&, const std::string& level) { edits.push_back(level); };

    const MatrixRef empty = Ref(0, 1, 1);   // Finance x SPC has no mark
    Check(d->GetModel().LevelAt(0, 1, 1).empty(), "the cell starts empty");
    d->SelectCell(empty);
    d->OnEvent(KeyEvent(UCKeys::Space));
    Check(d->GetModel().LevelAt(0, 1, 1).empty(), "Space edits nothing while editing is off");

    d->SetEditable(true);
    d->OnEvent(KeyEvent(UCKeys::Space));
    Check(d->GetModel().LevelAt(0, 1, 1) == "strong", "Space sets the strongest level first");
    d->OnEvent(KeyEvent(UCKeys::Space));
    Check(d->GetModel().LevelAt(0, 1, 1) == "medium", "then the next");
    d->OnEvent(KeyEvent(UCKeys::Space, true));
    Check(d->GetModel().LevelAt(0, 1, 1) == "strong", "Shift+Space steps back");
    d->OnEvent(KeyEvent(UCKeys::Delete));
    Check(d->GetModel().LevelAt(0, 1, 1).empty(), "Delete clears it");
    Check(edits.size() == 4 && edits.back().empty(), "every edit is reported, a clear as empty");

    // Past the weakest level the cell empties again, then starts over.
    for (int i = 0; i < 3; ++i) d->CycleCell(empty);
    Check(d->GetModel().LevelAt(0, 1, 1) == "weak", "three steps reach the weakest level");
    d->CycleCell(empty);
    Check(d->GetModel().LevelAt(0, 1, 1).empty(), "one more empties it");

    // A click on a cell edits it too, and keeps it selected.
    Point2Di at;
    int w = 0;
    if (FindCellCentre(*d, empty, kWidth, kHeight, at, w)) {
        d->OnEvent(MouseEvent(UCEventType::MouseDown, at));
        d->OnEvent(MouseEvent(UCEventType::MouseUp, at));
        Check(d->GetModel().LevelAt(0, 1, 1) == "strong" && d->GetSelectedCell() == empty,
              "a click sets the level and keeps the cell selected");
        Check(NearlyEqualScore(d->GetModel().RowScore(0, 1), 9.0 + 5.0),
              "and the totals follow the edit");
    }

    // The House roof edits like any panel, on its own scale.
    auto house = LaidOut(MatrixSamples::HouseOfQuality(), canvas);
    house->SetEditable(true);
    house->CycleCell(Ref(1, 0, 6));
    Check(house->GetModel().LevelAt(1, 6, 0) == "strongpositive",
          "a roof cell steps through the correlation scale, read either way round");
}

// The House's competitive block stands right of the grid. Pointing into it
// at a need's height reports that need, as its row label does, and hiding
// it gives the grid the width back.
void TestCompetitive() {
    std::cout << "\nCompetitive assessment\n";
    Canvas canvas(kWidth, kHeight);
    if (!canvas.ctx) return;
    auto house = LaidOut(MatrixSamples::HouseOfQuality(), canvas);
    const int lastCol = house->GetModel().ColSet(0)->Count() - 1;

    Point2Di centre;
    int cellW = 0;
    Check(FindCellCentre(*house, Ref(0, 2, lastCol), kWidth, kHeight, centre, cellW),
          "the last column of the 'Safe in traffic' row is on screen");

    int blockX = -1;
    for (int x = centre.x + cellW; x < kWidth; ++x) {
        const MatrixRef ref = HoverAt(*house, Point2Di(x, centre.y));
        if (ref.panel == 0 && ref.row == 2 && ref.col < 0) { blockX = x; break; }
    }
    Check(blockX >= centre.x + cellW, "right of the grid, the block reports the row's need");

    house->SetShowCompetitiveAssessment(false);
    house->Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
    Point2Di wider;
    int widerW = 0;
    FindCellCentre(*house, Ref(0, 2, lastCol), kWidth, kHeight, wider, widerW);
    Check(widerW > cellW, "hidden, it gives its width back to the cells");
    bool stillHit = false;
    for (int x = blockX; x < kWidth; x += 2) {
        const MatrixRef ref = HoverAt(*house, Point2Di(x, wider.y));
        if (ref.panel == 0 && ref.row == 2 && ref.col < 0 && x > wider.x + widerW * 2) stillHit = true;
    }
    Check(!stillHit, "and nothing right of the grid answers for it any more");

    // A House with no competitors draws no block at all.
    MatrixModel plain = MatrixSamples::HouseOfQuality();
    plain.competitors.clear();
    plain.ratings.clear();
    auto bare = LaidOut(plain, canvas);
    Point2Di bareCentre;
    int bareW = 0;
    FindCellCentre(*bare, Ref(0, 2, lastCol), kWidth, kHeight, bareCentre, bareW);
    Check(bareW > cellW, "a House without competitors keeps the whole width for its cells");
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
    CheckShape("House", MatrixSamples::HouseOfQuality(), MatrixHubMode::Blank, false,
               MatrixRollUp::Bars);
    CheckShape("L-bars", MatrixSamples::QualityFunctionDeployment(), MatrixHubMode::Blank,
               false, MatrixRollUp::Bars);
    TestTransposedXPanel();
    TestRotatedHeadersFit();
    TestPanZoom();
    TestKeyboard();
    TestEditing();
    TestCompetitive();
    TestThemeKeepsSettings();

    std::cout << "\n================================\n";
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED.\n";
    return 1;
}
