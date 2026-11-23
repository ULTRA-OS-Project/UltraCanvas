- **MediaViewer: word-processing documents show as their pages.** A `.odt`,
  `.docx` or `.doc` was not a file the viewer could show - UltraFiler's preview
  pane stayed closed on one, and UltraViewer passed it by. It now opens in the
  document view as its pages, laid out as print and PDF export lay them out
  (`UltraCanvasRichTextEdit`: page size and margins, fonts, pictures, tables,
  header and footer, on white paper in either theme), with the PDF view's zoom,
  page strip, page keys and search over the real text, fitted to the page
  width. A password-protected `.docx` or `.odt` waits for Unlock, as a PDF
  does. `UltraCanvasMediaViewer::IsWordProcessingFile`; builds without the PDF
  view do not list them. `Tests/MediaViewerWordDocumentTest.cpp` opens a
  `.docx`, a two-page `.odt` and a Word 97 `.doc`, and unlocks a protected
  `.odt`.
