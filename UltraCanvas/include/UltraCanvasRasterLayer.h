// include/UltraCanvasRasterLayer.h
// One editable bitmap layer of a raster document: a straight (non-
// premultiplied) 8-bit RGBA pixel buffer plus the per-layer attributes a
// bitmap editor keeps (name, visibility, opacity, blend mode, lock).
//
// This is the unit every painting operation works on. The brush engine
// (UltraCanvasBrushEngine.h) writes into it, the document
// (UltraCanvasRasterDocument.h) stacks several of them and composites the
// stack into a UCPixmap for display, and PixelFX filters run on it through
// the ToPixelFX() / FromPixelFX() round trip.
//
// Pixel layout is R, G, B, A bytes in that order, row-major, no padding —
// the layout libvips uses for a 4-band uchar sRGB image, so the PixelFX
// round trip is a plain memory copy. Alpha is straight, not premultiplied:
// a half-transparent red pixel is (255, 0, 0, 128). Conversion to Cairo's
// premultiplied ARGB32 happens once, in CompositeOnto().
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace PixelFX { class PFXImage; }

namespace UltraCanvas {

// ===== LAYER BLEND MODES =====
// How a layer combines with everything below it. The arithmetic is the
// usual separable formulas (Photoshop / W3C compositing), applied in
// straight RGB before the alpha "over" step.
enum class RasterBlendMode {
    Normal = 0,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    Difference,
    Addition,
    Subtract,
    SoftLight,
    HardLight
};

const char* RasterBlendModeName(RasterBlendMode mode);
// Every mode in menu order, for a dropdown.
const std::vector<RasterBlendMode>& AllRasterBlendModes();

// ===== ONE 8-BIT RGBA PIXEL =====
struct RasterPixel {
    uint8_t r = 0, g = 0, b = 0, a = 0;
    RasterPixel() = default;
    RasterPixel(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}
    explicit RasterPixel(const Color& c) : r(c.r), g(c.g), b(c.b), a(c.a) {}
    Color ToColor() const { return Color(r, g, b, a); }
    bool operator==(const RasterPixel& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const RasterPixel& o) const { return !(*this == o); }
};

// ===== A RASTER LAYER =====
class UCRasterLayer {
public:
    UCRasterLayer() = default;
    UCRasterLayer(int width, int height, const std::string& name = "Layer");
    UCRasterLayer(int width, int height, const RasterPixel& fill, const std::string& name = "Layer");

    // ===== GEOMETRY =====
    int  GetWidth()  const { return width; }
    int  GetHeight() const { return height; }
    bool IsValid()   const { return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width) * height * 4; }
    Rect2Di GetRect() const { return Rect2Di(0, 0, width, height); }
    // Clamp a rectangle to the layer; an empty rectangle comes back for one
    // that lies entirely outside.
    Rect2Di ClipRect(const Rect2Di& r) const;

    // ===== PIXEL ACCESS =====
    // Raw straight-RGBA buffer, 4 bytes per pixel, width*4 bytes per row.
    uint8_t*       Data()       { return pixels.data(); }
    const uint8_t* Data() const { return pixels.data(); }
    size_t         DataSize() const { return pixels.size(); }
    uint8_t*       Row(int y)       { return pixels.data() + static_cast<size_t>(y) * width * 4; }
    const uint8_t* Row(int y) const { return pixels.data() + static_cast<size_t>(y) * width * 4; }
    RasterPixel GetPixel(int x, int y) const;           // transparent black outside
    void        SetPixel(int x, int y, const RasterPixel& p);  // ignored outside

    // ===== WHOLE-LAYER EDITS =====
    void Fill(const RasterPixel& p);
    void FillRect(const Rect2Di& r, const RasterPixel& p);
    void Clear() { Fill(RasterPixel(0, 0, 0, 0)); }
    // Copies `src`'s pixels over this layer at (dx, dy); source pixels are
    // written as-is (no blending). Clipped to both layers.
    void CopyFrom(const UCRasterLayer& src, int dx, int dy);
    void CopyRectFrom(const UCRasterLayer& src, const Rect2Di& srcRect, int dx, int dy);
    // Composites `src` over this layer at (dx, dy) with source-over.
    void BlendFrom(const UCRasterLayer& src, int dx, int dy, float opacity = 1.0f,
                   RasterBlendMode mode = RasterBlendMode::Normal);
    // A new layer holding just `r` (clipped); the layer's attributes carry over.
    std::shared_ptr<UCRasterLayer> CropCopy(const Rect2Di& r) const;
    std::shared_ptr<UCRasterLayer> Clone() const;
    // Re-shape the canvas: pixels keep their position relative to the new
    // origin `(offsetX, offsetY)`; exposed area is transparent.
    void ResizeCanvas(int newWidth, int newHeight, int offsetX, int offsetY);
    // Geometry that needs no resampling.
    void FlipHorizontal();
    void FlipVertical();
    void Rotate90(bool clockwise);
    void Rotate180();
    // Simple bilinear resample (used when libvips is not available; the
    // document prefers PixelFX for quality).
    void ResampleBilinear(int newWidth, int newHeight);

    // ===== ANALYSIS =====
    // Bounding rectangle of the non-transparent pixels; empty when the layer
    // is fully transparent.
    Rect2Di GetOpaqueBounds() const;
    bool IsFullyOpaque() const;

    // ===== COMPOSITING =====
    // Composites `srcRect` of this layer over a premultiplied ARGB32 buffer
    // (Cairo's native pixmap layout, one uint32 per pixel, `dstStride`
    // pixels per row) at the same coordinates, honouring `opacity` (0..1)
    // and `mode`. `mask`, when given, is a width*height byte coverage map
    // (255 = fully affected) that further scales the layer's alpha — the
    // document passes the selection here for "show selection only" previews.
    void CompositeOnto(uint32_t* dst, int dstWidth, int dstHeight, int dstStride,
                       const Rect2Di& srcRect, float opacity, RasterBlendMode mode) const;

    // ===== PIXELFX ROUND TRIP =====
    // Only in builds with libvips (HAS_LIBVIPS) — the same guard
    // UCImageRaster::GetVImage() uses, so a build without libvips keeps the
    // layer, the brush engine and the document, and just has no filters.
#ifdef HAS_LIBVIPS
    // A 4-band uchar sRGB PixelFX image sharing nothing with this layer (the
    // pixels are copied, so the layer can keep changing).
    PixelFX::PFXImage ToPixelFX() const;
    // Replaces the pixels with `img`, cast to 8-bit RGBA (grey and RGB
    // images get an opaque alpha band; extra bands are dropped). The layer
    // takes `img`'s size. Returns false when the image cannot be
    // materialised.
    bool FromPixelFX(const PixelFX::PFXImage& img);
#endif

    // ===== ATTRIBUTES =====
    std::string     name    = "Layer";
    bool            visible = true;
    bool            locked  = false;      // painting tools refuse to touch it
    float           opacity = 1.0f;       // 0..1
    RasterBlendMode blendMode = RasterBlendMode::Normal;

private:
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

// ===== BLEND ARITHMETIC =====
// Blend one straight-RGB source channel value onto a destination channel
// value with `mode`; both 0..255. Exposed so the brush engine can paint in
// blend modes with the same formulas the layer compositor uses.
uint8_t RasterBlendChannel(RasterBlendMode mode, uint8_t src, uint8_t dst);

// Source-over one straight-RGBA pixel onto another (both straight), with
// `coverage` (0..1) scaling the source alpha. Returns the straight result.
RasterPixel RasterBlendPixel(const RasterPixel& src, const RasterPixel& dst,
                             float coverage, RasterBlendMode mode = RasterBlendMode::Normal);

} // namespace UltraCanvas
