- **Selected words can have their own letter spacing.** A run of the rich
  text editor carries a letter spacing of its own (`RichTextRun::
  letterSpacingPt`, in points, over the whole text's tracking): a
  `RichCharFormatDelta` sets it (`setLetterSpacing`), `GetFormatState`
  reports it, the editor draws it, and ODT (`fo:letter-spacing`) and HTML
  (CSS `letter-spacing`) keep it. The text bar (`UltraCanvasTextFormatBar`)
  treats letter spacing as a character attribute, like the font and the
  size: `TextFormatCharDelta` and `TextFormatAtCaret` convert it both ways.
- **A word of a drawing's text can have its own colour.** `VectorTextStyle::
  FillColor` is a span's colour (unset: the text's fill); `VectorRenderer`
  paints the span in it, and the SVG writer and reader keep it as the
  `<tspan>`'s `fill` and `fill-opacity`.
- **A size typed into the text bar went to the whole text.** Keys typed into
  the bar's size or spacing field, or into its font list to find a font,
  reached the program's own shortcuts: Ctrl+A ran Select All, which finished
  the text being typed, so the size went to all of it instead of to the
  selected words. `UltraCanvasTextFormatBar::HasKeyboardFocus` tells a host
  when the keys are the bar's.
