- **A core built with `ULTRACANVAS_ENABLE_CHARTS=OFF` links again.** The
  option dropped every source under `Plugins/Charts/`, but code outside it is
  built on four of them: every diagram derives from
  `UltraCanvasChartElementBase` (the Matrix and Packet diagrams draw
  `UltraCanvasChartLegend` too), and the progress dialog - which
  `UltraCanvasFileLoader` reaches - shows an
  `UltraCanvasCircularProgressChart`; those use `UltraCanvasColormap`. The
  library compiled, and then a program using a diagram or the file loader
  failed to link with undefined chart symbols. With charts off the core now
  keeps those four files and drops the rest.
