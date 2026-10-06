// core/UltraCanvasClipboardDib.cpp
// Device-independent bitmaps (CF_DIB / CF_DIBV5) to and from PNG.
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasClipboardDib.h"

#include <cairo/cairo.h>

#include <algorithm>
#include <cstring>

namespace UltraCanvas::ClipboardDib {

    namespace {
        // Header compression values (wingdi.h), spelled out so this file
        // needs no Windows header.
        constexpr uint32_t kBiRgb = 0;
        constexpr uint32_t kBiBitfields = 3;
        constexpr uint32_t kBiPng = 5;
        constexpr uint32_t kBiAlphaBitfields = 6;
        constexpr uint32_t kLcsSrgb = 0x73524742;   // 'sRGB'
        constexpr uint32_t kLcsGmImages = 4;

        constexpr size_t kInfoHeaderSize = 40;
        constexpr size_t kV5HeaderSize = 124;
        constexpr size_t kCoreHeaderSize = 12;

        // Big enough for any picture a person copies, small enough that a
        // corrupt header cannot ask for gigabytes.
        constexpr uint64_t kMaxPixels = uint64_t(1) << 28;
        constexpr int kMaxSide = 32767;   // cairo's image surface limit

        uint16_t ReadU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
        uint32_t ReadU32(const uint8_t* p) {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }
        int32_t ReadI32(const uint8_t* p) { return static_cast<int32_t>(ReadU32(p)); }

        void PutU16(std::vector<uint8_t>& out, size_t at, uint16_t v) {
            out[at] = static_cast<uint8_t>(v);
            out[at + 1] = static_cast<uint8_t>(v >> 8);
        }
        void PutU32(std::vector<uint8_t>& out, size_t at, uint32_t v) {
            for (int i = 0; i < 4; ++i) out[at + i] = static_cast<uint8_t>(v >> (8 * i));
        }

        // One channel of a bit-field pixel scaled to 0..255.
        struct Channel {
            uint32_t mask = 0;
            int shift = 0;
            int bits = 0;

            explicit Channel(uint32_t m = 0) : mask(m) {
                if (!mask) return;
                while (!((mask >> shift) & 1u)) ++shift;
                while (shift + bits < 32 && ((mask >> (shift + bits)) & 1u)) ++bits;
            }
            uint8_t Of(uint32_t value) const {
                if (!bits) return 0;
                const uint32_t v = (value & mask) >> shift;
                if (bits >= 8) return static_cast<uint8_t>(v >> (bits - 8));
                return static_cast<uint8_t>((v * 255u + ((1u << bits) - 1) / 2) / ((1u << bits) - 1));
            }
        };

        bool SizeAllowed(int64_t width, int64_t height) {
            return width > 0 && height > 0 && width <= kMaxSide && height <= kMaxSide &&
                   static_cast<uint64_t>(width) * static_cast<uint64_t>(height) <= kMaxPixels;
        }

        uint8_t FlattenOnWhite(uint8_t c, uint8_t a) {
            return static_cast<uint8_t>((c * a + 255 * (255 - a) + 127) / 255);
        }

#ifdef CAIRO_HAS_PNG_FUNCTIONS
        struct ReadCursor {
            const uint8_t* data;
            size_t size;
            size_t at;
        };

        cairo_status_t ReadPngBytes(void* closure, unsigned char* out, unsigned int length) {
            auto* cursor = static_cast<ReadCursor*>(closure);
            if (cursor->size - cursor->at < length) return CAIRO_STATUS_READ_ERROR;
            std::memcpy(out, cursor->data + cursor->at, length);
            cursor->at += length;
            return CAIRO_STATUS_SUCCESS;
        }

        cairo_status_t WritePngBytes(void* closure, const unsigned char* data, unsigned int length) {
            auto* out = static_cast<std::vector<uint8_t>*>(closure);
            out->insert(out->end(), data, data + length);
            return CAIRO_STATUS_SUCCESS;
        }
#endif
    }

    bool IsPng(const uint8_t* data, size_t size) {
        static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        return data && size >= 8 && std::memcmp(data, kSignature, 8) == 0;
    }

    size_t PngStreamLength(const uint8_t* data, size_t size) {
        if (!IsPng(data, size)) return 0;
        size_t at = 8;
        while (size - at >= 12) {
            const uint32_t length = (static_cast<uint32_t>(data[at]) << 24) |
                                    (static_cast<uint32_t>(data[at + 1]) << 16) |
                                    (static_cast<uint32_t>(data[at + 2]) << 8) |
                                    static_cast<uint32_t>(data[at + 3]);
            if (length > size - at - 12) return 0;
            const bool end = std::memcmp(data + at + 4, "IEND", 4) == 0;
            at += 12 + length;
            if (end) return at;
        }
        return 0;
    }

    std::vector<uint8_t> DibOfBmpFile(const uint8_t* data, size_t size) {
        constexpr size_t kFileHeaderSize = 14;
        if (!data || size <= kFileHeaderSize + kCoreHeaderSize || data[0] != 'B' || data[1] != 'M') return {};
        const uint32_t headerSize = ReadU32(data + kFileHeaderSize);
        if (headerSize < kCoreHeaderSize || headerSize > size - kFileHeaderSize) return {};
        return std::vector<uint8_t>(data + kFileHeaderSize, data + size);
    }

    bool DecodeDib(const uint8_t* data, size_t size, RgbaImage& image) {
        if (!data || size < 4) return false;
        const uint32_t headerSize = ReadU32(data);

        int64_t width = 0, height = 0;
        int bpp = 0;
        uint32_t compression = kBiRgb;
        uint32_t colorsUsed = 0;
        uint32_t masks[4] = {0, 0, 0, 0};   // R, G, B, A
        bool haveMasks = false;
        size_t offset = headerSize;
        size_t paletteEntrySize = 4;

        if (headerSize == kCoreHeaderSize) {
            if (size < kCoreHeaderSize) return false;
            width = ReadU16(data + 4);
            height = ReadU16(data + 6);
            bpp = ReadU16(data + 10);
            paletteEntrySize = 3;
        } else if (headerSize >= kInfoHeaderSize && headerSize <= size) {
            width = ReadI32(data + 4);
            height = ReadI32(data + 8);
            bpp = ReadU16(data + 14);
            compression = ReadU32(data + 16);
            colorsUsed = ReadU32(data + 32);
            if (headerSize >= 52) {
                masks[0] = ReadU32(data + 40);
                masks[1] = ReadU32(data + 44);
                masks[2] = ReadU32(data + 48);
                haveMasks = compression == kBiBitfields || compression == kBiAlphaBitfields;
            }
            if (headerSize >= 56 && haveMasks) masks[3] = ReadU32(data + 52);
            if (headerSize == kInfoHeaderSize &&
                (compression == kBiBitfields || compression == kBiAlphaBitfields)) {
                // A plain info header keeps the masks after itself.
                const size_t count = compression == kBiAlphaBitfields ? 4 : 3;
                if (size < offset + 4 * count) return false;
                for (size_t i = 0; i < count; ++i) masks[i] = ReadU32(data + offset + 4 * i);
                offset += 4 * count;
                haveMasks = true;
            }
        } else {
            return false;
        }

        if (compression == kBiPng) {
            return offset < size && DecodePng(data + offset, size - offset, image);
        }
        if (compression != kBiRgb && compression != kBiBitfields && compression != kBiAlphaBitfields) {
            return false;   // RLE and JPEG DIBs: not something a clipboard carries in practice
        }

        const bool topDown = height < 0;
        if (topDown) height = -height;
        if (!SizeAllowed(width, height)) return false;

        // The palette (up to 8 bits a pixel), or an optional table of
        // colorsUsed entries that a deeper DIB may carry and nobody reads.
        std::vector<uint8_t> palette;   // R, G, B per entry
        if (bpp <= 8) {
            if (bpp != 1 && bpp != 4 && bpp != 8) return false;
            const uint32_t maxEntries = 1u << bpp;
            if (colorsUsed > maxEntries) return false;
            const uint32_t entries = colorsUsed ? colorsUsed : maxEntries;
            if (size < offset + entries * paletteEntrySize) return false;
            palette.resize(static_cast<size_t>(maxEntries) * 3, 0);
            for (uint32_t i = 0; i < entries; ++i) {
                const uint8_t* q = data + offset + i * paletteEntrySize;
                palette[i * 3] = q[2];
                palette[i * 3 + 1] = q[1];
                palette[i * 3 + 2] = q[0];
            }
            offset += entries * paletteEntrySize;
        } else {
            if (bpp != 16 && bpp != 24 && bpp != 32) return false;
            if (colorsUsed && colorsUsed <= 256) offset += colorsUsed * paletteEntrySize;
        }

        if (bpp == 16 && !haveMasks) {
            masks[0] = 0x7C00; masks[1] = 0x03E0; masks[2] = 0x001F;   // 5-5-5
        } else if (bpp == 32 && !haveMasks) {
            masks[0] = 0x00FF0000; masks[1] = 0x0000FF00; masks[2] = 0x000000FF;
        }
        // Alpha only from bit fields: in a BI_RGB pixel the fourth byte is
        // padding whatever a larger header's alpha field holds, and the DIBV5
        // Windows makes from a program's 32-bit CF_DIB keeps whatever that
        // program left there. Every writer that means alpha uses bit fields.
        if (bpp == 24 || !haveMasks) masks[3] = 0;

        const uint64_t stride = ((static_cast<uint64_t>(width) * bpp + 31) / 32) * 4;
        if (offset > size || stride * static_cast<uint64_t>(height) > size - offset) return false;

        const int w = static_cast<int>(width), h = static_cast<int>(height);
        image.width = w;
        image.height = h;
        image.pixels.assign(static_cast<size_t>(w) * h * 4, 255);

        const Channel red(masks[0]), green(masks[1]), blue(masks[2]), alpha(masks[3]);
        bool anyAlpha = false;
        for (int row = 0; row < h; ++row) {
            const uint8_t* src = data + offset + static_cast<size_t>(row) * stride;
            uint8_t* dst = image.pixels.data() + static_cast<size_t>(topDown ? row : h - 1 - row) * w * 4;
            for (int x = 0; x < w; ++x, dst += 4) {
                if (bpp <= 8) {
                    const int perByte = 8 / bpp;
                    const uint8_t byte = src[x / perByte];
                    const int shift = 8 - bpp * (x % perByte + 1);
                    const uint32_t index = (byte >> shift) & ((1u << bpp) - 1);
                    dst[0] = palette[index * 3];
                    dst[1] = palette[index * 3 + 1];
                    dst[2] = palette[index * 3 + 2];
                } else if (bpp == 24) {
                    const uint8_t* p = src + x * 3;
                    dst[0] = p[2];
                    dst[1] = p[1];
                    dst[2] = p[0];
                } else {
                    const uint32_t v = bpp == 16 ? ReadU16(src + x * 2) : ReadU32(src + x * 4);
                    dst[0] = red.Of(v);
                    dst[1] = green.Of(v);
                    dst[2] = blue.Of(v);
                    if (masks[3]) {
                        dst[3] = alpha.Of(v);
                        anyAlpha = anyAlpha || dst[3] != 0;
                    }
                }
            }
        }
        if (masks[3] && !anyAlpha) {
            for (size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;
        }
        return true;
    }

    std::vector<uint8_t> EncodeDibV5(const RgbaImage& image) {
        if (!image.IsValid()) return {};
        const size_t w = static_cast<size_t>(image.width), h = static_cast<size_t>(image.height);
        const size_t pixelBytes = w * h * 4;
        std::vector<uint8_t> out(kV5HeaderSize + pixelBytes, 0);
        PutU32(out, 0, static_cast<uint32_t>(kV5HeaderSize));
        PutU32(out, 4, static_cast<uint32_t>(image.width));
        PutU32(out, 8, static_cast<uint32_t>(image.height));       // positive: bottom-up
        PutU16(out, 12, 1);                                          // planes
        PutU16(out, 14, 32);                                         // bits per pixel
        PutU32(out, 16, kBiBitfields);
        PutU32(out, 20, static_cast<uint32_t>(pixelBytes));
        PutU32(out, 40, 0x00FF0000);                                 // red mask
        PutU32(out, 44, 0x0000FF00);                                 // green mask
        PutU32(out, 48, 0x000000FF);                                 // blue mask
        PutU32(out, 52, 0xFF000000);                                 // alpha mask
        PutU32(out, 56, kLcsSrgb);
        PutU32(out, 108, kLcsGmImages);
        uint8_t* dst = out.data() + kV5HeaderSize;
        for (size_t row = 0; row < h; ++row) {
            const uint8_t* src = image.pixels.data() + (h - 1 - row) * w * 4;
            for (size_t x = 0; x < w; ++x, src += 4, dst += 4) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
            }
        }
        return out;
    }

    std::vector<uint8_t> EncodeDib24(const RgbaImage& image) {
        if (!image.IsValid()) return {};
        const size_t w = static_cast<size_t>(image.width), h = static_cast<size_t>(image.height);
        const size_t stride = (w * 3 + 3) & ~static_cast<size_t>(3);
        std::vector<uint8_t> out(kInfoHeaderSize + stride * h, 0);
        PutU32(out, 0, static_cast<uint32_t>(kInfoHeaderSize));
        PutU32(out, 4, static_cast<uint32_t>(image.width));
        PutU32(out, 8, static_cast<uint32_t>(image.height));
        PutU16(out, 12, 1);
        PutU16(out, 14, 24);
        PutU32(out, 16, kBiRgb);
        PutU32(out, 20, static_cast<uint32_t>(stride * h));
        for (size_t row = 0; row < h; ++row) {
            const uint8_t* src = image.pixels.data() + (h - 1 - row) * w * 4;
            uint8_t* dst = out.data() + kInfoHeaderSize + row * stride;
            for (size_t x = 0; x < w; ++x, src += 4, dst += 3) {
                dst[0] = FlattenOnWhite(src[2], src[3]);
                dst[1] = FlattenOnWhite(src[1], src[3]);
                dst[2] = FlattenOnWhite(src[0], src[3]);
            }
        }
        return out;
    }

    bool DecodePng(const uint8_t* data, size_t size, RgbaImage& image) {
#ifdef CAIRO_HAS_PNG_FUNCTIONS
        if (!IsPng(data, size)) return false;
        ReadCursor cursor{data, size, 0};
        cairo_surface_t* loaded = cairo_image_surface_create_from_png_stream(ReadPngBytes, &cursor);
        if (cairo_surface_status(loaded) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(loaded);
            return false;
        }
        const int w = cairo_image_surface_get_width(loaded);
        const int h = cairo_image_surface_get_height(loaded);
        if (!SizeAllowed(w, h)) {
            cairo_surface_destroy(loaded);
            return false;
        }
        // A 16-bit PNG comes back as a float surface on newer cairo, a grey
        // one as A8 or RGB24: paint whatever it is onto ARGB32.
        cairo_surface_t* argb = loaded;
        if (cairo_image_surface_get_format(loaded) != CAIRO_FORMAT_ARGB32 &&
            cairo_image_surface_get_format(loaded) != CAIRO_FORMAT_RGB24) {
            argb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            cairo_t* cr = cairo_create(argb);
            cairo_set_source_surface(cr, loaded, 0, 0);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_paint(cr);
            cairo_destroy(cr);
            cairo_surface_destroy(loaded);
        }
        cairo_surface_flush(argb);
        const bool opaque = cairo_image_surface_get_format(argb) == CAIRO_FORMAT_RGB24;
        const unsigned char* base = cairo_image_surface_get_data(argb);
        const int stride = cairo_image_surface_get_stride(argb);
        image.width = w;
        image.height = h;
        image.pixels.resize(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            const auto* src = reinterpret_cast<const uint32_t*>(base + static_cast<size_t>(y) * stride);
            uint8_t* dst = image.pixels.data() + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x, dst += 4) {
                const uint32_t v = src[x];
                const uint32_t a = opaque ? 255u : (v >> 24);
                uint32_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
                if (a && a < 255) {   // cairo stores premultiplied colour
                    r = std::min(255u, (r * 255 + a / 2) / a);
                    g = std::min(255u, (g * 255 + a / 2) / a);
                    b = std::min(255u, (b * 255 + a / 2) / a);
                }
                dst[0] = static_cast<uint8_t>(r);
                dst[1] = static_cast<uint8_t>(g);
                dst[2] = static_cast<uint8_t>(b);
                dst[3] = static_cast<uint8_t>(a);
            }
        }
        cairo_surface_destroy(argb);
        return true;
#else
        (void)data; (void)size; (void)image;
        return false;
#endif
    }

    bool EncodePng(const RgbaImage& image, std::vector<uint8_t>& png) {
#ifdef CAIRO_HAS_PNG_FUNCTIONS
        if (!image.IsValid() || !SizeAllowed(image.width, image.height)) return false;
        bool opaque = true;
        for (size_t i = 3; i < image.pixels.size() && opaque; i += 4) opaque = image.pixels[i] == 255;
        // RGB24 writes an RGB PNG: smaller, and what an opaque picture is.
        cairo_surface_t* surface = cairo_image_surface_create(
                opaque ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32, image.width, image.height);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(surface);
            return false;
        }
        cairo_surface_flush(surface);
        unsigned char* base = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        for (int y = 0; y < image.height; ++y) {
            auto* dst = reinterpret_cast<uint32_t*>(base + static_cast<size_t>(y) * stride);
            const uint8_t* src = image.pixels.data() + static_cast<size_t>(y) * image.width * 4;
            for (int x = 0; x < image.width; ++x, src += 4) {
                const uint32_t a = src[3];
                const uint32_t r = (src[0] * a + 127) / 255, g = (src[1] * a + 127) / 255, b = (src[2] * a + 127) / 255;
                dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        cairo_surface_mark_dirty(surface);
        png.clear();
        const cairo_status_t status = cairo_surface_write_to_png_stream(surface, WritePngBytes, &png);
        cairo_surface_destroy(surface);
        return status == CAIRO_STATUS_SUCCESS && !png.empty();
#else
        (void)image; (void)png;
        return false;
#endif
    }

    bool DibToPng(const uint8_t* data, size_t size, std::vector<uint8_t>& png) {
        RgbaImage image;
        return DecodeDib(data, size, image) && EncodePng(image, png);
    }

} // namespace UltraCanvas::ClipboardDib
