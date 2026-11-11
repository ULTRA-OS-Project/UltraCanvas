- **Word documents: text frames and floats placed from the page.** A
  letterhead's sender block now stands beside the address instead of
  following it in the text.
  - The model gains text frames: a run with `RichTextRun::frameIndex` refers
    to a `RichTextFrame` in `UCRichDocument::frames` (its paragraphs, fill,
    border and padding) and floats like a floating picture.
  - Floats can be placed from the page's edges or its text area as well as
    from the paragraph (`floatHorizontalFrom`, `floatVerticalFrom`,
    `floatVerticalAlign`).
  - The ODT reader reads `draw:text-box` frames (Writer frames and drawing
    text boxes) and page-anchored frames and pictures, with
    `style:horizontal-rel` / `vertical-rel`.
  - The DOCX reader reads text boxes (`wps:txbx`, and VML `v:textbox`
    fallbacks) and every `wp:positionH` / `wp:positionV` `relativeFrom`.
  - The DOC reader reads floating pictures and text boxes, which it used to
    drop: OfficeArt shapes from `PlcSpaMom`, `DggInfo` and the text-box story.
  - `UltraCanvasRichTextEdit` places and draws frames and page-placed floats
    in page view, with text wrapping round them.
  - Both writers save frames and placement (ODF `draw:frame` with
    `draw:text-box`; DOCX `wps` text boxes and `wp:anchor` relations), and
    LibreOffice reads the saved files back with the frames in place.
  - Markdown, HTML and plain text spell a frame's text before its paragraph
    (`UCRichDocument::WithFramesInline`).
  - New fixture `Tests/fixtures/word-frames.*`.
- **`UltraCanvasRichTextEdit`: text wraps round floats line by line.** A
  paragraph beside a floating picture or frame is narrowed only for the lines
  level with it; its lines below take the column's full width again, as in
  Writer. A top-and-bottom picture set lower down in its paragraph keeps the
  lines that fit above it there and continues the paragraph below it. Before,
  the whole paragraph was narrowed, or moved below the picture.
- **`UltraCanvasRichTextEdit::SetZoomFit`: page width and whole page.** In
  page view the zoom follows the element's size, so a narrow window shows the
  whole page. `SetZoom` and Ctrl+wheel end it; printing stays at 100 %.
- **DOCX writer: floating pictures keep their placement from the page or
  margin.** Before, every floating picture was written as placed from the
  column and the paragraph.
