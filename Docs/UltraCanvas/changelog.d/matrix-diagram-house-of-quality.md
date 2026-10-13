- **Matrix diagram, Phase 3: the House of Quality.** `MatrixShape::House` now
  lays out the QFD composite instead of falling back to an L: customer needs
  against technical requirements, with the correlation **roof** drawn at 45
  degrees over the requirements' header, an importance column, a
  direction-of-improvement row and a target row. The roof is a symmetric panel
  (`MatrixPanel::symmetric`): the model stores each pair once above the
  diagonal whichever way round it is set, and refuses the diagonal. New
  `MatrixScale::Correlation()` (++ / + / − / −−), `MatrixItem::improvement`
  and `MatrixItem::target`, and `MatrixSamples::HouseOfQuality()`.
- Matrix diagram: `SetRollUp(MatrixRollUp::Bars)` draws the column totals as a
  strip of bars with each column's rank, the top three highlighted;
  `MatrixModel::RelativeColumnWeights()` gives each column's share, and a
  column's tooltip now shows its rank, share and target.
- Matrix diagram: `ImportMatrixTable()` builds a model from a spreadsheet's
  tabular form - items from the header row and first column, marks by level id,
  label, glyph or weight, and an optional Importance column - and reports the
  line and mark it could not read. `ExportMatrixCsv(..., true)` writes the
  importances, and `ImportMatrixCsv` reads them back. Numbers are dot-decimal
  in every locale.
- Matrix diagram: a label wrapped onto its last line dropped the words that did
  not fit without a trace; the last line now ends in "..." instead.
- DemoApp: a House of Quality tab on the Matrix Diagram page, and a control
  that switches the column totals between bars and numbers.
