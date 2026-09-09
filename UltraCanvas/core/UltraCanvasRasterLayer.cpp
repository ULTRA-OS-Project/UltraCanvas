// core/UltraCanvasRasterLayer.cpp
// Straight-RGBA raster layer: pixel storage, whole-layer edits, blend
// arithmetic, compositing onto a premultiplied ARGB32 pixmap buffer and the
// PixelFX (libvips) round trip.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasRasterLayer.h"

#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace UltraCanvas {

// ===========================================================================
// BLEND MODES
// ===========================================================================

const char* RasterBlendModeName(RasterBlendMode mode) {
    switch (mode) {
        case RasterBlendMode::Normal:     return "Normal";
        case RasterBlendMode::Multiply:   return "Multiply";
        case RasterBlendMode::Screen:     return "Screen";
        case RasterBlendMode::Overlay:    return "Overlay";
        case RasterBlendMode::Darken:     return "Darken";
        case RasterBlendMode::Lighten:    return "Lighten";
        case RasterBlendMode::Difference: return "Difference";
        case RasterBlendMode::Addition:   return "Addition";
        case RasterBlendMode::Subtract:   return "Subtract";
        case RasterBlendMode::SoftLight:  return "Soft Light";
        case RasterBlendMode::HardLight:  return "Hard Light";
    }
    return "Normal";
}

const std::vector<RasterBlendMode>& AllRasterBlendModes() {
    static const std::vector<RasterBlendMode> modes = {
        RasterBlendMode::Normal, RasterBlendMode::Multiply, RasterBlendMode::Screen,
        RasterBlendMode::Overlay, RasterBlendMode::Darken, RasterBlendMode::Lighten,
        RasterBlendMode::Difference, RasterBlendMode::Addition, RasterBlendMode::Subtract,
        RasterBlendMode::SoftLight, RasterBlendMode::HardLight
    };
    return modes;
}

uint8_t RasterBlendChannel(RasterBlendMode mode, uint8_t s, uint8_t d) {
    const int S = s, D = d;
    int r;
    switch (mode) {
        case RasterBlendMode::Normal:     return s;
        case RasterBlendMode::Multiply:   r = (S * D + 127) / 255; break;
        case RasterBlendMode::Screen:     r = 255 - ((255 - S) * (255 - D) + 127) / 255; break;
        case RasterBlendMode::Overlay:
            r = (D < 128) ? (2 * S * D + 127) / 255
                          : 255 - (2 * (255 - S) * (255 - D) + 127) / 255;
            break;
        case RasterBlendMode::HardLight:
            r = (S < 128) ? (2 * S * D + 127) / 255
                          : 255 - (2 * (255 - S) * (255 - D) + 127) / 255;
            break;
        case RasterBlendMode::Darken:     r = std::min(S, D); break;
        case RasterBlendMode::Lighten:    r = std::max(S, D); break;
        case RasterBlendMode::Difference: r = std::abs(S - D); break;
        case RasterBlendMode::Addition:   r = S + D; break;
        case RasterBlendMode::Subtract:   r = D - S; break;
        case RasterBlendMode::SoftLight: {
            const double sf = S / 255.0, df = D / 255.0;
            double rf;
            if (sf <= 0.5) {
                rf = df - (1.0 - 2.0 * sf) * df * (1.0 - df);
            } else {
                const double g = (df <= 0.25) ? ((16.0 * df - 12.0) * df + 4.0) * df : std::sqrt(df);
                rf = df + (2.0 * sf - 1.0) * (g - df);
            }
            r = static_cast<int>(std::lround(rf * 255.0));
            break;
        }
        default: return s;
    }
    return static_cast<uint8_t>(std::clamp(r, 0, 255));
}

RasterPixel RasterBlendPixel(const RasterPixel& src, const RasterPixel& dst,
                             float coverage, RasterBlendMode mode) {
    const float sa = std::clamp(src.a / 255.0f * coverage, 0.0f, 1.0f);
    if (sa <= 0.0f) return dst;
    const float da = dst.a / 255.0f;
    // Blend-mode colour applies only where the destination has something to
    // blend with; over transparent destination the source colour shows as-is.
    uint8_t sr = src.r, sg = src.g, sb = src.b;
    if (mode != RasterBlendMode::Normal && da > 0.0f) {
        const uint8_t br = RasterBlendChannel(mode, src.r, dst.r);
        const uint8_t bg = RasterBlendChannel(mode, src.g, dst.g);
        const uint8_t bb = RasterBlendChannel(mode, src.b, dst.b);
        sr = static_cast<uint8_t>(std::lround(src.r + (br - src.r) * da));
        sg = static_cast<uint8_t>(std::lround(src.g + (bg - src.g) * da));
        sb = static_cast<uint8_t>(std::lround(src.b + (bb - src.b) * da));
    }
    const float oa = sa + da * (1.0f - sa);
    if (oa <= 0.0f) return RasterPixel(0, 0, 0, 0);
    auto ch = [&](uint8_t s, uint8_t d) -> uint8_t {
        const float v = (s * sa + d * da * (1.0f - sa)) / oa;
        return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
    };
    return RasterPixel(ch(sr, dst.r), ch(sg, dst.g), ch(sb, dst.b),
                       static_cast<uint8_t>(std::clamp(std::lround(oa * 255.0f), 0L, 255L)));
}

// ===========================================================================
// LAYER
// ===========================================================================

UCRasterLayer::UCRasterLayer(int w, int h, const std::string& layerName)
    : name(layerName), width(std::max(0, w)), height(std::max(0, h)),
      pixels(static_cast<size_t>(std::max(0, w)) * std::max(0, h) * 4, 0) {}

UCRasterLayer::UCRasterLayer(int w, int h, const RasterPixel& fill, const std::string& layerName)
    : UCRasterLayer(w, h, layerName) {
    Fill(fill);
}

Rect2Di UCRasterLayer::ClipRect(const Rect2Di& r) const {
    const int x0 = std::max(0, r.x);
    const int y0 = std::max(0, r.y);
    const int x1 = std::min(width, r.x + r.width);
    const int y1 = std::min(height, r.y + r.height);
    if (x1 <= x0 || y1 <= y0) return Rect2Di(0, 0, 0, 0);
    return Rect2Di(x0, y0, x1 - x0, y1 - y0);
}

RasterPixel UCRasterLayer::GetPixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return RasterPixel(0, 0, 0, 0);
    const uint8_t* p = Row(y) + static_cast<size_t>(x) * 4;
    return RasterPixel(p[0], p[1], p[2], p[3]);
}

void UCRasterLayer::SetPixel(int x, int y, const RasterPixel& px) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    uint8_t* p = Row(y) + static_cast<size_t>(x) * 4;
    p[0] = px.r; p[1] = px.g; p[2] = px.b; p[3] = px.a;
}

void UCRasterLayer::Fill(const RasterPixel& p) {
    FillRect(GetRect(), p);
}

void UCRasterLayer::FillRect(const Rect2Di& rIn, const RasterPixel& px) {
    const Rect2Di r = ClipRect(rIn);
    if (r.width <= 0) return;
    for (int y = r.y; y < r.y + r.height; ++y) {
        uint8_t* p = Row(y) + static_cast<size_t>(r.x) * 4;
        for (int x = 0; x < r.width; ++x, p += 4) {
            p[0] = px.r; p[1] = px.g; p[2] = px.b; p[3] = px.a;
        }
    }
}

void UCRasterLayer::CopyFrom(const UCRasterLayer& src, int dx, int dy) {
    CopyRectFrom(src, src.GetRect(), dx, dy);
}

void UCRasterLayer::CopyRectFrom(const UCRasterLayer& src, const Rect2Di& srcRectIn, int dx, int dy) {
    const Rect2Di sr = src.ClipRect(srcRectIn);
    if (sr.width <= 0) return;
    for (int y = 0; y < sr.height; ++y) {
        const int ty = dy + y;
        if (ty < 0 || ty >= height) continue;
        const int x0 = std::max(0, -dx);
        const int x1 = std::min(sr.width, width - dx);
        if (x1 <= x0) continue;
        std::memcpy(Row(ty) + static_cast<size_t>(dx + x0) * 4,
                    src.Row(sr.y + y) + static_cast<size_t>(sr.x + x0) * 4,
                    static_cast<size_t>(x1 - x0) * 4);
    }
}

void UCRasterLayer::BlendFrom(const UCRasterLayer& src, int dx, int dy, float opacity,
                              RasterBlendMode mode) {
    for (int y = 0; y < src.height; ++y) {
        const int ty = dy + y;
        if (ty < 0 || ty >= height) continue;
        const uint8_t* s = src.Row(y);
        for (int x = 0; x < src.width; ++x, s += 4) {
            const int tx = dx + x;
            if (tx < 0 || tx >= width) continue;
            if (s[3] == 0) continue;
            uint8_t* d = Row(ty) + static_cast<size_t>(tx) * 4;
            const RasterPixel out = RasterBlendPixel(RasterPixel(s[0], s[1], s[2], s[3]),
                                                     RasterPixel(d[0], d[1], d[2], d[3]),
                                                     opacity, mode);
            d[0] = out.r; d[1] = out.g; d[2] = out.b; d[3] = out.a;
        }
    }
}

std::shared_ptr<UCRasterLayer> UCRasterLayer::CropCopy(const Rect2Di& rIn) const {
    const Rect2Di r = ClipRect(rIn);
    auto out = std::make_shared<UCRasterLayer>(std::max(0, r.width), std::max(0, r.height), name);
    out->visible = visible; out->locked = locked; out->opacity = opacity; out->blendMode = blendMode;
    if (r.width > 0) out->CopyRectFrom(*this, r, 0, 0);
    return out;
}

std::shared_ptr<UCRasterLayer> UCRasterLayer::Clone() const {
    return std::make_shared<UCRasterLayer>(*this);
}

void UCRasterLayer::ResizeCanvas(int newWidth, int newHeight, int offsetX, int offsetY) {
    UCRasterLayer next(std::max(0, newWidth), std::max(0, newHeight), name);
    next.CopyFrom(*this, offsetX, offsetY);
    width = next.width; height = next.height; pixels.swap(next.pixels);
}

void UCRasterLayer::FlipHorizontal() {
    for (int y = 0; y < height; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(Row(y));
        std::reverse(row, row + width);
    }
}

void UCRasterLayer::FlipVertical() {
    const size_t stride = static_cast<size_t>(width) * 4;
    std::vector<uint8_t> tmp(stride);
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* a = Row(y);
        uint8_t* b = Row(height - 1 - y);
        std::memcpy(tmp.data(), a, stride);
        std::memcpy(a, b, stride);
        std::memcpy(b, tmp.data(), stride);
    }
}

void UCRasterLayer::Rotate90(bool clockwise) {
    UCRasterLayer next(height, width, name);
    for (int y = 0; y < height; ++y) {
        const uint8_t* s = Row(y);
        for (int x = 0; x < width; ++x, s += 4) {
            const int tx = clockwise ? (height - 1 - y) : y;
            const int ty = clockwise ? x : (width - 1 - x);
            uint8_t* d = next.Row(ty) + static_cast<size_t>(tx) * 4;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        }
    }
    width = next.width; height = next.height; pixels.swap(next.pixels);
}

void UCRasterLayer::Rotate180() {
    FlipHorizontal();
    FlipVertical();
}

void UCRasterLayer::ResampleBilinear(int newWidth, int newHeight) {
    newWidth = std::max(1, newWidth); newHeight = std::max(1, newHeight);
    if (!IsValid()) { *this = UCRasterLayer(newWidth, newHeight, name); return; }
    UCRasterLayer next(newWidth, newHeight, name);
    const double sx = static_cast<double>(width) / newWidth;
    const double sy = static_cast<double>(height) / newHeight;
    for (int y = 0; y < newHeight; ++y) {
        const double fy = std::clamp((y + 0.5) * sy - 0.5, 0.0, static_cast<double>(height - 1));
        const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, height - 1);
        const double wy = fy - y0;
        uint8_t* d = next.Row(y);
        for (int x = 0; x < newWidth; ++x, d += 4) {
            const double fx = std::clamp((x + 0.5) * sx - 0.5, 0.0, static_cast<double>(width - 1));
            const int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, width - 1);
            const double wx = fx - x0;
            const uint8_t* p00 = Row(y0) + static_cast<size_t>(x0) * 4;
            const uint8_t* p10 = Row(y0) + static_cast<size_t>(x1) * 4;
            const uint8_t* p01 = Row(y1) + static_cast<size_t>(x0) * 4;
            const uint8_t* p11 = Row(y1) + static_cast<size_t>(x1) * 4;
            // Weight colour by alpha so transparent neighbours do not bleed
            // their (meaningless) colour into the result.
            const double w00 = (1 - wx) * (1 - wy) * p00[3], w10 = wx * (1 - wy) * p10[3];
            const double w01 = (1 - wx) * wy * p01[3],       w11 = wx * wy * p11[3];
            const double wa = w00 + w10 + w01 + w11;
            const double a = (1 - wx) * (1 - wy) * p00[3] + wx * (1 - wy) * p10[3]
                           + (1 - wx) * wy * p01[3] + wx * wy * p11[3];
            for (int c = 0; c < 3; ++c) {
                const double v = wa > 0 ? (p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11) / wa : 0.0;
                d[c] = static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
            }
            d[3] = static_cast<uint8_t>(std::clamp(std::lround(a), 0L, 255L));
        }
    }
    width = next.width; height = next.height; pixels.swap(next.pixels);
}

Rect2Di UCRasterLayer::GetOpaqueBounds() const {
    int minX = width, minY = height, maxX = -1, maxY = -1;
    for (int y = 0; y < height; ++y) {
        const uint8_t* p = Row(y) + 3;
        for (int x = 0; x < width; ++x, p += 4) {
            if (*p) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0) return Rect2Di(0, 0, 0, 0);
    return Rect2Di(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

bool UCRasterLayer::IsFullyOpaque() const {
    const uint8_t* p = pixels.data() + 3;
    const size_t n = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < n; ++i, p += 4) if (*p != 255) return false;
    return true;
}

void UCRasterLayer::CompositeOnto(uint32_t* dst, int dstWidth, int dstHeight, int dstStride,
                                  const Rect2Di& srcRectIn, float opacity, RasterBlendMode mode) const {
    Rect2Di r = ClipRect(srcRectIn);
    r = Rect2Di(r.x, r.y, std::min(r.width, dstWidth - r.x), std::min(r.height, dstHeight - r.y));
    if (r.width <= 0 || r.height <= 0) return;
    const int op = static_cast<int>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
    if (op == 0) return;
    for (int y = r.y; y < r.y + r.height; ++y) {
        const uint8_t* s = Row(y) + static_cast<size_t>(r.x) * 4;
        uint32_t* d = dst + static_cast<size_t>(y) * dstStride + r.x;
        for (int x = 0; x < r.width; ++x, s += 4, ++d) {
            int sa = (s[3] * op + 127) / 255;
            if (sa == 0) continue;
            const uint32_t dv = *d;
            const int da = (dv >> 24) & 0xFF;
            // Destination is premultiplied: un-premultiply the colour for the
            // blend-mode formula, which wants straight values.
            int sr = s[0], sg = s[1], sb = s[2];
            if (mode != RasterBlendMode::Normal && da > 0) {
                const int dr = ((dv >> 16) & 0xFF) * 255 / da;
                const int dg = ((dv >> 8) & 0xFF) * 255 / da;
                const int db = (dv & 0xFF) * 255 / da;
                const int br = RasterBlendChannel(mode, static_cast<uint8_t>(sr), static_cast<uint8_t>(std::min(255, dr)));
                const int bg = RasterBlendChannel(mode, static_cast<uint8_t>(sg), static_cast<uint8_t>(std::min(255, dg)));
                const int bb = RasterBlendChannel(mode, static_cast<uint8_t>(sb), static_cast<uint8_t>(std::min(255, db)));
                sr = sr + ((br - sr) * da + 127) / 255;
                sg = sg + ((bg - sg) * da + 127) / 255;
                sb = sb + ((bb - sb) * da + 127) / 255;
            }
            if (sa == 255) {
                *d = 0xFF000000u | (static_cast<uint32_t>(sr) << 16) | (static_cast<uint32_t>(sg) << 8) | static_cast<uint32_t>(sb);
                continue;
            }
            const int inv = 255 - sa;
            const int oa = sa + (da * inv + 127) / 255;
            const int orr = (sr * sa + 127) / 255 + (((dv >> 16) & 0xFF) * inv + 127) / 255;
            const int og  = (sg * sa + 127) / 255 + (((dv >> 8) & 0xFF) * inv + 127) / 255;
            const int ob  = (sb * sa + 127) / 255 + ((dv & 0xFF) * inv + 127) / 255;
            *d = (static_cast<uint32_t>(std::min(255, oa)) << 24)
               | (static_cast<uint32_t>(std::min(255, orr)) << 16)
               | (static_cast<uint32_t>(std::min(255, og)) << 8)
               |  static_cast<uint32_t>(std::min(255, ob));
        }
    }
}

// ===========================================================================
// PIXELFX ROUND TRIP
// ===========================================================================

#ifdef HAS_LIBVIPS
PixelFX::PFXImage UCRasterLayer::ToPixelFX() const {
    if (!IsValid()) return PixelFX::PFXImage();
    // new_from_memory does not copy: hand libvips its own buffer so the
    // image stays valid however long the caller keeps it.
    PixelFX::PFXImage img = PixelFX::PFXImage::FromMemory(
        const_cast<uint8_t*>(pixels.data()), width, height, 4, PixelFX::BandFormat::FmtUChar);
    img = PixelFX::PFXImage(img.copy_memory());
    img = PixelFX::PFXImage(img.copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB)));
    return img;
}

bool UCRasterLayer::FromPixelFX(const PixelFX::PFXImage& imgIn) {
    try {
        vips::VImage v = imgIn;
        if (!v.get_image()) return false;
        // Colour: anything that is not already sRGB / RGB-ish goes through
        // the colourspace converter; multiband images of 3/4 bands are taken
        // as RGB(A) directly (filters often return "multiband").
        const int bands = v.bands();
        if (bands < 3) {
            v = v.colourspace(VIPS_INTERPRETATION_sRGB);
        } else if (v.interpretation() != VIPS_INTERPRETATION_sRGB &&
                   v.interpretation() != VIPS_INTERPRETATION_RGB &&
                   v.interpretation() != VIPS_INTERPRETATION_MULTIBAND) {
            v = v.colourspace(VIPS_INTERPRETATION_sRGB);
        }
        v = v.cast(VIPS_FORMAT_UCHAR);
        if (v.bands() == 3) v = v.bandjoin(255);
        else if (v.bands() > 4) v = v.extract_band(0, vips::VImage::option()->set("n", 4));
        else if (v.bands() == 2) v = v.extract_band(0).bandjoin(v.extract_band(1));
        else if (v.bands() == 1) v = v.bandjoin(std::vector<double>{v.avg(), v.avg(), 255.0});
        const int w = v.width(), h = v.height();
        if (w <= 0 || h <= 0) return false;
        size_t size = 0;
        void* data = v.write_to_memory(&size);
        if (!data) return false;
        const size_t need = static_cast<size_t>(w) * h * 4;
        if (size < need) { g_free(data); return false; }
        width = w; height = h;
        pixels.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + need);
        g_free(data);
        return true;
    } catch (...) {
        return false;
    }
}
#endif // HAS_LIBVIPS

} // namespace UltraCanvas
