// Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.cpp
// The UI-free model of a ridgeline chart.
// Version: 1.1.0 - Phase 2: series, period, tail cut, binned curves, JSON / CSV, Sleep
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasRidgelineModel.h"
#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasCSVImport.h"
#include "UltraCanvasTextUtils.h"
#include <algorithm>
#include <cctype>
#include <map>
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
// ROWS
// =============================================================================

namespace {

const std::vector<double> kNoValues;
const std::vector<RidgelineCurvePoint> kNoCurve;

double OwnBandwidth(const RidgelineDensityOptions& o, const std::vector<double>& values) {
    if (o.rule == RidgelineBandwidth::Fixed) return o.bandwidth;
    return o.rule == RidgelineBandwidth::Silverman ? RidgelineSilvermanBandwidth(values)
                                                   : RidgelineScottBandwidth(values);
}

} // namespace

const std::vector<double>& RidgelineRow::ValuesOf(size_t s) const {
    if (s == 0) return values;
    return s - 1 < more.size() ? more[s - 1].values : kNoValues;
}

const std::vector<RidgelineCurvePoint>& RidgelineRow::CurveOf(size_t s) const {
    if (s == 0) return curve;
    return s - 1 < more.size() ? more[s - 1].curve : kNoCurve;
}

void RidgelineRow::SetValues(size_t s, std::vector<double> data) {
    if (s == 0) { values = std::move(data); return; }
    if (more.size() < s) more.resize(s);
    more[s - 1].values = std::move(data);
}

void RidgelineRow::SetCurve(size_t s, std::vector<RidgelineCurvePoint> data) {
    if (s == 0) { curve = std::move(data); return; }
    if (more.size() < s) more.resize(s);
    more[s - 1].curve = std::move(data);
}

// =============================================================================
// MODEL
// =============================================================================

size_t UltraCanvasRidgelineModel::SeriesCount() const {
    size_t count = std::max<size_t>(1, series.size());
    for (const RidgelineRow& r : rows) count = std::max(count, r.more.size() + 1);
    return count;
}

double UltraCanvasRidgelineModel::Wrap(double x) const {
    if (!period || !(*period > 0.0) || !std::isfinite(x)) return x;
    const double start = xMin ? *xMin : 0.0;
    double t = std::fmod(x - start, *period);
    if (t < 0.0) t += *period;
    return start + t;
}

namespace {

// The values as the model draws them: finite, wrapped, sorted.
std::vector<double> PreparedValues(const UltraCanvasRidgelineModel& m, const std::vector<double>& values) {
    std::vector<double> out;
    out.reserve(values.size());
    for (double v : values) {
        if (std::isfinite(v)) out.push_back(m.Wrap(v));
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

double UltraCanvasRidgelineModel::Bandwidth(size_t row, size_t s) const {
    if (row >= rows.size() || !rows[row].HasValuesOf(s)) return 0.0;
    const double adjust = density.adjust > 0.0 ? density.adjust : 1.0;
    if (density.rule == RidgelineBandwidth::Fixed) return density.bandwidth > 0.0 ? density.bandwidth * adjust : 0.0;

    if (!density.sharedBandwidth) {
        const double own = OwnBandwidth(density, PreparedValues(*this, rows[row].ValuesOf(s)));
        if (own > 0.0) return own * adjust;
    }
    // The mean of every values curve's own bandwidth: the shared one, and the
    // fallback for a curve whose values are all the same.
    double sum = 0.0;
    int counted = 0;
    const size_t seriesCount = SeriesCount();
    for (const RidgelineRow& r : rows) {
        for (size_t k = 0; k < seriesCount; ++k) {
            if (!r.HasValuesOf(k)) continue;
            const double h = OwnBandwidth(density, PreparedValues(*this, r.ValuesOf(k)));
            if (h > 0.0) { sum += h; ++counted; }
        }
    }
    double h = counted > 0 ? sum / counted : 0.0;
    if (!(h > 0.0)) {
        // Every curve a single value: a narrow peak, 1 % of the span.
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const RidgelineRow& r : rows) {
            for (size_t k = 0; k < seriesCount; ++k) {
                for (double v : r.ValuesOf(k)) {
                    if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
                }
            }
        }
        if (xMin && xMax) { lo = *xMin; hi = *xMax; }
        if (period) { lo = 0.0; hi = *period; }
        const double span = (std::isfinite(lo) && hi > lo) ? hi - lo : 10.0;
        h = span * 0.01;
    }
    return h * adjust;
}

std::pair<double, double> UltraCanvasRidgelineModel::XRange() const {
    if (period && *period > 0.0) {
        const double start = xMin ? *xMin : 0.0;
        return {start, start + *period};
    }
    if (xMin && xMax && *xMax > *xMin) return {*xMin, *xMax};
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    const size_t seriesCount = SeriesCount();
    for (size_t i = 0; i < rows.size(); ++i) {
        for (size_t s = 0; s < seriesCount; ++s) {
            const RidgelineRow& r = rows[i];
            if (r.HasValuesOf(s)) {
                const std::vector<double> sorted = FiniteSorted(r.ValuesOf(s));
                if (sorted.empty()) continue;
                const double h = density.binned ? 0.0 : Bandwidth(i, s);
                lo = std::min(lo, sorted.front() - 3.0 * h);
                hi = std::max(hi, sorted.back() + 3.0 * h);
            } else {
                for (const RidgelineCurvePoint& p : CleanCurve(r.CurveOf(s))) {
                    lo = std::min(lo, p.x);
                    hi = std::max(hi, p.x);
                }
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

namespace {

// Drops the leading and trailing points below `cut` of the peak, keeping one
// point on each side so the curve still meets its baseline.
void CutTails(std::vector<RidgelineCurvePoint>& curve, double cut) {
    if (!(cut > 0.0) || curve.size() < 3) return;
    const double limit = CurvePeak(curve) * std::min(cut, 0.5);
    size_t first = 0, last = curve.size() - 1;
    while (first < last && curve[first].y < limit) ++first;
    while (last > first && curve[last].y < limit) --last;
    first = first > 0 ? first - 1 : 0;
    last = std::min(last + 1, curve.size() - 1);
    curve = std::vector<RidgelineCurvePoint>(curve.begin() + static_cast<std::ptrdiff_t>(first),
                                             curve.begin() + static_cast<std::ptrdiff_t>(last) + 1);
}

} // namespace

std::vector<RidgelineCurvePoint> UltraCanvasRidgelineModel::RawCurve(size_t row, size_t s) const {
    std::vector<RidgelineCurvePoint> out;
    if (row >= rows.size()) return out;
    const RidgelineRow& r = rows[row];
    const bool wraps = period && *period > 0.0;

    if (!r.HasValuesOf(s)) {
        if (r.CurveOf(s).size() < 2) return out;
        if (!wraps) {
            out = CleanCurve(r.CurveOf(s));
        } else {
            // A supplied curve on a wrapping axis: each point moved into the
            // period, then put back in order.
            for (const RidgelineCurvePoint& p : CleanCurve(r.CurveOf(s))) out.push_back({Wrap(p.x), p.y});
            std::stable_sort(out.begin(), out.end(),
                             [](const RidgelineCurvePoint& a, const RidgelineCurvePoint& b) { return a.x < b.x; });
        }
        CutTails(out, density.tailCut);
        return out;
    }

    const std::vector<double> values = PreparedValues(*this, r.ValuesOf(s));
    if (values.empty()) return out;
    auto [lo, hi] = XRange();
    if (!wraps) {
        if (density.lowerBound) lo = std::max(lo, *density.lowerBound);
        if (density.upperBound) hi = std::min(hi, *density.upperBound);
    }
    if (!(hi > lo)) return out;

    if (density.binned) {
        // Equal bins over the x range; each a flat top at count / (n width),
        // so the steps have the same area as the density would.
        const int bins = std::clamp(density.bins, 1, 1000);
        const double width = (hi - lo) / bins;
        std::vector<double> counts(static_cast<size_t>(bins), 0.0);
        for (double v : values) {
            if (v < lo || v > hi) continue;
            const int k = std::min(bins - 1, static_cast<int>((v - lo) / width));
            counts[static_cast<size_t>(k)] += 1.0;
        }
        const double scale = 1.0 / (static_cast<double>(values.size()) * width);
        out.reserve(static_cast<size_t>(bins) * 2);
        for (int k = 0; k < bins; ++k) {
            const double h = counts[static_cast<size_t>(k)] * scale;
            out.push_back({lo + width * k, h});
            out.push_back({lo + width * (k + 1), h});
        }
        CutTails(out, density.tailCut);
        return out;
    }

    const int n = std::max(16, density.resolution);
    std::vector<double> xs(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) xs[static_cast<size_t>(i)] = lo + (hi - lo) * i / (n - 1);
    std::vector<double> ys;
    const double h = Bandwidth(row, s);
    if (wraps) {
        // A circular density: each value also one period before and after,
        // so a kernel near one end of the axis comes back in at the other.
        std::vector<double> copies;
        copies.reserve(values.size() * 3);
        for (double v : values) {
            copies.push_back(v - *period);
            copies.push_back(v);
            copies.push_back(v + *period);
        }
        ys = RidgelineKernelDensity(copies, xs, h);
        for (double& y : ys) y *= 3.0;
    } else {
        ys = RidgelineKernelDensity(values, xs, h, density.lowerBound, density.upperBound);
    }
    out.reserve(xs.size());
    for (size_t i = 0; i < xs.size(); ++i) out.push_back({xs[i], ys[i]});
    CutTails(out, density.tailCut);
    return out;
}

std::vector<std::vector<std::vector<RidgelineCurvePoint>>> UltraCanvasRidgelineModel::AllCurves() const {
    const size_t seriesCount = SeriesCount();
    std::vector<std::vector<std::vector<RidgelineCurvePoint>>> curves(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        curves[i].resize(seriesCount);
        for (size_t s = 0; s < seriesCount; ++s) curves[i][s] = RawCurve(i, s);
    }
    if (scale == RidgelineScale::RowArea) {
        for (auto& row : curves) {
            for (auto& curve : row) {
                const double area = CurveArea(curve);
                if (area > 0.0) for (RidgelineCurvePoint& p : curve) p.y /= area;
            }
        }
    }
    if (scale == RidgelineScale::RowPeak) {
        for (auto& row : curves) {
            double peak = 0.0;
            for (const auto& curve : row) peak = std::max(peak, CurvePeak(curve));
            if (peak > 0.0) for (auto& curve : row) for (RidgelineCurvePoint& p : curve) p.y /= peak;
        }
    } else {
        double peak = 0.0;
        for (const auto& row : curves) for (const auto& curve : row) peak = std::max(peak, CurvePeak(curve));
        if (peak > 0.0) {
            for (auto& row : curves) for (auto& curve : row) for (RidgelineCurvePoint& p : curve) p.y /= peak;
        }
    }
    return curves;
}

std::vector<std::vector<RidgelineCurvePoint>> UltraCanvasRidgelineModel::Curves(size_t s) const {
    const auto all = AllCurves();
    std::vector<std::vector<RidgelineCurvePoint>> out(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        if (s < all[i].size()) out[i] = all[i][s];
    }
    return out;
}

std::vector<RidgelineCurvePoint> UltraCanvasRidgelineModel::Curve(size_t row, size_t s) const {
    if (row >= rows.size()) return {};
    return Curves(s)[row];
}

RidgelineRowStatistics UltraCanvasRidgelineModel::Statistics(size_t row, size_t s) const {
    RidgelineRowStatistics st;
    if (row >= rows.size()) return st;
    const RidgelineRow& r = rows[row];
    std::vector<RidgelineCurvePoint> raw = RawCurve(row, s);
    if (!raw.empty()) {
        size_t top = 0;
        for (size_t i = 1; i < raw.size(); ++i) if (raw[i].y > raw[top].y) top = i;
        st.peakAt = raw[top].x;
        if (density.binned && r.HasValuesOf(s) && top + 1 < raw.size()) st.peakAt = (raw[top].x + raw[top + 1].x) * 0.5;
    }

    if (r.HasValuesOf(s)) {
        const std::vector<double> sorted = PreparedValues(*this, r.ValuesOf(s));
        if (sorted.empty()) return st;
        st.valid = true;
        st.count = sorted.size();
        st.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
        st.standardDeviation = SampleDeviation(sorted);
        st.median = RidgelineQuantile(sorted, 0.5);
        st.quartile1 = RidgelineQuantile(sorted, 0.25);
        st.quartile3 = RidgelineQuantile(sorted, 0.75);
        st.minimum = sorted.front();
        st.maximum = sorted.back();
        return st;
    }

    // A supplied curve: its statistics are those of the distribution it
    // draws (before any tail cut).
    if (density.tailCut > 0.0) {
        UltraCanvasRidgelineModel uncut = *this;
        uncut.density.tailCut = 0.0;
        raw = uncut.RawCurve(row, s);
    }
    if (raw.size() < 2 || CurveArea(raw) <= 0.0) return st;
    st.valid = true;
    double weightSum = 0.0, moment = 0.0;
    for (size_t i = 0; i < raw.size(); ++i) {
        const double left = i > 0 ? raw[i].x - raw[i - 1].x : 0.0;
        const double right = i + 1 < raw.size() ? raw[i + 1].x - raw[i].x : 0.0;
        const double w = raw[i].y * (left + right) * 0.5;
        weightSum += w;
        moment += w * raw[i].x;
    }
    st.mean = weightSum > 0.0 ? moment / weightSum : 0.0;
    double ss = 0.0;
    for (size_t i = 0; i < raw.size(); ++i) {
        const double left = i > 0 ? raw[i].x - raw[i - 1].x : 0.0;
        const double right = i + 1 < raw.size() ? raw[i + 1].x - raw[i].x : 0.0;
        ss += raw[i].y * (left + right) * 0.5 * (raw[i].x - st.mean) * (raw[i].x - st.mean);
    }
    st.standardDeviation = weightSum > 0.0 ? std::sqrt(ss / weightSum) : 0.0;
    st.median = CurveQuantile(raw, 0.5);
    st.quartile1 = CurveQuantile(raw, 0.25);
    st.quartile3 = CurveQuantile(raw, 0.75);
    st.minimum = raw.front().x;
    st.maximum = raw.back().x;
    for (const RidgelineCurvePoint& p : raw) if (p.y > 0.0) { st.minimum = p.x; break; }
    for (auto it = raw.rbegin(); it != raw.rend(); ++it) if (it->y > 0.0) { st.maximum = it->x; break; }
    return st;
}

void UltraCanvasRidgelineModel::Sort(RidgelineSort by, bool descending) {
    if (by == RidgelineSort::AsGiven || rows.size() < 2) return;
    // Each row by its first series with data.
    const size_t seriesCount = SeriesCount();
    std::vector<RidgelineRowStatistics> stats(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        for (size_t s = 0; s < seriesCount; ++s) {
            if (!rows[i].HasDataOf(s)) continue;
            stats[i] = Statistics(i, s);
            break;
        }
    }
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
    if (density.binned && (density.bins < 1 || density.bins > 1000)) problems.push_back("Bins must be 1 to 1000.");
    if (density.tailCut < 0.0 || density.tailCut > 0.5) problems.push_back("The tail cut must be 0 to 0.5.");
    if (xMin && xMax && !(*xMax > *xMin)) problems.push_back("xMax must be above xMin.");
    if (period && !(*period > 0.0)) problems.push_back("The period must be above 0.");

    const size_t seriesCount = SeriesCount();
    bool integers = true, anyValues = false;
    for (const RidgelineRow& r : rows) {
        if (!series.empty() && r.more.size() + 1 > series.size()) {
            problems.push_back("Row \"" + r.name + "\" has more series than the model names.");
        }
        bool any = false;
        for (size_t s = 0; s < seriesCount; ++s) {
            if (!r.HasDataOf(s)) continue;
            any = true;
            if (r.HasValuesOf(s)) {
                anyValues = true;
                for (double v : r.ValuesOf(s)) {
                    if (!std::isfinite(v)) continue;
                    if (v != std::floor(v)) integers = false;
                    if (!period && ((density.lowerBound && v < *density.lowerBound) ||
                                    (density.upperBound && v > *density.upperBound))) {
                        problems.push_back("Row \"" + r.name + "\" has values outside the bounds.");
                        break;
                    }
                }
            } else {
                const std::vector<RidgelineCurvePoint>& c = r.CurveOf(s);
                for (size_t i = 1; i < c.size(); ++i) {
                    if (!(c[i].x > c[i - 1].x)) {
                        problems.push_back("Row \"" + r.name + "\": the curve's x values must increase.");
                        break;
                    }
                }
            }
        }
        if (!any) problems.push_back("Row \"" + r.name + "\" has no values and no curve.");
    }
    if (anyValues && integers && !density.binned && density.rule == RidgelineBandwidth::Fixed &&
        density.bandwidth > 0.0 && density.bandwidth * (density.adjust > 0.0 ? density.adjust : 1.0) < 0.5) {
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

UltraCanvasRidgelineModel UltraCanvasRidgelineModel::Sleep() {
    // Invented: when people of each age group might go to bed and wake up,
    // recorded as clock times (0 to 24) to the nearest five minutes. The axis
    // runs from 18:00 to 18:00 and wraps, so 00:30 lands after 23:30.
    struct Group { const char* name; Bump bed, wake; };
    const std::vector<Group> groups = {
        {"13-17", {23.4, 0.9, 1}, {7.2, 1.2, 1}},
        {"18-24", {0.6, 1.2, 1}, {8.6, 1.5, 1}},
        {"25-34", {23.7, 1.0, 1}, {7.4, 1.1, 1}},
        {"35-44", {23.2, 0.9, 1}, {6.8, 0.9, 1}},
        {"45-54", {23.0, 0.9, 1}, {6.6, 0.9, 1}},
        {"55-64", {22.8, 1.0, 1}, {6.5, 1.0, 1}},
        {"65+", {22.4, 1.1, 1}, {6.9, 1.2, 1}},
    };
    UltraCanvasRidgelineModel model;
    model.title = "When people sleep";
    model.subtitle = "Invented bed and wake-up times, 250 people per age group";
    model.xTitle = "Time of day";
    model.series = {{"Goes to bed", Color(0, 0, 0, 0)}, {"Wakes up", Color(0, 0, 0, 0)}};
    model.scale = RidgelineScale::Shared;
    model.xMin = 18.0;
    model.period = 24.0;
    SampleRandom random(19991231);
    auto clock = [&random](const Bump& b) {
        double v = b.centre + b.width * random.Normal();
        v = std::fmod(v, 24.0);
        if (v < 0.0) v += 24.0;
        return std::round(v * 12.0) / 12.0;
    };
    for (const Group& g : groups) {
        RidgelineRow row;
        row.name = g.name;
        std::vector<double> bed, wake;
        for (int i = 0; i < 250; ++i) bed.push_back(clock(g.bed));
        for (int i = 0; i < 250; ++i) wake.push_back(clock(g.wake));
        row.SetValues(0, std::move(bed));
        row.SetValues(1, std::move(wake));
        model.rows.push_back(std::move(row));
    }
    return model;
}

// =============================================================================
// INTERCHANGE
// =============================================================================

namespace {

constexpr const char* kRidgelineFormat = "ultracanvas-ridgeline";
constexpr int kRidgelineFormatVersion = 1;

const char* RuleName(RidgelineBandwidth r) {
    switch (r) {
        case RidgelineBandwidth::Silverman: return "silverman";
        case RidgelineBandwidth::Fixed: return "fixed";
        default: return "scott";
    }
}

const char* ScaleName(RidgelineScale s) {
    switch (s) {
        case RidgelineScale::RowPeak: return "rowPeak";
        case RidgelineScale::RowArea: return "rowArea";
        default: return "shared";
    }
}

JSONValue NumbersToJson(const std::vector<double>& values) {
    JSONValue array = JSONValue::MakeArray();
    for (double v : values) array.Append(JSONValue(v));
    return array;
}

JSONValue CurveToJson(const std::vector<RidgelineCurvePoint>& curve) {
    JSONValue array = JSONValue::MakeArray();
    for (const RidgelineCurvePoint& p : curve) {
        JSONValue pair = JSONValue::MakeArray();
        pair.Append(JSONValue(p.x));
        pair.Append(JSONValue(p.y));
        array.Append(pair);
    }
    return array;
}

void EntryToJson(JSONValue& object, const std::vector<double>& values, const std::vector<RidgelineCurvePoint>& curve) {
    if (!values.empty()) object.Set("values", NumbersToJson(values));
    else if (!curve.empty()) object.Set("curve", CurveToJson(curve));
}

bool NumbersFromJson(const JSONValue& json, std::vector<double>& out) {
    if (json.IsNull()) return true;
    if (!json.IsArray()) return false;
    for (const JSONValue& v : json.GetElements()) {
        if (!v.IsNumber()) return false;
        out.push_back(v.GetNumber());
    }
    return true;
}

bool CurveFromJson(const JSONValue& json, std::vector<RidgelineCurvePoint>& out) {
    if (json.IsNull()) return true;
    if (!json.IsArray()) return false;
    for (const JSONValue& p : json.GetElements()) {
        if (!p.IsArray() || p.GetSize() != 2 || !p.At(0).IsNumber() || !p.At(1).IsNumber()) return false;
        out.push_back({p.At(0).GetNumber(), p.At(1).GetNumber()});
    }
    return true;
}

bool ReadOptionalNumber(const JSONValue& object, const char* key, std::optional<double>& out) {
    if (!object.Contains(key)) return true;
    if (!object.Get(key).IsNumber()) return false;
    out = object.Get(key).GetNumber();
    return true;
}

bool ReadColor(const JSONValue& object, const char* key, Color& out) {
    if (!object.Contains(key)) return true;
    return JSON::ToColor(object.Get(key), out);
}

std::string LowerTrim(const std::string& text) {
    std::string lower = Trim(text);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

bool ParseNumber(const std::string& text, double& out) {
    const std::string t = Trim(text);
    if (t.empty()) return false;
    double value = 0.0;
    const char* end = ParseFloatClassic(t.data(), t.data() + t.size(), value);
    if (end != t.data() + t.size() || !std::isfinite(value)) return false;
    out = value;
    return true;
}

} // namespace

JSONValue RidgelineModelToJson(const UltraCanvasRidgelineModel& model) {
    JSONValue doc = JSONValue::MakeObject();
    doc.Set("format", kRidgelineFormat);
    doc.Set("version", kRidgelineFormatVersion);
    auto text = [&doc](const char* key, const std::string& value) {
        if (!value.empty()) doc.Set(key, value);
    };
    text("title", model.title);
    text("subtitle", model.subtitle);
    text("xTitle", model.xTitle);
    if (model.curveName != "Density") doc.Set("curveName", model.curveName);

    if (!model.series.empty()) {
        JSONValue series = JSONValue::MakeArray();
        for (const RidgelineSeries& s : model.series) {
            JSONValue j = JSONValue::MakeObject();
            j.Set("name", s.name);
            if (s.color.a != 0) j.Set("color", JSON::FromColor(s.color));
            series.Append(j);
        }
        doc.Set("series", series);
    }

    JSONValue rows = JSONValue::MakeArray();
    for (const RidgelineRow& r : model.rows) {
        JSONValue j = JSONValue::MakeObject();
        j.Set("name", r.name);
        if (r.color.a != 0) j.Set("color", JSON::FromColor(r.color));
        if (!r.note.empty()) j.Set("note", r.note);
        EntryToJson(j, r.values, r.curve);
        if (!r.more.empty()) {
            JSONValue more = JSONValue::MakeArray();
            for (const RidgelineEntry& e : r.more) {
                JSONValue m = JSONValue::MakeObject();
                EntryToJson(m, e.values, e.curve);
                more.Append(m);
            }
            j.Set("more", more);
        }
        rows.Append(j);
    }
    doc.Set("rows", rows);

    const RidgelineDensityOptions defaults;
    const RidgelineDensityOptions& d = model.density;
    JSONValue density = JSONValue::MakeObject();
    if (d.rule != defaults.rule) density.Set("rule", RuleName(d.rule));
    if (d.bandwidth != defaults.bandwidth) density.Set("bandwidth", d.bandwidth);
    if (d.adjust != defaults.adjust) density.Set("adjust", d.adjust);
    if (d.sharedBandwidth) density.Set("shared", true);
    if (d.lowerBound) density.Set("lowerBound", *d.lowerBound);
    if (d.upperBound) density.Set("upperBound", *d.upperBound);
    if (d.resolution != defaults.resolution) density.Set("resolution", d.resolution);
    if (d.tailCut != defaults.tailCut) density.Set("tailCut", d.tailCut);
    if (d.binned) density.Set("binned", true);
    if (d.bins != defaults.bins) density.Set("bins", d.bins);
    if (density.GetSize() > 0) doc.Set("density", density);

    if (model.scale != RidgelineScale::Shared) doc.Set("scale", ScaleName(model.scale));
    if (model.xMin) doc.Set("xMin", *model.xMin);
    if (model.xMax) doc.Set("xMax", *model.xMax);
    if (model.period) doc.Set("period", *model.period);
    return doc;
}

std::string ExportRidgelineJson(const UltraCanvasRidgelineModel& model, bool pretty) {
    JSONSerializeOptions options;
    options.pretty = pretty;
    options.indentWidth = 2;
    return JSON::Serialize(RidgelineModelToJson(model), options);
}

bool RidgelineModelFromJson(const JSONValue& json, UltraCanvasRidgelineModel& out, std::string* error) {
    auto failWith = [error](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (!json.IsObject()) return failWith("the document is not a JSON object");
    if (json.Get("format").GetString() != kRidgelineFormat) {
        return failWith("not a ridgeline chart (format is not \"" + std::string(kRidgelineFormat) + "\")");
    }
    const int64_t version = json.Get("version").GetInteger(0);
    if (version < 1 || version > kRidgelineFormatVersion) {
        return failWith("format version " + std::to_string(version) + " is not one this build reads");
    }

    UltraCanvasRidgelineModel model;
    model.title = json.Get("title").GetString();
    model.subtitle = json.Get("subtitle").GetString();
    model.xTitle = json.Get("xTitle").GetString();
    model.curveName = json.Get("curveName").GetString("Density");

    for (const JSONValue& j : json.Get("series").GetElements()) {
        RidgelineSeries s;
        s.name = j.Get("name").GetString();
        if (!ReadColor(j, "color", s.color)) return failWith("series '" + s.name + "' has an unreadable colour");
        model.series.push_back(s);
    }
    for (const JSONValue& j : json.Get("rows").GetElements()) {
        RidgelineRow r;
        r.name = j.Get("name").GetString();
        r.note = j.Get("note").GetString();
        const std::string where = "row '" + r.name + "'";
        if (!ReadColor(j, "color", r.color)) return failWith(where + " has an unreadable colour");
        if (!NumbersFromJson(j.Get("values"), r.values)) return failWith(where + ": \"values\" is not a list of numbers");
        if (!CurveFromJson(j.Get("curve"), r.curve)) return failWith(where + ": \"curve\" is not a list of [x, y] pairs");
        for (const JSONValue& m : j.Get("more").GetElements()) {
            RidgelineEntry e;
            if (!NumbersFromJson(m.Get("values"), e.values) || !CurveFromJson(m.Get("curve"), e.curve)) {
                return failWith(where + ": a further series is not values or a curve");
            }
            r.more.push_back(std::move(e));
        }
        model.rows.push_back(std::move(r));
    }

    const JSONValue& d = json.Get("density");
    const std::string rule = d.Get("rule").GetString("scott");
    if (rule == "scott") model.density.rule = RidgelineBandwidth::Scott;
    else if (rule == "silverman") model.density.rule = RidgelineBandwidth::Silverman;
    else if (rule == "fixed") model.density.rule = RidgelineBandwidth::Fixed;
    else return failWith("unknown bandwidth rule '" + rule + "'");
    model.density.bandwidth = d.Get("bandwidth").GetNumber(0.0);
    model.density.adjust = d.Get("adjust").GetNumber(1.0);
    model.density.sharedBandwidth = d.Get("shared").GetBoolean(false);
    if (!ReadOptionalNumber(d, "lowerBound", model.density.lowerBound) ||
        !ReadOptionalNumber(d, "upperBound", model.density.upperBound)) {
        return failWith("a bound is not a number");
    }
    model.density.resolution = static_cast<int>(d.Get("resolution").GetInteger(256));
    model.density.tailCut = d.Get("tailCut").GetNumber(0.0);
    model.density.binned = d.Get("binned").GetBoolean(false);
    model.density.bins = static_cast<int>(d.Get("bins").GetInteger(20));

    const std::string scale = json.Get("scale").GetString("shared");
    if (scale == "shared") model.scale = RidgelineScale::Shared;
    else if (scale == "rowPeak") model.scale = RidgelineScale::RowPeak;
    else if (scale == "rowArea") model.scale = RidgelineScale::RowArea;
    else return failWith("unknown scale '" + scale + "'");
    if (!ReadOptionalNumber(json, "xMin", model.xMin) || !ReadOptionalNumber(json, "xMax", model.xMax) ||
        !ReadOptionalNumber(json, "period", model.period)) {
        return failWith("xMin, xMax or period is not a number");
    }

    const std::vector<std::string> problems = model.Validate();
    if (!problems.empty()) return failWith(problems.front());
    out = std::move(model);
    return true;
}

bool ImportRidgelineJson(const std::string& text, UltraCanvasRidgelineModel& out, std::string* error) {
    JSONParseResult parsed;
    const JSONValue json = JSON::Parse(text, &parsed);
    if (!parsed.success) {
        if (error) {
            *error = "not valid JSON: " + parsed.errorMessage + " at line " + std::to_string(parsed.errorLine) +
                     ", column " + std::to_string(parsed.errorColumn);
        }
        return false;
    }
    return RidgelineModelFromJson(json, out, error);
}

std::string ExportRidgelineCsv(const UltraCanvasRidgelineModel& model) {
    const size_t seriesCount = model.SeriesCount();
    bool anyValues = false, anyCurves = false;
    for (const RidgelineRow& r : model.rows) {
        for (size_t s = 0; s < seriesCount; ++s) {
            if (r.HasValuesOf(s)) anyValues = true;
            else if (!r.CurveOf(s).empty()) anyCurves = true;
        }
    }
    const bool withSeries = seriesCount > 1;
    std::vector<std::string> header = {"row"};
    if (withSeries) header.push_back("series");
    if (anyValues || !anyCurves) header.push_back("value");
    if (anyCurves) { header.push_back("x"); header.push_back("y"); }
    const CSVExportOptions csvOptions;
    std::string csv = CSVFormatRecord(header, csvOptions);

    auto seriesName = [&model](size_t s) {
        return s < model.series.size() && !model.series[s].name.empty() ? model.series[s].name
                                                                       : "Series " + std::to_string(s + 1);
    };
    for (const RidgelineRow& r : model.rows) {
        for (size_t s = 0; s < seriesCount; ++s) {
            auto line = [&](const std::string& value, const std::string& x, const std::string& y) {
                std::vector<std::string> fields = {r.name};
                if (withSeries) fields.push_back(seriesName(s));
                if (anyValues || !anyCurves) fields.push_back(value);
                if (anyCurves) { fields.push_back(x); fields.push_back(y); }
                csv += CSVFormatRecord(fields, csvOptions);
            };
            if (r.HasValuesOf(s)) {
                for (double v : r.ValuesOf(s)) line(FormatFloatClassic(v, 17), std::string(), std::string());
            } else {
                for (const RidgelineCurvePoint& p : r.CurveOf(s)) {
                    line(std::string(), FormatFloatClassic(p.x, 17), FormatFloatClassic(p.y, 17));
                }
            }
        }
    }
    return csv;
}

bool ImportRidgelineCsv(const std::string& csv, UltraCanvasRidgelineModel& model, std::string* error) {
    auto failWith = [error](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    CSVImportOptions options;
    options.detectNumbers = false;
    const CSVGrid grid = CSVParse(CSVDecodeToUtf8(csv, CSVImportOptions::Encoding::UTF8), options);
    auto blank = [](const CSVRow& row) { return row.size() == 1 && Trim(row[0].text).empty(); };
    size_t line = 0;
    while (line < grid.size() && blank(grid[line])) ++line;
    if (line == grid.size()) return failWith("the file is empty");

    const CSVRow& header = grid[line];
    std::map<std::string, size_t> columns;
    for (size_t c = 0; c < header.size(); ++c) {
        const std::string name = LowerTrim(header[c].text);
        if (!name.empty() && !columns.count(name)) columns[name] = c;
    }
    size_t rowColumn = SIZE_MAX;
    for (const char* name : {"row", "name", "group", "category"}) {
        if (rowColumn == SIZE_MAX && columns.count(name)) rowColumn = columns[name];
    }
    if (rowColumn == SIZE_MAX) return failWith("the header has no row column (row, name, group or category)");
    const bool hasValue = columns.count("value") > 0;
    const bool hasCurve = columns.count("x") > 0 && columns.count("y") > 0;
    if (!hasValue && !hasCurve) return failWith("the header has neither a value column nor x and y");
    const size_t seriesColumn = columns.count("series") ? columns["series"] : SIZE_MAX;

    std::vector<RidgelineSeries> series;
    std::vector<RidgelineRow> rows;
    auto seriesIndex = [&](const std::string& name) {
        for (size_t i = 0; i < series.size(); ++i) if (series[i].name == name) return i;
        RidgelineSeries s;
        s.name = name;
        for (const RidgelineSeries& old : model.series) if (old.name == name) s.color = old.color;
        series.push_back(s);
        return series.size() - 1;
    };
    for (++line; line < grid.size(); ++line) {
        const CSVRow& row = grid[line];
        if (blank(row)) continue;
        const std::string where = "line " + std::to_string(line + 1);
        if (row.size() != header.size()) {
            return failWith(where + " has " + std::to_string(row.size()) + " fields, the header " +
                            std::to_string(header.size()));
        }
        const std::string name = Trim(row[rowColumn].text);
        const size_t s = seriesColumn == SIZE_MAX ? 0 : seriesIndex(Trim(row[seriesColumn].text));
        auto it = std::find_if(rows.begin(), rows.end(), [&name](const RidgelineRow& r) { return r.name == name; });
        if (it == rows.end()) {
            RidgelineRow r;
            r.name = name;
            // A row the model already has keeps its colour and note.
            for (const RidgelineRow& old : model.rows) {
                if (old.name == name) { r.color = old.color; r.note = old.note; }
            }
            rows.push_back(std::move(r));
            it = rows.end() - 1;
        }
        const std::string value = hasValue ? Trim(row[columns["value"]].text) : std::string();
        if (!value.empty()) {
            double v = 0.0;
            if (!ParseNumber(value, v)) return failWith(where + ": '" + value + "' is not a number");
            std::vector<double> values = it->ValuesOf(s);
            values.push_back(v);
            it->SetValues(s, std::move(values));
            continue;
        }
        if (!hasCurve) continue;
        double x = 0.0, y = 0.0;
        const std::string xs = Trim(row[columns["x"]].text), ys = Trim(row[columns["y"]].text);
        if (xs.empty() && ys.empty()) continue;
        if (!ParseNumber(xs, x) || !ParseNumber(ys, y)) {
            return failWith(where + ": '" + xs + "', '" + ys + "' is not a point");
        }
        std::vector<RidgelineCurvePoint> curve = it->CurveOf(s);
        curve.push_back({x, y});
        it->SetCurve(s, std::move(curve));
    }
    if (rows.empty()) return failWith("the file has a header but no data");
    model.rows = std::move(rows);
    model.series = series.size() > 1 ? std::move(series) : std::vector<RidgelineSeries>();
    return true;
}

} // namespace UltraCanvas
