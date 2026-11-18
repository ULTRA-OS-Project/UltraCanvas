- **Radar, bubble and stock charts in documents.** They were drawn as
  columns from DOCX and as Writer's replacement picture from ODT. They are
  now read as what they are (`RichChart::Kind::Radar`, `Bubble`, `Stock`;
  `RichChartSeries::sizes`; `RichChart::filled`), drawn by the engine's
  radar, scatter (size channel) and financial charts, and written back as
  such to DOCX (`c:radarChart`, `c:bubbleChart`, `c:stockChart`) and ODT
  (`chart:radar` / `chart:filled-radar`, `chart:bubble`, `chart:stock` - in
  ODF's own order of a stock chart's series).
- **Smoothed lines.** Word's `c:smooth` and ODF's `chart:interpolation` are
  read (`RichChartSeries::smooth`) and written; a smoothed line chart is
  drawn curved, and the engine's scatter chart draws a joined group as a
  curve through its points (`EngineScatterGroup::smooth`, `SmoothThrough`).
- A line, a radar line or a bubble without a stated colour is written to
  DOCX in the office palette's colour it is shown in: Writer draws such a
  series only where the file states one.
