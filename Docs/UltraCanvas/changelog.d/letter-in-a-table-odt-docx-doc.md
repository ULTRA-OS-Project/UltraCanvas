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
- **A borderless text frame's guide line was printed.** The thin outline an
  editable view draws round a frame without a border went into the PDF export
  and onto paper; it is the screen's only.
- **A section's own header, footer and page margins.** A document of several
  page styles - a cover without a header, pages with one - kept one header and
  footer for all of its pages, and one top margin.
  - `RichSectionSetup::furniture` names a section's own set in
    `UCRichDocument::sectionFurniture` (`RichSectionFurniture`: its pages' and
    its first page's), and `marginTopPt` / `marginBottomPt` / `headerTopPt` /
    `footerBottomPt` its room for them; `UCRichDocument::FurnitureFor` and
    `AllFurniture` find them.
  - `UltraCanvasRichTextEdit` frames each page by the section it begins in,
    and a double-click on a header edits the one that page shows.
  - ODT: a paragraph whose style names another master page starts such a
    section; the writer writes a master page and page layout per page style.
  - DOCX: each section's `w:headerReference` / `w:footerReference`, `w:titlePg`
    and `w:pgMar` (a section without a reference keeps the header of the one
    before it, as in Word); the document's own are its first section's, where
    they were its last's. The writer says every section's own.
  - A header or footer of empty paragraphs is still not kept as one, but takes
    its lines' room (ODT and DOCX): Word gives every page style one, and the
    text starts a line lower for it.
  - Not done: a .doc's later sections (its first section's header and footer
    serve the whole document), and a section's own page size or side margins.
- **ODT: the room above and below a table** (`fo:margin-top` / `-bottom`, kept
  as the table block's `spaceBeforePt` / `spaceAfterPt` and written back), an
  empty cell as tall as a line of its paragraph's font (a heading row's 8 pt,
  not the view's), and a shape anchored to the page placed from the sheet's
  edge whatever its style says it counts from (a letter's fold marks stood a
  margin too far right).
