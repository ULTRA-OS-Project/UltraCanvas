- **Matrix diagram: the House of Quality's competitive assessment.** Right of
  the grid, how customers rate your product and its rivals on each need: one
  marker per competitor per need on a 1-to-`ratingMax` track, joined row to row.
  New `MatrixModel::competitors`, `ratings` and `ratingMax`, with
  `AddCompetitor`, `SetRating` (refusing unknown needs or competitors and values
  out of range) and `RatingOf`; `Validate()` and the JSON form carry them. The
  competitors join the legend - a click brings one's line forward - a need's
  tooltip lists its ratings, and `SetShowCompetitiveAssessment(false)` hides the
  block. The House of Quality sample rates three bicycles.
