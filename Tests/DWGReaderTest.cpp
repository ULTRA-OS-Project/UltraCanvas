// Tests/DWGReaderTest.cpp
// Tests for the CAD import path: the DXF reader's block machinery (BLOCKS
// definitions, nested/scaled/rotated/mirrored/arrayed INSERTs, "0"-layer
// and ByBlock inheritance, DIMENSION blocks, LEADER, 3DFACE, 3D POLYLINE,
// layer/entity visibility, paper-space filtering, geometry-derived extents)
// through a synthetic drawing, and the native DWG decoder through the
// R2000 fixture in Tests/DataFormats (the framework's own test document,
// written by the DXF writer and converted with LibreDWG's dxf2dwg).
//
// Usage: DWGReaderTest [file.dwg ...]
// Extra DWG files are decoded and reported (entity histogram, page size);
// with DWG_TEST_SVG_DIR set, each is also exported as SVG there for a look.
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasCADConverters.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasDWGDecoder.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorConverter.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok: %s\n", what.c_str());
    }
}

// Every drawable in the imported document, layers/groups flattened.
void CollectDrawables(const VectorElement& e, std::vector<const VectorElement*>& out) {
    if (const auto* g = dynamic_cast<const VectorGroup*>(&e)) {
        for (const auto& child : g->Children) {
            if (child) CollectDrawables(*child, out);
        }
        return;
    }
    out.push_back(&e);
}

std::vector<const VectorElement*> Drawables(const VectorDocument& doc) {
    std::vector<const VectorElement*> out;
    for (const auto& layer : doc.Layers) {
        if (layer) CollectDrawables(*layer, out);
    }
    return out;
}

bool SameColor(const Color& a, const Color& b, int tol) {
    return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol && std::abs(a.b - b.b) <= tol;
}

bool HasFillColor(const VectorDocument& doc, const Color& want, int tol) {
    for (const VectorElement* e : Drawables(doc)) {
        if (!e->Style.Fill) continue;
        if (const Color* c = std::get_if<Color>(&*e->Style.Fill)) {
            if (SameColor(*c, want, tol)) return true;
        }
    }
    return false;
}

const Color* StrokeColor(const VectorElement* e) {
    if (!e->Style.Stroke) return nullptr;
    return std::get_if<Color>(&e->Style.Stroke->Fill);
}

const VectorText* FindText(const VectorDocument& doc) {
    for (const VectorElement* e : Drawables(doc)) {
        if (e->Type == VectorElementType::Text) return static_cast<const VectorText*>(e);
    }
    return nullptr;
}

bool HasDash(const VectorDocument& doc) {
    for (const VectorElement* e : Drawables(doc)) {
        if (e->Style.Stroke && !e->Style.Stroke->DashArray.empty()) return true;
    }
    return false;
}

std::string TextOf(const VectorText& t) {
    std::string s;
    for (const auto& span : t.Spans) s += span.Text;
    return s;
}

// The synthetic drawing: three block definitions and every construct the
// block machinery has to handle.
const char* kBlockDrawing = R"DXF(  0
SECTION
  2
HEADER
  0
ENDSEC
  0
SECTION
  2
TABLES
  0
TABLE
  2
LAYER
  0
LAYER
  2
Walls
 70
0
 62
1
  6
CONTINUOUS
  0
LAYER
  2
Hidden
 70
0
 62
-3
  0
LAYER
  2
Frozen
 70
1
 62
5
  0
ENDTAB
  0
ENDSEC
  0
SECTION
  2
BLOCKS
  0
BLOCK
  8
0
  2
UNIT
 70
0
 10
0
 20
0
  3
UNIT
  0
CIRCLE
  8
0
 62
0
 10
5
 20
5
 40
2
  0
LINE
  8
Walls
 10
0
 20
0
 11
10
 21
0
  0
ENDBLK
  0
BLOCK
  8
0
  2
PAIR
 70
0
 10
0
 20
0
  3
PAIR
  0
INSERT
  8
0
 62
0
  2
UNIT
 10
0
 20
0
  0
INSERT
  8
0
 62
0
  2
UNIT
 10
20
 20
0
  0
ENDBLK
  0
BLOCK
  8
0
  2
*D1
 70
1
 10
0
 20
0
  3
*D1
  0
LINE
  8
0
 10
0
 20
50
 11
30
 21
50
  0
TEXT
  8
0
 10
10
 20
52
 40
2.5
  1
30.00
  0
ENDBLK
  0
ENDSEC
  0
SECTION
  2
ENTITIES
  0
INSERT
  8
Walls
 62
2
  2
PAIR
 10
100
 20
100
 41
2
 42
2
 50
90
  0
INSERT
  8
Walls
  2
UNIT
 10
200
 20
100
210
0
220
0
230
-1
  0
INSERT
  8
Walls
  2
UNIT
 10
0
 20
0
 70
3
 71
2
 44
15
 45
15
  0
DIMENSION
  8
Walls
  2
*D1
 10
0
 20
0
 70
32
  0
LEADER
  8
Walls
 10
0
 20
0
 10
10
 20
10
 10
20
 20
10
  0
3DFACE
  8
Walls
 10
0
 20
0
 30
0
 11
10
 21
0
 31
0
 12
10
 22
10
 32
0
 13
10
 23
10
 33
0
  0
POLYLINE
  8
Walls
 66
1
 70
8
  0
VERTEX
  8
Walls
 10
0
 20
0
 30
5
 70
32
  0
VERTEX
  8
Walls
 10
10
 20
5
 30
7
 70
32
  0
VERTEX
  8
Walls
 10
20
 20
0
 30
9
 70
32
  0
SEQEND
  0
LINE
  8
Hidden
 10
0
 20
0
 11
1000
 21
1000
  0
LINE
  8
Frozen
 10
0
 20
0
 11
-1000
 21
1000
  0
LINE
  8
Walls
 60
1
 10
0
 20
0
 11
-1000
 21
-1000
  0
LINE
  8
Walls
 67
1
 10
5000
 20
5000
 11
6000
 21
6000
  0
INSERT
  8
Walls
  2
MISSING
 10
0
 20
0
  0
ENDSEC
  0
EOF
)DXF";

Point2Dd GlobalPoint(const VectorElement& e, const Point2Dd& p) {
    return e.GetGlobalTransform().Transform(p);
}

void TestBlockMachinery() {
    std::printf("== DXF block machinery\n");
    std::vector<std::string> warnings;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [&](const std::string& w) { warnings.push_back(w); };
    auto doc = VectorConverter::DXFConverter().ImportFromString(kBlockDrawing, options);
    Check(doc != nullptr, "synthetic drawing imports");
    if (!doc) return;

    auto drawables = Drawables(*doc);
    // array 3x2 -> 6 units x 2 = 12, nested PAIR -> 4, mirrored -> 2,
    // dimension block -> 2, leader, 3dface, 3D polyline -> 3
    Check(drawables.size() == 23,
          "23 drawables after block expansion (got " + std::to_string(drawables.size()) + ")");

    int circles = 0, yellow = 0, red = 0;
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    for (const VectorElement* e : drawables) {
        if (e->Type == VectorElementType::Circle) {
            ++circles;
            const auto* c = static_cast<const VectorCircle*>(e);
            if (const Color* col = StrokeColor(e)) {
                if (SameColor(*col, Color(255, 255, 0, 255), 2)) ++yellow;
                if (SameColor(*col, Color(255, 0, 0, 255), 2)) ++red;
            }
            Point2Dd g = GlobalPoint(*e, c->Center);
            minX = std::min(minX, g.x); maxX = std::max(maxX, g.x);
            minY = std::min(minY, g.y); maxY = std::max(maxY, g.y);
        }
    }
    Check(circles == 9, "9 circles (got " + std::to_string(circles) + ")");
    Check(yellow == 2, "ByBlock circles take the nested insert's colour (yellow x2, got " +
                       std::to_string(yellow) + ")");
    Check(red == 7, "ByBlock circles under ByLayer inserts take the layer colour (red x7, got " +
                    std::to_string(red) + ")");
    // Circle centres in world coordinates: mirrored insert at (-205, 105),
    // array from (5, 5) to (35, 20), nested insert at (90, 110) and
    // (90, 150) (the unit's (5,5) and (25,5) scaled by 2, rotated 90
    // degrees, moved to (100,100)). The spreads are what is checked.
    // The page is the geometry's extents plus a 1% margin: x from -210 to
    // 100 (mirrored insert to nested insert), y 0..160 - 316.2 x 166.2
    // drawing units, scaled so the page is 1000 points wide.
    const double unit = doc->Size.width / 316.2;
    Check(std::fabs(doc->Size.width - 1000) < 0.5,
          "a page derived from the extents is normalised to 1000 points (" +
          std::to_string(doc->Size.width) + ")");
    Check(std::fabs(doc->Size.height / unit - 166.2) < 1.0,
          "page height follows the real extents, block content included (" +
          std::to_string(doc->Size.height / unit) + " units)");
    Check(std::fabs((maxX - minX) / unit - 295) < 1.0,
          "circle centres span the mirrored/array/nested placements (span " +
          std::to_string((maxX - minX) / unit) + " units)");
    Check(std::fabs((maxY - minY) / unit - 145) < 1.0,
          "circle centres span vertically as placed (span " +
          std::to_string((maxY - minY) / unit) + " units)");

    const VectorText* text = FindText(*doc);
    Check(text && TextOf(*text) == "30.00", "dimension block text appears");
    bool missingReported = false;
    for (const auto& w : warnings) if (w.find("INSERT(missing block)") != std::string::npos) missingReported = true;
    Check(missingReported, "insert of a missing block is reported");
    bool polyline3d = false;
    for (const VectorElement* e : drawables) {
        if (e->Type == VectorElementType::Polyline) {
            const auto* p = static_cast<const VectorPolyline*>(e);
            if (p->Points.size() == 3) polyline3d = true;
        }
    }
    Check(polyline3d, "3D polyline projects to a 3-point polyline");
}

// The framework's CAD test document (the one VectorFormatsPluginTest
// round-trips); `--write-dxf <path>` exports it so the DWG fixture can be
// regenerated with LibreDWG's dxf2dwg.
std::shared_ptr<VectorDocument> BuildTestDocument() {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{400, 300};
    auto layer = doc->AddLayer("Artwork");

    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd{40, 40, 100, 60};
    rect->Style.Fill = Color(255, 0, 0, 255);
    StrokeData rectStroke;
    rectStroke.Fill = Color(0, 0, 255, 255);
    rectStroke.Width = 2.0f;
    rect->Style.Stroke = rectStroke;
    layer->AddChild(rect);

    auto dashLine = std::make_shared<VectorLine>();
    dashLine->Start = Point2Dd(40, 130);
    dashLine->End = Point2Dd(150, 130);
    StrokeData dashStroke;
    dashStroke.Fill = Color(0, 0, 0, 255);
    dashStroke.Width = 2.0f;
    dashStroke.DashArray = {6.0, 3.0};
    dashLine->Style.Stroke = dashStroke;
    layer->AddChild(dashLine);

    auto path = std::make_shared<VectorPath>();
    path->MoveTo(200, 140);
    path->CurveTo(240, 120, 280, 120, 300, 160);
    path->CurveTo(280, 200, 240, 200, 200, 160);
    path->ClosePath();
    path->Style.Fill = Color(90, 40, 160, 255);
    layer->AddChild(path);

    auto text = std::make_shared<VectorText>();
    text->Position = Point2Dd(40, 250);
    text->BaseStyle.FontFamily = "Liberation Sans";
    text->BaseStyle.FontSize = 18.0f;
    text->SetText("Hello Plugin");
    text->Style.Fill = Color(20, 20, 20, 255);
    layer->AddChild(text);
    return doc;
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::map<std::string, int> EntityHistogram(const std::string& dxf) {
    std::map<std::string, int> hist;
    std::istringstream in(dxf);
    std::string code, value, section;
    while (std::getline(in, code) && std::getline(in, value)) {
        if (std::atoi(code.c_str()) != 0) continue;
        if (value == "SECTION") {
            std::getline(in, code);
            std::getline(in, section);
        } else if (section == "ENTITIES" && value != "ENDSEC") {
            ++hist[value];
        }
    }
    return hist;
}

void TestFixture() {
    std::printf("== native DWG decoder (R2000 fixture)\n");
    std::string path = std::string(DWG_TEST_DATA_DIR) + "/cad-test-document.r2000.dwg";
    std::string data = ReadFile(path);
    if (data.empty()) {
        std::printf("  note: fixture %s not found; skipped\n", path.c_str());
        return;
    }
    Check(VectorConverter::DWGDecoderSupportsVersion(data), "fixture version is supported");
    std::vector<std::string> warnings;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [&](const std::string& w) { warnings.push_back(w); };

    std::string dxf = VectorConverter::DWGConverter::DecodeToDxf(data, options);
    Check(!dxf.empty(), "DecodeToDxf yields DXF text");
    auto hist = EntityHistogram(dxf);
    Check(hist["HATCH"] >= 2, "fills decode as HATCH entities");
    Check(hist["LWPOLYLINE"] + hist["SPLINE"] >= 2, "strokes decode as LWPOLYLINE/SPLINE");
    Check(hist["TEXT"] == 1, "the text entity decodes");

    VectorConverter::DWGConverter dwg;
    auto doc = dwg.ImportFromString(data, options);
    Check(doc != nullptr, "DWG imports natively (no external tool)");
    if (!doc) return;
    for (const auto& w : warnings) std::printf("      warning: %s\n", w.c_str());
    Check(Drawables(*doc).size() >= 4, "all elements survive");
    // R2000 has no true colour: the fixture carries the nearest ACI entries.
    Check(HasFillColor(*doc, Color(255, 0, 0, 255), 4), "red fill survives");
    Check(HasFillColor(*doc, Color(90, 40, 160, 255), 60), "purple bezier fill survives (nearest ACI)");
    Check(HasDash(*doc), "dash pattern survives");
    const VectorText* text = FindText(*doc);
    Check(text != nullptr, "text survives");
    if (text) {
        Check(TextOf(*text) == "Hello Plugin", "text content intact");
        // The page is normalised (no declared extents in a DWG), so the
        // 18-unit text is checked against the page width (~325 units).
        double ratio = text->BaseStyle.FontSize / doc->Size.width;
        Check(ratio > 0.045 && ratio < 0.07,
              "font size scales with the page (" + std::to_string(ratio) + ")");
    }
    // The page is the drawing's own extents: the test document spans
    // x 40..360 and y 40..259 (text baseline), roughly 4:3 landscape.
    Check(doc->Size.width > doc->Size.height, "page is landscape like the source");
}

void ReportFile(const std::string& path) {
    std::printf("== %s\n", path.c_str());
    std::string data = ReadFile(path);
    if (data.empty()) { std::printf("  cannot read\n"); return; }
    std::vector<std::string> warnings;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [&](const std::string& w) { warnings.push_back(w); };
    VectorConverter::DWGDecodeResult res = VectorConverter::DecodeDWG(data, options.WarningCallback);
    std::printf("  %s ok=%d entities=%u blocks=%u skipped=%u %s\n", res.version.c_str(), res.ok,
                res.entities, res.blocks, res.skipped, res.error.c_str());
    for (const auto& w : warnings) std::printf("  warning: %s\n", w.c_str());
    if (!res.ok) return;
    auto hist = EntityHistogram(res.dxf);
    std::printf(" ");
    for (const auto& [k, v] : hist) std::printf(" %s=%d", k.c_str(), v);
    std::printf("\n");
    auto doc = VectorConverter::DXFConverter().ImportFromString(res.dxf, options);
    if (!doc) { std::printf("  DXF import failed\n"); return; }
    std::printf("  document %.1f x %.1f, %zu layers, %zu drawables\n", doc->Size.width,
                doc->Size.height, doc->Layers.size(), Drawables(*doc).size());
    if (const char* dir = std::getenv("DWG_TEST_SVG_DIR")) {
        std::string name = path.substr(path.find_last_of('/') + 1);
        std::string out = std::string(dir) + "/" + name + ".svg";
        VectorConverter::SVGConverter svg;
        std::printf("  svg: %s (%s)\n", out.c_str(), svg.Export(*doc, out) ? "written" : "failed");
    }
}

}   // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-dxf") {
        auto doc = BuildTestDocument();
        bool ok = VectorConverter::DXFConverter().Export(*doc, argv[2]);
        std::printf("%s: %s\n", argv[2], ok ? "written" : "failed");
        return ok ? 0 : 1;
    }
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) ReportFile(argv[i]);
        return 0;
    }
    TestBlockMachinery();
    TestFixture();
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
