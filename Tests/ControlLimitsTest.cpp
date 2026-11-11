// Tests/ControlLimitsTest.cpp
// Unit tests for the statistical process control helper: the constants and
// chart factors against the published tables, X-bar-R and I-MR limits on
// hand-checked data, the within-subgroup sigma on a shifted process, and
// every Western Electric rule on its minimal pattern and a near miss. UI-free.
//
// Version: 1.1.0 - Phase 2: X-bar-S with equal and unequal subgroups, the p, np,
//   c and u charts, the Nelson rules, rules on the dispersion pane, stages,
//   excluded points, baselines and capability
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


static ControlOptions Nelson(int rule) {
    ControlOptions options;
    options.ruleSet = ControlRuleSet::Nelson;
    options.rulesEnabled = {};
    options.rulesEnabled[static_cast<size_t>(rule - 1)] = true;
    return options;
}

static ControlSubgroup Lot(double count, double size) {
    ControlSubgroup g;
    g.count = count;
    g.size = size;
    return g;
}

static ControlSubgroup Values(std::vector<double> values) {
    ControlSubgroup g;
    g.values = std::move(values);
    return g;
}

static bool Warned(const ControlResult& r, const char* text) {
    for (const std::string& w : r.warnings) {
        if (w.find(text) != std::string::npos) return true;
    }
    return false;
}

static void TestXbarS() {
    std::printf("\n[X-bar-S]\n");
    // n:            2      3      4      5      6      7      8      9      10
    const double a3[] = {2.659, 1.954, 1.628, 1.427, 1.287, 1.182, 1.099, 1.032, 0.975};
    const double b3[] = {0.0, 0.0, 0.0, 0.0, 0.030, 0.118, 0.185, 0.239, 0.284};
    const double b4[] = {3.267, 2.568, 2.266, 2.089, 1.970, 1.882, 1.815, 1.761, 1.716};
    bool ok = true;
    for (int n = 2; n <= 10; ++n) {
        const size_t k = static_cast<size_t>(n - 2);
        ok = ok && Near(ControlA3(n), a3[k], 0.001) && Near(ControlSigmaLowerFactor(n), b3[k], 0.001) &&
             Near(ControlSigmaUpperFactor(n), b4[k], 0.001);
    }
    CHECK(ok, "A3, B3 and B4 match the published table for n = 2..10");
    CHECK(Near(ControlC4(50), 0.9949, 0.0001) && std::isnan(ControlC4(1)), "c4 is computed for any n >= 2");

    ControlOptions options;
    options.kind = ControlChartKind::XbarS;
    // Three subgroups of four with the same spread: S-bar = sd(10, 11, 12, 13).
    const ControlResult equal = ComputeControlChart({Values({10, 12, 11, 13}), Values({9, 11, 10, 12}),
                                                     Values({11, 13, 12, 14})}, options);
    const double sBar = std::sqrt(5.0 / 3.0);
    CHECK(equal.dispersion && equal.dispersion->name == "Standard deviation" &&
              Near(equal.dispersion->value[0], sBar, 1e-12),
          "the dispersion pane plots each subgroup's standard deviation");
    CHECK(Near(equal.location.upper[0], 11.5 + 1.628 * sBar, 0.002) &&
              Near(equal.location.upper[0] - 11.5, ControlA3(4) * sBar, 1e-9),
          "equal n: UCL = X-double-bar + A3 S-bar");
    CHECK(Near(equal.dispersion->centre[0], sBar, 1e-12) &&
              Near(equal.dispersion->upper[0], ControlSigmaUpperFactor(4) * sBar, 1e-9) &&
              equal.dispersion->lowerClamped,
          "S chart: CL = S-bar, UCL = B4 S-bar, no lower limit for n = 4");
    CHECK(equal.location.constantLimits && Near(equal.sigmaWithin, sBar / ControlC4(4), 1e-12),
          "sigma within = S-bar / c4");

    // Unequal sizes: the pooled standard deviation, unbiased by c4(dof + 1),
    // and limits for each subgroup's own n.
    const ControlResult unequal = ComputeControlChart(
        {Values({1, 2, 3}), Values({2, 4, 6, 8}), Values({5, 6, 7, 8, 9})}, options);
    const double pooled = std::sqrt((2.0 * 1.0 + 3.0 * (20.0 / 3.0) + 4.0 * 2.5) / 9.0);
    const double sigma = pooled / ControlC4(10);
    const double grand = (3.0 * 2.0 + 4.0 * 5.0 + 5.0 * 7.0) / 12.0;
    CHECK(Near(unequal.sigmaWithin, sigma, 1e-12), "unequal n: sigma = pooled SD / c4(dof + 1)");
    CHECK(Near(unequal.location.centre[0], grand, 1e-12), "X-double-bar weighs each mean by its n");
    CHECK(Near(unequal.location.upper[0], grand + 3.0 * sigma / std::sqrt(3.0), 1e-9) &&
              Near(unequal.location.upper[2], grand + 3.0 * sigma / std::sqrt(5.0), 1e-9),
          "each subgroup's limits follow its own n");
    CHECK(Near(unequal.dispersion->centre[2], ControlC4(5) * sigma, 1e-12) && !unequal.location.constantLimits,
          "so do the S chart's, and the limits are not constant");
    CHECK(Warned(unequal, "vary"), "with a note that the sizes vary");
}

static void TestAttributes() {
    std::printf("\n[p, np, c and u charts]\n");
    ControlOptions options;
    options.kind = ControlChartKind::P;
    const ControlResult p = ComputeControlChart({Lot(5, 100), Lot(8, 200), Lot(3, 100), Lot(12, 200)}, options);
    const double pBar = 28.0 / 600.0;
    CHECK(!p.dispersion && p.location.name == "Proportion defective" && Near(p.location.value[1], 0.04, 1e-12),
          "p: one pane, the proportion defective per lot");
    CHECK(Near(p.location.centre[0], pBar, 1e-12), "p-bar = all defectives / all items (28 / 600)");
    CHECK(Near(p.location.upper[0], pBar + 3.0 * std::sqrt(pBar * (1 - pBar) / 100.0), 1e-12) &&
              Near(p.location.upper[1], pBar + 3.0 * std::sqrt(pBar * (1 - pBar) / 200.0), 1e-12),
          "each lot's limits follow its size");
    CHECK(!p.location.HasLowerLimit(0) && p.location.HasLowerLimit(1) && !p.location.constantLimits &&
              !p.location.lowerClamped,
          "a lower limit below zero is none, for that lot only");

    options.kind = ControlChartKind::NP;
    const ControlResult np = ComputeControlChart({Lot(2, 50), Lot(3, 50), Lot(1, 50), Lot(4, 50)}, options);
    CHECK(Near(np.location.centre[0], 2.5, 1e-12) &&
              Near(np.location.upper[0], 2.5 + 3.0 * std::sqrt(50 * 0.05 * 0.95), 1e-12) &&
              np.location.lowerClamped && np.subgroupSize == 50,
          "np: CL = n p-bar, UCL = n p-bar + 3 sqrt(n p-bar (1 - p-bar)), no lower limit");
    options.defaultSize = 50;
    const ControlResult npDefault = ComputeControlChart({Lot(2, 0), Lot(3, 0)}, options);
    CHECK(Near(npDefault.location.centre[0], 2.5, 1e-12), "np: defaultSize stands in for a missing lot size");
    options.defaultSize = 0;

    options.kind = ControlChartKind::C;
    const ControlResult c = ComputeControlChart({Lot(4, 0), Lot(6, 0), Lot(5, 0), Lot(9, 0), Lot(6, 0)}, options);
    CHECK(Near(c.location.centre[0], 6.0, 1e-12) && Near(c.location.upper[0], 6.0 + 3.0 * std::sqrt(6.0), 1e-12) &&
              c.location.lowerClamped && c.location.constantLimits,
          "c: CL = c-bar, UCL = c-bar + 3 sqrt(c-bar)");

    options.kind = ControlChartKind::U;
    const ControlResult u = ComputeControlChart({Lot(6, 2), Lot(10, 4), Lot(3, 1)}, options);
    const double uBar = 19.0 / 7.0;
    CHECK(Near(u.location.value[1], 2.5, 1e-12) && Near(u.location.centre[1], uBar, 1e-12) &&
              Near(u.location.upper[1], uBar + 3.0 * std::sqrt(uBar / 4.0), 1e-12),
          "u: defects per unit, u-bar = 19 / 7, limits for each lot's units");

    options.kind = ControlChartKind::P;
    options.upperSpec = 0.1;
    const ControlResult bad = ComputeControlChart({Lot(5, 100), Lot(3, 0), Lot(-1, 100)}, options);
    CHECK(std::isnan(bad.location.value[1]) && std::isnan(bad.location.value[2]) &&
              Warned(bad, "no lot size") && Warned(bad, "no valid count"),
          "a lot with no size or no valid count is left out, with warnings");
    CHECK(Warned(bad, "measurement charts") && std::isnan(bad.upperSpec) && !bad.capability.available,
          "specification limits are refused on an attribute chart");
}

static void TestNelsonRules() {
    std::printf("\n[Nelson rules: minimal patterns and near misses]\n");
    CHECK(Flags(FindRuleViolations(Standard({0, 3.1}), Nelson(1)), 1, 1), "rule 1: beyond 3 sigma");
    CHECK(Flags(FindRuleViolations(Standard(std::vector<double>(9, 0.4)), Nelson(2)), 8, 2) &&
              FindRuleViolations(Standard(std::vector<double>(8, 0.4)), Nelson(2)).empty(),
          "rule 2: nine on one side, not eight");
    const auto up = FindRuleViolations(Standard({-1, -0.6, -0.2, 0.2, 0.6, 1.0}), Nelson(3));
    CHECK(up.size() == 1 && Flags(up, 5, 3) && up.front().firstPoint == 0, "rule 3: six steadily increasing");
    CHECK(Flags(FindRuleViolations(Standard({1, 0.6, 0.2, -0.2, -0.6, -1.0}), Nelson(3)), 5, 3),
          "rule 3: or decreasing");
    CHECK(FindRuleViolations(Standard({-1, -0.6, -0.2, -0.2, 0.6, 1.0}), Nelson(3)).empty() &&
              FindRuleViolations(Standard({-1, -0.6, -0.2, 0.2, 0.6}), Nelson(3)).empty(),
          "rule 3: a level step or five points do not");
    std::vector<double> zigzag;
    for (int i = 0; i < 14; ++i) zigzag.push_back(i % 2 ? -0.5 : 0.5);
    const auto alternating = FindRuleViolations(Standard(zigzag), Nelson(4));
    CHECK(alternating.size() == 1 && Flags(alternating, 13, 4) && alternating.front().firstPoint == 0,
          "rule 4: fourteen alternating up and down");
    zigzag.pop_back();
    CHECK(FindRuleViolations(Standard(zigzag), Nelson(4)).empty(), "rule 4: thirteen do not");
    CHECK(Flags(FindRuleViolations(Standard({2.5, 0, 2.5}), Nelson(5)), 2, 5), "rule 5: two of three beyond 2 sigma");
    CHECK(Flags(FindRuleViolations(Standard({1.5, 1.5, 0, 1.5, 1.5}), Nelson(6)), 4, 6),
          "rule 6: four of five beyond 1 sigma");
    std::vector<double> quiet(15, 0.5);
    for (size_t i = 0; i < quiet.size(); i += 2) quiet[i] = -0.5;
    CHECK(Flags(FindRuleViolations(Standard(quiet), Nelson(7)), 14, 7), "rule 7: fifteen within 1 sigma");
    quiet.pop_back();
    CHECK(FindRuleViolations(Standard(quiet), Nelson(7)).empty(), "rule 7: fourteen do not");
    std::vector<double> wild(8, 1.5);
    for (size_t i = 0; i < wild.size(); i += 2) wild[i] = -1.5;
    CHECK(Flags(FindRuleViolations(Standard(wild), Nelson(8)), 7, 8), "rule 8: eight beyond 1 sigma, either side");
    wild[3] = 0.5;
    CHECK(FindRuleViolations(Standard(wild), Nelson(8)).empty(), "rule 8: one within 1 sigma breaks the run");
    CHECK(ControlRuleDescription(ControlRuleSet::Nelson, 2).find("9 points") != std::string::npos &&
              ControlRuleDescription(ControlRuleSet::Nelson, 7).find("fifteen") != std::string::npos &&
              ControlRuleDescription(ControlRuleSet::WesternElectric, 4).find("8 points") != std::string::npos,
          "descriptions follow the set and its run length");

    ControlOptions dispersion;
    dispersion.rulesOnDispersion = true;
    CHECK(Flags(FindRuleViolations(Standard({2.5, 0, 2.5}), dispersion, 1), 2, 2),
          "rulesOnDispersion runs the set on the dispersion pane");
    CHECK(FindRuleViolations(Standard({2.5, 0, 2.5}), ControlOptions(), 1).empty(), "otherwise rule 1 only");
}

static std::vector<ControlSubgroup> Individuals(const std::vector<double>& values) {
    std::vector<ControlSubgroup> data;
    for (double v : values) data.push_back(Values({v}));
    return data;
}

static void TestStagesAndExclusions() {
    std::printf("\n[Stages, excluded points, baselines]\n");
    ControlOptions options;
    options.kind = ControlChartKind::IndividualsMR;
    const std::vector<double> a = {10, 11, 9, 10, 12, 8, 10, 11, 9, 10};
    std::vector<double> both = a;
    for (double v : a) both.push_back(v + 10.0);

    const ControlResult one = ComputeControlChart(Individuals(both), options);
    CHECK(one.HasSignal(), "(one stage over a process change: signals)");
    options.stages = {{10, "New tooling"}};
    const ControlResult two = ComputeControlChart(Individuals(both), options);
    CHECK(two.stages.size() == 2 && two.stages[1].firstPoint == 10 && two.stages[1].name == "New tooling",
          "the stages as applied, the first at 0");
    CHECK(Near(two.location.centre[0], 10.0, 1e-12) && Near(two.location.centre[10], 20.0, 1e-12) &&
              !two.location.constantLimits,
          "each stage has its own centre line");
    CHECK(std::isnan(two.dispersion->value[10]), "no moving range spans the stage boundary");
    CHECK(!two.HasSignal(), "two stable stages: no signal");
    CHECK(two.StageOf(9) == 0 && two.StageOf(10) == 1, "StageOf");
    bool inside = true;
    std::vector<double> runAcross = {10, 11, 9, 10, 12, 8, 10.5, 10.5, 10.5, 10.5};
    for (double v : a) runAcross.push_back(v + 10.0);
    for (size_t i = 10; i < 14; ++i) runAcross[i] = 20.5;
    const ControlResult across = ComputeControlChart(Individuals(runAcross), options);
    for (const ControlViolation& v : across.violations) inside = inside && two.StageOf(v.firstPoint) == two.StageOf(v.point);
    CHECK(inside && across.RulesAt(13).empty(), "no rule runs across a stage boundary (8 above CL across it)");

    options.stages.clear();
    std::vector<double> outlier = a;
    outlier[5] = 30;
    std::vector<ControlSubgroup> data = Individuals(outlier);
    data[5].excluded = true;
    data[5].note = "Gauge dropped";
    const ControlResult excluded = ComputeControlChart(data, options);
    const double meanOthers = (100.0 - 8.0) / 9.0;
    CHECK(Near(excluded.location.centre[0], meanOthers, 1e-12), "an excluded point is left out of the centre line");
    CHECK(!excluded.RulesAt(5).empty() && excluded.excluded[5] && excluded.notes[5] == "Gauge dropped",
          "but still plotted, checked (rule 1) and noted");
    data[5].excluded = false;
    CHECK(ComputeControlChart(data, options).location.centre[0] > meanOthers + 1.0, "(included, it pulls the centre up)");

    options.baselineCount = 5;
    const ControlResult baseline = ComputeControlChart(Individuals(a), options);
    CHECK(Near(baseline.location.centre[9], (10 + 11 + 9 + 10 + 12) / 5.0, 1e-12),
          "baselineCount: limits from the first points only, for every point");
}

static void TestCapability() {
    std::printf("\n[Capability]\n");
    ControlOptions options;
    options.kind = ControlChartKind::IndividualsMR;
    options.lowerSpec = 4;
    options.upperSpec = 16;
    const std::vector<double> a = {10, 11, 9, 10, 12, 8, 10, 11, 9, 10};
    const ControlResult r = ComputeControlChart(Individuals(a), options);
    const double within = (16.0 / 9.0) / ControlD2(2), overall = std::sqrt(12.0 / 9.0);
    CHECK(r.capability.available && Near(r.capability.mean, 10.0, 1e-12), "the mean of the values");
    CHECK(Near(r.capability.sigmaWithin, within, 1e-12) && Near(r.capability.sigmaOverall, overall, 1e-12),
          "within: MR-bar / d2; overall: the sample SD");
    CHECK(Near(r.capability.cp, 12.0 / (6.0 * within), 1e-12) && Near(r.capability.cpk, 6.0 / (3.0 * within), 1e-12),
          "Cp = (USL - LSL) / 6 sigma within, Cpk = nearest limit / 3 sigma within");
    CHECK(Near(r.capability.pp, 12.0 / (6.0 * overall), 1e-12) && Near(r.capability.ppk, 6.0 / (3.0 * overall), 1e-12),
          "Pp and Ppk from the overall SD");
    options.lowerSpec = std::nan("");
    const ControlResult upperOnly = ComputeControlChart(Individuals(a), options);
    CHECK(std::isnan(upperOnly.capability.cp) && Near(upperOnly.capability.cpk, 6.0 / (3.0 * within), 1e-12),
          "one limit: no Cp, Cpk from that limit");
    options.upperSpec = std::nan("");
    CHECK(!ComputeControlChart(Individuals(a), options).capability.available, "no limits, no capability");
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
    TestXbarS();
    TestAttributes();
    TestNelsonRules();
    TestStagesAndExclusions();
    TestCapability();

    std::printf("\n====================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
