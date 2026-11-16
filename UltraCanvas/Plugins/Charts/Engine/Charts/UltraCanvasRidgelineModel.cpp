// Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.cpp
// The UI-free model of a ridgeline chart.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>

namespace UltraCanvas {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<double> FiniteSorted(const std::vector<double>& values) {
    std::vector<double> out;
    out.reserve(values.size());
    for (double v : values) {
        if (std::isfinite(v)) out.push_back(v);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Sample standard deviation (n - 1) of finite values; 0 below two.
double SampleDeviation(const std::vector<double>& sorted) {
    if (sorted.size() < 2) return 0.0;
    const double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
    double ss = 0.0;
    for (double v : sorted) ss += (v - mean) * (v - mean);
    return std::sqrt(ss / static_cast<double>(sorted.size() - 1));
}

// The curve's points with finite x and y, y clamped to >= 0, in increasing x.
std::vector<RidgelineCurvePoint> CleanCurve(const std::vector<RidgelineCurvePoint>& curve) {
    std::vector<RidgelineCurvePoint> out;
    out.reserve(curve.size());
    for (const RidgelineCurvePoint& p : curve) {
        if (std::isfinite(p.x) && std::isfinite(p.y)) out.push_back({p.x, std::max(0.0, p.y)});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const RidgelineCurvePoint& a, const RidgelineCurvePoint& b) { return a.x < b.x; });
    return out;
}

double CurvePeak(const std::vector<RidgelineCurvePoint>& curve) {
    double peak = 0.0;
    for (const RidgelineCurvePoint& p : curve) peak = std::max(peak, p.y);
    return peak;
}

double CurveArea(const std::vector<RidgelineCurvePoint>& curve) {
    double area = 0.0;
    for (size_t i = 1; i < curve.size(); ++i) area += (curve[i].x - curve[i - 1].x) * (curve[i].y + curve[i - 1].y) * 0.5;
    return area;
}

// The x at which the area under the curve (trapezoids) reaches `share` of
// the whole; each segment's area is spread evenly along it.
double CurveQuantile(const std::vector<RidgelineCurvePoint>& curve, double share) {
    const double total = CurveArea(curve);
    if (curve.empty()) return 0.0;
    if (total <= 0.0) return curve.front().x;
    const double target = std::clamp(share, 0.0, 1.0) * total;
    double running = 0.0;
    for (size_t i = 1; i < curve.size(); ++i) {
        const double piece = (curve[i].x - curve[i - 1].x) * (curve[i].y + curve[i - 1].y) * 0.5;
        if (running + piece >= target && piece > 0.0) {
            const double f = (target - running) / piece;
            return curve[i - 1].x + f * (curve[i].x - curve[i - 1].x);
        }
        running += piece;
    }
    return curve.back().x;
}

// A small, portable generator for the samples: the same numbers on every
// platform and standard library (std::normal_distribution is not).
struct SampleRandom {
    uint64_t state;
    explicit SampleRandom(uint64_t seed) : state(seed) {}
    double Uniform() {                       // SplitMix64, [0, 1)
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }
    double Normal() {                        // Box-Muller
        const double u1 = std::max(Uniform(), 1e-12);
        const double u2 = Uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * kPi * u2);
    }
};

struct Bump {
    double centre, width, weight;
};

} // namespace

// =============================================================================
// DENSITY
// =============================================================================

double RidgelineQuantile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const double h = std::clamp(p, 0.0, 1.0) * static_cast<double>(sorted.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(h));
    const size_t hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (h - static_cast<double>(lo)) * (sorted[hi] - sorted[lo]);
}

double RidgelineScottBandwidth(const std::vector<double>& values) {
    const std::vector<double> sorted = FiniteSorted(values);
    const double sigma = SampleDeviation(sorted);
    if (sigma <= 0.0) return 0.0;
    return 1.06 * sigma * std::pow(static_cast<double>(sorted.size()), -0.2);
}

double RidgelineSilvermanBandwidth(const std::vector<double>& values) {
    const std::vector<double> sorted = FiniteSorted(values);
    const double sigma = SampleDeviation(sorted);
    if (sigma <= 0.0) return 0.0;
    const double iqr = RidgelineQuantile(sorted, 0.75) - RidgelineQuantile(sorted, 0.25);
    const double spread = iqr > 0.0 ? std::min(sigma, iqr / 1.34) : sigma;
    return 0.9 * spread * std::pow(static_cast<double>(sorted.size()), -0.2);
}

std::vector<double> RidgelineKernelDensity(const std::vector<double>& values, const std::vector<double>& xs,
                                           double bandwidth, std::optional<double> lowerBound,
                                           std::optional<double> upperBound) {
    std::vector<double> out(xs.size(), 0.0);
    if (!(bandwidth > 0.0)) return out;
    const std::vector<double> sorted = FiniteSorted(values);
    if (sorted.empty()) return out;

    // Points and weights: the values themselves, or - for a large row - the
    // corners of a fine grid they are shared out to (linear binning).
    std::vector<double> points, weights;
    const size_t gridSize = std::max<size_t>(64, xs.size() * 4);
    if (sorted.size() > gridSize && sorted.back() > sorted.front()) {
        const double lo = sorted.front(), step = (sorted.back() - lo) / static_cast<double>(gridSize - 1);
        points.resize(gridSize);
        weights.assign(gridSize, 0.0);
        for (size_t j = 0; j < gridSize; ++j) points[j] = lo + step * static_cast<double>(j);
        for (double v : sorted) {
            const double t = (v - lo) / step;
            const size_t j = std::min(static_cast<size_t>(std::floor(t)), gridSize - 2);
            const double f = t - static_cast<double>(j);
            weights[j] += 1.0 - f;
            weights[j + 1] += f;
        }
    } else {
        points = sorted;
        weights.assign(sorted.size(), 1.0);
    }

    const double norm = 1.0 / (static_cast<double>(sorted.size()) * bandwidth * std::sqrt(2.0 * kPi));
    auto kernel = [bandwidth](double d) {
        const double z = d / bandwidth;
        return (z > -8.0 && z < 8.0) ? std::exp(-0.5 * z * z) : 0.0;
    };
    for (size_t i = 0; i < xs.size(); ++i) {
        const double x = xs[i];
        if ((lowerBound && x < *lowerBound) || (upperBound && x > *upperBound)) continue;
        double sum = 0.0;
        for (size_t k = 0; k < points.size(); ++k) {
            if (weights[k] == 0.0) continue;
            double s = kernel(x - points[k]);
            if (lowerBound) s += kernel(x - (2.0 * *lowerBound - points[k]));
            if (upperBound) s += kernel(x - (2.0 * *upperBound - points[k]));
            sum += weights[k] * s;
        }
        out[i] = sum * norm;
    }
    return out;
}

// =============================================================================
// MODEL
// =============================================================================

namespace {

double OwnBandwidth(const RidgelineDensityOptions& o, const std::vector<double>& values) {
    if (o.rule == RidgelineBandwidth::Fixed) return o.bandwidth;
    return o.rule == RidgelineBandwidth::Silverman ? RidgelineSilvermanBandwidth(values)
                                                   : RidgelineScottBandwidth(values);
}

} // namespace

double UltraCanvasRidgelineModel::Bandwidth(size_t row) const {
    if (row >= rows.size() || !rows[row].HasValues()) return 0.0;
    const double adjust = density.adjust > 0.0 ? density.adjust : 1.0;
    if (density.rule == RidgelineBandwidth::Fixed) return density.bandwidth > 0.0 ? density.bandwidth * adjust : 0.0;

    if (!density.sharedBandwidth) {
        const double own = OwnBandwidth(density, rows[row].values);
        if (own > 0.0) return own * adjust;
    }
    // The mean of the rows' own bandwidths: the shared one, and the fallback
    // for a row whose values are all the same.
    double sum = 0.0;
    int counted = 0;
    for (const RidgelineRow& r : rows) {
        if (!r.HasValues()) continue;
        const double h = OwnBandwidth(density, r.values);
        if (h > 0.0) { sum += h; ++counted; }
    }
    const double shared = counted > 0 ? sum / counted : 0.0;
    double h = density.sharedBandwidth ? shared : OwnBandwidth(density, rows[row].values);
    if (!(h > 0.0)) h = shared;
    if (!(h > 0.0)) {
        // Every row a single value: a narrow peak, 1 % of the span.
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const RidgelineRow& r : rows) {
            for (double v : r.values) {
                if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
            }
        }
        if (xMin && xMax) { lo = *xMin; hi = *xMax; }
        const double span = (std::isfinite(lo) && hi > lo) ? hi - lo : 10.0;
        h = span * 0.01;
    }
    return h * adjust;
}

std::pair<double, double> UltraCanvasRidgelineModel::XRange() const {
    if (xMin && xMax && *xMax > *xMin) return {*xMin, *xMax};
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (size_t i = 0; i < rows.size(); ++i) {
        const RidgelineRow& r = rows[i];
        if (r.HasValues()) {
            const std::vector<double> sorted = FiniteSorted(r.values);
            if (sorted.empty()) continue;
            const double h = Bandwidth(i);
            lo = std::min(lo, sorted.front() - 3.0 * h);
            hi = std::max(hi, sorted.back() + 3.0 * h);
        } else {
            for (const RidgelineCurvePoint& p : CleanCurve(r.curve)) {
                lo = std::min(lo, p.x);
                hi = std::max(hi, p.x);
            }
        }
    }
    if (!std::isfinite(lo)) { lo = 0.0; hi = 1.0; }
    if (density.lowerBound) lo = std::max(lo, *density.lowerBound);
    if (density.upperBound) hi = std::min(hi, *density.upperBound);
    if (xMin) lo = *xMin;
    if (xMax) hi = *xMax;
    if (!(hi > lo)) hi = lo + 1.0;
    return {lo, hi};
}

std::vector<RidgelineCurvePoint> UltraCanvasRidgelineModel::RawCurve(size_t row) const {
    std::vector<RidgelineCurvePoint> out;
    if (row >= rows.size()) return out;
    const RidgelineRow& r = rows[row];
    if (!r.HasValues()) return r.curve.size() >= 2 ? CleanCurve(r.curve) : out;

    auto [lo, hi] = XRange();
    if (density.lowerBound) lo = std::max(lo, *density.lowerBound);
    if (density.upperBound) hi = std::min(hi, *density.upperBound);
    if (!(hi > lo)) return out;
    const int n = std::max(16, density.resolution);
    std::vector<double> xs(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) xs[static_cast<size_t>(i)] = lo + (hi - lo) * i / (n - 1);
    const std::vector<double> ys =
        RidgelineKernelDensity(r.values, xs, Bandwidth(row), density.lowerBound, density.upperBound);
    out.reserve(xs.size());
    for (size_t i = 0; i < xs.size(); ++i) out.push_back({xs[i], ys[i]});
    return out;
}

std::vector<std::vector<RidgelineCurvePoint>> UltraCanvasRidgelineModel::Curves() const {
    std::vector<std::vector<RidgelineCurvePoint>> curves(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) curves[i] = RawCurve(i);

    if (scale == RidgelineScale::RowArea) {
        for (auto& curve : curves) {
            const double area = CurveArea(curve);
            if (area > 0.0) for (RidgelineCurvePoint& p : curve) p.y /= area;
        }
    }
    if (scale == RidgelineScale::RowPeak) {
        for (auto& curve : curves) {
            const double peak = CurvePeak(curve);
            if (peak > 0.0) for (RidgelineCurvePoint& p : curve) p.y /= peak;
        }
    } else {
        double peak = 0.0;
        for (const auto& curve : curves) peak = std::max(peak, CurvePeak(curve));
        if (peak > 0.0) {
            for (auto& curve : curves) for (RidgelineCurvePoint& p : curve) p.y /= peak;
        }
    }
    return curves;
}

std::vector<RidgelineCurvePoint> UltraCanvasRidgelineModel::Curve(size_t row) const {
    if (row >= rows.size()) return {};
    return Curves()[row];
}

RidgelineRowStatistics UltraCanvasRidgelineModel::Statistics(size_t row) const {
    RidgelineRowStatistics s;
    if (row >= rows.size()) return s;
    const RidgelineRow& r = rows[row];
    const std::vector<RidgelineCurvePoint> raw = RawCurve(row);
    if (!raw.empty()) {
        size_t top = 0;
        for (size_t i = 1; i < raw.size(); ++i) if (raw[i].y > raw[top].y) top = i;
        s.peakAt = raw[top].x;
    }

    if (r.HasValues()) {
        const std::vector<double> sorted = FiniteSorted(r.values);
        if (sorted.empty()) return s;
        s.valid = true;
        s.count = sorted.size();
        s.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
        s.standardDeviation = SampleDeviation(sorted);
        s.median = RidgelineQuantile(sorted, 0.5);
        s.quartile1 = RidgelineQuantile(sorted, 0.25);
        s.quartile3 = RidgelineQuantile(sorted, 0.75);
        s.minimum = sorted.front();
        s.maximum = sorted.back();
        return s;
    }

    // A supplied curve: its statistics are those of the distribution it draws.
    if (raw.size() < 2 || CurveArea(raw) <= 0.0) return s;
    s.valid = true;
    double weightSum = 0.0, moment = 0.0;
    for (size_t i = 0; i < raw.size(); ++i) {
        const double left = i > 0 ? raw[i].x - raw[i - 1].x : 0.0;
        const double right = i + 1 < raw.size() ? raw[i + 1].x - raw[i].x : 0.0;
        const double w = raw[i].y * (left + right) * 0.5;
        weightSum += w;
        moment += w * raw[i].x;
    }
    s.mean = weightSum > 0.0 ? moment / weightSum : 0.0;
    double ss = 0.0;
    for (size_t i = 0; i < raw.size(); ++i) {
        const double left = i > 0 ? raw[i].x - raw[i - 1].x : 0.0;
        const double right = i + 1 < raw.size() ? raw[i + 1].x - raw[i].x : 0.0;
        ss += raw[i].y * (left + right) * 0.5 * (raw[i].x - s.mean) * (raw[i].x - s.mean);
    }
    s.standardDeviation = weightSum > 0.0 ? std::sqrt(ss / weightSum) : 0.0;
    s.median = CurveQuantile(raw, 0.5);
    s.quartile1 = CurveQuantile(raw, 0.25);
    s.quartile3 = CurveQuantile(raw, 0.75);
    s.minimum = raw.front().x;
    s.maximum = raw.back().x;
    for (const RidgelineCurvePoint& p : raw) if (p.y > 0.0) { s.minimum = p.x; break; }
    for (auto it = raw.rbegin(); it != raw.rend(); ++it) if (it->y > 0.0) { s.maximum = it->x; break; }
    return s;
}

void UltraCanvasRidgelineModel::Sort(RidgelineSort by, bool descending) {
    if (by == RidgelineSort::AsGiven || rows.size() < 2) return;
    std::vector<RidgelineRowStatistics> stats(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) stats[i] = Statistics(i);
    std::vector<size_t> order(rows.size());
    std::iota(order.begin(), order.end(), 0);
    auto key = [&](size_t i) {
        switch (by) {
            case RidgelineSort::ByMedian: return stats[i].median;
            case RidgelineSort::ByPeak: return stats[i].peakAt;
            default: return stats[i].mean;
        }
    };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (stats[a].valid != stats[b].valid) return stats[a].valid;     // no data: last
        if (!stats[a].valid) return false;
        if (by == RidgelineSort::ByName) {
            return descending ? rows[b].name < rows[a].name : rows[a].name < rows[b].name;
        }
        return descending ? key(b) < key(a) : key(a) < key(b);
    });
    std::vector<RidgelineRow> sorted;
    sorted.reserve(rows.size());
    for (size_t i : order) sorted.push_back(rows[i]);
    rows = std::move(sorted);
}

std::vector<std::string> UltraCanvasRidgelineModel::Validate() const {
    std::vector<std::string> problems;
    if (rows.empty()) problems.push_back("The model has no rows.");
    if (density.rule == RidgelineBandwidth::Fixed && !(density.bandwidth > 0.0)) {
        problems.push_back("A fixed bandwidth must be above 0.");
    }
    if (density.lowerBound && density.upperBound && !(*density.upperBound > *density.lowerBound)) {
        problems.push_back("The upper bound must be above the lower bound.");
    }
    if (density.resolution < 16) problems.push_back("The resolution must be at least 16 points.");
    if (xMin && xMax && !(*xMax > *xMin)) problems.push_back("xMax must be above xMin.");

    bool integers = true, anyValues = false;
    for (const RidgelineRow& r : rows) {
        if (!r.HasData()) {
            problems.push_back("Row \"" + r.name + "\" has no values and no curve.");
            continue;
        }
        if (r.HasValues()) {
            anyValues = true;
            for (double v : r.values) {
                if (!std::isfinite(v)) continue;
                if (v != std::floor(v)) integers = false;
                if ((density.lowerBound && v < *density.lowerBound) || (density.upperBound && v > *density.upperBound)) {
                    problems.push_back("Row \"" + r.name + "\" has values outside the bounds.");
                    break;
                }
            }
        } else {
            for (size_t i = 1; i < r.curve.size(); ++i) {
                if (!(r.curve[i].x > r.curve[i - 1].x)) {
                    problems.push_back("Row \"" + r.name + "\": the curve's x values must increase.");
                    break;
                }
            }
        }
    }
    if (anyValues && integers && density.rule == RidgelineBandwidth::Fixed && density.bandwidth > 0.0 &&
        density.bandwidth * (density.adjust > 0.0 ? density.adjust : 1.0) < 0.5) {
        problems.push_back("The values are whole numbers: a bandwidth below 0.5 draws a spike at each.");
    }
    return problems;
}

// =============================================================================
// SAMPLES
// =============================================================================

UltraCanvasRidgelineModel UltraCanvasRidgelineModel::WordRatings() {
    // Invented: how positive each word might sound on a scale of 0 to 10.
    // Each word's answers come from one or two bumps, plus a few answers
    // anywhere on the scale (people who read the scale the other way round).
    struct Word { const char* name; std::vector<Bump> bumps; };
    const std::vector<Word> words = {
        {"Abysmal", {{0.3, 0.8, 1}}},           {"Appalling", {{0.5, 0.9, 1}}},
        {"Dreadful", {{0.6, 1.1, 1}}},          {"Awful", {{0.8, 1.2, 1}}},
        {"Terrible", {{0.8, 1.3, 1}}},          {"Very bad", {{1.0, 1.3, 1}}},
        {"Really bad", {{1.2, 1.4, 1}}},        {"Rubbish", {{0.5, 0.9, 0.5}, {3.0, 1.1, 0.5}}},
        {"Unsatisfactory", {{2.3, 1.4, 1}}},    {"Bad", {{2.3, 1.2, 1}}},
        {"Poor", {{2.6, 1.3, 1}}},              {"Pretty bad", {{2.7, 1.2, 1}}},
        {"Somewhat bad", {{3.2, 1.1, 1}}},      {"Below average", {{3.5, 0.9, 1}}},
        {"Mediocre", {{4.4, 0.9, 0.7}, {2.8, 1.2, 0.3}}},
        {"Average", {{5.0, 0.5, 1}}},           {"Not bad", {{5.3, 1.3, 1}}},
        {"Fair", {{5.4, 1.0, 1}}},              {"Alright", {{5.5, 0.9, 1}}},
        {"OK", {{5.4, 0.9, 1}}},                {"Satisfactory", {{5.8, 1.0, 1}}},
        {"Fine", {{5.0, 0.9, 0.5}, {6.6, 1.0, 0.5}}},
        {"Somewhat good", {{6.0, 0.9, 1}}},     {"Quite good", {{6.6, 1.0, 1}}},
        {"Decent", {{6.6, 1.1, 1}}},            {"Above average", {{6.7, 0.8, 1}}},
        {"Pretty good", {{6.9, 1.0, 1}}},       {"Good", {{7.0, 1.1, 1}}},
        {"Great", {{8.0, 1.0, 1}}},             {"Very good", {{8.0, 0.9, 1}}},
        {"Awesome", {{9.5, 0.9, 0.7}, {8.0, 1.0, 0.3}}},
        {"Excellent", {{9.3, 0.9, 1}}},         {"Perfect", {{9.9, 0.6, 1}}},
    };
    UltraCanvasRidgelineModel model;
    model.title = "How positive is a word?";
    model.subtitle = "Invented ratings from 0 (very negative) to 10 (very positive), 300 answers per word";
    model.xTitle = "Rating";
    model.density.rule = RidgelineBandwidth::Fixed;   // whole-number answers: see the guide
    model.density.bandwidth = 0.6;
    model.density.lowerBound = 0.0;
    model.density.upperBound = 10.0;
    model.scale = RidgelineScale::Shared;
    SampleRandom random(20201003);
    for (const Word& word : words) {
        RidgelineRow row;
        row.name = word.name;
        double total = 0.0;
        for (const Bump& b : word.bumps) total += b.weight;
        for (int i = 0; i < 300; ++i) {
            double v;
            if (random.Uniform() < 0.05) {
                v = random.Uniform() * 10.0;
            } else {
                double pick = random.Uniform() * total;
                const Bump* bump = &word.bumps.back();
                for (const Bump& b : word.bumps) {
                    if (pick < b.weight) { bump = &b; break; }
                    pick -= b.weight;
                }
                v = bump->centre + bump->width * random.Normal();
            }
            row.values.push_back(std::clamp(std::round(v), 0.0, 10.0));
        }
        model.rows.push_back(std::move(row));
    }
    model.Sort(RidgelineSort::ByMean);
    return model;
}

UltraCanvasRidgelineModel UltraCanvasRidgelineModel::DailyActivities() {
    // Invented: participants per quarter hour, from 03:00 to 03:00 the next
    // day (hours 3 to 27), as bumps around the times each activity is done.
    struct Activity { const char* name; std::vector<Bump> bumps; };
    const std::vector<Activity> activities = {
        {"Running", {{7.0, 1.0, 1}, {18.0, 1.2, 0.8}}},
        {"Walking", {{8.0, 1.5, 0.7}, {13.0, 2.5, 0.6}, {18.5, 1.5, 0.9}}},
        {"Doing aerobics", {{8.5, 1.0, 1}, {18.0, 1.0, 0.8}}},
        {"Using cardio equipment", {{6.5, 1.2, 1}, {17.5, 1.5, 0.9}}},
        {"Golfing", {{11.0, 2.2, 1}}},
        {"Hiking", {{12.5, 2.0, 1}}},
        {"Fishing", {{11.0, 3.5, 1}}},
        {"Boating", {{14.0, 2.5, 1}}},
        {"Playing baseball", {{17.5, 1.5, 1}, {11.0, 2.0, 0.3}}},
        {"Playing basketball", {{17.0, 2.0, 1}}},
        {"Playing soccer", {{17.5, 1.6, 1}}},
        {"Doing yoga", {{8.5, 1.2, 0.9}, {18.0, 1.2, 0.8}}},
        {"Biking", {{8.0, 1.5, 0.6}, {16.0, 2.5, 0.8}}},
        {"Bowling", {{19.5, 1.5, 1}}},
        {"Dancing", {{22.5, 2.0, 1}}},
        {"Playing billiards", {{22.0, 2.5, 1}}},
    };
    UltraCanvasRidgelineModel model;
    model.title = "When in the day";
    model.subtitle = "Invented participants through the day, each activity to its own peak";
    model.xTitle = "Time of day";
    model.curveName = "Participants (share of the peak)";
    model.scale = RidgelineScale::RowPeak;
    model.xMin = 3.0;
    model.xMax = 27.0;
    SampleRandom random(19740601);
    for (const Activity& activity : activities) {
        RidgelineRow row;
        row.name = activity.name;
        for (int q = 0; q <= 96; ++q) {
            const double hour = 3.0 + q * 0.25;
            double y = 0.0;
            for (const Bump& b : activity.bumps) {
                const double z = (hour - b.centre) / b.width;
                y += b.weight * std::exp(-0.5 * z * z);
            }
            y *= 1.0 + 0.04 * (random.Uniform() - 0.5);      // a little survey noise
            row.curve.push_back({hour, std::max(0.0, y)});
        }
        model.rows.push_back(std::move(row));
    }
    model.Sort(RidgelineSort::ByPeak, true);      // the latest peak at the top
    return model;
}

} // namespace UltraCanvas
