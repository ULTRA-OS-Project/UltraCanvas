- **A node diagram label that fit by the arithmetic was shortened by a
  rounding, and the backend is cleared of the blame.** A card that grew to
  exactly the width its name needed drew that name with an ellipsis: the
  text is measured in whole pixels and the room for it computed in
  fractions, so an exact fit came out a hundredth short. The fit now keeps
  one pixel of slack on both sides of the comparison, in place of the five
  percent margin 2.4.0 added on the theory that font hinting widened text
  drawn at a low zoom. `Tests/TextMeasureZoomTest.cpp` measures that theory
  against the Cairo backend and finds it false: text drawn under a zoom from
  0.4 to 3.0 is the unscaled measurement times the zoom, to within a pixel,
  so an element that lays out at one zoom and draws at another can trust its
  measurements. The test stays as the guard.
