- **Vector text draws underline, strikethrough and letter spacing, and
  aligns each line on its own.** `VectorRenderer::RenderText` ignored
  `VectorTextStyle::Underline`, `StrikeThrough` and `LetterSpacing` - an SVG
  with `text-decoration` or `letter-spacing` lost them on screen and in every
  bitmap export - and shifted every line of a centred or right-aligned text
  by the width of all its lines together, so a multi-line text came apart. A
  span with a `Position` of its own now starts a text chunk (SVG's rule),
  which the anchor places on its own start point.
- **`VectorText::GetBoundingBox` counts every line of a text set over
  several**, each anchored on its own, with its letter spacing: the estimate
  stretched the first line by every line's characters and left the others
  out, so the selection frame and the hit test missed all but the first
  line. Characters are counted, not UTF-8 bytes (an accented or Thai text
  measured up to three times too wide). `VectorModelTest` checks it.
- `TextAttributeFactory::CreateLetterSpacing` takes fractional pixels
  (`double`): in a drawing's units, whole pixels were whole points of
  tracking.
- `RichTextEditStyle::letterSpacing`: `UltraCanvasRichTextEdit` tracks the
  whole text wider or tighter, so text typed on a canvas shows the letter
  spacing it will be drawn with.
