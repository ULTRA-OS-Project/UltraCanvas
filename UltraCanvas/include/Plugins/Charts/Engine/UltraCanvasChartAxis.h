// include/Plugins/Charts/Engine/UltraCanvasChartAxis.h
// Unified axis model for the chart engine: range accumulation, scale types,
// normalisation, tick generation and value formatting.
//
// Replaces the ten private axis implementations found across the chart plugins.
// Free of UI dependencies - it maps values onto the normalised interval [0,1]
// and leaves screen geometry to the projection, so it can be unit tested
// without a window.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/Engine/UltraCanvasChartTypes.h"
#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// SCALES
// =============================================================================

enum class ChartScale {
    Linear,
    Log,            // logarithmic; non-positive values are clamped to the floor
    SymLog,         // linear near zero, logarithmic beyond - handles signed data
    Percentile,     // rank among the observed values: uniformises skewed columns
    ZScore,         // (v - mean) / stddev
    RobustZScore,   // (v - median) / MAD - resistant to outliers
    Category        // discrete slots; the value is the category index
};

// Where the axis furniture is drawn. Independent of the projection: a
// horizontal bar chart still has a value axis, it just runs the other way.
enum class ChartAxisSide { Left, Right, Bottom, Top };

struct ChartTick {
    double value = 0.0;         // the data value the tick marks
    double normalized = 0.0;    // its position on [0,1] after scale and inversion
    std::string label;
    bool major = true;
    int priority = 0;           // for PriorityGreedy declutter: ends and zero score higher
};

// =============================================================================
// AXIS
// =============================================================================

class ChartAxis {
public:
    ChartAxis() = default;
    explicit ChartAxis(std::string axisName) : name(std::move(axisName)) {}

    // ---- identity and configuration -------------------------------------
    std::string name;
    ChartScale scale = ChartScale::Linear;
    ChartAxisSide side = ChartAxisSide::Left;
    bool visible = true;
    bool inverted = false;          // flip which end is "high"
    // In-plot axes (parallel coordinates, small multiples): drawn as a rule
    // inside the plot at normalized domain position `plotPosition` instead of
    // along an edge. The engine's axes layer renders both kinds.
    bool inPlot = false;
    double plotPosition = 0.0;      // 0..1 along the domain direction
    // In-plot axes label their values one of two ways, never both: an
    // integrated axis (tick labels along the rule, showTickLabels) or bare
    // min/max endpoint labels at the ends (showEndpointLabels). When the
    // integrated axis is active the endpoint labels are suppressed - they
    // would only repeat the outermost ticks.
    bool showTickLabels = true;
    bool showEndpointLabels = false; // min/max printed at the axis ends
    std::string title;              // drawn by the axes layer; may differ from name
    double logBase = 10.0;
    double symLogThreshold = 1.0;   // |v| below this is linear
    int decimals = -1;              // -1 = choose from the range
    std::string unitPrefix;         // e.g. "$"
    std::string unitSuffix;         // e.g. "%"
    bool compactNumbers = false;    // 12'500 -> "12.5K"
    ValueFormatter formatter;       // wins over every built-in formatting rule
    std::vector<std::string> categories;   // Category scale slot labels
    // Explicit tick positions: when non-empty, GenerateTicks emits exactly
    // these (formatted through the axis), overriding every generation rule -
    // category slots under bars, hand-picked decades, threshold lists.
    std::vector<double> tickValues;

    // ---- range ----------------------------------------------------------
    // Explicit range; turns auto-ranging off.
    void SetRange(double lo, double hi);
    void SetAutoRange(bool on) { autoRange = on; finalized = false; }
    bool IsAutoRange() const { return autoRange; }

    // Feed the data. Non-finite values are ignored, so a NaN hole in a series
    // cannot poison the range.
    void Observe(double value);
    void Observe(const std::vector<double>& values);
    void ResetObservations();

    // Resolves the effective range from the observations: applies nice-number
    // rounding for auto ranges, pads a degenerate range (every value identical,
    // which would otherwise divide by zero or collapse the axis), and computes
    // the statistics the Percentile / ZScore scales need. Idempotent.
    void Finalize();

    double Min() const { return effectiveMin; }
    double Max() const { return effectiveMax; }
    bool IsDegenerate() const { return degenerate; }
    size_t SampleCount() const { return samples.size(); }

    // ---- mapping ---------------------------------------------------------
    // Value -> [0,1], honouring scale and inversion. Out-of-range values map
    // outside [0,1] rather than being clamped, so callers can decide.
    double Normalize(double value) const;
    // The inverse. Exact for Linear/Log/SymLog/Category; Percentile and the
    // z-scores invert through their statistics.
    double Denormalize(double t) const;

    // ---- ticks -----------------------------------------------------------
    // Generates roughly `targetCount` ticks: nice round values for Linear,
    // decade steps for Log, one per category for Category. Ends and the zero
    // line get a higher declutter priority.
    std::vector<ChartTick> GenerateTicks(int targetCount = 6) const;

    // ---- formatting ------------------------------------------------------
    std::string FormatValue(double value) const;

private:
    bool autoRange = true;
    bool finalized = false;
    bool degenerate = false;
    double requestedMin = 0.0, requestedMax = 1.0;
    double effectiveMin = 0.0, effectiveMax = 1.0;

    // Kept for the scales that need the distribution, not just the extent.
    std::vector<double> samples;
    std::vector<double> sortedSamples;      // built by Finalize for Percentile
    double mean = 0.0, stddev = 1.0, median = 0.0, mad = 1.0;

    double ApplyScale(double value) const;      // value -> scale space
    double UnapplyScale(double s) const;        // scale space -> value
    bool NeedsSamples() const;
};

// =============================================================================
// AXIS SET
// =============================================================================

// A chart's axes. Parallel coordinates has one per dimension, a financial chart
// has price and volume, a scatter plot has two: the engine does not care.
class ChartAxisSet {
public:
    size_t Add(const ChartAxis& axis);
    size_t Count() const { return axes.size(); }
    ChartAxis& At(size_t index) { return axes[index]; }
    const ChartAxis& At(size_t index) const { return axes[index]; }
    ChartAxis* Find(const std::string& name);
    const ChartAxis* Find(const std::string& name) const;
    void Clear() { axes.clear(); }
    void FinalizeAll();

    std::vector<ChartAxis>::iterator begin() { return axes.begin(); }
    std::vector<ChartAxis>::iterator end() { return axes.end(); }
    std::vector<ChartAxis>::const_iterator begin() const { return axes.begin(); }
    std::vector<ChartAxis>::const_iterator end() const { return axes.end(); }

private:
    std::vector<ChartAxis> axes;
};

// =============================================================================
// HELPERS
// =============================================================================

// Rounds a span to a "nice" step: 1, 2, 2.5 or 5 times a power of ten. Used for
// auto tick selection and auto range rounding, so labels read 0.20 rather than
// 0.1978.
double NiceNumber(double range, bool round);

// Formats a number compactly: 12'500 -> "12.5K", 3'400'000 -> "3.4M".
std::string FormatCompactNumber(double value, int decimals);

} // namespace UltraCanvas
