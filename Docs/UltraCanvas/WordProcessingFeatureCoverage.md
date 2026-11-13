# Word-Processing Documents — Feature Coverage and Roadmap

What UltraCanvas reads from `.odt`, `.docx` and legacy `.doc` files, what the
shared document model (`UCRichDocument`) can hold, what the WYSIWYG view
(`UltraCanvasRichTextEdit`) draws, and what is still missing compared with
OpenOffice / LibreOffice Writer. The priorities at the end are based on real
documents that did not look right.

Related: [ODT-DOCX-Support-Proposal.md](ODT-DOCX-Support-Proposal.md) (the
original design), [UltraCanvasRichTextEdit.md](UltraCanvasRichTextEdit.md)
(the view and editor).

## How a document gets to the screen

```
.odt ─┐                                     ┌─ UltraCanvasRichTextEdit  (WYSIWYG view/editor)
.docx ├─ UCWordDocumentIO::Load ─ UCRichDocument ─┼─ ToMarkdown → TextArea (Texter's Markdown mode)
.doc ─┘   (one reader per format)                 ├─ ToHTML / ToPlainText
                                                  └─ SaveOdt / SaveDocx
```

Readers never talk to a UI element. Anything a reader drops cannot be shown
anywhere, and anything the model cannot hold is lost however good the reader
is. For each feature there are three questions: does the reader parse it,
can the model hold it, and does the view draw it?

The DemoApp's *OpenDocument Text* page used to serialise the document to
Markdown and show it in a TextArea. Markdown has no fonts, sizes, colours,
alignment, list start numbers or column widths, so all of those were lost
before anything was drawn. The page now hands the document straight to
`UltraCanvasRichTextEdit` in read-only mode.

## Coverage matrix

Legend: **yes** = supported end to end · *partial* = see note · no = missing.
Columns: ODT / DOCX / DOC = the reader; Model = `UCRichDocument` can hold it;
View = `UltraCanvasRichTextEdit` draws it.

### Characters

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Bold, italic, underline, strike-through | yes | yes | yes | yes | yes | Underline and strike styles (double, dotted, wavy) all become single. |
| Superscript / subscript | yes | yes | yes | yes | yes | |
| Font family, size, colour | yes | yes | yes | yes | yes | |
| Hyperlinks | yes | yes | yes | yes | yes | DOC: from `HYPERLINK` fields. |
| Hidden text | yes | *partial* | yes | — | — | Dropped on import, as Writer does when hidden text is not shown. |
| Highlight / character background | yes | yes | yes | yes | yes | 2026-09. `RichTextRun::highlightColor`, from `fo:background-color`, `w:highlight` (named colours) or `w:shd`, and `sprmCHighlight` or character shading. Saved as ODF `fo:background-color` and DOCX character shading (any colour). |
| Small caps, all caps, letter spacing | no | no | no | no | no | |
| Symbol fonts (Wingdings 1–3, Webdings, Symbol) | yes | yes | yes | yes | yes | Mapped to Unicode on import (☎ ✉ ✓ α ≥ …), and the symbol font is dropped, so they draw without the font installed. A few pictographs that few fonts contain get a common equivalent: 🕿 → ☎, 🖁 → 📱, 🖆 → ✉ (2026-09). DOCX `w:sym` and DOC `sprmCSymbol` included. |
| Spaces between differently formatted words | yes | yes | yes | yes | yes | ODT and DOCX lost a lone space between two styled runs (tinyxml2 drops whitespace-only text); fixed 2026-09. |

### Paragraphs

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Headings 1–6 | yes | yes | yes | yes | yes | DOC: built-in Heading styles and outline level. |
| Alignment (left/centre/right/justify) | yes | yes | yes | yes | yes | |
| Paragraph style → character formatting | yes | yes | yes | yes | yes | A "Standard + bold 14pt" title arrives as bold 14pt runs. DOCX: the paragraph style's character properties (bold, italic, underline, colour, size, font, and `w:docDefaults`) reach text without its own. |
| Paragraph font (empty lines) | yes | yes | yes | yes | yes | 2026-09. `RichDocBlock::paragraphFontSizePt` / `paragraphFontFamily`: the style's font, or the paragraph mark's for an empty paragraph, so blank lines are as tall as in Writer. A contract's blank lines drew at the view's 12 pt instead of 11 pt and pushed its last line onto a second page. |
| Block quote, preformatted | yes | yes | yes | yes | yes | By style name / monospace style font. |
| Line breaks, page breaks | yes | yes | yes | yes | yes | In page view a page break starts a new page (2026-09); the continuous view draws it as a dashed rule. |
| Indents (left, right, first line, hanging) | yes | yes | yes | yes | yes | 2026-09. Styles, style inheritance (DOCX `w:basedOn`, `w:docDefaults`) and direct formatting. List items keep the view's own list indentation. |
| Spacing above / below | yes | yes | yes | yes | yes | 2026-09. Stated spacing is added (after + before), as Word and Writer do; blocks that state none (Markdown) keep the view's block spacing. |
| Line spacing | yes | yes | yes | yes | yes | Proportional (`lineSpacing`), and since 2026-09 exact or at-least heights (`lineHeightPt`, `lineHeightAtLeast`): ODF `fo:line-height="14pt"` / `style:line-height-at-least`, Word `w:lineRule="exact"`/`"atLeast"`, DOC `LSPD`. |
| Tab stops (left/centre/right/decimal) and default tab interval | yes | yes | yes | yes | yes | 2026-09. Positions count from the text margin; ODT's indent-relative positions are converted (`TabsRelativeToIndent`). Bar tabs are skipped. |
| Paragraph borders and background | yes | yes | yes | yes | yes | 2026-09. `paragraphBorderTop` … `paragraphBackground` from ODF `fo:border*`/`fo:background-color`, `w:pBdr`/`w:shd`, DOC `sprmPBrc*`/`sprmPShd*`. Consecutive paragraphs with the same frame form one box. In a table cell the paragraph's frame fills in the sides the cell has none on (a letterhead's rule under the sender line). An empty paragraph with only a bottom border is still read as a horizontal rule. Padding is fixed at 3 pt. |
| Drop caps, keep-with-next, widows/orphans | no | no | no | no | n/a | Pagination features; they only matter once the view paginates. |

### Lists

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Bullet and numbered lists, nesting | yes | yes | yes | yes | yes | |
| Numbering that continues across interruptions | yes | yes | yes | yes | yes | `listStartNumber` + `RichListNumbering` (2026-09). |
| Start values, restarts | yes | yes | yes | yes | yes | ODT `text:start-value`, DOCX `w:start`/`w:startOverride`, DOC `iStartAt`/LFO overrides. |
| Number formats (1, 01, a, A, i, I, none), prefix/suffix | yes | yes | yes | yes | yes | 2026-09. `RichDocBlock::numberFormat` + `numberTemplate` in Word's `%1.%2)` notation; ODT `num-format`/`num-prefix`/`num-suffix`, DOCX `w:numFmt`/`w:lvlText`, DOC `nfc` + number text. Markdown output stays decimal. |
| Multi-level numbers ("1.2.3") | yes | yes | yes | yes | yes | 2026-09. ODT `text:display-levels`; Word templates directly. `RichDocListLabel()` builds the label; the view lines a level's text up behind its widest label. |
| Custom bullet characters | yes | yes | yes | yes | yes | 2026-09. `bulletText`; bullets in a symbol font (Symbol U+F0B7, Wingdings "§") are mapped to Unicode. Picture bullets are not read. |
| Numbered headings (outline numbering) | no | no | no | no | no | |

### Tables

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Rows, cells, header rows | yes | yes | yes | yes | yes | |
| Column and row spans | yes | yes | *partial* | yes | yes | DOC: merged cells (`fVertMerge`/`fHorzMerge`) are not read yet. |
| Column widths (relative) | yes | yes | yes | yes | yes | Scaled to the view width (2026-09). |
| Cell text alignment | yes | yes | yes | yes | yes | From the cell's first paragraph (2026-09). |
| Cell borders (width, colour per side) and background | yes | yes | yes | yes | yes | 2026-09. ODT cell styles (and a column's default cell style); DOCX table style → `w:tblBorders` → `w:tcBorders`, `w:shd`; DOC `TC80` borders, table borders, `sprmTSetBrc`, cell shading. A borderless document table draws no lines (editable view: faint guides only). Line styles (double, dotted…) draw solid. Tables from Markdown or the editor keep the view's grid. |
| Cell vertical alignment, padding | yes | yes | yes | yes | yes | 2026-09. `RichTableCell::verticalAlign`, `padding*Pt`: ODF `style:vertical-align`, `fo:padding*`; Word `w:vAlign`, `w:tblCellMar`/`w:tcMar` (default 5.4 pt at the sides); DOC cell flags, `sprmTVertAlign`, `sprmTCellPadding(Default)`, half the cell gap. |
| Table width and position | yes | yes | yes | yes | yes | 2026-09. `tableWidthPt` / `tableWidthPercent`, `tableAlign`, `tableIndentPt`: ODF `style:width`/`style:rel-width`, `table:align`, `fo:margin-left`; Word `w:tblW` (else the grid's total), `w:jc`, `w:tblInd`; DOC cell edges and `sprmTJc`. Never wider than the view's column. |
| Several paragraphs in a cell | *partial* | *partial* | *partial* | *partial* | yes | Kept as line breaks; lists and headings inside a cell lose their structure. |
| Nested tables | *partial* | no | *partial* | no | no | Flattened into the outer cell's text. |

### Pictures, frames, drawings

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Inline pictures (PNG, JPEG, GIF) | yes | yes | yes | yes | yes | DOC: PNG/JPEG BLIPs; EMF/WMF/DIB not decoded. DOCX: VML pictures (`v:imagedata`, Word 2003 and older) in the line or floating. |
| Picture on its own line | yes | yes | yes | yes | yes | |
| Floating pictures | yes | yes | yes | yes | yes | 2026-11. A floating run (`RichTextRun::imageWrap`, `imageFloatAlign`, offsets) placed from the paragraph, the page's text area or the page (`floatHorizontalFrom`, `floatVerticalFrom`, `floatVerticalAlign`): ODF `style:horizontal-rel` / `vertical-rel` and page-anchored frames, DOCX `wp:positionH` / `wp:positionV` `relativeFrom`, DOC OfficeArt shapes (`PlcSpaMom` anchors, `posrelh` / `posrelv`, BStore pictures). Page view puts them at their place on the page; text wraps beside a square-wrapped one, above and below a top-and-bottom one. Both writers save the placement. |
| Text boxes and frames | yes | yes | yes | yes | yes | 2026-11. A text frame (`RichTextRun::frameIndex`, `UCRichDocument::frames`): its paragraphs, fill, border and padding, floating like a picture. ODF `draw:text-box` (Writer frames and drawing text boxes), DOCX `wps:txbx` and VML `v:textbox`, DOC text boxes (the text-box story, `PlcftxbxTxt`). A letterhead's sender block stands beside the address. Both writers save them; LibreOffice reads the saved files back with the frames in place. Markdown, HTML and plain text spell a frame's text before its paragraph. Frames and floating pictures in headers and footers float on every page, from that page, and the body wraps round them (DOC: `PlcSpaHdr` and the header text-box story) - a Word letterhead's logo and sender block. Frames in table cells are not drawn; see the gaps below. |
| Shapes, lines, drawings (`draw:*`, DrawingML, OfficeArt) | yes | yes | yes | yes | yes | 2026-11. A drawing is a frame with shapes (`RichTextFrame::shapes`, `RichShape`): lines, rectangles, rounded rectangles, ellipses, outlines (straight and curved), groups, pictures in groups; fill, line colour and width, dashes, arrowheads, a turn and mirroring; words in a shape, centred or at the foot. ODF `draw:rect` / `ellipse` / `line` / `path` / `polygon` / `polyline` / `custom-shape` / `g`; DOCX `wps:wsp` (preset and custom outlines; theme colours through `wps:style` and `a:schemeClr` with their tint, shade and luminance changes), `wpg:wgp` groups with pictures and text boxes, and VML (`v:line`, `v:rect`, `v:roundrect`, `v:oval`, `v:shape` paths and types, `v:group`); DOC OfficeArt shapes and groups (shape types, freeform vertices, `FSPGR` / `FChildAnchor`). About forty of Word's named outlines (stars, arrows, flowchart symbols); others draw as their box. Both writers save drawings, and LibreOffice reads them back in place. Gradient fills (linear, axial, radial with their centre: ODF `draw:gradient`, DOCX `a:gradFill`, VML `v:fill`, DOC shades) are drawn and saved; pattern fills draw in one colour; no shadows, 3-D or WordArt. In page view a floating frame, drawing or picture is selected, moved by dragging and resized; the new place and size are saved. |
| Charts, embedded spreadsheets (OLE) | no | no | no | no | no | |
| Formulas | yes | yes | no | yes | yes | ODT MathML / DOCX OMML → LaTeX. |

### Page and document structure

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Page size, margins, orientation | yes | yes | yes | yes | yes | 2026-09. `UCRichDocument::page` (`RichPageSetup`): size, margins and header/footer distances, from the ODF page layout, DOCX `w:sectPr` and the DOC section (SEP). Page view draws the pages and uses the page's text column. Both writers save it. Only the first section's page is read. |
| Headers and footers | yes | yes | yes | yes | yes | 2026-09. `pageFurniture` and `firstPageFurniture` (another first page: ODF `style:header-first` or a first master page with a next style, DOCX `w:titlePg`, DOC first-page stories). Drawn on every page in page view, and above and below the body otherwise. Both writers save them. Even/odd headers are not read. |
| Page number and page count fields | yes | yes | yes | yes | yes | 2026-09. `RichTextRun::field`: ODF `text:page-number` / `text:page-count`, DOCX `PAGE` / `NUMPAGES` / `SECTIONPAGES` (complex and simple fields), DOC field codes. Page view fills in each page's numbers in headers and footers. |
| Dates and other fields | *partial* | *partial* | *partial* | no | — | Their last displayed value is kept as text. Captions and cross-references are real fields (see below). |
| Footnotes / endnotes | yes | yes | no | yes | yes | `UCRichDocument::notes` with numbered reference runs; footnotes at the foot of their page in page view, endnotes after the body. DOC: the note text is not read yet. |
| Table of contents and indexes | yes | yes | *partial* | yes | yes | Contents entries are `RichDocBlock::tocLevel` paragraphs whose page numbers are `PageReference` fields; `UpdateTableOfContents` rebuilds them. ODT: the generated text (`text:index-body`) of every index is read; only a table of contents gets `tocLevel`. DOCX: "TOC 1-9" paragraphs, also inside `w:sdt`. DOC shows the field result. Both writers save a table of contents. Alphabetical and other indexes are plain text. |
| Sections, multiple columns | yes | yes | no | yes | *partial* | `RichSectionSetup` (columns, gap, new page) in `UCRichDocument::firstSection` and `RichDocBlock::sectionStart` / `section`; ODF `text:section` with `style:columns`, DOCX `w:sectPr` with `w:cols` / `w:type`. Page view flows a section column by column; the continuous view draws one column. Unequal column widths become equal, and a section's own page size is not read. Hidden ODT sections are dropped. Both writers save them. |
| Comments, tracked changes | yes | yes | no | yes | yes | Comments: `UCRichDocument::comments` (`RichComment`), anchored by `RichTextRun::commentIds`; ODF `office:annotation`, DOCX `comments.xml`. Tracked changes: `RichTextRun::change` / `revision` into `UCRichDocument::revisions`; ODF `text:tracked-changes`, DOCX `w:ins` / `w:del` / `w:moveTo` / `w:moveFrom`. The view shades commented text with a comment pane beside it, underlines insertions and strikes through deletions. Both writers save them. Format changes are not kept. DOC does not read comments or revision marks. |
| Bookmarks, cross references | yes | yes | no | yes | yes | `RichDocBlock::bookmarks` (paragraph-level; where a bookmark ends is not kept) and `RichTextRun::Field` `Sequence` / `Reference` / `PageReference` with `fieldArgument`; ODF `text:bookmark`, `text:sequence`, `text:bookmark-ref`; DOCX `w:bookmarkStart`, `SEQ` / `REF` / `PAGEREF`. `UpdateFields` renumbers captions and references; page view fills in page references. Both writers save them. DOC: only the field's result text. |
| Document metadata | yes | yes | no | yes | — | |

## Checked against real documents

- **A two-page agreement saved by Word as `.doc`**: a letter with a bold title,
  a bullet list, a numbered list interrupted by empty paragraphs, and a
  two-column table with centred dates and right-aligned amounts. Before
  2026-09 it opened as unstyled lines with the table flattened to tabs. It now
  imports with the same structure as LibreOffice's own ODT conversion of it:
  the title, the bullets, numbers 1–5 running on past the gaps, and the table
  with its header row, alignment and widths.
  In page view (2026-09) it fills one A4 page with the original's margins,
  its table spans the page's text column, and it ends on page one, as in
  Writer.
- **A business letter on a letterhead, as `.odt`**: everything is imported (logo, sender
  block, contact lines, the address table, the date, the body, the signature
  image and the bank footer), but the layout is not. The sender block and
  address are frames placed on the page, which cannot be represented yet, so
  they appear one after another in reading order. The footer's three
  tab-aligned columns line up as in the original, the Webdings telephone, fax,
  mobile and e-mail icons draw as ☎ 🖨 📱 ✉, and the borderless address and
  date tables no longer show grid lines. In page view (2026-09) the bank
  footer sits at the foot of page one only, and the following pages carry
  the "Seite N / M" header with their own numbers. Because the sender block
  still flowed in the text instead of standing beside the address, the
  signature's last two lines moved to a second page; Writer fits the letter
  on one. Positioned frames (2026-11) put a page-anchored sender block
  beside the address; that was checked on `Tests/fixtures/word-frames`, a
  page of the same shape, against LibreOffice's rendering - the original
  letter itself has not been re-checked.

## Roadmap, in priority order

Ordered by how many ordinary documents each item fixes, not by how hard it is.

Done in 2026-09: paragraph geometry (indents, spacing, line spacing and
fixed line heights), tab stops with the default interval, symbol-font
mapping, table cell borders and backgrounds, list number formats,
multi-level labels and document bullets, highlight, paragraph borders and
backgrounds, table width, position, cell padding and vertical alignment,
and the page model: page size, margins, headers, footers, page number
fields and page view. Since then: blocks split across pages (widow and
orphan control, repeated table header rows), footnotes and endnotes, zoom
and sideways scrolling, and (2026-11) positioned frames, text boxes and
floating pictures placed from the page, text wrapping round them line by
line, frames and floating pictures in headers and footers (DOC included),
zoom to the page's width or the whole page
(`UltraCanvasRichTextEdit::SetZoomFit`), and drawn shapes and groups from
all three formats.

1. **The DOC reader's remaining gaps**: merged cells, footnotes and endnotes
   (their text lies after the main text in the piece table: `ccpFtn`,
   `ccpEdn`), comments and revision marks, sections and their columns,
   bookmarks and caption / cross-reference fields. ODT and DOCX read all of
   these.
2. Charts: read-only display first.

Each can be checked against a LibreOffice-generated fixture the way
`Tests/fixtures/word97-formatting.*` is.

## What the WYSIWYG element does not support yet

Found while checking real documents against Writer. Each is something the
model or a reader can already carry, or that a document needs, but
`UltraCanvasRichTextEdit` does not draw or edit:

- **A frame's distance from the text is a fixed 9 pt**, whatever the
  document says, and text wraps round a frame's rectangle, not round a
  picture's outline (Writer's contour wrap).
- **Frames in table cells and in other frames** do not float: a cell drops
  a frame's text, a frame shows a nested frame's text in line. Frames in
  headers and footers cannot be selected or moved on the page, and are
  drawn under the body's text (Word's order; Writer draws a frame set in
  front of the text over it).
- **A frame's text cannot be edited**: a click selects the frame (one
  behind the text passes the click to the text) and it can be moved and
  resized, but its words are changed only by editing the document's source.
- **A drawing is edited as a whole**: it is selected, moved and resized
  like a picture, but its shapes cannot be picked out one by one, and its
  words cannot be edited. Frames and pictures in headers and footers cannot
  be picked up at all. Pattern fills draw in one colour; shadows, glow, 3-D
  and WordArt are not drawn; text wraps round a shape's box, not its
  outline. Word's named outlines outside
  the forty or so the model knows (`RichShape::ApplyPreset`) draw as their
  box.
- **One page setup per document.** Sections can have their own columns, but
  not their own size, margins or orientation; even/odd headers are not
  modelled.
- **Picture bullets** and **numbered headings** (chapter numbering) are not
  drawn.
- **Merged cells in DOC** are not read (the model and view support them).

## Testing approach

`Tests/fixtures/word97-formatting.fodt` is a hand-written flat ODT.
`word97-formatting.odt`, `word97-formatting.doc` and `word97-formatting.docx`
are LibreOffice's saves of it:

```bash
soffice --headless --convert-to odt word97-formatting.fodt
soffice --headless --convert-to "doc:MS Word 97" word97-formatting.fodt
soffice --headless --convert-to docx word97-formatting.fodt
```

`word-frames.fodt` (a letterhead-like page: a page-anchored sender frame
beside the address, a note box a paragraph wraps round, a floating picture)
is converted the same way to `word-frames.odt`, `.doc` and `.docx`, and so
is `word-shapes.fodt` (a rule, a box of words, an ellipse, a dashed arrow, a
triangle, a group and a turned box). `word-shapes-word.docx` is built by
`make-word-shapes-fixture.py`: shapes as Word itself writes them, which
LibreOffice's export does not produce (theme colours, a custom outline with
an arc, a group with a picture, VML-only drawings, a VML picture).
`word-gradients.fodt` (a linear, an axial and a radial fill) is converted
like the others.

`WordFormatsTest` checks that all three readers recover the same structure and
that it survives a save to `.odt` and `.docx`. To cover a new feature, add it
to the `.fodt`, regenerate both files and extend `CheckFormattingFixture`.
Never commit a real person's document as a fixture.
