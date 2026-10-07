- **DemoApp gauges: the cards no longer carry a size they never had.** Every
  gauge card, and the gauge in it, was built at 272 x 374 (`kCardW` /
  `kCardH`) and then placed in a 1fr grid cell of about 180 x 247. The grid
  sized them anyway, but the stale heights made the Progress, Specialized
  and Analog tabs taller than their area, so a vertical scrollbar appeared
  and narrowed every card. They are built without a size now; the tabs fit
  and the scrollbar is gone. The Round Gauges tab is unchanged, pixel for
  pixel.
- **`Docs/UltraCanvas/UltraCanvasLabelExamples.md` documents the label that
  exists.** It described a constructor with a numeric `long id`, `LabelStyle`
  fields the struct does not have (padding, border, `wordWrap`,
  `autoResize`), and functions that were never there or are gone -
  `SetAutoResize`, `SetWordWrap`, `SetCrossAlignment`, `SetShadow`,
  `SetBorderWidth`, `AppendText`, `ClearText`, `IsEmpty`, `CreateAutoLabel`,
  `CreateHeaderLabel`, `CreateStatusLabel`, `CreateLabelBuilder`,
  `onSizeChanged`. It now follows `UltraCanvasLabel.h`: the four
  constructors, the real `LabelStyle`, text links, the factories,
  `LabelBuilder`, and how a label sizes itself in a layout. Every example in
  it compiles against the headers.
