- **A document reads on a dark theme.** A document states its colours for
  white paper; on a dark page the view drew them as they are - black text on
  the dark page, the view's light text on a pale grey table header. The view
  now fits a document's colours to a dark page
  (`RichTextEditStyle::adaptDocumentColors`, on by default;
  `AdaptDocumentColorToPage`): lightness turned over between the page and the
  ink, hue kept. Text colour and highlight, paragraph and cell fills, borders,
  frames, drawn shapes and a chart's axes follow; pictures, printing, PDF
  export and the saved document keep the document's own colours.
