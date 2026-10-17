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
