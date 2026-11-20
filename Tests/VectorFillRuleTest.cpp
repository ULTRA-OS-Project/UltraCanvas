// Tests/VectorFillRuleTest.cpp
// A shape's fill rule (VectorStyle::ShapeFillRule) through every vector
// writer that has a reader. Three rings, each two circles in one path:
//   A  non-zero, both circles wound the same way   -> filled solid
//   B  even-odd, both circles wound the same way   -> a hole in the middle
//   C  non-zero, the circles wound opposite ways   -> a hole in the middle
// are written, read back and drawn, and each ring's middle and band are
// checked. EMF and WMF carry the rule itself (SETPOLYFILLMODE); XAR, CDR,
// DXF and ODG fill only even-odd, so A goes out as the subpaths that bound
// its fill (UltraCanvasVectorFillRule.h) and must still come back solid.
// The helper is also checked on its own: which subpaths it keeps, and that
// it reports subpaths that cross.
//
// Usage: VectorFillRuleTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFormatsPlugin.h"
#include "UltraCanvasVectorFillRule.h"
#include "UltraCanvasVectorRaster.h"
#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "DataFormats/UltraCanvasVectorPathOps.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;
using UltraCanvas::VectorConverter::NonZeroAsEvenOdd;
namespace PathOps = UltraCanvas::VectorConverter::PathOps;
using Rule = UltraCanvas::VectorStorage::FillRule;   // UltraCanvas::FillRule is the render context's

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// A circle as four cubics, clockwise on the page or counter-clockwise.
void Circle(VectorPath& p, double cx, double cy, double r, bool clockwise) {
    const double k = 0.5522847498 * r;
    const float x = static_cast<float>(cx), y = static_cast<float>(cy), R = static_cast<float>(r),
                K = static_cast<float>(k);
    p.MoveTo(x + R, y);
    if (clockwise) {   // y down: right, bottom, left, top
        p.CurveTo(x + R, y + K, x + K, y + R, x, y + R);
        p.CurveTo(x - K, y + R, x - R, y + K, x - R, y);
        p.CurveTo(x - R, y - K, x - K, y - R, x, y - R);
        p.CurveTo(x + K, y - R, x + R, y - K, x + R, y);
    } else {
        p.CurveTo(x + R, y - K, x + K, y - R, x, y - R);
        p.CurveTo(x - K, y - R, x - R, y - K, x - R, y);
        p.CurveTo(x - R, y + K, x - K, y + R, x, y + R);
        p.CurveTo(x + K, y + R, x + R, y + K, x + R, y);
    }
    p.ClosePath();
}

std::shared_ptr<VectorPath> Ring(double cx, bool innerClockwise, Rule rule) {
    auto p = std::make_shared<VectorPath>();
    Circle(*p, cx, 50, 40, true);
    Circle(*p, cx, 50, 20, innerClockwise);
    p->Style.Fill = Color(0, 0, 0, 255);
    p->Style.ShapeFillRule = rule;
    return p;
}

std::shared_ptr<VectorDocument> Rings() {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{300, 100};
    doc->ViewBox = Rect2Dd(0, 0, 300, 100);
    auto layer = doc->AddLayer("Rings");
    layer->AddChild(Ring(50, true, Rule::NonZero));    // A: solid
    layer->AddChild(Ring(150, true, Rule::EvenOdd));   // B: hole
    layer->AddChild(Ring(250, false, Rule::NonZero));  // C: hole
    return doc;
}

// Dark at (x, y) on the document drawn at 3 pixels a unit, on white.
struct Drawn {
    std::shared_ptr<UCRasterLayer> layer;
    bool Dark(double x, double y) const {
        const RasterPixel p = layer->GetPixel(static_cast<int>(x * 3), static_cast<int>(y * 3));
        return p.r < 100 && p.g < 100 && p.b < 100;
    }
};

Drawn Draw(const VectorDocument& doc) {
    VectorRasterOptions o;
    o.width = 900;
    o.height = 300;
    o.background = RasterPixel(255, 255, 255, 255);
    std::string error;
    return {RasterizeVectorDocument(doc, o, error)};
}

void CheckRings(const Drawn& d, const std::string& format) {
    if (!d.layer) {
        Check(false, format + ": the drawing draws");
        return;
    }
    Check(d.Dark(50, 50) && d.Dark(50, 18), format + ": a non-zero ring wound one way is solid");
    Check(!d.Dark(150, 50) && d.Dark(150, 18), format + ": an even-odd one has its hole");
    Check(!d.Dark(250, 50) && d.Dark(250, 18), format + ": a non-zero one wound both ways has its hole");
}

void TestHelper() {
    std::printf("--- NonZeroAsEvenOdd ---\n");
    auto count = [](const std::vector<PathOps::FlatSeg>& segs) {
        int n = 0;
        for (const auto& s : segs) n += s.kind == PathOps::FlatSeg::Move;
        return n;
    };
    bool exact = false;
    auto same = NonZeroAsEvenOdd(PathOps::NormalizePath(Ring(50, true, Rule::NonZero)->Path), &exact);
    Check(count(same) == 1 && exact, "two circles wound alike: only the outer one bounds the fill");
    auto opposite = NonZeroAsEvenOdd(PathOps::NormalizePath(Ring(50, false, Rule::NonZero)->Path), &exact);
    Check(count(opposite) == 2 && exact, "wound opposite ways: both do");

    // An "O" inside an "O": outer and its counter alike, then a second ring
    // inside wound alike again - non-zero fills from the outer edge in, so
    // only the outermost circle is a boundary.
    VectorPath nested;
    Circle(nested, 50, 50, 40, true);
    Circle(nested, 50, 50, 30, true);
    Circle(nested, 50, 50, 20, false);
    Circle(nested, 50, 50, 10, false);
    auto n = NonZeroAsEvenOdd(PathOps::NormalizePath(nested.Path), &exact);
    // Windings from outside in: 1, 2, 1, 0 - filled, filled, filled, empty:
    // the outer edge and the innermost circle bound it.
    Check(count(n) == 2 && exact, "four nested circles wound + + - -: the outer and the innermost bound it (" +
                                      std::to_string(count(n)) + ")");

    VectorPath crossing;
    Circle(crossing, 40, 50, 30, true);
    Circle(crossing, 70, 50, 30, true);
    NonZeroAsEvenOdd(PathOps::NormalizePath(crossing.Path), &exact);
    Check(!exact, "two overlapping circles are reported as crossing");
}

}   // namespace

int main() {
    UCImage::InitializeImageSubsysterm("VectorFillRuleTest");
    TestHelper();

    auto doc = Rings();
    std::printf("--- the model ---\n");
    CheckRings(Draw(*doc), "model");

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "uc-fill-rule-test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    for (const char* ext : {"emf", "wmf", "dxf", "xar", "cdr", "odg", "svg"}) {
        std::printf("--- .%s ---\n", ext);
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(ext);
        if (!converter || !converter->CanExport() || !converter->CanImport()) {
            std::printf("  [SKIP] .%s has no reader in this build\n", ext);
            continue;
        }
        const std::string path = PathToUtf8(dir / (std::string("rings.") + ext));
        Check(UltraCanvasVectorFormatsPlugin::SaveVectorDocument(*doc, path), std::string(".") + ext + " written");
        auto back = UltraCanvasVectorFormatsPlugin::LoadVectorDocument(path);
        Check(back != nullptr, std::string(".") + ext + " read back");
        if (back) CheckRings(Draw(*back), std::string(".") + ext);
    }
    std::filesystem::remove_all(dir, ec);
    std::printf("%d failure(s)\n", failures);
    return failures;
}
