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
| Highlight / character background | no | no | no | no | no | `fo:background-color`, `w:highlight`, `sprmCHighlight`. |
| Small caps, all caps, letter spacing | no | no | no | no | no | |
| Symbol fonts (Wingdings, Webdings, Symbol) | *partial* | *partial* | *partial* | yes | *partial* | The characters come through; without that font installed they draw as the wrong glyphs. They should be mapped to Unicode (☎ ✉ • …) on import. |

### Paragraphs

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Headings 1–6 | yes | yes | yes | yes | yes | DOC: built-in Heading styles and outline level. |
| Alignment (left/centre/right/justify) | yes | yes | yes | yes | yes | |
| Paragraph style → character formatting | yes | *partial* | yes | yes | yes | A "Standard + bold 14pt" title arrives as bold 14pt runs. |
| Block quote, preformatted | yes | yes | yes | yes | yes | By style name / monospace style font. |
| Line breaks, page breaks | yes | yes | yes | yes | *partial* | The view draws a page break as a dashed rule; it does not paginate. |
| Indents (left, right, first line) | no | no | no | no | no | Most visible remaining gap for letters and contracts. |
| Spacing above / below, line spacing | no | no | no | no | no | The view uses a fixed block spacing. |
| Tab stops (left/centre/right/decimal) | no | no | no | no | no | Tab-aligned columns (footers, signature lines, price lists) collapse. |
| Borders, padding, background | *partial* | *partial* | no | no | no | Only "empty paragraph with a bottom border" → horizontal rule. |
| Drop caps, keep-with-next, widows/orphans | no | no | no | no | n/a | Pagination features; they only matter once the view paginates. |

### Lists

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Bullet and numbered lists, nesting | yes | yes | yes | yes | yes | |
| Numbering that continues across interruptions | yes | yes | yes | yes | yes | `listStartNumber` + `RichListNumbering` (2026-09). |
| Start values, restarts | yes | yes | yes | yes | yes | ODT `text:start-value`, DOCX `w:start`/`w:startOverride`, DOC `iStartAt`/LFO overrides. |
| Number formats (a, i, A, I), prefix/suffix | no | no | no | no | no | The view always draws `N.`. |
| Multi-level numbers ("1.2.3") | no | no | no | no | no | |
| Custom bullet characters / picture bullets | no | no | no | no | no | The view uses its own • ◦ ▪. |
| Numbered headings (outline numbering) | no | no | no | no | no | |

### Tables

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Rows, cells, header rows | yes | yes | yes | yes | yes | |
| Column and row spans | yes | yes | *partial* | yes | yes | DOC: merged cells (`fVertMerge`/`fHorzMerge`) are not read yet. |
| Column widths (relative) | yes | yes | yes | yes | yes | Scaled to the view width (2026-09). |
| Cell text alignment | yes | yes | yes | yes | yes | From the cell's first paragraph (2026-09). |
| Cell borders, background, vertical alignment, padding | no | no | no | no | *partial* | The view draws every cell with a light border. Borderless layout tables look wrong because of it. |
| Table width and position | no | no | no | no | no | |
| Several paragraphs in a cell | *partial* | *partial* | *partial* | *partial* | yes | Kept as line breaks; lists and headings inside a cell lose their structure. |
| Nested tables | *partial* | no | *partial* | no | no | Flattened into the outer cell's text. |

### Pictures, frames, drawings

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Inline pictures (PNG, JPEG, GIF) | yes | yes | yes | yes | yes | DOC: PNG/JPEG BLIPs; EMF/WMF/DIB not decoded. |
| Picture on its own line | yes | yes | yes | yes | yes | |
| Floating / page-anchored pictures and frames | *partial* | *partial* | no | no | no | Written out in reading order. There is no position or text wrap. |
| Text boxes | *partial* | *partial* | no | no | no | Their paragraphs follow the anchoring paragraph. |
| Shapes, lines, drawings (`draw:*`, DrawingML, OfficeArt) | no | no | no | no | no | |
| Charts, embedded spreadsheets (OLE) | no | no | no | no | no | |
| Formulas | yes | yes | no | yes | yes | ODT MathML / DOCX OMML → LaTeX. |

### Page and document structure

| Feature | ODT | DOCX | DOC | Model | View | Note |
|---|---|---|---|---|---|---|
| Page size, margins, orientation | no | no | no | no | no | The view is one continuous column. |
| Headers and footers | *partial* | no | no | *partial* | *partial* | ODT: the first page's header and footer are emitted before and after the body. |
| Page numbers, dates and other fields | *partial* | *partial* | *partial* | — | — | Their last displayed value is kept as text. |
| Footnotes / endnotes | *partial* | no | no | no | no | ODT: placed inline in parentheses. |
| Table of contents and indexes | yes | no | *partial* | — | — | ODT: the generated text (`text:index-body`) is read (2026-09). DOCX TOCs are fields inside `w:sdt`. DOC shows the field result. |
| Sections, multiple columns | *partial* | no | no | no | no | Hidden sections are dropped; columns are flattened. |
| Comments, tracked changes | no | *partial* | no | no | no | DOCX keeps inserted text. |
| Bookmarks, cross references | no | no | no | no | no | The reference text itself comes through. |
| Document metadata | yes | yes | no | yes | — | |

## Checked against real documents

- **A two-page agreement saved by Word as `.doc`**: a letter with a bold title,
  a bullet list, a numbered list interrupted by empty paragraphs, and a
  two-column table with centred dates and right-aligned amounts. Before
  2026-09 it opened as unstyled lines with the table flattened to tabs. It now
  imports with the same structure as LibreOffice's own ODT conversion of it:
  the title, the bullets, numbers 1–5 running on past the gaps, and the table
  with its header row, alignment and widths.
- **A business letter on a letterhead, as `.odt`**: everything is imported (logo, sender
  block, contact lines, the address table, the date, the body, the signature
  image and the bank footer), but the layout is not. The sender block and
  address are frames placed on the page, and the footer's three columns are
  tab stops. Neither can be represented yet, so they appear one after another
  in reading order, and the address table shows the grid lines of a
  borderless layout table.

## Roadmap, in priority order

Ordered by how many ordinary documents each item fixes, not by how hard it is.

1. **Paragraph geometry: indents, spacing above and below, line height.**
   Model: `leftIndentPt`, `rightIndentPt`, `firstLineIndentPt`,
   `spaceBeforePt`, `spaceAfterPt`, `lineHeight` on `RichDocBlock`. All three
   readers already see these properties (`fo:margin-*`, `w:ind`/`w:spacing`,
   `sprmPDxaLeft`/`sprmPDyaBefore`); the view needs them in `BlockIndentFor`
   and in block spacing.
2. **Tab stops.** Model: a list of `{positionPt, kind}` per block. The view
   lays tab-separated segments out at the stops. This fixes letterhead
   footers, signature lines and price lists.
3. **Cell borders and backgrounds.** Stop drawing a grid by default. Read
   `fo:border*`/`w:tcBorders`/`TC80.brc*` and `fo:background-color`/`w:shd`.
   Letterheads use borderless tables for layout.
4. **Number formats and multi-level numbering.** Keep the level's format
   (`style:num-format`, `w:numFmt`, `nfc`) and prefix/suffix, and draw
   `a)`, `iv.`, `1.2.`
5. **Highlight and character background**, and **symbol-font mapping** to
   Unicode.
6. **Page model: size, margins, headers and footers** as page furniture
   rather than blocks, then pagination in the view (page boxes on a grey
   desk, like Writer's print layout).
7. **Positioned frames and floating pictures** (anchor, x/y, wrap). This
   depends on 6 and is the only way a letterhead can look like the original.
8. **Footnotes and endnotes** as real notes, **DOCX headers and footers**,
   and **DOC merged cells, headers, footers and footnotes** (the text for
   these lies after the main text in the piece table: `ccpFtn`, `ccpHdd`).
9. Comments, tracked changes, shapes, charts: read-only display first.

Items 1–4 are self-contained (a model field, three readers, the view and the
two writers each), and each can be checked against a LibreOffice-generated
fixture the way `Tests/fixtures/word97-formatting.*` is.

## Testing approach

`Tests/fixtures/word97-formatting.fodt` is a hand-written flat ODT.
`word97-formatting.odt` and `word97-formatting.doc` are LibreOffice's saves of
it:

```bash
soffice --headless --convert-to odt word97-formatting.fodt
soffice --headless --convert-to "doc:MS Word 97" word97-formatting.fodt
```

`WordFormatsTest` checks that both readers recover the same structure and
that it survives a save to `.odt` and `.docx`. To cover a new feature, add it
to the `.fodt`, regenerate both files and extend `CheckFormattingFixture`.
Never commit a real person's document as a fixture.
