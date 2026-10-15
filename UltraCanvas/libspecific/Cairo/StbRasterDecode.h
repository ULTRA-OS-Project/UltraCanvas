// libspecific/Cairo/StbRasterDecode.h
// Version: 1.0.0 - JPEG, BMP and GIF decoded by the bundled stb_image, for a
//                  build without libvips
//
// What UCImageRaster reads when there is no libvips to read it: the bytes
// of a JPEG, BMP or GIF (first frame) become a cairo ARGB32 surface through
// the bundled stb_image (third_party/stb). PNG is cairo's own reader's.
// Compiled to nothing in a build with libvips.
#pragma once

#include <cstddef>
#include <cstdint>

#include <cairo.h>

namespace UltraCanvas {
namespace StbRaster {

    // Whether stb_image reads these bytes (a JPEG, BMP or GIF signature).
    bool Recognises(const uint8_t* data, size_t size);

    // Width, height and whether the picture carries alpha, from the header
    // alone. False when stb_image does not read the bytes.
    bool ReadHeader(const uint8_t* data, size_t size, int& width, int& height, bool& alpha);

    // The picture decoded to a new ARGB32 surface (premultiplied, as cairo
    // wants it), or nullptr. The caller owns the surface.
    cairo_surface_t* Decode(const uint8_t* data, size_t size);

}   // namespace StbRaster
}   // namespace UltraCanvas
