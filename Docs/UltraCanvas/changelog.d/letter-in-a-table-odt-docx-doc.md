- **A letter laid out in a table read wrong from ODT, DOCX and DOC.** A Word
  letter template (a banner across the sheet, the address and text in a table
  whose tall rows hold the logo at the foot of the page, a banner turned on its
  side) came out with its text run together, the logo halfway up the page and
  the banners shrunk into the column. Compared page by page with the PDF its
  own program exports:
  - **Empty paragraphs in a table cell are empty lines** (ODT, DOCX, DOC). A
    cell keeps runs, not paragraphs, and an empty paragraph left nothing
    behind: the blank lines between address, date and text were gone. Two line
    breaks in a row are an empty line too (they were one break).
  - **A table row's height** (`RichTableRow::heightPt`, `heightExact`): read
    from ODF's `style:min-row-height` / `style:row-height`, Word's
    `w:trHeight` and a .doc's `sprmTDyaRowHeight`, written back by the ODT and
    DOCX writers, and laid out by `UltraCanvasRichTextEdit`.
  - **ODT: the default paragraph style** (`style:default-style`) is what a
    paragraph style based on nothing starts from - its font, size and
    spacing. Word's ODT export names the font there and nowhere else, so the
    whole text fell to the view's font, a quarter wider. Its colour is not
    taken: the view's ink serves, and follows a dark theme.
  - **Floating pictures and drawings** keep a negative offset (above their
    paragraph, left of the text area - all three readers clamped it to zero),
    keep their width when placed from the page, and are cut at the edges of
    their page instead of showing on the one before.
  - **ODT: a shape's `draw:transform`** is applied in the order it is listed
    ("translate rotate translate" turns about the centre; the translations
    were summed), and a group's box is its members' bounds as they are turned.
  - **DOC**: the page size and margins of a document of several sections
    (the section table was read as if it had one, so the page fell back to
    US Letter); merged cells, from the rows' cell edges and the merge flags
    (a row of fewer, wider cells lost the column widths for the whole
    table); a run's character style (`sprmCIstd`) and letter spacing
    (`sprmCDxaSpace`); a floating picture alone in its paragraph stays
    floating (it became a picture paragraph at the margin).
  - `FontFamilySubstitute` knows the Liberation fonts, Carlito and Caladea
    the other way round too (a Writer document on Windows).
  - `Tests/DocumentRenderProbe.cpp`: loads a document and writes what the
    rich text edit draws of it as a PDF, with the reader's page, floats and
    table grids listed - to put beside the document's own PDF export.
  - Not done: a header or footer per section (the model has the document's
    and the first page's). A section whose header is one empty line starts
    its text that line lower in Word.
- **A borderless text frame's guide line was printed.** The thin outline an
  editable view draws round a frame without a border went into the PDF export
  and onto paper; it is the screen's only.
