// Plugins/Charts/Engine/UltraCanvasChartAxis.cpp
// Unified axis model implementation
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartAxis.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace UltraCanvas {

namespace {

bool IsFinite(double v) { return std::isfinite(v); }

// Smallest positive value a log axis will accept, so a zero or negative sample
// cannot produce -inf.
constexpr double kLogFloor = 1e-12;

double SafeLog(double v, double base) {
    return std::log(std::max(v, kLogFloor)) / std::log(base);
}

double Percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted[0];
    const double pos = fraction * static_cast<double>(sorted.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = std::min(lo + 1, sorted.size() - 1);
    const double frac = pos - static_cast<double>(lo);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
}

} // namespace

// =============================================================================
// HELPERS
// =============================================================================

double NiceNumber(double range, bool round) {
    if (range <= 0.0 || !IsFinite(range)) return 1.0;
    const double exponent = std::floor(std::log10(range));
    const double fraction = range / std::pow(10.0, exponent);
    double nice;
    if (round) {
        if      (fraction < 1.5) nice = 1.0;
        else if (fraction < 3.0) nice = 2.0;
        else if (fraction < 7.0) nice = 5.0;
        else                     nice = 10.0;
    } else {
        if      (fraction <= 1.0) nice = 1.0;
        else if (fraction <= 2.0) nice = 2.0;
        else if (fraction <= 2.5) nice = 2.5;
        else if (fraction <= 5.0) nice = 5.0;
        else                      nice = 10.0;
    }
    return nice * std::pow(10.0, exponent);
}

std::string FormatCompactNumber(double value, int decimals) {
    static const struct { double scale; const char* suffix; } units[] = {
        {1e12, "T"}, {1e9, "B"}, {1e6, "M"}, {1e3, "K"},
    };
    const double magnitude = std::abs(value);
    char buffer[64];
    for (const auto& unit : units) {
        if (magnitude >= unit.scale) {
            // A scaled number needs a decimal to stay informative: "2.5M" says
            // something "2M" does not. Auto (-1) therefore means one place
            // once a suffix is in play.
            const int places = (decimals < 0) ? 1 : decimals;
            std::snprintf(buffer, sizeof(buffer), "%.*f%s",
                          places, value / unit.scale, unit.suffix);
            return buffer;
        }
    }
    std::snprintf(buffer, sizeof(buffer), "%.*f", std::max(0, decimals), value);
    return buffer;
}

// =============================================================================
// RANGE
// =============================================================================

void ChartAxis::SetRange(double lo, double hi) {
    requestedMin = std::min(lo, hi);
    requestedMax = std::max(lo, hi);
    autoRange = false;
    finalized = false;
}

void ChartAxis::Observe(double value) {
    if (!IsFinite(value)) return;      // NaN holes must not poison the range
    samples.push_back(value);
    finalized = false;
}

void ChartAxis::Observe(const std::vector<double>& values) {
    samples.reserve(samples.size() + values.size());
    for (double v : values) Observe(v);
}

void ChartAxis::ResetObservations() {
    samples.clear();
    sortedSamples.clear();
    finalized = false;
}

bool ChartAxis::NeedsSamples() const {
    return scale == ChartScale::Percentile ||
           scale == ChartScale::ZScore ||
           scale == ChartScale::RobustZScore;
}

void ChartAxis::Finalize() {
    if (finalized) return;
    finalized = true;
    degenerate = false;

    if (scale == ChartScale::Category) {
        effectiveMin = 0.0;
        effectiveMax = categories.empty() ? 1.0
                                          : static_cast<double>(categories.size() - 1);
        if (categories.size() == 1) { effectiveMax = 1.0; degenerate = true; }
        return;
    }

    // Statistics first: the z-score scales define their own range in terms of
    // them, so they have to exist before the range is settled.
    if (NeedsSamples() && !samples.empty()) {
        sortedSamples = samples;
        std::sort(sortedSamples.begin(), sortedSamples.end());

        const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        mean = sum / static_cast<double>(samples.size());
        double variance = 0.0;
        for (double v : samples) variance += (v - mean) * (v - mean);
        variance /= static_cast<double>(samples.size());
        stddev = std::sqrt(variance);
        if (stddev < 1e-12) stddev = 1.0;         // constant column: keep the maths sane

        median = Percentile(sortedSamples, 0.5);
        std::vector<double> deviations;
        deviations.reserve(samples.size());
        for (double v : samples) deviations.push_back(std::abs(v - median));
        std::sort(deviations.begin(), deviations.end());
        mad = Percentile(deviations, 0.5) * 1.4826;   // scaled to match stddev
        if (mad < 1e-12) mad = 1.0;
    }

    double lo, hi;
    if (autoRange) {
        if (samples.empty()) {
            lo = 0.0;
            hi = 1.0;
        } else {
            lo = *std::min_element(samples.begin(), samples.end());
            hi = *std::max_element(samples.begin(), samples.end());
        }
    } else {
        lo = requestedMin;
        hi = requestedMax;
    }

    // A column whose values are all identical would otherwise divide by zero
    // and collapse the axis to a point. Pad it into a readable range instead of
    // dropping the axis, which is the behaviour HiPlot users complain about.
    if (!(hi > lo)) {
        degenerate = true;
        const double magnitude = std::max(std::abs(lo), 1.0);
        lo -= magnitude * 0.5;
        hi += magnitude * 0.5;
    } else if (autoRange && scale == ChartScale::Linear) {
        // Round an auto range outward to nice numbers so tick labels read well.
        const double step = NiceNumber(NiceNumber(hi - lo, false) / 5.0, true);
        lo = std::floor(lo / step) * step;
        hi = std::ceil(hi / step) * step;
    }

    effectiveMin = lo;
    effectiveMax = hi;
}

// =============================================================================
// MAPPING
// =============================================================================

double ChartAxis::ApplyScale(double value) const {
    switch (scale) {
        case ChartScale::Log:
            return SafeLog(value, logBase);
        case ChartScale::SymLog: {
            const double sign = (value < 0.0) ? -1.0 : 1.0;
            const double magnitude = std::abs(value);
            if (magnitude <= symLogThreshold) return value / std::max(symLogThreshold, kLogFloor);
            return sign * (1.0 + SafeLog(magnitude / symLogThreshold, logBase));
        }
        case ChartScale::ZScore:
            return (value - mean) / stddev;
        case ChartScale::RobustZScore:
            return (value - median) / mad;
        case ChartScale::Percentile: {
            if (sortedSamples.empty()) return 0.0;
            const auto it = std::lower_bound(sortedSamples.begin(), sortedSamples.end(), value);
            const double rank = static_cast<double>(std::distance(sortedSamples.begin(), it));
            return rank / static_cast<double>(std::max<size_t>(1, sortedSamples.size() - 1));
        }
        case ChartScale::Linear:
        case ChartScale::Category:
        default:
            return value;
    }
}

double ChartAxis::UnapplyScale(double s) const {
    switch (scale) {
        case ChartScale::Log:
            return std::pow(logBase, s);
        case ChartScale::SymLog: {
            const double sign = (s < 0.0) ? -1.0 : 1.0;
            const double magnitude = std::abs(s);
            if (magnitude <= 1.0) return s * symLogThreshold;
            return sign * symLogThreshold * std::pow(logBase, magnitude - 1.0);
        }
        case ChartScale::ZScore:
            return s * stddev + mean;
        case ChartScale::RobustZScore:
            return s * mad + median;
        case ChartScale::Percentile:
            return Percentile(sortedSamples, std::clamp(s, 0.0, 1.0));
        case ChartScale::Linear:
        case ChartScale::Category:
        default:
            return s;
    }
}

double ChartAxis::Normalize(double value) const {
    double t;
    if (scale == ChartScale::Percentile) {
        // Already a 0..1 rank; the range does not apply.
        t = ApplyScale(value);
    } else {
        const double lo = ApplyScale(effectiveMin);
        const double hi = ApplyScale(effectiveMax);
        const double span = hi - lo;
        t = (std::abs(span) < 1e-15) ? 0.5 : (ApplyScale(value) - lo) / span;
    }
    return inverted ? 1.0 - t : t;
}

double ChartAxis::Denormalize(double t) const {
    const double u = inverted ? 1.0 - t : t;
    if (scale == ChartScale::Percentile) return UnapplyScale(u);
    const double lo = ApplyScale(effectiveMin);
    const double hi = ApplyScale(effectiveMax);
    return UnapplyScale(lo + u * (hi - lo));
}

// =============================================================================
// TICKS
// =============================================================================

std::vector<ChartTick> ChartAxis::GenerateTicks(int targetCount) const {
    std::vector<ChartTick> ticks;
    targetCount = std::max(2, targetCount);

    if (scale == ChartScale::Category) {
        for (size_t i = 0; i < categories.size(); ++i) {
            ChartTick t;
            t.value = static_cast<double>(i);
            t.normalized = Normalize(t.value);
            t.label = categories[i];
            t.priority = (i == 0 || i + 1 == categories.size()) ? 10 : 0;
            ticks.push_back(t);
        }
        return ticks;
    }

    if (scale == ChartScale::Log) {
        // One tick per decade, thinned to roughly the requested count.
        const double lo = SafeLog(effectiveMin, logBase);
        const double hi = SafeLog(effectiveMax, logBase);
        const int first = static_cast<int>(std::floor(lo));
        const int last = static_cast<int>(std::ceil(hi));
        const int decades = std::max(1, last - first);
        const int step = std::max(1, decades / std::max(1, targetCount - 1));
        for (int e = first; e <= last; e += step) {
            const double value = std::pow(logBase, e);
            if (value < effectiveMin * 0.999 || value > effectiveMax * 1.001) continue;
            ChartTick t;
            t.value = value;
            t.normalized = Normalize(value);
            t.label = FormatValue(value);
            t.priority = (e == first || e == last) ? 10 : 0;
            ticks.push_back(t);
        }
        if (!ticks.empty()) return ticks;
        // Fall through to linear ticks when the range spans less than a decade.
    }

    // Linear (and the z-score / symlog / percentile fallbacks): nice round steps
    // across the effective range.
    const double lo = (scale == ChartScale::Percentile) ? 0.0 : effectiveMin;
    const double hi = (scale == ChartScale::Percentile) ? 1.0 : effectiveMax;
    const double span = NiceNumber(hi - lo, false);
    const double step = NiceNumber(span / (targetCount - 1), true);
    if (step <= 0.0 || !IsFinite(step)) return ticks;

    const double start = std::ceil(lo / step) * step;
    for (double v = start; v <= hi + step * 0.5; v += step) {
        // Snap values that land a hair off zero because of accumulated error.
        const double value = (std::abs(v) < step * 1e-9) ? 0.0 : v;
        if (value < lo - step * 0.001 || value > hi + step * 0.001) continue;
        ChartTick t;
        t.value = value;
        t.normalized = Normalize(value);
        t.label = FormatValue(value);
        // Ends and the zero line survive decluttering longest.
        t.priority = (value == 0.0) ? 5 : 0;
        if (std::abs(value - lo) < step * 0.5 || std::abs(value - hi) < step * 0.5) {
            t.priority = 10;
        }
        ticks.push_back(t);
    }
    return ticks;
}

// =============================================================================
// FORMATTING
// =============================================================================

std::string ChartAxis::FormatValue(double value) const {
    if (formatter) return formatter(value);

    if (scale == ChartScale::Category) {
        const long index = std::lround(value);
        if (index >= 0 && static_cast<size_t>(index) < categories.size()) {
            return categories[static_cast<size_t>(index)];
        }
    }

    int places = decimals;
    if (places < 0) {
        // Enough decimals to tell neighbouring ticks apart, and no more.
        const double span = std::abs(effectiveMax - effectiveMin);
        if (span >= 100.0)      places = 0;
        else if (span >= 10.0)  places = 1;
        else if (span >= 1.0)   places = 2;
        else if (span >= 0.1)   places = 3;
        else                    places = 4;
    }

    std::string text;
    if (compactNumbers) {
        // Pass the caller's explicit choice through; auto lets the compact
        // formatter decide, since its needs differ from a plain decimal.
        text = FormatCompactNumber(value, decimals);
    } else {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.*f", places, value);
        text = buffer;
        // "-0.00" reads as a mistake.
        if (text.find_first_not_of("-0.") == std::string::npos && !text.empty()) {
            if (text[0] == '-') text.erase(text.begin());
        }
    }
    return unitPrefix + text + unitSuffix;
}

// =============================================================================
// AXIS SET
// =============================================================================

size_t ChartAxisSet::Add(const ChartAxis& axis) {
    axes.push_back(axis);
    return axes.size() - 1;
}

ChartAxis* ChartAxisSet::Find(const std::string& axisName) {
    for (ChartAxis& a : axes) if (a.name == axisName) return &a;
    return nullptr;
}

const ChartAxis* ChartAxisSet::Find(const std::string& axisName) const {
    for (const ChartAxis& a : axes) if (a.name == axisName) return &a;
    return nullptr;
}

void ChartAxisSet::FinalizeAll() {
    for (ChartAxis& a : axes) a.Finalize();
}

// =============================================================================
// LAYOUT
// =============================================================================

Rect2Dd SolvePlotArea(const Rect2Dd& chartArea, const ChartMargins& margins,
                      double minWidth, double minHeight) {
    double left = std::max(0.0, margins.left);
    double right = std::max(0.0, margins.right);
    double top = std::max(0.0, margins.top);
    double bottom = std::max(0.0, margins.bottom);

    // Greedy reservations must never produce an inverted or empty plot area;
    // scale them back proportionally instead.
    const double horizontal = left + right;
    if (horizontal > 0.0 && chartArea.width - horizontal < minWidth) {
        const double available = std::max(0.0, chartArea.width - minWidth);
        const double factor = available / horizontal;
        left *= factor;
        right *= factor;
    }
    const double vertical = top + bottom;
    if (vertical > 0.0 && chartArea.height - vertical < minHeight) {
        const double available = std::max(0.0, chartArea.height - minHeight);
        const double factor = available / vertical;
        top *= factor;
        bottom *= factor;
    }

    return Rect2Dd(chartArea.x + left,
                   chartArea.y + top,
                   std::max(minWidth, chartArea.width - left - right),
                   std::max(minHeight, chartArea.height - top - bottom));
}

} // namespace UltraCanvas
