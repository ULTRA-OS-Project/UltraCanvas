- **PowerPoint slides are read and drawn as they look.** The reader
  (`UltraCanvasPresentationPptx.h`) now resolves each run's typeface (the
  theme's heading and body fonts for `+mj-lt` / `+mn-lt`), strike,
  super- and subscript, capitals, highlight and hyperlinks; each paragraph's
  spacing, margin, hanging indent, bullet character, font, colour and size
  and its numbering scheme (`PptxNumberLabel`: `c)`, `iv.`, `(3)` ...); each
  shape's insets, wrap and vertical text, turn and mirroring, custom outline,
  gradient fill, dash and arrowheads. Connectors are read as lines; tables
  keep their style (`ppt/tableStyles.xml`, else PowerPoint's Medium Style 2),
  merged cells, fills, borders and margins; pictures their crop; charts are
  read; SmartArt is read from the drawing PowerPoint saves beside it; an
  embedded object shows the picture it carries. `RenderPptxSlide` draws all
  of it, charts through the chart engine.
- **`UltraCanvasDrawingML.h`: one chart and colour reader for every Office
  format.** `ReadDrawingMLChart` (a `c:chartSpace` part into `RichChart`) and
  `DrawingMLColor` moved out of the Word plugin into core; the `.docx`
  reader and the PowerPoint reader both call them, and an `.xlsx` chart view
  can. Elements are matched by local name, so a part that binds the chart
  namespace to another prefix reads too.
- **`UltraCanvasPresentationPages.h`: a deck's slides as a document's pages.**
  An `IPDFDocument` whose pages are slides, so `UltraCanvasPDFView` shows a
  `.pptx` or `.odp` as it shows a PDF - zoom, thumbnails, page keys, search
  framed on the shapes. MediaViewer opens decks this way, names the slide
  and its speaker notes on the info bar, shows the outline on **O**
  (`SetPresentationOutline`) and runs a full-screen slide show on **F5**
  (`StartPresenting` / `StopPresenting`).
- **A deck's file thumbnail is its first slide** (the `powerpoint` preview
  provider, `.odp` / `.otp` / `.fodp` too), not a page of its outline.
- **`RichShape::ApplyPreset` knows more of DrawingML's shapes**: bent and
  curved connectors (open paths), double and notched arrows, stars of 6 to 32
  points, hepta-, deca- and dodecagons, snipped and rounded rectangles,
  callouts and more flowchart shapes - for `.docx` drawings too.
