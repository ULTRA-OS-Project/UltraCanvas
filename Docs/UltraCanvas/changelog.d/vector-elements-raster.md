- **`RasterizeVectorElements`: chosen elements of an open drawing as a
  picture.** `UltraCanvasVectorRaster.h` could rasterize a vector *file*;
  a drawing program had no way to turn its own selection into pixels, so its
  Copy had nothing to offer a paint program, a word processor or a chat. The
  new call draws the elements where they sit in the document (their
  ancestors' transforms applied, definitions resolved) at a given number of
  pixels per point, over transparency, and crops the layer to what they
  paint, so a thick stroke or a shadow is kept and empty margin is not.
  ArtCreator's Copy is its first user. `Tests/VectorRasterTest.cpp` covers
  it, and the read-back it shares with the plugin rasterizer is one function
  now instead of two copies.
