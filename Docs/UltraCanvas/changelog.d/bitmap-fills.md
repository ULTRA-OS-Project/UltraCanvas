- **Bitmap fills: pattern fills are drawn.** `VectorRenderer` drew a
  `PatternData` fill or stroke (or a reference to a `VectorPattern`) as
  nothing. It now draws one tile of the pattern's content offscreen at the
  resolution it is painted at (at most 4096 pixels a side, and no finer than
  twice a picture's own pixels) and repeats it, through the pattern's
  `PatternRect`, units, `ViewBox` and `Transform`; tiles are cached by content
  and size (`ClearCaches` empties them). `VectorStorage` gains the helpers a
  bitmap fill is made with: `MakeBitmapFill` (a picture as a pattern holding
  one `VectorImage`, the tile given as a rectangle or as three corners -
  turned, skewed or mirrored), `BitmapFillImage`, `PatternOf`,
  `PatternTileCorners` and `SetPatternTileCorners`. A user-space fill lives in
  the element's own space, so it follows the element's `Transform` as it is
  moved, scaled or turned. `Tests/VectorBitmapFillTest.cpp` covers it.
- **SVG reads `<pattern>` fills.** The reader skipped them (the shape kept an
  unresolved `url(#...)` and drew nothing); it now reads the tile,
  `patternUnits`, `viewBox`, `patternTransform`, the content and the `href`
  chain into `PatternData`, so a bitmap fill written as `<pattern><image/>
  </pattern>` reads back as one. The writer adds `preserveAspectRatio="none"`
  to a pattern with a `viewBox`, as the model stretches it.
- **XAR writes pictures and bitmap fills, and reads them properly.** The
  writer skipped every picture and filled a bitmap fill flat black; it now
  writes `TAG_DEFINEBITMAP_JPEG` / `_PNG` (PNG with Xara's transparency byte,
  straight colour), `TAG_NODE_BITMAP` on the picture's parallelogram and
  `TAG_BITMAPFILL`. The reader turns bitmap and contone fills into bitmap fills
  on the fill's parallelogram instead of flat grey, keeps a turned or skewed
  bitmap's turn as its `Transform` instead of placing it upright, tints a
  contone bitmap, hands a PNG on with its transparency the right way round,
  and gives a picture its `data:` source so it draws (`XARBitmapToPng` in the
  XAR plugin).
- **`UltraCanvasVectorCanvas::onFilesDroppedAt`:** dropped files with the
  document point under the drop, called instead of `onFilesDropped` when set.
