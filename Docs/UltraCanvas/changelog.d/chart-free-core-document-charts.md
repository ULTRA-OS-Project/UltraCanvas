- **A core built without charts links again.** With
  `ULTRACANVAS_ENABLE_CHARTS=OFF` the bridge that draws a word-processing
  document's chart with the chart engine (`UltraCanvasRichDocumentChart.cpp`)
  still called the engine's charts, which that core leaves out, so no program
  could link it (the Image codecs check on `main` failed this way). That core
  now builds `UltraCanvasRichDocumentChartNone.cpp` in its place, which makes
  no chart, and the rich text view draws the chart's shapes instead, as it
  does for a chart the engine cannot draw.
