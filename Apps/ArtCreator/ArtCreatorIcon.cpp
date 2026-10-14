// Apps/ArtCreator/ArtCreatorIcon.cpp
// Icon mode's raster check and its preview: see ArtCreatorIcon.h.
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "ArtCreatorIcon.h"
#include "UltraCanvasVectorRaster.h"
#include "UltraCanvasRasterLayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace UltraCanvas {

namespace {
    // Covered by more than this and less than its complement: a soft pixel.
    constexpr int kSoftLow = 26;      // 10 %
    constexpr int kSoftHigh = 229;    // 90 %
    constexpr float kPad = 3.0f;      // ground around a real-size view
    constexpr float kGap = 6.0f;
    constexpr float kMaxEnlarged = 128.0f;   // the enlarged view's height at most
    const Color kLightGround(250, 250, 250, 255);
    const Color kDarkGround(44, 44, 46, 255);
    const Color kFrame(200, 200, 204, 255);

    uint32_t Premultiplied(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        auto pm = [a](uint8_t c) { return static_cast<uint32_t>((c * a + 127) / 255); };
        return (static_cast<uint32_t>(a) << 24) | (pm(r) << 16) | (pm(g) << 8) | pm(b);
    }
    uint32_t Opaque(int r, int g, int b) {
        return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }
}

const std::vector<int>& ArtIconSizes() {
    static const std::vector<int> sizes = {16, 20, 24, 28, 32, 40, 48, 64, 96, 128, 256};
    return sizes;
}

ArtIconRaster RasterizeIcon(const VectorStorage::VectorDocument& document, int width, int height) {
    ArtIconRaster out;
    VectorRasterOptions options;
    options.width = width;
    options.height = height;
    auto layer = RasterizeVectorDocument(document, options, out.error);
    if (!layer || !layer->IsValid()) {
        if (out.error.empty()) out.error = "nothing to draw";
        return out;
    }
    out.width = layer->GetWidth();
    out.height = layer->GetHeight();
    out.rgba.assign(layer->Data(), layer->Data() + layer->DataSize());
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const uint8_t a = out.rgba[(static_cast<size_t>(y) * out.width + x) * 4 + 3];
            if (a > 0) ++out.coveredPixels;
            if (a > kSoftLow && a < kSoftHigh) out.softPixels.emplace_back(x, y);
        }
    return out;
}

// ===== PREVIEW =====

ArtIconPreview::ArtIconPreview(const std::string& id, float width)
    : UltraCanvasUIElement(id, width, 0), panelWidth(width) {
    Layout();
}

int ArtIconPreview::Magnification(int size, float width) {
    if (size <= 0) return 0;
    // as wide as the panel allows, but no taller than kMaxEnlarged
    const int m = std::min({8, static_cast<int>((width - 2) / size), static_cast<int>(kMaxEnlarged / size)});
    return m >= 2 ? m : 0;
}

void ArtIconPreview::SetRaster(std::shared_ptr<const ArtIconRaster> r) {
    raster = std::move(r);
    actual.reset();
    enlarged.reset();
    magnification = 0;
    if (raster && raster->width > 0 && raster->height > 0) {
        const int w = raster->width, h = raster->height;
        const auto& px = raster->rgba;
        actual = std::make_shared<UCPixmap>(w, h);
        if (actual->IsValid()) {
            uint32_t* dst = actual->GetPixelData();
            const int stride = actual->GetRawWidth();
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
                    dst[static_cast<size_t>(y) * stride + x] = Premultiplied(p[0], p[1], p[2], p[3]);
                }
            actual->MarkDirty();
        }
        magnification = Magnification(std::max(w, h), panelWidth);
        if (magnification > 0) {
            const int m = magnification;
            enlarged = std::make_shared<UCPixmap>(w * m, h * m);
            if (enlarged->IsValid()) {
                uint32_t* dst = enlarged->GetPixelData();
                const int stride = enlarged->GetRawWidth();
                std::vector<uint8_t> soft(static_cast<size_t>(w) * h, 0);
                for (const auto& s : raster->softPixels) soft[static_cast<size_t>(s.y) * w + s.x] = 1;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
                        // the pixel on white, as the light view shows it
                        const int a = p[3];
                        auto over = [a](int c) { return (c * a + 255 * (255 - a) + 127) / 255; };
                        const uint32_t colour = Opaque(over(p[0]), over(p[1]), over(p[2]));
                        const bool isSoft = soft[static_cast<size_t>(y) * w + x] != 0;
                        for (int dy = 0; dy < m; ++dy)
                            for (int dx = 0; dx < m; ++dx) {
                                uint32_t c = colour;
                                const bool edge = dx == 0 || dy == 0 || dx == m - 1 || dy == m - 1;
                                if (isSoft && edge) c = Opaque(226, 36, 36);              // the soft pixel, outlined
                                else if (m >= 4 && (dx == 0 || dy == 0)) {                 // the pixel grid, faintly
                                    const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                                    c = Opaque(r * 15 / 16, g * 15 / 16, b * 15 / 16);
                                }
                                dst[static_cast<size_t>(y * m + dy) * stride + x * m + dx] = c;
                            }
                    }
                enlarged->MarkDirty();
            } else {
                enlarged.reset();
                magnification = 0;
            }
        }
    }
    Layout();
    RequestRedraw();
}

void ArtIconPreview::Layout() {
    float height = 0;
    if (raster && raster->width > 0) {
        const float w = static_cast<float>(raster->width), h = static_cast<float>(raster->height);
        sideBySide = 2 * (w + 2 * kPad) + kGap <= panelWidth;
        height = h + 2 * kPad;                                     // the real-size views
        if (magnification > 0) height += kGap + h * magnification + 2;
    }
    SetSize(panelWidth, height);
    size.height = CSSLayout::Dimension::Px(height);
    layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(height));
    InvalidateLayout();
}

void ArtIconPreview::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    UltraCanvasUIElement::Render(ctx, dirtyRect);
    if (!raster || !actual || !actual->IsValid()) return;
    const Rect2Df b = GetLocalBounds();
    const double w = raster->width, h = raster->height;
    const double x0 = std::floor(b.x), y0 = std::floor(b.y);

    // the icon at its real size: on light, and on dark beside it when there is room
    auto ground = [&](double x, double y, const Color& c) {
        ctx->SetFillPaint(c);
        ctx->FillRectangle(Rect2Dd(x, y, w + 2 * kPad, h + 2 * kPad));
        ctx->DrawPixmap(*actual, Rect2Dd(x + kPad, y + kPad, w, h), ImageFitMode::NoScale);
    };
    ground(x0, y0, kLightGround);
    if (sideBySide) ground(x0 + w + 2 * kPad + kGap, y0, kDarkGround);

    // enlarged, soft pixels outlined
    if (enlarged && enlarged->IsValid()) {
        const double y = y0 + h + 2 * kPad + kGap;
        const double ew = w * magnification, eh = h * magnification;
        ctx->SetFillPaint(kFrame);
        ctx->FillRectangle(Rect2Dd(x0, y, ew + 2, eh + 2));
        ctx->DrawPixmap(*enlarged, Rect2Dd(x0 + 1, y + 1, ew, eh), ImageFitMode::NoScale);
    }
}

} // namespace UltraCanvas
