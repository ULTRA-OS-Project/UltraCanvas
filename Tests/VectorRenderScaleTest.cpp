// Tests/VectorRenderScaleTest.cpp
// What VectorRenderer and the vector preview seam must not do to a drawing
// before it reaches the pixels.
//
// The rules this guards, each one a bug that reached a release:
//   * a drawing is fitted to its box exactly once - RenderVectorDocumentPixmap
//     applies the fit to the context, so passing it to the renderer as well
//     drew it at the square of the fit (a tenth of the box became a
//     hundredth, and a large-unit drawing arrived as specks in a corner);
//   * an element carrying a singular transform is skipped rather than handed
//     to the context, because a non-invertible matrix latches an error state
//     on the whole context and everything drawn after it - including other
//     widgets sharing the frame - is then silently discarded;
//   * a drawing whose own units are far larger than pixels still lands in
//     the box, which is what says the fit was not clamped away.
// Version: 1.0.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework

#include "UltraCanvasVectorPreview.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cairo/cairo.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// The painted extent of a pixmap, in pixels, and how many pixels carry ink.
struct Ink {
    int minX = 0, minY = 0, maxX = -1, maxY = -1;
    long count = 0;
    bool Empty() const { return count == 0; }
    int Width() const { return Empty() ? 0 : maxX - minX + 1; }
    int Height() const { return Empty() ? 0 : maxY - minY + 1; }
};

Ink Measure(const std::shared_ptr<UCPixmap>& pixmap) {
    Ink ink;
    if (!pixmap) return ink;
    cairo_surface_t* surface = pixmap->GetSurface();
    if (!surface) return ink;
    cairo_surface_flush(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    if (!data) return ink;
    const int w = cairo_image_surface_get_width(surface);
    const int h = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    bool first = true;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t px = *reinterpret_cast<const uint32_t*>(data + y * stride + x * 4);
            if ((px >> 24) == 0) continue;            // untouched, background is transparent
            ++ink.count;
            if (first) { ink.minX = ink.maxX = x; ink.minY = ink.maxY = y; first = false; continue; }
            if (x < ink.minX) ink.minX = x;
            if (x > ink.maxX) ink.maxX = x;
            if (y < ink.minY) ink.minY = y;
            if (y > ink.maxY) ink.maxY = y;
        }
    }
    return ink;
}

std::shared_ptr<VectorRect> MakeRect(double x, double y, double w, double h) {
    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd(x, y, w, h);
    rect->Style.Fill = Color(0, 0, 0, 255);
    return rect;
}

// A document of `size` units holding one black rectangle that covers it all.
std::shared_ptr<VectorDocument> FullBleed(double size) {
    auto document = std::make_shared<VectorDocument>();
    document->Size = Size2Dd(size, size);
    document->ViewBox = Rect2Dd(0, 0, size, size);
    auto layer = std::make_shared<VectorLayer>();
    layer->Children.push_back(MakeRect(0, 0, size, size));
    document->Layers.push_back(layer);
    return document;
}

// The fit is applied once, so a full-bleed drawing fills the pixmap whatever
// its own units are. Applied twice it covered the square of the fit: a tenth
// of the box for a 10-unit drawing, a hundredth for a 100-unit one - which is
// why the bug hid in small documents and blanked large ones.
void TestFitAppliedOnce() {
    std::cout << "\n--- the fit is applied once, at any document scale ---\n";
    for (const double size : {10.0, 100.0, 10000.0}) {
        auto pixmap = RenderVectorDocumentPixmap(*FullBleed(size), 200, 200);
        const Ink ink = Measure(pixmap);
        const std::string at = " (document " + std::to_string(static_cast<long>(size)) + " units)";
        Check(!ink.Empty(), "the drawing reached the pixmap" + at);
        Check(ink.Width() >= 198 && ink.Height() >= 198,
              "a full-bleed drawing fills the 200x200 box" + at +
              " - painted " + std::to_string(ink.Width()) + "x" + std::to_string(ink.Height()));
    }
}

// A singular transform is an error to the context, not a small shape: cairo
// latches CAIRO_STATUS_INVALID_MATRIX and discards everything drawn
// afterwards. The element must never reach the context, so the elements
// behind it still paint.
void TestSingularTransformDoesNotPoisonTheContext() {
    std::cout << "\n--- a singular transform does not take the rest of the frame with it ---\n";
    auto document = std::make_shared<VectorDocument>();
    document->Size = Size2Dd(100, 100);
    document->ViewBox = Rect2Dd(0, 0, 100, 100);
    auto layer = std::make_shared<VectorLayer>();

    // Determinant zero: the plane collapses onto a line. A CAD drawing gets
    // here from a 3D entity whose projection to plan view flattens an axis.
    Matrix3x3 flattened = Matrix3x3::Identity();
    flattened.m[0][0] = 0;
    flattened.m[1][0] = -1;
    flattened.m[0][1] = 0;
    flattened.m[1][1] = 0;
    auto doomed = MakeRect(0, 0, 100, 100);
    doomed->Transform = flattened;
    layer->Children.push_back(doomed);

    // Drawn after it, and the thing that must survive.
    layer->Children.push_back(MakeRect(0, 0, 100, 100));
    document->Layers.push_back(layer);

    const Ink ink = Measure(RenderVectorDocumentPixmap(*document, 200, 200));
    Check(!ink.Empty(), "the element after the singular one still painted");
    Check(ink.Width() >= 198 && ink.Height() >= 198,
          "and painted in full - " + std::to_string(ink.Width()) + "x" + std::to_string(ink.Height()));
}

} // namespace

int main() {
    std::cout << "=== VectorRenderer scale and transform robustness ===\n";
    TestFitAppliedOnce();
    TestSingularTransformDoesNotPoisonTheContext();
    std::cout << "\n=== " << (g_failures == 0 ? "all checks passed" : "FAILURES")
              << ": " << g_failures << " failure(s) ===\n";
    return g_failures == 0 ? 0 : 1;
}
