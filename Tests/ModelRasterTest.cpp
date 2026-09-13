// Tests/ModelRasterTest.cpp
// The software mesh rasterizer, now that two callers share it, and the model
// file -> raster layer path built on top of it.
//
// It spent its life as a private function inside UltraCanvasFilerWidget.cpp,
// reachable only through a thumbnail worker and therefore only ever tested by
// looking at a folder. Extracting it so the media viewer's non-GL fallback can
// draw the same still made it directly callable, and these are the assertions
// that were never possible before: that it refuses what it cannot frame, that
// it honours the size and scale it is given, that it actually puts the model
// on the canvas, and that the pose is the one the Filer has always drawn.
//
// The second half covers what a bitmap editor asks of a model: recognising one
// by extension without opening it, reading what geometry it holds, and turning
// it into a UCRasterLayer from a view the caller chose. The rules there are
// that the requested size is delivered exactly, that the pose is what decides
// the picture (turn the model and the pixels change; back the camera off and
// it shrinks), that the background is composited under the model rather than
// over it, that the colour is the caller's, and that an absurd size is refused
// instead of allocated. The model is a binary STL the test writes itself, so
// it asserts against geometry it knows.
//
// Version: 1.1.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework

#include "UltraCanvasModelRaster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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


// ===========================================================================
// A MODEL FILE AS AN EDITABLE LAYER
// ===========================================================================

// ===== A BOX, WRITTEN AS A BINARY STL =====
// 80 bytes of header, the triangle count, then 50 bytes per triangle: the
// facet normal, three corners, and two attribute bytes. The normals are left
// at zero on purpose - a great many real STL files have them wrong or absent,
// and the renderer is supposed to shade from the geometry regardless.
static void PutFloat(std::vector<uint8_t>& bytes, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
}

static void PutU32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
}

static void PutTriangle(std::vector<uint8_t>& bytes,
                        const Vec3& a, const Vec3& b, const Vec3& c) {
    for (int i = 0; i < 3; ++i) PutFloat(bytes, 0.0f);   // the facet normal, left to the renderer
    for (const Vec3& p : { a, b, c }) {
        PutFloat(bytes, p.x);
        PutFloat(bytes, p.y);
        PutFloat(bytes, p.z);
    }
    bytes.push_back(0);
    bytes.push_back(0);
}

// A box from (-w, -h, -d) to (+w, +h, +d) as the twelve triangles of its six
// faces. Twice as wide as it is deep, so a quarter turn visibly narrows the
// silhouette - which is how the pose assertions tell one view from another.
static std::string WriteTempStl() {
    const float w = 20.0f, h = 10.0f, d = 10.0f;
    const Vec3 v[8] = {
        { -w, -h, -d }, { +w, -h, -d }, { +w, +h, -d }, { -w, +h, -d },
        { -w, -h, +d }, { +w, -h, +d }, { +w, +h, +d }, { -w, +h, +d },
    };
    const int faces[6][4] = {
        { 4, 5, 6, 7 }, { 1, 0, 3, 2 }, { 5, 1, 2, 6 },
        { 0, 4, 7, 3 }, { 3, 7, 6, 2 }, { 0, 1, 5, 4 },
    };

    std::vector<uint8_t> bytes(80, 0);
    const char* header = "UltraCanvas ModelRasterTest box";
    std::memcpy(bytes.data(), header, std::strlen(header));
    PutU32(bytes, 12);
    for (const auto& f : faces) {
        PutTriangle(bytes, v[f[0]], v[f[1]], v[f[2]]);
        PutTriangle(bytes, v[f[0]], v[f[2]], v[f[3]]);
    }

    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "ultracanvas-model-raster-test.stl";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    return path.string();
}

// How much of a layer the model covers and how wide its ink is: enough for the
// pose assertions to say "a different picture" without a reference image in
// the repository.
struct LayerInk {
    size_t drawn = 0;
    int minX = 0, maxX = -1;
};

static LayerInk InkOf(const std::shared_ptr<UCRasterLayer>& layer) {
    LayerInk ink;
    ink.minX = layer->GetWidth();
    for (int y = 0; y < layer->GetHeight(); ++y) {
        for (int x = 0; x < layer->GetWidth(); ++x) {
            if (layer->GetPixel(x, y).a == 0) continue;
            ++ink.drawn;
            ink.minX = std::min(ink.minX, x);
            ink.maxX = std::max(ink.maxX, x);
        }
    }
    return ink;
}

static bool SamePixels(const std::shared_ptr<UCRasterLayer>& a,
                       const std::shared_ptr<UCRasterLayer>& b) {
    if (!a || !b) return false;
    if (a->GetWidth() != b->GetWidth() || a->GetHeight() != b->GetHeight()) return false;
    for (int y = 0; y < a->GetHeight(); ++y)
        for (int x = 0; x < a->GetWidth(); ++x) {
            const RasterPixel pa = a->GetPixel(x, y), pb = b->GetPixel(x, y);
            if (pa.r != pb.r || pa.g != pb.g || pa.b != pb.b || pa.a != pb.a) return false;
        }
    return true;
}

static void TestWhatIsAModel() {
    std::printf("What counts as a model\n");

    const std::vector<std::string> extensions = GetModelRasterExtensions();
    Check(std::find(extensions.begin(), extensions.end(), "stl") != extensions.end(),
          "STL is rasterizable in every build - core reads it without a plugin");
    Check(IsModelGraphicsPath("part.stl"), "an STL is a model");
    Check(IsModelGraphicsPath("PART.STL"), "and the extension test ignores case");
    Check(!IsModelGraphicsPath("notes.txt"), "a plain text file is not a model");
    Check(!IsModelGraphicsPath("photo.png"), "a bitmap is not a model");
    Check(!IsModelGraphicsPath("drawing.svg"), "a drawing is not a model");
    Check(!IsModelGraphicsPath("noextension"), "and neither is a name with no extension");
}

static void TestInspection(const std::string& stlPath) {
    std::printf("Reading what a file holds\n");

    const ModelSourceInfo info = InspectModelFile(stlPath);
    Check(info.ok, "the test STL can be read: " + info.error);
    Check(info.triangleCount == 12,
          "a box is twelve triangles (got " + std::to_string(info.triangleCount) + ")");
    Check(info.vertexCount >= 3,
          "it reports vertices too (got " + std::to_string(info.vertexCount) + ")");
    Check(info.bounds.IsValid() &&
          std::fabs((info.bounds.max.x - info.bounds.min.x) - 40.0f) < 0.01f &&
          std::fabs((info.bounds.max.z - info.bounds.min.z) - 20.0f) < 0.01f,
          "and the bounds are the file's own units, not normalised ones");

    const ModelSourceInfo missing = InspectModelFile("no-such-file.stl");
    Check(!missing.ok && !missing.error.empty(),
          "a model that is not there is reported, not guessed at");

    const ModelSourceInfo foreign = InspectModelFile("notes.txt");
    Check(!foreign.ok && !foreign.error.empty(),
          "and a format no reader in this build handles says so");
}

static void TestFileToLayer(const std::string& stlPath) {
    std::printf("A model file as a raster layer\n");

    std::string error;
    ModelRasterOptions options;
    options.width = 320;
    options.height = 240;

    auto front = RasterizeModelFile(stlPath, options, error);
    Check(front != nullptr, "rasterizes at 320 x 240: " + error);
    if (!front) return;

    Check(front->GetWidth() == 320 && front->GetHeight() == 240,
          "the requested size is delivered exactly (got " +
          std::to_string(front->GetWidth()) + " x " + std::to_string(front->GetHeight()) + ")");
    Check(front->name == "ultracanvas-model-raster-test",
          "the layer is named after the file, so it reads in a layer list (got \"" +
          front->name + "\")");

    const LayerInk frontInk = InkOf(front);
    Check(frontInk.drawn > 0, "the model is actually drawn");
    Check(frontInk.drawn < static_cast<size_t>(320) * 240,
          "and framed with margin rather than filling the raster");
    Check(front->GetPixel(0, 0).a == 0 && front->GetPixel(319, 239).a == 0,
          "the corners stay transparent - nothing is painted behind the model");

    // ----- the pose is what decides the picture -----
    // A quarter turn puts the box end-on: the same geometry, a much narrower
    // silhouette. This is the promise the 3D import dialog rests on - the
    // bitmap is the view the user framed, not a fixed one.
    ModelRasterOptions turned = options;
    turned.pose.yaw = options.pose.yaw + 1.5707963f;
    auto side = RasterizeModelFile(stlPath, turned, error);
    Check(side != nullptr, "rasterizes from a second pose: " + error);
    if (side) {
        Check(!SamePixels(front, side), "turning the model changes the pixels");
        const LayerInk sideInk = InkOf(side);
        Check(sideInk.maxX - sideInk.minX < frontInk.maxX - frontInk.minX,
              "and a quarter turn on a box twice as wide as it is deep narrows it (" +
              std::to_string(sideInk.maxX - sideInk.minX) + " px vs " +
              std::to_string(frontInk.maxX - frontInk.minX) + " px)");
    }

    ModelRasterOptions further = options;
    further.pose.distance = options.pose.distance * 2.0f;
    auto smaller = RasterizeModelFile(stlPath, further, error);
    Check(smaller != nullptr, "rasterizes from further away: " + error);
    if (smaller) {
        Check(InkOf(smaller).drawn < frontInk.drawn,
              "and backing the camera off makes the model smaller");
    }

    auto again = RasterizeModelFile(stlPath, options, error);
    Check(SamePixels(front, again), "the same pose gives the same pixels");

    // ----- background -----
    ModelRasterOptions onWhite = options;
    onWhite.background = RasterPixel(255, 255, 255, 255);
    auto white = RasterizeModelFile(stlPath, onWhite, error);
    Check(white != nullptr, "rasterizes onto a white background: " + error);
    if (white) {
        const RasterPixel corner = white->GetPixel(0, 0);
        Check(corner.r == 255 && corner.g == 255 && corner.b == 255 && corner.a == 255,
              "the empty area becomes the background colour");
        bool modelSurvives = false;
        for (int y = 0; y < white->GetHeight() && !modelSurvives; ++y)
            for (int x = 0; x < white->GetWidth(); ++x) {
                if (front->GetPixel(x, y).a == 0) continue;
                const RasterPixel p = white->GetPixel(x, y);
                if (p.r != 255 || p.g != 255 || p.b != 255) { modelSurvives = true; break; }
            }
        Check(modelSurvives, "and the background goes under the model, not over it");
    }

    // ----- the colour is the caller's -----
    ModelRasterOptions red = options;
    red.modelColor = Vec3(1.0f, 0.0f, 0.0f);
    auto reddish = RasterizeModelFile(stlPath, red, error);
    Check(reddish != nullptr, "rasterizes in a colour of the caller's choosing: " + error);
    if (reddish) {
        bool sawRed = false, sawSomethingElse = false;
        for (int y = 0; y < reddish->GetHeight() && !sawSomethingElse; ++y)
            for (int x = 0; x < reddish->GetWidth(); ++x) {
                const RasterPixel p = reddish->GetPixel(x, y);
                if (p.a == 0) continue;
                if (p.g > 8 || p.b > 8) { sawSomethingElse = true; break; }
                if (p.r > 0) sawRed = true;
            }
        Check(sawRed && !sawSomethingElse,
              "and every shaded pixel is a shade of it");
    }

    // ----- what it refuses -----
    ModelRasterOptions huge = options;
    huge.width = huge.height = 100000;
    error.clear();
    Check(RasterizeModelFile(stlPath, huge, error) == nullptr && !error.empty(),
          "a 10-gigapixel request is refused with a reason, not attempted");

    ModelRasterOptions zero = options;
    zero.width = 0;
    error.clear();
    Check(RasterizeModelFile(stlPath, zero, error) == nullptr && !error.empty(),
          "a zero-width raster is refused with a reason");

    error.clear();
    Check(RasterizeModelFile("no-such-file.stl", options, error) == nullptr && !error.empty(),
          "an unreadable path reports an error");

    error.clear();
    Check(RasterizeModelFile("notes.txt", options, error) == nullptr && !error.empty(),
          "and so does a format no reader in this build handles");

    // ----- the mesh already in hand -----
    // What the 3D import dialog takes: the viewer holds the mesh it loaded, so
    // the file is not parsed a second time. It has to give the same picture.
    error.clear();
    auto fromMesh = RasterizeMesh(Cube(), options, error);
    Check(fromMesh != nullptr, "a mesh already in hand rasterizes without a file: " + error);
    if (fromMesh) Check(InkOf(fromMesh).drawn > 0, "and it draws something");

    error.clear();
    Check(RasterizeMesh(Mesh3D{}, options, error) == nullptr && !error.empty(),
          "an empty mesh is refused rather than returned as a blank layer");
}

int main() {
    TestRefusals();
    TestGeometryOfTheOutput();
    TestShadingAndPose();
    TestTwoSided();

    const std::string stlPath = WriteTempStl();
    TestWhatIsAModel();
    TestInspection(stlPath);
    TestFileToLayer(stlPath);
    std::filesystem::remove(stlPath);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
