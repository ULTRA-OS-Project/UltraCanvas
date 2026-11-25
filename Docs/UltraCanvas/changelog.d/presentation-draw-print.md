- **A slide could only be drawn as a whole pixmap, and a deck could not be
  printed or saved as PDF.** The slide painter was private to
  `RenderPptxSlide` and cleared its whole context. `DrawPptxSlide`
  (`UltraCanvasPptxSlideRender.h`) now draws a slide on any render context
  into a rectangle, filling only that with the slide's background; for an
  editor it can draw grey prompt text in empty placeholders and leave one
  shape's text out (`PptxSlideDrawOptions`).
- **`PresentationPrintPages`** (`UltraCanvasPresentationPrint.h`): a page per
  slide at the slide's size, hidden slides left out unless asked for, drawn
  by `DrawPptxSlide`; `ExportPdf` to a file or memory, and an
  `IPrintPageSource` for `PrintDocumentWithDialog`.
- **The deck model** gains masters and layouts (`PptxMaster`, `PptxLayout`,
  `PptxSlide::layoutIndex`), transitions (`PptxTransition`), the
  format-neutral names `UCPresentation` / `UCSlide` / `UCSlideShape`, and
  `PptxSlideFromLayout`, a new slide made from a layout (Title and Content
  when the deck has none).

  Covered by `PresentationPrintTest`.
