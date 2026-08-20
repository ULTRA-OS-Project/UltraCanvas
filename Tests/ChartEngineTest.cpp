// Tests/ChartEngineTest.cpp
// Unit tests for the chart engine model layer: axis ranges and scales, tick
// generation and formatting, the layout negotiation, the three 2-D projections
// and the label policy / plan.
//
// Exercises the real engine sources with no UI stack and no link dependencies
// beyond the files under test.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartAxis.h"
#include "Plugins/Charts/Engine/UltraCanvasChartLabels.h"
#include "Plugins/Charts/Engine/UltraCanvasChartProjection.h"
#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"
#include "Plugins/Charts/Engine/UltraCanvasChartTheme.h"
#include "Plugins/Charts/UltraCanvasParallelAxisModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }           \
    } while (0)

static bool Near(double a, double b, double tol = 1e-9) { return std::abs(a - b) <= tol; }

// =============================================================================
// AXIS
// =============================================================================

static void TestLinearAxis() {
    std::printf("Linear axis (range, normalisation, inversion)\n");

    ChartAxis axis("value");
    axis.Observe({2.0, 8.0, 5.0, 3.0});
    axis.Finalize();

    CHECK(axis.Min() <= 2.0 && axis.Max() >= 8.0, "the auto range covers the data");
    CHECK(Near(axis.Normalize(axis.Min()), 0.0) && Near(axis.Normalize(axis.Max()), 1.0),
          "the range ends map to 0 and 1");

    const double mid = (axis.Min() + axis.Max()) * 0.5;
    CHECK(Near(axis.Normalize(mid), 0.5), "the midpoint maps to 0.5");
    CHECK(Near(axis.Denormalize(axis.Normalize(6.25)), 6.25, 1e-9),
          "normalise and denormalise round-trip");

    axis.inverted = true;
    CHECK(Near(axis.Normalize(axis.Min()), 1.0), "inversion flips the ends");
    CHECK(Near(axis.Denormalize(axis.Normalize(6.25)), 6.25, 1e-9),
          "the round trip survives inversion");
}

static void TestExplicitRangeAndOutOfRange() {
    std::printf("Explicit range (values outside are not clamped)\n");

    ChartAxis axis;
    axis.SetRange(0.0, 100.0);
    axis.Finalize();

    CHECK(Near(axis.Min(), 0.0) && Near(axis.Max(), 100.0), "an explicit range is used verbatim");
    CHECK(Near(axis.Normalize(25.0), 0.25), "a value maps by proportion");
    CHECK(axis.Normalize(150.0) > 1.0, "an out-of-range value maps outside 0..1");
    CHECK(axis.Normalize(-50.0) < 0.0, "so does one below the range");
}

static void TestDegenerateRange() {
    std::printf("Degenerate range (a constant column must not divide by zero)\n");

    ChartAxis axis;
    axis.Observe({7.0, 7.0, 7.0});
    axis.Finalize();

    CHECK(axis.IsDegenerate(), "a constant column is reported as degenerate");
    CHECK(axis.Max() > axis.Min(), "the range is padded rather than collapsed");
    const double t = axis.Normalize(7.0);
    CHECK(std::isfinite(t), "normalising the constant value yields a finite result");
    CHECK(Near(t, 0.5, 1e-6), "the constant value sits in the middle of the padded range");

    ChartAxis empty;
    empty.Finalize();
    CHECK(empty.Max() > empty.Min() && std::isfinite(empty.Normalize(0.5)),
          "an axis with no data at all still yields a usable range");
}

static void TestNaNIsIgnored() {
    std::printf("Missing values (NaN must not poison the range)\n");

    ChartAxis axis;
    axis.Observe(1.0);
    axis.Observe(std::nan(""));
    axis.Observe(std::numeric_limits<double>::infinity());
    axis.Observe(9.0);
    axis.Finalize();

    CHECK(std::isfinite(axis.Min()) && std::isfinite(axis.Max()),
          "the range stays finite with NaN and infinity in the data");
    CHECK(axis.SampleCount() == 2, "non-finite observations are dropped");
}

static void TestLogAxis() {
    std::printf("Log axis\n");

    ChartAxis axis;
    axis.scale = ChartScale::Log;
    axis.SetRange(1.0, 1000.0);
    axis.Finalize();

    CHECK(Near(axis.Normalize(1.0), 0.0) && Near(axis.Normalize(1000.0), 1.0),
          "the ends map to 0 and 1");
    CHECK(Near(axis.Normalize(10.0), 1.0 / 3.0, 1e-9), "a decade is a third of the way");
    CHECK(Near(axis.Denormalize(axis.Normalize(42.0)), 42.0, 1e-6), "log round-trips");
    CHECK(std::isfinite(axis.Normalize(0.0)) && std::isfinite(axis.Normalize(-5.0)),
          "non-positive values are floored rather than producing -inf");

    const std::vector<ChartTick> ticks = axis.GenerateTicks(4);
    CHECK(!ticks.empty(), "a log axis generates ticks");
    bool decades = true;
    for (const ChartTick& t : ticks) {
        const double e = std::log10(t.value);
        if (std::abs(e - std::round(e)) > 1e-9) decades = false;
    }
    CHECK(decades, "log ticks land on decades");
}

static void TestZScoreAndPercentile() {
    std::printf("Standardising scales (z-score, percentile)\n");

    ChartAxis z;
    z.scale = ChartScale::ZScore;
    z.Observe({10.0, 12.0, 14.0, 16.0, 18.0});
    z.Finalize();
    // Mean 14, so the mean must land in the middle of the standardised range.
    CHECK(Near(z.Normalize(14.0), 0.5, 1e-9), "the mean maps to the centre");
    CHECK(z.Normalize(18.0) > z.Normalize(10.0), "order is preserved");
    CHECK(Near(z.Denormalize(z.Normalize(12.0)), 12.0, 1e-6), "z-score round-trips");

    ChartAxis robust;
    robust.scale = ChartScale::RobustZScore;
    robust.Observe({10.0, 12.0, 14.0, 16.0, 1000.0});   // one wild outlier
    robust.Finalize();
    CHECK(Near(robust.Normalize(14.0), 0.5, 1e-6) || robust.Normalize(14.0) < 0.6,
          "the median stays near the centre despite an outlier");

    ChartAxis p;
    p.scale = ChartScale::Percentile;
    p.Observe({1.0, 2.0, 3.0, 4.0, 5.0});
    p.Finalize();
    CHECK(Near(p.Normalize(1.0), 0.0), "the smallest value is rank 0");
    CHECK(Near(p.Normalize(5.0), 1.0), "the largest value is rank 1");
    CHECK(Near(p.Normalize(3.0), 0.5), "the median value is rank 0.5");
}

static void TestCategoryAxis() {
    std::printf("Category axis\n");

    ChartAxis axis;
    axis.scale = ChartScale::Category;
    axis.categories = {"setosa", "versicolor", "virginica"};
    axis.Finalize();

    CHECK(Near(axis.Normalize(0.0), 0.0) && Near(axis.Normalize(2.0), 1.0),
          "the first and last categories sit at the ends");
    CHECK(Near(axis.Normalize(1.0), 0.5), "the middle category sits in the middle");
    CHECK(axis.FormatValue(1.0) == "versicolor", "values format as their category name");

    const std::vector<ChartTick> ticks = axis.GenerateTicks(10);
    CHECK(ticks.size() == 3, "one tick per category regardless of the target count");
    CHECK(ticks[2].label == "virginica", "tick labels are the category names");
}

static void TestCategoryPadding() {
    std::printf("Category axis slot padding\n");

    ChartAxis axis;
    axis.scale = ChartScale::Category;
    axis.categories = {"A", "B", "C", "D"};
    axis.categoryPadding = 0.5;
    axis.Finalize();

    // Range -0.5 .. 3.5: every slot is 1/4 of the domain and the outer slots
    // have the same width as the inner ones.
    CHECK(Near(axis.Min(), -0.5) && Near(axis.Max(), 3.5),
          "the padded range extends half a slot past the outer categories");
    CHECK(Near(axis.Normalize(0.0), 0.125), "the first slot centre sits half a slot in");
    CHECK(Near(axis.Normalize(3.0), 0.875), "the last slot centre sits half a slot short of the end");
    CHECK(Near(axis.Normalize(1.0) - axis.Normalize(0.0), 0.25),
          "neighbouring slot centres are one slot apart");

    ChartAxis single;
    single.scale = ChartScale::Category;
    single.categories = {"only"};
    single.categoryPadding = 0.5;
    single.Finalize();
    CHECK(Near(single.Normalize(0.0), 0.5), "a padded single category is centred");
    CHECK(!single.IsDegenerate(), "padding keeps a single category non-degenerate");
}

// =============================================================================
// BAR SERIES GEOMETRY
// =============================================================================

static ChartAxis MakeBarCategoryAxis(size_t count) {
    ChartAxis axis;
    axis.scale = ChartScale::Category;
    for (size_t i = 0; i < count; ++i) axis.categories.push_back("C" + std::to_string(i));
    axis.categoryPadding = 0.5;
    axis.Finalize();
    return axis;
}

static void TestGroupedBarSpans() {
    std::printf("Bar spans: grouped\n");

    const std::vector<std::vector<double>> series = {{10.0, 20.0}, {30.0, 40.0}};
    ChartBarLayoutOptions options;
    options.arrangement = ChartBarArrangement::Grouped;

    ChartAxis value;
    ObserveBarSeries(value, series, options);
    value.Finalize();
    CHECK(value.Min() <= 0.0 && value.Max() >= 40.0,
          "the observed range covers zero and the largest value");

    const ChartAxis category = MakeBarCategoryAxis(2);
    const auto spans = BuildBarSpans(value, category, 2, series, options);
    CHECK(spans.size() == 4, "two series x two categories = four bars");

    // Category 0 occupies u 0..0.5; 70% fill = 0.175 half-width around 0.25.
    CHECK(Near(spans[0].u0, 0.25 - 0.175) && Near(spans[1].u1, 0.25 + 0.175),
          "the group fills the slot fraction around the slot centre");
    CHECK(Near(spans[0].u1, spans[1].u0), "bars in a group touch with zero group gap");
    CHECK(spans[0].v1 > spans[0].v0, "a positive bar's value edge is above its base");
    CHECK(Near(spans[0].v0, value.Normalize(0.0)), "grouped bars grow from zero");
}

static void TestStackedBarSpans() {
    std::printf("Bar spans: stacked, negatives diverge\n");

    const std::vector<std::vector<double>> series = {{10.0}, {-4.0}, {20.0}};
    ChartBarLayoutOptions options;
    options.arrangement = ChartBarArrangement::Stacked;

    ChartAxis value;
    ObserveBarSeries(value, series, options);
    value.Finalize();
    CHECK(value.Min() <= -4.0 && value.Max() >= 30.0,
          "the range covers the positive stack total and the negative total");

    const ChartAxis category = MakeBarCategoryAxis(1);
    const auto spans = BuildBarSpans(value, category, 1, series, options);
    CHECK(spans.size() == 3, "one span per series");

    CHECK(Near(spans[0].v0, value.Normalize(0.0)) &&
          Near(spans[0].v1, value.Normalize(10.0)),
          "the first positive segment runs 0 to 10");
    CHECK(Near(spans[2].v0, value.Normalize(10.0)) &&
          Near(spans[2].v1, value.Normalize(30.0)),
          "the next positive segment stacks on the positive total, skipping the negative");
    CHECK(Near(spans[1].v0, value.Normalize(0.0)) &&
          Near(spans[1].v1, value.Normalize(-4.0)),
          "the negative segment diverges downward from zero");
    CHECK(Near(spans[0].u0, spans[2].u0) && Near(spans[0].u1, spans[2].u1),
          "stack segments share the same domain extent");
}

static void TestPercentStackedBarSpans() {
    std::printf("Bar spans: 100%% stacked\n");

    const std::vector<std::vector<double>> series = {{30.0, 10.0}, {70.0, 30.0}};
    ChartBarLayoutOptions options;
    options.arrangement = ChartBarArrangement::PercentStacked;

    ChartAxis value;
    ObserveBarSeries(value, series, options);
    value.Finalize();
    CHECK(value.Min() <= 0.0 && value.Max() >= 100.0,
          "the percent range covers 0 to 100");

    const ChartAxis category = MakeBarCategoryAxis(2);
    const auto spans = BuildBarSpans(value, category, 2, series, options);
    CHECK(spans.size() == 4, "one segment per series per category");

    CHECK(Near(spans[0].plotted, 30.0) && Near(spans[1].plotted, 70.0),
          "category 0 splits 30/70");
    CHECK(Near(spans[2].plotted, 25.0) && Near(spans[3].plotted, 75.0),
          "category 1 rescales 10/30 to 25/75");
    CHECK(Near(spans[3].v1, value.Normalize(100.0)),
          "every percent stack tops out at 100");
    CHECK(Near(spans[0].value, 30.0), "the span still carries the raw value");
}

static void TestBarSpansRaggedAndGapped() {
    std::printf("Bar spans: ragged series and group gaps\n");

    // The second series has no value in category 1.
    const std::vector<std::vector<double>> series = {{5.0, 7.0}, {3.0}};
    ChartBarLayoutOptions options;
    options.arrangement = ChartBarArrangement::Grouped;
    options.groupGap = 0.5;

    ChartAxis value;
    ObserveBarSeries(value, series, options);
    value.Finalize();

    const ChartAxis category = MakeBarCategoryAxis(2);
    const auto spans = BuildBarSpans(value, category, 2, series, options);
    CHECK(spans.size() == 3, "a missing value simply has no bar");
    CHECK(spans[1].u0 > spans[0].u1, "a group gap separates bars in a group");
}

static void TestBarOutlineRounding() {
    std::printf("Bar outline: corner rounding under projections\n");

    ChartVerticalProjection vertical;
    vertical.SetPlotArea(Rect2Dd(0.0, 0.0, 200.0, 100.0));

    // u 0.2..0.6, v 0..0.5 is a 80x50 screen rectangle.
    const auto plain = BuildBarOutline(vertical, 0.2, 0.0, 0.6, 0.5, 8, 0.0);
    CHECK(plain.size() == 4 * 8, "an unrounded outline is the subdivided quad");
    const Point2Dd corner = vertical.ToScreen(ChartNormalizedPoint(0.2, 0.0));
    bool hasCorner = false;
    for (const auto& p : plain) {
        if (Near(p.x, corner.x, 1e-6) && Near(p.y, corner.y, 1e-6)) hasCorner = true;
    }
    CHECK(hasCorner, "the unrounded outline passes through the corner");

    const double radius = 10.0;
    const auto rounded = BuildBarOutline(vertical, 0.2, 0.0, 0.6, 0.5, 8, radius);
    CHECK(rounded.size() > plain.size(), "rounding adds fillet points");

    // Every corner of the rectangle is cut back: no outline point comes
    // closer to a corner than the fillet allows, and none leaves the box.
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const auto& p : plain) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    const Point2Dd corners[4] = {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
    double closestCorner = 1e9;
    bool inBox = true;
    for (const auto& p : rounded) {
        for (const auto& c : corners) {
            closestCorner = std::min(closestCorner, std::hypot(p.x - c.x, p.y - c.y));
        }
        if (p.x < minX - 0.01 || p.x > maxX + 0.01 ||
            p.y < minY - 0.01 || p.y > maxY + 0.01) inBox = false;
    }
    // A quadratic fillet's nearest approach to the corner is radius/2 along
    // the diagonal; anything clearly away from zero proves the cut.
    CHECK(closestCorner > radius * 0.3, "rounded corners no longer touch the corner points");
    CHECK(inBox, "the fillets stay inside the bar's bounding box");

    // The trim points where the fillet meets the straight edges survive.
    bool hasTrim = false;
    for (const auto& p : rounded) {
        if (Near(p.x, minX + radius, 0.5) && Near(p.y, minY, 0.5)) hasTrim = true;
    }
    CHECK(hasTrim, "the fillet joins the edge at the trim distance");

    // A collapsed bar (animation start) degrades without blowing up.
    const auto collapsed = BuildBarOutline(vertical, 0.2, 0.0, 0.6, 0.0, 8, radius);
    CHECK(!collapsed.empty(), "a zero-height bar still yields an outline");

    // Under Polar the same call rounds a ring sector's corners.
    ChartPolarProjection polar;
    polar.SetPlotArea(Rect2Dd(0.0, 0.0, 200.0, 200.0));
    polar.SetInnerRadiusFraction(0.3);
    const auto sector = BuildBarOutline(polar, 0.1, 0.2, 0.3, 0.9, 8, 6.0);
    bool inPlot = !sector.empty();
    for (const auto& p : sector) {
        if (p.x < -0.5 || p.x > 200.5 || p.y < -0.5 || p.y > 200.5) inPlot = false;
    }
    CHECK(inPlot, "a rounded ring sector stays inside the plot");
}

static void TestTicksAndFormatting() {
    std::printf("Ticks and formatting\n");

    ChartAxis axis;
    axis.SetRange(0.0, 1.0);
    axis.Finalize();
    const std::vector<ChartTick> ticks = axis.GenerateTicks(6);

    CHECK(ticks.size() >= 3 && ticks.size() <= 12, "roughly the requested number of ticks");
    bool ordered = true, inRange = true;
    for (size_t i = 0; i < ticks.size(); ++i) {
        if (i && ticks[i].value <= ticks[i - 1].value) ordered = false;
        if (ticks[i].normalized < -1e-9 || ticks[i].normalized > 1.0 + 1e-9) inRange = false;
    }
    CHECK(ordered, "ticks come back in ascending order");
    CHECK(inRange, "every tick normalises inside the plot");

    bool endsPrioritised = false;
    for (const ChartTick& t : ticks) if (t.priority >= 10) endsPrioritised = true;
    CHECK(endsPrioritised, "the range ends get a declutter priority");

    // Nice numbers: a scruffy range must produce round tick labels.
    ChartAxis scruffy;
    scruffy.Observe({0.0031, 0.1978});
    scruffy.Finalize();
    const std::vector<ChartTick> niceTicks = scruffy.GenerateTicks(5);
    bool round = true;
    for (const ChartTick& t : niceTicks) {
        const double scaled = t.value * 1000.0;
        if (std::abs(scaled - std::round(scaled)) > 1e-6) round = false;
    }
    CHECK(round, "auto ticks land on round values, not raw data extremes");

    ChartAxis money;
    money.SetRange(0.0, 5000000.0);
    money.unitPrefix = "$";
    money.compactNumbers = true;
    money.Finalize();
    CHECK(money.FormatValue(2500000.0) == "$2.5M", "compact formatting with a unit prefix");

    ChartAxis custom;
    custom.SetRange(0.0, 1.0);
    custom.formatter = [](double v) { return std::string("<") + std::to_string((int)(v * 100)) + ">"; };
    custom.Finalize();
    CHECK(custom.FormatValue(0.5) == "<50>", "a custom formatter overrides everything");

    // Explicit tick values override generation - what a bar chart's category
    // axis uses to put one tick under each bar.
    ChartAxis explicitTicks;
    explicitTicks.SetRange(-0.6, 3.4);
    explicitTicks.tickValues = {0.0, 1.0, 2.0, 3.0};
    explicitTicks.formatter = [](double v) {
        static const char* names[] = {"Q1", "Q2", "Q3", "Q4"};
        const int i = (int)std::lround(v);
        return std::string((i >= 0 && i < 4) ? names[i] : "?");
    };
    explicitTicks.Finalize();
    const std::vector<ChartTick> et = explicitTicks.GenerateTicks(6);
    CHECK(et.size() == 4, "explicit tickValues produce exactly those ticks");
    CHECK(et[1].label == "Q2", "explicit ticks format through the axis formatter");
    CHECK(et[0].normalized > 0.0 && et[3].normalized < 1.0,
          "explicit ticks sit inside a padded range");
}

static void TestAxisSet() {
    std::printf("Axis set\n");

    ChartAxisSet axes;
    ChartAxis a("price");
    a.Observe({10.0, 20.0});
    ChartAxis b("volume");
    b.Observe({100.0, 900.0});
    axes.Add(a);
    axes.Add(b);
    axes.FinalizeAll();

    CHECK(axes.Count() == 2, "axes accumulate");
    CHECK(axes.Find("volume") != nullptr, "axes are findable by name");
    CHECK(axes.Find("missing") == nullptr, "an unknown name yields null");
    CHECK(axes.At(0).Max() >= 20.0 && axes.At(1).Max() >= 900.0,
          "each axis keeps its own independent range");
}

// =============================================================================
// LAYOUT
// =============================================================================

static void TestLayoutNegotiation() {
    std::printf("Layout negotiation (measure then solve)\n");

    ChartLayoutRequest request;
    request.Reserve(ChartAxisEdge::Left, 60.0);     // y tick labels
    request.Reserve(ChartAxisEdge::Left, 40.0);     // a smaller claim must not shrink it
    request.Reserve(ChartAxisEdge::Bottom, 30.0);
    request.Reserve(ChartAxisEdge::Right, 120.0);   // legend
    request.Reserve(ChartAxisEdge::Top, 24.0);      // title

    const Rect2Dd chartArea(0, 0, 800, 600);
    const Rect2Dd plot = SolvePlotArea(chartArea, request.margins);

    CHECK(Near(request.margins.left, 60.0), "the largest reservation on an edge wins");
    CHECK(Near(plot.x, 60.0) && Near(plot.y, 24.0), "the plot area starts inside the margins");
    CHECK(Near(plot.width, 800.0 - 60.0 - 120.0), "width accounts for both side margins");
    CHECK(Near(plot.height, 600.0 - 24.0 - 30.0), "height accounts for top and bottom");

    // Greedy reservations must not invert the plot area.
    ChartLayoutRequest greedy;
    greedy.Reserve(ChartAxisEdge::Left, 5000.0);
    greedy.Reserve(ChartAxisEdge::Right, 5000.0);
    const Rect2Dd squeezed = SolvePlotArea(chartArea, greedy.margins);
    CHECK(squeezed.width > 0.0 && squeezed.height > 0.0,
          "impossible reservations still leave a usable plot area");
}

// =============================================================================
// PROJECTIONS
// =============================================================================

static void TestVerticalAndHorizontalProjections() {
    std::printf("Vertical and horizontal projections\n");

    const Rect2Dd plot(100, 50, 400, 300);

    ChartVerticalProjection vertical;
    vertical.SetPlotArea(plot);
    const Point2Dd origin = vertical.ToScreen(ChartNormalizedPoint(0.0, 0.0));
    const Point2Dd corner = vertical.ToScreen(ChartNormalizedPoint(1.0, 1.0));
    CHECK(Near(origin.x, 100.0) && Near(origin.y, 350.0),
          "vertical: value 0 is at the bottom left");
    CHECK(Near(corner.x, 500.0) && Near(corner.y, 50.0),
          "vertical: value 1 is at the top right");

    ChartHorizontalProjection horizontal;
    horizontal.SetPlotArea(plot);
    const Point2Dd hOrigin = horizontal.ToScreen(ChartNormalizedPoint(0.0, 0.0));
    const Point2Dd hCorner = horizontal.ToScreen(ChartNormalizedPoint(1.0, 1.0));
    CHECK(Near(hOrigin.x, 100.0) && Near(hOrigin.y, 50.0),
          "horizontal: the domain runs down, the value runs right");
    CHECK(Near(hCorner.x, 500.0) && Near(hCorner.y, 350.0),
          "horizontal: the far corner is bottom right");

    // Round trips.
    const ChartNormalizedPoint p(0.32, 0.71);
    const ChartNormalizedPoint v2 = vertical.ToNormalized(vertical.ToScreen(p));
    const ChartNormalizedPoint h2 = horizontal.ToNormalized(horizontal.ToScreen(p));
    CHECK(Near(v2.u, p.u, 1e-9) && Near(v2.v, p.v, 1e-9), "vertical round-trips");
    CHECK(Near(h2.u, p.u, 1e-9) && Near(h2.v, p.v, 1e-9), "horizontal round-trips");
}

static void TestPolarProjection() {
    std::printf("Polar projection\n");

    ChartPolarProjection polar;
    polar.SetPlotArea(Rect2Dd(0, 0, 400, 400));
    polar.SetAngles(-90.0, 360.0, true);

    const Point2Dd centre = polar.ToScreen(ChartNormalizedPoint(0.0, 0.0));
    CHECK(Near(centre.x, 200.0) && Near(centre.y, 200.0), "value 0 sits at the centre");

    const Point2Dd top = polar.ToScreen(ChartNormalizedPoint(0.0, 1.0));
    CHECK(Near(top.x, 200.0) && Near(top.y, 0.0),
          "the first axis points straight up with the radar default");

    const Point2Dd quarter = polar.ToScreen(ChartNormalizedPoint(0.25, 1.0));
    CHECK(Near(quarter.x, 400.0, 1e-9) && Near(quarter.y, 200.0, 1e-9),
          "a quarter turn clockwise points right");

    const ChartNormalizedPoint p(0.3, 0.8);
    const ChartNormalizedPoint back = polar.ToNormalized(polar.ToScreen(p));
    CHECK(Near(back.u, p.u, 1e-9) && Near(back.v, p.v, 1e-9), "polar round-trips");

    // Donut: the inner radius must displace value 0 outward.
    polar.SetInnerRadiusFraction(0.5);
    const Point2Dd inner = polar.ToScreen(ChartNormalizedPoint(0.0, 0.0));
    CHECK(Near(inner.y, 100.0), "an inner radius pushes value 0 off the centre");
    const ChartNormalizedPoint donutBack = polar.ToNormalized(polar.ToScreen(p));
    CHECK(Near(donutBack.u, p.u, 1e-9) && Near(donutBack.v, p.v, 1e-9),
          "polar round-trips with an inner radius");

    const std::unique_ptr<IChartProjection> made = CreateChartProjection(ChartProjectionKind::Polar);
    CHECK(made && made->Kind() == ChartProjectionKind::Polar, "the factory returns the right kind");
}

// =============================================================================
// LABELS
// =============================================================================

static ChartLabelRequest MakeRequest(const std::string& text, ChartLabelClass klass,
                                     double x, double y) {
    ChartLabelRequest r;
    r.text = text;
    r.klass = klass;
    r.anchor = Point2Dd(x, y);
    r.textSize = Size2Dd(40.0, 12.0);
    return r;
}

static void TestLabelPolicyRoles() {
    std::printf("Label policy (solved / obstacle / excluded per chart type)\n");

    const ChartLabelPolicy def = ChartLabelPolicy::Default();
    CHECK(HasClass(def.excluded, ChartLabelClass::AxisTick),
          "axis ticks are excluded from the solver by default");
    CHECK(HasClass(def.excluded, ChartLabelClass::AxisTitle),
          "so are axis titles");
    CHECK(HasClass(def.solved, ChartLabelClass::ValueLabel),
          "value labels are solved");
    CHECK(HasClass(def.obstacles, ChartLabelClass::LegendEntry),
          "the legend blocks without moving");

    const ChartLabelPolicy pcp = ChartLabelPolicy::ParallelCoordinates();
    CHECK(HasClass(pcp.obstacles, ChartLabelClass::AxisTitle) &&
          !HasClass(pcp.excluded, ChartLabelClass::AxisTitle),
          "parallel coordinates promotes axis headers to obstacles");

    const ChartLabelPolicy heat = ChartLabelPolicy::Heatmap();
    CHECK(HasClass(heat.obstacles, ChartLabelClass::AxisTick),
          "the heatmap promotes row/column labels to obstacles");

    const ChartLabelPolicy radial = ChartLabelPolicy::Radial();
    CHECK(HasClass(radial.solved, ChartLabelClass::AxisTitle),
          "radial charts solve their spoke labels");

    // A role change must clear the other two.
    ChartLabelPolicy p = ChartLabelPolicy::Default();
    p.SetRole(ChartLabelClass::AxisTick, true, false);
    CHECK(HasClass(p.solved, ChartLabelClass::AxisTick) &&
          !HasClass(p.excluded, ChartLabelClass::AxisTick) &&
          !HasClass(p.obstacles, ChartLabelClass::AxisTick),
          "a class holds exactly one role at a time");
}

static void TestLabelBrokerRouting() {
    std::printf("Label broker (routing by class, one solve for the whole frame)\n");

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 400, 300);

    ChartLabelBroker broker(ChartLabelPolicy::Default(), opts);

    // An excluded axis tick, an obstacle legend entry, and value labels that
    // must avoid the legend.
    ChartLabelRequest tick = MakeRequest("0.5", ChartLabelClass::AxisTick, 10.0, 290.0);
    tick.fixedBounds = Rect2Dd(0.0, 284.0, 30.0, 12.0);
    broker.Add(tick);

    ChartLabelRequest legend = MakeRequest("Series A", ChartLabelClass::LegendEntry, 0, 0);
    legend.fixedBounds = Rect2Dd(280.0, 20.0, 100.0, 60.0);
    broker.Add(legend);

    for (int i = 0; i < 6; ++i) {
        ChartLabelRequest v = MakeRequest("12.3", ChartLabelClass::ValueLabel,
                                          300.0, 30.0 + i * 9.0);
        v.allowSuppress = true;
        broker.Add(v);
    }

    const ChartLabelPlan plan = broker.Solve(7);

    CHECK(plan.generation == 7, "the plan carries its generation stamp");
    CHECK(plan.labels.size() == 8, "every request appears in the plan");

    // The excluded tick keeps exactly the position the caller gave it.
    const PlacedChartLabel* placedTick = nullptr;
    for (const PlacedChartLabel& l : plan.labels)
        if (l.klass == ChartLabelClass::AxisTick) placedTick = &l;
    CHECK(placedTick && Near(placedTick->bounds.x, 0.0) && Near(placedTick->bounds.y, 284.0),
          "an excluded label is passed through untouched");

    // No solved value label may sit under the legend.
    const Rect2Dd legendRect(280.0, 20.0, 100.0, 60.0);
    int overlapping = 0;
    for (const PlacedChartLabel& l : plan.labels) {
        if (l.klass != ChartLabelClass::ValueLabel || l.suppressed) continue;
        const double w = std::min(l.bounds.Right(), legendRect.Right()) -
                         std::max(l.bounds.Left(), legendRect.Left());
        const double h = std::min(l.bounds.Bottom(), legendRect.Bottom()) -
                         std::max(l.bounds.Top(), legendRect.Top());
        if (w > 0.01 && h > 0.01) ++overlapping;
    }
    CHECK(overlapping == 0, "solved labels steer around the obstacle legend");
    CHECK(plan.DrawableCount() <= plan.labels.size(), "suppressed labels are not drawable");
}

static void TestLabelBrokerUserData() {
    std::printf("Label broker (results map back to the caller's own indices)\n");

    LabelPlacementOptions opts;
    opts.bounds = Rect2Dd(0, 0, 300, 300);
    ChartLabelBroker broker(ChartLabelPolicy::Default(), opts);

    for (size_t i = 0; i < 5; ++i) {
        ChartLabelRequest r = MakeRequest("p" + std::to_string(i),
                                          ChartLabelClass::ValueLabel,
                                          40.0 + i * 50.0, 150.0);
        r.userData = 1000 + i;
        r.anchorRadius = 4.0;
        broker.Add(r);
    }

    const ChartLabelPlan plan = broker.Solve();
    std::vector<size_t> seen;
    for (const PlacedChartLabel& l : plan.labels) seen.push_back(l.userData);
    std::sort(seen.begin(), seen.end());

    CHECK(seen.size() == 5, "one result per request");
    CHECK(seen.front() == 1000 && seen.back() == 1004, "userData survives the solve");

    bool clearsMarks = true;
    for (const PlacedChartLabel& l : plan.labels) {
        if (l.suppressed) continue;
        // The label must not sit on top of the 4px mark it belongs to.
        const double cx = l.bounds.x + l.bounds.width * 0.5;
        if (std::abs(l.bounds.Bottom() - 150.0) < 1e-9 && std::abs(cx - 40.0) < 1e-9) {
            clearsMarks = false;
        }
    }
    CHECK(clearsMarks, "a label with an anchor radius clears its mark");
}

// =============================================================================
// PARALLEL AXIS MODEL
// =============================================================================

static ParallelAxisModel MakeIrisLikeModel() {
    ParallelAxisModel model;
    model.AddDimensionColumn("petal", {1.0, 2.0, 3.0, 4.0});
    model.AddDimensionColumn("sepal", {10.0, 20.0, 30.0, 40.0});
    model.AddDimensionColumn("width", {100.0, 300.0, 200.0, 400.0});
    model.SetRecordGroups({"a", "a", "b", "b"});
    return model;
}

static void TestPCPModelBasics() {
    std::printf("PCP model (columns, order, visibility, inversion)\n");

    ParallelAxisModel model = MakeIrisLikeModel();
    CHECK(model.DimensionCount() == 3 && model.RecordCount() == 4,
          "columns build dimensions and records");

    std::vector<size_t> order = model.DisplayOrder();
    CHECK(order.size() == 3 && order[0] == 0 && order[2] == 2,
          "the default display order is insertion order");

    model.MoveAxis(2, 0);            // drag "width" to the front
    order = model.DisplayOrder();
    CHECK(order[0] == 2 && order[1] == 0 && order[2] == 1,
          "MoveAxis reorders the display");

    model.SetAxisVisible(0, false);  // hide "petal"
    order = model.DisplayOrder();
    CHECK(order.size() == 2 && order[0] == 2 && order[1] == 1,
          "a hidden dimension leaves the display order");
    CHECK(Near(model.AxisU(0), 0.0) && Near(model.AxisU(1), 1.0),
          "axis positions respan after hiding");

    model.SetAxisVisible(0, true);
    ChartAxisSet axes;
    model.ConfigureAxes(axes);
    model.BuildCache(axes);
    CHECK(axes.Count() == 3, "one axis per visible dimension");
    CHECK(axes.At(0).inPlot && Near(axes.At(0).plotPosition, 0.0) &&
              Near(axes.At(2).plotPosition, 1.0),
          "axes are in-plot at their display positions");

    // Per-axis normalisation: each column's extremes hit 0 and 1.
    // Display order is now width, petal, sepal.
    CHECK(Near(model.NormalizedValue(0, 1), 0.0) &&
              Near(model.NormalizedValue(3, 1), 1.0),
          "per-axis: a column's extremes map to the axis ends");

    model.SetAxisInverted(0, true);  // invert petal (display slot 1)
    ChartAxisSet inverted;
    model.ConfigureAxes(inverted);
    model.BuildCache(inverted);
    CHECK(Near(model.NormalizedValue(0, 1), 1.0),
          "inversion flips a single axis");
}

static void TestPCPCommonScale() {
    std::printf("PCP model (common scale and standardisation)\n");

    ParallelAxisModel model;
    model.AddDimensionColumn("small", {0.0, 5.0, 10.0});
    model.AddDimensionColumn("large", {0.0, 50.0, 100.0});

    // Raw common scale: the same value lands at the same height on every axis.
    model.SetNormalization(PCPNormalization::CommonScale);
    ChartAxisSet axes;
    model.ConfigureAxes(axes);
    model.BuildCache(axes);
    CHECK(Near(model.NormalizedValue(1, 0), 0.05) &&
              Near(model.NormalizedValue(1, 1), 0.5),
          "raw common scale maps values onto one shared band");
    double lo = 0.0, hi = 0.0;
    model.SharedRange(lo, hi);
    CHECK(Near(lo, 0.0) && Near(hi, 100.0), "the shared range spans all columns");

    // Standardised: each column's mean sits at the same height even though the
    // units differ by 10x - the Iris look.
    model.SetStandardize(PCPStandardize::ZScore);
    ChartAxisSet zAxes;
    model.ConfigureAxes(zAxes);
    model.BuildCache(zAxes);
    CHECK(Near(model.NormalizedValue(1, 0), model.NormalizedValue(1, 1), 1e-9),
          "z-scored common scale aligns the column means");
    CHECK(Near(model.NormalizedValue(0, 0), model.NormalizedValue(0, 1), 1e-9),
          "and the standardised extremes");
}

static void TestPCPBrushes() {
    std::printf("PCP model (brush semantics: AND across axes, OR within)\n");

    ParallelAxisModel model = MakeIrisLikeModel();
    CHECK(model.RecordPasses(0) && model.PassingCount() == 4,
          "no brushes: everything passes");

    model.AddBrush(0, 1.5, 3.5);            // petal in [1.5, 3.5] -> records 1, 2
    CHECK(model.PassingCount() == 2 && !model.RecordPasses(0) && model.RecordPasses(1),
          "one brush filters its dimension");

    model.AddBrush(1, 25.0, 45.0);          // AND sepal in [25, 45] -> records 2, 3
    CHECK(model.PassingCount() == 1 && model.RecordPasses(2),
          "brushes on different axes intersect (AND)");

    model.AddBrush(0, 3.8, 4.2);            // OR petal in [3.8, 4.2] -> adds record 3
    CHECK(model.PassingCount() == 2 && model.RecordPasses(3),
          "brushes on the same axis union (OR)");

    model.ClearBrushes(0);
    CHECK(model.PassingCount() == 2, "clearing one axis keeps the other's filter");
    model.ClearBrushes();
    CHECK(model.PassingCount() == 4, "clearing all brushes restores everything");
}

static void TestPCPMissingAndHitTest() {
    std::printf("PCP model (missing values and nearest-line hit testing)\n");

    ParallelAxisModel model;
    model.AddDimensionColumn("a", {0.0, 1.0, std::nan("")});
    model.AddDimensionColumn("b", {0.0, 1.0, 0.5});
    ChartAxisSet axes;
    model.ConfigureAxes(axes);
    model.BuildCache(axes);

    CHECK(!model.HasValue(2, 0) && model.HasValue(2, 1),
          "a NaN cell is missing, its neighbours are not");

    // Record 0 runs along v=0, record 1 along v=1 (both columns share ranges).
    CHECK(model.NearestRecord(0.5, 0.05, 0.2) == 0,
          "the nearest line to a point near the bottom is record 0");
    CHECK(model.NearestRecord(0.5, 0.95, 0.2) == 1,
          "and near the top is record 1");
    CHECK(model.NearestRecord(0.5, 0.5, 0.05) == -1,
          "nothing within tolerance yields -1");
    // Record 2's segment is broken by the NaN, so it can never be hit.
    CHECK(model.NearestRecord(0.5, 0.55, 0.12) != 2,
          "a record with a missing endpoint is not hit through the gap");

    CHECK(MakeIrisLikeModel().Groups() == std::vector<std::string>({"a", "b"}),
          "groups come back distinct, in first-seen order");
}

// =============================================================================
// THEME AND PALETTE
// =============================================================================

static void TestThemeRegistry() {
    std::printf("ChartThemes registry:\n");

    CHECK(ChartThemes::Names().size() == 14, "fourteen built-in themes");
    CHECK(ChartThemes::Find("Dark") != nullptr, "Find resolves an exact name");
    CHECK(ChartThemes::Find("dARk") == ChartThemes::Find("Dark"),
          "Find is case-insensitive");
    CHECK(ChartThemes::Find("NoSuchTheme") == nullptr,
          "Find returns nullptr for an unknown name");
    CHECK(&ChartThemes::Get("NoSuchTheme") == &ChartThemes::Light(),
          "Get falls back to Light for an unknown name");
    CHECK(ChartThemes::Get("ocean").name == "Ocean",
          "Get returns the canonical capitalization");

    for (const std::string& name : ChartThemes::Names()) {
        const ChartTheme& theme = ChartThemes::Get(name);
        CHECK(!theme.palette.Empty(), ("palette not empty: " + name).c_str());
        CHECK(theme.palette.Size() >= 6 && theme.palette.Size() <= 10,
              ("palette size 6..10: " + name).c_str());
    }

    const ChartTheme& dark = ChartThemes::Dark();
    CHECK(dark.backgroundColor.r < 80 && dark.axisLabelColor.r > 120,
          "Dark theme is dark furniture with light labels");
}

static void TestPaletteCycling() {
    std::printf("ChartPalette cycling:\n");

    const ChartPalette& palette = ChartThemes::Light().palette;
    const size_t n = palette.Size();

    CHECK(palette.ColorAt(0) == palette.colors[0], "index 0 is the first colour");
    CHECK(palette.ColorAt(n - 1) == palette.colors[n - 1],
          "last in-range index is the last colour");
    CHECK(!(palette.ColorAt(n) == palette.colors[0]),
          "first wrapped colour is re-tinted, not a repeat");
    CHECK(palette.ColorAt(n).a == palette.colors[0].a,
          "re-tinting preserves alpha");
    CHECK(!(palette.ColorAt(n) == palette.ColorAt(2 * n)),
          "second wrap differs from the first wrap");

    const ChartPalette empty;
    CHECK(empty.ColorAt(3) == Color(128, 128, 128, 255),
          "empty palette yields the grey fallback");
}

static void TestPaletteCountAware() {
    std::printf("ChartPalette count-aware selection:\n");

    // Categorical list: first-k, untouched.
    const ChartPalette& bright = ChartThemes::Light().palette;
    CHECK(bright.ColorAt(2, 3) == bright.colors[2],
          "categorical: element 2 of 3 is base colour 2");

    // Ramp: spread across the run, ends included.
    const ChartPalette& ocean = ChartThemes::Ocean().palette;
    CHECK(ocean.isRamp, "Ocean palette is a ramp");
    CHECK(ocean.ColorAt(0, 3) == ocean.colors.front(),
          "ramp: first of 3 is the dark end");
    CHECK(ocean.ColorAt(2, 3) == ocean.colors.back(),
          "ramp: last of 3 is the light end");
    CHECK(ocean.ColorAt(0, 1) == ocean.colors[ocean.Size() / 2],
          "ramp: a single element takes the middle");
    CHECK(ocean.ColorAt(1, ocean.Size()) == ocean.colors[1],
          "ramp: count == size is the identity");

    // count > size falls back to tinted cycling.
    CHECK(ocean.ColorAt(ocean.Size(), ocean.Size() + 1) ==
              ocean.ColorAt(ocean.Size()),
          "count beyond size matches plain cycling");

    const std::vector<Color> five = bright.ColorsFor(5);
    CHECK(five.size() == 5, "ColorsFor returns the requested count");
    CHECK(five[4] == bright.ColorAt(4, 5), "ColorsFor agrees with ColorAt");
}

static void TestPaletteFromColormap() {
    std::printf("ChartPalette::FromColormap:\n");

    const ChartPalette viridis =
        ChartPalette::FromColormap(HeatmapColormap::Viridis, 12);
    CHECK(viridis.Size() == 12, "twelve colours sampled");
    CHECK(viridis.isRamp, "a sequential colormap yields a ramp palette");
    const std::vector<Color> anchors = ColormapAnchors(HeatmapColormap::Viridis);
    CHECK(viridis.colors.front() == anchors.front(),
          "sampling starts at the colormap's first anchor");
    CHECK(viridis.colors.back() == anchors.back(),
          "sampling ends at the colormap's last anchor");

    const ChartPalette spectral =
        ChartPalette::FromColormap(HeatmapColormap::Spectral, 4);
    CHECK(!spectral.isRamp, "a diverging colormap is not flagged as a ramp");

    const ChartPalette one =
        ChartPalette::FromColormap(HeatmapColormap::Viridis, 1);
    CHECK(one.Size() == 1, "a single-colour sample works");
    CHECK(ChartPalette::FromColormap(HeatmapColormap::Viridis, 0).Empty(),
          "zero-colour sample yields an empty palette");
}

// =============================================================================

int main() {
    std::printf("=== ChartEngineTest ===\n\n");

    TestLinearAxis();
    TestExplicitRangeAndOutOfRange();
    TestDegenerateRange();
    TestNaNIsIgnored();
    TestLogAxis();
    TestZScoreAndPercentile();
    TestCategoryAxis();
    TestCategoryPadding();
    TestGroupedBarSpans();
    TestStackedBarSpans();
    TestPercentStackedBarSpans();
    TestBarSpansRaggedAndGapped();
    TestBarOutlineRounding();
    TestTicksAndFormatting();
    TestAxisSet();
    TestLayoutNegotiation();
    TestVerticalAndHorizontalProjections();
    TestPolarProjection();
    TestLabelPolicyRoles();
    TestLabelBrokerRouting();
    TestLabelBrokerUserData();
    TestPCPModelBasics();
    TestPCPCommonScale();
    TestPCPBrushes();
    TestPCPMissingAndHitTest();
    TestThemeRegistry();
    TestPaletteCycling();
    TestPaletteCountAware();
    TestPaletteFromColormap();

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
