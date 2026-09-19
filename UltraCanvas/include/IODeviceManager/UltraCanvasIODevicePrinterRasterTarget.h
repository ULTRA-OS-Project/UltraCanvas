// include/IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h
// An IPrintPageTarget that draws onto a bitmap instead of onto a device.
//
// The Windows GDI renderer draws its pages onto a printer device context, and
// the driver turns those calls into the printer's commands. The GutenPrint
// renderer has no device context to draw onto: GutenPrint's filter wants a
// page of pixels. So the same IPrintPageSource - the same wrapped text, the
// same fitted image, the same pagination - is driven against this instead,
// and the pixels go to the filter.
//
// That is the point of IPrintPageTarget being abstract: one page source, one
// layout, and a target per kind of destination.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevicePrinterPage.h"
#include "UltraCanvasIODevicePrinterRaster.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class IRenderContext;

// Draws a page into an off-screen surface and hands the pixels back a row at
// a time.
//
// Off-screen means exactly that: the render context is created over an image
// surface with no window and no display behind it, which is a path this
// codebase already relies on for chart and QR-code export and covers in
// Tests/OffscreenRenderTest. A print job must work on a machine with no
// screen at all - a server printing invoices has no display to borrow.
class RasterPageTarget : public IPrintPageTarget {
public:
    RasterPageTarget(int widthPixels, int heightPixels, int dpiX, int dpiY);
    ~RasterPageTarget() override;

    RasterPageTarget(const RasterPageTarget&) = delete;
    RasterPageTarget& operator=(const RasterPageTarget&) = delete;

    // False when no surface could be created - a build with no rendering
    // backend, or a size the backend refused.
    bool IsValid() const;

    // Clears to white. Called before each page; paper starts blank, and a
    // surface starts as whatever was on it.
    void BeginPage();

    // ===== IPrintPageTarget =====

    IOPrintPageMetrics GetMetrics() const override;
    bool DrawImage(const uint8_t* pixels, int width, int height,
                   const IOPrintRect& dest) override;
    bool DrawTextLine(const std::string& utf8, int x, int baselineY,
                      int pixelHeight) override;
    int MeasureTextWidth(const std::string& utf8, int pixelHeight) const override;
    int GetLineHeight(int pixelHeight) const override;
    int GetAscent(int pixelHeight) const override;

    // ===== READING THE PAGE BACK =====

    // Calls `row` once per scan line, top to bottom, with 32-bit RGBA.
    // Stops early and returns false if `row` does.
    //
    // A row at a time rather than the whole page: an A4 page at 1440 dpi is
    // about 700 MB in RGBA, and the caller is converting each row into the
    // raster's colour space anyway, so there is no reason for both copies to
    // exist at once.
    bool ForEachRow(const std::function<bool(const uint8_t* rgba, int width)>& row) const;

private:
    // The font size that makes an em `pixelHeight` pixels tall.
    //
    // Measured rather than converted: the size the backend takes is in its
    // own units, and whether that is pixels or points at some assumed
    // resolution is a property of the text stack, not something worth
    // hard-coding a 72/96 into here. One measurement settles it, and the
    // answer is cached per requested height.
    double FontSizeFor(int pixelHeight) const;

    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace UltraCanvas
