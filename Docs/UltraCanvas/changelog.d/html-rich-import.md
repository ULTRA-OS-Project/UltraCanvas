- **HTML can be opened in the WYSIWYG editor.** New
  `HTMLReader/HTMLRichDocumentImporter.h`: `ImportHTMLToRichDocument` /
  `AppendHTMLToRichDocument` turn an HTML page or fragment into a
  `UCRichDocument`, through the HTML reader's own parser and style resolver.
  - Mapped: paragraphs, headings, lists (nested, start numbers, letter and
    Roman formats), rules and line breaks.
  - Text formatting: bold, italic, underline, strike, sub/superscript, code,
    links, text and highlight colours, and font families and sizes.
  - Layout: alignment, left indents, and the space between blocks, collapsed
    as CSS collapses margins.
  - Pictures (`data:` URIs, or any other source through a `resolveImage`
    callback) become a picture paragraph when alone in their block, else
    sit inside the line.
  - Tables with several columns keep spans, cell colours, borders, padding and
    widths. One-column layout tables are unwrapped into the text flow, and
    a table inside a cell becomes lines of that cell.
- **Blocks carry a quote level.** New `RichDocBlock::quoteLevel`: how many
  quotes a block sits inside. It applies to any kind of block, so a quoted
  list or table stays one.
  - `UltraCanvasRichTextEdit` draws a bar per level and indents the block.
  - Enter keeps the level. Enter on an empty quoted line, or Backspace at the
    start of a quoted block, steps one level out.
  - `ToHTML` nests `<blockquote type="cite">`. `ToPlainText` and
    `ToMarkdown` prefix the lines with `> `.
- **`UCRichDocument::ToHTML(RichDocumentHTMLOptions)`**: an `imageSource` hook
  decides a picture's `src`, for example `cid:` for mail; without it pictures
  stay `data:` URIs.
  - Pictures now carry their `width`/`height`.
  - Headings and picture paragraphs keep their alignment.
- **The HTML reader reads `<font color face size>`, `bgcolor` and
  `<body text>`**, which much mail HTML is still written with. As in a
  browser, CSS for the same property wins.
- **An inline picture in the rich text editor no longer runs past the right
  edge** of an indented or quoted paragraph: it is fitted to the line, not
  the column.
- **UltraNet can send HTML mail with a plain-text version and embedded
  pictures.**
  - New `UltraNetMimeBuildInput::alternativeText` builds
    `multipart/alternative`.
  - Inline attachments with a Content-ID travel with the HTML in
    `multipart/related`.
  - Both text parts are quoted-printable, so long HTML lines stay within
    SMTP's 998-character limit.
  - `UltraNetMailMessage` carries the same as `alternativeText` and
    `inlineParts`, and the SMTP plug-in passes them on.
