// Tests/EngineLineChartTest.cpp
// UltraCanvasEngineLineChart drawn offscreen.
//
// Covers what the engine line chart gained for control charts, each a general
// feature: a line shape, markers, a legend entry and interactivity per
// series; a look and a tooltip note per point; stacked panes, each with its
// own value axis, grid and limiters. Then ShowControlChart, which builds an
// X-bar-R or I-MR chart from a ControlResult with those parts. A chart that
// uses none of them must lay out as before.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, each case is also written there as a
// PNG for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineLineChart.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <algorithm>
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

    Color PixelAt(const Point2Dd& at) const {
        if (!cr) return Color(0, 0, 0, 0);
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const int x = static_cast<int>(std::lround(at.x)), y = static_cast<int>(std::lround(at.y));
        if (x < 0 || y < 0 || x >= cairo_image_surface_get_width(s) || y >= cairo_image_surface_get_height(s)) {
            return Color(0, 0, 0, 0);
        }
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

bool Close(const Color& a, const Color& b, int tolerance = 40) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

const int W = 720, H = 480;
const Color kBlue(30, 90, 200, 255);
const Color kRed(210, 30, 30, 255);

std::shared_ptr<UltraCanvasEngineLineChart> NewChart() {
    auto chart = CreateEngineLineChart("line", 0, 0, W, H);
    chart->SetEnableTooltips(false);          // no window to show one in
    chart->SetAnimateOnDataChange(false);     // one frame: the lines in place
    return chart;
}

void Draw(Canvas& canvas, UltraCanvasEngineLineChart& chart) {
    chart.Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
}

// Where point i of series s is drawn, from the frame's own axes.
Point2Dd At(const UltraCanvasEngineLineChart& chart, size_t s, size_t i) {
    const ChartAxisSet& axes = chart.Axes();
    const ChartAxis& domain = axes.At(1);
    const ChartAxis& value =
        axes.At(UltraCanvasEngineLineChart::ValueAxisOfPane(chart.PaneOfSeries(s)));
    const double v = chart.GetSeries()[s].values[i];
    return chart.Frame().projection->ToScreen(
        ChartNormalizedPoint(domain.Normalize(static_cast<double>(i)), value.Normalize(v)));
}

EngineLineSeries Series(const std::string& name, const std::vector<double>& values,
                        const Color& color = kBlue) {
    EngineLineSeries s;
    s.name = name;
    s.values = values;
    s.color = color;
    return s;
}

void TestUnchanged() {
    std::cout << "\nA chart that uses none of the new features\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    auto chart = NewChart();
    chart->SetCategories({"Jan", "Feb", "Mar", "Apr"});
    chart->SetSeries({Series("North", {3, 5, 4, 6}), Series("South", {2, 4, 3, 5}, kRed)});
    Draw(canvas, *chart);
    Check(chart->Axes().Count() == 2, "one value axis and the domain axis, as before");
    Check(chart->Axes().At(0).bandStart == 0.0 && chart->Axes().At(0).bandEnd == 1.0,
          "the value axis spans the whole plot");
    Check(chart->GetLegend().GetEntryCount() == 2, "both series in the legend");
    Check(chart->HitRegionAt(At(*chart, 0, 1)) >= 0, "points are hit regions");
    canvas.Save("engine_line_unchanged");
}

void TestShapePerSeries() {
    std::cout << "\nLine shape per series\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    auto chart = NewChart();
    chart->SetCategories({"a", "b"});
    EngineLineSeries step = Series("step", {2, 8});
    step.shape = ChartLineShape::StepMiddle;
    step.showMarkers = false;
    chart->SetSeries({step});
    Draw(canvas, *chart);
    const Point2Dd p0 = At(*chart, 0, 0), p1 = At(*chart, 0, 1);
    // Before the middle a StepMiddle line holds the first value...
    const Point2Dd held(p0.x + (p1.x - p0.x) * 0.3, p0.y);
    Check(Close(canvas.PixelAt(held), kBlue), "a StepMiddle series holds its value up to the middle");
    // ...where a straight line would already be well on its way up.
    const Point2Dd straight(p0.x + (p1.x - p0.x) * 0.3, p0.y + (p1.y - p0.y) * 0.3);
    Check(!Close(canvas.PixelAt(straight), kBlue), "and does not run straight to the next point");
    canvas.Save("engine_line_step_series");

    Canvas plain(W, H);
    auto other = NewChart();
    other->SetCategories({"a", "b"});
    other->SetSeries({Series("straight", {2, 8})});
    Draw(plain, *other);
    Check(Close(plain.PixelAt(straight), kBlue), "the chart's own shape (straight) is unchanged for other series");
}

void TestMarkersLegendInteractivity() {
    std::cout << "\nMarkers, legend entry and interactivity per series\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    auto chart = NewChart();
    chart->SetCategories({"a", "b", "c", "d", "e"});
    const double gap = std::nan("");
    EngineLineSeries data = Series("data", {gap, 5.3, gap, gap, gap});
    EngineLineSeries quiet = Series("quiet", {gap, gap, gap, 4.1, gap}, kRed);
    quiet.showMarkers = false;
    EngineLineSeries reference = Series("limit", {6, 6, 6, 6, 6}, Color(90, 90, 90, 255));
    reference.inLegend = false;
    reference.interactive = false;
    reference.showMarkers = false;
    chart->SetSeries({data, quiet, reference});
    Draw(canvas, *chart);

    Check(Close(canvas.PixelAt(At(*chart, 0, 1)), kBlue), "a lone point is drawn as its marker");
    Check(!Close(canvas.PixelAt(At(*chart, 1, 3)), kRed), "a series with markers off draws no lone point");
    Check(chart->GetLegend().GetEntryCount() == 2, "a series with inLegend = false has no legend entry");
    Check(chart->HitRegionAt(At(*chart, 2, 2)) < 0, "a non-interactive series has no hit regions");
    Check(chart->HitRegionAt(At(*chart, 0, 1)) >= 0, "the data series keeps its own");

    // The legend's entries map to the series that have one: hiding entry 1
    // hides "quiet" (series 1), not the reference line.
    chart->OnLegendEntryToggled(1, false);
    Canvas after(W, H);
    Draw(after, *chart);
    Check(Close(after.PixelAt(At(*chart, 2, 2)), Color(90, 90, 90, 255)),
          "toggling a legend entry leaves the series without one alone");
    canvas.Save("engine_line_markers_legend");
}

void TestPointStylesAndNotes() {
    std::cout << "\nPoint styles and notes\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    auto chart = NewChart();
    chart->SetCategories({"a", "b", "c", "d", "e"});
    EngineLineSeries s = Series("values", {3, 4, 5, 4, 3});
    s.showMarkers = false;
    s.lineWidth = 1.0f;
    s.pointStyles.resize(5);
    s.pointStyles[1].color = kRed;
    s.pointStyles[1].marker = ChartMarkerShape::Diamond;
    s.pointStyles[1].radiusScale = 2.0f;
    s.pointStyles[1].alwaysShown = true;
    s.pointStyles[3].color = kRed;
    s.pointStyles[3].hollow = true;
    s.pointStyles[3].radiusScale = 2.5f;
    s.pointStyles[3].alwaysShown = true;
    s.pointNotes = {"", "Rule 1: beyond the limits", "", "", ""};
    chart->SetSeries({s});
    Draw(canvas, *chart);

    const Point2Dd flagged = At(*chart, 0, 1), hollow = At(*chart, 0, 3);
    Check(Close(canvas.PixelAt(flagged), kRed), "a styled point is drawn in its own colour though markers are off");
    Check(Close(canvas.PixelAt(Point2Dd(flagged.x, flagged.y - 6.0)), kRed),
          "at its own size (a diamond twice the radius)");
    Check(!Close(canvas.PixelAt(hollow), kRed), "a hollow point is not filled");
    Check(Close(canvas.PixelAt(Point2Dd(hollow.x, hollow.y - 10.0)), kRed),
          "but has its outline at its radius");
    Check(!Close(canvas.PixelAt(At(*chart, 0, 2)), kRed), "an unstyled point keeps the series' look");

    std::string tip;
    chart->HitRegionAt(flagged, &tip);
    Check(tip.find("Rule 1: beyond the limits") != std::string::npos && tip.find("values") == 0,
          "a point's note is appended to its tooltip");
    chart->HitRegionAt(At(*chart, 0, 2), &tip);
    Check(tip.find("Rule") == std::string::npos, "a point without a note gets none");
    canvas.Save("engine_line_point_styles");
}

void TestPanes() {
    std::cout << "\nStacked panes\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    auto chart = NewChart();
    chart->SetCategories({"1", "2", "3", "4", "5", "6"});
    EngineLineSeries top = Series("Mean", {10.0, 10.2, 9.9, 10.1, 10.3, 10.0});
    EngineLineSeries bottom = Series("Range", {0.4, 0.6, 0.3, 0.5, 0.7, 0.4}, kRed);
    bottom.pane = 1;
    chart->SetSeries({top, bottom});
    chart->SetPanes({{1.0, "Mean", false}, {0.5, "Range", true}});
    ChartLimiter limit;
    limit.axisIndex = UltraCanvasEngineLineChart::ValueAxisOfPane(1);
    limit.value = 0.8;
    limit.caption = "UCL";
    chart->AddLimiter(limit);
    Draw(canvas, *chart);

    Check(chart->Axes().Count() == 3, "a second pane adds a third axis");
    const ChartAxis& a0 = chart->Axes().At(0);
    const ChartAxis& a2 = chart->Axes().At(2);
    Check(a0.bandEnd == 1.0 && a2.bandStart == 0.0 && a2.bandEnd < a0.bandStart,
          "pane 0 at the top, pane 1 below it, with a gap between");
    Check(std::abs((a0.bandEnd - a0.bandStart) / (a2.bandEnd - a2.bandStart) - 2.0) < 1e-9,
          "pane heights follow their weights (1 : 0.5)");
    Check(a0.title == "Mean" && a2.title == "Range", "each pane has its own value title");
    Check(a2.Normalize(0.0) >= a2.bandStart - 1e-9 && a2.Normalize(0.0) <= a2.bandStart + 0.02,
          "includeZero keeps zero at the foot of pane 1's axis");

    const Point2Dd topPoint = At(*chart, 0, 4), bottomPoint = At(*chart, 1, 4);
    const double boundary = chart->Frame().projection->ToScreen(ChartNormalizedPoint(0.5, a0.bandStart)).y;
    Check(topPoint.y < boundary && bottomPoint.y > boundary, "each series is drawn in its own pane");
    Check(Close(canvas.PixelAt(bottomPoint), kRed) && Close(canvas.PixelAt(topPoint), kBlue),
          "and its points land where its pane's axis says");
    std::string tip;
    chart->HitRegionAt(bottomPoint, &tip);
    Check(tip.find("Range") == 0 && tip.find("0.7") != std::string::npos,
          "the tooltip reads the value through the pane's axis");
    const double limitY = chart->Frame().projection->ToScreen(
        ChartNormalizedPoint(0.5, a2.Normalize(0.8))).y;
    Check(limitY > boundary, "a limiter on ValueAxisOfPane(1) lands in pane 1");
    Check(chart->PaneOfSeries(1) == 1, "PaneOfSeries reports the pane");
    canvas.Save("engine_line_panes");

    chart->SetMode(EngineLineChartMode::StackedArea);
    Canvas stacked(W, H);
    Draw(stacked, *chart);
    Check(chart->Axes().Count() == 2 && chart->PaneOfSeries(1) == 0,
          "the stacked modes draw every series in pane 0");
}

std::vector<ControlSubgroup> ShiftedProcess() {
    // 24 subgroups of five, the mean stepping up after subgroup 16.
    std::vector<ControlSubgroup> data;
    const double pattern[5] = {-0.02, 0.01, 0.0, -0.01, 0.02};
    for (int i = 0; i < 24; ++i) {
        ControlSubgroup g;
        g.label = std::to_string(i + 1);
        const double mean = (i < 16) ? 10.0 + 0.004 * ((i * 7) % 5 - 2) : 10.03;
        for (int k = 0; k < 5; ++k) g.values.push_back(mean + pattern[(k + i) % 5]);
        data.push_back(g);
    }
    return data;
}

void TestShowControlChart() {
    std::cout << "\nShowControlChart\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    const ControlResult result = ComputeControlChart(ShiftedProcess());
    Check(result.HasSignal(), "(the shifted process has signals)");
    auto chart = NewChart();
    ControlChartStyle style;
    style.flagColor = kRed;
    ShowControlChart(*chart, result, style);
    Draw(canvas, *chart);

    Check(chart->Axes().Count() == 3 && chart->GetPanes().size() == 2, "X-bar-R: two panes");
    Check(chart->Axes().At(0).title == "Subgroup mean" && chart->Axes().At(2).title == "Range",
          "titled by their statistics");
    // X-bar: UCL, CL, LCL; R: UCL, CL (its lower limit is clamped at zero for n = 5).
    const std::vector<ChartLimiter>& limiters = chart->GetLimiters();
    Check(limiters.size() == 5, "five limit lines: no LCL on the R chart for n = 5");
    size_t onRange = 0;
    for (const ChartLimiter& l : limiters) onRange += (l.axisIndex == 2) ? 1 : 0;
    Check(onRange == 2, "two of them in the range pane");
    Check(limiters[0].caption.rfind("UCL = 10.0", 0) == 0, "captions read like \"UCL = 10.0...\"");
    bool beyond = false, inside = false;
    for (const PlacedChartLabel& label : chart->GetLabelPlan().labels) {
        if (label.text.rfind("UCL = ", 0) != 0 && label.text.rfind("CL = ", 0) != 0) continue;
        if (label.bounds.x >= chart->Frame().plotArea.Right()) beyond = true;
        else inside = true;
    }
    Check(beyond && !inside, "the limit values sit in a column past the plot's right edge");
    Check(chart->GetLegend().GetEntryCount() == 2 && chart->GetHighlights().empty(),
          "no zones unless asked");

    size_t flagged = 0;
    for (size_t i = 0; i < result.location.value.size(); ++i) {
        if (result.RulesAt(i).empty()) continue;
        ++flagged;
        std::string tip;
        chart->HitRegionAt(At(*chart, 0, i), &tip);
        if (tip.find("Rule ") == std::string::npos) Check(false, "flagged point " + std::to_string(i) + " names its rule");
    }
    Check(flagged > 0, "the shift is flagged");
    size_t firstFlag = 0;
    while (result.RulesAt(firstFlag).empty()) ++firstFlag;
    Check(Close(canvas.PixelAt(At(*chart, 0, firstFlag)), kRed), "a flagged point is drawn in the flag colour");
    std::string tip;
    chart->HitRegionAt(At(*chart, 0, firstFlag), &tip);
    Check(tip.find("Rule ") != std::string::npos, "and its tooltip names the rule");
    canvas.Save("engine_line_control_xbar_r");

    ControlChartStyle full = style;
    full.showZones = true;
    full.showSigmaLines = true;
    ShowControlChart(*chart, result, full);
    Canvas zones(W, H);
    Draw(zones, *chart);
    Check(chart->GetHighlights().size() == 6, "zones: A, B and C either side of the centre");
    Check(chart->GetLimiters().size() == 9, "sigma lines add four limiters, without captions");
    zones.Save("engine_line_control_zones");

    ControlChartStyle single = style;
    single.showDispersion = false;
    ShowControlChart(*chart, result, single);
    Canvas one(W, H);
    Draw(one, *chart);
    Check(chart->Axes().Count() == 2 && chart->GetLimiters().size() == 3,
          "showDispersion = false: the means alone, with their three limits");
}

void TestIndividuals() {
    std::cout << "\nShowControlChart, I-MR\n";
    Canvas canvas(W, H);
    if (!canvas.ctx) return;
    std::vector<ControlSubgroup> data;
    const double values[] = {52, 49, 51, 50, 53, 48, 50, 51, 49, 52, 50, 51, 49, 50, 52, 61, 50, 49, 51, 50};
    for (double v : values) data.push_back({"", {v}});
    ControlOptions options;
    options.kind = ControlChartKind::IndividualsMR;
    const ControlResult result = ComputeControlChart(data, options);
    auto chart = NewChart();
    ShowControlChart(*chart, result);
    Draw(canvas, *chart);
    Check(chart->Axes().At(0).title == "Value" && chart->Axes().At(2).title == "Moving range",
          "I-MR: values over moving ranges");
    Check(!result.RulesAt(15).empty(), "(the outlier at point 16 breaks rule 1)");
    Check(chart->HitRegionAt(At(*chart, 1, 1)) >= 0, "the moving ranges are points to inspect too");
    canvas.Save("engine_line_control_imr");
}

} // namespace

int main() {
    std::cout << "UltraCanvas engine line chart\n";
    TestUnchanged();
    TestShapePerSeries();
    TestMarkersLegendInteractivity();
    TestPointStylesAndNotes();
    TestPanes();
    TestShowControlChart();
    TestIndividuals();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed." : "Some checks FAILED.") << "\n";
    return g_failures == 0 ? 0 : 1;
}
