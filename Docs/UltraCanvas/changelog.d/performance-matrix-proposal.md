- **New design proposal: a performance matrix element.** The demo app has
  listed *Performance Matrix* under Info Graphics as a "planned" placeholder
  with no design behind it.
  `Docs/UltraCanvas/UltraCanvasPerformanceMatrixProposal.md` now is that
  design, worked out from five reference images: the 9-box talent grid, the
  performance management matrix with coloured header tabs, coded
  performance-vs-potential tiles, bordered talent-review cards and Slack's
  importance–performance matrix with curved zones. It argues why none of the
  quadrant chart, matrix diagram, heatmap or SWOT diagram can absorb these,
  and proposes one UI-free `PerformanceMatrixModel` (named-level axes, cells,
  zones and an optional population placed by cell or by score) under an
  `UltraCanvasPerformanceMatrix` element with five designs (`Grid`, `Tiles`,
  `HeaderTabs`, `Cards`, `Zones`). It also covers text fitting, crowded cells,
  zone classification, colour-blind safety and personal data, and sets out
  six open questions and four phases. Its sketched API is recorded in
  `scripts/doc_examples_baseline.txt` until it is written.
