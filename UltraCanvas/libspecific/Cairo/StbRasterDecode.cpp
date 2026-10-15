// libspecific/Cairo/StbRasterDecode.cpp
// Version: 1.0.0 - JPEG, BMP and GIF decoded by the bundled stb_image, for a
//                  build without libvips
#include "StbRasterDecode.h"

#ifndef HAS_LIBVIPS

#include <cstring>

// JPEG, BMP and GIF only: PNG is cairo's, and the rest (PSD, TGA, HDR, PIC,
// PNM) nobody hands a UI toolkit. No stdio: the bytes are always in memory.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "../../third_party/stb/stb_image.h"

namespace UltraCanvas {
namespace StbRaster {

    bool Recognises(const uint8_t* data, size_t size) {
        if (!data || size < 6) return false;
        if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return true;          // JPEG
        if (data[0] == 'B' && data[1] == 'M') return true;                                 // BMP
        if (std::memcmp(data, "GIF87a", 6) == 0 || std::memcmp(data, "GIF89a", 6) == 0) return true;
        return false;
    }

    bool ReadHeader(const uint8_t* data, size_t size, int& width, int& height, bool& alpha) {
        if (!Recognises(data, size)) return false;
        int w = 0, h = 0, channels = 0;
        if (!stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &channels)) return false;
        if (w <= 0 || h <= 0) return false;
        width = w;
        height = h;
        alpha = channels == 2 || channels == 4;
        return true;
    }

    cairo_surface_t* Decode(const uint8_t* data, size_t size) {
        if (!Recognises(data, size)) return nullptr;
        int w = 0, h = 0, channels = 0;
        // Four channels whatever the file has: RGBA, straight alpha.
        stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
        if (!pixels) return nullptr;
        if (w <= 0 || h <= 0) {
            stbi_image_free(pixels);
            return nullptr;
        }
        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(surface);
            stbi_image_free(pixels);
            return nullptr;
        }
        cairo_surface_flush(surface);
        unsigned char* out = cairo_image_surface_get_data(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        for (int y = 0; y < h; ++y) {
            const stbi_uc* src = pixels + static_cast<size_t>(y) * w * 4;
            auto* dst = reinterpret_cast<uint32_t*>(out + static_cast<size_t>(y) * stride);
            for (int x = 0; x < w; ++x, src += 4) {
                // cairo's ARGB32 is premultiplied and native-endian: one
                // 32-bit word per pixel, alpha in the top byte.
                const uint32_t a = src[3];
                const uint32_t r = (src[0] * a + 127) / 255;
                const uint32_t g = (src[1] * a + 127) / 255;
                const uint32_t b = (src[2] * a + 127) / 255;
                dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        cairo_surface_mark_dirty(surface);
        stbi_image_free(pixels);
        return surface;
    }

}   // namespace StbRaster
}   // namespace UltraCanvas

#endif  // !HAS_LIBVIPS
