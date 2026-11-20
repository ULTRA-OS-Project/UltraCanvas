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
//
// RasterizeVectorDocument (a drawing program's export as a bitmap): the whole
// page at the size asked for, drawn there rather than stretched - an edge on
// the page stays one sharp pixel step at twenty times the size - each side
// scaled on its own, the background under it, and no page is an error.
//
// A password-protected PDF (Tests/fixtures/pdf-aes256-locked.pdf, "tulip"):
// without its password it reports passwordRequired - not just "damaged" -
// a wrong one passwordWrong, and the right one measures and rasterizes it.
// Version: 1.4.0 - gradients drawn with their Transform and spread method
// Version: 1.3.0 - a password-protected PDF
// Version: 1.2.0 - RasterizeVectorDocument
// Version: 1.1.0 - RasterizeVectorElements
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "UltraCanvasVectorRaster.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

    // A line far wider than the object it runs along: a 100-point line 10
    // points long, round-capped, is 110 x 100 - more than any fixed margin
    // round its 10 x 0 middle.
    auto fat = std::make_shared<VectorLine>();
    fat->Id = "fat";
    fat->Start = Point2Dd(150, 50);
    fat->End = Point2Dd(160, 50);
    StrokeData fatLine;
    fatLine.Fill = Color(0, 0, 0, 255);
    fatLine.Width = 100;
    fatLine.LineCap = StrokeLineCap::Round;
    fat->Style.Stroke = fatLine;
    layer->AddChild(fat);
    auto wide = RasterizeVectorElements(doc, {fat}, 1.0, error, &area);
    Check(wide && wide->GetWidth() == 110 && wide->GetHeight() == 100,
          "a 100-point line is kept whole: 110 x 100 (got " +
          (wide ? std::to_string(wide->GetWidth()) + " x " + std::to_string(wide->GetHeight()) : error) + ")");
    layer->RemoveChild("fat");

    // A shadow thrown far from a small object is kept too.
    auto cast = std::make_shared<VectorRect>();
    cast->Id = "cast";
    cast->Bounds = Rect2Dd(10, 10, 10, 10);
    cast->Style.Fill = Color(255, 0, 0, 255);
    ShadowEffect shadow;
    shadow.Offset = Point2Dd(120, 0);
    shadow.Blur = 0;
    shadow.Darkness = 1.0f;
    cast->Effects.Shadow = shadow;
    layer->AddChild(cast);
    auto shadowed = RasterizeVectorElements(doc, {cast}, 1.0, error);
    Check(shadowed && shadowed->GetWidth() == 130,
          "a shadow 120 points away is kept: 10 + 120 wide (got " +
          (shadowed ? std::to_string(shadowed->GetWidth()) : error) + ")");
    layer->RemoveChild("cast");

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

// The framework's own renderer again, so this runs on every build too.
void TestDocument() {
    using namespace UltraCanvas::VectorStorage;
    std::cout << "--- RasterizeVectorDocument ---\n";
    // A 20 x 10 page, its left half black; the right half is empty.
    VectorDocument doc;
    doc.Size = Size2Dd{20, 10};
    doc.ViewBox = Rect2Dd{0, 0, 20, 10};
    auto layer = doc.AddLayer("Layer 1");
    auto black = std::make_shared<VectorRect>();
    black->Bounds = Rect2Dd(0, 0, 10, 10);
    black->Style.Fill = Color(0, 0, 0, 255);
    layer->AddChild(black);

    std::string error;
    VectorRasterOptions natural;
    auto page = RasterizeVectorDocument(doc, natural, error);
    Check(page && page->GetWidth() == 20 && page->GetHeight() == 10,
          "no size asked: a pixel a unit, 20 x 10 (got " +
          (page ? std::to_string(page->GetWidth()) + " x " + std::to_string(page->GetHeight()) : error) + ")");

    VectorRasterOptions big;
    big.width = 400;
    big.height = 200;
    auto large = RasterizeVectorDocument(doc, big, error);
    Check(large && large->GetWidth() == 400 && large->GetHeight() == 200, "the size asked for: 400 x 200");
    if (large) {
        int between = 0;
        for (int x = 0; x < 400; ++x) {
            const RasterPixel p = large->GetPixel(x, 100);
            if (p.a > 20 && p.a < 235) ++between;
        }
        Check(between <= 2, "drawn at that size, not stretched: the edge is one sharp step (" +
                            std::to_string(between) + " soft pixels)");
        Check(IsColour(large->GetPixel(100, 100), 0, 0, 0) && large->GetPixel(300, 100).a == 0,
              "the left half black, the right half clear");
    }

    VectorRasterOptions wide;
    wide.width = 100;
    auto followed = RasterizeVectorDocument(doc, wide, error);
    Check(followed && followed->GetWidth() == 100 && followed->GetHeight() == 50,
          "a width alone: the height follows the page's shape");

    VectorRasterOptions square;
    square.width = 100;
    square.height = 100;
    auto stretched = RasterizeVectorDocument(doc, square, error);
    Check(stretched && stretched->GetWidth() == 100 && stretched->GetHeight() == 100 &&
          IsColour(stretched->GetPixel(45, 90), 0, 0, 0) && stretched->GetPixel(55, 90).a == 0,
          "a size in another shape stretches the page to it");

    VectorRasterOptions white = big;
    white.background = RasterPixel(255, 255, 255, 255);
    auto backed = RasterizeVectorDocument(doc, white, error);
    Check(backed && IsColour(backed->GetPixel(300, 100), 255, 255, 255) &&
          IsColour(backed->GetPixel(100, 100), 0, 0, 0),
          "the background goes under the drawing");

    doc.BackgroundColor = Color(0, 0, 255, 255);
    auto blue = RasterizeVectorDocument(doc, natural, error);
    Check(blue && IsColour(blue->GetPixel(15, 5), 0, 0, 255), "the document's own background colour is drawn");

    VectorDocument empty;
    Check(!RasterizeVectorDocument(empty, natural, error) && !error.empty(), "no page size is refused with a reason");
    VectorRasterOptions huge;
    huge.width = 100000;
    huge.height = 100000;
    Check(!RasterizeVectorDocument(doc, huge, error), "an absurd size is refused");
}

} // namespace

// Gradients drawn as their data says: a Transform of their own (SVG's
// gradientTransform, the scaled circle an elliptical gradient is) and their
// spread method. The renderer used to drop both, so an ellipse came out a
// circle, a rotated ramp unrotated, and a repeating one padded.
void TestGradients() {
    using namespace UltraCanvas::VectorStorage;
    std::cout << "--- gradient transforms and spread ---\n";
    auto page = [](FillData fill) {
        VectorDocument doc;
        doc.Size = Size2Dd{200, 100};
        doc.ViewBox = Rect2Dd{0, 0, 200, 100};
        auto box = std::make_shared<VectorRect>();
        box->Bounds = Rect2Dd(0, 0, 200, 100);
        box->Style.Fill = fill;
        doc.AddLayer("Layer 1")->AddChild(box);
        std::string error;
        return RasterizeVectorDocument(doc, VectorRasterOptions(), error);
    };
    auto grey = [](const RasterPixel& p) { return static_cast<int>(p.r); };
    const std::vector<GradientStop> blackToWhite = {GradientStop(0, Color(0, 0, 0, 255)),
                                                    GradientStop(1, Color(255, 255, 255, 255))};

    // A radius-1 circle at the origin, placed by its Transform as a 100 x 50
    // ellipse at the page's centre: 0.6 of the way out is the same grey along
    // both axes - 60 units right, 30 units down.
    RadialGradientData ellipse;
    ellipse.Units = GradientUnits::UserSpaceOnUse;
    ellipse.Center = ellipse.FocalPoint = Point2Dd(0, 0);
    ellipse.Radius = 1;
    ellipse.Stops = blackToWhite;
    ellipse.Transform = Matrix3x3::Translate(100, 50) * Matrix3x3::Scale(100, 50);
    if (auto img = page(FillData(GradientData(ellipse)))) {
        const int across = grey(img->GetPixel(160, 50)), down = grey(img->GetPixel(100, 80));
        Check(std::abs(across - 153) <= 12 && std::abs(down - 153) <= 12,
              "an elliptical radial: 0.6 of each radius is the same grey (" + std::to_string(across) + ", " +
                  std::to_string(down) + ", 153 expected)");
        Check(grey(img->GetPixel(100, 50)) < 10, "black at its centre");
    } else {
        Check(false, "the elliptical radial draws");
    }

    // A left-to-right ramp in the bounding box, turned a quarter about the
    // box's centre by its gradientTransform: it runs top to bottom.
    LinearGradientData turned;
    turned.Units = GradientUnits::ObjectBoundingBox;
    turned.Start = Point2Dd(0, 0);
    turned.End = Point2Dd(1, 0);
    turned.Stops = blackToWhite;
    turned.Transform = Matrix3x3::Translate(0.5, 0.5) * Matrix3x3::RotateDegrees(90) * Matrix3x3::Translate(-0.5, -0.5);
    if (auto img = page(FillData(GradientData(turned)))) {
        const int top = grey(img->GetPixel(100, 5)), bottom = grey(img->GetPixel(100, 95));
        const int left = grey(img->GetPixel(5, 50)), right = grey(img->GetPixel(195, 50));
        Check(top < 30 && bottom > 225, "a bounding-box ramp turned a quarter runs top to bottom (" +
                                            std::to_string(top) + " .. " + std::to_string(bottom) + ")");
        Check(std::abs(left - right) <= 4, "and is level across");
    } else {
        Check(false, "the turned ramp draws");
    }

    // A 20-unit ramp repeated across the page: a quarter into its second
    // cycle is a quarter grey, where padding would have left it white.
    LinearGradientData repeated;
    repeated.Units = GradientUnits::UserSpaceOnUse;
    repeated.Start = Point2Dd(0, 0);
    repeated.End = Point2Dd(20, 0);
    repeated.Stops = blackToWhite;
    repeated.SpreadMethod = GradientSpreadMethod::Repeat;
    if (auto img = page(FillData(GradientData(repeated)))) {
        const int v = grey(img->GetPixel(25, 50));
        Check(std::abs(v - 64) <= 16, "spreadMethod repeat starts the ramp over (" + std::to_string(v) + ", 64 expected)");
    } else {
        Check(false, "the repeating ramp draws");
    }
    repeated.SpreadMethod = GradientSpreadMethod::Reflect;
    if (auto img = page(FillData(GradientData(repeated)))) {
        const int v = grey(img->GetPixel(25, 50));
        Check(std::abs(v - 191) <= 16, "spreadMethod reflect runs it back (" + std::to_string(v) + ", 191 expected)");
    } else {
        Check(false, "the reflected ramp draws");
    }
}

int main() {
    std::cout << "=== UltraCanvasVectorRaster ===\n";
    TestElements();
    TestDocument();
    TestGradients();

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

    // ===== A PASSWORD-PROTECTED PDF =====
    {
        const std::string pdf = std::string(UC_FIXTURE_DIR) + "/pdf-aes256-locked.pdf";
        if (!IsVectorGraphicsPath(pdf)) {
            std::cout << "  [SKIP] this build has no PDF rasterizer\n";
        } else {
            const VectorSourceInfo locked = InspectVectorFile(pdf);
            Check(!locked.ok && locked.passwordRequired && !locked.passwordWrong,
                  "a protected PDF without its password says it needs one (" + locked.error + ")");
            const VectorSourceInfo wrong = InspectVectorFile(pdf, "tulpi");
            Check(!wrong.ok && wrong.passwordRequired && wrong.passwordWrong,
                  "a wrong password is reported as wrong");
            const VectorSourceInfo right = InspectVectorFile(pdf, "tulip");
            Check(right.ok && right.naturalWidth == 200 && right.naturalHeight == 200,
                  "the right password measures the page: 200 x 200");

            VectorRasterOptions pdfOptions;
            pdfOptions.width = 100;
            error.clear();
            Check(!RasterizeVectorFile(pdf, pdfOptions, error) && !error.empty(),
                  "rasterizing it without the password is refused with a reason");
            pdfOptions.password = "tulip";
            error.clear();
            auto page = RasterizeVectorFile(pdf, pdfOptions, error);
            Check(page && page->GetWidth() == 100 && page->GetHeight() == 100,
                  "with its password it rasterizes at 100 x 100: " + error);
            if (page) {
                // The blue square: 40..160 of 200 pt, so 20..80 of 100 px.
                const RasterPixel middle = page->GetPixel(50, 50);
                Check(middle.b > 180 && middle.r < 80 && middle.a == 255, "and draws its page");
            }
            const VectorSourceInfo plain = InspectVectorFile(svgPath, "tulip");
            Check(plain.ok && !plain.passwordRequired, "a password given to a file without one is ignored");
        }
    }

    // ===== A FILE NOTHING READS =====
    error.clear();
    auto missing = RasterizeVectorFile("no-such-file.dxf", VectorRasterOptions(), error);
    Check(missing == nullptr && !error.empty(), "an unreadable path reports an error");

    fs::remove(UltraCanvas::PathFromUtf8(svgPath));

    std::cout << (g_failures == 0 ? "=== PASSED ===\n" : "=== FAILED ===\n");
    return g_failures == 0 ? 0 : 1;
}
