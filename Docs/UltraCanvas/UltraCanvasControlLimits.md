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

| Kind | Plots (pane 0 / pane 1) | Data |
|---|---|---|
| `ControlChartKind::XbarR` | subgroup means / subgroup ranges | subgroups of 2..25 measurements |
| `ControlChartKind::IndividualsMR` | individual values / moving ranges of two | one value per time step |

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

For individuals, one value per point:

```cpp
ControlOptions options;
options.kind = ControlChartKind::IndividualsMR;
std::vector<ControlSubgroup> days = {{"Mon", {4.1}}, {"Tue", {3.8}}, {"Wed", {4.4}}, {"Thu", {4.0}}};
ControlResult daily = ComputeControlChart(days, options);
```

## Where the limits come from

Sigma is estimated within subgroups, never from the standard deviation of
all the values: a shift in the process inflates that standard deviation,
which widens the limits and hides the very shift the chart is meant to show.

| Pane | Centre | Limits | Point sigma |
|---|---|---|---|
| X-bar | the mean of the subgroup means | centre +- A2 R-bar | R-bar / (d2 sqrt n) |
| R | R-bar | D3 R-bar and D4 R-bar | d3 R-bar / d2 |
| I | the mean | centre +- 3 MR-bar / d2(2) (2.66 MR-bar) | MR-bar / d2(2) |
| MR | MR-bar | 0 and D4(2) MR-bar (3.267 MR-bar) | d3(2) MR-bar / d2(2) |

`result.sigmaWithin` is the estimate of an individual value's sigma
(R-bar / d2, MR-bar / d2(2)). The constants:

```cpp
double d2 = ControlD2(5);                 // 2.326 (tabulated, n = 2..25)
double d3 = ControlD3(5);                 // 0.864
double c4 = ControlC4(5);                 // 0.9400 (closed form)
double a2 = ControlA2(5);                 // 0.577 = 3 / (d2 sqrt n)
double lo = ControlRangeLowerFactor(5);   // D3 = 0 up to n = 6
double hi = ControlRangeUpperFactor(5);   // D4 = 2.114
```

d2 and d3 are kept to four decimals, so the derived factors match their own
published three-decimal tables; outside n = 2..25 the constants are NaN.

A computed lower limit below zero for a statistic that cannot go below zero
(the R chart up to n = 6, the MR chart always) is raised to 0 and marked
`lowerClamped`: that pane has no lower limit, and ShowControlChart draws
none.

Subgroups of unequal size use the most common size for the constants, with a
warning; a subgroup with fewer than two values is left out (not plotted).
I-MR takes the first value of each point and warns about any more.

## Run rules

Rules are evaluated on each point's z-score, (value - centre) / sigma, with
that point's own sigma. A pattern flags **the point that completes it**;
`firstPoint` keeps where it started, and a long run flags every point from
the one that completes it on. The Western Electric rules, all on by default:

1. One point beyond 3 sigma (outside the limits).
2. Two of three consecutive points beyond 2 sigma on one side.
3. Four of five consecutive points beyond 1 sigma on one side.
4. Eight points in a row on one side of the centre line.

The dispersion pane (R, MR) is checked against rule 1 only: the pattern rules
assume roughly normal, independent values, which ranges are not. A point on
the centre line, or a gap, ends a run; a point with no value or a sigma of
zero is never flagged.

```cpp
ControlOptions options;
options.rulesEnabled = {true, true, false, true};   // rules 1, 2 and 4
options.sameSideRun = 9;                            // some sources use nine
ControlResult checked = ComputeControlChart(hours, options);
std::vector<int> rules = checked.RulesAt(2);        // the rules point 2 completes, pane 0
bool anything = checked.HasSignal();
```

`FindRuleViolations` runs the rules on one pane's series against whatever
limits it carries - limits frozen from a baseline, say:

```cpp
ControlSeries frozen = result.location;   // centre, limits and sigma from the baseline
frozen.value = {25.001, 25.031, 24.998};  // today's means against them
std::vector<ControlViolation> today = FindRuleViolations(frozen, ControlOptions(), 0);
```

## Drawing it

`ShowControlChart(chart, result, style)` replaces a line chart's categories,
series, panes, limiters and highlights with the result
([UltraCanvasEngineCharts.md](UltraCanvasEngineCharts.md#control-charts)).
`ControlChartStyle` chooses the range pane and its height
(`showDispersion`, `dispersionWeight`), A / B / C zones (`showZones`), dotted
1 and 2 sigma lines (`showSigmaLines`), the "UCL = ..." captions
(`showLimitCaptions`) and the colours; a colour left at alpha 0 comes from
the chart's theme. Each pane's axis always reaches its limits.

## Not yet

X-bar-S, the attribute charts (p, np, c, u), EWMA and CUSUM, the Nelson
rules, stages, excluded points and capability indices are later phases of
[UltraCanvasControlChartProposal.md](UltraCanvasControlChartProposal.md).
