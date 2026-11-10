- **Histogram chart.** `UltraCanvasHistogramChart` (chart engine) shows a
  picture's histogram: how many pixels have each of the 256 levels, for red,
  green and blue overlaid (`HistogramDisplay::Combined`) or for one channel -
  red, green, blue or luminosity - on a linear or logarithmic scale, with the
  level under the pointer read out and, in Combined, the legend switching a
  channel off. `SetImage` counts a decoded picture; the UI-free
  `UltraCanvasHistogramModel.h` (core library) counts any pixel buffer (premultiplied ARGB32,
  XRGB32, RGBA8, RGB8) and gives each channel's mean, median, standard
  deviation, range, clipping and percentiles. Demo: **Charts > Histogram
  Chart**. Tests: `HistogramModelTest`, `HistogramChartRenderTest`. Guide:
  `UltraCanvasHistogramChart.md`.
