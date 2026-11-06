// Tests/ControlLimitsTest.cpp
// Unit tests for the statistical process control helper: the constants and
// chart factors against the published tables, X-bar-R and I-MR limits on
// hand-checked data, the within-subgroup sigma on a shifted process, and
// every Western Electric rule on its minimal pattern and a near miss. UI-free.
//
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasControlLimits.h"

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

// A series in standard units: centre 0, sigma 1, limits at +-3, so each value
// is its own z-score.
static ControlSeries Standard(const std::vector<double>& z) {
    ControlSeries s;
    s.name = "z";
    s.value = z;
    s.centre.assign(z.size(), 0.0);
    s.sigma.assign(z.size(), 1.0);
    s.upper.assign(z.size(), 3.0);
    s.lower.assign(z.size(), -3.0);
    return s;
}

static bool Flags(const std::vector<ControlViolation>& found, size_t point, int rule) {
    for (const ControlViolation& v : found) {
        if (v.point == point && v.rule == rule) return true;
    }
    return false;
}

static size_t CountRule(const std::vector<ControlViolation>& found, int rule) {
    size_t n = 0;
    for (const ControlViolation& v : found) n += (v.rule == rule) ? 1 : 0;
    return n;
}

static ControlOptions Only(int rule) {
    ControlOptions options;
    options.rulesEnabled = {rule == 1, rule == 2, rule == 3, rule == 4};
    return options;
}

static void TestConstants() {
    std::printf("\n[Constants against the published tables]\n");
    // n:            2      3      4      5      6      7      8      9      10
    const double a2[] = {1.880, 1.023, 0.729, 0.577, 0.483, 0.419, 0.373, 0.337, 0.308};
    const double d3f[] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.076, 0.136, 0.184, 0.223};
    const double d4f[] = {3.267, 2.574, 2.282, 2.114, 2.004, 1.924, 1.864, 1.816, 1.777};
    const double c4[] = {0.7979, 0.8862, 0.9213, 0.9400, 0.9515, 0.9594, 0.9650, 0.9693, 0.9727};
    bool a2ok = true, d3ok = true, d4ok = true, c4ok = true;
    for (int n = 2; n <= 10; ++n) {
        const size_t k = static_cast<size_t>(n - 2);
        // The tables round to three decimals (some round 2.5746 down).
        a2ok = a2ok && Near(ControlA2(n), a2[k], 0.001);
        d3ok = d3ok && Near(ControlRangeLowerFactor(n), d3f[k], 0.001);
        d4ok = d4ok && Near(ControlRangeUpperFactor(n), d4f[k], 0.001);
        c4ok = c4ok && Near(ControlC4(n), c4[k], 0.00006);
    }
    CHECK(a2ok, "A2 = 3 / (d2 sqrt n) matches the table for n = 2..10");
    CHECK(d3ok, "the range chart's D3 matches the table (0 up to n = 6)");
    CHECK(d4ok, "the range chart's D4 matches the table");
    CHECK(c4ok, "c4 from its closed form matches the table");
    CHECK(Near(ControlRangeLowerFactor(25), 0.459, 0.001) && Near(ControlRangeUpperFactor(25), 1.541, 0.001),
          "and at n = 25, the end of the table");
    CHECK(Near(ControlC4(25), 0.9896, 0.0001), "c4(25) = 0.9896");
    CHECK(Near(ControlD2(2), 1.128, 0.0005) && Near(ControlD2(5), 2.326, 0.0005) &&
              Near(ControlD3(2), 0.853, 0.0005) && Near(ControlD2(25), 3.931, 0.0005),
          "d2 and d3 round to the published three-decimal values");
    CHECK(std::isnan(ControlD2(1)) && std::isnan(ControlD2(26)) && std::isnan(ControlC4(1)),
          "outside n = 2..25 the constants are NaN");
}

static void TestXbarR() {
    std::printf("\n[X-bar-R on hand-checked data]\n");
    // Three subgroups of four, each with range 3: X-double-bar 11.5, R-bar 3.
    const std::vector<ControlSubgroup> data = {
        {"A", {10, 12, 11, 13}}, {"B", {9, 11, 10, 12}}, {"C", {11, 13, 12, 14}}};
    const ControlResult r = ComputeControlChart(data);
    CHECK(r.kind == ControlChartKind::XbarR && r.subgroupSize == 4, "n = 4");
    CHECK(r.labels.size() == 3 && r.labels[1] == "B", "the subgroups' labels are kept");
    CHECK(r.location.value.size() == 3 && Near(r.location.value[0], 11.5, 1e-12) &&
              Near(r.location.value[1], 10.5, 1e-12),
          "the location pane plots the subgroup means");
    CHECK(Near(r.location.centre[0], 11.5, 1e-12), "CL = X-double-bar = 11.5");
    CHECK(Near(r.location.upper[0], 11.5 + 0.729 * 3.0, 0.002) &&
              Near(r.location.lower[0], 11.5 - 0.729 * 3.0, 0.002),
          "UCL / LCL = X-double-bar +- A2 R-bar (13.686 / 9.314)");
    CHECK(Near(r.location.upper[0] - r.location.centre[0], ControlA2(4) * 3.0, 1e-9),
          "exactly A2 R-bar from the centre");
    CHECK(Near(r.sigmaWithin, 3.0 / ControlD2(4), 1e-12) && Near(r.sigmaWithin, 3.0 / 2.059, 0.001),
          "sigma within = R-bar / d2");
    CHECK(r.dispersion.has_value() && r.dispersion->name == "Range" &&
              Near(r.dispersion->value[2], 3.0, 1e-12),
          "the dispersion pane plots the ranges");
    CHECK(Near(r.dispersion->centre[0], 3.0, 1e-12) &&
              Near(r.dispersion->upper[0], 2.282 * 3.0, 0.003),
          "R chart: CL = R-bar, UCL = D4 R-bar");
    CHECK(r.dispersion->lowerClamped && r.dispersion->lower[0] == 0.0,
          "for n = 4 the R chart's lower limit is clamped at zero (none)");
    CHECK(r.violations.empty(), "three ordinary subgroups break no rule");
    bool preliminary = false;
    for (const std::string& w : r.warnings) preliminary = preliminary || w.find("preliminary") != std::string::npos;
    CHECK(preliminary, "fewer than 20 subgroups are reported as preliminary");

    ControlResult r8 = ComputeControlChart({{"", {1, 2, 3, 4, 5, 6, 7, 8}}, {"", {2, 3, 4, 5, 6, 7, 8, 9}}});
    CHECK(r8.labels[0] == "1" && r8.labels[1] == "2", "unnamed subgroups are numbered from 1");
    CHECK(!r8.dispersion->lowerClamped && Near(r8.dispersion->lower[0], 0.136 * 7.0, 0.002),
          "for n = 8 the R chart has a real lower limit (D3 R-bar)");

    const ControlResult uneven = ComputeControlChart(
        {{"", {1, 2, 3}}, {"", {2, 3, 4}}, {"", {1, 2, 3, 4, 5}}, {"", {7}}});
    bool differ = false, leftOut = false;
    for (const std::string& w : uneven.warnings) {
        differ = differ || w.find("differ") != std::string::npos;
        leftOut = leftOut || w.find("left out") != std::string::npos;
    }
    CHECK(uneven.subgroupSize == 3 && differ, "unequal sizes: the most common n is used, with a warning");
    CHECK(leftOut && std::isnan(uneven.location.value[3]), "a subgroup of one value is left out and not plotted");

    CHECK(ComputeControlChart({}).location.value.empty(), "no data, no limits");
}

static void TestShiftedProcess() {
    std::printf("\n[Within-subgroup sigma on a shifted process]\n");
    // 20 subgroups of five, the same spread in each; the mean steps from 10.00
    // to 10.05 halfway. The overall standard deviation includes the step.
    std::vector<ControlSubgroup> data;
    std::vector<double> all;
    for (int i = 0; i < 20; ++i) {
        const double mean = i < 10 ? 10.00 : 10.05;
        ControlSubgroup g;
        for (int k = -2; k <= 2; ++k) {
            g.values.push_back(mean + 0.01 * k);
            all.push_back(mean + 0.01 * k);
        }
        data.push_back(g);
    }
    const ControlResult r = ComputeControlChart(data);
    double mean = 0.0;
    for (double v : all) mean += v;
    mean /= static_cast<double>(all.size());
    double ss = 0.0;
    for (double v : all) ss += (v - mean) * (v - mean);
    const double overall = std::sqrt(ss / static_cast<double>(all.size() - 1));
    CHECK(Near(r.sigmaWithin, 0.04 / ControlD2(5), 1e-12) && Near(r.sigmaWithin, 0.04 / 2.326, 1e-5),
          "sigma within = R-bar / d2 = 0.04 / 2.326");
    CHECK(r.sigmaWithin < overall * 0.75, "and is well below the overall SD, which the step inflates");
    size_t flagged = 0;
    for (size_t i = 10; i < 20; ++i) flagged += r.RulesAt(i).empty() ? 0 : 1;
    CHECK(flagged == 10 && !r.RulesAt(12).empty() && r.RulesAt(12).front() == 1,
          "so the step is flagged: every mean after it is beyond the UCL (rule 1)");
    const double overallUcl = mean + 3.0 * overall / std::sqrt(5.0);
    CHECK(overallUcl > 10.05, "(limits from the overall SD would have hidden it)");
    bool warned = false;
    for (const std::string& w : r.warnings) warned = warned || w.find("preliminary") != std::string::npos;
    CHECK(!warned, "20 subgroups are not preliminary");
}

static void TestIndividuals() {
    std::printf("\n[I-MR on hand-checked data]\n");
    ControlOptions options;
    options.kind = ControlChartKind::IndividualsMR;
    const ControlResult r = ComputeControlChart(
        {{"", {10}}, {"", {12}}, {"", {11}}, {"", {13}}, {"", {12}}}, options);
    CHECK(r.kind == ControlChartKind::IndividualsMR && r.subgroupSize == 1, "one value per point");
    CHECK(Near(r.location.centre[0], 11.6, 1e-12), "CL = the mean, 11.6");
    CHECK(r.dispersion && std::isnan(r.dispersion->value[0]) && Near(r.dispersion->value[1], 2.0, 1e-12),
          "the first moving range is empty, the next |12 - 10| = 2");
    CHECK(Near(r.dispersion->centre[0], 1.5, 1e-12), "MR-bar = 1.5");
    CHECK(Near(r.sigmaWithin, 1.5 / ControlD2(2), 1e-12), "sigma = MR-bar / d2(2)");
    CHECK(Near(r.location.upper[0], 11.6 + 2.66 * 1.5, 0.004), "UCL = mean + 2.66 MR-bar (2.66 rounds 3 / d2(2))");
    CHECK(Near(r.dispersion->upper[0], 3.267 * 1.5, 0.002) && r.dispersion->lowerClamped,
          "MR chart: UCL = 3.267 MR-bar, no lower limit");

    const ControlResult extra = ComputeControlChart({{"", {1, 99}}, {"", {2}}, {"", {3}}}, options);
    CHECK(Near(extra.location.value[0], 1.0, 1e-12) && !extra.warnings.empty(),
          "further values in a point are ignored, with a warning");
}

static void TestRules() {
    std::printf("\n[Western Electric rules: minimal patterns and near misses]\n");
    CHECK(Flags(FindRuleViolations(Standard({0, 3.1}), Only(1)), 1, 1), "rule 1: a point beyond 3 sigma");
    CHECK(Flags(FindRuleViolations(Standard({-3.2}), Only(1)), 0, 1), "rule 1: below the lower limit too");
    CHECK(FindRuleViolations(Standard({0, 3.0}), Only(1)).empty(), "rule 1: exactly on the limit is not beyond");

    const auto r2 = FindRuleViolations(Standard({2.5, 0, 2.5}), Only(2));
    CHECK(Flags(r2, 2, 2) && r2.size() == 1 && r2.front().firstPoint == 0,
          "rule 2: two of three beyond 2 sigma on one side, flagged on the second, starting at the first");
    CHECK(FindRuleViolations(Standard({2.5, 0, 0, 2.5}), Only(2)).empty(),
          "rule 2: two beyond 2 sigma four points apart do not count");
    CHECK(FindRuleViolations(Standard({2.5, 0, -2.5}), Only(2)).empty(),
          "rule 2: one each side does not count");

    const auto r3 = FindRuleViolations(Standard({1.5, 1.5, 0, 1.5, 1.5}), Only(3));
    CHECK(Flags(r3, 4, 3) && r3.size() == 1 && r3.front().firstPoint == 0,
          "rule 3: four of five beyond 1 sigma on one side");
    CHECK(FindRuleViolations(Standard({1.5, 1.5, 0, 0, 1.5}), Only(3)).empty(), "rule 3: three of five do not");

    const auto r4 = FindRuleViolations(Standard(std::vector<double>(8, 0.5)), Only(4));
    CHECK(r4.size() == 1 && Flags(r4, 7, 4) && r4.front().firstPoint == 0,
          "rule 4: eight in a row on one side, flagged on the eighth");
    CHECK(FindRuleViolations(Standard(std::vector<double>(7, 0.5)), Only(4)).empty(), "rule 4: seven do not");
    CHECK(CountRule(FindRuleViolations(Standard(std::vector<double>(9, -0.5)), Only(4)), 4) == 2,
          "rule 4: a ninth flags again (a long run flags every point from the eighth on)");
    CHECK(FindRuleViolations(Standard({0.5, 0.5, 0.5, 0.5, 0, 0.5, 0.5, 0.5, 0.5}), Only(4)).empty(),
          "rule 4: a point on the centre line ends the run");
    ControlOptions nine = Only(4);
    nine.sameSideRun = 9;
    CHECK(FindRuleViolations(Standard(std::vector<double>(8, 0.5)), nine).empty(),
          "rule 4 with a run of 9: eight do not");

    ControlOptions none;
    none.rulesEnabled = {false, false, false, false};
    CHECK(FindRuleViolations(Standard({4, 4, 4, 4, 4, 4, 4, 4}), none).empty(), "rules switched off flag nothing");

    const auto both = FindRuleViolations(Standard({2.5, 3.5}));
    CHECK(Flags(both, 1, 1) && Flags(both, 1, 2), "a point can complete several rules at once");

    ControlSeries gap = Standard({0.5, 0.5, 0.5, 0.5, std::nan(""), 0.5, 0.5, 0.5, 0.5});
    CHECK(FindRuleViolations(gap, Only(4)).empty(), "a gap ends a run");
    ControlSeries flat = Standard({5});
    flat.sigma[0] = 0.0;
    CHECK(FindRuleViolations(flat).empty(), "no spread, no signal");

    ControlSeries range = Standard({2.5, 2.5, 3.5, -4});
    range.lowerClamped = true;
    range.lower.assign(4, 0.0);
    const auto dispersion = FindRuleViolations(range, ControlOptions(), 1);
    CHECK(dispersion.size() == 1 && Flags(dispersion, 2, 1) && dispersion.front().pane == 1,
          "the dispersion pane checks rule 1 only, and a clamped lower limit is none");

    ControlResult result;
    result.violations = {{3, 1, 2, 0}, {3, 3, 1, 0}, {3, 3, 1, 1}};
    CHECK(result.RulesAt(3) == std::vector<int>({1, 2}) && result.RulesAt(3, 1) == std::vector<int>({1}) &&
              result.RulesAt(2).empty(),
          "RulesAt lists a point's rules per pane");
    CHECK(ControlRuleDescription(4, 9).find("9 points") != std::string::npos &&
              ControlRuleDescription(5).empty(),
          "rule descriptions name the run length");
}

int main() {
    std::printf("====================================\n");
    std::printf("  UltraCanvas Control Limits\n");
    std::printf("====================================\n");

    TestConstants();
    TestXbarR();
    TestShiftedProcess();
    TestIndividuals();
    TestRules();

    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
