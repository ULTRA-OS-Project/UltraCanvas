- **A document's lines broke elsewhere than in Word and Writer, and stood
  closer together.** Measured on a letter against LibreOffice's rendering of
  the same file:
  - **Glyphs on whole pixels made words about 1% narrower** than their font
    says, so a paragraph of four lines came out in three.
    `IRenderContext::CreateDocumentTextLayout` gives a layout with the glyphs
    where the font puts them, and `UltraCanvasRichTextEdit` lays its text out
    with it; interface text is unchanged.
  - **The font's line gap was left out**: Arial's lines were 3% closer than a
    word processor sets them. Lines are now the font's own height apart
    (`UCRichDocument::fontLineGap`; false for a Writer file whose settings
    say `AddExternalLeading` false, kept in the ODT's `settings.xml`).
  - **Paragraph spacing inside a table cell was ignored** (ODT, DOCX, DOC):
    a cell keeps its paragraphs as lines, and their space before and after
    was dropped. The room between two paragraphs is now an empty line of
    exactly that height (`RichTextRun::emptyLineHeightPt`), and above the
    first and below the last it is part of the cell's padding. The ODT and
    DOCX writers write a cell's lines as paragraphs with that spacing, so it
    survives a save. Not counted where a Writer file says so
    (`AddParaSpacingToTableCells`, `UCRichDocument::paragraphSpacingInCells`).
  - **Letter kerning follows the document** (`UCRichDocument::letterKerning`):
    off for Word's files unless `w:kern` turns it on (ODF
    `style:letter-kerning`), through the new
    `TextAttributeFactory::CreateFontFeatures`.
  - An empty line in a cell is as tall as a line of its paragraph's font.
