- **The media viewer's Details panel uses smaller text, so metadata fits a
  preview pane.** It was 12 pt with document-sized headings (1.5x and 1.3x)
  and two blank lines before each, so in UltraFiler's preview pane the
  "Image information" table alone filled the height and the metadata began
  below the fold. The panel now uses 11 pt, or 10 pt when it is narrower than
  360 px, headings only a step above the text (1.2x / 1.08x), and one blank
  line between blocks. `UltraCanvasMediaViewer::SetDetailsFontSize()` sets a
  size of one's own (0 = automatic).
