- **A raster fill can replace pixels, alpha included, instead of painting
  over them.** `RasterPaint::FloodFill` and `StampMask` only composited
  source-over, so a transparent colour changed nothing and no fill could
  make pixels more transparent. Both take a trailing
  `RasterPaint::FillCompositing` now: `Blend` (the default, unchanged) or
  `Replace`, which moves each pixel toward the colour by its coverage in
  premultiplied space - a transparent colour clears the region, stored as
  (0, 0, 0, 0) like a new transparent layer. The arithmetic is public as
  `RasterReplacePixel()` beside `RasterBlendPixel()`; for an opaque colour
  the two agree. UltraPaint's Fill tool uses it for its new *Replace* mode
  (UltraPaint 0.2.12).
