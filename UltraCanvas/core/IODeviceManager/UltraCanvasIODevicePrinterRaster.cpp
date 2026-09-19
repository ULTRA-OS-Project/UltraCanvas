// core/IODeviceManager/UltraCanvasIODevicePrinterRaster.cpp
// The CUPS raster page header, written field by field in the order the format
// fixes. Platform-neutral and pure arithmetic, so the whole thing is testable
// without a printer, a filter or GutenPrint installed.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODevicePrinterRaster.h"

#include <algorithm>
#include <cstring>

namespace UltraCanvas {

namespace {

// Every number in the header is big-endian: that is what the "RaS3" sync word
// declares. (The reversed spelling, "3SaR", would declare little-endian; the
// reader picks its byte order from which of the two it sees.)
void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void PutU32(std::vector<uint8_t>& out, int value) {
    PutU32(out, static_cast<uint32_t>(value < 0 ? 0 : value));
}

// IEEE-754 single precision, big-endian. Copied through memcpy rather than
// reinterpreted, because type-punning a float through a pointer is undefined
// and the optimiser is entitled to act on that.
void PutFloat(std::vector<uint8_t>& out, float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float is not 32 bits here");
    std::memcpy(&bits, &value, sizeof(bits));
    PutU32(out, bits);
}

// A fixed-width C string field: padded with NULs, and truncated so a long
// name cannot run into the next field.
void PutCString(std::vector<uint8_t>& out, const std::string& text,
                size_t width) {
    const size_t copied = std::min(text.size(), width - 1);
    out.insert(out.end(), text.begin(), text.begin() + static_cast<long>(copied));
    out.insert(out.end(), width - copied, 0);
}

void PutZeros(std::vector<uint8_t>& out, size_t count) {
    out.insert(out.end(), count, 0);
}

}  // namespace

// ============================================================================
// PAGE HEADER
// ============================================================================

bool WriteCupsRasterPageHeader(const IOCupsRasterPage& page,
                               std::vector<uint8_t>& out) {
    if (!page.IsValid()) {
        return false;
    }

    const size_t start = out.size();

    // Sync word: version 3 (uncompressed), big-endian.
    const char sync[] = {'R', 'a', 'S', '3'};
    out.insert(out.end(), sync, sync + 4);

    // --- Fields shared with PostScript's setpagedevice -----------------

    PutCString(out, std::string(), 64);   // MediaClass
    PutCString(out, std::string(), 64);   // MediaColor
    PutCString(out, std::string(), 64);   // MediaType
    PutCString(out, std::string(), 64);   // OutputType

    PutU32(out, 0);                       // AdvanceDistance
    PutU32(out, 0);                       // AdvanceMedia
    PutU32(out, 0);                       // Collate
    PutU32(out, 0);                       // CutMedia
    PutU32(out, page.duplex ? 1 : 0);     // Duplex

    PutU32(out, page.dpiX);               // HWResolution[0]
    PutU32(out, page.dpiY);               // HWResolution[1]

    PutZeros(out, 4 * 4);                 // ImagingBoundingBox[4]

    PutU32(out, 0);                       // InsertSheet
    PutU32(out, 0);                       // Jog
    PutU32(out, 0);                       // LeadingEdge

    PutZeros(out, 2 * 4);                 // Margins[2]

    PutU32(out, 0);                       // ManualFeed
    PutU32(out, 0);                       // MediaPosition
    PutU32(out, 0);                       // MediaWeight
    PutU32(out, 0);                       // MirrorPrint
    PutU32(out, 0);                       // NegativePrint
    PutU32(out, std::max(1, page.copies));// NumCopies
    PutU32(out, 0);                       // Orientation
    PutU32(out, 0);                       // OutputFaceUp

    PutU32(out, page.pageWidthPoints);    // PageSize[0], in points
    PutU32(out, page.pageHeightPoints);   // PageSize[1]

    PutU32(out, 0);                       // Separations
    PutU32(out, 0);                       // TraySwitch
    PutU32(out, page.tumble ? 1 : 0);     // Tumble

    // --- The cups* fields, which describe the bitmap itself -------------

    PutU32(out, page.widthPixels);        // cupsWidth
    PutU32(out, page.heightPixels);       // cupsHeight
    PutU32(out, 0);                       // cupsMediaType
    PutU32(out, page.BitsPerColor());     // cupsBitsPerColor
    PutU32(out, page.BitsPerPixel());     // cupsBitsPerPixel
    PutU32(out, page.BytesPerLine());     // cupsBytesPerLine
    PutU32(out, 0);                       // cupsColorOrder: 0 = chunked
    PutU32(out, static_cast<int>(page.colorSpace));   // cupsColorSpace
    PutU32(out, 0);                       // cupsCompression: none, per "RaS3"
    PutU32(out, 0);                       // cupsRowCount
    PutU32(out, 0);                       // cupsRowFeed
    PutU32(out, 0);                       // cupsRowStep

    // --- Version 2 additions -------------------------------------------
    //
    // Present in a v3 header too: the version in the sync word selects the
    // *encoding* of the pixels, not the size of the header.

    PutU32(out, page.NumColors());        // cupsNumColors
    PutFloat(out, 1.0f);                  // cupsBorderlessScalingFactor
    PutFloat(out, static_cast<float>(page.pageWidthPoints));   // cupsPageSize[0]
    PutFloat(out, static_cast<float>(page.pageHeightPoints));  // cupsPageSize[1]

    PutFloat(out, 0.0f);                                       // cupsImagingBBox[0]
    PutFloat(out, 0.0f);                                       // [1]
    PutFloat(out, static_cast<float>(page.pageWidthPoints));   // [2]
    PutFloat(out, static_cast<float>(page.pageHeightPoints));  // [3]

    PutZeros(out, 16 * 4);                // cupsInteger[16]
    PutZeros(out, 16 * 4);                // cupsReal[16]
    PutZeros(out, 16 * 64);               // cupsString[16][64]

    PutCString(out, std::string(), 64);   // cupsMarkerType
    PutCString(out, std::string(), 64);   // cupsRenderingIntent
    PutCString(out, page.pageSizeName, 64);   // cupsPageSizeName

    // The format fixes this exactly; a field added or dropped above shifts
    // everything after it and the filter reads garbage without complaining.
    return out.size() - start ==
           kCupsRasterSyncBytes + kCupsRasterHeaderBytes;
}

// ============================================================================
// PIXELS
// ============================================================================

bool WriteCupsRasterRow(const uint8_t* rgba, int width,
                        IOCupsColorSpace colorSpace,
                        std::vector<uint8_t>& out) {
    if (!rgba || width <= 0) {
        return false;
    }

    for (int x = 0; x < width; ++x) {
        const uint8_t* pixel = rgba + static_cast<size_t>(x) * 4u;
        const unsigned alpha = pixel[3];

        // Composited onto white, not dropped: paper is opaque, so a
        // transparent region is paper-coloured. Dropping alpha instead would
        // print the transparent parts of a PNG as whatever happened to be in
        // the colour channels, which for most encoders is black.
        const unsigned r = (pixel[0] * alpha + 255u * (255u - alpha)) / 255u;
        const unsigned g = (pixel[1] * alpha + 255u * (255u - alpha)) / 255u;
        const unsigned b = (pixel[2] * alpha + 255u * (255u - alpha)) / 255u;

        if (colorSpace == IOCupsColorSpace::RGB) {
            out.push_back(static_cast<uint8_t>(r));
            out.push_back(static_cast<uint8_t>(g));
            out.push_back(static_cast<uint8_t>(b));
        } else {
            // Rec. 601 luma, the weighting every other grayscale conversion
            // in this codebase uses.
            const unsigned luma = (r * 299u + g * 587u + b * 114u) / 1000u;
            out.push_back(static_cast<uint8_t>(std::min(luma, 255u)));
        }
    }
    return true;
}

}  // namespace UltraCanvas
