- **A document's chart is drawn by the chart engine.** The DOCX reader
  turned a chart into drawn shapes with code of its own. It now also keeps
  the chart's data in its frame (`RichTextFrame::chart`, a `RichChart`: kind,
  categories, series, legend, title) and `UltraCanvasRichTextEdit` draws it
  with the engine's charts - with their axes, grid, legend and palette
  handling - on screen, in print and in PDF.
  `CreateRichDocumentChart` (UltraCanvasRichDocumentChart.h) gives the same
  chart as a live element.
- **New: `UltraCanvasEngineBarChart`**, the engine's column and bar chart
  (side by side, stacked, per cent; lying down; ring sectors under Polar).
  The engine had it only as a class of the demo application.
- **A chart saved to DOCX is a chart again**: the writer makes a chart part
  (`word/charts/chartN.xml`) instead of a group of shapes, so Word and Writer
  open it as a chart. A doughnut now has its hole. ODT is still given the
  chart as a drawing.
