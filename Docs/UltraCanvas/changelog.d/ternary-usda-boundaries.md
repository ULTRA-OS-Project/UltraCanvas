- **The ternary chart's USDA soil classes now match the Soil Survey Manual on
  the class boundaries.** A composition exactly on a boundary went to the
  class listed first, so 258 of the 5,151 whole-percent compositions - where
  lab results land - got another class than the manual gives: 7 % clay with
  48 % sand was sandy loam instead of loam, 50 % sand and 50 % silt sandy loam
  instead of silt loam. The manual (USDA Handbook 18, 2017, chapter 3) gives
  every boundary to the finer side, and the new `TernaryModel::edgeLean` says
  which side of a shared edge a point on it belongs to; the USDA preset leans
  that way and lists its silty classes so the rim matches too. The lean is
  saved in the JSON. `TernaryModelTest` now checks the classes against the
  manual's wording, transcribed word for word, at every whole percent.
