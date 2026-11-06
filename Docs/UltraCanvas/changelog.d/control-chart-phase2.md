- **Control charts, Phase 2: X-bar-S, the attribute charts, Nelson rules,
  stages and capability.** `ComputeControlChart` adds X-bar-S (unequal
  subgroups too: the pooled standard deviation, limits for each subgroup's
  own size), the p, np, c and u charts (limits per lot where the sizes vary),
  the Nelson rules (`ControlRuleSet::Nelson`; the run length a setting; the
  whole set on the dispersion pane with `rulesOnDispersion`), stages (each
  with its own limits, no rule or moving range across a boundary), excluded
  points (plotted and checked, left out of the limits), `baselineCount`
  (limits from a stage's first points), notes, and specification limits with
  Cp, Cpk, Pp and Ppk. New factors `ControlA3`, `ControlSigmaLowerFactor`
  and `ControlSigmaUpperFactor`; `ControlC4` for any n >= 2.
- **`ShowControlChart` draws limits that change** as stepped lines with the
  last point's values in the caption column, zones under them as bands, a
  named divider at each stage, excluded points hollow, notes in the tooltips
  and specification limits in their own colour.
- **The engine line chart** fills between two series (`AddSeriesBand`),
  reports clicks on points (`onPointClick`) and can leave a series' line out
  (`drawLine = false`).
- **Chart engine:** the captions past the plot's edge are spread apart where
  they would overprint, and an `AboveEnd` caption on a vertical line sits
  beside it, inside the plot.
- **Demo:** the Line Chart page's control chart card adds a p chart of failed
  inspections and a c chart of paint defects, the Nelson rules, the late
  change as a new stage, specification limits with the capability in the
  summary, and a click that excludes a point from the limits.
