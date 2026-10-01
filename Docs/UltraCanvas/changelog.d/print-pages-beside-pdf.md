- **A word-processing document prints on Windows, through GutenPrint, and on an
  IPP printer without PDF.** It went to the printer only as the PDF
  `ExportToPdf` writes, which those renderers cannot lay out, so they refused
  the job. A job can now carry the same document as pages that draw themselves
  (`IOPrintJob::pages`, an `IPrintPageSource`) beside the PDF, and each
  renderer takes the form it can use: CUPS and a PDF-reading IPP printer the
  PDF, as before; GDI and GutenPrint the pages; IPP the pages as PWG raster when
  the printer takes no PDF or cannot select the page range from one.
  - `PrintDocumentWithDialog` / `PrintDocumentWithSettings` /
    `MakeDocumentPrintJob` take the pages as an optional last argument.
  - `CreateRichDocumentPrintPages(editor)` (`UltraCanvasRichTextPrint.h`): the
    pages `ExportToPdf` writes, laid out by a hidden copy of the document, drawn
    straight into a render-context target or off screen at up to 300 dpi for a
    Windows printer DC. A page the sheet's size prints 1:1 on the sheet's
    edges; a larger one is scaled to fit.
  - `UltraCanvasRichTextEdit::BeginPrintLayout` / `RenderPrintPage` /
    `EndPrintLayout` draw a document's pages into any context; `ExportToPdf`
    is built on them.
  - `IPrintPageTarget::GetRenderContext()` (`RasterPageTarget` returns its
    off-screen context). The native renderer refuses a job of pages alone by
    name instead of queuing an empty one.
  - New test `RichTextPrintTest`; `IODevicePrinterIPPTest` covers planning a
    job of pages.
