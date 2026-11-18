- **The engine's scatter chart joins a group's points.**
  `EngineScatterGroup::joined` draws a straight line through the group's
  points in the order they are given - each group over its own x values,
  which need not rise - with `showMarkers`, `lineWidth`, `SetJoinLineWidth`
  and `GroupLineRuns`.
- **A document's scatter chart keeps its lines whatever its series' x
  values.** A joined scatter chart was drawn by the line chart, which needs
  one row of x values for all series; with different x values the lines were
  dropped. Every scatter chart is now the engine's scatter chart.
