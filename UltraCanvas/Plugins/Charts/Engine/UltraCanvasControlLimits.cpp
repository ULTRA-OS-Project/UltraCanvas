// Plugins/Charts/Engine/UltraCanvasControlLimits.cpp
// Statistical process control, UI-free: limits and run rules.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasControlLimits.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace UltraCanvas {

namespace {

// d2 and d3 for n = 2..25 (index n - 2), to four decimals: the published
// three-decimal tables round them, and the chart factors derived from them
// (A2, D3, D4) then match their own published tables only from these.
constexpr double kD2[] = {1.1284, 1.6926, 2.0588, 2.3259, 2.5344, 2.7044, 2.8472, 2.9700,
                          3.0775, 3.1729, 3.2585, 3.3360, 3.4068, 3.4718, 3.5320, 3.5879,
                          3.6401, 3.6890, 3.7350, 3.7783, 3.8194, 3.8583, 3.8953, 3.9306};
constexpr double kD3[] = {0.8525, 0.8884, 0.8798, 0.8641, 0.8480, 0.8332, 0.8198, 0.8078,
                          0.7971, 0.7873, 0.7785, 0.7704, 0.7630, 0.7562, 0.7499, 0.7441,
                          0.7386, 0.7335, 0.7287, 0.7242, 0.7199, 0.7159, 0.7121, 0.7084};
constexpr int kMinN = 2;
constexpr int kMaxN = 25;

bool Finite(double v) { return std::isfinite(v); }

double Mean(const std::vector<double>& values) {
    double sum = 0.0;
    size_t count = 0;
    for (double v : values) {
        if (!Finite(v)) continue;
        sum += v;
        ++count;
    }
    return count ? sum / static_cast<double>(count) : std::nan("");
}

ControlSeries ConstantSeries(const std::string& name, std::vector<double> value,
                             double centre, double upper, double lower, double sigma) {
    ControlSeries s;
    s.name = name;
    const size_t count = value.size();
    s.value = std::move(value);
    s.centre.assign(count, centre);
    s.upper.assign(count, upper);
    s.lower.assign(count, lower);
    s.sigma.assign(count, sigma);
    s.constantLimits = true;
    return s;
}

// The point's distance from its centre line in its own sigmas; NaN when it
// has no value or no spread.
double ZScore(const ControlSeries& s, size_t i) {
    if (i >= s.value.size() || i >= s.sigma.size() || i >= s.centre.size() ||
        !Finite(s.value[i]) || !(s.sigma[i] > 0.0)) return std::nan("");
    return (s.value[i] - s.centre[i]) / s.sigma[i];
}

// "k of the last `window` points beyond `limit` sigmas on one side", the
// point itself among them: Western Electric rules 2 and 3.
void WindowRule(const std::vector<double>& z, int rule, size_t window,
                size_t needed, double limit, std::vector<ControlViolation>& out) {
    for (size_t i = 0; i < z.size(); ++i) {
        if (!Finite(z[i])) continue;
        for (int side : {1, -1}) {
            if (!(z[i] * side > limit)) continue;
            const size_t start = (i + 1 >= window) ? i + 1 - window : 0;
            size_t count = 0, first = i;
            for (size_t k = start; k <= i; ++k) {
                if (Finite(z[k]) && z[k] * side > limit) {
                    ++count;
                    first = std::min(first, k);
                }
            }
            if (count >= needed) out.push_back({i, first, rule, 0});
        }
    }
}

void EvaluateLocationRules(const ControlSeries& s, const ControlOptions& options,
                           std::vector<ControlViolation>& out) {
    std::vector<double> z(s.value.size());
    for (size_t i = 0; i < z.size(); ++i) z[i] = ZScore(s, i);

    if (options.rulesEnabled[0]) {
        for (size_t i = 0; i < z.size(); ++i) {
            if (Finite(z[i]) && std::abs(z[i]) > 3.0) out.push_back({i, i, 1, 0});
        }
    }
    if (options.rulesEnabled[1]) WindowRule(z, 2, 3, 2, 2.0, out);
    if (options.rulesEnabled[2]) WindowRule(z, 3, 5, 4, 1.0, out);
    if (options.rulesEnabled[3]) {
        const size_t run = static_cast<size_t>(std::max(2, options.sameSideRun));
        size_t length = 0;
        int side = 0;
        for (size_t i = 0; i < z.size(); ++i) {
            // A point on the centre line, or a gap, ends the run.
            const int here = !Finite(z[i]) ? 0 : (z[i] > 0.0 ? 1 : (z[i] < 0.0 ? -1 : 0));
            if (here != 0 && here == side) ++length;
            else length = (here != 0) ? 1 : 0;
            side = here;
            if (length >= run) out.push_back({i, i + 1 - run, 4, 0});
        }
    }
}

void EvaluateDispersionRules(const ControlSeries& s, std::vector<ControlViolation>& out) {
    const size_t count = std::min({s.value.size(), s.upper.size(), s.lower.size()});
    for (size_t i = 0; i < count; ++i) {
        const double v = s.value[i];
        if (!Finite(v)) continue;
        const bool above = v > s.upper[i];
        const bool below = !s.lowerClamped && v < s.lower[i];
        if (above || below) out.push_back({i, i, 1, 1});
    }
}

std::string Count(size_t n) { return std::to_string(n); }

ControlResult ComputeXbarR(const std::vector<ControlSubgroup>& subgroups) {
    ControlResult result;
    result.kind = ControlChartKind::XbarR;
    std::vector<double> means(subgroups.size(), std::nan(""));
    std::vector<double> ranges(subgroups.size(), std::nan(""));
    std::vector<int> sizes;
    size_t tooSmall = 0;
    for (size_t i = 0; i < subgroups.size(); ++i) {
        std::vector<double> values;
        for (double v : subgroups[i].values) {
            if (Finite(v)) values.push_back(v);
        }
        if (values.size() < 2) {
            ++tooSmall;
            continue;
        }
        const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
        means[i] = Mean(values);
        ranges[i] = *hi - *lo;
        sizes.push_back(static_cast<int>(values.size()));
    }
    if (tooSmall > 0) {
        result.warnings.push_back(Count(tooSmall) +
                                  " subgroup(s) with fewer than two values are left out.");
    }
    if (sizes.empty()) {
        result.warnings.push_back("No subgroup has two or more values: there are no limits.");
        return result;
    }

    // The constants need one subgroup size; with unequal sizes the most
    // common one stands in (X-bar-S handles unequal sizes properly).
    int n = sizes.front();
    {
        std::vector<int> sorted = sizes;
        std::sort(sorted.begin(), sorted.end());
        size_t best = 0;
        for (size_t i = 0; i < sorted.size();) {
            size_t j = i;
            while (j < sorted.size() && sorted[j] == sorted[i]) ++j;
            if (j - i > best) {
                best = j - i;
                n = sorted[i];
            }
            i = j;
        }
        if (sorted.front() != sorted.back()) {
            result.warnings.push_back("Subgroup sizes differ (" + Count(static_cast<size_t>(sorted.front())) +
                                      " to " + Count(static_cast<size_t>(sorted.back())) +
                                      "); the limits use n = " + Count(static_cast<size_t>(n)) + ".");
        }
    }
    if (n > kMaxN) {
        result.warnings.push_back("Subgroups of more than 25 values: the limits use the constants for n = 25.");
        n = kMaxN;
    }
    result.subgroupSize = n;

    const double grandMean = Mean(means);
    const double meanRange = Mean(ranges);
    const double d2 = ControlD2(n);
    const double d3 = ControlD3(n);
    result.sigmaWithin = meanRange / d2;
    const double sigmaMean = result.sigmaWithin / std::sqrt(static_cast<double>(n));
    result.location = ConstantSeries("Subgroup mean", means, grandMean,
                                     grandMean + 3.0 * sigmaMean, grandMean - 3.0 * sigmaMean,
                                     sigmaMean);
    const double lowerFactor = 1.0 - 3.0 * d3 / d2;
    ControlSeries range = ConstantSeries("Range", ranges, meanRange,
                                         ControlRangeUpperFactor(n) * meanRange,
                                         std::max(0.0, lowerFactor) * meanRange,
                                         d3 * result.sigmaWithin);
    range.lowerClamped = lowerFactor <= 0.0;
    result.dispersion = std::move(range);
    if (!(meanRange > 0.0)) {
        result.warnings.push_back("Every subgroup's range is zero: there is no spread to set limits from.");
    }
    return result;
}

ControlResult ComputeIndividuals(const std::vector<ControlSubgroup>& subgroups) {
    ControlResult result;
    result.kind = ControlChartKind::IndividualsMR;
    result.subgroupSize = 1;
    std::vector<double> x(subgroups.size(), std::nan(""));
    bool extra = false;
    for (size_t i = 0; i < subgroups.size(); ++i) {
        size_t taken = 0;
        for (double v : subgroups[i].values) {
            if (!Finite(v)) continue;
            if (taken == 0) x[i] = v;
            ++taken;
        }
        extra = extra || taken > 1;
    }
    if (extra) {
        result.warnings.push_back("I-MR takes one value per point; further values are ignored.");
    }
    std::vector<double> mr(x.size(), std::nan(""));
    for (size_t i = 1; i < x.size(); ++i) {
        if (Finite(x[i]) && Finite(x[i - 1])) mr[i] = std::abs(x[i] - x[i - 1]);
    }
    const double mean = Mean(x);
    const double meanMR = Mean(mr);
    if (!Finite(mean) || !Finite(meanMR)) {
        result.warnings.push_back("Fewer than two consecutive values: there are no limits.");
        return result;
    }
    result.sigmaWithin = meanMR / ControlD2(2);
    result.location = ConstantSeries("Value", x, mean, mean + 3.0 * result.sigmaWithin,
                                     mean - 3.0 * result.sigmaWithin, result.sigmaWithin);
    ControlSeries moving = ConstantSeries("Moving range", mr, meanMR,
                                          ControlRangeUpperFactor(2) * meanMR, 0.0,
                                          ControlD3(2) * result.sigmaWithin);
    moving.lowerClamped = true;
    result.dispersion = std::move(moving);
    if (!(meanMR > 0.0)) {
        result.warnings.push_back("Every moving range is zero: there is no spread to set limits from.");
    }
    return result;
}

} // namespace

double ControlD2(int n) {
    return (n >= kMinN && n <= kMaxN) ? kD2[n - kMinN] : std::nan("");
}

double ControlD3(int n) {
    return (n >= kMinN && n <= kMaxN) ? kD3[n - kMinN] : std::nan("");
}

double ControlC4(int n) {
    if (n < kMinN || n > kMaxN) return std::nan("");
    const double m = static_cast<double>(n);
    return std::sqrt(2.0 / (m - 1.0)) * std::exp(std::lgamma(m / 2.0) - std::lgamma((m - 1.0) / 2.0));
}

double ControlA2(int n) {
    return 3.0 / (ControlD2(n) * std::sqrt(static_cast<double>(n)));
}

double ControlRangeLowerFactor(int n) {
    return std::max(0.0, 1.0 - 3.0 * ControlD3(n) / ControlD2(n));
}

double ControlRangeUpperFactor(int n) {
    return 1.0 + 3.0 * ControlD3(n) / ControlD2(n);
}

std::vector<int> ControlResult::RulesAt(size_t index, size_t pane) const {
    std::set<int> rules;
    for (const ControlViolation& v : violations) {
        if (v.point == index && v.pane == pane) rules.insert(v.rule);
    }
    return std::vector<int>(rules.begin(), rules.end());
}

std::string ControlRuleDescription(int rule, int sameSideRun) {
    switch (rule) {
        case 1: return "Rule 1: a point beyond the control limits";
        case 2: return "Rule 2: two of three points beyond 2 sigma on one side";
        case 3: return "Rule 3: four of five points beyond 1 sigma on one side";
        case 4: return "Rule 4: " + std::to_string(std::max(2, sameSideRun)) +
                       " points in a row on one side of the centre line";
        default: return std::string();
    }
}

ControlResult ComputeControlChart(const std::vector<ControlSubgroup>& subgroups,
                                  const ControlOptions& options) {
    ControlResult result = options.kind == ControlChartKind::IndividualsMR
                               ? ComputeIndividuals(subgroups)
                               : ComputeXbarR(subgroups);
    result.labels.reserve(subgroups.size());
    for (size_t i = 0; i < subgroups.size(); ++i) {
        result.labels.push_back(subgroups[i].label.empty() ? std::to_string(i + 1)
                                                           : subgroups[i].label);
    }
    if (result.location.value.empty()) return result;
    if (subgroups.size() < 20) {
        result.warnings.push_back("Only " + Count(subgroups.size()) +
                                  " points: limits from fewer than 20 are preliminary.");
    }
    result.violations = FindRuleViolations(result.location, options, 0);
    if (result.dispersion) {
        for (const ControlViolation& v : FindRuleViolations(*result.dispersion, options, 1)) {
            result.violations.push_back(v);
        }
    }
    return result;
}

std::vector<ControlViolation> FindRuleViolations(const ControlSeries& series,
                                                 const ControlOptions& options, size_t pane) {
    std::vector<ControlViolation> found;
    if (pane == 0) EvaluateLocationRules(series, options, found);
    else EvaluateDispersionRules(series, found);
    for (ControlViolation& v : found) v.pane = pane;
    std::sort(found.begin(), found.end(), [](const ControlViolation& a, const ControlViolation& b) {
        if (a.point != b.point) return a.point < b.point;
        return a.rule < b.rule;
    });
    return found;
}

} // namespace UltraCanvas
