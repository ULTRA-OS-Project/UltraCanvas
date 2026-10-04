// include/IODeviceManager/UltraCanvasIODevicePrinterRaster.h
// CUPS raster: the page format GutenPrint's filter reads on its standard input.
//
// Why this exists. The GutenPrint renderer does not link libgutenprint - that
// would make the distributed binary GPL, and UltraCanvas is MIT. It runs
// GutenPrint's own tools instead, which is the pattern this repository
// already uses for QEMU and Wine. The tool that turns a page into a printer's
// command stream is `rastertogutenprint`, a CUPS filter: it reads CUPS raster
// on stdin and writes the device's own language on stdout. So a page has to
// be handed over in that format, and this is the writer for it.
//
// The format is a 4-byte sync word, once, at the start of the stream; then,
// for each page, a 1796-byte header and the pixels. Everything is big-endian,
// because that is what the sync word chosen below declares.
//
// "Once" is the part that matters. A sync word before every page reads, to
// the filter, as the first four bytes of the second page's header: every
// field after it is shifted, and CUPS's reader stops at that header without
// an error - so a multi-page job printed its first page and nothing else,
// successfully. That is what this writer did until the sync word was made
// its own call.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevicePrinterTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// COLOUR SPACE
// ============================================================================

// The CUPS colour-space numbers, which a GutenPrint PPD states directly:
// `*ColorModel RGB/RGB Color: "<</cupsColorSpace 1/cupsColorOrder 0>>..."`.
//
// Only the two uninterpreted ones are offered here. A GutenPrint PPD also
// advertises CMY, CMYK and KCMY, but feeding those would mean separating the
// colour ourselves and handing GutenPrint the result - and separation against
// a specific ink set at a specific resolution is the single thing GutenPrint
// is better at than anything we could write. It is given RGB (or grey) and
// left to do its own. That is also what the PPDs default to.
enum class IOCupsColorSpace {
    Gray = 0,       // CUPS_CSPACE_W: 8 bits, 255 is white
    RGB = 1         // CUPS_CSPACE_RGB: 8 bits per channel, chunked
};

// ============================================================================
// PAGE HEADER
// ============================================================================

// One page's worth of raster geometry.
//
// The fields here have to agree with the PPD the filter is run against: the
// filter reads the model and its options from the PPD and the geometry from
// this header, and a disagreement between the two is not diagnosed - it comes
// out as a misprinted page.
struct IOCupsRasterPage {
    int widthPixels = 0;
    int heightPixels = 0;
    int dpiX = 0;
    int dpiY = 0;

    // The sheet in PostScript points (1/72 inch), which is the unit the PPD's
    // *PageSize entries use: `*PageSize Letter/Letter: "<</PageSize[612 792]..."`.
    int pageWidthPoints = 0;
    int pageHeightPoints = 0;

    // Must be a *PageSize keyword the PPD defines - "Letter", "A4", "Legal".
    // The filter looks the name up; a name it does not know falls back to the
    // PPD's default size, silently.
    std::string pageSizeName;

    IOCupsColorSpace colorSpace = IOCupsColorSpace::RGB;

    int copies = 1;
    bool duplex = false;
    bool tumble = false;

    int BitsPerColor() const { return 8; }
    int NumColors() const { return colorSpace == IOCupsColorSpace::RGB ? 3 : 1; }
    int BitsPerPixel() const { return BitsPerColor() * NumColors(); }
    int BytesPerLine() const { return widthPixels * NumColors(); }

    bool IsValid() const {
        return widthPixels > 0 && heightPixels > 0 && dpiX > 0 && dpiY > 0 &&
               pageWidthPoints > 0 && pageHeightPoints > 0;
    }
};

// The sizes the format fixes: the sync word that opens a stream, and the
// header that opens each page. Asserted by the tests rather than trusted.
constexpr size_t kCupsRasterSyncBytes = 4;
constexpr size_t kCupsRasterHeaderBytes = 1796;

// Appends the sync word that opens a stream. Once per stream, before the
// first page header - never again.
//
// The sync word is "RaS3": version 3, big-endian, and **uncompressed**. V2
// raster is run-length encoded, and the encoder is the kind of code that is
// wrong in ways that only show up on one printer at one resolution. Nothing
// here is worth that: the raster goes down a pipe to a filter that reads it
// immediately, so the bytes never reach a disk or a network and the size
// costs nothing but a moment of memory.
void AppendCupsRasterSync(std::vector<uint8_t>& out);

// Appends one page's header - the header only; the stream's sync word is
// AppendCupsRasterSync's. False, with nothing appended, for a page that
// fails IsValid().
bool AppendCupsRasterPageHeader(const IOCupsRasterPage& page,
                                std::vector<uint8_t>& out);

// Appends one row, converting from 32-bit RGBA (top-down, tightly packed).
//
// Alpha is composited onto white rather than dropped: paper is opaque, and a
// transparent PNG with dropped alpha prints its transparent regions as black.
bool WriteCupsRasterRow(const uint8_t* rgba, int width,
                        IOCupsColorSpace colorSpace,
                        std::vector<uint8_t>& out);

}  // namespace UltraCanvas
