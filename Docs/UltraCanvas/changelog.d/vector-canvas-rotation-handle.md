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
