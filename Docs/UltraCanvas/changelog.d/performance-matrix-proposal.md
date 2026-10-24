- **New design proposal: a performance matrix element.** The demo app has
  listed *Performance Matrix* under Info Graphics as a "planned" placeholder
  with no design behind it.
  `Docs/UltraCanvas/UltraCanvasPerformanceMatrixProposal.md` now is that
  design, worked out from reference images of the 9-box talent grid, the
  performance management matrix with coloured header tabs, coded
  performance-vs-potential tiles and bordered talent-review cards. It argues why none of the
  quadrant chart, matrix diagram, heatmap or SWOT diagram can absorb these,
  and proposes one UI-free `PerformanceMatrixModel` (named-level axes, cells
  and an optional population placed by cell or by score) under an
  `UltraCanvasPerformanceMatrix` element with four designs (`Grid`, `Tiles`,
  `HeaderTabs`, `Cards`). It also covers text fitting, crowded cells,
  colour-blind safety and personal data, and sets out its open questions and
  three phases. Its sketched API is recorded in
  `scripts/doc_examples_baseline.txt` until it is written.
