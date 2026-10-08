- **A rich paste reads the clipboard's HTML through the HTMLReader.**
  `UCRichDocument::FromHTML` - what `UltraCanvasRichTextEdit` pastes from a
  browser, Word or LibreOffice - had its own tokenizer, a table of 55
  entities and its own `style=""` reader. It now calls
  `ImportHTMLToRichDocument`, the importer UltraMail's composer already uses,
  so a paste reads like the page it was copied from:
  - the page's `<style>` sheets apply, through the same cascade a mail is
    shown with (a class colour, Word's fonts and sizes), and paragraphs keep
    the spacing their CSS gives them;
  - every HTML entity is decoded, not only the 55;
  - a `<blockquote>` is a quote level, so a quoted list, heading or table
    stays what it is (it was one quote paragraph);
  - a picture the clipboard only links to is pasted as its alt text in
    brackets, and a 1-2 px tracking pixel is left out.
  As before, `<pre>` is a code block, Word's typed-out list labels are left
  out, a no-break space is pasted as a space, and `dir="rtl"` makes a
  right-to-left paragraph.
- **`ImportHTMLToRichDocument` reads `dir="rtl"`**, on a paragraph or any
  element around it, into right-to-left paragraphs - a reply to an Arabic or
  Hebrew mail keeps its direction. Two options serve a paste and are off for
  a mail: `preAsCodeBlock` (a `<pre>` as a code block; a mail's `<pre>` is
  mostly quoted plain text) and `skipWordListLabels` (the "1." Word types out
  in a `mso-list:Ignore` span; a browser shows it, so a mail keeps it).
- **The Filer's preview of an `.html` file is the page as a browser lays it
  out** (`HTML::ExtractPlainText`, `PlainTextLayout::Lines`): a line per
  paragraph, list item and table row. Its own tag-level splitter showed the
  `<title>` and a mail's hidden preheader as page text, dropped the list
  markers and ran a table row's cells together ("NamePrice").
  `UltraCanvasFilerWidget::TextPreviewLines` (static) gives the lines a
  document's preview page shows.
- **Nothing in the repository reads HTML, CSS or entities on its own any
  more**: `scripts/html_reuse_baseline.txt` is empty.
