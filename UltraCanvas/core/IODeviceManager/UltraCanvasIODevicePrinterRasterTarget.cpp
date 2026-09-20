// core/IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.cpp
// Drawing a print page onto an off-screen surface.
//
// Everything here goes through IRenderContext, so it is the same text and
// image stack the rest of UltraCanvas draws with - the same font selection,
// the same shaping, the same scaling filters - rather than a second one
// written for printing.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"

#include "UltraCanvasRenderContext.h"
#include "UltraCanvasImage.h"

#include <algorithm>
#include <map>

namespace UltraCanvas {

struct RasterPageTarget::Impl {
    int width = 0;
    int height = 0;
    int dpiX = 0;
    int dpiY = 0;

    std::unique_ptr<IRenderContext> context;
    UCPixmap readback;

    // Font size per requested pixel height, measured once. Mutable because
    // measuring happens from the const query methods - MeasureTextWidth is
    // logically a question, not a change.
    mutable std::map<int, double> fontSizes;
    mutable std::map<int, int> lineHeights;
    mutable std::map<int, int> ascents;
};

namespace {

// A string with both a tall ascender and a descender, so the measured line
// box is the real one rather than the height of whatever happened to be
// asked about first.
const char* const kMetricSample = "Agjy";

}  // namespace

// ============================================================================
// LIFETIME
// ============================================================================

RasterPageTarget::RasterPageTarget(int widthPixels, int heightPixels,
                                   int dpiX, int dpiY)
    : impl(std::make_unique<Impl>()) {
    if (widthPixels <= 0 || heightPixels <= 0 || dpiX <= 0 || dpiY <= 0) {
        return;
    }
    impl->width = widthPixels;
    impl->height = heightPixels;
    impl->dpiX = dpiX;
    impl->dpiY = dpiY;

    // nullptr for the surface to be similar to: that is what asks for a
    // stand-alone image surface rather than one matched to a window.
    impl->context = CreateRenderContext(Size2Di(widthPixels, heightPixels), nullptr);
    if (impl->context && !impl->readback.Init(widthPixels, heightPixels)) {
        impl->context.reset();
    }
}

RasterPageTarget::~RasterPageTarget() = default;

bool RasterPageTarget::IsValid() const {
    return impl && impl->context != nullptr;
}

void RasterPageTarget::BeginPage() {
    if (!IsValid()) return;
    // Opaque white: paper. Opaque matters as much as white - the pixels are
    // read back as-is, and a transparent page would come out of the readback
    // as zeroes, which is black ink over the whole sheet.
    impl->context->Clear(Color(255, 255, 255, 255));
}

// ============================================================================
// METRICS
// ============================================================================

IOPrintPageMetrics RasterPageTarget::GetMetrics() const {
    IOPrintPageMetrics metrics;
    if (!IsValid()) return metrics;
    metrics.widthDots = impl->width;
    metrics.heightDots = impl->height;
    metrics.dpiX = impl->dpiX;
    metrics.dpiY = impl->dpiY;
    // The filter is handed the full sheet and applies the printer's own
    // unprintable margin, so there is no offset to report here.
    metrics.offsetXDots = 0;
    metrics.offsetYDots = 0;
    return metrics;
}

double RasterPageTarget::FontSizeFor(int pixelHeight) const {
    if (!IsValid() || pixelHeight <= 0) return 0.0;

    auto cached = impl->fontSizes.find(pixelHeight);
    if (cached != impl->fontSizes.end()) return cached->second;

    // Start by assuming the size is already in pixels, measure what that
    // actually produced, and scale once. Font metrics are linear in the size,
    // so one correction lands it; a second pass would only chase rounding.
    double size = static_cast<double>(pixelHeight);
    impl->context->SetFontSize(size);
    const Size2Di measured =
        impl->context->GetTextLineDimensions(kMetricSample);

    if (measured.height > 0) {
        const double scale = static_cast<double>(pixelHeight) /
                             static_cast<double>(measured.height);
        size = std::max(1.0, size * scale);
    }

    impl->fontSizes[pixelHeight] = size;
    return size;
}

int RasterPageTarget::MeasureTextWidth(const std::string& utf8,
                                       int pixelHeight) const {
    if (!IsValid() || utf8.empty() || pixelHeight <= 0) return 0;
    impl->context->SetFontSize(FontSizeFor(pixelHeight));
    return impl->context->GetTextLineDimensions(utf8).width;
}

int RasterPageTarget::GetLineHeight(int pixelHeight) const {
    if (!IsValid() || pixelHeight <= 0) return 0;

    auto cached = impl->lineHeights.find(pixelHeight);
    if (cached != impl->lineHeights.end()) return cached->second;

    impl->context->SetFontSize(FontSizeFor(pixelHeight));
    const int measured = impl->context->GetTextLineDimensions(kMetricSample).height;

    // A line box with no leading sets consecutive lines touching, which is
    // hard to read on paper; a fifth of the height is the usual allowance.
    const int lineHeight = std::max(1, measured + measured / 5);
    impl->lineHeights[pixelHeight] = lineHeight;
    return lineHeight;
}

int RasterPageTarget::GetAscent(int pixelHeight) const {
    if (!IsValid() || pixelHeight <= 0) return 0;

    auto cached = impl->ascents.find(pixelHeight);
    if (cached != impl->ascents.end()) return cached->second;

    // The distance from the top of a line box down to the baseline.
    //
    // What matters is that this agrees with DrawTextLine, which subtracts it
    // again to get a top edge - the two are the only users, so a value that
    // is consistent puts the first line's top at zero and each later line a
    // line-height below, whatever the text stack means by its extents. A
    // sample without a descender is the better estimate where the stack
    // reports ink extents; where it reports the font's own logical extents
    // both samples measure the same and the consistency is what carries it.
    impl->context->SetFontSize(FontSizeFor(pixelHeight));
    const int withDescender = impl->context->GetTextLineDimensions(kMetricSample).height;
    const int noDescender = impl->context->GetTextLineDimensions("A").height;

    const int ascent = noDescender > 0 && noDescender <= withDescender
                           ? noDescender
                           : std::max(1, (withDescender * 4) / 5);
    impl->ascents[pixelHeight] = ascent;
    return ascent;
}

// ============================================================================
// DRAWING
// ============================================================================

bool RasterPageTarget::DrawImage(const uint8_t* pixels, int width, int height,
                                 const IOPrintRect& dest) {
    if (!IsValid() || !pixels || width <= 0 || height <= 0 || dest.IsEmpty()) {
        return false;
    }

    // The drawing stack takes a pixmap, so the caller's rows are copied into
    // one. 32-bit native-endian ARGB is what a pixmap holds; the caller's
    // bytes are RGBA in memory order, so the two differ by more than a name.
    UCPixmap image;
    if (!image.Init(width, height)) return false;

    uint32_t* out = image.GetPixelData();
    if (!out) return false;

    for (int y = 0; y < height; ++y) {
        const uint8_t* row = pixels + static_cast<size_t>(y) *
                                      static_cast<size_t>(width) * 4u;
        for (int x = 0; x < width; ++x) {
            const uint8_t* p = row + static_cast<size_t>(x) * 4u;
            const uint32_t a = p[3];

            // Premultiplied, because that is how a 32-bit surface stores
            // alpha here; handing it straight-alpha pixels makes anything
            // semi-transparent come out too bright.
            const uint32_t r = (p[0] * a + 127u) / 255u;
            const uint32_t g = (p[1] * a + 127u) / 255u;
            const uint32_t b = (p[2] * a + 127u) / 255u;

            out[static_cast<size_t>(y) * static_cast<size_t>(width) +
                static_cast<size_t>(x)] =
                (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    image.MarkDirty();

    impl->context->SetImageSmoothing(true);
    impl->context->DrawPixmap(image,
                              Rect2Dd(dest.x, dest.y, dest.width, dest.height),
                              ImageFitMode::Fill);
    return true;
}

bool RasterPageTarget::DrawTextLine(const std::string& utf8, int x,
                                    int baselineY, int pixelHeight) {
    if (!IsValid() || pixelHeight <= 0) return false;
    if (utf8.empty()) return true;   // a blank line draws nothing and is fine

    impl->context->SetFontSize(FontSizeFor(pixelHeight));
    impl->context->SetTextPaint(Color(0, 0, 0, 255));

    // The interface positions by baseline - that is what every text engine
    // and every printer driver uses - while the drawing stack positions a
    // line by its top edge, so the ascent is subtracted here rather than
    // being made the caller's problem.
    const double top = static_cast<double>(baselineY - GetAscent(pixelHeight));
    impl->context->DrawText(utf8, Point2Dd(static_cast<double>(x), top));
    return true;
}

// ============================================================================
// READING BACK
// ============================================================================

bool RasterPageTarget::ForEachRow(
        const std::function<bool(const uint8_t* rgba, int width)>& row) const {
    if (!IsValid() || !row) return false;

    impl->context->FlushToSurface(impl->readback.GetSurface(), Point2Dd(0, 0));
    impl->readback.Flush();

    const uint32_t* pixels = impl->readback.GetPixelData();
    if (!pixels) return false;

    std::vector<uint8_t> rgba(static_cast<size_t>(impl->width) * 4u);

    for (int y = 0; y < impl->height; ++y) {
        const uint32_t* source = pixels + static_cast<size_t>(y) *
                                          static_cast<size_t>(impl->width);
        for (int x = 0; x < impl->width; ++x) {
            const uint32_t pixel = source[x];
            const uint32_t a = (pixel >> 24) & 0xFFu;
            uint32_t r = (pixel >> 16) & 0xFFu;
            uint32_t g = (pixel >> 8) & 0xFFu;
            uint32_t b = pixel & 0xFFu;

            // Back out of premultiplication, so what leaves here is ordinary
            // RGBA. A page begins opaque white, so in practice alpha is 255
            // and this is a copy - but a target handed to something that
            // clears differently should still hand back what it was given.
            if (a != 0 && a != 255) {
                r = std::min(255u, (r * 255u) / a);
                g = std::min(255u, (g * 255u) / a);
                b = std::min(255u, (b * 255u) / a);
            }

            uint8_t* out = rgba.data() + static_cast<size_t>(x) * 4u;
            out[0] = static_cast<uint8_t>(r);
            out[1] = static_cast<uint8_t>(g);
            out[2] = static_cast<uint8_t>(b);
            out[3] = static_cast<uint8_t>(a);
        }
        if (!row(rgba.data(), impl->width)) return false;
    }
    return true;
}

}  // namespace UltraCanvas
