// Tests/AxisDistributionRenderTest.cpp
// The chart engine's axis distribution layer (slot 550) drawn offscreen.
//
// The statistics have unit tests (ChartEngineTest); this one covers what the
// engine element adds: where the bands go. An Outside band must take its depth
// beyond the plot's opposite edge - the x distribution above the plot, the y
// distribution to its right - and the legend on that edge must move out by as
// much rather than be drawn over. The bars must line up with the axis, tallest
// where the values are. Near draws inside the plot without moving it; a band
// along an in-plot rule must stay clear of the neighbouring rule, and the
// outermost rules' bands must turn inward rather than run over the margin; under
// the polar projection there is no band at all. Every kind - histogram,
// density, box, strip, violin - paints its band where the values are.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, each case is also written there as a
// PNG for a person to look at.
//
// Version: 1.1.0 - the outermost in-plot rules: bands turn inward, never over the margin
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
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

const int kWidth = 800;
const int kHeight = 560;
const Color kBandFill(220, 30, 30, 255);       // opaque, so a pixel says whose it is
const Color kSubsetFill(30, 60, 220, 255);

struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;

    Canvas(int w, int h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!ctx) return;
        cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }

    Color PixelAt(double fx, double fy) const {
        const int x = static_cast<int>(std::lround(fx));
        const int y = static_cast<int>(std::lround(fy));
        if (!cr || x < 0 || y < 0 || x >= kWidth || y >= kHeight) return Color(0, 0, 0, 0);
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

// A scatter field: x along the bottom, y up the left, or - with inPlotAxes -
// two vertical in-plot rules like a parallel coordinate chart. The values
// cluster at x = 2.5 and y = 7.5 - mid-bin for one-unit bins - so the tallest
// bars are there and every other bar is short.
class FieldChart : public UltraCanvasChartEngineElement {
public:
    // inPlot: the y rule at firstRule, the x rule at lastRule (normalised).
    FieldChart(bool inPlot, double firstRule = 0.25, double lastRule = 0.75)
        : UltraCanvasChartEngineElement("field", 0, 0, kWidth, kHeight),
          inPlotAxes(inPlot), firstRulePosition(firstRule), lastRulePosition(lastRule) {
        SetEnableTooltips(false);           // no window to show one in
        SetAnimateOnDataChange(false);      // one frame: everything at full size
        for (int i = 0; i < 200; ++i) {
            const double spread = static_cast<double>(i % 20) / 20.0 - 0.5;   // -0.5 .. 0.45
            xs.push_back(i < 120 ? 2.5 + spread * 0.6 : 2.5 + spread * 14.0);
            ys.push_back(i < 120 ? 7.5 + spread * 0.6 : 5.5 + spread * 8.0);
        }
    }

    std::vector<double> xs, ys;
    bool inPlotAxes = false;
    double firstRulePosition = 0.25, lastRulePosition = 0.75;

    void DescribeAxes(ChartAxisSet& axes) override {
        ChartAxis y("y");
        y.side = ChartAxisSide::Left;
        y.SetRange(0.0, 10.0);
        y.Observe(ys);
        ChartAxis x("x");
        x.side = ChartAxisSide::Bottom;
        x.SetRange(-6.0, 10.0);
        x.Observe(xs);
        if (inPlotAxes) {
            y.inPlot = true;
            y.plotPosition = firstRulePosition;
            x.inPlot = true;
            x.plotPosition = lastRulePosition;
            x.SetRange(0.0, 10.0);
        }
        axes.Add(y);
        axes.Add(x);
    }

    void RenderChartContent(IRenderContext*, const ChartEngineFrame&) override {}

    Rect2Dd LegendBox() const { return LegendRect(); }
};

void CheckJointPlot() {
    std::cout << "\nOutside bands: the joint plot\n";
    Canvas bare(kWidth, kHeight), canvas(kWidth, kHeight);
    if (!canvas.ctx || !bare.ctx) { Check(false, "an offscreen context is available"); return; }

    // The same chart without distributions, for the plot it would have had.
    FieldChart plain(false);
    plain.SetShowLegend(true);
    plain.SetLegendEntries({ChartLegendEntry("field", Color(90, 90, 90, 255))});
    plain.Render(bare.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
    const Rect2Dd before = plain.Frame().plotArea;
    const Rect2Dd legendBefore = plain.LegendBox();

    FieldChart chart(false);
    chart.SetShowLegend(true);
    chart.SetLegendEntries({ChartLegendEntry("field", Color(90, 90, 90, 255))});
    AxisDistribution top;                    // the x axis's: above the plot
    top.axisIndex = 1;
    top.kind = AxisDistributionKind::Histogram;
    top.binCount = 16;
    top.thickness = 50.0;
    top.gap = 4.0;
    top.fill = kBandFill;
    top.strokeWidth = 0.0f;
    chart.AddAxisDistribution(top);
    AxisDistribution right = top;            // the y axis's: right of the plot
    right.axisIndex = 0;
    right.binCount = 10;
    right.filteredValues.assign(40, 7.5);   // 40 of the bin's 132 values: under a third of the bar
    right.filteredFill = kSubsetFill;
    chart.AddAxisDistribution(right);
    chart.Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
    canvas.Save("axis_distribution_joint_plot");

    const ChartEngineFrame& frame = chart.Frame();
    const Rect2Dd plot = frame.plotArea;
    Check(std::abs((before.y + 54.0) - plot.y) < 1.0,
          "the plot starts lower by the top band's depth (gap + thickness)");
    Check(std::abs((before.Right() - 54.0) - plot.Right()) < 1.0,
          "and ends further left by the right band's");
    Check(std::abs(before.x - plot.x) < 1.0 && std::abs(before.Bottom() - plot.Bottom()) < 1.0,
          "the edges with no band stay where they were");
    const Rect2Dd legend = chart.LegendBox();
    Check(legend.width > 0.0 && legend.x >= plot.Right() + 54.0 - 1.0,
          "the legend stays clear of the right band");
    Check(std::abs(legend.x - legendBefore.x) < 1.0, "and keeps its own place at the edge");

    const ChartAxis& x = frame.axes->At(1);
    const ChartAxis& y = frame.axes->At(0);
    const double peakX = plot.x + plot.width * x.Normalize(2.5);
    const double emptyX = plot.x + plot.width * x.Normalize(-5.5);
    Check(Close(canvas.PixelAt(peakX, plot.y - 8.0), kBandFill),
          "the x histogram's tallest bar stands above the plot where the x values cluster");
    Check(Close(canvas.PixelAt(peakX, plot.y - 4.0 - 48.0), kBandFill),
          "and reaches the full depth of the band");
    Check(!Close(canvas.PixelAt(emptyX, plot.y - 8.0), kBandFill),
          "where there are no values there is no bar");
    Check(!Close(canvas.PixelAt(peakX, plot.y + 8.0), kBandFill),
          "nothing of the band is drawn inside the plot");

    const double peakY = plot.Bottom() - plot.height * y.Normalize(7.5);
    Check(Close(canvas.PixelAt(plot.Right() + 8.0, peakY), kSubsetFill),
          "the subset is drawn over the y histogram's bar at its own values");
    Check(Close(canvas.PixelAt(plot.Right() + 4.0 + 48.0, peakY), kBandFill),
          "with the whole bar reaching past it");
}

void CheckNearInside() {
    std::cout << "\nNear: inside the plot\n";
    Canvas bare(kWidth, kHeight), canvas(kWidth, kHeight);
    if (!canvas.ctx || !bare.ctx) { Check(false, "an offscreen context is available"); return; }

    FieldChart plain(false);
    plain.Render(bare.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));

    FieldChart chart(false);
    AxisDistribution d;
    d.axisIndex = 1;
    d.kind = AxisDistributionKind::Histogram;
    d.side = AxisDistributionSide::Near;
    d.binCount = 16;
    d.thickness = 60.0;
    d.fill = kBandFill;
    d.strokeWidth = 0.0f;
    chart.AddAxisDistribution(d);
    chart.Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
    canvas.Save("axis_distribution_near");

    const Rect2Dd plot = chart.Frame().plotArea;
    const Rect2Dd before = plain.Frame().plotArea;
    Check(std::abs(plot.y - before.y) < 0.5 && std::abs(plot.Right() - before.Right()) < 0.5 &&
          std::abs(plot.Bottom() - before.Bottom()) < 0.5,
          "a Near band reserves nothing: the plot is unchanged");
    const double peakX = plot.x + plot.width * chart.Frame().axes->At(1).Normalize(2.5);
    Check(Close(canvas.PixelAt(peakX, plot.Bottom() - 6.0), kBandFill),
          "the bars rise from the axis's own edge into the plot");
    Check(!Close(canvas.PixelAt(peakX, plot.Bottom() - 64.0), kBandFill),
          "no further than the band's depth");
    Check(!Close(canvas.PixelAt(peakX, plot.Bottom() + 6.0), kBandFill),
          "and nothing spills below the plot");
}

void CheckInPlotRules() {
    std::cout << "\nBoth sides of an in-plot rule\n";
    Canvas canvas(kWidth, kHeight);
    if (!canvas.ctx) { Check(false, "an offscreen context is available"); return; }

    FieldChart chart(true);
    AxisDistribution d;
    d.axisIndex = 0;                         // the rule at 0.25
    d.kind = AxisDistributionKind::Histogram;
    d.side = AxisDistributionSide::Both;
    d.binCount = 10;
    d.thickness = 1000.0;                    // far wider than the gap to the next rule
    d.fill = kBandFill;
    d.strokeWidth = 0.0f;
    chart.AddAxisDistribution(d);
    chart.Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
    canvas.Save("axis_distribution_in_plot");

    const ChartEngineFrame& frame = chart.Frame();
    const Rect2Dd plot = frame.plotArea;
    const double rule = frame.projection->ToScreen(ChartNormalizedPoint(0.25, 0.0)).x;
    const double next = frame.projection->ToScreen(ChartNormalizedPoint(0.75, 0.0)).x;
    const double gap = next - rule;
    const double peakY = frame.projection->ToScreen(
        ChartNormalizedPoint(0.25, frame.axes->At(0).Normalize(7.5))).y;
    Check(Close(canvas.PixelAt(rule + 6.0, peakY), kBandFill) &&
          Close(canvas.PixelAt(rule - 6.0, peakY), kBandFill),
          "the bars grow both ways from the rule");
    Check(Close(canvas.PixelAt(rule + gap * 0.40, peakY), kBandFill),
          "the tallest reaches most of the way to the next rule");
    Check(!Close(canvas.PixelAt(rule + gap * 0.50, peakY), kBandFill),
          "but stops short of half the gap, whatever thickness asked for");
    (void)plot;
}

// The first and last rules of a parallel coordinate chart stand on the plot's
// edges. A band on their outward side has no room there: it must turn inward
// rather than run over the margin, where the tick labels and the legend are.
void CheckOutermostRulesStayInside() {
    std::cout << "\nThe outermost in-plot rules: bands stay inside the plot\n";
    struct Case { const char* name; size_t axis; AxisDistributionSide side; };
    const Case cases[] = {
        {"far_last", 1, AxisDistributionSide::Far},      // the last rule's right side
        {"near_first", 0, AxisDistributionSide::Near},   // the first rule's tick side
        {"both_last", 1, AxisDistributionSide::Both},
    };
    for (const Case& c : cases) {
        Canvas canvas(kWidth, kHeight);
        if (!canvas.ctx) { Check(false, "an offscreen context is available"); return; }
        FieldChart chart(true, 0.0, 1.0);
        chart.SetShowLegend(true);
        chart.SetLegendEntries({ChartLegendEntry("field", Color(90, 90, 90, 255))});
        AxisDistribution d;
        d.axisIndex = c.axis;
        d.kind = AxisDistributionKind::Histogram;
        d.side = c.side;
        d.binCount = 10;
        d.thickness = 60.0;
        d.fill = kBandFill;
        d.strokeWidth = 0.0f;
        chart.AddAxisDistribution(d);
        chart.Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
        canvas.Save(std::string("axis_distribution_outermost_") + c.name);

        const ChartEngineFrame& frame = chart.Frame();
        const Rect2Dd plot = frame.plotArea;
        const ChartAxis& axis = frame.axes->At(c.axis);
        const double peak = (c.axis == 0) ? 7.5 : 2.5;     // where the values cluster
        const Point2Dd at = frame.projection->ToScreen(
            ChartNormalizedPoint(axis.plotPosition, axis.Normalize(peak)));
        const double inward = (axis.plotPosition < 0.5) ? 1.0 : -1.0;
        int outside = 0;                                   // band pixels past the plot's side
        for (int px = 1; px <= 60; ++px) {
            if (Close(canvas.PixelAt(at.x - inward * px, at.y), kBandFill)) ++outside;
        }
        Check(outside == 0, std::string(c.name) + ": nothing of the band is drawn past the plot's edge");
        Check(Close(canvas.PixelAt(at.x + inward * 6.0, at.y), kBandFill),
              std::string(c.name) + ": the band is drawn inward, inside the plot");
        Check(Close(canvas.PixelAt(at.x + inward * 55.0, at.y), kBandFill),
              std::string(c.name) + ": at its full depth");
        if (axis.plotPosition > 0.5) {
            const Rect2Dd legend = chart.LegendBox();
            bool overLegend = false;
            for (double ly = legend.y + 2.0; ly < legend.Bottom() - 2.0; ly += 3.0) {
                for (double lx = legend.x + 2.0; lx < legend.Right() - 2.0; lx += 3.0) {
                    overLegend = overLegend || Close(canvas.PixelAt(lx, ly), kBandFill);
                }
            }
            Check(legend.width > 0.0 && !overLegend,
                  std::string(c.name) + ": and the legend beside the last rule is not drawn over");
        }
        (void)plot;
    }
}

void CheckPolarDrawsNothing() {
    std::cout << "\nPolar: no band\n";
    Canvas bare(kWidth, kHeight), canvas(kWidth, kHeight);
    if (!canvas.ctx || !bare.ctx) { Check(false, "an offscreen context is available"); return; }

    FieldChart plain(false);
    plain.SetProjectionKind(ChartProjectionKind::Polar);
    plain.Render(bare.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));

    FieldChart chart(false);
    chart.SetProjectionKind(ChartProjectionKind::Polar);
    AxisDistribution d;
    d.axisIndex = 1;
    d.fill = kBandFill;
    chart.AddAxisDistribution(d);
    chart.Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));

    const Rect2Dd a = plain.Frame().plotArea, b = chart.Frame().plotArea;
    Check(std::abs(a.x - b.x) < 0.5 && std::abs(a.y - b.y) < 0.5 &&
          std::abs(a.width - b.width) < 0.5 && std::abs(a.height - b.height) < 0.5,
          "an Outside band reserves nothing under Polar");
    bool painted = false;
    for (int py = 0; py < kHeight && !painted; py += 4) {
        for (int px = 0; px < kWidth && !painted; px += 4) {
            painted = Close(canvas.PixelAt(px, py), kBandFill, 4);
        }
    }
    Check(!painted, "and nothing is drawn");
}

void CheckEveryKind() {
    std::cout << "\nEvery kind, on the y axis's Outside band\n";
    struct Kind { AxisDistributionKind kind; const char* name; double across; };
    // Where across the band each kind is sure to paint at the values' peak:
    // the bars and the density from the baseline, the violin about its centre
    // line, the box between its quartiles, the strip's ticks in the middle.
    const Kind kinds[] = {
        {AxisDistributionKind::Histogram, "histogram", 0.1},
        {AxisDistributionKind::Density, "density", 0.1},
        {AxisDistributionKind::Box, "box", 0.3},
        {AxisDistributionKind::Strip, "strip", 0.5},
        {AxisDistributionKind::Violin, "violin", 0.5},
    };
    for (const Kind& k : kinds) {
        Canvas canvas(kWidth, kHeight);
        if (!canvas.ctx) { Check(false, "an offscreen context is available"); return; }
        FieldChart chart(false);
        AxisDistribution d;
        d.axisIndex = 0;
        d.kind = k.kind;
        d.thickness = 60.0;
        d.fill = kBandFill;
        d.stroke = kBandFill;               // the strip's ticks are strokes
        chart.AddAxisDistribution(d);
        chart.Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
        canvas.Save(std::string("axis_distribution_") + k.name);

        const Rect2Dd plot = chart.Frame().plotArea;
        const ChartAxis& y = chart.Frame().axes->At(0);
        // The box's quartiles straddle 7, the other kinds peak at 7.5.
        const double at = (k.kind == AxisDistributionKind::Box) ? 7.0 : 7.5;
        const double py = plot.Bottom() - plot.height * y.Normalize(at);
        const double px = plot.Right() + d.gap + d.thickness * k.across;
        Check(Close(canvas.PixelAt(px, py), kBandFill),
              std::string("the ") + k.name + " paints its band where the values are");
        Check(!Close(canvas.PixelAt(plot.Right() - 6.0, py), kBandFill),
              std::string("and nothing of it inside the plot"));
    }
}

} // namespace

int main() {
    std::cout << "=== AxisDistributionRenderTest ===\n";
    CheckJointPlot();
    CheckNearInside();
    CheckInPlotRules();
    CheckOutermostRulesStayInside();
    CheckPolarDrawsNothing();
    CheckEveryKind();
    std::cout << "\n" << (g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED")
              << " (" << g_failures << " failure" << (g_failures == 1 ? "" : "s") << ")\n";
    return g_failures == 0 ? 0 : 1;
}
