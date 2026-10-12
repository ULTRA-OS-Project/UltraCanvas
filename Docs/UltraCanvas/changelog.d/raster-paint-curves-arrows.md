- **The brush engine draws curves and arrows, and a path's stroke costs its
  length.** `RasterPaint::DrawCurve` strokes a cubic Bezier curve, and
  `FlattenCubicBezier` gives the points it passes through (at most 2 px
  apart by default, 8 to 4096 segments) for a preview to draw.
  `RasterPaint::DrawArrow` draws a shaft with a filled triangular head at
  the tip - and at the tail with `headAtTail` - shaft and heads in one
  coverage pass, so a translucent arrow has no darker seam; `ArrowShape`
  returns that geometry (the shaft between the heads' bases, the heads'
  corners) for the preview. A head is never narrower than the shaft plus
  2 px nor longer than the arrow, and a wide shaft stops at the head's base
  instead of poking out past the point. The stroke of `DrawPolygon` - and of
  `DrawCurve` - is now one coverage map that each segment marks near itself
  only: it tested every segment at every sample of the path's bounding box,
  which for a curve of a few hundred segments across a canvas cost seconds;
  the samples and the inside test are unchanged, so a polyline looks the
  same, its joins still painted once. `RasterEditingTest` checks a
  translucent polyline's corner and arrow seam, a curve's bend and an
  arrow's heads. UltraPaint's Polyline, Arrow and Curve tools draw with them
  (UltraPaint 0.2.16).
