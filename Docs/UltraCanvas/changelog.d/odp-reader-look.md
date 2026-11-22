- **The `.odp` reader reads the look as drawn, so UltraViewer, MediaViewer's
  slide view and UltraFiler's thumbnails draw an Impress deck as fully as a
  PowerPoint one.** It used to read text with bold, italic and underline
  only; now every style is resolved through its parents and the family
  defaults (common styles, the content's automatic styles, the master pages'
  own):
  - shapes: their fill (solid, or a gradient's stops - linear, axial, radial;
    LibreOffice's bare tenths of a degree too), outline (colour, width,
    dashes and dots, arrowheads from markers), text area (anchor, padding,
    wrap, vertical writing, shrink to fit), turn (`draw:transform`) and
    mirroring; their outline - an Office preset LibreOffice names
    `ooxml-...`, ODF's own shape types (rectangles, ellipses, diamonds,
    triangles, arrows, stars, flowchart shapes ...), an enhanced path in
    plain numbers, `draw:rect` / `ellipse` / `polygon` / `polyline` / `path`
    / `regular-polygon`, lines and connectors (a standard connector routed
    with elbows);
  - text: each run's size (points or a share of the size under it), colour,
    typeface (through the font-face declarations), strike, super- and
    subscript, capitals, highlight and links (`#name` of a page as a jump to
    its slide); each paragraph's alignment, spacing, line height and
    margins; list labels - the bullet character (a symbol font's private-use
    code), its font, colour and size, the numbering scheme and the number
    each item shows (start values, continued lists), where the label hangs;
    an outline's levels from their own presentation styles;
  - tables: column widths, row heights, each cell's fill, borders, padding,
    anchor and span, a table template applied;
  - each slide's background (its page style, else its master page's), the
    theme's colours (LibreOffice's `loext:theme`), and with the new
    `withPictures` argument of `ReadOdpPresentation` /
    `ReadOdpPresentationFromMemory` the pictures' bytes (inline ones of a
    flat `.fodp` too).
  - Tested on LibreOffice's own conversion of the PowerPoint feature deck
    (`Tests/fixtures/pptx-features.odp`), read field by field against the
    `.pptx` reader and drawn the same, and on a flat deck written in the test.
- **The `.odp` writer turns a turned shape's gradient with it, and starts a
  radial gradient at the edge.** ODF's gradient angle is on the page, so a
  turned shape's fill came out turned against the shape in LibreOffice; and a
  radial gradient's colours were the wrong way round.
