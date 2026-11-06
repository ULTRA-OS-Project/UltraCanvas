- **Control charts on the engine line chart.** `ComputeControlChart`
  (`UltraCanvasControlLimits.h`, UI-free) computes the centre line, control
  limits and Western Electric run-rule signals of an X-bar-R or I-MR chart,
  with sigma estimated within subgroups; `ShowControlChart` draws a result on
  an `UltraCanvasEngineLineChart`: the statistic over its range, limits with
  their values in a column past the plot ("UCL = ..."), flagged points as red diamonds with the rule in their
  tooltip, optional A / B / C zones and sigma lines. `FindRuleViolations` runs
  the rules against limits from elsewhere. The demo's Charts > Line Chart page
  ends with a control chart card. Guide: `UltraCanvasControlLimits.md`.
- **The engine line chart gained general options.** Per series: a line shape
  (`shape`), markers (`showMarkers`), a legend entry (`inLegend`) and
  interactivity (`interactive`); per point: a look (`pointStyles`: colour,
  marker, hollow, size, always shown) and a tooltip note (`pointNotes`).
  `SetPanes` stacks panes, each with its own value axis, grid and title, the
  series naming their pane; `ValueAxisOfPane` gives the axis index limiters
  and highlights take, and `EngineLinePane::rangeIncludes` keeps limits in
  view. A chart that uses none of them draws as before.
- **Chart engine:** `SetExtraGridAxes` rules further axes (a grid in every
  pane); a limiter outside its axis's band is no longer drawn across a
  neighbouring pane; a limiter's caption is anchored half its width in from
  the line's end, so it sits beside the line instead of across it, and
  `ChartLimiter::captionPlacement` can pin it above the line's end
  (`AboveEnd`) or put it past the plot's edge in reserved room (`BeyondEnd`);
  new read accessors `GetLimiters`, `GetHighlights` and `HitRegionAt`.
- **Tests:** `ControlLimitsTest` (the constants against the published tables,
  hand-checked limits, within-subgroup sigma on a shifted process, every rule
  on its minimal pattern and a near miss) and `EngineLineChartTest` (the first
  test of the engine line chart: each new option, panes, and both control
  chart kinds drawn).
