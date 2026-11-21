// Tests/VectorExportFixtures.cpp
// Writes the drawings the LibreOffice check renders: each fixture through
// every vector writer this build has, and a manifest saying what must be
// where once another program has drawn them.
//
// scripts/check_libreoffice_render.py runs this, converts each file to PNG
// with LibreOffice, finds the drawing on the page and checks each probe.
// It catches a writer whose output reads back in our own reader but not in
// someone else's: before the fill-rule fix, the EMF and WMF writers set
// WINDING, which LibreOffice ignores, and the CDR and ODG writers lost a
// ring's middle; our readers agreed with our writers, LibreOffice did not.
//
// The fixtures, each spanning (10, 10) - (290, 90) on a 300 x 100 pt page,
// so the script can map document points onto the page it is given:
//   rings   three rings, two circles in one path each: non-zero wound one
//           way (solid), even-odd (a hole), non-zero wound both ways (a hole)
//   shapes  a red rectangle, a green ellipse and a blue triangle
//
// Usage: VectorExportFixtures <output folder> [ext...]
//   with no extensions, every format the Vector plugin writes.
// Exit code: 0 when every file was written.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasVectorFormatsPlugin.h"
#include "UltraCanvasCADConverters.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;
using Rule = UltraCanvas::VectorStorage::FillRule;

namespace {

void Circle(VectorPath& p, double cx, double cy, double r, bool clockwise) {
    const double k = 0.5522847498 * r;
    const float x = static_cast<float>(cx), y = static_cast<float>(cy), R = static_cast<float>(r),
                K = static_cast<float>(k);
    p.MoveTo(x + R, y);
    if (clockwise) {
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

std::shared_ptr<VectorDocument> Page() {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{300, 100};
    doc->ViewBox = Rect2Dd(0, 0, 300, 100);
    return doc;
}

std::shared_ptr<VectorDocument> Rings() {
    auto doc = Page();
    auto layer = doc->AddLayer("Rings");
    auto ring = [&](double cx, bool innerClockwise, Rule rule) {
        auto p = std::make_shared<VectorPath>();
        Circle(*p, cx, 50, 40, true);
        Circle(*p, cx, 50, 20, innerClockwise);
        p->Style.Fill = Color(0, 0, 0, 255);
        p->Style.ShapeFillRule = rule;
        layer->AddChild(p);
    };
    ring(50, true, Rule::NonZero);    // solid
    ring(150, true, Rule::EvenOdd);   // a hole
    ring(250, false, Rule::NonZero);  // a hole
    return doc;
}

std::shared_ptr<VectorDocument> Shapes() {
    auto doc = Page();
    auto layer = doc->AddLayer("Shapes");
    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd(10, 10, 80, 80);
    rect->Style.Fill = Color(220, 0, 0, 255);
    layer->AddChild(rect);
    auto ellipse = std::make_shared<VectorEllipse>();
    ellipse->Center = Point2Dd(150, 50);
    ellipse->RadiusX = 40;
    ellipse->RadiusY = 40;
    ellipse->Style.Fill = Color(0, 160, 0, 255);
    layer->AddChild(ellipse);
    auto triangle = std::make_shared<VectorPolygon>();
    triangle->Points = {Point2Dd(210, 90), Point2Dd(250, 10), Point2Dd(290, 90)};
    triangle->Style.Fill = Color(0, 0, 220, 255);
    layer->AddChild(triangle);
    return doc;
}

// What must be where, in document points: a colour (r, g, b) or "dark" /
// "light", with a reason a failure can name.
struct Probe {
    int x, y;   // whole points: written as integers, whatever the locale
    std::string expect, what;
};

const std::vector<Probe> kRingProbes = {
    {50, 50, "dark", "non-zero ring wound one way: its middle is filled"},
    {50, 18, "dark", "non-zero ring wound one way: its band"},
    {150, 50, "light", "even-odd ring: its hole"},
    {150, 18, "dark", "even-odd ring: its band"},
    {250, 50, "light", "non-zero ring wound both ways: its hole"},
    {250, 18, "dark", "non-zero ring wound both ways: its band"},
    {100, 50, "light", "between the rings"},
};

const std::vector<Probe> kShapeProbes = {
    {50, 50, "220,0,0", "the red rectangle"},
    {150, 50, "0,160,0", "the green ellipse"},
    {250, 70, "0,0,220", "the blue triangle"},
    {100, 50, "light", "between the rectangle and the ellipse"},
    {215, 20, "light", "beside the triangle's tip"},
};

std::string Json(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

}   // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: VectorExportFixtures <output folder> [ext...]\n");
        return 2;
    }
    UCImage::InitializeImageSubsysterm("VectorExportFixtures");
    RegisterVectorFormatsPlugin();
    const std::filesystem::path dir = PathFromUtf8(argv[1]);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::vector<std::string> exts;
    for (int i = 2; i < argc; ++i) exts.emplace_back(argv[i]);
    if (exts.empty()) exts = UltraCanvasVectorFormatsPlugin().GetSaveExtensions();

    struct Fixture {
        const char* name;
        std::shared_ptr<VectorDocument> doc;
        const std::vector<Probe>* probes;
    };
    const std::vector<Fixture> fixtures = {{"rings", Rings(), &kRingProbes}, {"shapes", Shapes(), &kShapeProbes}};

    int failed = 0;
    std::ostringstream files;
    bool first = true;
    for (std::string ext : exts) {
        if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(ext);
        if (!converter || !converter->CanExport()) continue;
        // The DWG family (.dwg, .dwt, .dws, .sv$) is written through
        // LibreDWG's dxf2dwg, when it is installed.
        if (dynamic_cast<VectorConverter::DWGConverter*>(converter.get()) &&
            VectorConverter::DWGConverter::FindDxf2Dwg().empty()) {
            std::printf("skip .%s: LibreDWG's dxf2dwg is not installed\n", ext.c_str());
            continue;
        }
        for (const auto& f : fixtures) {
            const std::string file = std::string(f.name) + "." + ext;
            const bool ok = UltraCanvasVectorFormatsPlugin::SaveVectorDocument(*f.doc, PathToUtf8(dir / file));
            std::printf("%s %s\n", ok ? "wrote" : "FAILED to write", file.c_str());
            if (!ok) {
                ++failed;
                continue;
            }
            files << (first ? "" : ",\n") << "    {\"file\": " << Json(file) << ", \"format\": " << Json(ext)
                  << ", \"fixture\": " << Json(f.name) << "}";
            first = false;
        }
    }

    std::ofstream manifest(dir / "manifest.json");
    manifest << "{\n  \"page\": [300, 100],\n  \"content\": [10, 10, 290, 90],\n  \"fixtures\": {\n";
    for (size_t i = 0; i < fixtures.size(); ++i) {
        manifest << "    " << Json(fixtures[i].name) << ": [\n";
        const auto& probes = *fixtures[i].probes;
        for (size_t j = 0; j < probes.size(); ++j)
            manifest << "      {\"x\": " << probes[j].x << ", \"y\": " << probes[j].y << ", \"expect\": "
                     << Json(probes[j].expect) << ", \"what\": " << Json(probes[j].what) << "}"
                     << (j + 1 < probes.size() ? ",\n" : "\n");
        manifest << "    ]" << (i + 1 < fixtures.size() ? ",\n" : "\n");
    }
    manifest << "  },\n  \"files\": [\n" << files.str() << "\n  ]\n}\n";
    return failed;
}
