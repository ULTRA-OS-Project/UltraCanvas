- **HTML reader: an inline element's horizontal margin, border and padding
  are room in its line.** `<nav><a style="margin-right:18px">About</a><a>Index</a></nav>`
  showed "AboutIndex": inline elements went into the label's Pango markup
  with no box, so their margins were dropped. Each side's margin, border
  width and padding is now a picture-less U+FFFC placeholder of that width in
  the run.
  - A word joiner (U+2060) glues the room to the element's first or last
    letter, so a line does not break between a word and its margin.
  - The room lies outside the element's link range: a click on the margin
    is not a click on the link.
  - White space collapses across it: `About <span style="margin-left:10px">
    Index</span>` has one space plus the margin.
  - Negative margins take no room. Vertical margins do not move the line,
    as in CSS. An empty element with a margin builds no line.
  - The inline element's background and border are still not drawn; only
    their width counts.
  - `UltraCanvasLabel` leaves word joiners out of copied text.
  - New `HTMLInlineMarginTest`.
