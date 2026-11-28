// Tests/RidgelineModelTest.cpp
// Unit tests for the ridgeline model: quantiles, the bandwidth rules, the
// kernel density (its area, reflection at bounds, binning of large rows), the
// x range, the three scales, statistics from values and from curves, sorting,
// validation and the samples; several series per row, the wrapping axis,
// the tail cut, binned curves and the JSON and CSV round trips. UI-free.
//
// Version: 1.1.0 - Phase 2: series, period, tail cut, binned, JSON / CSV, Sleep
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.h"
#include "DataFormats/UltraCanvasJSON.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

static const double kPi = 3.14159265358979323846;

// The area under ys sampled at xs, by trapezoids.
static double Area(const std::vector<double>& xs, const std::vector<double>& ys) {
    double a = 0.0;
    for (size_t i = 1; i < xs.size(); ++i) a += (xs[i] - xs[i - 1]) * (ys[i] + ys[i - 1]) * 0.5;
    return a;
}

static std::vector<double> Grid(double lo, double hi, int n) {
    std::vector<double> xs(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) xs[static_cast<size_t>(i)] = lo + (hi - lo) * i / (n - 1);
    return xs;
}

static bool HasProblem(const std::vector<std::string>& problems, const std::string& part) {
    for (const std::string& p : problems) if (p.find(part) != std::string::npos) return true;
    return false;
}

static void TestQuantilesAndBandwidths() {
    std::printf("\n[Quantiles and bandwidth rules]\n");
    const std::vector<double> sorted = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    CHECK(Near(RidgelineQuantile(sorted, 0.5), 5.5, 1e-12) && Near(RidgelineQuantile(sorted, 0.25), 3.25, 1e-12) &&
              Near(RidgelineQuantile(sorted, 0.0), 1.0, 1e-12) && Near(RidgelineQuantile(sorted, 1.0), 10.0, 1e-12),
          "type-7 quantiles: median 5.5, first quartile 3.25, the ends");

    const double sigma = std::sqrt(82.5 / 9.0);    // sample deviation of 1..10
    CHECK(Near(RidgelineScottBandwidth(sorted), 1.06 * sigma * std::pow(10.0, -0.2), 1e-12),
          "Scott: 1.06 sigma n^(-1/5)");
    const double iqr = RidgelineQuantile(sorted, 0.75) - RidgelineQuantile(sorted, 0.25);
    CHECK(Near(RidgelineSilvermanBandwidth(sorted), 0.9 * std::min(sigma, iqr / 1.34) * std::pow(10.0, -0.2), 1e-12),
          "Silverman: 0.9 min(sigma, IQR / 1.34) n^(-1/5)");
    CHECK(RidgelineScottBandwidth({4, 4, 4}) == 0.0 && RidgelineSilvermanBandwidth({7}) == 0.0,
          "no spread, no rule bandwidth");
}

static void TestDensity() {
    std::printf("\n[Kernel density]\n");
    const std::vector<double> values = {2.0, 3.0, 3.5, 5.0, 8.0};
    const std::vector<double> xs = Grid(-10.0, 20.0, 3001);
    const std::vector<double> d = RidgelineKernelDensity(values, xs, 0.8);
    CHECK(Near(Area(xs, d), 1.0, 1e-6), "the density's area is 1");

    const std::vector<double> one = RidgelineKernelDensity({4.0}, {4.0, 4.5}, 0.5);
    CHECK(Near(one[0], 1.0 / (0.5 * std::sqrt(2.0 * kPi)), 1e-12) &&
              Near(one[1], std::exp(-0.5) / (0.5 * std::sqrt(2.0 * kPi)), 1e-12),
          "one value: a Gaussian of the bandwidth around it");

    // Values near a lower bound of 0: reflected, nothing below it.
    const std::vector<double> nearZero = {0.0, 0.0, 0.2, 0.5, 1.0, 1.5};
    const std::vector<double> xs0 = Grid(-2.0, 12.0, 2801);
    const std::vector<double> bounded = RidgelineKernelDensity(nearZero, xs0, 0.6, 0.0, 10.0);
    bool noneBelow = true;
    for (size_t i = 0; i < xs0.size(); ++i) if (xs0[i] < 0.0 && bounded[i] != 0.0) noneBelow = false;
    CHECK(noneBelow, "a bounded density is 0 outside its bounds");
    // Integrated from the bound itself: a trapezoid across the jump at 0
    // would add half a step's worth of the edge.
    const std::vector<double> inside = Grid(0.0, 12.0, 2401);
    CHECK(Near(Area(inside, RidgelineKernelDensity(nearZero, inside, 0.6, 0.0, 10.0)), 1.0, 1e-4),
          "and still has area 1: the mass below 0 is reflected back");
    const std::vector<double> open = RidgelineKernelDensity(nearZero, {0.0}, 0.6);
    const std::vector<double> atZero = RidgelineKernelDensity(nearZero, {0.0}, 0.6, 0.0, 10.0);
    CHECK(atZero[0] > open[0] * 1.5, "so the density at the bound stands taller than unbounded");

    // A large row is binned: the result stays within 1 % of the exact sum.
    std::vector<double> many;
    for (int i = 0; i < 5000; ++i) many.push_back(5.0 + 2.0 * std::sin(i * 0.37) + 0.5 * std::cos(i * 1.3));
    const std::vector<double> xsSmall = Grid(0.0, 10.0, 16);   // grid of 64 bins: binned
    const std::vector<double> binned = RidgelineKernelDensity(many, xsSmall, 0.4);
    double worst = 0.0, peak = 0.0;
    for (size_t i = 0; i < xsSmall.size(); ++i) {
        double exact = 0.0;
        for (double v : many) {
            const double z = (xsSmall[i] - v) / 0.4;
            exact += std::exp(-0.5 * z * z);
        }
        exact /= 5000.0 * 0.4 * std::sqrt(2.0 * kPi);
        worst = std::max(worst, std::abs(exact - binned[i]));
        peak = std::max(peak, exact);
    }
    CHECK(worst < peak * 0.01, "5,000 values, binned: within 1 % of the exact density");
    CHECK(RidgelineKernelDensity({}, xs, 1.0)[0] == 0.0 && RidgelineKernelDensity(values, xs, 0.0)[0] == 0.0,
          "no values or no bandwidth: all zero");
}

static void TestBandwidthAndRange() {
    std::printf("\n[The model's bandwidth and x range]\n");
    UltraCanvasRidgelineModel m;
    m.rows.push_back({"a", {1, 2, 3, 4, 5}, {}, Color(0, 0, 0, 0), ""});
    m.rows.push_back({"b", {10, 12, 14, 16, 30}, {}, Color(0, 0, 0, 0), ""});
    m.rows.push_back({"c", {6, 6, 6}, {}, Color(0, 0, 0, 0), ""});
    const double ha = RidgelineScottBandwidth(m.rows[0].values), hb = RidgelineScottBandwidth(m.rows[1].values);
    CHECK(Near(m.Bandwidth(0), ha, 1e-12) && Near(m.Bandwidth(1), hb, 1e-12), "each row its own by default");
    CHECK(Near(m.Bandwidth(2), (ha + hb) / 2.0, 1e-12), "a row of one value takes the rows' mean");
    m.density.sharedBandwidth = true;
    CHECK(Near(m.Bandwidth(0), (ha + hb) / 2.0, 1e-12) && Near(m.Bandwidth(1), m.Bandwidth(0), 1e-12),
          "shared: the mean for every row");
    m.density.adjust = 2.0;
    CHECK(Near(m.Bandwidth(1), ha + hb, 1e-12), "adjust multiplies it");
    m.density.rule = RidgelineBandwidth::Fixed;
    m.density.bandwidth = 0.75;
    m.density.adjust = 1.0;
    CHECK(Near(m.Bandwidth(2), 0.75, 1e-12), "Fixed: the given width");

    const auto range = m.XRange();
    CHECK(Near(range.first, 1.0 - 3 * 0.75, 1e-12) && Near(range.second, 30.0 + 3 * 0.75, 1e-12),
          "x range: the data, three bandwidths wider");
    m.density.lowerBound = 0.0;
    m.density.upperBound = 25.0;
    CHECK(Near(m.XRange().first, 0.0, 1e-12) && Near(m.XRange().second, 25.0, 1e-12), "clamped to the bounds");
    m.xMin = -5.0;
    m.xMax = 40.0;
    CHECK(Near(m.XRange().first, -5.0, 1e-12) && Near(m.XRange().second, 40.0, 1e-12), "xMin / xMax win");
    const std::vector<RidgelineCurvePoint> curve = m.RawCurve(0);
    CHECK(curve.size() == 256 && Near(curve.front().x, 0.0, 1e-12) && Near(curve.back().x, 25.0, 1e-12),
          "a density is evaluated at `resolution` points, inside the bounds");
}

static UltraCanvasRidgelineModel ThreeCurves() {
    // Three supplied curves: a tall narrow one, a low wide one, a medium one.
    UltraCanvasRidgelineModel m;
    auto gauss = [](double centre, double width, double height) {
        std::vector<RidgelineCurvePoint> c;
        for (int i = 0; i <= 200; ++i) {
            const double x = i * 0.1;
            const double z = (x - centre) / width;
            c.push_back({x, height * std::exp(-0.5 * z * z)});
        }
        return c;
    };
    m.rows.push_back({"narrow", {}, gauss(5.0, 0.5, 8.0), Color(0, 0, 0, 0), ""});
    m.rows.push_back({"wide", {}, gauss(12.0, 3.0, 2.0), Color(0, 0, 0, 0), ""});
    m.rows.push_back({"medium", {}, gauss(9.0, 1.0, 4.0), Color(0, 0, 0, 0), ""});
    return m;
}

static double PeakOf(const std::vector<RidgelineCurvePoint>& c) {
    double p = 0.0;
    for (const auto& q : c) p = std::max(p, q.y);
    return p;
}

static double AreaOf(const std::vector<RidgelineCurvePoint>& c) {
    double a = 0.0;
    for (size_t i = 1; i < c.size(); ++i) a += (c[i].x - c[i - 1].x) * (c[i].y + c[i - 1].y) * 0.5;
    return a;
}

static void TestScales() {
    std::printf("\n[Scales]\n");
    UltraCanvasRidgelineModel m = ThreeCurves();
    m.scale = RidgelineScale::Shared;
    auto curves = m.Curves();
    CHECK(Near(PeakOf(curves[0]), 1.0, 1e-12) && Near(PeakOf(curves[1]), 0.25, 1e-9) && Near(PeakOf(curves[2]), 0.5, 1e-9),
          "Shared: the tallest is 1, the others keep their ratio (0.25, 0.5)");
    m.scale = RidgelineScale::RowPeak;
    curves = m.Curves();
    CHECK(Near(PeakOf(curves[0]), 1.0, 1e-12) && Near(PeakOf(curves[1]), 1.0, 1e-12) && Near(PeakOf(curves[2]), 1.0, 1e-12),
          "RowPeak: every row to 1");
    m.scale = RidgelineScale::RowArea;
    curves = m.Curves();
    CHECK(Near(AreaOf(curves[0]), AreaOf(curves[1]), 1e-6) && Near(AreaOf(curves[1]), AreaOf(curves[2]), 1e-6),
          "RowArea: equal areas");
    double tallest = 0.0;
    for (const auto& c : curves) tallest = std::max(tallest, PeakOf(c));
    CHECK(Near(tallest, 1.0, 1e-12), "and the tallest of them is 1");
    CHECK(m.Curve(1).size() == curves[1].size() && Near(m.Curve(1)[60].y, curves[1][60].y, 1e-12),
          "Curve(row) is the row of Curves()");
}

static void TestStatistics() {
    std::printf("\n[Statistics]\n");
    UltraCanvasRidgelineModel m;
    m.rows.push_back({"v", {4, 8, 1, 2, 10, 5, 7}, {}, Color(0, 0, 0, 0), ""});
    const RidgelineRowStatistics s = m.Statistics(0);
    double ss = 0.0;
    for (double v : m.rows[0].values) ss += (v - 37.0 / 7.0) * (v - 37.0 / 7.0);
    CHECK(s.valid && s.count == 7 && Near(s.mean, 37.0 / 7.0, 1e-12), "values: count and mean");
    CHECK(Near(s.standardDeviation, std::sqrt(ss / 6.0), 1e-12), "sample standard deviation");
    CHECK(Near(s.median, 5.0, 1e-12) && Near(s.quartile1, 3.0, 1e-12) && Near(s.quartile3, 7.5, 1e-12),
          "median and quartiles");
    CHECK(s.minimum == 1.0 && s.maximum == 10.0, "range");

    UltraCanvasRidgelineModel c = ThreeCurves();
    const RidgelineRowStatistics sc = c.Statistics(2);
    CHECK(sc.valid && sc.count == 0, "a supplied curve: no count");
    CHECK(Near(sc.mean, 9.0, 1e-3) && Near(sc.median, 9.0, 0.02) && Near(sc.peakAt, 9.0, 1e-9),
          "its mean, median and peak at the bump's centre");
    CHECK(Near(sc.standardDeviation, 1.0, 0.01), "its spread is the bump's width");
    CHECK(Near(sc.quartile3 - sc.quartile1, 1.349, 0.03), "its quartiles 1.35 widths apart, as a normal's");
    CHECK(!c.Statistics(9).valid, "a row that does not exist: invalid");
}

static void TestSortAndValidate() {
    std::printf("\n[Sorting and validation]\n");
    UltraCanvasRidgelineModel m;
    m.rows.push_back({"middle", {4, 5, 6}, {}, Color(0, 0, 0, 0), ""});
    m.rows.push_back({"empty", {}, {}, Color(0, 0, 0, 0), ""});
    m.rows.push_back({"high", {8, 9, 10}, {}, Color(0, 0, 0, 0), ""});
    m.rows.push_back({"low", {0, 1, 2}, {}, Color(0, 0, 0, 0), ""});
    m.Sort(RidgelineSort::ByMean);
    CHECK(m.rows[0].name == "low" && m.rows[1].name == "middle" && m.rows[2].name == "high" && m.rows[3].name == "empty",
          "by mean, rising; a row without data last");
    m.Sort(RidgelineSort::ByMean, true);
    CHECK(m.rows[0].name == "high" && m.rows[3].name == "empty", "falling, the empty row still last");
    m.Sort(RidgelineSort::ByName);
    CHECK(m.rows[0].name == "high" && m.rows[1].name == "low" && m.rows[2].name == "middle", "by name");

    const std::vector<std::string> problems = m.Validate();
    CHECK(HasProblem(problems, "\"empty\" has no values"), "Validate names the row without data");
    m.rows.pop_back();
    m.density.lowerBound = 3.0;
    CHECK(HasProblem(m.Validate(), "outside the bounds"), "values outside the bounds");
    m.density.lowerBound.reset();
    m.density.rule = RidgelineBandwidth::Fixed;
    m.density.bandwidth = 0.2;
    CHECK(HasProblem(m.Validate(), "whole numbers"), "a narrow bandwidth over whole numbers is warned of");
    m.density.bandwidth = 0.0;
    CHECK(HasProblem(m.Validate(), "fixed bandwidth must be above 0"), "a fixed bandwidth of 0");
    m.density.bandwidth = 0.8;
    CHECK(m.Validate().empty(), "otherwise fine");
    UltraCanvasRidgelineModel backwards;
    backwards.rows.push_back({"b", {}, {{2, 1}, {1, 1}}, Color(0, 0, 0, 0), ""});
    CHECK(HasProblem(backwards.Validate(), "must increase"), "a curve whose x goes back");
}

static void TestSamples() {
    std::printf("\n[Samples]\n");
    const UltraCanvasRidgelineModel words = UltraCanvasRidgelineModel::WordRatings();
    CHECK(words.rows.size() == 33 && words.Validate().empty(), "WordRatings: 33 words, valid");
    bool inScale = true, rising = true;
    double previous = -1.0;
    for (size_t i = 0; i < words.rows.size(); ++i) {
        for (double v : words.rows[i].values) if (v < 0.0 || v > 10.0 || v != std::floor(v)) inScale = false;
        const double mean = words.Statistics(i).mean;
        if (mean < previous) rising = false;
        previous = mean;
    }
    CHECK(inScale, "whole-number answers from 0 to 10");
    CHECK(rising, "sorted by the mean, the most negative word at the top");
    CHECK(words.Statistics(0).mean < 2.0 && words.Statistics(words.rows.size() - 1).mean > 8.0,
          "the ends are clearly negative and clearly positive");

    const UltraCanvasRidgelineModel day = UltraCanvasRidgelineModel::DailyActivities();
    CHECK(day.rows.size() == 16 && day.Validate().empty(), "DailyActivities: 16 activities, valid");
    bool latestFirst = true, allPeakOne = true;
    const auto curves = day.Curves();
    for (size_t i = 0; i < day.rows.size(); ++i) {
        if (i > 0 && day.Statistics(i).peakAt > day.Statistics(i - 1).peakAt) latestFirst = false;
        if (!Near(PeakOf(curves[i]), 1.0, 1e-12)) allPeakOne = false;
    }
    CHECK(latestFirst, "sorted by when they peak, the latest at the top");
    CHECK(allPeakOne, "each to its own peak");
    CHECK(Near(day.XRange().first, 3.0, 1e-12) && Near(day.XRange().second, 27.0, 1e-12), "from 03:00 to 03:00");
}

static void TestSeries() {
    std::printf("\n[Several series per row]\n");
    UltraCanvasRidgelineModel m;
    m.series = {{"before", Color(0, 0, 0, 0)}, {"after", Color(0, 0, 0, 0)}};
    RidgelineRow r;
    r.name = "a";
    r.SetValues(0, {1, 2, 2, 3, 3, 3, 4});
    r.SetValues(1, {6, 7, 7, 8});
    m.rows.push_back(r);
    CHECK(m.SeriesCount() == 2 && m.rows[0].HasValuesOf(1) && !m.rows[0].HasDataOf(2), "two series in a row");
    CHECK(Near(m.Statistics(0, 1).mean, 7.0, 1e-12) && m.Statistics(0, 1).count == 4, "each series has its own statistics");
    RidgelineRow tall, low;
    tall.name = "tall";
    tall.SetCurve(0, {{0, 0}, {1, 4}, {2, 0}});
    tall.SetCurve(1, {{0, 0}, {1, 2}, {2, 0}});
    UltraCanvasRidgelineModel c;
    c.series = m.series;
    c.rows.push_back(tall);
    c.scale = RidgelineScale::RowPeak;
    const auto all = c.AllCurves();
    CHECK(Near(PeakOf(all[0][0]), 1.0, 1e-12) && Near(PeakOf(all[0][1]), 0.5, 1e-12),
          "RowPeak: a row's series keep their ratio to the row's tallest");
    CHECK(Near(PeakOf(c.Curves(1)[0]), 0.5, 1e-12), "Curves(series) is that series of AllCurves()");
    m.series.resize(1);
    CHECK(HasProblem(m.Validate(), "more series than the model names"), "a row with more series than named");
}

static void TestWrap() {
    std::printf("\n[A wrapping axis]\n");
    UltraCanvasRidgelineModel m;
    m.xMin = 18.0;
    m.period = 24.0;
    CHECK(Near(m.Wrap(0.5), 24.5, 1e-12) && Near(m.Wrap(18.0), 18.0, 1e-12) && Near(m.Wrap(42.0), 18.0, 1e-12) &&
              Near(m.Wrap(-1.0), 23.0, 1e-12),
          "values move by whole periods into 18..42");
    m.rows.push_back({"night", {23.5, 23.9, 0.2, 0.4, 0.5, 1.0, 17.9, 18.1}, {}, Color(0, 0, 0, 0), ""});
    CHECK(Near(m.XRange().first, 18.0, 1e-12) && Near(m.XRange().second, 42.0, 1e-12), "the range is one period");
    const RidgelineRowStatistics s = m.Statistics(0);
    CHECK(s.minimum >= 18.0 && s.maximum < 42.0 && s.median > 24.0 && s.median < 25.0,
          "statistics on the wrapped values: the median just after midnight");
    m.density.rule = RidgelineBandwidth::Fixed;
    m.density.bandwidth = 1.0;
    const auto curve = m.RawCurve(0);
    std::vector<double> xs, ys;
    for (const auto& p : curve) { xs.push_back(p.x); ys.push_back(p.y); }
    CHECK(Near(Area(xs, ys), 1.0, 1e-3), "the circular density has area 1 over the period");
    CHECK(Near(curve.front().y, curve.back().y, 1e-9) && curve.front().y > 0.01,
          "and meets itself at the ends: 17:59 and 18:01 are neighbours");
    UltraCanvasRidgelineModel c;
    c.xMin = 0.0;
    c.period = 10.0;
    c.rows.push_back({"c", {}, {{8, 1}, {9, 2}, {11, 3}, {12, 1}}, Color(0, 0, 0, 0), ""});
    const auto wrapped = c.RawCurve(0);
    CHECK(wrapped.size() == 4 && Near(wrapped[0].x, 1.0, 1e-12) && Near(wrapped[3].x, 9.0, 1e-12),
          "a supplied curve is wrapped and put back in order");
}

static void TestTailsAndBins() {
    std::printf("\n[Tail cut and binned curves]\n");
    UltraCanvasRidgelineModel m;
    m.rows.push_back({"t", {4, 5, 5, 5, 6}, {}, Color(0, 0, 0, 0), ""});
    m.xMin = 0.0;
    m.xMax = 10.0;
    const auto full = m.RawCurve(0);
    m.density.tailCut = 0.05;
    const auto cut = m.RawCurve(0);
    double peak = 0.0;
    for (const auto& p : cut) peak = std::max(peak, p.y);
    bool interiorAbove = true;
    for (size_t i = 1; i + 1 < cut.size(); ++i) if (cut[i].y < 0.05 * peak) interiorAbove = false;
    CHECK(cut.size() < full.size() && cut.front().x > 0.0 && cut.back().x < 10.0, "the tails below 5 % are dropped");
    CHECK(interiorAbove && cut.front().y < 0.05 * peak, "keeping one point each side, so the curve meets its baseline");

    UltraCanvasRidgelineModel b;
    b.rows.push_back({"b", {0.5, 1.5, 1.6, 1.7, 9.9}, {}, Color(0, 0, 0, 0), ""});
    b.xMin = 0.0;
    b.xMax = 10.0;
    b.density.binned = true;
    b.density.bins = 10;
    const auto steps = b.RawCurve(0);
    CHECK(steps.size() == 20 && Near(steps[2].x, 1.0, 1e-12) && Near(steps[3].x, 2.0, 1e-12) &&
              Near(steps[2].y, 3.0 / 5.0, 1e-12),
          "binned: ten flat tops, three of five values in 1..2 (height 0.6)");
    std::vector<double> sx, sy;
    for (const auto& p : steps) { sx.push_back(p.x); sy.push_back(p.y); }
    CHECK(Near(Area(sx, sy), 1.0, 1e-12), "with area 1, as the density");
    CHECK(Near(b.Statistics(0).peakAt, 1.5, 1e-12), "the peak is the tallest bin's middle");
    b.density.bins = 0;
    CHECK(HasProblem(b.Validate(), "Bins must be"), "bins of 0 are refused");
}

static bool SameRows(const UltraCanvasRidgelineModel& a, const UltraCanvasRidgelineModel& b, double tol) {
    if (a.rows.size() != b.rows.size()) return false;
    for (size_t i = 0; i < a.rows.size(); ++i) {
        if (a.rows[i].name != b.rows[i].name) return false;
        for (size_t s = 0; s < a.SeriesCount(); ++s) {
            const auto& va = a.rows[i].ValuesOf(s);
            const auto& vb = b.rows[i].ValuesOf(s);
            if (va.size() != vb.size()) return false;
            for (size_t k = 0; k < va.size(); ++k) if (!Near(va[k], vb[k], tol)) return false;
            const auto& ca = a.rows[i].CurveOf(s);
            const auto& cb = b.rows[i].CurveOf(s);
            if (ca.size() != cb.size()) return false;
            for (size_t k = 0; k < ca.size(); ++k) {
                if (!Near(ca[k].x, cb[k].x, tol) || !Near(ca[k].y, cb[k].y, tol)) return false;
            }
        }
    }
    return true;
}

static void TestInterchange() {
    std::printf("\n[JSON and CSV]\n");
    UltraCanvasRidgelineModel sleep = UltraCanvasRidgelineModel::Sleep();
    sleep.density.tailCut = 0.02;
    sleep.rows[0].note = "teenagers";
    sleep.series[1].color = Color(230, 120, 30, 255);
    const std::string json = ExportRidgelineJson(sleep);
    UltraCanvasRidgelineModel back;
    std::string error;
    CHECK(ImportRidgelineJson(json, back, &error), "the Sleep sample survives a JSON round trip");
    CHECK(SameRows(sleep, back, 0.0) && back.rows[0].note == "teenagers", "rows, values and notes alike");
    CHECK(back.series.size() == 2 && back.series[1].name == "Wakes up" && back.series[1].color.r == 230,
          "series names and colours");
    CHECK(back.period && *back.period == 24.0 && back.xMin && *back.xMin == 18.0 && back.density.tailCut == 0.02 &&
              back.title == sleep.title,
          "period, start, tail cut and titles");
    const UltraCanvasRidgelineModel day = UltraCanvasRidgelineModel::DailyActivities();
    UltraCanvasRidgelineModel dayBack;
    CHECK(ImportRidgelineJson(ExportRidgelineJson(day, false), dayBack, &error) && SameRows(day, dayBack, 0.0) &&
              dayBack.scale == RidgelineScale::RowPeak,
          "curves and the scale too");

    UltraCanvasRidgelineModel untouched = UltraCanvasRidgelineModel::WordRatings();
    const size_t before = untouched.rows.size();
    CHECK(!ImportRidgelineJson("{\"format\": \"ultracanvas-ternary\", \"version\": 1}", untouched, &error) &&
              error.find("not a ridgeline chart") != std::string::npos && untouched.rows.size() == before,
          "another format is refused, the model untouched");
    CHECK(!ImportRidgelineJson("{\"format\": \"ultracanvas-ridgeline\", \"version\": 9}", untouched, &error) &&
              error.find("version 9") != std::string::npos,
          "a newer version is refused");
    CHECK(!ImportRidgelineJson("{\"format\": \"ultracanvas-ridgeline\", \"version\": 1, \"scale\": \"wide\", "
                               "\"rows\": [{\"name\": \"a\", \"values\": [1, 2]}]}",
                               untouched, &error) &&
              error.find("unknown scale") != std::string::npos,
          "an unknown scale is refused");
    CHECK(!ImportRidgelineJson("{\"format\": \"ultracanvas-ridgeline\", \"version\": 1, "
                               "\"rows\": [{\"name\": \"a\", \"values\": [1, \"x\"]}]}",
                               untouched, &error) &&
              error.find("not a list of numbers") != std::string::npos,
          "a value that is not a number is refused");

    // CSV: the data only; the other settings stay as the model has them.
    const std::string csv = ExportRidgelineCsv(sleep);
    CHECK(csv.rfind("row,series,value\r\n", 0) == 0 || csv.rfind("row,series,value\n", 0) == 0,
          "values with two series: row, series, value");
    UltraCanvasRidgelineModel fromCsv;
    fromCsv.period = 24.0;
    fromCsv.series = {{"Goes to bed", Color(10, 20, 30, 255)}};
    CHECK(ImportRidgelineCsv(csv, fromCsv, &error), "the CSV reads back");
    CHECK(SameRows(sleep, fromCsv, 1e-9) && fromCsv.series.size() == 2 && fromCsv.series[0].color.r == 10 &&
              fromCsv.period && *fromCsv.period == 24.0,
          "rows and series alike; a known series keeps its colour; the period stays");
    UltraCanvasRidgelineModel dayCsv;
    CHECK(ImportRidgelineCsv(ExportRidgelineCsv(day), dayCsv, &error) && SameRows(day, dayCsv, 1e-9) &&
              dayCsv.series.empty(),
          "curves: row, x, y, and one series stays unnamed");
    CHECK(ImportRidgelineCsv("group,value\nA,1\nA,2.5\nB,3\n", dayCsv, &error) && dayCsv.rows.size() == 2 &&
              dayCsv.rows[0].values.size() == 2 && Near(dayCsv.rows[0].values[1], 2.5, 1e-12),
          "the row column may be called group");

    UltraCanvasRidgelineModel keep = UltraCanvasRidgelineModel::WordRatings();
    CHECK(!ImportRidgelineCsv("label,value\nA,1\n", keep, &error) && error.find("no row column") != std::string::npos,
          "no row column: refused");
    CHECK(!ImportRidgelineCsv("row,amount\nA,1\n", keep, &error) && error.find("neither a value") != std::string::npos,
          "no value column and no x, y: refused");
    CHECK(!ImportRidgelineCsv("row,value\nA,1\nB,one\n", keep, &error) && error.find("line 3") != std::string::npos,
          "a word where a number goes: refused, naming the line");
    CHECK(!ImportRidgelineCsv("row,value\nA,1,2\n", keep, &error) && error.find("fields") != std::string::npos &&
              keep.rows.size() == 33,
          "a line with too many fields: refused, the model untouched");
}

static void TestSleepSample() {
    std::printf("\n[The Sleep sample]\n");
    const UltraCanvasRidgelineModel m = UltraCanvasRidgelineModel::Sleep();
    CHECK(m.rows.size() == 7 && m.SeriesCount() == 2 && m.Validate().empty(), "seven age groups, two series, valid");
    const RidgelineRowStatistics bed = m.Statistics(1, 0), wake = m.Statistics(1, 1);
    CHECK(bed.median > 24.0 && bed.median < 25.5, "18-24: bedtime after midnight, on the axis past 24:00");
    CHECK(wake.median > 32.0 && wake.median < 33.5, "and waking about half past eight the next morning");
    CHECK(m.Statistics(6, 0).median < m.Statistics(1, 0).median, "the oldest go to bed earlier than the young");
}

int main() {
    std::printf("====================================\n");
    std::printf("  UltraCanvas Ridgeline Model\n");
    std::printf("====================================\n");
    TestQuantilesAndBandwidths();
    TestDensity();
    TestBandwidthAndRange();
    TestScales();
    TestStatistics();
    TestSortAndValidate();
    TestSamples();
    TestSeries();
    TestWrap();
    TestTailsAndBins();
    TestInterchange();
    TestSleepSample();
    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
