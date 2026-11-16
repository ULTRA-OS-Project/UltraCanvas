- **A .doc's later sections kept the first section's header and footer.**
  The legacy reader now starts a section of the model at the paragraph each
  later section begins with, with a header and footer of its own where its
  stories in the `PlcfHdd` are not empty (an empty story keeps the section
  before's, as in Word), its first page's when `sprmSFTitlePage` says so, its
  own top and bottom margins, and whether it starts a page (`sprmSBkc`).
- **A .doc's shapes throw their shadows**: OfficeArt's `fShadow`,
  `shadowColor`, `shadowOpacity` and the two offsets, into
  `RichShape::shadowColor` and its fields.
- **A picture text stops above and resumes below stood in the line where
  nothing floats** (a logo at the top of a sender frame, a picture in a
  cell): the words after it started beside it, a centred one stood at the
  left, and the frame's text began a line too high. Such a picture now has a
  line of its own, at the side or in the middle it is aligned to, and a
  paragraph of nothing else keeps its empty line below it, as Writer sets it.
  A centred picture text may pass is set the same way, as when it floats.
  `Tests/fixtures/word-sections.doc` (from `word-sections.fodt`, saved as Word
  97 by LibreOffice) holds the sections and a shadowed rectangle.
