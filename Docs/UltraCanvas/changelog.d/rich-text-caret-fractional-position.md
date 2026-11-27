- **The rich text view's caret stands where its line is, to the fraction of
  a pixel.** It was placed from `ITextLayout::GetCursorPos`, whose rectangle
  is in whole pixels cut toward zero, so in a font whose line is not a whole
  number of pixels the caret on a paragraph's second line and after it stood
  up to a pixel above its line - in DejaVu Serif at 12 pt (18.625 px a line)
  at 18, 37 and 55 px instead of 18.6, 37.3 and 55.9 - and scrolling the
  caret into view used the same rounded rectangle. The layout's lines were
  right; the caret was not. `ITextLayout::GetCursorRect` gives the strong
  cursor in fractional pixels (`UltraCanvasRenderContext.h` 2.15.0), and
  `UltraCanvasRichTextEdit` places and scrolls to the caret with it. The
  editor test measures its line heights in DejaVu Serif too where it is
  installed, with hinted font metrics off and on.
