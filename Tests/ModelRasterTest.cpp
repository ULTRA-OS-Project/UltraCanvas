// Tests/ModelRasterTest.cpp
// The software mesh rasterizer, now that two callers share it.
//
// It spent its life as a private function inside UltraCanvasFilerWidget.cpp,
// reachable only through a thumbnail worker and therefore only ever tested by
// looking at a folder. Extracting it so the media viewer's non-GL fallback can
// draw the same still made it directly callable, and these are the assertions
// that were never possible before: that it refuses what it cannot frame, that
// it honours the size and scale it is given, that it actually puts the model
// on the canvas, and that the pose is the one the Filer has always drawn.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "UltraCanvasModelRaster.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// A unit cube, the least ambiguous thing to frame: its silhouette from any
// three-quarter angle is a hexagon covering a good fraction of the tile.
static Mesh3D Cube() {
    Mesh3D m;
    m.name = "cube";
    m.positions = {{-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
                   {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1}};
    m.indices = {0,1,2, 0,2,3,  4,6,5, 4,7,6,  0,4,5, 0,5,1,
                 3,2,6, 3,6,7,  0,3,7, 0,7,4,  1,5,6, 1,6,2};
    m.ComputeBounds();
    return m;
}

// What fraction of the pixmap the model actually covers. The background is
// transparent, so an opaque alpha is the model.
static double Coverage(const std::shared_ptr<UCPixmap>& pm) {
    if (!pm) return 0.0;
    const uint32_t* px = pm->GetPixelData();
    if (!px) return 0.0;
    const size_t n = static_cast<size_t>(pm->GetWidth()) * pm->GetHeight();
    size_t opaque = 0;
    for (size_t i = 0; i < n; ++i)
        if ((px[i] >> 24) != 0u) ++opaque;
    return n ? static_cast<double>(opaque) / static_cast<double>(n) : 0.0;
}

static void TestRefusals() {
    std::printf("What it refuses to draw\n");

    Check(RenderMeshPreviewPixmap(Mesh3D{}, 64, 64) == nullptr,
          "an empty mesh gives no pixmap rather than a blank one");

    // Every vertex at one point: bounds are valid but the radius is zero, and
    // the framing divides by it. This is the case that would crash or produce
    // NaN coordinates if it were not caught.
    Mesh3D degenerate;
    degenerate.positions = {{2, 2, 2}, {2, 2, 2}, {2, 2, 2}};
    degenerate.indices = {0, 1, 2};
    degenerate.ComputeBounds();
    Check(RenderMeshPreviewPixmap(degenerate, 64, 64) == nullptr,
          "a mesh with no extent is refused rather than divided by");

    // The cap is the caller's contract: over it, the tile keeps its glyph.
    Mesh3D huge = Cube();
    huge.indices.resize((kModelPreviewTriangleCap + 1) * 3, 0);
    Check(RenderMeshPreviewPixmap(huge, 64, 64) == nullptr,
          "a mesh over the triangle cap is refused");

    // Bounds are computed when absent: a caller that built a mesh by hand and
    // forgot ComputeBounds() still gets a picture.
    Mesh3D unbounded = Cube();
    unbounded.bounds = BoundingBox3D{};
    Check(RenderMeshPreviewPixmap(unbounded, 64, 64) != nullptr,
          "a mesh with no bounds yet gets them computed rather than refused");
}

static void TestGeometryOfTheOutput() {
    std::printf("The pixmap it produces\n");
    const Mesh3D cube = Cube();

    auto pm = RenderMeshPreviewPixmap(cube, 64, 48);
    Check(pm != nullptr, "a cube renders");
    if (!pm) return;
    Check(pm->GetWidth() == 64 && pm->GetHeight() == 48,
          "at exactly the size asked for, non-square included");

    // HiDPI: the caller asks in logical pixels and gets device pixels.
    auto hidpi = RenderMeshPreviewPixmap(cube, 64, 48, 2.0f);
    Check(hidpi && hidpi->GetWidth() == 128 && hidpi->GetHeight() == 96,
          "and at scale 2 it is twice that in each direction");

    // A scale below 1 must not shrink the buffer below what was asked for -
    // the rasterizer clamps it, because a caller passing 0.5 wants the same
    // tile, not a quarter of one.
    auto small = RenderMeshPreviewPixmap(cube, 64, 48, 0.5f);
    Check(small && small->GetWidth() == 64 && small->GetHeight() == 48,
          "a scale below 1 is clamped rather than shrinking the tile");

    const double coverage = Coverage(pm);
    Check(coverage > 0.20 && coverage < 0.95,
          "the model covers a good part of the tile without filling it "
          "(got " + std::to_string(static_cast<int>(coverage * 100)) + "%)");

    // Framing leaves a margin on purpose, so the silhouette never touches the
    // edge. Check the border ring is clear.
    const uint32_t* px = pm->GetPixelData();
    bool borderClear = true;
    if (px) {
        for (int x = 0; x < pm->GetWidth(); ++x) {
            if ((px[x] >> 24) != 0u) borderClear = false;
            if ((px[(pm->GetHeight() - 1) * pm->GetWidth() + x] >> 24) != 0u)
                borderClear = false;
        }
    }
    Check(borderClear, "and never touches the top or bottom edge, as the margin intends");
}

static void TestShadingAndPose() {
    std::printf("Shading and pose\n");
    const Mesh3D cube = Cube();
    auto pm = RenderMeshPreviewPixmap(cube, 96, 96);
    if (!pm) { Check(false, "a cube renders"); return; }
    const uint32_t* px = pm->GetPixelData();
    if (!px) { Check(false, "the pixmap exposes its pixels"); return; }

    // Flat shading from a single head-light: a cube seen three-quarters shows
    // three faces at three different angles, so a correctly shaded render has
    // several distinct greys. One shade would mean the lighting is not being
    // applied at all.
    std::vector<uint32_t> shades;
    const size_t n = static_cast<size_t>(pm->GetWidth()) * pm->GetHeight();
    for (size_t i = 0; i < n; ++i) {
        if ((px[i] >> 24) == 0u) continue;
        const uint32_t rgb = px[i] & 0x00FFFFFFu;
        bool seen = false;
        for (uint32_t s : shades) if (s == rgb) { seen = true; break; }
        if (!seen && shades.size() < 16) shades.push_back(rgb);
    }
    Check(shades.size() >= 3,
          "a cube shows at least three distinct face shades, so the light is applied "
          "(got " + std::to_string(shades.size()) + ")");

    // The pose looks down on the model from slightly above, so a cube's top
    // face is visible and lit more than its front. That makes the upper half
    // of the silhouette brighter on average than the lower half - which is
    // what makes the tile read as a solid rather than a flat hexagon.
    auto meanLuma = [&](int fromY, int toY) {
        double sum = 0.0; size_t count = 0;
        for (int y = fromY; y < toY; ++y)
            for (int x = 0; x < pm->GetWidth(); ++x) {
                const uint32_t p = px[static_cast<size_t>(y) * pm->GetWidth() + x];
                if ((p >> 24) == 0u) continue;
                sum += ((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF);
                ++count;
            }
        return count ? sum / (3.0 * count) : 0.0;
    };
    const double upper = meanLuma(0, pm->GetHeight() / 2);
    const double lower = meanLuma(pm->GetHeight() / 2, pm->GetHeight());
    Check(upper > lower,
          "and the top of the model is lit more than the bottom, which is the pose "
          "(upper " + std::to_string(static_cast<int>(upper)) +
          " vs lower " + std::to_string(static_cast<int>(lower)) + ")");

    // Determinism: two renders of the same mesh at the same size must be
    // identical. The Filer caches these and the viewer redraws from a cache,
    // so anything time- or address-dependent in here would show as flicker.
    auto again = RenderMeshPreviewPixmap(cube, 96, 96);
    bool identical = again != nullptr;
    if (identical) {
        const uint32_t* px2 = again->GetPixelData();
        for (size_t i = 0; i < n && identical; ++i)
            if (px[i] != px2[i]) identical = false;
    }
    Check(identical, "and rendering the same mesh twice gives identical pixels");
}

static void TestTwoSided() {
    std::printf("Winding\n");

    // One triangle wound each way. The rasterizer lights whichever facet faces
    // the viewer rather than culling, so a mesh with inconsistent winding -
    // which is most STL in the wild - still reads as a solid instead of
    // showing holes.
    Mesh3D front;
    front.positions = {{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}};
    front.indices = {0, 1, 2};
    front.ComputeBounds();

    Mesh3D back = front;
    back.indices = {0, 2, 1};

    auto a = RenderMeshPreviewPixmap(front, 64, 64);
    auto b = RenderMeshPreviewPixmap(back, 64, 64);
    Check(a && Coverage(a) > 0.05, "a triangle wound one way is drawn");
    Check(b && Coverage(b) > 0.05, "and wound the other way it is still drawn, not culled");
}

int main() {
    TestRefusals();
    TestGeometryOfTheOutput();
    TestShadingAndPose();
    TestTwoSided();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
