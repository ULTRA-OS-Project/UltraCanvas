// Plugins/Charts/Engine/UltraCanvasChartDistribution.cpp
// The distribution layer's statistics: histogram binning, kernel density and
// the box summary an axis distribution draws.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartDistribution.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace UltraCanvas {

namespace {

std::vector<double> FiniteSorted(const std::vector<double>& values) {
    std::vector<double> out;
    out.reserve(values.size());
    for (double v : values) {
        if (std::isfinite(v)) out.push_back(v);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Linear interpolation between order statistics ("type 7").
double Quantile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const double pos = p * static_cast<double>(sorted.size() - 1);
    const size_t below = static_cast<size_t>(std::floor(pos));
    const size_t above = std::min(below + 1, sorted.size() - 1);
    const double frac = pos - static_cast<double>(below);
    return sorted[below] + (sorted[above] - sorted[below]) * frac;
}

} // namespace

int FreedmanDiaconisBinCount(const std::vector<double>& values, double low, double high,
                             int maxBins) {
    maxBins = std::max(1, maxBins);
    if (!(high > low)) return 1;
    std::vector<double> inside;
    inside.reserve(values.size());
    for (double v : values) {
        if (std::isfinite(v) && v >= low && v <= high) inside.push_back(v);
    }
    if (inside.size() < 2) return 1;
    std::sort(inside.begin(), inside.end());

    const double n = static_cast<double>(inside.size());
    const double iqr = Quantile(inside, 0.75) - Quantile(inside, 0.25);
    int bins = 0;
    if (iqr > 0.0) {
        const double width = 2.0 * iqr / std::cbrt(n);
        bins = static_cast<int>(std::ceil((high - low) / width));
    } else {
        bins = static_cast<int>(std::ceil(std::log2(n) + 1.0));   // Sturges
    }
    return std::clamp(bins, 1, maxBins);
}

ChartHistogram ComputeHistogram(const std::vector<double>& values, double low, double high,
                                int binCount) {
    ChartHistogram h;
    if (high < low) std::swap(low, high);
    h.low = low;
    h.high = high;
    if (binCount <= 0) binCount = FreedmanDiaconisBinCount(values, low, high);
    h.counts.assign(static_cast<size_t>(std::max(1, binCount)), 0);
    const double span = high - low;
    for (double v : values) {
        if (!std::isfinite(v) || v < low || v > high) continue;
        size_t bin = 0;
        if (span > 0.0) {
            bin = static_cast<size_t>((v - low) / span * static_cast<double>(h.counts.size()));
            bin = std::min(bin, h.counts.size() - 1);      // `high` itself
        }
        ++h.counts[bin];
        ++h.total;
    }
    h.maxCount = *std::max_element(h.counts.begin(), h.counts.end());
    return h;
}

double SilvermanBandwidth(const std::vector<double>& values) {
    const std::vector<double> sorted = FiniteSorted(values);
    if (sorted.size() < 2) return 0.0;
    const double n = static_cast<double>(sorted.size());
    const double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / n;
    double variance = 0.0;
    for (double v : sorted) variance += (v - mean) * (v - mean);
    const double sigma = std::sqrt(variance / (n - 1.0));
    const double iqr = Quantile(sorted, 0.75) - Quantile(sorted, 0.25);
    double spread = sigma;
    if (iqr > 0.0) spread = std::min(sigma, iqr / 1.34);
    if (!(spread > 0.0)) {
        // A constant sample: a narrow peak at the value, relative to its size.
        spread = std::max(1e-3, std::abs(sorted.front()) * 1e-3);
    }
    return 0.9 * spread * std::pow(n, -0.2);
}

std::vector<double> ComputeKernelDensity(const std::vector<double>& values, double low,
                                         double high, size_t sampleCount, double bandwidth) {
    std::vector<double> density(sampleCount, 0.0);
    if (sampleCount == 0) return density;
    const std::vector<double> sorted = FiniteSorted(values);
    if (sorted.empty()) return density;
    if (!(bandwidth > 0.0)) bandwidth = SilvermanBandwidth(sorted);
    if (!(bandwidth > 0.0)) bandwidth = std::max(1e-3, (high - low) * 0.02);   // one value

    const double norm = 1.0 / (static_cast<double>(sorted.size()) * bandwidth *
                               std::sqrt(2.0 * M_PI));
    // The kernel is negligible past four bandwidths; the sorted sample lets
    // each evaluation visit only the values that contribute.
    const double reach = 4.0 * bandwidth;
    for (size_t i = 0; i < sampleCount; ++i) {
        const double x = (sampleCount == 1)
            ? (low + high) * 0.5
            : low + (high - low) * static_cast<double>(i) / static_cast<double>(sampleCount - 1);
        auto first = std::lower_bound(sorted.begin(), sorted.end(), x - reach);
        auto last = std::upper_bound(first, sorted.end(), x + reach);
        double sum = 0.0;
        for (auto it = first; it != last; ++it) {
            const double z = (x - *it) / bandwidth;
            sum += std::exp(-0.5 * z * z);
        }
        density[i] = sum * norm;
    }
    return density;
}

ChartBoxSummary ComputeBoxSummary(const std::vector<double>& values) {
    ChartBoxSummary s;
    const std::vector<double> sorted = FiniteSorted(values);
    if (sorted.empty()) return s;
    s.count = sorted.size();
    s.minimum = sorted.front();
    s.maximum = sorted.back();
    s.q1 = Quantile(sorted, 0.25);
    s.median = Quantile(sorted, 0.5);
    s.q3 = Quantile(sorted, 0.75);
    s.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) /
             static_cast<double>(sorted.size());

    const double iqr = s.q3 - s.q1;
    const double fenceLow = s.q1 - 1.5 * iqr;
    const double fenceHigh = s.q3 + 1.5 * iqr;
    s.whiskerLow = s.q1;
    s.whiskerHigh = s.q3;
    for (double v : sorted) {
        if (v >= fenceLow) { s.whiskerLow = std::min(v, s.q1); break; }
    }
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        if (*it <= fenceHigh) { s.whiskerHigh = std::max(*it, s.q3); break; }
    }
    for (double v : sorted) {
        if (v < fenceLow || v > fenceHigh) s.outliers.push_back(v);
    }
    s.valid = true;
    return s;
}

} // namespace UltraCanvas
