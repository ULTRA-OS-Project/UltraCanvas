- **`UltraCanvasTextArea`: a subclass can style ranges of a plain line.** The
  protected virtual `GetLineSpans(lineIndex, lineText)` answers byte ranges of
  a line with a `TokenStyle` (`TextAreaLineSpan`: colour, bold, italic,
  underline), drawn over the syntax colours whenever the line is laid out -
  in PlainText mode and on the raw line being edited in Markdown mode, an
  input method's composition moving the ranges after it along.
  `InvalidateLineSpans()` lays the lines out again when the answer changes for
  unchanged text (a theme change). UltraMail draws the links of a plain-text
  mail with it.
