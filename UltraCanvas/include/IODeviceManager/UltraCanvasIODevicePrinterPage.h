// include/IODeviceManager/UltraCanvasIODevicePrinterPage.h
// The page-drawing seam: what a renderer produces when it cannot produce
// bytes, and the device surface it draws onto.
//
// Why this exists. A print payload is normally a byte stream - either the
// printer's own command language (GutenPrint) or a document the platform's
// driver will process (CUPS hands a PDF to its filter chain). Windows GDI is
// neither: a printer device context is a *drawing session*, StartDoc,
// StartPage, GDI calls, EndPage, EndDoc, and the driver turns those calls
// into device commands itself. There is nothing to put in a buffer. So a
// renderer for that path produces an IPrintPageSource, and the transport
// drives it against an IPrintPageTarget that wraps the real device.
//
// Keeping the target abstract is what makes the layout testable: pagination,
// scaling and centring are ordinary arithmetic that must not need a printer
// - or Windows - to verify.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODeviceTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// PAGE GEOMETRY
// ============================================================================

// A rectangle in device dots, relative to the printable area's origin.
struct IOPrintRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool IsEmpty() const { return width <= 0 || height <= 0; }
};

// What one page of the destination device looks like.
//
// Every measurement is in device dots, because that is the only unit a
// printer driver speaks natively and converting twice loses more than it
// clarifies. The unprintable margin is reported rather than silently
// subtracted: a caller that wants edge-to-edge output on a borderless photo
// printer needs to know the offset exists.
struct IOPrintPageMetrics {
    int widthDots = 0;          // printable width
    int heightDots = 0;         // printable height
    int dpiX = 0;
    int dpiY = 0;

    // Distance from the sheet's physical edge to the printable area, in
    // dots. GDI's PHYSICALOFFSETX / PHYSICALOFFSETY.
    int offsetXDots = 0;
    int offsetYDots = 0;

    bool IsValid() const {
        return widthDots > 0 && heightDots > 0 && dpiX > 0 && dpiY > 0;
    }

    // The whole printable area as a rectangle, for a source that just wants
    // to fill the page.
    IOPrintRect PrintableArea() const {
        return IOPrintRect{0, 0, widthDots, heightDots};
    }
};

// ============================================================================
// PAGE TARGET
// ============================================================================

// The surface one page is drawn onto. Implemented by a platform transport
// over its device context, and by tests over a recording fake.
//
// Deliberately small. This is not a general 2D API - UltraCanvas already has
// one - it is the minimum a print renderer needs, and every operation here
// maps to a single call on a Windows printer DC. Anything richer belongs in
// the rendering stack, drawn into a bitmap, and handed over through
// DrawImage.
class IPrintPageTarget {
public:
    virtual ~IPrintPageTarget() = default;

    virtual IOPrintPageMetrics GetMetrics() const = 0;

    // 32-bit RGBA, 8 bits per channel, top-down, tightly packed
    // (stride == width * 4). Scaled into `dest`; the implementation picks
    // the best filtering the device offers.
    virtual bool DrawImage(const uint8_t* pixels, int width, int height,
                           const IOPrintRect& dest) = 0;

    // Draws one line of UTF-8 text with its left edge at x and its *baseline*
    // at y. Baseline rather than top because that is what every text engine
    // and GDI itself position by, and converting in the caller would need the
    // font's ascent, which only the target knows.
    //
    // Named DrawTextLine, not DrawText, because <windows.h> defines DrawText
    // as a macro expanding to DrawTextA/DrawTextW. A method called DrawText
    // is quietly renamed by the preprocessor in any translation unit that has
    // included windows.h, so an override stops overriding and the class turns
    // abstract - with an error that points at the override rather than at the
    // macro. The same trap took GetLastError() in this module already.
    virtual bool DrawTextLine(const std::string& utf8, int x, int baselineY,
                              int pixelHeight) = 0;

    // Width in dots that DrawTextLine would occupy for this string at this size.
    // Needed before drawing, to wrap - so a text source cannot paginate until
    // it has met the device.
    virtual int MeasureTextWidth(const std::string& utf8,
                                 int pixelHeight) const = 0;

    // Distance from one baseline to the next at this size, including the
    // font's own leading.
    virtual int GetLineHeight(int pixelHeight) const = 0;

    // Distance from the top of a line box to its baseline.
    virtual int GetAscent(int pixelHeight) const = 0;
};

// ============================================================================
// PAGE SOURCE
// ============================================================================

// A paginated document waiting for a device to be drawn onto.
class IPrintPageSource {
public:
    virtual ~IPrintPageSource() = default;

    // Called once, with the real target, before GetPageCount() or DrawPage().
    //
    // Pagination cannot happen earlier: how many pages a text document needs
    // depends on the printable area and the font metrics, so the same
    // document is a different number of pages on A4 at 600 dpi than on Letter
    // at 300. A source that does not paginate (a single image) can still use
    // this to work out its placement once instead of per page.
    virtual IODeviceResult Prepare(IPrintPageTarget& target) = 0;

    // Valid only after a successful Prepare().
    virtual int GetPageCount() const = 0;

    // Draws page `pageIndex` (0-based). The transport has already begun the
    // page on the device; this only draws.
    virtual IODeviceResult DrawPage(int pageIndex, IPrintPageTarget& target) = 0;
};

using IPrintPageSourcePtr = std::shared_ptr<IPrintPageSource>;

// ============================================================================
// LAYOUT HELPERS
// ============================================================================

// Largest rectangle with the source's aspect ratio that fits inside `page`,
// centred. Returns an empty rectangle for a degenerate source or page rather
// than dividing by zero.
//
// Scaling *down* only is not an option here: a 640x480 screenshot on an A4
// page at 600 dpi would come out under an inch wide. Print scales to the
// paper, unlike screen display.
IOPrintRect FitPreservingAspect(int sourceWidth, int sourceHeight,
                                const IOPrintRect& page);

// Splits `text` into lines that each fit within `maxWidth` when measured by
// `target` at `pixelHeight`.
//
// The line breaking is `TextWrapping::WrapGreedy`, which is already
// render-context-free for exactly this reason - it takes a measure callable -
// and already handles UTF-8 boundaries and over-long words. This adds only
// what a page needs and a caption does not: paragraphs, and settings that
// suit page flow rather than a truncated label (no line budget, so nothing is
// ever dropped; no overflow slack, because a printer clips at the hardware
// margin without saying so).
std::vector<std::string> WrapTextToWidth(const std::string& text,
                                         const IPrintPageTarget& target,
                                         int pixelHeight, int maxWidth);

// ============================================================================
// BUILT-IN SOURCES
// ============================================================================

// One decoded raster image, scaled to fill as much of the page as its aspect
// ratio allows, centred. Always exactly one page.
class ImagePageSource : public IPrintPageSource {
public:
    // Copies the pixels: the caller's buffer is usually a decode scratch
    // that will not outlive the call, and a print job can sit in a spooler
    // queue long after Print() returns.
    ImagePageSource(const uint8_t* rgba, int width, int height);

    IODeviceResult Prepare(IPrintPageTarget& target) override;
    int GetPageCount() const override { return pixels.empty() ? 0 : 1; }
    IODeviceResult DrawPage(int pageIndex, IPrintPageTarget& target) override;

    // Where the image will land on the page. Exposed for tests and for a
    // print preview.
    IOPrintRect GetPlacement() const { return placement; }

private:
    std::vector<uint8_t> pixels;
    int imageWidth = 0;
    int imageHeight = 0;
    IOPrintRect placement;
};

// Plain UTF-8 text, wrapped to the printable width and paginated.
class TextPageSource : public IPrintPageSource {
public:
    explicit TextPageSource(std::string utf8, int pixelHeight = 0);

    IODeviceResult Prepare(IPrintPageTarget& target) override;
    int GetPageCount() const override { return pageCount; }
    IODeviceResult DrawPage(int pageIndex, IPrintPageTarget& target) override;

    // Exposed for tests: the wrapped lines and how many land on each page.
    const std::vector<std::string>& GetLines() const { return lines; }
    int GetLinesPerPage() const { return linesPerPage; }

private:
    std::string text;
    int requestedPixelHeight = 0;
    int pixelHeight = 0;
    std::vector<std::string> lines;
    int linesPerPage = 0;
    int pageCount = 0;
};

}  // namespace UltraCanvas
