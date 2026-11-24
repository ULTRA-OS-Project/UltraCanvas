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
- **`UltraCanvasSlider` sizes itself.** Its `MeasureOwnContent` was a TODO,
  so a slider had to be given a size, and one given too little for its ticks
  and labels had them cut off. It now reports its size to the layout - a
  preferred length, and across the bar what the handle, ticks, labels, value
  text, bubble or histogram need (`GetMinimumSize`, `GetPreferredLength`) -
  and when a size given explicitly is too small across the bar for what it
  shows, it sets its own `min-height` (`min-width` when vertical) so the
  layout grows it. A plain slider keeps its given size exactly; a minimum the
  application set is left alone; `SetGrowToFit(false)` turns it off.
- **DemoApp follows a dark theme.** The page kit (`UltraCanvasDemoPageKit`)
  and the demo's own frame - the header, the status legend, the page area and
  its scrollbars - had fixed light colours, so with a dark theme the tree and
  the controls turned dark while the pages, cards and header stayed light,
  and theme-coloured text on them (the header's title, a slider's value, the
  range sliders' labels) was white on white. They now take the theme's roles
  in a dark or high-contrast theme (the light look is unchanged) and change
  with it when the theme switches while a page is shown
  (`DemoKit::ColourOf`, `KitContainer`, `KitLabel`). Pages that paint their
  own fixed colours still do.
- **DemoApp: the example pages follow a dark theme too.** About 90 example
  files painted their own light panels, readouts and grey captions with fixed
  colours, so with a dark theme they stayed light, or showed the theme's
  white text on a light panel. `DemoKit::Themed(colour)` keeps a colour as it
  is in the light look and, in a dark one, turns its lightness over while
  keeping its hue (a pale green panel becomes a dark green one, dark grey
  text light grey); `ThemedFill` / `ThemedInk` do it for a colour known only
  at run time, and only for a pale fill or dark text. The pages' decorative
  colours - about 400 calls on labels, panels, panes, dividers and toolbars -
  go through them; charts and diagrams drawn on their own white paper,
  colour swatches, coloured buttons and status pills keep their colours. The
  page shown is rebuilt when the theme switches, so its colours follow.
- DemoApp: the Performance Matrix page's title is 40 px tall, room for a
  Japanese or Chinese line (it had 35 and needed 37).
