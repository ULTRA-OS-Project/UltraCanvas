- **One histogram count for every Curves dialog.** `UltraCanvasCurveEditor` and
  `UltraCanvasCurvesDialog` take `SetHistograms(const ImageHistogram&)`: Red,
  Green and Blue from their channels, RGB from the three added level by level
  (`ImageHistogram::RgbTotal`), transparent pixels left out. The image viewer
  (`UltraCanvasMediaViewer`) now counts its Curves histogram with
  `UltraCanvasHistogramModel` instead of `HistFind` - still in sRGB at 8 bits a
  channel, a strip of rows at a time - and no longer counts fully transparent
  pixels. `UltraCanvasHistogramModel` is in the core library (`core/`), so a
  build without the charts has it; `ImageHistogram::Add` sums strips. Test:
  `CurveHistogramTest`. Guide: `UltraCanvasCurveEditor.md`.
