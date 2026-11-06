// Plugins/Charts/Engine/UltraCanvasControlLimits.cpp
// Statistical process control, UI-free: limits and run rules.
// Version: 1.1.0 - X-bar-S, p, np, c, u; Nelson rules; stages, exclusions,
//   baselines; capability
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasControlLimits.h"
#include <algorithm>
#include <cmath>
#include <map>
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
const double kNaN = std::nan("");

bool Finite(double v) { return std::isfinite(v); }

double Mean(const std::vector<double>& values) {
    double sum = 0.0;
    size_t count = 0;
    for (double v : values) {
        if (!Finite(v)) continue;
        sum += v;
        ++count;
    }
    return count ? sum / static_cast<double>(count) : kNaN;
}

double SampleSd(const std::vector<double>& values) {
    const double mean = Mean(values);
    double ss = 0.0;
    size_t count = 0;
    for (double v : values) {
        if (!Finite(v)) continue;
        ss += (v - mean) * (v - mean);
        ++count;
    }
    return count > 1 ? std::sqrt(ss / static_cast<double>(count - 1)) : kNaN;
}

std::string Count(size_t n) { return std::to_string(n); }

int MostCommon(const std::vector<int>& sizes) {
    std::map<int, size_t> tally;
    for (int n : sizes) ++tally[n];
    int best = 0;
    size_t bestCount = 0;
    for (const auto& [n, count] : tally) {
        if (count > bestCount) {
            best = n;
            bestCount = count;
        }
    }
    return best;
}

// The finite values of a subgroup.
std::vector<double> FiniteValues(const ControlSubgroup& g) {
    std::vector<double> v;
    for (double x : g.values) {
        if (Finite(x)) v.push_back(x);
    }
    return v;
}

// ---- stages ----------------------------------------------------------------

struct StageRange {
    size_t begin = 0, end = 0;   // [begin, end)
    std::string name;
};

std::vector<StageRange> Stages(const std::vector<ControlStage>& requested, size_t count,
                               std::vector<ControlStage>& applied) {
    std::vector<ControlStage> sorted = requested;
    std::sort(sorted.begin(), sorted.end(),
              [](const ControlStage& a, const ControlStage& b) { return a.firstPoint < b.firstPoint; });
    applied.clear();
    applied.push_back({0, std::string()});
    for (const ControlStage& s : sorted) {
        if (s.firstPoint >= count) continue;
        if (s.firstPoint == applied.back().firstPoint) {
            if (!s.name.empty()) applied.back().name = s.name;
            continue;
        }
        applied.push_back(s);
    }
    std::vector<StageRange> ranges;
    for (size_t k = 0; k < applied.size(); ++k) {
        const size_t end = (k + 1 < applied.size()) ? applied[k + 1].firstPoint : count;
        ranges.push_back({applied[k].firstPoint, end, applied[k].name});
    }
    return ranges;
}

std::string StageLabel(const StageRange& stage, size_t index, size_t total) {
    if (total <= 1) return std::string();
    return "Stage " + Count(index + 1) + (stage.name.empty() ? std::string() : " (" + stage.name + ")") + ": ";
}

// ---- series ----------------------------------------------------------------

ControlSeries EmptySeries(const std::string& name, size_t count, bool nonNegative) {
    ControlSeries s;
    s.name = name;
    s.value.assign(count, kNaN);
    s.centre.assign(count, kNaN);
    s.upper.assign(count, kNaN);
    s.lower.assign(count, kNaN);
    s.sigma.assign(count, kNaN);
    s.nonNegative = nonNegative;
    return s;
}

// Point i's limits: centre +- 3 sigma, the lower one raised to 0 (and a
// proportion's upper one capped at 1) where the statistic cannot go there.
void SetLimits(ControlSeries& s, size_t i, double centre, double sigma, double cap = kNaN) {
    s.centre[i] = centre;
    s.sigma[i] = sigma;
    s.upper[i] = centre + 3.0 * sigma;
    if (Finite(cap)) s.upper[i] = std::min(cap, s.upper[i]);
    s.lower[i] = centre - 3.0 * sigma;
    if (s.nonNegative) s.lower[i] = std::max(0.0, s.lower[i]);
}

void Finish(ControlSeries& s) {
    bool first = true, same = true;
    double c0 = 0.0, u0 = 0.0, l0 = 0.0;
    bool allClamped = s.nonNegative;
    for (size_t i = 0; i < s.centre.size(); ++i) {
        if (!Finite(s.centre[i])) continue;
        if (first) {
            c0 = s.centre[i];
            u0 = s.upper[i];
            l0 = s.lower[i];
            first = false;
        } else {
            const double tol = 1e-12 * std::max({1.0, std::abs(c0), std::abs(u0)});
            same = same && std::abs(s.centre[i] - c0) <= tol && std::abs(s.upper[i] - u0) <= tol &&
                   std::abs(s.lower[i] - l0) <= tol;
        }
        allClamped = allClamped && !(s.lower[i] > 0.0);
    }
    s.constantLimits = same;
    s.lowerClamped = !first && allClamped;
}

// ---- the kinds, one stage at a time ----------------------------------------

struct Context {
    const std::vector<ControlSubgroup>& data;
    const ControlOptions& options;
    ControlResult& result;
    std::vector<StageRange> stages;

    // Whether point i feeds its stage's limits.
    bool Feeds(size_t i, const StageRange& stage) const {
        if (data[i].excluded) return false;
        return options.baselineCount == 0 || i < stage.begin + options.baselineCount;
    }
    size_t FeedCount(const StageRange& stage) const {
        size_t n = 0;
        for (size_t i = stage.begin; i < stage.end; ++i) n += Feeds(i, stage) ? 1 : 0;
        return n;
    }
    void Warn(const std::string& text) {
        if (std::find(result.warnings.begin(), result.warnings.end(), text) == result.warnings.end()) {
            result.warnings.push_back(text);
        }
    }
};

void XbarR(Context& ctx) {
    const size_t count = ctx.data.size();
    ControlSeries location = EmptySeries("Subgroup mean", count, false);
    ControlSeries range = EmptySeries("Range", count, true);
    std::vector<int> sizes(count, 0);
    size_t tooSmall = 0;
    for (size_t i = 0; i < count; ++i) {
        const std::vector<double> v = FiniteValues(ctx.data[i]);
        if (v.size() < 2) {
            ++tooSmall;
            continue;
        }
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        location.value[i] = Mean(v);
        range.value[i] = *hi - *lo;
        sizes[i] = static_cast<int>(v.size());
    }
    if (tooSmall > 0) ctx.Warn(Count(tooSmall) + " subgroup(s) with fewer than two values are left out.");
    for (size_t k = 0; k < ctx.stages.size(); ++k) {
        const StageRange& stage = ctx.stages[k];
        const std::string prefix = StageLabel(stage, k, ctx.stages.size());
        std::vector<double> means, ranges;
        std::vector<int> fed, all;
        for (size_t i = stage.begin; i < stage.end; ++i) {
            if (sizes[i] < 2) continue;
            all.push_back(sizes[i]);
            if (!ctx.Feeds(i, stage)) continue;
            means.push_back(location.value[i]);
            ranges.push_back(range.value[i]);
            fed.push_back(sizes[i]);
        }
        if (fed.empty()) {
            ctx.Warn(prefix + "No subgroup has two or more values: there are no limits.");
            continue;
        }
        // The constants need one subgroup size; with unequal sizes the most
        // common one stands in (X-bar-S handles unequal sizes properly).
        int n = MostCommon(fed);
        const auto [mn, mx] = std::minmax_element(all.begin(), all.end());
        if (*mn != *mx) {
            ctx.Warn(prefix + "Subgroup sizes differ (" + Count(static_cast<size_t>(*mn)) + " to " +
                     Count(static_cast<size_t>(*mx)) + "); the limits use n = " + Count(static_cast<size_t>(n)) +
                     ". X-bar-S handles unequal sizes.");
        }
        if (n > kMaxN) {
            ctx.Warn("Subgroups of more than 25 values: the limits use the constants for n = 25.");
            n = kMaxN;
        }
        const double grand = Mean(means), meanRange = Mean(ranges);
        const double sigma = meanRange / ControlD2(n);
        if (!(meanRange > 0.0)) ctx.Warn(prefix + "Every subgroup's range is zero: there is no spread to set limits from.");
        for (size_t i = stage.begin; i < stage.end; ++i) {
            SetLimits(location, i, grand, sigma / std::sqrt(static_cast<double>(n)));
            SetLimits(range, i, meanRange, ControlD3(n) * sigma);
            // D3 R-bar exactly (the clamp at zero is the same thing).
            range.lower[i] = ControlRangeLowerFactor(n) * meanRange;
        }
        ctx.result.sigmaWithin = sigma;
        ctx.result.subgroupSize = n;
    }
    ctx.result.location = std::move(location);
    ctx.result.dispersion = std::move(range);
}

void XbarS(Context& ctx) {
    const size_t count = ctx.data.size();
    ControlSeries location = EmptySeries("Subgroup mean", count, false);
    ControlSeries deviation = EmptySeries("Standard deviation", count, true);
    std::vector<int> sizes(count, 0);
    size_t tooSmall = 0;
    for (size_t i = 0; i < count; ++i) {
        const std::vector<double> v = FiniteValues(ctx.data[i]);
        if (v.size() < 2) {
            ++tooSmall;
            continue;
        }
        location.value[i] = Mean(v);
        deviation.value[i] = SampleSd(v);
        sizes[i] = static_cast<int>(v.size());
    }
    if (tooSmall > 0) ctx.Warn(Count(tooSmall) + " subgroup(s) with fewer than two values are left out.");
    std::vector<int> allSizes;
    for (size_t k = 0; k < ctx.stages.size(); ++k) {
        const StageRange& stage = ctx.stages[k];
        const std::string prefix = StageLabel(stage, k, ctx.stages.size());
        double weighted = 0.0, weights = 0.0, sumS = 0.0, pooled = 0.0, dof = 0.0;
        std::vector<int> fed;
        for (size_t i = stage.begin; i < stage.end; ++i) {
            if (sizes[i] < 2) continue;
            allSizes.push_back(sizes[i]);
            if (!ctx.Feeds(i, stage)) continue;
            const double n = sizes[i];
            weighted += n * location.value[i];
            weights += n;
            sumS += deviation.value[i];
            pooled += (n - 1.0) * deviation.value[i] * deviation.value[i];
            dof += n - 1.0;
            fed.push_back(sizes[i]);
        }
        if (fed.empty()) {
            ctx.Warn(prefix + "No subgroup has two or more values: there are no limits.");
            continue;
        }
        const bool equal = std::all_of(fed.begin(), fed.end(), [&](int n) { return n == fed.front(); });
        // Equal sizes: S-bar / c4(n), the textbook estimate (so the limits
        // are A3 S-bar, B3 S-bar and B4 S-bar). Unequal: the pooled standard
        // deviation, unbiased by c4 of its degrees of freedom plus one.
        const double sigma = equal ? (sumS / static_cast<double>(fed.size())) / ControlC4(fed.front())
                                   : std::sqrt(pooled / dof) / ControlC4(static_cast<int>(std::lround(dof)) + 1);
        const double grand = weighted / weights;
        if (!(sigma > 0.0)) ctx.Warn(prefix + "Every subgroup's spread is zero: there is no spread to set limits from.");
        for (size_t i = stage.begin; i < stage.end; ++i) {
            const int n = sizes[i] >= 2 ? sizes[i] : MostCommon(fed);
            const double c4 = ControlC4(n);
            SetLimits(location, i, grand, sigma / std::sqrt(static_cast<double>(n)));
            SetLimits(deviation, i, c4 * sigma, sigma * std::sqrt(std::max(0.0, 1.0 - c4 * c4)));
        }
        ctx.result.sigmaWithin = sigma;
        ctx.result.subgroupSize = MostCommon(fed);
    }
    if (!allSizes.empty()) {
        const auto [mn, mx] = std::minmax_element(allSizes.begin(), allSizes.end());
        if (*mn != *mx) {
            ctx.Warn("Subgroup sizes vary (" + Count(static_cast<size_t>(*mn)) + " to " +
                     Count(static_cast<size_t>(*mx)) + "): each point has limits for its own size.");
        }
    }
    ctx.result.location = std::move(location);
    ctx.result.dispersion = std::move(deviation);
}

void Individuals(Context& ctx) {
    const size_t count = ctx.data.size();
    ControlSeries location = EmptySeries("Value", count, false);
    ControlSeries moving = EmptySeries("Moving range", count, true);
    bool extra = false;
    for (size_t i = 0; i < count; ++i) {
        size_t taken = 0;
        for (double v : ctx.data[i].values) {
            if (!Finite(v)) continue;
            if (taken == 0) location.value[i] = v;
            ++taken;
        }
        extra = extra || taken > 1;
    }
    if (extra) ctx.Warn("I-MR takes one value per point; further values are ignored.");
    ctx.result.subgroupSize = 1;
    for (size_t k = 0; k < ctx.stages.size(); ++k) {
        const StageRange& stage = ctx.stages[k];
        const std::string prefix = StageLabel(stage, k, ctx.stages.size());
        // A moving range never spans a stage boundary.
        for (size_t i = stage.begin + 1; i < stage.end; ++i) {
            if (Finite(location.value[i]) && Finite(location.value[i - 1])) {
                moving.value[i] = std::abs(location.value[i] - location.value[i - 1]);
            }
        }
        std::vector<double> xs, mrs;
        for (size_t i = stage.begin; i < stage.end; ++i) {
            if (ctx.Feeds(i, stage)) xs.push_back(location.value[i]);
            if (i > stage.begin && ctx.Feeds(i, stage) && ctx.Feeds(i - 1, stage)) mrs.push_back(moving.value[i]);
        }
        const double mean = Mean(xs), meanMR = Mean(mrs);
        if (!Finite(mean) || !Finite(meanMR)) {
            ctx.Warn(prefix + "Fewer than two consecutive values: there are no limits.");
            continue;
        }
        const double sigma = meanMR / ControlD2(2);
        if (!(meanMR > 0.0)) ctx.Warn(prefix + "Every moving range is zero: there is no spread to set limits from.");
        for (size_t i = stage.begin; i < stage.end; ++i) {
            SetLimits(location, i, mean, sigma);
            SetLimits(moving, i, meanMR, ControlD3(2) * sigma);
            moving.upper[i] = ControlRangeUpperFactor(2) * meanMR;
            moving.lower[i] = 0.0;
        }
        ctx.result.sigmaWithin = sigma;
    }
    ctx.result.location = std::move(location);
    ctx.result.dispersion = std::move(moving);
}

void Attributes(Context& ctx) {
    const ControlChartKind kind = ctx.options.kind;
    const size_t count = ctx.data.size();
    const char* names[] = {"", "", "", "Proportion defective", "Number defective", "Defects", "Defects per unit"};
    ControlSeries s = EmptySeries(names[static_cast<int>(kind)], count, true);
    const bool perSize = kind != ControlChartKind::C;
    std::vector<double> sizes(count, kNaN);
    size_t badSize = 0, badCount = 0, overflow = 0;
    for (size_t i = 0; i < count; ++i) {
        const ControlSubgroup& g = ctx.data[i];
        if (!Finite(g.count) || g.count < 0.0) {
            ++badCount;
            continue;
        }
        double n = 1.0;
        if (perSize) {
            n = g.size > 0.0 ? g.size : ctx.options.defaultSize;
            if (!(n > 0.0) || !Finite(n)) {
                ++badSize;
                continue;
            }
        }
        if (kind == ControlChartKind::P && g.count > n) ++overflow;
        sizes[i] = n;
        s.value[i] = (kind == ControlChartKind::P || kind == ControlChartKind::U) ? g.count / n : g.count;
    }
    if (badCount > 0) ctx.Warn(Count(badCount) + " point(s) with no valid count are left out.");
    if (badSize > 0) ctx.Warn(Count(badSize) + " point(s) with no lot size are left out.");
    if (overflow > 0) ctx.Warn(Count(overflow) + " lot(s) count more defectives than items.");
    std::set<double> distinct;
    for (double n : sizes) {
        if (Finite(n)) distinct.insert(n);
    }
    if (kind == ControlChartKind::NP && distinct.size() > 1) {
        ctx.Warn("An np chart needs one lot size; each point has limits for its own. A p chart suits varying lots.");
    }
    for (size_t k = 0; k < ctx.stages.size(); ++k) {
        const StageRange& stage = ctx.stages[k];
        const std::string prefix = StageLabel(stage, k, ctx.stages.size());
        double defects = 0.0, units = 0.0;
        size_t fed = 0;
        for (size_t i = stage.begin; i < stage.end; ++i) {
            if (!Finite(s.value[i]) || !ctx.Feeds(i, stage)) continue;
            defects += ctx.data[i].count;
            units += sizes[i];
            ++fed;
        }
        if (fed == 0 || !(units > 0.0)) {
            ctx.Warn(prefix + "No valid point: there are no limits.");
            continue;
        }
        const double rate = defects / units;   // p-bar, u-bar; c-bar for C (units = points)
        if (!(rate > 0.0)) ctx.Warn(prefix + "No defects at all: there is no spread to set limits from.");
        for (size_t i = stage.begin; i < stage.end; ++i) {
            const double n = Finite(sizes[i]) ? sizes[i] : (units / static_cast<double>(fed));
            switch (kind) {
                case ControlChartKind::P:
                    SetLimits(s, i, rate, std::sqrt(rate * (1.0 - rate) / n), 1.0);
                    break;
                case ControlChartKind::NP:
                    SetLimits(s, i, n * rate, std::sqrt(n * rate * (1.0 - rate)), n);
                    break;
                case ControlChartKind::C:
                    SetLimits(s, i, rate, std::sqrt(rate));
                    break;
                default:   // U
                    SetLimits(s, i, rate, std::sqrt(rate / n));
                    break;
            }
        }
    }
    int common = 1;
    {
        std::vector<int> whole;
        for (double n : sizes) {
            if (Finite(n)) whole.push_back(static_cast<int>(std::lround(n)));
        }
        if (perSize && !whole.empty()) common = MostCommon(whole);
    }
    ctx.result.subgroupSize = common;
    ctx.result.sigmaWithin = kNaN;
    ctx.result.location = std::move(s);
    ctx.result.dispersion.reset();
}

// ---- rules -----------------------------------------------------------------

// The point's distance from its centre line in its own sigmas; NaN when it
// has no value or no spread.
double ZScore(const ControlSeries& s, size_t i) {
    if (i >= s.value.size() || i >= s.sigma.size() || i >= s.centre.size() ||
        !Finite(s.value[i]) || !(s.sigma[i] > 0.0) || !Finite(s.centre[i])) return kNaN;
    return (s.value[i] - s.centre[i]) / s.sigma[i];
}

// "k of the last `window` points beyond `limit` sigmas on one side", the
// point itself among them.
void WindowRule(const std::vector<double>& z, int rule, size_t window, size_t needed, double limit,
                std::vector<ControlViolation>& out) {
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

// `length` points in a row each meeting `test` (a gap ends the run).
template <typename Test>
void RunRule(const std::vector<double>& z, int rule, size_t length, Test test,
             std::vector<ControlViolation>& out) {
    size_t run = 0;
    for (size_t i = 0; i < z.size(); ++i) {
        run = (Finite(z[i]) && test(z[i])) ? run + 1 : 0;
        if (run >= length) out.push_back({i, i + 1 - length, rule, 0});
    }
}

// `length` points in a row on one side of the centre line; a point on it,
// or a gap, ends the run.
void SameSideRule(const std::vector<double>& z, int rule, size_t length, std::vector<ControlViolation>& out) {
    size_t run = 0;
    int side = 0;
    for (size_t i = 0; i < z.size(); ++i) {
        const int here = !Finite(z[i]) ? 0 : (z[i] > 0.0 ? 1 : (z[i] < 0.0 ? -1 : 0));
        if (here != 0 && here == side) ++run;
        else run = (here != 0) ? 1 : 0;
        side = here;
        if (run >= length) out.push_back({i, i + 1 - length, rule, 0});
    }
}

// Steps between consecutive points: +1 up, -1 down, 0 level or a gap.
std::vector<int> Steps(const std::vector<double>& z) {
    std::vector<int> steps(z.size(), 0);
    for (size_t i = 1; i < z.size(); ++i) {
        if (Finite(z[i]) && Finite(z[i - 1])) steps[i] = z[i] > z[i - 1] ? 1 : (z[i] < z[i - 1] ? -1 : 0);
    }
    return steps;
}

int SameSideLength(const ControlOptions& options) {
    if (options.sameSideRun > 0) return std::max(2, options.sameSideRun);
    return options.ruleSet == ControlRuleSet::Nelson ? 9 : 8;
}

void PatternRules(const ControlSeries& s, const ControlOptions& options, std::vector<ControlViolation>& out) {
    std::vector<double> z(s.value.size());
    for (size_t i = 0; i < z.size(); ++i) z[i] = ZScore(s, i);
    auto on = [&](int rule) { return rule >= 1 && rule <= 8 && options.rulesEnabled[static_cast<size_t>(rule - 1)]; };
    const size_t sameSide = static_cast<size_t>(SameSideLength(options));
    if (on(1)) RunRule(z, 1, 1, [](double v) { return std::abs(v) > 3.0; }, out);
    if (options.ruleSet == ControlRuleSet::WesternElectric) {
        if (on(2)) WindowRule(z, 2, 3, 2, 2.0, out);
        if (on(3)) WindowRule(z, 3, 5, 4, 1.0, out);
        if (on(4)) SameSideRule(z, 4, sameSide, out);
        return;
    }
    if (on(2)) SameSideRule(z, 2, sameSide, out);
    const std::vector<int> steps = Steps(z);
    if (on(3)) {
        // Six points steadily up or down: five steps the same way.
        for (size_t i = 5; i < z.size(); ++i) {
            bool up = true, down = true;
            for (size_t k = i - 4; k <= i; ++k) {
                up = up && steps[k] == 1;
                down = down && steps[k] == -1;
            }
            if (up || down) out.push_back({i, i - 5, 3, 0});
        }
    }
    if (on(4)) {
        // Fourteen points alternating: thirteen steps, each the other way.
        for (size_t i = 13; i < z.size(); ++i) {
            bool alternating = true;
            for (size_t k = i - 12; k <= i && alternating; ++k) {
                alternating = steps[k] != 0 && (k == i - 12 || steps[k] == -steps[k - 1]);
            }
            if (alternating) out.push_back({i, i - 13, 4, 0});
        }
    }
    if (on(5)) WindowRule(z, 5, 3, 2, 2.0, out);
    if (on(6)) WindowRule(z, 6, 5, 4, 1.0, out);
    if (on(7)) RunRule(z, 7, 15, [](double v) { return std::abs(v) < 1.0; }, out);
    if (on(8)) RunRule(z, 8, 8, [](double v) { return std::abs(v) > 1.0; }, out);
}

void LimitRule(const ControlSeries& s, std::vector<ControlViolation>& out) {
    const size_t count = std::min({s.value.size(), s.upper.size(), s.lower.size()});
    for (size_t i = 0; i < count; ++i) {
        const double v = s.value[i];
        if (!Finite(v)) continue;
        const bool above = Finite(s.upper[i]) && v > s.upper[i];
        const bool below = s.HasLowerLimit(i) && v < s.lower[i];
        if (above || below) out.push_back({i, i, 1, 0});
    }
}

ControlSeries Slice(const ControlSeries& s, size_t begin, size_t end) {
    ControlSeries out = s;
    auto cut = [&](std::vector<double>& v) {
        v = std::vector<double>(v.begin() + static_cast<std::ptrdiff_t>(std::min(begin, v.size())),
                                v.begin() + static_cast<std::ptrdiff_t>(std::min(end, v.size())));
    };
    cut(out.value);
    cut(out.centre);
    cut(out.upper);
    cut(out.lower);
    cut(out.sigma);
    return out;
}

void Capability(ControlResult& result, const std::vector<ControlSubgroup>& data, const StageRange& last) {
    ControlCapability& cap = result.capability;
    const bool measured = result.kind == ControlChartKind::XbarR || result.kind == ControlChartKind::XbarS ||
                          result.kind == ControlChartKind::IndividualsMR;
    const bool hasSpec = Finite(result.lowerSpec) || Finite(result.upperSpec);
    if (!measured || !hasSpec) return;
    std::vector<double> values;
    for (size_t i = last.begin; i < last.end; ++i) {
        if (data[i].excluded) continue;
        if (result.kind == ControlChartKind::IndividualsMR) {
            if (Finite(result.location.value[i])) values.push_back(result.location.value[i]);
        } else if (Finite(result.location.value[i])) {
            for (double v : data[i].values) {
                if (Finite(v)) values.push_back(v);
            }
        }
    }
    cap.mean = Mean(values);
    cap.sigmaWithin = result.sigmaWithin;
    cap.sigmaOverall = SampleSd(values);
    if (!Finite(cap.mean)) return;
    cap.available = true;
    auto indices = [&](double sigma, double& both, double& worst) {
        if (!(sigma > 0.0) || !Finite(sigma)) return;
        if (Finite(result.lowerSpec) && Finite(result.upperSpec)) {
            both = (result.upperSpec - result.lowerSpec) / (6.0 * sigma);
        }
        double k = kNaN;
        if (Finite(result.upperSpec)) k = (result.upperSpec - cap.mean) / (3.0 * sigma);
        if (Finite(result.lowerSpec)) {
            const double lower = (cap.mean - result.lowerSpec) / (3.0 * sigma);
            k = Finite(k) ? std::min(k, lower) : lower;
        }
        worst = k;
    };
    indices(cap.sigmaWithin, cap.cp, cap.cpk);
    indices(cap.sigmaOverall, cap.pp, cap.ppk);
}

} // namespace

double ControlD2(int n) {
    return (n >= kMinN && n <= kMaxN) ? kD2[n - kMinN] : kNaN;
}

double ControlD3(int n) {
    return (n >= kMinN && n <= kMaxN) ? kD3[n - kMinN] : kNaN;
}

double ControlC4(int n) {
    if (n < kMinN) return kNaN;
    const double m = static_cast<double>(n);
    return std::sqrt(2.0 / (m - 1.0)) * std::exp(std::lgamma(m / 2.0) - std::lgamma((m - 1.0) / 2.0));
}

double ControlA2(int n) { return 3.0 / (ControlD2(n) * std::sqrt(static_cast<double>(n))); }
double ControlRangeLowerFactor(int n) { return std::max(0.0, 1.0 - 3.0 * ControlD3(n) / ControlD2(n)); }
double ControlRangeUpperFactor(int n) { return 1.0 + 3.0 * ControlD3(n) / ControlD2(n); }
double ControlA3(int n) { return 3.0 / (ControlC4(n) * std::sqrt(static_cast<double>(n))); }

double ControlSigmaLowerFactor(int n) {
    const double c4 = ControlC4(n);
    return std::max(0.0, 1.0 - 3.0 * std::sqrt(1.0 - c4 * c4) / c4);
}

double ControlSigmaUpperFactor(int n) {
    const double c4 = ControlC4(n);
    return 1.0 + 3.0 * std::sqrt(1.0 - c4 * c4) / c4;
}

std::vector<int> ControlResult::RulesAt(size_t index, size_t pane) const {
    std::set<int> rules;
    for (const ControlViolation& v : violations) {
        if (v.point == index && v.pane == pane) rules.insert(v.rule);
    }
    return std::vector<int>(rules.begin(), rules.end());
}

size_t ControlResult::StageOf(size_t index) const {
    size_t stage = 0;
    for (size_t k = 0; k < stages.size(); ++k) {
        if (stages[k].firstPoint <= index) stage = k;
    }
    return stage;
}

std::string ControlRuleDescription(ControlRuleSet set, int rule, int sameSideRun) {
    ControlOptions options;
    options.ruleSet = set;
    options.sameSideRun = sameSideRun;
    const std::string run = std::to_string(SameSideLength(options)) + " points in a row on one side of the centre line";
    if (set == ControlRuleSet::WesternElectric) {
        switch (rule) {
            case 1: return "Rule 1: a point beyond the control limits";
            case 2: return "Rule 2: two of three points beyond 2 sigma on one side";
            case 3: return "Rule 3: four of five points beyond 1 sigma on one side";
            case 4: return "Rule 4: " + run;
            default: return std::string();
        }
    }
    switch (rule) {
        case 1: return "Rule 1: a point beyond the control limits";
        case 2: return "Rule 2: " + run;
        case 3: return "Rule 3: six points in a row steadily increasing or decreasing";
        case 4: return "Rule 4: fourteen points in a row alternating up and down";
        case 5: return "Rule 5: two of three points beyond 2 sigma on one side";
        case 6: return "Rule 6: four of five points beyond 1 sigma on one side";
        case 7: return "Rule 7: fifteen points in a row within 1 sigma of the centre line";
        case 8: return "Rule 8: eight points in a row beyond 1 sigma, either side";
        default: return std::string();
    }
}

std::string ControlRuleDescription(int rule, int sameSideRun) {
    return ControlRuleDescription(ControlRuleSet::WesternElectric, rule, sameSideRun);
}

std::vector<ControlViolation> FindRuleViolations(const ControlSeries& series,
                                                 const ControlOptions& options, size_t pane) {
    std::vector<ControlViolation> found;
    if (pane == 0 || options.rulesOnDispersion) PatternRules(series, options, found);
    else LimitRule(series, found);
    for (ControlViolation& v : found) v.pane = pane;
    std::sort(found.begin(), found.end(), [](const ControlViolation& a, const ControlViolation& b) {
        if (a.point != b.point) return a.point < b.point;
        return a.rule < b.rule;
    });
    return found;
}

ControlResult ComputeControlChart(const std::vector<ControlSubgroup>& subgroups,
                                  const ControlOptions& options) {
    ControlResult result;
    result.kind = options.kind;
    result.ruleSet = options.ruleSet;
    result.sameSideRun = SameSideLength(options);
    result.lowerSpec = options.lowerSpec;
    result.upperSpec = options.upperSpec;
    result.target = options.target;
    result.labels.reserve(subgroups.size());
    for (size_t i = 0; i < subgroups.size(); ++i) {
        result.labels.push_back(subgroups[i].label.empty() ? std::to_string(i + 1) : subgroups[i].label);
        result.excluded.push_back(subgroups[i].excluded);
        result.notes.push_back(subgroups[i].note);
    }
    Context ctx{subgroups, options, result, Stages(options.stages, subgroups.size(), result.stages)};
    if (subgroups.empty()) {
        result.location = EmptySeries("Value", 0, false);
        return result;
    }
    switch (options.kind) {
        case ControlChartKind::XbarR: XbarR(ctx); break;
        case ControlChartKind::XbarS: XbarS(ctx); break;
        case ControlChartKind::IndividualsMR: Individuals(ctx); break;
        default: Attributes(ctx); break;
    }
    Finish(result.location);
    if (result.dispersion) Finish(*result.dispersion);
    if ((Finite(options.lowerSpec) || Finite(options.upperSpec)) &&
        options.kind != ControlChartKind::XbarR && options.kind != ControlChartKind::XbarS &&
        options.kind != ControlChartKind::IndividualsMR) {
        ctx.Warn("Specification limits apply to measurement charts; they are left out here.");
        result.lowerSpec = result.upperSpec = result.target = kNaN;
    }

    for (size_t k = 0; k < ctx.stages.size(); ++k) {
        const StageRange& stage = ctx.stages[k];
        const size_t fed = ctx.FeedCount(stage);
        if (fed > 0 && fed < 20) {
            ctx.Warn(ctx.stages.size() > 1
                         ? StageLabel(stage, k, ctx.stages.size()) + "limits from " + Count(fed) +
                               " points (fewer than 20) are preliminary."
                         : "Only " + Count(fed) + " points: limits from fewer than 20 are preliminary.");
        }
        // No rule looks across a stage boundary.
        auto run = [&](const ControlSeries& series, size_t pane) {
            for (ControlViolation v : FindRuleViolations(Slice(series, stage.begin, stage.end), options, pane)) {
                v.point += stage.begin;
                v.firstPoint += stage.begin;
                result.violations.push_back(v);
            }
        };
        run(result.location, 0);
        if (result.dispersion) run(*result.dispersion, 1);
    }
    std::sort(result.violations.begin(), result.violations.end(),
              [](const ControlViolation& a, const ControlViolation& b) {
                  if (a.pane != b.pane) return a.pane < b.pane;
                  if (a.point != b.point) return a.point < b.point;
                  return a.rule < b.rule;
              });
    Capability(result, subgroups, ctx.stages.back());
    return result;
}

} // namespace UltraCanvas
