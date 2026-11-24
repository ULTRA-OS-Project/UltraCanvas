- **`UltraCanvasSlider`: tick marks, scales and new designs.** The
  slider had no ticks at all (the demo page carried a commented-out
  `SetShowTicks` that was never written), its circular style drew a ring with
  no value arc, and `SliderVisualStyle::cornerRadius` was declared but never
  drawn. Now:
  - **Ticks and labels**: `SetTickPosition` (`Before`, `After`, `BothSides`,
    `OnTrack`), `SetTickStyle` (`Line`, `Ramp` - marks that grow towards the
    maximum - and `Dot`), `SetTickInterval(major, minor)`, `SetTickMarks` for
    chosen values, `SetTickLabels` with a `tickLabelFormatter`, and
    `SetStopLabels` to name each step. The layout leaves the ticks and labels
    room and pulls the travel in so the end labels are not cut off.
  - **Handle shapes**: `Pointer` (points at the ticks), `Bar` (with
    `handleGap`), `Block`, `FaderCap` and `NoHandle`, and `handleShadow`.
  - **Styles**: `Knob` (a cap in a ring of dots), `Field` (a property-panel
    field that fills with the value), `Seek` (a media timeline with
    `SetSecondaryValue` for what has loaded and `SetHoverPreview`), `Ruler`
    (the scale slides under a fixed marker; `SetPixelsPerUnit`) and
    `Segmented` (blocks lit up to the value, `SetSegmentCount`).
  - **Values**: `SliderValueDisplay::Bubble` and `BubbleWhileDragging`;
    `SetFillOrigin` for a bipolar control that fills from zero; `SetResetValue`
    for a double-click reset; `SetColorZones`; `SetHistogram` draws the
    data's spread behind a (range) slider.
  - **Circular** draws its value arc now, over `SetArcAngles` (135° and 270°
    by default, a gap at the bottom), and the value in its centre. Circular,
    Knob and Ruler are turned by dragging from where they are, not set by
    where the press lands, and take the mouse wheel.
  - The track, its fill and the Progress bar are rounded by `cornerRadius`
    (at most half the thickness, so the default 3 on a 6 px track gives round
    ends); set it to 0 for the square look. The tooltip's colours are style
    fields from the theme rather than a fixed pale yellow.
  - The demo app's slider page has a **Slider Designs** section with each of
    them live, and its first slider shows ticks.
  - `Tests/SliderDesignsTest` covers where the ticks fall (a ruler's on round
    numbers), their labels and named stops, the double-click reset, and the
    relative drag of a knob and a ruler.
