- **A text field's caret and selection reach the bottom of the text.** The
  field's line box was `GetTextLineHeight("H")` tall: whole pixels, with the
  fraction cut off (the size is converted with a `static_cast<int>`). The
  glyphs are drawn at the font's full line height, so the caret ended up to a
  pixel above the descenders and the bottoms of parentheses, and
  `TextMetricsScreenshotTest` failed on that ("The caret spans the text's line
  box: at or below the ink's bottom"). The box is the font's height in
  fractional pixels now, from the new `IRenderContext::GetLineBoxHeight(font)`
  (cached per font, cleared with the other font measurements), and the caret
  covers every pixel row the box touches. `UltraCanvasTextInput` 1.7.1,
  `UltraCanvasRenderContext.h` 2.8.0.
