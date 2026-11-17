// Tests/RidgelineChartRenderTest.cpp
// The ridgeline chart drawn offscreen: each row on its own baseline in its
// own colour, a lower row in front of the row behind it where they overlap,
// the tallest curve reaching the top of the plot, hit strips that name the
// row in front, the value column, the colourings and the hover callback.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, the cases are also written there as
// PNGs for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineChart.h"
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

const int W = 720, H = 480;

struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;
    Canvas() {
        ctx = CreateRenderContext(Size2Di(W, H), nullptr);
        if (ctx) cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }
    Color PixelAt(const Point2Dd& at) const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const int x = static_cast<int>(std::lround(at.x)), y = static_cast<int>(std::lround(at.y));
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

bool Reddish(const Color& c) { return c.r > c.g + 60 && c.r > c.b + 60; }
bool Greenish(const Color& c) { return c.g > c.r + 50 && c.g > c.b + 40; }
bool Bluish(const Color& c) { return c.b > c.r + 60 && c.b > c.g + 30; }

std::vector<double> Around(double centre, int n = 200) {
    std::vector<double> v;
    for (int i = 0; i < n; ++i) v.push_back(centre + 0.6 * std::sin(i * 0.7) + 0.3 * std::cos(i * 1.9));
    return v;
}

// Three rows: red around 2 at the top, green around 5, blue around 8.
UltraCanvasRidgelineModel ThreeRows() {
    UltraCanvasRidgelineModel m;
    m.title = "Three rows";
    m.rows.push_back({"Red", Around(2.0), {}, Color(220, 40, 40, 255), ""});
    m.rows.push_back({"Green", Around(5.0), {}, Color(40, 170, 60, 255), ""});
    m.rows.push_back({"Blue", Around(8.0), {}, Color(40, 80, 220, 255), "the last row"});
    m.xMin = 0.0;
    m.xMax = 10.0;
    return m;
}

std::shared_ptr<UltraCanvasRidgelineChart> Make(Canvas& canvas, const UltraCanvasRidgelineModel& model,
                                                double overlap = 1.5) {
    auto chart = CreateRidgelineChart("ridgeline", 0, 0, W, H);
    chart->SetEnableTooltips(false);
    chart->SetAnimateOnDataChange(false);
    chart->SetModel(model);
    chart->SetOverlap(overlap);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
    return chart;
}

// The screen point at x, `share` of the way up a row's curve from its baseline.
Point2Dd Inside(const UltraCanvasRidgelineChart& chart, size_t row, double x, double share = 0.5) {
    const ChartAxis& xAxis = chart.Axes().At(1);
    const double v = chart.RowBaseline(row) + chart.RowHeightAt(row, x) * share;
    return chart.Frame().projection->ToScreen(ChartNormalizedPoint(xAxis.Normalize(x), v));
}

void TestRows() {
    std::cout << "\nRows\n";
    Canvas canvas;
    if (!canvas.ctx) return;
    auto chart = Make(canvas, ThreeRows());
    Check(chart->RowBaseline(0) > chart->RowBaseline(1) && chart->RowBaseline(1) > chart->RowBaseline(2),
          "the first row is at the top");
    Check(Reddish(canvas.PixelAt(Inside(*chart, 0, 2.0))), "red's curve is red, around 2");
    Check(Greenish(canvas.PixelAt(Inside(*chart, 1, 5.0))), "green's around 5");
    Check(Bluish(canvas.PixelAt(Inside(*chart, 2, 8.0))), "blue's around 8");
    const Color above = canvas.PixelAt(Inside(*chart, 2, 2.0, 0.0) + Point2Dd(0.0, -2.0));
    Check(!Bluish(above) && !Greenish(above), "where blue has no values, its baseline shows nothing");

    double tallest = 0.0;
    for (size_t i = 0; i < 3; ++i) {
        for (double x = 0.0; x <= 10.0; x += 0.05) tallest = std::max(tallest, chart->RowBaseline(i) + chart->RowHeightAt(i, x));
    }
    Check(std::abs(tallest - 1.0) < 0.01, "the tallest curve just reaches the top of the plot");

    std::string tip;
    const int64_t id = chart->HitRegionAt(Inside(*chart, 2, 8.0), &tip);
    Check(id >= 0 && id / 64 == 2, "the hit strip under blue's peak is blue's");
    Check(tip.find("Blue") == 0 && tip.find("200 values, mean") != std::string::npos &&
              tip.find("the last row") != std::string::npos,
          "its tooltip names the row, its count and mean, and its note");
    Check(chart->HitRegionAt(Point2Dd(W - 2, 2)) < 0, "outside every curve: no row");
    canvas.Save("ridgeline_rows");
}

void TestOverlap() {
    std::cout << "\nOverlap\n";
    // Two rows on the same values: the lower one rises in front of the upper.
    UltraCanvasRidgelineModel m;
    m.rows.push_back({"Back", Around(5.0), {}, Color(220, 40, 40, 255), ""});
    m.rows.push_back({"Front", Around(5.0), {}, Color(40, 170, 60, 255), ""});
    m.xMin = 0.0;
    m.xMax = 10.0;
    Canvas canvas;
    if (!canvas.ctx) return;
    auto chart = Make(canvas, m, 3.0);
    // Halfway between the back row's baseline and the front row's peak:
    // inside both curves.
    const double v = (chart->RowBaseline(0) + chart->RowBaseline(1) + chart->RowHeightAt(1, 5.0)) * 0.5;
    const Point2Dd both = chart->Frame().projection->ToScreen(ChartNormalizedPoint(chart->Axes().At(1).Normalize(5.0), v));
    Check(v > chart->RowBaseline(0) && v < chart->RowBaseline(1) + chart->RowHeightAt(1, 5.0), "a point inside both curves");
    Check(Greenish(canvas.PixelAt(both)), "the front row covers the back row there");
    Check(chart->HitRegionAt(both) / 64 == 1, "and the pointer finds the front row");
    canvas.Save("ridgeline_overlap");
}

void TestValueColumnAndColours() {
    std::cout << "\nValue column and colourings\n";
    UltraCanvasRidgelineModel m = ThreeRows();
    for (RidgelineRow& row : m.rows) row.color = Color(0, 0, 0, 0);   // the chart's colouring
    Canvas canvas;
    if (!canvas.ctx) return;
    auto chart = CreateRidgelineChart("ridgeline", 0, 0, W, H);
    chart->SetEnableTooltips(false);
    chart->SetAnimateOnDataChange(false);
    chart->SetModel(m);
    chart->SetValueColumn("Average", RidgelineValue::Mean, 2);
    chart->SetColoring(RidgelineColoring::ByRowValue);
    chart->SetColormap(HeatmapColormap::RdYlGn);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
    char expected[32];
    std::snprintf(expected, sizeof(expected), "%.2f", m.Statistics(0).mean);   // locale-ok: test text
    Check(chart->RowValueText(0) == expected, "the value column shows each row's mean");
    Check(Reddish(chart->RowColor(0)) && Greenish(chart->RowColor(2)),
          "ByRowValue on red-yellow-green: the lowest mean red, the highest green");
    Check(chart->Frame().plotArea.x > 60.0, "room is left of the plot for the names and the values");
    canvas.Save("ridgeline_value_column");

    chart->SetColoring(RidgelineColoring::Alternating);
    Check(chart->RowColor(0).r != chart->RowColor(1).r || chart->RowColor(0).g != chart->RowColor(1).g,
          "Alternating: neighbouring rows differ");
    Check(chart->RowColor(0).r == chart->RowColor(2).r, "and every other row repeats");
    chart->SetColoring(RidgelineColoring::Single);
    chart->SetFillColor(Color(10, 20, 30, 255));
    Check(chart->RowColor(1).r == 10 && chart->RowColor(1).b == 30, "Single: the colour set");
}

void TestHover() {
    std::cout << "\nHover\n";
    Canvas canvas;
    if (!canvas.ctx) return;
    auto chart = Make(canvas, ThreeRows());
    int reported = -2;
    chart->onRowHover = [&reported](int row) { reported = row; };
    chart->OnHitRegionHoverChanged(1 * 64 + 32);
    Check(reported == 1 && chart->GetHoveredRow() == 1, "hovering a strip reports its row");
    Check(chart->GetHoveredX() > 5.0 && chart->GetHoveredX() < 5.2, "and the x of the strip");
    chart->OnHitRegionHoverChanged(-1);
    Check(reported == -1 && chart->GetHoveredRow() == -1, "leaving reports -1");
    Check(chart->DescribeRow(1, 5.0).find("Green\n5.00: Density ") == 0, "DescribeRow: name, x and height");
}

void TestSamples() {
    std::cout << "\nSamples\n";
    Canvas words;
    if (!words.ctx) return;
    auto chart = CreateRidgelineChart("words", 0, 0, W, H);
    chart->SetAnimateOnDataChange(false);
    chart->SetModel(UltraCanvasRidgelineModel::WordRatings());
    chart->SetOverlap(2.2);
    chart->SetValueColumn("Average score", RidgelineValue::Mean, 2);
    chart->SetColoring(RidgelineColoring::ByRowValue);
    chart->SetColormap(HeatmapColormap::RdYlGn);
    chart->Render(words.ctx.get(), Rect2Df(0, 0, W, H));
    Check(chart->GetModel().rows.size() == 33 && Reddish(chart->RowColor(0)) && Greenish(chart->RowColor(32)),
          "WordRatings draws 33 rows, red to green");
    words.Save("ridgeline_words");

    Canvas day;
    auto times = CreateRidgelineChart("day", 0, 0, W, H);
    times->SetAnimateOnDataChange(false);
    times->SetModel(UltraCanvasRidgelineModel::DailyActivities());
    times->SetColoring(RidgelineColoring::Alternating);
    times->SetOutline(Color(255, 255, 255, 255), 1.5f);
    times->SetXTicks({3, 6, 9, 12, 15, 18, 21, 24, 27});
    times->SetXFormatter([](double h) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%02d:00", static_cast<int>(std::lround(h)) % 24);   // locale-ok: test text
        return std::string(buffer);
    });
    times->Render(day.ctx.get(), Rect2Df(0, 0, W, H));
    Check(times->DescribeRow(0, 22.0).find("22:00: Participants") != std::string::npos, "times read as hh:00");
    day.Save("ridgeline_day");
}

} // namespace

int main() {
    std::cout << "UltraCanvas ridgeline chart\n";
    TestRows();
    TestOverlap();
    TestValueColumnAndColours();
    TestHover();
    TestSamples();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed." : "Some checks FAILED.") << "\n";
    return g_failures == 0 ? 0 : 1;
}
