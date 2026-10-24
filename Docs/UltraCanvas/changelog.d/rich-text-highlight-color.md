- **The rich text editor sets a background colour behind text.**
  `UltraCanvasRichTextEdit::SetHighlightColor("#RRGGBB")` (and
  `UCRichDocumentEditor::SetHighlightColor`) colours the background of the
  selection, or of what is typed next, as a highlighter does; an empty
  colour removes it. Runs always had `highlightColor`, drawn on screen and
  written to HTML as `background-color`, but nothing could set it while
  editing. `RichCharFormatDelta::setHighlight` / `highlightColor` carry it,
  `RichCharFormatState::highlightColor` / `highlightMixed` report it for a
  toolbar, and `ClearFormatting` takes it away with the rest. UltraMail's
  compose window uses it for its Text | Background colour switch.
