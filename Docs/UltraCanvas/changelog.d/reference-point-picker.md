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
  `IsStepping()` says whether a run is open.
- **Linux: a held key is one press, its repeats and one release.** X11 sent
  a held key's every repeat as a release and a press, so an element's key-up
  handler ran once per repeat and could not tell the key was still down -
  unlike Windows and macOS, where repeats are presses only. The Linux backend
  now turns on X11's detectable auto-repeat (`XkbSetDetectableAutoRepeat`),
  and on a server without it drops a release that the next queued event
  presses again at the same moment. `KeyUp` now means the key was let go on
  every platform.
