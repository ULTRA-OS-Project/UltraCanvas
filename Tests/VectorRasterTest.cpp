// Tests/VectorRasterTest.cpp
// UltraCanvasVectorRaster: inspecting a vector file for its natural size and
// rasterizing it into a UCRasterLayer at a chosen pixel size.
//
// The rules this guards: the natural size is the size the artwork asks for
// (not whatever the loader felt like); a requested size is delivered exactly;
// one requested dimension keeps the aspect ratio; the background is composited
// under the drawing rather than replacing it; and an absurd size is refused
// instead of allocated.
//
// RasterizeVectorElements (a drawing program's copy as a picture): the layer
// is cropped to what the elements paint, a stroke past their bounds is kept,
// the scale is honoured, an ancestor's transform puts an element where it
// sits in the document, and nothing visible is an error, not an empty layer.
// Version: 1.1.0 - RasterizeVectorElements
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "UltraCanvasVectorRaster.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "UltraCanvasPathUtf8.h"

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// 100 x 50: a red rectangle over the left half, the right half left empty so
// the background test has transparency to composite against.
const char* kSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"50\">"
    "<rect x=\"0\" y=\"0\" width=\"50\" height=\"50\" fill=\"#FF0000\"/>"
    "</svg>";

std::string WriteTempSvg() {
    const fs::path path = fs::temp_directory_path() / "ultracanvas-vector-raster-test.svg";
    std::ofstream out(path, std::ios::binary);
    out << kSvg;
    out.close();
    return path.string();
}

bool IsColour(const RasterPixel& p, uint8_t r, uint8_t g, uint8_t b) {
    return p.r == r && p.g == g && p.b == b && p.a == 255;
}

// Needs no rasterizer of files: the elements are drawn by the framework's own
// vector renderer, so this runs on every build.
void TestElements() {
    using namespace UltraCanvas::VectorStorage;
    std::cout << "--- RasterizeVectorElements ---\n";
    VectorDocument doc;
    doc.Size = Size2Dd{200, 100};
    doc.ViewBox = Rect2Dd{0, 0, 200, 100};
    auto layer = doc.AddLayer("Layer 1");

    auto red = std::make_shared<VectorRect>();
    red->Bounds = Rect2Dd(10, 20, 40, 30);
    red->Style.Fill = Color(255, 0, 0, 255);
    layer->AddChild(red);

    auto blue = std::make_shared<VectorRect>();
    blue->Bounds = Rect2Dd(100, 20, 20, 20);
    blue->Style.Fill = Color(0, 0, 255, 255);
    StrokeData stroke;
    stroke.Fill = Color(0, 0, 0, 255);
    stroke.Width = 10;
    blue->Style.Stroke = stroke;
    layer->AddChild(blue);

    std::string error;
    Rect2Dd area;
    auto one = RasterizeVectorElements(doc, {red}, 1.0, error, &area);
    Check(one && one->GetWidth() == 40 && one->GetHeight() == 30,
          "one element is cropped to what it paints: 40 x 30 (got " +
          (one ? std::to_string(one->GetWidth()) + " x " + std::to_string(one->GetHeight()) : error) + ")");
    Check(one && IsColour(one->GetPixel(0, 0), 255, 0, 0) && IsColour(one->GetPixel(39, 29), 255, 0, 0),
          "its corners are the rectangle's red");
    Check(area.x == 10 && area.y == 20 && area.width == 40 && area.height == 30,
          "documentArea is where it sits in the document");

    auto twice = RasterizeVectorElements(doc, {red}, 2.0, error);
    Check(twice && twice->GetWidth() == 80 && twice->GetHeight() == 60, "two pixels a point doubles the size");

    auto stroked = RasterizeVectorElements(doc, {blue}, 1.0, error);
    Check(stroked && stroked->GetWidth() == 30 && stroked->GetHeight() == 30,
          "a 10-point stroke past the bounds is kept: 20 + 2 x 5 (got " +
          (stroked ? std::to_string(stroked->GetWidth()) + " x " + std::to_string(stroked->GetHeight()) : error) + ")");
    Check(stroked && IsColour(stroked->GetPixel(0, 0), 0, 0, 0) && IsColour(stroked->GetPixel(15, 15), 0, 0, 255),
          "the stroke is black and the fill blue");

    auto both = RasterizeVectorElements(doc, {red, blue}, 1.0, error, &area);
    Check(both && both->GetWidth() == 115 && both->GetHeight() == 35,
          "two elements span from the first's left to the second's stroke (got " +
          (both ? std::to_string(both->GetWidth()) + " x " + std::to_string(both->GetHeight()) : error) + ")");
    Check(both && both->GetPixel(60, 10).a == 0, "the gap between them stays transparent");

    // An element inside a moved group is drawn where the group puts it.
    auto group = std::make_shared<VectorGroup>();
    group->Transform = Matrix3x3::Translate(50, 0);
    layer->AddChild(group);
    auto green = std::make_shared<VectorRect>();
    green->Bounds = Rect2Dd(0, 0, 10, 10);
    green->Style.Fill = Color(0, 255, 0, 255);
    group->AddChild(green);
    auto moved = RasterizeVectorElements(doc, {green}, 1.0, error, &area);
    Check(moved && moved->GetWidth() == 10 && area.x == 50 && area.y == 0,
          "an ancestor's transform places the element (x " + std::to_string(area.x) + ")");

    auto hidden = std::make_shared<VectorRect>();
    hidden->Bounds = Rect2Dd(0, 0, 10, 10);
    layer->AddChild(hidden);   // no fill, no stroke: paints nothing
    Check(!RasterizeVectorElements(doc, {hidden}, 1.0, error) && !error.empty(),
          "nothing visible is refused with a reason");
    Check(!RasterizeVectorElements(doc, {}, 1.0, error), "no elements is refused");
    Check(!RasterizeVectorElements(doc, {red}, 0.0, error), "a zero scale is refused");
}

} // namespace

int main() {
    std::cout << "=== UltraCanvasVectorRaster ===\n";
    TestElements();

    const std::string svgPath = WriteTempSvg();
    const std::vector<std::string> extensions = GetVectorRasterExtensions();
    std::cout << "  rasterizable vector extensions in this build:";
    for (const auto& e : extensions) std::cout << " " << e;
    std::cout << (extensions.empty() ? " (none)" : "") << "\n";

    if (!IsVectorGraphicsPath(svgPath)) {
        // No SVG rasterizer compiled in (no libvips, or a libvips without
        // librsvg). Everything below needs one, so report and stop - a build
        // that cannot rasterize SVG is a configuration, not a failure.
        std::cout << "  [SKIP] this build has no SVG rasterizer\n";
        Check(!IsVectorGraphicsPath("notes.txt"), "a plain text file is not vector artwork");
        fs::remove(UltraCanvas::PathFromUtf8(svgPath));
        return g_failures == 0 ? 0 : 1;
    }

    Check(!IsVectorGraphicsPath("notes.txt"), "a plain text file is not vector artwork");
    Check(!IsVectorGraphicsPath("photo.png"), "a bitmap is not vector artwork");

    // ===== INSPECTION =====
    const VectorSourceInfo info = InspectVectorFile(svgPath);
    Check(info.ok, "the test SVG can be rasterized");
    Check(info.source == VectorRasterSource::ImagePipeline,
          std::string("SVG goes through the image pipeline (got: ") +
          VectorRasterSourceName(info.source) + ")");
    Check(info.naturalWidth == 100 && info.naturalHeight == 50,
          "natural size is 100 x 50 (got " + std::to_string(info.naturalWidth) + " x " +
          std::to_string(info.naturalHeight) + ")");
    Check(info.pageCount == 1, "an SVG is a single page");

    // ===== NATURAL SIZE =====
    std::string error;
    VectorRasterOptions options;
    auto natural = RasterizeVectorFile(svgPath, options, error);
    Check(natural != nullptr, "rasterizes with no options set: " + error);
    if (natural) {
        Check(natural->GetWidth() == 100 && natural->GetHeight() == 50,
              "no size asked for gives the natural size (got " +
              std::to_string(natural->GetWidth()) + " x " + std::to_string(natural->GetHeight()) + ")");
        const RasterPixel left = natural->GetPixel(25, 25);
        const RasterPixel right = natural->GetPixel(75, 25);
        Check(left.r > 200 && left.g < 60 && left.b < 60 && left.a == 255,
              "the drawn half is opaque red");
        Check(right.a == 0, "the empty half stays transparent");
    }

    // ===== AN EXACT SIZE, RENDERED AT THAT RESOLUTION =====
    options.width = 400;
    options.height = 200;
    auto scaled = RasterizeVectorFile(svgPath, options, error);
    Check(scaled != nullptr, "rasterizes at 400 x 200: " + error);
    if (scaled) {
        Check(scaled->GetWidth() == 400 && scaled->GetHeight() == 200,
              "the requested size is delivered exactly (got " +
              std::to_string(scaled->GetWidth()) + " x " + std::to_string(scaled->GetHeight()) + ")");
        const RasterPixel left = scaled->GetPixel(100, 100);
        Check(left.r > 200 && left.g < 60 && left.b < 60 && left.a == 255,
              "the drawing scales with the raster, it is not padded");
    }

    // ===== ONE DIMENSION KEEPS THE ASPECT RATIO =====
    options.width = 300;
    options.height = 0;
    auto aspect = RasterizeVectorFile(svgPath, options, error);
    Check(aspect != nullptr, "rasterizes with only a width: " + error);
    if (aspect) {
        Check(aspect->GetWidth() == 300 && aspect->GetHeight() == 150,
              "a width alone keeps the aspect ratio (got " +
              std::to_string(aspect->GetWidth()) + " x " + std::to_string(aspect->GetHeight()) + ")");
    }

    // ===== BACKGROUND =====
    options.width = 100;
    options.height = 50;
    options.background = RasterPixel(255, 255, 255, 255);
    auto onWhite = RasterizeVectorFile(svgPath, options, error);
    Check(onWhite != nullptr, "rasterizes onto a white background: " + error);
    if (onWhite) {
        const RasterPixel right = onWhite->GetPixel(75, 25);
        const RasterPixel left = onWhite->GetPixel(25, 25);
        Check(right.r == 255 && right.g == 255 && right.b == 255 && right.a == 255,
              "the empty half becomes the background colour");
        Check(left.r > 200 && left.g < 60 && left.b < 60,
              "the background goes under the drawing, not over it");
    }
    options.background = RasterPixel(0, 0, 0, 0);

    // ===== REFUSING AN ABSURD SIZE =====
    options.width = 100000;
    options.height = 100000;
    error.clear();
    auto huge = RasterizeVectorFile(svgPath, options, error);
    Check(huge == nullptr && !error.empty(),
          "a 10-gigapixel request is refused with a reason, not attempted");

    // ===== A FILE NOTHING READS =====
    error.clear();
    auto missing = RasterizeVectorFile("no-such-file.dxf", VectorRasterOptions(), error);
    Check(missing == nullptr && !error.empty(), "an unreadable path reports an error");

    fs::remove(UltraCanvas::PathFromUtf8(svgPath));

    std::cout << (g_failures == 0 ? "=== PASSED ===\n" : "=== FAILED ===\n");
    return g_failures == 0 ? 0 : 1;
}
