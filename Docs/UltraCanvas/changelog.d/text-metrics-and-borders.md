- **Menu: the checkbox / radio indicator sits level with its label.** It was
  drawn one pixel above the row's centre line while the label was drawn on
  it, so the box floated above the text. The label is now centred on the row
  by its cap height rather than its line box, which holds the ascender and
  descender space too and so put a mixed-case label a shade lower than the
  indicator. The radio dot is a filled disc rather than a stroked ring, and
  the mark greys with a disabled item.
- **Single-line text centres on its capitals, everywhere.** A layout drawn
  with `VerticalAlignment::Middle` in a box of known height - a button
  label, a list-view cell, a dropdown, a tab - now puts the middle of a
  capital letter on the middle of the box instead of the middle of the
  font's ascent + descent band, which sat up to a pixel higher on fonts
  with tall ascenders. Text drawn at a point gets the same line through
  `IRenderContext::TextTopCentredOnCaps(row, font)` and
  `GetCapCentreOffset(font)`; the menu and the spinner use them. The cap
  height comes from `ITextLayout::GetCapHeight()`, measured once per font
  from the ink of a capital H and cached on the render context, so no
  render pass measures twice. The caches are per context, not process-wide,
  and `IRenderContext::InvalidateFontMetricsCache()` clears them: the Cairo
  backend calls it when a context's surface, resolution, hinting or
  antialiasing changes, so a font is measured again under the new settings.
  The antialias, hint-style and hint-metrics setters, which each cleared
  the shared layout cache and re-applied the options by hand, now share
  one `InvalidateAllFontMetricsCaches`, the single place that knows what
  a font-settings change invalidates.
- **TextInput: text, selection and caret share one cap-centred line box.**
  The field centred its text by the line height, placed the caret by
  1.2 × the font size and sized it by 1.4 ×, so the three drifted apart and
  the text sat below a button or checkbox beside the field. One line box
  (`GetTextLineBox`) now positions all three, with the font's capitals on
  the field's centre line and the caret spanning the font's line height.
  `UltraCanvasAutoComplete` inherits it. The text area's lines flow from
  the top and its caret follows the layout, which is right for a
  multi-line editor; its one single-line label, the placeholder for a
  missing markdown image, is now cap-centred in its box too.
- **`DrawFilledRectangle` / `DrawFilledCircle` keep the border inside the
  shape.** A stroke is centred on its path, so a 1px outline on a rectangle
  with whole-pixel edges was smeared over two rows of pixels on each side
  and read as a grey haze, on every checkbox, input and menu indicator in
  the framework. The path is now inset by half the border width
  (`IRenderContext::InsetForStroke`, usable on its own), so the outline
  sits on whole pixels with its outer edge on the rectangle's edge and its
  centre unchanged; a rounded corner keeps its outer radius. A circle's
  ring likewise stays within its radius instead of overhanging it by half
  a stroke.
- **Menu: a Radio item's outline is a circle.** It was the same square box a
  Checkbox item gets, so the two item kinds could not be told apart until one
  was checked. `MenuStyle::radioShape` chooses: `MenuRadioShape::Round` (the
  default, as `UltraCanvasRadio` draws it) or `MenuRadioShape::Square`, the
  old look. UltraMail's "Show emails" filter menu uses the default and so
  gets the circle.
- **TextArea: a scroll step is one laid-out line.** The wheel and the page
  keys stepped by 1.3 x the font size, an estimate that drifted from the
  real line height by a few pixels a notch and left the top line cut
  part-way through after a few turns. They now step by the measured line
  height the layout uses, and PageUp / PageDown, when the caret has no
  on-screen rectangle to measure a page from, move by the lines that fit
  the visible area instead of a fixed ten.
- **The first-surface text-render diagnostic answers a measurement question
  on its own.** It logged the Pango and Cairo resolutions, the device scale
  and cairo's font options as bare enum numbers, read before the framework's
  own options were applied. It now logs after they are, names every option
  (antialias, hint style, hint metrics, subpixel order) for both the Pango
  context and cairo, adds the surface size and the pinned resolution, and
  measures the default font on that context: line height, baseline, cap
  height and the width of an H, to compare across machines before
  suspecting a caller. A runtime change to the antialias, hint-style or
  hint-metrics setting logs the new options too, so the log stays true
  after it.
- **The caret follows a DPI change.** When a window moved to a display with
  another scale, the window, popup and tooltip contexts were rebuilt at
  the new scale but the shared caret's was not: it is rebuilt only when
  its size changes, and the caret's logical size is the same on both
  displays, so it kept painting from a surface made at the old scale. The
  window now drops it with the others. (Checked on the way: Windows,
  macOS and Linux all reach the shared `HandleDeviceScaleChange`, which
  makes a new render context, so the per-context font-metrics caches
  start fresh on every platform.)
