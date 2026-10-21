- **Chart axes are marked at whole steps.** The line, area, scatter and bar
  charts (and every chart that uses the base class's axes) divided the value
  range into six equal parts, so an axis read 0, 24.85, 49.7 ... 149.1 with
  as many decimals as the division produced, and the grid's eight and ten
  fixed lines matched none of them. Ticks now fall on every multiple of a
  whole step - 1, 2, 2.5 or 5 x 10^n - inside the range, labelled with the
  decimals that step needs and in K or M once the largest tick reaches ten
  thousand or ten million; the grid lies on the same ticks (in DataLabel
  mode, on the labelled points). `ComputeChartAxisTicks` and `ChartNiceStep`
  are public for charts that draw their own axes. `FormatAxisLabel` writes
  1500 as "1.5K" (it truncated to "1K") and no longer goes through
  `std::to_string`.
- **A bar chart whose smallest value is 0 started its axis below zero.** The
  base class padded the range by 5 % first, and the bar chart's "the axis
  reaches 0" check then saw -7.1 and did nothing, so the zero line floated
  above the bottom of the plot. The axis now reaches 0 before it is padded,
  is padded only away from zero, and is rounded out to the next whole tick.
- **`UltraCanvasBarChartElement` runs sideways and takes a fixed range.**
  `SetOrientation(BarChartOrientation::Horizontal)`: one row per category,
  its name on the left, values along the bottom - for named amounts like
  power by device. `SetValueRange(min, max)` / `ClearValueRange()` fix the
  value axis (a bar past it stops at the plot's edge). `SetShowValueLabels`
  now works for bars (off by default for them), a point's own colour is
  drawn instead of the bar colour, and `GetBarRect(index)` says where a bar
  is. `Tests/ChartElementBehaviourTest.cpp` section 12 covers all of it.
