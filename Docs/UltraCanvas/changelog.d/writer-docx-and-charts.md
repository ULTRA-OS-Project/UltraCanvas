- **A DOCX saved by Writer had a horizontal rule after almost every
  paragraph and a bullet before every heading.** Writer writes
  `w:pBdr` sides of "nil" on its paragraphs, and an empty paragraph with a
  bottom border was taken for a rule whatever the border said; its headings
  are in an outline numbering whose levels show nothing (`w:numFmt` "none",
  an empty `w:lvlText`), which made list items of them. Only a border that is
  drawn makes a rule now, and such a level makes no list item.
- **A font named with its alternatives** ("Liberation Sans;Arial", as Writer
  writes `w:rFonts`) is the first of them; the whole string named no font, so
  the text fell back to the default one.
- **Charts in a DOCX are drawn** (they were left out): a DrawingML chart is
  read from its part as a drawing of shapes - columns for a bar or column
  chart, lines for a line or area chart - with the value axis, the grid, the
  categories and the legend, from the values cached in the file. A .doc's
  chart was already shown from the metafile stored with it.
- **A heading is set in its style's own size and font** where the document
  gives them (it took the view's heading sizes and font).
- **A picture or frame that does not fit the rest of its page goes to the
  next page with its paragraph**; it was cut by the page's edge.
- **The document's first paragraph keeps its space above** (a title's 12 pt),
  and **a hanging first line starts left of the column** where the
  paragraph's indent is smaller than the hang (Writer's headings).
- `DocumentRenderProbe` registers the vector readers, so a document's EMF and
  WMF pictures are drawn in its output.
- **A heading keeps its own font, size and spacing through ODT**: the ODT
  writer wrote the plain heading style and the reader took no font from it.
