- **Pattern and picture fills are drawn as they are**, not in one colour.
  `RichShape::fillPattern` holds Word's preset patterns (`"pct50"`,
  `"dnDiag"`, `"smCheck"`, ...) or an 8 x 8 tile of their own, drawn in the
  fill colour on `patternBackColor`; `fillMediaIndex` fills a shape with a
  picture, tiled or stretched.
  - Read from DOCX `a:pattFill` and `a:blipFill`, ODF `draw:hatch` and
    `draw:fill-image` (inline pictures too), and DOC pattern bitmaps,
    textures and picture fills.
  - Written to DOCX by name; to ODT as a hatch, or for dots and checks as
    the pattern's tile.
- **Frames, drawings and pictures in a header or footer can be picked up
  while it is being edited**: they are drawn and placed as its own (they
  used to vanish while it was open), selected by a click (which no longer
  leaves the header), moved by dragging and resized, and saved in place.
