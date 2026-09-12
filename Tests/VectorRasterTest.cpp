// Tests/VectorRasterTest.cpp
// UltraCanvasVectorRaster: inspecting a vector file for its natural size and
// rasterizing it into a UCRasterLayer at a chosen pixel size.
//
// The rules this guards: the natural size is the size the artwork asks for
// (not whatever the loader felt like); a requested size is delivered exactly;
// one requested dimension keeps the aspect ratio; the background is composited
// under the drawing rather than replacing it; and an absurd size is refused
// instead of allocated.
// Version: 1.0.0
// Last Modified: 2026-09-12
// Author: UltraCanvas Framework

#include "UltraCanvasVectorRaster.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

} // namespace

int main() {
    std::cout << "=== UltraCanvasVectorRaster ===\n";

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
        fs::remove(svgPath);
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

    fs::remove(svgPath);

    std::cout << (g_failures == 0 ? "=== PASSED ===\n" : "=== FAILED ===\n");
    return g_failures == 0 ? 0 : 1;
}
