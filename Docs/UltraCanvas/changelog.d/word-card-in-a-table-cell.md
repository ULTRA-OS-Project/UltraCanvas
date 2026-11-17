- **A Word card laid out in a table (a name badge) came out on two pages,
  without its photo, in the wrong font and in lower case.** Six gaps, each
  general:
  - **Theme fonts.** `w:rFonts w:asciiTheme="minorHAnsi"` / `majorHAnsi` (and
    `hAnsiTheme`) name the theme's body and heading fonts
    (`word/theme/theme1.xml`, `a:minorFont` / `a:majorFont`); the DOCX reader
    read only `w:ascii`, so every such document was set in the default font.
  - **Capitals.** `w:caps` on a run, or on its paragraph's style (a heading
    style in capitals), now writes the words in capitals (Latin, Greek and
    Cyrillic letters).
  - **A picture or drawing behind or in front of the text, anchored in a
    table cell, floats from that cell** (`UltraCanvasRichTextEdit`): it stood
    in the cell's line at full size and pushed the row over the page. It is
    placed across from the left edge of the cell's text and down from its
    anchor's line, and may cover the whole table.
  - **A turned picture** (`RichTextRun::imageRotationDeg`, new): Word's
    `a:xfrm rot` and ODF's `draw:transform` on a picture are read and written;
    the picture is drawn turned about its centre.
  - **A frame or drawing in a table cell stands in the cell's line** at its
    own size (with room for its outline) and is drawn there; it took no room
    and was not drawn at all. The ODT reader reads drawn shapes inside cells,
    and the ODT writer anchors such a drawing as a character so Writer sets
    it in the line too.
  - **Word's "rectangle: top corners rounded"** (`round2SameRect`, with both
    of its adjustments) is read as its outline - an arch when the rounding is
    a half - where it was a plain rectangle.
- **A picture, drawn shape or formula standing in a line lay over the lines
  round it** in documents whose lines keep their fonts' line gap
  (`UCRichDocument::fontLineGap`, the default): Pango forgets a box's height
  when lines are spaced by their fonts' heights. A paragraph with such a box
  is now set without that spacing.
