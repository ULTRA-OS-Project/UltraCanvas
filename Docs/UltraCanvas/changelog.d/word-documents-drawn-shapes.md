- **Word documents show their drawn shapes.** Lines, rectangles, rounded
  rectangles, ellipses, freeform outlines and groups from `.odt`, `.docx` and
  `.doc` were dropped; a letterhead's rules, a boxed heading and a diagram
  now stand where the document puts them, in page view and when printed.
  - Model: `RichShape` (kind, box, turn, mirroring, fill, line, dash,
    arrowheads, outline points, group members, words, a picture in a group)
    in `RichTextFrame::shapes`; `RichShape::ApplyPreset` knows about forty of
    Word's named outlines; `RichTextFrame::textAnchor` centres a box's words.
  - ODT: `draw:rect`, `draw:ellipse`, `draw:line`, `draw:path`,
    `draw:polygon`, `draw:polyline`, `draw:custom-shape` and `draw:g` are read
    and written.
  - DOCX: `wps:wsp` shapes with preset or custom outlines, theme colours
    (`wps:style`, `a:schemeClr` with tint, shade and luminance), `wpg:wgp`
    groups with pictures and text boxes, and VML shapes and groups are read;
    drawings are written as `wps` shapes and `wpg` groups. A header's
    pictures now load (its own relationships were not read).
  - DOC: OfficeArt shapes and groups - shape types, freeform vertices,
    turns, flips, dashes and arrowheads.
  - `UltraCanvasRichTextEdit` draws them (`DrawShape`); a drawing with no
    words is as tall as it was drawn, so a rule takes no room.
  - ODT text frames are written with their height and their words' vertical
    place, so Writer no longer shrinks them; DOCX text boxes with a height
    keep it instead of shrinking to their text.
