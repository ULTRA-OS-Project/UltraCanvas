- **A text field's caret reaches the descenders.** The field's line box -
  the height its text, selection and caret share - came from
  `GetTextLineHeight("H")`, which truncates the font's line height to whole
  pixels (17.94 became 17), so the caret stopped a pixel short of a `p` or a
  `(`. The new `IRenderContext::GetSingleLineHeight(font)` gives the
  unrounded height, cached per font like `GetCapCentreOffset`, and the field
  uses it. `TextMetricsScreenshotTest` passes again under Xvfb (it failed its
  caret check), and `TextInputFontTest` checks the measurement headless.
- The public headers' inline default bodies mark their unused parameters
  `(void)`, as `UltraCanvasRenderContext.h` already did in places: a build
  with `-Wextra` (the Models plugin, the tests, Texter, AnchorPoint) saw 48
  `-Wunused-parameter` warnings from 17 headers - the render context's on
  every file - and now sees none. Signatures and parameter names are
  unchanged.
