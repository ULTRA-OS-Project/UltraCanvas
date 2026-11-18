- **Charts in ODT are chart objects.** The ODT writer wrote a chart as a
  group of drawn shapes, and the reader showed Writer's own charts as their
  replacement picture. A chart is now written as an object of its own in the
  package (`Object N/content.xml`: a `chart:chart` with its series, styles
  and the table their ranges point into) and read from one into
  `RichTextFrame::chart` - columns and bars (side by side, stacked, per
  cent), lines, areas, pies, doughnuts and scatter charts, with their
  series' names and colours, categories, legend, title and stated axis
  ranges. A chart stays a chart from DOCX to ODT and back, and Writer opens
  ours as a chart.
- `UCRichDocument::ChartAsShapes` makes a chart's drawn shapes from its data
  (the code the DOCX reader had, now shared by both readers), and
  `RichChart` carries a chart's stated axis ranges (`valueMin` / `valueMax`,
  `xMin` / `xMax`).
