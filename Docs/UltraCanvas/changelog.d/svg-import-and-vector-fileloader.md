- **Imported SVG drawings keep their shapes, placement and text.** Opening
  an SVG in ArtCreator (or previewing one through the vector reader) lost
  most of an optimised file and misplaced the rest. Five defects in the
  shared vector code, each visible in the sample files under
  `media/vector/SVG/`:
  - `VectorStorage::ParsePathString` read path data with `istream >>`, so
    it broke on the compact form every optimiser and editor writes: numbers
    run together (`423.38-18.759`, `.95-.16.857`), repeated coordinates
    after one command letter, and arc flags without separators
    (`a1 1 0 01 5 5`). Such paths became runs of empty commands;
    `robot.svg` showed a few fragments of 632 shapes. The reader now follows
    the SVG path grammar (dot-decimal, locale-proof).
  - `VectorStorage::ParseTransformString` "skipped the comma" by reading one
    character after each number, which with a space separator ate the first
    digit of the next: `translate(483.572 574.049)` became `(483.572,
    74.049)` and `scale(1 -1)` lost its sign. `rotate(a cx cy)` now turns
    about its centre.
  - The SVG importer left inherited paint unset — a shape without its own
    `fill` (black by default in SVG), or inside `<g fill="…">`, drew
    nothing, because the renderer has no style inheritance. The importer
    now writes the inherited fill and stroke into each element; an explicit
    `none` is kept.
  - A transform on a top-level `<g>` (which becomes a layer) and the
    viewBox's offset and scale were ignored; the importer now places them in
    a group inside each layer, so the drawing lands on the page
    (`photo-camera.svg`, Xara's `Logo_Texter.svg`).
  - `VectorRenderer` culled elements by comparing bounds in their parent's
    space against the viewport in document space, dropping whole subtrees
    of transformed groups; it now culls through the accumulated transform
    (also for `<use>`). Text was drawn with its top-left, not its baseline,
    at the text position, and at 4/3 of its size (the context's font size is
    in points at 96 dpi, the model's in drawing units).
  - `Tests/SVGConverterTest` pins each case.
- **`UltraCanvasFileLoader` loads and saves editable vector documents.**
  `LoadVectorDocument(path, error, notes)`, `SaveVectorDocument(doc, path,
  error, notes)`, `GetVectorLoadExtensions()`, `GetVectorSaveExtensions()`
  and `CanLoadVectorDocument()` read into and write from the shared
  `VectorStorage::VectorDocument` with the Vector plugin's converters. They go
  through the existing `VectorPreviewProvider` seam, which gains optional
  `Import` (with the reader's notes), `SaveExtensions` and `Save` members
  that `RegisterVectorFormatsPlugin()` fills in; core still links no reader.
  `UCImage` keeps opening `.svg` as pixels through librsvg.
