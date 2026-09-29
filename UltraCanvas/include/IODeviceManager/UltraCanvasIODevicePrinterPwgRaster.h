// include/IODeviceManager/UltraCanvasIODevicePrinterPwgRaster.h
// PWG raster (PWG 5102.4): the page format every IPP Everywhere printer must
// accept, and so the one this side draws into when a printer will not take a
// document as it is.
//
// It is CUPS raster version 2 with the variable parts pinned down: a sync
// word once at the start of the stream, then per page a 1796-byte header and
// the page's lines, each line compressed. The header has the same layout as
// the CUPS raster header the GutenPrint path writes - the field meanings are
// what PWG narrowed - but the stream differs in the two ways a printer cares
// about: the pixels are compressed ("RaS2", not "RaS3"), and the colour
// spaces are the calibrated sRGB and sGray rather than device RGB.
//
// Pure arithmetic, like the CUPS raster writer beside it, so the byte layout
// and the compression are both tested without a printer.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// The two colour spaces IPP Everywhere requires a printer to accept, as the
// numbers the header's cupsColorSpace field takes.
enum class IOPwgColorSpace {
    SGray = 18,     // "sgray_8": one 8-bit channel, 255 is white
    SRGB = 19       // "srgb_8": three 8-bit channels, chunked
};

// One page's header.
struct IOPwgRasterPage {
    int widthPixels = 0;
    int heightPixels = 0;
    int dpiX = 0;
    int dpiY = 0;

    // The sheet, in points, portrait - the media's own dimensions whatever
    // the orientation of what is drawn on it.
    int pageWidthPoints = 0;
    int pageHeightPoints = 0;

    // The PWG media name, "iso_a4_210x297mm". Empty for a custom size.
    std::string pageSizeName;

    IOPwgColorSpace colorSpace = IOPwgColorSpace::SRGB;

    // Two-sided, and whether the binding is on the short edge.
    bool duplex = false;
    bool tumble = false;

    // How the pixels were turned for the back of a sheet, per
    // IOPwgBackSideTransform: mirrored across the page (cross-feed) and/or
    // top to bottom (feed). Written into the header so the printer knows.
    bool crossFeedMirrored = false;
    bool feedMirrored = false;

    // The number of pages in the document, or 0 when unknown.
    int totalPageCount = 0;

    // IPP print-quality: 3 draft, 4 normal, 5 high; 0 for the printer's
    // default.
    int printQuality = 0;

    int NumColors() const { return colorSpace == IOPwgColorSpace::SRGB ? 3 : 1; }
    int BitsPerPixel() const { return 8 * NumColors(); }
    int BytesPerLine() const { return widthPixels * NumColors(); }

    bool IsValid() const {
        return widthPixels > 0 && heightPixels > 0 && dpiX > 0 && dpiY > 0 &&
               pageWidthPoints > 0 && pageHeightPoints > 0 &&
               widthPixels <= 0xFFFFFF && heightPixels <= 0xFFFFFF;
    }
};

constexpr size_t kPwgRasterSyncBytes = 4;
constexpr size_t kPwgRasterHeaderBytes = 1796;

// Appends "RaS2", the sync word that opens a PWG raster stream. Once per
// stream, not once per page: the pages that follow are header and lines only.
void AppendPwgRasterSync(std::vector<uint8_t>& out);

// Appends one page header. False, with nothing appended, for a page that
// fails IsValid().
bool AppendPwgRasterPageHeader(const IOPwgRasterPage& page, std::vector<uint8_t>& out);

// Appends a page's lines, compressed. `pixels` holds heightPixels lines of
// BytesPerLine() bytes each, top to bottom, already in the page's colour
// space.
//
// The compression is the one the format fixes: each line starts with a count
// of how many times it repeats (so a run of blank lines costs one line), then
// runs of pixels - a count of up to 128 copies of one pixel, or of up to 128
// pixels written out as they are.
bool AppendPwgRasterPageLines(const IOPwgRasterPage& page, const uint8_t* pixels,
                              size_t pixelBytes, std::vector<uint8_t>& out);

// How the back of a sheet has to be turned for this printer, from its
// pwg-raster-document-sheet-back keyword and the binding edge. A duplexer
// that flips the sheet over one edge delivers its back side upside down or
// mirrored relative to the front, and the printer says which so that the
// page can be drawn to match - otherwise every even page is printed the
// wrong way up.
//
// The table is the one CUPS applies when it builds a PWG raster header for a
// back side:
//
//   sheet-back       long edge          short edge
//   normal           -                  -
//   flipped          mirror feed        mirror cross-feed
//   rotated          mirror both        -
//   manual-tumble    -                  mirror both
struct IOPwgBackSide {
    bool mirrorCrossFeed = false;
    bool mirrorFeed = false;
};

IOPwgBackSide IOPwgBackSideTransform(const std::string& sheetBack, bool shortEdge);

// Turns a page's pixels: `quarterTurns` quarter turns counter-clockwise
// (0-3), then mirrored across (x) and/or top to bottom (y). `width` and
// `height` are the source's and are updated to the result's. Used for
// landscape pages, which are drawn landscape and delivered on a portrait
// sheet, and for the back sides above.
std::vector<uint8_t> IOPwgTransformPixels(const std::vector<uint8_t>& pixels,
                                          int& width, int& height,
                                          int bytesPerPixel, int quarterTurns,
                                          bool mirrorX, bool mirrorY);

}  // namespace UltraCanvas
