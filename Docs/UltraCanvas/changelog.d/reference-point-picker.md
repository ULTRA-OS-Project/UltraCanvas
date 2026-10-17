- **New element: `UltraCanvasReferencePointPicker`.** A 3 x 3 grid of the
  corners, edge midpoints and centre of a box, for a transform bar to say
  which point of the selection stays put when it is moved, scaled, rotated or
  sheared. Click or the arrow keys pick a point; `ReferencePointIn(point, box)`
  gives its position. Doc: `Docs/UltraCanvas/UltraCanvasReferencePointPicker.md`.
  ArtCreator 0.8.0's transform bar is the first user.
- **`UltraCanvasSpinner::SetSnapToStep(false)`** keeps a Decimal value off the
  step grid: a field can show a measurement of `8.61 cm` and still step by
  `0.1` from it. Until now `SetValue` rounded every value onto
  `min + n * step`, so a field had either fine steps or rounded values.
- **`UltraCanvasSpinner::onStepsFinished`** fires once when a run of steps
  ends - the arrow button or key released, the wheel at rest, the focus gone -
  instead of once per step like `onValueChanged`. A held arrow key reached a
  caller as one change per repeat, so an editor that records undo per change
  made a held key dozens of undo steps; with this it can make them one.
  `IsStepping()` says whether a run is open. On X11 a held key's repeats
  each arrive as a release and a press; the run waits 120 ms after a release
  for that press, so it is not split.
