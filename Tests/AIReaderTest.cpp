// Tests/AIReaderTest.cpp
// Tests for the Adobe Illustrator import path (UltraCanvasAIReader.cpp).
//
// The bug this guards against: a .ai written without "Create PDF Compatible
// File" is a PDF whose page content stream draws nothing at all, with the
// whole drawing in the /AIPrivateData streams. Opening such a file with a
// PDF engine yields a blank page - which is what both samples under
// media/vector/AI did. So the first check is the one that would have caught
// it: those two files must import as real geometry, upright and on the page
// their header declares.
//
// A synthetic legacy (v8, EPS-based) .ai covers the rest of the art
// language - the colour operators, compound paths, clipping, dashes, groups
// and layer names - and the PostScript y-up coordinate space that a legacy
// file uses instead of Illustrator's y-down ruler space.
//
// Usage: AIReaderTest [file.ai ...]
// Extra .ai files are imported and reported; with AI_TEST_SVG_DIR set, each
// imported document is also written there as SVG for a look.
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasMetafileConverters.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorConverter.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
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

// Every drawable in the imported document, layers and groups flattened.
void CollectPaths(const VectorElement& e, std::vector<const VectorPath*>& out) {
    if (const auto* g = dynamic_cast<const VectorGroup*>(&e)) {
        for (const auto& child : g->Children) if (child) CollectPaths(*child, out);
        return;
    }
    if (const auto* p = dynamic_cast<const VectorPath*>(&e)) out.push_back(p);
}

std::vector<const VectorPath*> Paths(const VectorDocument& doc) {
    std::vector<const VectorPath*> out;
    for (const auto& layer : doc.Layers) if (layer) CollectPaths(*layer, out);
    return out;
}

Rect2Dd DrawingBounds(const VectorDocument& doc) {
    bool first = true;
    Rect2Dd box(0, 0, 0, 0);
    for (const VectorPath* p : Paths(doc)) {
        const Rect2Dd b = p->GetBoundingBox();
        if (first) { box = b; first = false; continue; }
        const double x0 = std::min(box.x, b.x);
        const double y0 = std::min(box.y, b.y);
        const double x1 = std::max(box.x + box.width, b.x + b.width);
        const double y1 = std::max(box.y + box.height, b.y + b.height);
        box = Rect2Dd(x0, y0, x1 - x0, y1 - y0);
    }
    return box;
}

const Color* FillColor(const VectorPath& p) {
    if (!p.Style.Fill) return nullptr;
    return std::get_if<Color>(&*p.Style.Fill);
}

const Color* StrokeColor(const VectorPath& p) {
    if (!p.Style.Stroke) return nullptr;
    return std::get_if<Color>(&p.Style.Stroke->Fill);
}

bool SameColor(const Color& a, const Color& b, int tolerance) {
    return std::abs(a.r - b.r) <= tolerance &&
           std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

size_t CommandCount(const VectorPath& p, PathCommandType type) {
    size_t n = 0;
    for (const auto& c : p.Path.commands) if (c.Type == type) ++n;
    return n;
}

std::shared_ptr<VectorDocument> ImportFile(const std::string& path,
                                           std::vector<std::string>* warnings = nullptr) {
    VectorConverter::AIConverter converter;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [warnings](const std::string& message) {
        if (warnings) warnings->push_back(message);
    };
    return converter.Import(path, options);
}

std::shared_ptr<VectorDocument> ImportString(const std::string& data,
                                             std::vector<std::string>* warnings = nullptr) {
    VectorConverter::AIConverter converter;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [warnings](const std::string& message) {
        if (warnings) warnings->push_back(message);
    };
    return converter.ImportFromString(data, options);
}

// ===== THE SHIPPED SAMPLES =====

// Both are CorelDRAW exports with an empty PDF page and all of their line art
// in the private data, which is exactly the case that used to come up blank.
void TestPrivateDataSamples() {
    struct Sample {
        const char* file;
        double width;
        double height;
        size_t minimumPaths;
    };
    const Sample samples[] = {
            {"turtle.ai", 612.0, 792.0, 800},
            {"mandalorian-star-wars.ai", 595.0, 842.0, 70},
    };

    for (const Sample& sample : samples) {
        const std::string path = std::string(AI_SAMPLES_DIR) + "/" + sample.file;
        std::vector<std::string> warnings;
        auto doc = ImportFile(path, &warnings);
        const std::string name(sample.file);
        Check(doc != nullptr, name + " imports");
        if (!doc) continue;

        const auto paths = Paths(*doc);
        Check(paths.size() >= sample.minimumPaths,
              name + " has " + std::to_string(paths.size()) + " paths (>= " +
              std::to_string(sample.minimumPaths) + ")");
        Check(std::abs(doc->Size.width - sample.width) < 1.0 &&
              std::abs(doc->Size.height - sample.height) < 1.0,
              name + " is " + std::to_string(static_cast<int>(doc->Size.width)) + " x " +
              std::to_string(static_cast<int>(doc->Size.height)) + " points");
        Check(!doc->Title.empty(), name + " carries its title");

        // Upright and on the page: Illustrator's ruler space runs y down from
        // the top of the artboard as negative numbers, so a reader that
        // forgets to flip it puts the whole drawing above the page.
        const Rect2Dd bounds = DrawingBounds(*doc);
        Check(bounds.y > -1.0 && bounds.y + bounds.height < doc->Size.height + 1.0,
              name + " sits inside the page vertically (y " +
              std::to_string(static_cast<int>(bounds.y)) + " to " +
              std::to_string(static_cast<int>(bounds.y + bounds.height)) + ")");
        Check(bounds.width > doc->Size.width * 0.5 && bounds.height > doc->Size.height * 0.5,
              name + " fills a reasonable part of the page");

        // Line art: every path is stroked, none is filled, and the curves
        // came through as curves rather than being flattened.
        size_t stroked = 0, filled = 0, curves = 0;
        for (const VectorPath* p : paths) {
            if (StrokeColor(*p)) ++stroked;
            if (FillColor(*p)) ++filled;
            curves += CommandCount(*p, PathCommandType::CurveTo);
        }
        Check(stroked == paths.size(), name + " strokes every path");
        Check(filled == 0, name + " fills nothing (it is line art)");
        Check(curves >= paths.size(), name + " keeps its beziers (" +
                                      std::to_string(curves) + " curve segments)");
    }
}

// ===== THE ART LANGUAGE =====

// A legacy (v8) .ai: the same operators, in the open, in PostScript's y-up
// space. Page is 200 x 100, so a y of 90 lands 10 points below the top.
const char* kLegacyArt =
        "%!PS-Adobe-3.0\n"
        "%%Creator: UltraCanvas test\n"
        "%%Title: (Legacy art)\n"
        "%%BoundingBox: 0 0 200 100\n"
        "%AI5_ArtSize: 200 100\n"
        "%%EndSetup\n"
        "1 1 1 1 0 0 -1 0 0 0 Lb\n"
        "(Artwork) Ln\n"
        "u\n"
        // A filled square, RGB red, no stroke.
        "1 0 0 Xa\n"
        "10 10 m\n"
        "50 10 L\n"
        "50 50 L\n"
        "10 50 L\n"
        "f\n"
        // A stroked, dashed line: 4-point round-capped bevel-joined stroke
        // in 50% grey.
        "0.5 G\n"
        "4 w\n"
        "1 J\n"
        "2 j\n"
        "[3 2 ]0 d\n"
        "60 10 m\n"
        "100 90 L\n"
        "S\n"
        // A compound path - outer box with a hole - filled from CMYK.
        "0 1 1 0 k\n"
        "*u\n"
        "110 10 m\n"
        "150 10 L\n"
        "150 50 L\n"
        "110 50 L\n"
        "f\n"
        "120 20 m\n"
        "140 20 L\n"
        "140 40 L\n"
        "120 40 L\n"
        "f\n"
        "*U\n"
        // A curve, given both as a full curveto and as the v and y shorthands.
        "0 0 1 XA\n"
        "1 w\n"
        "[]0 d\n"
        "10 60 m\n"
        "20 90 40 90 50 60 C\n"
        "60 30 80 30 v\n"
        "90 60 100 90 y\n"
        "S\n"
        "U\n"
        "LB\n"
        "%%Trailer\n"
        "%%EOF\n";

void TestArtLanguage() {
    std::vector<std::string> warnings;
    auto doc = ImportString(kLegacyArt, &warnings);
    Check(doc != nullptr, "legacy .ai imports");
    if (!doc) return;

    Check(std::abs(doc->Size.width - 200.0) < 0.5 &&
          std::abs(doc->Size.height - 100.0) < 0.5, "legacy page is 200 x 100");
    Check(doc->Layers.size() == 1 && doc->Layers[0]->Name == "Artwork",
          "Ln names the layer");

    const auto paths = Paths(*doc);
    Check(paths.size() == 4, "four objects (" + std::to_string(paths.size()) + ")");
    if (paths.size() != 4) return;

    // The filled square: red, closed by the lowercase paint operator, and
    // flipped into the document's y-down page - a y of 10 in PostScript's
    // y-up space is 90 from the top of a 100-point page.
    const VectorPath& square = *paths[0];
    const Color* squareFill = FillColor(square);
    Check(squareFill && SameColor(*squareFill, Color(255, 0, 0, 255), 1),
          "Xa sets an RGB fill");
    Check(!StrokeColor(square), "f fills without stroking");
    Check(CommandCount(square, PathCommandType::ClosePath) == 1,
          "the lowercase paint operator closes the path");
    const Rect2Dd squareBox = square.GetBoundingBox();
    Check(std::abs(squareBox.y - 50.0) < 0.5 && std::abs(squareBox.height - 40.0) < 0.5,
          "y-up art is flipped onto the y-down page");

    // The dashed line.
    const VectorPath& line = *paths[1];
    const Color* lineStroke = StrokeColor(line);
    Check(lineStroke && SameColor(*lineStroke, Color(128, 128, 128, 255), 2),
          "G sets a grey stroke");
    Check(!FillColor(line), "S strokes without filling");
    Check(line.Style.Stroke && std::abs(line.Style.Stroke->Width - 4.0f) < 0.01f,
          "w sets the stroke width");
    Check(line.Style.Stroke && line.Style.Stroke->LineCap == StrokeLineCap::Round,
          "J sets the line cap");
    Check(line.Style.Stroke && line.Style.Stroke->LineJoin == StrokeLineJoin::Bevel,
          "j sets the line join");
    Check(line.Style.Stroke && line.Style.Stroke->DashArray.size() == 2,
          "d sets the dash pattern");

    // The compound path: both subpaths in one object, so the hole is a hole.
    const VectorPath& compound = *paths[2];
    Check(CommandCount(compound, PathCommandType::MoveTo) == 2,
          "*u..*U keeps both subpaths in one object");
    const Color* compoundFill = FillColor(compound);
    Check(compoundFill && SameColor(*compoundFill, Color(255, 0, 0, 255), 2),
          "k sets a CMYK fill");

    // The curves, including the v and y shorthands.
    const VectorPath& curve = *paths[3];
    Check(CommandCount(curve, PathCommandType::CurveTo) == 3,
          "C, v and y all produce cubic segments");
    const Color* curveStroke = StrokeColor(curve);
    Check(curveStroke && SameColor(*curveStroke, Color(0, 0, 255, 255), 1),
          "XA sets an RGB stroke");
}

// A clipping path takes the rest of its group with it, and is not itself
// drawn.
void TestClipping() {
    const std::string art =
            "%!PS-Adobe-3.0\n"
            "%AI5_ArtSize: 100 100\n"
            "%%EndSetup\n"
            "u\n"
            "0 0 m\n"
            "50 0 L\n"
            "50 50 L\n"
            "W\n"
            "n\n"
            "1 0 0 Xa\n"
            "10 10 m\n"
            "90 10 L\n"
            "90 90 L\n"
            "f\n"
            "U\n";
    auto doc = ImportString(art);
    Check(doc != nullptr, "clipped art imports");
    if (!doc) return;
    const auto paths = Paths(*doc);
    Check(paths.size() == 1, "the clipping path is not drawn (" +
                             std::to_string(paths.size()) + " drawn)");
    Check(doc->GetDefinition("aiClip1") != nullptr, "the clip becomes a definition");
    // The clip opens a group of its own inside the enclosing one, so the
    // objects after it are the group's children.
    std::function<bool(const VectorElement&)> carriesClip =
            [&carriesClip](const VectorElement& e) -> bool {
        if (e.Style.ClipPath && *e.Style.ClipPath == "aiClip1") return true;
        if (const auto* g = dynamic_cast<const VectorGroup*>(&e)) {
            for (const auto& child : g->Children) {
                if (child && carriesClip(*child)) return true;
            }
        }
        return false;
    };
    bool clipped = false;
    for (const auto& layer : doc->Layers) if (layer && carriesClip(*layer)) clipped = true;
    Check(clipped, "W clips the rest of the group");

    // W does not stop the path being painted: "W f" both clips and fills.
    const std::string painted =
            "%!PS-Adobe-3.0\n"
            "%AI5_ArtSize: 100 100\n"
            "%%EndSetup\n"
            "u\n"
            "0 1 0 Xa\n"
            "0 0 m\n"
            "50 0 L\n"
            "50 50 L\n"
            "W\n"
            "f\n"
            "1 0 0 Xa\n"
            "10 10 m\n"
            "90 10 L\n"
            "90 90 L\n"
            "f\n"
            "U\n";
    auto both = ImportString(painted);
    Check(both != nullptr, "a painted clipping path imports");
    if (!both) return;
    Check(Paths(*both).size() == 2, "W f is drawn as well as clipped (" +
                                    std::to_string(Paths(*both).size()) + " drawn)");
}

// A .ai whose artwork really is in its PDF page carries no private data.
// The reader declines it rather than returning an empty document, which is
// what lets a caller fall back to the PDF engine.
void TestPdfCompatibleFileIsDeclined() {
    const std::string pdf =
            "%PDF-1.5\n"
            "1 0 obj\n<<\n/Type /Catalog\n>>\nendobj\n"
            "trailer\n<<\n/Root 1 0 R\n>>\n%%EOF\n";
    std::vector<std::string> warnings;
    auto doc = ImportString(pdf, &warnings);
    Check(doc == nullptr, "a PDF-compatible .ai is declined, not imported empty");
    bool explained = false;
    for (const std::string& w : warnings) {
        if (w.find("PDF") != std::string::npos) explained = true;
    }
    Check(explained, "and the warning says why");

    std::vector<std::string> junkWarnings;
    Check(ImportString("not an illustrator file", &junkWarnings) == nullptr,
          "a file that is neither PDF nor PostScript is declined");
    Check(!junkWarnings.empty(), "and warns");
}

void TestValidation() {
    VectorConverter::AIConverter converter;
    Check(converter.CanImport(), "AIConverter advertises import");
    Check(converter.CanExport(), "AIConverter still advertises export");
    Check(converter.ValidateData("%PDF-1.5"), "a PDF container validates");
    Check(converter.ValidateData("%!PS-Adobe-3.0"), "a legacy file validates");
    Check(!converter.ValidateData("RIFF"), "something else does not");
}

// ===== EXTRA FILES FROM THE COMMAND LINE =====

void WriteSvg(const VectorDocument& doc, const std::string& path) {
    std::ofstream out(path);
    if (!out) return;
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << doc.Size.width
        << "\" height=\"" << doc.Size.height << "\" viewBox=\"0 0 " << doc.Size.width
        << " " << doc.Size.height << "\">\n";
    for (const VectorPath* p : Paths(doc)) {
        out << "<path d=\"";
        for (const auto& c : p->Path.commands) {
            switch (c.Type) {
                case PathCommandType::MoveTo:
                    out << "M " << c.Parameters[0] << " " << c.Parameters[1] << " "; break;
                case PathCommandType::LineTo:
                    out << "L " << c.Parameters[0] << " " << c.Parameters[1] << " "; break;
                case PathCommandType::CurveTo:
                    out << "C";
                    for (float v : c.Parameters) out << " " << v;
                    out << " ";
                    break;
                case PathCommandType::ClosePath: out << "Z "; break;
                default: break;
            }
        }
        out << "\" fill=\"";
        if (const Color* f = FillColor(*p)) {
            out << "rgb(" << int(f->r) << "," << int(f->g) << "," << int(f->b) << ")";
        } else {
            out << "none";
        }
        out << "\" stroke=\"";
        if (const Color* s = StrokeColor(*p)) {
            out << "rgb(" << int(s->r) << "," << int(s->g) << "," << int(s->b) << ")\" "
                << "stroke-width=\"" << p->Style.Stroke->Width << "\"";
        } else {
            out << "none\"";
        }
        out << "/>\n";
    }
    out << "</svg>\n";
}

void ReportFile(const std::string& path) {
    std::vector<std::string> warnings;
    auto doc = ImportFile(path, &warnings);
    std::printf("\n%s\n", path.c_str());
    if (!doc) {
        std::printf("  not imported\n");
        for (const std::string& w : warnings) std::printf("  warning: %s\n", w.c_str());
        return;
    }
    const Rect2Dd bounds = DrawingBounds(*doc);
    std::printf("  page %.1f x %.1f, %zu layers, %zu paths, drawing %.1f %.1f %.1f %.1f\n",
                doc->Size.width, doc->Size.height, doc->Layers.size(), Paths(*doc).size(),
                bounds.x, bounds.y, bounds.width, bounds.height);
    for (const std::string& w : warnings) std::printf("  warning: %s\n", w.c_str());

    if (const char* dir = std::getenv("AI_TEST_SVG_DIR")) {
        const size_t slash = path.find_last_of("/\\");
        const std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
        const std::string out = std::string(dir) + "/" + name + ".svg";
        WriteSvg(*doc, out);
        std::printf("  wrote %s\n", out.c_str());
    }
}

}   // namespace

int main(int argc, char** argv) {
    std::printf("=== AI reader ===\n");
    TestPrivateDataSamples();
    TestArtLanguage();
    TestClipping();
    TestPdfCompatibleFileIsDeclined();
    TestValidation();

    for (int i = 1; i < argc; ++i) ReportFile(argv[i]);

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASSED" : "FAILED",
                failures, failures == 1 ? "" : "s");
    return failures;
}
