- **A Word or LibreOffice document's thumbnail is its first page.** A `.docx`,
  `.doc` or `.odt` tile in the file display (UltraFiler, the desktop's boards,
  `RenderFileThumbnail`) showed a white square holding the document's text
  lines in one small grey font - what the letter says, not what it looks
  like, so a letterhead, a CV with a photo and a plain draft looked the same.
  The `word-processing` preview provider now draws thumbnails too: it reads
  the document with `UCWordDocumentIO` and lays out its first page with
  `UltraCanvasRichTextEdit`, the same pages its PDF export and print give -
  page size and margins, fonts and sizes, pictures, colours, tables, header
  and footer - on white paper in either theme, drawn on the thumbnail
  workers and cached like every other thumbnail. Only the first 200 blocks
  are laid out, so page 1 of a long report costs what a letter's does; the
  layouts are serialized, as the vector drawings' are. A page that cannot be
  drawn falls back to the page of text; a password-protected `.docx` keeps
  its type glyph. RTF, Markdown, HTML and LaTeX keep their page of text.
  `FilePreviewsTest` checks the page of the `.docx`, `.odt` and `.doc`
  fixtures.
