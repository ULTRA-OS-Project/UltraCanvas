- **A document saved as DOCX came back with more in it than it had.** Three
  letters read from ODT, written as DOCX and read again, compared word by word
  with the rendering of the ODT:
  - **Every table gained an empty paragraph, on every save.** Word wants a
    paragraph after a table; the writer's is now a trailer a point high that
    the reader takes out again, and it carries the table's room below
    (`spaceAfterPt`, which a DOCX had no place for).
  - **A section starting a page after a page break turned two extra pages.**
    The page break and the section break were both written, with a paragraph
    of its own for the latter; the section break alone is written now, on the
    table's trailer or a trailer of its own.
  - **A paragraph that only anchors a picture, and an empty table cell, took
    the style's font** instead of their own (the paragraph mark's), so a
    signature's picture pushed the next line 27 pt down and a heading row
    stood 7 pt taller.
  - **An empty line took the font of the line after it** (DOCX and ODT): a
    small blank line before a large name grew with the name. It keeps the
    font of the break that began it.
  - **Lines without the font's line gap** (`UCRichDocument::fontLineGap`
    false) are written as Word's `w:noLeading` compatibility setting and read
    back.
  After this the three letters render the same from the DOCX as from the ODT
  (no word more than 0.1 pt away, fewer than 0.15% of pixels differing), and
  the same again after a second save. `DocumentRenderProbe` takes a third
  argument, a file to write the loaded document to.
