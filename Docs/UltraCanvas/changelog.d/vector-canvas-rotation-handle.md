- **`UltraCanvasVectorCanvas`: a rotation handle, and one object's own turned frame in rotate mode.**
  The Rotate handles now sit on the selected object's own box, turned and
  sheared as the object is (`SelectionFrame`, `SelectionFramePoint`,
  `SelectionFrameFraction`); several objects keep their upright bounds. A
  rotation handle (`VectorHandle::Rotation`) starts at the rotation centre and
  points the way the selection is turned: a dial of 15° ticks with a wedge
  from 0 to the angle (`VectorRotationHandleStyle::Dial`, the default) or a
  stem to a knob beyond the frame (`Stem`). While a tool drags a turning
  handle (`SetDraggedHandle`) the frame and the other handles are hidden.
  New `onHandleModeChanged` and `onRotationCenterChanged`. A moved rotation
  centre is no longer dropped by an edit that selects the same objects again,
  only when the selection changes.
- **`UltraCanvasSpinner`: a held arrow button repeats.** After 400 ms, then
  every 60 ms while the pointer stays on the button, as a held arrow key does;
  `SetButtonRepeat(delayMs, intervalMs)` changes or turns it off. The repeats
  are one run of steps, so `onStepsFinished` still fires once, on release.
- **`UltraCanvasReferencePointPicker`: a free point.** `SetFreePoint(fractions)`
  shows a point that is none of the nine as a ring in its direction, kept
  inside the frame; picking one of the nine clears it.
- **A selection's box now includes the width of its lines.** It ran down the
  middle of a line, so a thick stroke stood out past the handles and the
  transform bar measured the object too small. `VectorEdit::PaintBounds` (and
  `OwnPaintBounds`, before the element's own transform) gives the box an
  element paints: half the line's width to each side, a miter join's point
  while within the miter limit, square and round caps, arrowheads, the widest
  point of a width profile or brush. The points go through every transform
  first, so a turned line measures as drawn. `VectorSelection::Bounds`,
  `AlignElements`, `DistributeElements` and the canvas's frame, per-object
  outlines and object snapping use it; `DocumentBounds` stays the geometry
  alone. The canvas measures the frame once per paint and hit test.
- **Marquee selection measures lines by their width too.**
  `VectorHitTester::ElementsIn` (and the canvas's `ElementsIn`) took an
  object's box down the middle of its line, so a marquee that cut through a
  thick line still selected it "fully inside", and one touching only its edge
  missed it. It uses `PaintBounds` now.
- **`RasterizeVectorElements` no longer cuts off wide lines and far shadows.**
  It rendered the elements' geometry box plus a fixed margin (32 points and
  5%), so a 100-point line along a 10-point path came out 76 x 66 instead of
  110 x 100, and a shadow cast 120 points away was lost. The area now starts
  from `PaintBounds` and adds the reach of each element's shadow (offset,
  blur, floor), feather, outward contour and outer bevel, its children's
  included; the old margin stays on top for what is measured loosely.
