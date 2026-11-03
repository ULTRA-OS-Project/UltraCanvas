// Tests/TernaryChartRenderTest.cpp
// The ternary chart drawn offscreen.
//
// The model has its own unit test (TernaryModelTest); this one covers what
// the chart adds. The triangle must be equilateral and inside the element.
// Every point must be drawn where its shares say and be hit there. The
// reading must be right: from any point, the grid line of each part leads to
// that part's tick on its labelled edge, in both reading directions. Clicks,
// the legend and tooltips must report the right point, and a point with no
// shares must be left out without harm.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, each case is also written there as a
// PNG for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasTernaryChart.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

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

bool Close(const Color& a, const Color& b, int tolerance = 12) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

UCEvent Mouse(UCEventType type, const Point2Dd& at) {
    UCEvent e;
    e.type = type;
    e.pointer = Point2Di(static_cast<int>(std::lround(at.x)), static_cast<int>(std::lround(at.y)));
    e.button = UCMouseButton::Left;
    return e;
}

double Distance(const Point2Dd& a, const Point2Dd& b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Whether the segment p -> q runs parallel to the segment r -> s.
bool Parallel(const Point2Dd& p, const Point2Dd& q, const Point2Dd& r, const Point2Dd& s) {
    const double ax = q.x - p.x, ay = q.y - p.y, bx = s.x - r.x, by = s.y - r.y;
    const double cross = ax * by - ay * bx;
    return std::abs(cross) <= 1e-6 * std::hypot(ax, ay) * std::hypot(bx, by) + 1e-9;
}

const int W = 760, H = 600;

std::shared_ptr<UltraCanvasTernaryChart> Make(Canvas& canvas, const TernaryModel& model) {
    auto chart = CreateTernaryChart("ternary", 0, 0, W, H);
    chart->SetEnableTooltips(false);          // no window to show one in
    chart->SetAnimateOnDataChange(false);     // one frame: the points in place
    chart->SetModel(model);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
    return chart;
}

void TestTriangleAndPoints() {
    std::cout << "\nTriangle and points\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    const TernaryModel model = TernarySamples::ThreePartyVotes();
    auto chart = Make(canvas, model);

    const Point2Dd a = chart->CornerPoint(0), b = chart->CornerPoint(1), c = chart->CornerPoint(2);
    const double side = Distance(b, c);
    Check(side > 300.0, "the triangle takes most of the element");
    Check(std::abs(Distance(a, b) - side) < 1e-6 && std::abs(Distance(a, c) - side) < 1e-6,
          "the triangle is equilateral");
    Check(a.y < b.y && b.x < c.x && a.x > 0 && c.x < W && b.y < H, "apex up, inside the element");

    // The plot area is the triangle: inside it the plot colour (between the
    // grid lines), outside it the background.
    const Color plot = chart->GetTheme().plotAreaColor;
    const Point2Dd centre((a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0);
    bool filled = false;
    for (int dy = -6; dy <= 6 && !filled; dy += 3) {
        filled |= Close(canvas.PixelAt(static_cast<int>(centre.x) + 2, static_cast<int>(centre.y) + dy), plot, 1);
    }
    const Color outside = canvas.PixelAt(static_cast<int>(b.x) + 4, static_cast<int>(a.y) + 4);
    Check(filled && !Close(outside, plot, 1), "the triangle, and only the triangle, is filled as the plot area");

    bool allPlaced = true, allHit = true;
    for (size_t s = 0; s < model.series.size(); ++s) {
        for (size_t p = 0; p < model.series[s].points.size(); ++p) {
            const TernaryPoint& point = model.series[s].points[p];
            const Point2Dd at = chart->PointPosition(s, p);
            double sa = 0, sb = 0, sc = 0, ra = 0, rb = 0, rc = 0;
            point.Shares(sa, sb, sc);
            allPlaced &= chart->ScreenToShares(at, ra, rb, rc) &&
                         std::abs(ra - sa) < 1e-9 && std::abs(rb - sb) < 1e-9 && std::abs(rc - sc) < 1e-9;
            chart->OnEvent(Mouse(UCEventType::MouseMove, at));
            const std::pair<size_t, size_t> hit = chart->GetHoveredPoint();
            // Overlapping markers: the later one wins, which is drawn on top.
            allHit &= hit.first != SIZE_MAX &&
                      Distance(chart->PointPosition(hit.first, hit.second), at) < 9.0;
        }
    }
    Check(allPlaced, "every point is drawn where its shares say, and reads back the same shares");
    Check(allHit, "every point answers the pointer where it is drawn");

    // A point's colour is its series'.
    const Point2Dd first = chart->PointPosition(0, 0);
    const Color marker = canvas.PixelAt(static_cast<int>(std::lround(first.x)), static_cast<int>(std::lround(first.y)));
    Check(Close(marker, chart->Palette().ColorAt(0, model.series.size()), 40),
          "a marker is drawn in its series' colour");

    // The corners and the centroid.
    Check(Distance(chart->SharesToScreen(1, 0, 0), a) < 1e-9 && Distance(chart->SharesToScreen(0, 0, 7), c) < 1e-9,
          "a pure part sits on its corner, however much of it there is");
    Check(Distance(chart->SharesToScreen(5, 5, 5), Point2Dd((a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3)) < 1e-9,
          "equal parts sit at the centroid");
    double ra = 0, rb = 0, rc = 0;
    Check(!chart->ScreenToShares(Point2Dd(b.x - 20, b.y + 20), ra, rb, rc), "a screen point outside the triangle has no shares");

    const std::string tip = chart->BuildPointTooltip(0, 0);
    Check(tip.find("Northfield") != std::string::npos && tip.find("Blue Party: 43.3") != std::string::npos &&
          tip.find("(41200)") != std::string::npos, "a tooltip gives each share and the raw votes");
    canvas.Save("ternary_votes");
}

void TestReading(TernaryReading reading, const std::string& name) {
    std::cout << "\nReading " << name << "\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    TernaryModel model = TernarySamples::SoilSamples();
    model.reading = reading;
    model.series[0].points.emplace_back(20, 30, 50, "Probe");   // 20 % A, 30 % B, 50 % C
    auto chart = Make(canvas, model);
    const Point2Dd a = chart->CornerPoint(0), b = chart->CornerPoint(1), c = chart->CornerPoint(2);
    const Point2Dd p = chart->SharesToScreen(20, 30, 50);

    // The grid line of each part runs parallel to the edge opposite its
    // corner, and the tick for that part's share sits on it.
    const Point2Dd ta = chart->TickPosition(0, 0.2), tb = chart->TickPosition(1, 0.3), tc = chart->TickPosition(2, 0.5);
    Check(Parallel(p, ta, b, c), "the A share is read along a line parallel to BC, to the A tick");
    Check(Parallel(p, tb, a, c), "the B share is read along a line parallel to AC, to the B tick");
    Check(Parallel(p, tc, a, b), "the C share is read along a line parallel to AB, to the C tick");

    // Which edge carries which labels.
    const bool clockwise = reading == TernaryReading::Clockwise;
    Check(Distance(chart->TickPosition(0, 0.0), clockwise ? b : c) < 1e-9 && Distance(chart->TickPosition(0, 1.0), a) < 1e-9,
          clockwise ? "clockwise: A reads up the left edge" : "anticlockwise: A reads up the right edge");
    Check(Distance(chart->TickPosition(1, 0.0), clockwise ? c : a) < 1e-9,
          clockwise ? "B reads right to left along the bottom" : "B reads down the left edge");
    Check(Distance(chart->TickPosition(2, 0.0), clockwise ? a : b) < 1e-9,
          clockwise ? "C reads down the right edge" : "C reads left to right along the bottom");

    // The tooltip of a lab result that sums to 100 gives shares, not raw values twice.
    const size_t probe = model.series[0].points.size() - 1;
    const std::string tip = chart->BuildPointTooltip(0, probe);
    Check(tip.find("Clay: 20.0") != std::string::npos && tip.find("(20)") == std::string::npos,
          "percentages are not repeated as raw amounts");
    canvas.Save(std::string("ternary_soil_") + name);
}

void TestInteraction() {
    std::cout << "\nInteraction\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    TernaryModel model = TernarySamples::ThreePartyVotes();
    model.series[1].points.emplace_back(0, 0, 0, "Empty region");   // no shares: left out
    auto chart = Make(canvas, model);
    Check(!model.Validate().empty(), "a point with no shares is named by Validate");
    Check(std::isnan(chart->PointPosition(1, model.series[1].points.size() - 1).x), "and is not drawn");

    size_t clickedSeries = SIZE_MAX, clickedPoint = SIZE_MAX;
    chart->onPointClick = [&](size_t s, size_t p) { clickedSeries = s; clickedPoint = p; };
    const Point2Dd at = chart->PointPosition(1, 4);
    chart->OnEvent(Mouse(UCEventType::MouseMove, at));
    chart->OnEvent(Mouse(UCEventType::MouseDown, at));
    Check(clickedSeries == 1 && clickedPoint == 4, "a click reports the point's series and index");

    chart->SetSeriesVisible(1, false);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
    Check(std::isnan(chart->PointPosition(1, 4).x), "a hidden series has no positions");
    chart->OnEvent(Mouse(UCEventType::MouseMove, at));
    const std::pair<size_t, size_t> hit = chart->GetHoveredPoint();
    Check(hit.first != 1, "and none of its points answers the pointer");
    chart->SetSeriesVisible(1, true);
    Check(chart->IsSeriesVisible(1), "a series can be shown again");

    chart->SetShowPointLabels(true);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
    size_t placed = 0;
    for (const PlacedChartLabel& label : chart->GetLabelPlan().labels) placed += label.suppressed ? 0 : 1;
    Check(placed > 0, "point labels go through the label plan");
    canvas.Save("ternary_votes_labels");
}

} // namespace

int main() {
    std::cout << "====================================\n";
    std::cout << "  Ternary Chart Render Test\n";
    std::cout << "====================================\n";

    TestTriangleAndPoints();
    TestReading(TernaryReading::Clockwise, "clockwise");
    TestReading(TernaryReading::Anticlockwise, "anticlockwise");
    TestInteraction();

    std::cout << "\n====================================\n";
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED.\n";
    return 1;
}
