// include/Plugins/Charts/Engine/UltraCanvasControlLimits.h
// Statistical process control, UI-free: the centre line, control limits and
// run-rule signals of a Shewhart control chart, computed from the data.
//
// A control chart plots a process measurement in time order against a centre
// line and limits three standard deviations either side. Sigma is estimated
// WITHIN subgroups (R-bar / d2, MR-bar / d2(2)), never from the standard
// deviation of all values, which a shift in the process inflates - widening
// the limits and hiding the very shift the chart is meant to show.
//
// Kinds: X-bar-R (subgroups of 2..25 measurements: means over ranges) and
// I-MR (one value per time step: individuals over moving ranges). Rules: the
// Western Electric set, evaluated on each point's z-score, flagging the point
// that completes a pattern. The location pane gets all enabled rules, the
// dispersion pane rule 1 only (the pattern rules assume roughly normal,
// independent values, which ranges are not).
//
// ShowControlChart (UltraCanvasEngineLineChart.h) draws a result on a line
// chart. Docs: Docs/UltraCanvas/UltraCanvasControlLimits.md.
//
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class ControlChartKind {
    XbarR,          // subgroup means over subgroup ranges
    IndividualsMR   // individual values over moving ranges of two
};

// One time step: a subgroup of measurements, or a single value for I-MR.
struct ControlSubgroup {
    std::string label;                // "08:00", "Lot 1142"; empty = its number
    std::vector<double> values;       // non-finite entries are ignored
};

// The statistic one pane plots, with its limits per point. For X-bar-R and
// I-MR every point has the same limits (constantLimits); the vectors keep the
// per-point shape that varying limits (p, u charts) will need.
struct ControlSeries {
    std::string name;                 // "Subgroup mean", "Range", "Value", "Moving range"
    std::vector<double> value;        // NaN = nothing plotted (MR's first point)
    std::vector<double> centre;
    std::vector<double> upper;
    std::vector<double> lower;        // clamped at 0 where the statistic cannot go below
    std::vector<double> sigma;        // the point's own sigma, for zones and rules
    bool constantLimits = true;
    // The computed lower limit fell below zero and was raised to 0: the
    // statistic has no lower limit (R for n <= 6, MR always).
    bool lowerClamped = false;
};

// A rule signal. `point` completes the pattern; `firstPoint` is where the
// pattern starts (equal for rule 1).
struct ControlViolation {
    size_t point = 0;
    size_t firstPoint = 0;
    int rule = 0;                     // 1-based Western Electric rule number
    size_t pane = 0;                  // 0 = location, 1 = dispersion
};

struct ControlOptions {
    ControlChartKind kind = ControlChartKind::XbarR;
    // Western Electric rules 1..4 on the location pane; all on by default.
    std::array<bool, 4> rulesEnabled = {true, true, true, true};
    // Rule 4's run length: 8 points on one side of the centre line (some
    // sources use 9).
    int sameSideRun = 8;
};

struct ControlResult {
    ControlChartKind kind = ControlChartKind::XbarR;
    std::vector<std::string> labels;
    ControlSeries location;
    std::optional<ControlSeries> dispersion;
    std::vector<ControlViolation> violations;
    // The within-subgroup estimate of the process sigma (of an individual
    // value, not of a mean).
    double sigmaWithin = 0.0;
    int subgroupSize = 0;             // X-bar-R: n; I-MR: 1
    // Unequal subgroup sizes, too few subgroups for trusted limits, and the
    // like - in English, for a status line or a log.
    std::vector<std::string> warnings;

    // The rules point `index` of `pane` completes (empty = none).
    std::vector<int> RulesAt(size_t index, size_t pane = 0) const;
    bool HasSignal() const { return !violations.empty(); }
};

ControlResult ComputeControlChart(const std::vector<ControlSubgroup>& subgroups,
                                  const ControlOptions& options = {});

// The rule signals of one pane's series against its own centre, limits and
// sigma - what ComputeControlChart runs on each pane, usable on its own with
// limits from elsewhere (a frozen baseline). Pane 0 checks the enabled
// Western Electric rules; any other pane checks rule 1 only, against its
// limits (a lower limit clamped at zero is none). A point with no value or a
// sigma of zero is never flagged.
std::vector<ControlViolation> FindRuleViolations(const ControlSeries& series,
                                                 const ControlOptions& options = {},
                                                 size_t pane = 0);

// One line describing a Western Electric rule (1..4), in English, with the
// run length rule 4 used ("Eight points in a row on one side of the centre
// line").
std::string ControlRuleDescription(int rule, int sameSideRun = 8);

// The bias-correction constants of a subgroup of n values, n = 2..25 (NaN
// outside): d2 and d3 (lower-case in the SPC tables) are tabulated, c4 is
// computed from its closed form.
double ControlD2(int n);
double ControlD3(int n);
double ControlC4(int n);
// The chart factors derived from them: A2 = 3 / (d2 sqrt n), and the range
// chart's D3 = max(0, 1 - 3 d3 / d2) and D4 = 1 + 3 d3 / d2.
double ControlA2(int n);
double ControlRangeLowerFactor(int n);   // D3
double ControlRangeUpperFactor(int n);   // D4

} // namespace UltraCanvas
