- **The `*Examples.md` component docs describe the API that exists.** A new
  checker, `scripts/check_doc_examples.py`, compiles the C++ in a doc
  against the headers (clang++, Linux) and reports, at the doc's own line,
  every example that does not compile, every listed function, field or
  enumerator the class does not have, and every signature that differs. Run
  over the 60 Examples docs it found 694 problems in 42 of them; all 60 pass
  it now. The usual faults: a numeric `long id` / `uid` argument and `long`
  coordinates on constructors and factories (they take `float`s and no id),
  functions that were renamed or never existed (`GetButtonState`,
  `SetAutoresize`, `Show`/`Hide` on a menu, `SetModel(IListModel*)`,
  `SetGridEnabled`, `onOverflowChange`), style fields that are not there
  (`hoverBackgroundColor`, `padding`, `borderWidth`), wrong return types
  (`Point2Df` for `Point2Dd`, `Rect2Di` for `Rect2Df`), and listings written
  as pseudo-code. `UltraCanvasLayoutExamples.md` is rewritten for the CSS
  layout engine (the box/grid/flex layout managers it documented are gone),
  and `UltraCanvasBasicChartsExamples.md`, which held a stale copy of the
  demo's C++ source, is now a doc for the line, bar, scatter and area
  charts.
- **`CreateAdjacencyDiagram` can be called.** Its declaration took a
  `long uid` that its only definition does not, so every call failed to
  link; the declaration now matches.
- Comments: `UltraCanvasSpacer.h` and `AddSpacer` no longer say the cross
  axis stretches by default, and the usage notes in
  `UltraCanvasJitterPlotElement.h` drop the numeric id.
