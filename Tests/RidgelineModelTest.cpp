// Tests/RidgelineModelTest.cpp
// Unit tests for the ridgeline model: quantiles, the bandwidth rules, the
// kernel density (its area, reflection at bounds, binning of large rows), the
// x range, the three scales, statistics from values and from curves, sorting,
// validation and the two samples. UI-free.
//
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.h"

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
    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
