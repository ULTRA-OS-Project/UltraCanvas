// include/Plugins/Charts/Engine/UltraCanvasControlLimits.h
// Statistical process control, UI-free: the centre line, control limits and
// run-rule signals of a Shewhart control chart, computed from the data.
//
// A control chart plots a process measurement in time order against a centre
// line and limits three standard deviations either side. Sigma is estimated
// WITHIN subgroups (R-bar / d2, S-bar / c4, MR-bar / d2(2)), never from the
// standard deviation of all values, which a shift in the process inflates -
// widening the limits and hiding the very shift the chart is meant to show.
//
// Kinds: X-bar-R, X-bar-S and I-MR for measurements (a location pane over a
// dispersion pane); p, np, c and u for counts (one pane). Rules: the Western
// Electric or the Nelson set, evaluated on each point's z-score, flagging the
// point that completes a pattern; the dispersion pane gets rule 1 only unless
// asked (the pattern rules assume roughly normal, independent values, which
// ranges are not). Stages split the points at a process change - each stage
// has its own limits and no rule looks across a stage boundary. Excluded
// points are plotted and checked but left out of the limits; a baseline
// takes the limits from a stage's first points only. Specification limits
// give the capability indices Cp, Cpk, Pp and Ppk.
//
// ShowControlChart (UltraCanvasEngineLineChart.h) draws a result on a line
// chart. Docs: Docs/UltraCanvas/UltraCanvasControlLimits.md.
//
// Version: 1.1.0 - X-bar-S (unequal subgroups too), p, np, c and u; the Nelson
//   rules, rules on the dispersion pane; stages, excluded points, baselines;
//   specification limits and capability; notes
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class ControlChartKind {
    XbarR,          // subgroup means over subgroup ranges (n = 2..25)
    IndividualsMR,  // individual values over moving ranges of two
    XbarS,          // subgroup means over subgroup standard deviations (any n >= 2, may vary)
    P,              // proportion defective per lot: count / size (sizes may vary)
    NP,             // number defective per lot of constant size
    C,              // defects per inspection unit (constant opportunity)
    U               // defects per unit: count / size (sizes may vary)
};

enum class ControlRuleSet {
    WesternElectric,    // rules 1-4
    Nelson              // rules 1-8
};

// One time step: a subgroup of measurements, a single value for I-MR, or a
// count and the size it was counted over for the attribute charts.
struct ControlSubgroup {
    std::string label;                // "08:00", "Lot 1142"; empty = its number
    std::vector<double> values;       // measurements; non-finite entries are ignored
    double count = 0.0;               // p, np: defectives; c, u: defects
    double size = 0.0;                // p, np: lot size; u: units inspected
    bool excluded = false;            // plotted and checked, left out of the limits
    std::string note;                 // an assignable cause, shown with the point
};

// The statistic one pane plots, with its limits per point.
struct ControlSeries {
    std::string name;                 // "Subgroup mean", "Range", "Proportion defective", ...
    std::vector<double> value;        // NaN = nothing plotted (MR's first point)
    std::vector<double> centre;
    std::vector<double> upper;
    std::vector<double> lower;        // raised to 0 where the statistic cannot go below
    std::vector<double> sigma;        // the point's own sigma, for zones and rules
    // Every point has the same limits (one stage, one subgroup size).
    bool constantLimits = true;
    // The lower limit was raised to 0 at every point: the pane has no lower
    // limit (R for n <= 6, MR always, an S chart for n <= 5).
    bool lowerClamped = false;
    // The statistic cannot go below zero (ranges, deviations, counts,
    // proportions): a lower limit of 0 there is none.
    bool nonNegative = false;

    // Whether point i has a lower limit at all.
    bool HasLowerLimit(size_t i) const {
        return !lowerClamped && i < lower.size() && std::isfinite(lower[i]) && !(nonNegative && lower[i] <= 0.0);
    }
};

// A rule signal. `point` completes the pattern; `firstPoint` is where the
// pattern starts (equal for rule 1).
struct ControlViolation {
    size_t point = 0;
    size_t firstPoint = 0;
    int rule = 0;                     // 1-based number within the result's rule set
    size_t pane = 0;                  // 0 = location, 1 = dispersion
};

// A run of points with limits of their own: the process as it was between
// two changes.
struct ControlStage {
    size_t firstPoint = 0;
    std::string name;                 // "New tooling"; empty = none shown
};

struct ControlCapability {
    bool available = false;           // a measurement chart with at least one spec limit
    double mean = std::nan("");
    double sigmaWithin = std::nan(""); // the chart's estimate: short-term spread
    double sigmaOverall = std::nan(""); // the sample SD of every value: long-term spread
    double cp = std::nan(""), cpk = std::nan("");   // from sigmaWithin; Cp needs both limits
    double pp = std::nan(""), ppk = std::nan("");   // from sigmaOverall
};

struct ControlOptions {
    ControlChartKind kind = ControlChartKind::XbarR;
    ControlRuleSet ruleSet = ControlRuleSet::WesternElectric;
    // Rules 1..8 of the set (only 1..4 exist in the Western Electric set);
    // all on by default.
    std::array<bool, 8> rulesEnabled = {true, true, true, true, true, true, true, true};
    // The "same side of the centre line" run: Western Electric rule 4,
    // Nelson rule 2. 0 = the set's own length (8, Nelson 9).
    int sameSideRun = 0;
    // Run the whole set on the dispersion pane too, not rule 1 alone.
    bool rulesOnDispersion = false;

    // A process change starts a new stage at each of these points (the
    // first stage starts at 0 whether listed or not).
    std::vector<ControlStage> stages;
    // Limits from each stage's first N points only (Phase I), later points
    // judged against them (Phase II). 0 = from every point of the stage.
    size_t baselineCount = 0;

    // np charts: the lot size when the subgroups give none.
    double defaultSize = 0.0;

    // Specification limits (NaN = none): drawn on the location pane and
    // giving the capability of the last stage. Measurement charts only.
    double lowerSpec = std::nan("");
    double upperSpec = std::nan("");
    double target = std::nan("");
};

struct ControlResult {
    ControlChartKind kind = ControlChartKind::XbarR;
    ControlRuleSet ruleSet = ControlRuleSet::WesternElectric;
    int sameSideRun = 8;              // the run length the rules used
    std::vector<std::string> labels;
    ControlSeries location;
    std::optional<ControlSeries> dispersion;     // none for the attribute charts
    std::vector<ControlViolation> violations;
    // The within-subgroup estimate of the process sigma (of an individual
    // value), of the last stage; NaN for the attribute charts.
    double sigmaWithin = 0.0;
    int subgroupSize = 0;             // X-bar-R / np: n; I-MR, c: 1; varying: the most common
    std::vector<ControlStage> stages; // as applied: sorted, the first at 0
    std::vector<bool> excluded;       // per point
    std::vector<std::string> notes;   // per point
    double lowerSpec = std::nan(""), upperSpec = std::nan(""), target = std::nan("");
    ControlCapability capability;
    // Unequal subgroup sizes, too few subgroups for trusted limits, and the
    // like - in English, for a status line or a log.
    std::vector<std::string> warnings;

    // The rules point `index` of `pane` completes (empty = none).
    std::vector<int> RulesAt(size_t index, size_t pane = 0) const;
    bool HasSignal() const { return !violations.empty(); }
    // The stage point `index` belongs to.
    size_t StageOf(size_t index) const;
};

ControlResult ComputeControlChart(const std::vector<ControlSubgroup>& subgroups,
                                  const ControlOptions& options = {});

// The rule signals of one pane's series against its own centre, limits and
// sigma - what ComputeControlChart runs on each pane and stage, usable on its
// own with limits from elsewhere (a frozen baseline). Pane 0 checks the
// enabled rules of the set; any other pane checks rule 1 only, against its
// limits (a lower limit of 0 on a non-negative statistic is none), unless
// options.rulesOnDispersion. A point with no value or a sigma of zero is never
// flagged.
std::vector<ControlViolation> FindRuleViolations(const ControlSeries& series,
                                                 const ControlOptions& options = {},
                                                 size_t pane = 0);

// One line describing a rule, in English ("Rule 2: nine points in a row on
// one side of the centre line"). sameSideRun 0 = the set's own length.
std::string ControlRuleDescription(ControlRuleSet set, int rule, int sameSideRun = 0);
// The Western Electric rule (1..4).
std::string ControlRuleDescription(int rule, int sameSideRun = 8);

// The bias-correction constants of a subgroup of n values: d2 and d3
// (lower-case in the SPC tables) are tabulated for n = 2..25, c4 is computed
// from its closed form for any n >= 2 (NaN outside).
double ControlD2(int n);
double ControlD3(int n);
double ControlC4(int n);
// The chart factors derived from them: A2 = 3 / (d2 sqrt n), the range
// chart's D3 = max(0, 1 - 3 d3 / d2) and D4 = 1 + 3 d3 / d2, A3 = 3 / (c4
// sqrt n), and the S chart's B3 = max(0, 1 - 3 sqrt(1 - c4^2) / c4) and B4 =
// 1 + 3 sqrt(1 - c4^2) / c4.
double ControlA2(int n);
double ControlRangeLowerFactor(int n);   // D3
double ControlRangeUpperFactor(int n);   // D4
double ControlA3(int n);
double ControlSigmaLowerFactor(int n);   // B3
double ControlSigmaUpperFactor(int n);   // B4

} // namespace UltraCanvas
