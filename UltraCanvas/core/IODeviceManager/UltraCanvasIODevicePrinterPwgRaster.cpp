// core/IODeviceManager/UltraCanvasIODevicePrinterPwgRaster.cpp
// The PWG raster page header, its line compression, and the page transforms
// landscape pages and duplex back sides need. See the header.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODevicePrinterPwgRaster.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace UltraCanvas {

namespace {

// Big-endian, which is what "RaS2" (as opposed to "2SaR") declares.
void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void PutInt(std::vector<uint8_t>& out, int value) {
    PutU32(out, static_cast<uint32_t>(value < 0 ? 0 : value));
}

void PutCString(std::vector<uint8_t>& out, const std::string& text, size_t width) {
    const size_t copied = std::min(text.size(), width - 1);
    out.insert(out.end(), text.begin(), text.begin() + static_cast<long>(copied));
    out.insert(out.end(), width - copied, 0);
}

void PutZeros(std::vector<uint8_t>& out, size_t count) {
    out.insert(out.end(), count, 0);
}

// PWG's CrossFeedTransform / FeedTransform: 1 as drawn, -1 mirrored.
constexpr uint32_t kTransformNormal = 1;
constexpr uint32_t kTransformMirrored = 0xFFFFFFFFu;

// Appends one line's runs. A repeated run is `count - 1` then the pixel; a
// literal run is `257 - count` then the pixels. A lone pixel goes out as a
// repeat of one, since 257 - 1 does not fit in a byte.
void CompressLine(const uint8_t* line, int width, int bytesPerPixel,
                  std::vector<uint8_t>& out) {
    const size_t pixelBytes = static_cast<size_t>(bytesPerPixel);
    auto pixel = [&](int x) { return line + static_cast<size_t>(x) * pixelBytes; };
    auto same = [&](int a, int b) {
        return std::memcmp(pixel(a), pixel(b), pixelBytes) == 0;
    };

    int x = 0;
    while (x < width) {
        // A run of identical pixels starting here?
        int run = 1;
        while (x + run < width && run < 128 && same(x, x + run)) ++run;

        if (run >= 2 || x + 1 == width) {
            out.push_back(static_cast<uint8_t>(run - 1));
            out.insert(out.end(), pixel(x), pixel(x) + pixelBytes);
            x += run;
            continue;
        }

        // Literal pixels, up to the start of the next repeat.
        int count = 1;
        while (x + count < width && count < 128) {
            if (x + count + 1 < width && same(x + count, x + count + 1)) break;
            ++count;
        }
        if (count == 1) {
            out.push_back(0);
        } else {
            out.push_back(static_cast<uint8_t>(257 - count));
        }
        out.insert(out.end(), pixel(x), pixel(x) + static_cast<size_t>(count) * pixelBytes);
        x += count;
    }
}

}  // namespace

void AppendPwgRasterSync(std::vector<uint8_t>& out) {
    const char sync[] = {'R', 'a', 'S', '2'};
    out.insert(out.end(), sync, sync + 4);
}

bool AppendPwgRasterPageHeader(const IOPwgRasterPage& page, std::vector<uint8_t>& out) {
    if (!page.IsValid()) return false;

    const size_t start = out.size();

    PutCString(out, "PwgRaster", 64);   // PwgRaster (MediaClass)
    PutCString(out, std::string(), 64); // MediaColor
    PutCString(out, std::string(), 64); // MediaType
    PutCString(out, std::string(), 64); // PrintContentOptimize (OutputType)

    PutZeros(out, 3 * 4);               // reserved (AdvanceDistance, AdvanceMedia, Collate)
    PutInt(out, 0);                     // CutMedia
    PutInt(out, page.duplex ? 1 : 0);   // Duplex
    PutInt(out, page.dpiX);             // HWResolution[0]
    PutInt(out, page.dpiY);             // HWResolution[1]
    PutZeros(out, 4 * 4);               // reserved (ImagingBoundingBox)
    PutInt(out, 0);                     // InsertSheet
    PutInt(out, 0);                     // Jog
    PutInt(out, 0);                     // LeadingEdge
    PutZeros(out, 3 * 4);               // reserved (Margins, ManualFeed)
    PutInt(out, 0);                     // MediaPosition
    PutInt(out, 0);                     // MediaWeightMetric
    PutZeros(out, 2 * 4);               // reserved (MirrorPrint, NegativePrint)

    // 1, not the job's copy count: the Print-Job request asks for copies, and
    // saying it in both places would print the job squared.
    PutInt(out, 1);                     // NumCopies
    PutInt(out, 0);                     // Orientation
    PutZeros(out, 4);                   // reserved (OutputFaceUp)
    PutInt(out, page.pageWidthPoints);  // PageSize[0]
    PutInt(out, page.pageHeightPoints); // PageSize[1]
    PutZeros(out, 2 * 4);               // reserved (Separations, TraySwitch)
    PutInt(out, page.tumble ? 1 : 0);   // Tumble

    PutInt(out, page.widthPixels);      // Width
    PutInt(out, page.heightPixels);     // Height
    PutZeros(out, 4);                   // reserved (cupsMediaType)
    PutInt(out, 8);                     // BitsPerColor
    PutInt(out, page.BitsPerPixel());   // BitsPerPixel
    PutInt(out, page.BytesPerLine());   // BytesPerLine
    PutInt(out, 0);                     // ColorOrder: chunky
    PutInt(out, static_cast<int>(page.colorSpace));   // ColorSpace
    PutZeros(out, 4 * 4);               // reserved (compression, row count/feed/step)
    PutInt(out, page.NumColors());      // NumColors
    PutZeros(out, 7 * 4);               // reserved (scaling factor, page size, bbox)

    // cupsInteger[16], which PWG names field by field.
    PutInt(out, page.totalPageCount);   // TotalPageCount
    PutU32(out, page.crossFeedMirrored ? kTransformMirrored : kTransformNormal);
    PutU32(out, page.feedMirrored ? kTransformMirrored : kTransformNormal);
    PutInt(out, 0);                     // ImageBoxLeft
    PutInt(out, 0);                     // ImageBoxTop
    PutInt(out, page.widthPixels);      // ImageBoxRight
    PutInt(out, page.heightPixels);     // ImageBoxBottom
    PutInt(out, 0);                     // AlternatePrimary
    PutInt(out, page.printQuality);     // PrintQuality
    PutZeros(out, 5 * 4);               // reserved
    PutInt(out, 0);                     // VendorIdentifier
    PutInt(out, 0);                     // VendorLength

    PutZeros(out, 1088);                // VendorData
    PutZeros(out, 64);                  // reserved (cupsMarkerType)
    PutCString(out, std::string(), 64); // RenderingIntent: the printer's default
    PutCString(out, page.pageSizeName, 64);   // PageSizeName

    if (out.size() - start != kPwgRasterHeaderBytes) {
        out.resize(start);
        return false;
    }
    return true;
}

bool AppendPwgRasterPageLines(const IOPwgRasterPage& page, const uint8_t* pixels,
                              size_t pixelBytes, std::vector<uint8_t>& out) {
    if (!page.IsValid() || !pixels) return false;
    const size_t lineBytes = static_cast<size_t>(page.BytesPerLine());
    if (pixelBytes != lineBytes * static_cast<size_t>(page.heightPixels)) return false;

    int y = 0;
    while (y < page.heightPixels) {
        const uint8_t* line = pixels + static_cast<size_t>(y) * lineBytes;

        // Identical lines below this one, up to the 256 one count byte holds.
        int repeats = 1;
        while (y + repeats < page.heightPixels && repeats < 256 &&
               std::memcmp(line, line + static_cast<size_t>(repeats) * lineBytes, lineBytes) == 0) {
            ++repeats;
        }

        out.push_back(static_cast<uint8_t>(repeats - 1));
        CompressLine(line, page.widthPixels, page.NumColors(), out);
        y += repeats;
    }
    return true;
}

IOPwgBackSide IOPwgBackSideTransform(const std::string& sheetBack, bool shortEdge) {
    std::string keyword;
    for (char c : sheetBack) {
        keyword.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    IOPwgBackSide back;
    if (keyword == "flipped") {
        if (shortEdge) back.mirrorCrossFeed = true;
        else back.mirrorFeed = true;
    } else if (keyword == "rotated") {
        if (!shortEdge) back.mirrorCrossFeed = back.mirrorFeed = true;
    } else if (keyword == "manual-tumble") {
        if (shortEdge) back.mirrorCrossFeed = back.mirrorFeed = true;
    }
    return back;
}

std::vector<uint8_t> IOPwgTransformPixels(const std::vector<uint8_t>& pixels,
                                          int& width, int& height,
                                          int bytesPerPixel, int quarterTurns,
                                          bool mirrorX, bool mirrorY) {
    quarterTurns = ((quarterTurns % 4) + 4) % 4;
    const int sourceWidth = width;
    const int sourceHeight = height;
    const size_t bpp = static_cast<size_t>(bytesPerPixel);

    const bool sideways = quarterTurns % 2 == 1;
    const int outWidth = sideways ? sourceHeight : sourceWidth;
    const int outHeight = sideways ? sourceWidth : sourceHeight;

    std::vector<uint8_t> out(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * bpp);

    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            // Where this output pixel comes from. The mirror is applied to
            // the turned page, so undo it first, then undo the turn.
            const int mx = mirrorX ? outWidth - 1 - x : x;
            const int my = mirrorY ? outHeight - 1 - y : y;

            int sx = mx;
            int sy = my;
            switch (quarterTurns) {
                case 1:     // counter-clockwise: the source's top edge is now the left
                    sx = sourceWidth - 1 - my;
                    sy = mx;
                    break;
                case 2:
                    sx = sourceWidth - 1 - mx;
                    sy = sourceHeight - 1 - my;
                    break;
                case 3:     // clockwise: the source's top edge is now the right
                    sx = my;
                    sy = sourceHeight - 1 - mx;
                    break;
                default:
                    break;
            }

            const uint8_t* from = pixels.data() +
                (static_cast<size_t>(sy) * static_cast<size_t>(sourceWidth) +
                 static_cast<size_t>(sx)) * bpp;
            uint8_t* to = out.data() +
                (static_cast<size_t>(y) * static_cast<size_t>(outWidth) +
                 static_cast<size_t>(x)) * bpp;
            std::memcpy(to, from, bpp);
        }
    }

    width = outWidth;
    height = outHeight;
    return out;
}

}  // namespace UltraCanvas
