// include/UltraCanvasRichTextPrint.h
// A word-processing document's pages for a printer that cannot take its PDF.
//
// Why this exists. A formatted document prints as the PDF
// UltraCanvasRichTextEdit::ExportToPdf writes, which CUPS - and an IPP printer
// that reads PDF - print as it is. The Windows GDI renderer, GutenPrint and an
// IPP printer without PDF cannot: they print what they can draw, and nothing
// in this framework lays a PDF out. They can draw an IPrintPageSource
// (UltraCanvasIODevicePrinterPage.h), and the element can draw its pages into
// any context - so this hands them the same pages the PDF holds, drawn at the
// printer's resolution instead.
//
// Usage - the PDF and its pages go together, and each printer takes the one
// it can use:
//
//     std::vector<uint8_t> pdf;
//     std::string error;
//     if (richEdit->ExportToPdf(pdf, error)) {
//         PrintDocumentWithDialog("Letter.docx", pdf, "application/pdf", window,
//                                 CreateRichDocumentPrintPages(*richEdit));
//     }
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "IODeviceManager/UltraCanvasIODevicePrinterPage.h"
#include "UltraCanvasRichTextEdit.h"

#include <memory>
#include <string>

namespace UltraCanvas {

class RasterPageTarget;

// The pages of one rich document, for the printers that draw:
//
//   * a target with a render context (GutenPrint's page, IPP's PWG raster
//     page) has each page drawn straight into it - text stays text at the
//     device's resolution;
//   * a device the rendering stack cannot draw on (a Windows printer DC) gets
//     each page drawn off screen at up to 300 dpi and placed as an image.
//
// They are the pages ExportToPdf writes: the same layout, headers, footers,
// notes and floating pictures, and nothing that belongs to editing.
//
// The document is copied when the source is made, so editing it while a job
// is spooling cannot change what prints. The copy is laid out by an element
// of its own, never shown, so the one on screen keeps its view.
//
// Placement on a device: a page the size of the sheet prints 1:1, lined up
// with the sheet's edges, so the document's margins land where it says they
// are. A page larger than the sheet (an A4 document on Letter paper) is
// scaled down to fit it; one smaller than the sheet is centred on it.
class RichDocumentPrintPages : public IPrintPageSource {
public:
    // `style` supplies what a document leaves to the view - the base font,
    // the heading sizes; pass the on-screen element's so the print matches.
    RichDocumentPrintPages(const UCRichDocument& document,
                           const RichTextEditStyle& style);
    ~RichDocumentPrintPages() override;

    RichDocumentPrintPages(const RichDocumentPrintPages&) = delete;
    RichDocumentPrintPages& operator=(const RichDocumentPrintPages&) = delete;

    // ===== IPrintPageSource =====
    IODeviceResult Prepare(IPrintPageTarget& target) override;
    int GetPageCount() const override { return pageCount; }
    IODeviceResult DrawPage(int pageIndex, IPrintPageTarget& target) override;

    // Where a page lands on the target Prepare() was given, in device dots
    // from the printable area's origin (so negative by the unprintable
    // margin when the page is lined up with the sheet). Exposed for tests
    // and for a print preview.
    struct Placement {
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
        // Device dots per view pixel (the element draws 96 to the inch).
        double scaleX = 1.0;
        double scaleY = 1.0;
    };
    const Placement& GetPlacement() const { return placement; }

private:
    IODeviceResult DrawPageAsImage(int pageIndex, IPrintPageTarget& target);

    std::shared_ptr<UltraCanvasRichTextEdit> layout;
    int pageCount = 0;
    Placement placement;

    // The off-screen page used for a target with no render context of its
    // own. Made in Prepare() so the pages are laid out on the context they are
    // drawn on, and reused for every page.
    std::unique_ptr<RasterPageTarget> offscreen;
    double offscreenScale = 1.0;      // offscreen pixels per view pixel
};

// The pages of the document `editor` shows, styled as it is, or null when it
// holds no document.
std::shared_ptr<IPrintPageSource> CreateRichDocumentPrintPages(
        const UltraCanvasRichTextEdit& editor);

} // namespace UltraCanvas
