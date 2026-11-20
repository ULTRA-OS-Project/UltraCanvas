- **Drawings rasterized from a file have their size.** `InspectVectorFile`
  reported 0 x 0 for every format the Vector plugin reads (EMF, WMF, DXF,
  DWG, ODG and the rest): the plugin's `GetFileInfo` gave no size. So
  `RasterizeVectorFile` made a square canvas, and the plugin's element
  drew the drawing into it unscaled. This hit UltraPaint's import of those
  formats.
  - `RasterizeVectorFile` reads such a format once into its document and
    draws it with `RasterizeVectorDocument`. The page is the natural size,
    and the drawing is scaled to the pixels asked for.
  - `InspectVectorFile` takes the page as the natural size when a plugin
    reports none, and the Vector plugin's `GetFileInfo` reports it (in
    points).
  - New `VectorFileSizeTest`: a drawing written in each format the plugin
    writes and reads, rasterized from the file - natural size, shape and
    scale (all 24 per-format checks fail on the old code; DWG runs where
    LibreDWG's dxf2dwg is installed).
