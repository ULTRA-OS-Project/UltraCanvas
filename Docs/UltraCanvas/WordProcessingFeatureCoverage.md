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
| Paragraph style → character formatting | yes | *partial* | yes | yes | yes | A "Standard + bold 14pt" title arrives as bold 14pt runs. |
| Block quote, preformatted | yes | yes | yes | yes | yes | By style name / monospace style font. |
| Line breaks, page breaks | yes | yes | yes | yes | *partial* | The view draws a page break as a dashed rule; it does not paginate. |
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
| Cell vertical alignment, padding | no | no | no | no | no | |
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
  address are frames placed on the page, which cannot be represented yet, so
  they appear one after another in reading order. The footer's three
  tab-aligned columns line up as in the original, the Webdings telephone, fax,
  mobile and e-mail icons draw as ☎ 🖨 📱 ✉, and the borderless address and
  date tables no longer show grid lines.

## Roadmap, in priority order

Ordered by how many ordinary documents each item fixes, not by how hard it is.

Done in 2026-09: paragraph geometry (indents, spacing, line spacing and
fixed line heights), tab stops with the default interval, symbol-font
mapping, table cell borders and backgrounds, list number formats,
multi-level labels and document bullets, highlight, and paragraph borders
and backgrounds.

1. **Table width and position** (a narrow table stays narrow), and cell
   vertical alignment and padding.
2. **Page model: size, margins, headers and footers** as page furniture
   rather than blocks, then pagination in the view (page boxes on a grey
   desk, like Writer's print layout).
3. **Positioned frames and floating pictures** (anchor, x/y, wrap). This
   depends on 2 and is the only way a letterhead can look like the original.
4. **Footnotes and endnotes** as real notes, **DOCX headers and footers**,
   and **DOC merged cells, headers, footers and footnotes** (the text for
   these lies after the main text in the piece table: `ccpFtn`, `ccpHdd`).
5. Comments, tracked changes, shapes, charts: read-only display first.

Item 1 is self-contained (a model field, three readers, the view and the
two writers each), and each can be checked against a LibreOffice-generated
fixture the way `Tests/fixtures/word97-formatting.*` is.

## Testing approach

`Tests/fixtures/word97-formatting.fodt` is a hand-written flat ODT.
`word97-formatting.odt`, `word97-formatting.doc` and `word97-formatting.docx`
are LibreOffice's saves of it:

```bash
soffice --headless --convert-to odt word97-formatting.fodt
soffice --headless --convert-to "doc:MS Word 97" word97-formatting.fodt
soffice --headless --convert-to docx word97-formatting.fodt
```

`WordFormatsTest` checks that all three readers recover the same structure and
that it survives a save to `.odt` and `.docx`. To cover a new feature, add it
to the `.fodt`, regenerate both files and extend `CheckFormattingFixture`.
Never commit a real person's document as a fixture.
