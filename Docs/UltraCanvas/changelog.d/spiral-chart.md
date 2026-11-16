- **Spiral chart (Phase 1).** `UltraCanvasSpiralChart` (chart engine) winds a
  time series along a spiral, one turn per period, so a daily, weekly or
  yearly rhythm lines up from the centre outward: a ribbon coloured by value
  (with a colour bar), a band whose width is the value, or a line; spokes and
  labels for the parts of a period, a label at each turn's start, and a
  tooltip per sample through a time formatter. `SetPeriod` rewinds the
  spiral. The UI-free `UltraCanvasSpiralModel` holds the samples and the
  period and maps a time to its place (`Position`), with the value range,
  the samples' spans and gaps and the nearest sample. Demo: **Charts >
  Spiral Chart** (temperatures, a year a turn; website visits, a week a
  turn) with a period slider. Tests: `SpiralModelTest`,
  `SpiralChartRenderTest`. Guide: `UltraCanvasSpiralChart.md`.
