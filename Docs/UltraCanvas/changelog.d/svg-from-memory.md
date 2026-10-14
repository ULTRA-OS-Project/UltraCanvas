- **An SVG that arrives as bytes renders without libvips' SVG loader too.**
  `UCImage::LoadFromMemory` now recognises SVG text (a BOM, whitespace, then
  the XML declaration, a DOCTYPE or `<svg` itself) and, where libvips has no
  `svgload` or refuses the bytes, reads it through the vector preview seam's
  new `LoadFromMemory` (`UltraCanvasVectorPreview.h`,
  `LoadVectorPreviewDocumentFromMemory`), which the Vector plugin answers
  with its converter's `ImportFromString`. The file path took that road
  since 0.9.273; a picture in a mail or a clipboard paste did not.
