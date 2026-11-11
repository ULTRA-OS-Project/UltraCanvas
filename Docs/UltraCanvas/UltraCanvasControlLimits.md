# UltraCanvasControlLimits

Statistical process control without a window: the centre line, control
limits and run-rule signals of a Shewhart control chart, computed from the
data. `ShowControlChart` draws a result on the engine line chart
([UltraCanvasEngineCharts.md](UltraCanvasEngineCharts.md#control-charts)).

```cpp
#include "Plugins/Charts/Engine/UltraCanvasControlLimits.h"
```

## Overview

A control chart plots a measurement of a process in time order against a
centre line and two control limits three standard deviations either side. It
answers one question: is the variation in this process the usual kind, or has
something changed? A point beyond the limits, or a pattern of points inside
them, is a signal worth investigating; everything else is noise to leave
alone.

Control limits are not specification limits: they describe what the process
does, not what the customer accepts.

| Kind | Plots (pane 0 / pane 1) | Data (`ControlSubgroup`) |
|---|---|---|
| `XbarR` | subgroup means / ranges | `values`: 2..25 measurements per subgroup |
| `XbarS` | subgroup means / standard deviations | `values`: 2 or more, sizes may vary |
| `IndividualsMR` | individual values / moving ranges of two | `values`: one value per point |
| `P` | proportion defective (one pane) | `count` defectives in a lot of `size` (sizes may vary) |
| `NP` | number defective | `count` in a lot of constant `size` |
| `C` | defects per inspection unit | `count` |
| `U` | defects per unit | `count` defects over `size` units (may vary) |

## Quick start

```cpp
std::vector<ControlSubgroup> hours = {
    {"08:00", {25.004, 24.993, 25.011, 25.002, 24.998}},
    {"09:00", {25.007, 25.001, 24.996, 25.009, 25.000}},
    {"10:00", {24.995, 25.003, 25.006, 24.999, 25.012}},
};
ControlResult result = ComputeControlChart(hours);

double centre = result.location.centre[0];
double ucl = result.location.upper[0];
for (const ControlViolation& v : result.violations) {
    std::string what = ControlRuleDescription(v.rule);   // "Rule 1: a point beyond the control limits"
    std::string when = result.labels[v.point];
}
for (const std::string& warning : result.warnings) {
    // "Only 3 points: limits from fewer than 20 are preliminary."
}
```

For individuals, one value per point; for counts, a count and the size it
was counted over:

```cpp
ControlOptions options;
options.kind = ControlChartKind::IndividualsMR;
std::vector<ControlSubgroup> days = {{"Mon", {4.1}}, {"Tue", {3.8}}, {"Wed", {4.4}}, {"Thu", {4.0}}};
ControlResult daily = ComputeControlChart(days, options);

ControlSubgroup lot;
lot.label = "Lot 1142";
lot.count = 7;            // failed inspection
lot.size = 180;           // items inspected
ControlOptions attributes;
attributes.kind = ControlChartKind::P;
ControlResult lots = ComputeControlChart({lot}, attributes);
```

## Where the limits come from

Sigma is estimated within subgroups, never from the standard deviation of
all the values: a shift in the process inflates that standard deviation,
which widens the limits and hides the very shift the chart is meant to show.

| Pane | Centre | Limits | Point sigma |
|---|---|---|---|
| X-bar (with R) | the mean of the subgroup means | centre +- A2 R-bar | R-bar / (d2 sqrt n) |
| R | R-bar | D3 R-bar and D4 R-bar | d3 R-bar / d2 |
| X-bar (with S) | the means weighted by n | centre +- 3 sigma / sqrt n_i | sigma / sqrt n_i |
| S | c4(n_i) sigma | centre +- 3 sigma sqrt(1 - c4^2) | sigma sqrt(1 - c4^2) |
| I | the mean | centre +- 2.66 MR-bar | MR-bar / d2(2) |
| MR | MR-bar | 0 and 3.267 MR-bar | d3(2) MR-bar / d2(2) |
| p | p-bar = all defectives / all items | p-bar +- 3 sqrt(p-bar (1 - p-bar) / n_i) | per lot |
| np | n p-bar | n p-bar +- 3 sqrt(n p-bar (1 - p-bar)) | |
| c | c-bar | c-bar +- 3 sqrt(c-bar) | |
| u | u-bar = all defects / all units | u-bar +- 3 sqrt(u-bar / n_i) | per lot |

For X-bar-S, sigma is S-bar / c4(n) when the subgroups are the same size
(so the limits are the textbook A3 S-bar, B3 S-bar and B4 S-bar), and the
pooled standard deviation unbiased by c4 of its degrees of freedom plus one
when they are not. Where the size varies - X-bar-S, p, u - each point has
limits for its own size (`constantLimits` is false). `result.sigmaWithin` is
the estimate of an individual value's sigma (NaN for the counts).

```cpp
double d2 = ControlD2(5);                 // 2.326 (tabulated, n = 2..25)
double d3 = ControlD3(5);                 // 0.864
double c4 = ControlC4(5);                 // 0.9400 (closed form, any n >= 2)
double a2 = ControlA2(5);                 // 0.577 = 3 / (d2 sqrt n)
double d3f = ControlRangeLowerFactor(5);  // D3 = 0 up to n = 6
double d4f = ControlRangeUpperFactor(5);  // D4 = 2.114
double a3 = ControlA3(5);                 // 1.427
double b3 = ControlSigmaLowerFactor(5);   // B3 = 0 up to n = 5
double b4 = ControlSigmaUpperFactor(5);   // B4 = 2.089
```

d2 and d3 are kept to four decimals, so the derived factors match their own
published three-decimal tables.

A lower limit below zero for a statistic that cannot go below zero (ranges,
deviations, counts, proportions: `nonNegative`) is raised to 0, and that
point has no lower limit (`HasLowerLimit`); a pane where no point has one is
`lowerClamped`. A proportion's upper limit stops at 1, an np chart's at n.

Subgroups of unequal size on X-bar-R use the most common size for the
constants, with a warning that X-bar-S handles them; a subgroup with fewer
than two values is left out. I-MR takes the first value of each point. An
np chart wants one lot size (`defaultSize` stands in where a lot gives none);
a lot without a size or a valid count is left out, with a warning.

## Run rules

Rules are evaluated on each point's z-score, (value - centre) / sigma, with
that point's own sigma. A pattern flags **the point that completes it**;
`firstPoint` keeps where it started, and a long run flags every point from
the one that completes it on.

| Rule | Western Electric (default) | Nelson (`ControlRuleSet::Nelson`) |
|---|---|---|
| 1 | a point beyond 3 sigma | a point beyond 3 sigma |
| 2 | two of three beyond 2 sigma, one side | nine in a row on one side |
| 3 | four of five beyond 1 sigma, one side | six in a row steadily increasing or decreasing |
| 4 | eight in a row on one side | fourteen in a row alternating up and down |
| 5 | | two of three beyond 2 sigma, one side |
| 6 | | four of five beyond 1 sigma, one side |
| 7 | | fifteen in a row within 1 sigma |
| 8 | | eight in a row beyond 1 sigma, either side |

The dispersion pane is checked against rule 1 only, unless
`rulesOnDispersion`: the pattern rules assume roughly normal, independent
values, which ranges are not. A point on the centre line, or a gap, ends a
run; a point with no value or a sigma of zero is never flagged.

```cpp
ControlOptions options;
options.ruleSet = ControlRuleSet::Nelson;
options.rulesEnabled = {true, true, false, true, true, true, true, true};   // all but rule 3
options.sameSideRun = 8;                            // 0 = the set's own: 8, Nelson 9
ControlResult checked = ComputeControlChart(hours, options);
std::vector<int> rules = checked.RulesAt(2);        // the rules point 2 completes, pane 0
std::string text = ControlRuleDescription(checked.ruleSet, rules.empty() ? 1 : rules.front(),
                                          checked.sameSideRun);
```

## Stages, excluded points, baselines

A process change - new tooling, a new supplier - starts a **stage**: each
stage has its own limits, and no rule (nor a moving range) looks across a
stage boundary. An **excluded** point (an assignable cause found and fixed)
is still plotted and checked, but left out of the limits. A **baseline**
takes each stage's limits from its first N points only and judges the rest
against them.

```cpp
std::vector<ControlSubgroup> data = hours;
data[1].excluded = true;
data[1].note = "Gauge dropped";                   // shown with the point

ControlOptions staged;
staged.stages = {{2, "New tooling"}};             // a new stage from point 2 on
staged.baselineCount = 20;                        // limits from each stage's first 20 points
ControlResult phases = ComputeControlChart(data, staged);
size_t stage = phases.StageOf(2);                 // 1
```

`result.stages` lists the stages as applied (sorted, the first at 0),
`result.excluded` and `result.notes` the points' flags and notes. A stage
with fewer than 20 points behind its limits gets a "preliminary" warning.

`FindRuleViolations` runs the rules on one pane's series against whatever
limits it carries - limits frozen from a baseline, say:

```cpp
ControlSeries frozen = result.location;   // centre, limits and sigma from the baseline
frozen.value = {25.001, 25.031, 24.998};  // today's means against them
std::vector<ControlViolation> today = FindRuleViolations(frozen, ControlOptions(), 0);
```

## Capability

Specification limits on a measurement chart give the capability of the last
stage, from its values (excluded points left out):

```cpp
ControlOptions specified;
specified.lowerSpec = 24.97;
specified.upperSpec = 25.03;
specified.target = 25.0;
const ControlCapability cap = ComputeControlChart(hours, specified).capability;
// cap.cp  = (USL - LSL) / 6 sigma within      (both limits needed)
// cap.cpk = (nearest limit - mean) / 3 sigma within
// cap.pp, cap.ppk: the same with the overall standard deviation
```

Cp and Pp need both limits; with one, only Cpk and Ppk are given. On an
attribute chart specification limits are refused, with a warning.

## Drawing it

`ShowControlChart(chart, result, style)` replaces a line chart's categories,
series, bands, panes, limiters and highlights with the result
([UltraCanvasEngineCharts.md](UltraCanvasEngineCharts.md#control-charts)).
`ControlChartStyle` chooses the range pane and its height
(`showDispersion`, `dispersionWeight`), A / B / C zones (`showZones`), dotted
1 and 2 sigma lines (`showSigmaLines`), the "UCL = ..." captions
(`showLimitCaptions`), the specification lines (`showSpecLimits`,
`specColor`), the stage names (`showStageNames`) and the colours; a colour
left at alpha 0 comes from the chart's theme. Each pane's axis always reaches
its limits.

## Not yet

EWMA and CUSUM, and live appending against frozen limits, are Phase 3 of
[UltraCanvasControlChartProposal.md](UltraCanvasControlChartProposal.md).
