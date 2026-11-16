// include/Plugins/Charts/Engine/UltraCanvasChartDistribution.h
// The chart engine's distribution layer - the file the engine proposal reserves
// (§5.2, §8.1): the marginal distribution of an axis's values drawn along that
// axis (slot 550), sharing its scale exactly - the joint plot's histograms on
// the top and right of a scatter field, or a histogram on every axis of a
// parallel coordinate chart. Five kinds: histogram, kernel density, box
// summary, strip (rug) and violin.
//
// The statistics are UI-free and unit-tested (Tests/ChartEngineTest); the
// engine element maps them through the axis and draws the band
// (UltraCanvasChartEngineElement::AddAxisDistribution).
//
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <cstddef>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// DISTRIBUTION SPEC
// =============================================================================

enum class AxisDistributionKind {
    Histogram,   // equal-width bins over the axis, bar length by count
    Density,     // Gaussian kernel density, a filled curve
    Box,         // quartile box, median line, Tukey whiskers (1.5 IQR)
    Strip,       // one short tick per value (a rug)
    Violin       // the density mirrored about the band's centre line
};

// Where the band sits. For an edge axis Outside reserves a band beyond the
// plot's opposite edge (the joint plot: the x distribution above the plot, the
// y distribution to its right); Near and Far draw inside the plot, under the
// content, growing in from the axis's own edge (Near) or the opposite edge
// (Far). For an in-plot axis (parallel coordinates) Near is the side its
// ticks point to and Far the other; Outside draws as Far there, since an
// in-plot axis has no outside. Both is Near and Far together - mirrored about
// an in-plot rule.
enum class AxisDistributionSide { Near, Far, Both, Outside };

struct AxisDistribution {
    size_t axisIndex = 0;
    AxisDistributionKind kind = AxisDistributionKind::Histogram;
    AxisDistributionSide side = AxisDistributionSide::Outside;

    // The values to summarise; empty = everything the axis observed while it
    // was described (ChartAxis::Samples). Give them explicitly when the axis
    // observed something else - a bar chart's value axis sees the stack totals.
    std::vector<double> values;
    // A subset drawn over the total at the same scale (a brushed selection),
    // so it reads as a share of the whole. Empty = none.
    std::vector<double> filteredValues;

    int binCount = 0;              // Histogram: 0 = Freedman-Diaconis
    double bandwidth = 0.0;        // Density/Violin, normalised axis units: 0 = Silverman
    bool logCount = false;         // Histogram: bar length by log(1 + count)

    double thickness = 48.0;       // px: the band's depth - the tallest bar's length
    double gap = 4.0;              // px between the plot edge and an Outside band

    Color fill = Color(100, 140, 220, 110);
    Color stroke = Color(70, 100, 170, 200);
    float strokeWidth = 1.0f;
    Color filteredFill = Color(230, 120, 40, 170);
};

// =============================================================================
// STATISTICS (UI-free, unit-tested)
// =============================================================================

// Equal-width bins over [low, high]. The last bin includes `high`; values
// outside the interval and non-finite values are not counted.
struct ChartHistogram {
    double low = 0.0;
    double high = 1.0;
    std::vector<size_t> counts;
    size_t maxCount = 0;
    size_t total = 0;              // values counted

    double BinWidth() const {
        return counts.empty() ? 0.0 : (high - low) / static_cast<double>(counts.size());
    }
};

// The Freedman-Diaconis bin count for the values that fall inside
// [low, high]: bin width 2 * IQR / n^(1/3), clamped to [1, maxBins]. Falls
// back to Sturges' rule (1 + log2 n) when the IQR is zero.
int FreedmanDiaconisBinCount(const std::vector<double>& values, double low, double high,
                             int maxBins = 200);

// binCount <= 0 picks FreedmanDiaconisBinCount.
ChartHistogram ComputeHistogram(const std::vector<double>& values, double low, double high,
                                int binCount = 0);

// Silverman's rule of thumb: 0.9 * min(sigma, IQR / 1.34) * n^(-1/5). Zero for
// fewer than two finite values; a constant sample falls back to a small
// positive width so the density is a narrow peak rather than nothing.
double SilvermanBandwidth(const std::vector<double>& values);

// Gaussian kernel density at `sampleCount` evenly spaced points from low to
// high inclusive. bandwidth <= 0 selects SilvermanBandwidth. The density
// integrates to one over the real line, so a subset's curve computed the same
// way and scaled by its share of the total sits under the total's curve.
std::vector<double> ComputeKernelDensity(const std::vector<double>& values, double low,
                                         double high, size_t sampleCount,
                                         double bandwidth = 0.0);

// Five-number summary with Tukey whiskers: the quartiles interpolate linearly
// between order statistics (the common "type 7" definition), the whiskers end
// at the most extreme values within 1.5 IQR of the box, and everything beyond
// them is an outlier. Non-finite values are skipped; valid needs one value.
struct ChartBoxSummary {
    double minimum = 0.0, q1 = 0.0, median = 0.0, q3 = 0.0, maximum = 0.0;
    double whiskerLow = 0.0, whiskerHigh = 0.0;
    double mean = 0.0;
    size_t count = 0;
    std::vector<double> outliers;
    bool valid = false;
};

ChartBoxSummary ComputeBoxSummary(const std::vector<double>& values);

} // namespace UltraCanvas
