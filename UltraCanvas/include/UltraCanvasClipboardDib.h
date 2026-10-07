// include/UltraCanvasClipboardDib.h
// Device-independent bitmaps - the pixels the Windows clipboard carries as
// CF_DIB and CF_DIBV5 - converted to and from PNG, which is the form every
// clipboard backend hands an image to the framework in. Plain byte work with
// no Win32 types, so it builds and is tested on every platform.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace UltraCanvas::ClipboardDib {

    // An image as straight (not premultiplied) RGBA, rows top to bottom.
    struct RgbaImage {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> pixels;   // width * height * 4: R, G, B, A

        bool IsValid() const {
            return width > 0 && height > 0 &&
                   pixels.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        }
    };

    // A DIB as the clipboard holds it: a BITMAPINFOHEADER (40 bytes) or a
    // larger version of it (V4 108, V5 124, ...) or a BITMAPCOREHEADER (12),
    // then the colour masks or the palette, then the pixels - no
    // BITMAPFILEHEADER in front. Reads 1/4/8-bit palettes, 16/24/32-bit BI_RGB
    // and BI_BITFIELDS, BI_PNG, and rows stored either way up. Alpha is taken
    // only from a bit-field DIB whose header names an alpha mask (a BI_RGB
    // pixel's fourth byte is padding), and a picture whose alpha is zero
    // everywhere is read as opaque: plenty of programs leave that byte zero,
    // and believing it would paste an invisible image.
    bool DecodeDib(const uint8_t* data, size_t size, RgbaImage& image);

    // CF_DIBV5: BITMAPV5HEADER, 32 bits per pixel, BI_BITFIELDS with the masks
    // (alpha included) in the header, straight alpha, sRGB, rows bottom-up.
    // What alpha-aware programs read.
    std::vector<uint8_t> EncodeDibV5(const RgbaImage& image);

    // CF_DIB: BITMAPINFOHEADER, 24 bits per pixel, BI_RGB, transparency
    // flattened onto white. Most programs ignore alpha in a plain DIB and
    // would otherwise show every transparent pixel as black.
    std::vector<uint8_t> EncodeDib24(const RgbaImage& image);

    // PNG through cairo. Both return false when this cairo was built without
    // PNG support.
    bool DecodePng(const uint8_t* data, size_t size, RgbaImage& image);
    bool EncodePng(const RgbaImage& image, std::vector<uint8_t>& png);

    // True when data starts with the eight-byte PNG signature.
    bool IsPng(const uint8_t* data, size_t size);

    // Length of the PNG stream up to and including its IEND chunk, or 0 when
    // the data is not a complete PNG. A clipboard memory block is often larger
    // than what was put into it (GlobalSize rounds up); the rest is padding.
    size_t PngStreamLength(const uint8_t* data, size_t size);

    // The DIB inside a .bmp file: the bytes after its 14-byte
    // BITMAPFILEHEADER. Empty when data is not a .bmp file.
    std::vector<uint8_t> DibOfBmpFile(const uint8_t* data, size_t size);

    // DecodeDib then EncodePng.
    bool DibToPng(const uint8_t* data, size_t size, std::vector<uint8_t>& png);

} // namespace UltraCanvas::ClipboardDib
