// Tests/ModelWriterTest.cpp
// The 3DS, COLLADA and X3D writers, and the claim that says they exist.
//
// A writer is easy to get wrong in a way that looks right: the file is
// produced, it has a plausible size, and nothing complains until someone opens
// it. So the shape of this suite is a round trip - write the document, read the
// file back with this framework's own reader, and compare - plus the one check
// a round trip cannot make, which is that the dispatch's list of writable
// extensions agrees with the converters themselves.
//
// Counts alone are not enough. A writer that emits every triangle at the origin
// preserves the face count exactly, so the geometry is compared through the
// bounding box as well. Where a format forces its own up axis - 3DS is always
// Z-up, X3D always Y-up - the comparison applies the same conversion, so the
// deliberate rotation passes and a wrong one still fails.
//
// argv[1] is media/3D. Without it only the contract cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework

#include "Models/UltraCanvasModelFormatsPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelConverter;
using namespace UltraCanvas::ModelStorage;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

namespace {

std::filesystem::path TempDir() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "uc-model-writer";
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool Claims(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// The document's bounds, expressed in the up axis `target` - so a format that
// forces its own axis can be compared against one that does not.
Bounds3D BoundsIn(const ModelDocument& document, UpAxis target) {
    const Bounds3D source = document.ComputeBounds();
    if (document.Up == target || !source.IsValid()) return source;
    auto convert = [target](const Vec3d& p) {
        return target == UpAxis::ZUp ? Vec3d(p.x, -p.z, p.y) : Vec3d(p.x, p.z, -p.y);
    };
    Bounds3D rotated;
    for (int corner = 0; corner < 8; ++corner)
        rotated.Expand(convert(Vec3d((corner & 1) ? source.Max.x : source.Min.x,
                                     (corner & 2) ? source.Max.y : source.Min.y,
                                     (corner & 4) ? source.Max.z : source.Min.z)));
    return rotated;
}

// Primitives across the whole document. This is the structural invariant a
// round trip can actually hold: the *mesh* count is not, because X3D has no
// mesh grouping at all - geometry is a node, so a mesh of two primitives comes
// back as two meshes of one - while 3DS merges primitives into one object and
// splits them again by material group. Both keep the primitives; only one keeps
// the boxes they were packed in.
size_t TotalPrimitiveCount(const ModelDocument& document) {
    size_t total = 0;
    for (const ModelMesh& mesh : document.Meshes) total += mesh.Primitives.size();
    return total;
}

// Write `document` as `extension`, read it back, and compare. Returns false
// when anything along the way failed, having already reported why.
//
// The comparison is against a *triangulated* copy of the input, because the
// read-back triangulates: a writer that keeps a quad as a quad and one that
// splits it are both correct, and counting raw faces would call the first a
// success and the second a failure for no reason a caller would care about.
bool RoundTrip(const ModelDocument& source, const std::string& extension,
               const std::string& label) {
    ModelDocument document = source;
    document.TriangulateAll();
    const std::filesystem::path path = TempDir() / ("roundtrip." + extension);

    ConversionOptions write;
    if (!UltraCanvasModelFormatsPlugin::SaveModelDocument(document, path.string(), write)) {
        Check(false, label + ": the writer refused the document");
        return false;
    }

    std::error_code ec;
    const std::uintmax_t bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes == 0) {
        Check(false, label + ": the writer produced no file");
        return false;
    }

    ConversionOptions read;
    read.TriangulateOnImport = true;
    auto back = UltraCanvasModelFormatsPlugin::LoadModelDocument(path.string(), read);
    if (!back) {
        Check(false, label + ": this framework cannot read back what it just wrote");
        return false;
    }

    Check(back->TotalFaceCount() == document.TotalFaceCount(),
          label + ": every triangle survives (" + std::to_string(document.TotalFaceCount()) +
                  " -> " + std::to_string(back->TotalFaceCount()) + ")");
    Check(TotalPrimitiveCount(*back) == TotalPrimitiveCount(document),
          label + ": every primitive survives (" +
                  std::to_string(TotalPrimitiveCount(document)) + " -> " +
                  std::to_string(TotalPrimitiveCount(*back)) + ")");

    // The check counts cannot make: that the geometry is still where it was.
    const Bounds3D before = BoundsIn(document, back->Up);
    const Bounds3D after = back->ComputeBounds();
    bool placed = before.IsValid() && after.IsValid();
    if (placed) {
        const Vec3d sizeBefore = before.Size(), sizeAfter = after.Size();
        const Vec3d midBefore = before.Center(), midAfter = after.Center();
        const double scale = std::max({sizeBefore.x, sizeBefore.y, sizeBefore.z, 1e-9});
        auto near = [&](double a, double b) { return std::abs(a - b) <= 1e-3 * scale; };
        placed = near(sizeBefore.x, sizeAfter.x) && near(sizeBefore.y, sizeAfter.y) &&
                 near(sizeBefore.z, sizeAfter.z) && near(midBefore.x, midAfter.x) &&
                 near(midBefore.y, midAfter.y) && near(midBefore.z, midAfter.z);
    }
    Check(placed, label + ": the geometry keeps its size and position");

    std::filesystem::remove(path, ec);
    return true;
}

} // namespace

// ===== THE CLAIM =====

static void TestSaveListMatchesTheConverters() {
    std::printf("The writable-extension list and the converters agree\n");

    const std::vector<std::string> save = UltraCanvasModelFormatsPlugin::SupportedSaveExtensions();

    // Forwards: nothing is advertised that cannot actually be written. This is
    // the direction that lies to a caller - a file dialog offering a format
    // whose converter refuses it.
    std::string advertisedButUnwritable;
    for (const std::string& extension : save) {
        auto converter = UltraCanvasModelFormatsPlugin::CreateConverterForExtension(extension);
        if (!converter || !converter->CanExport())
            advertisedButUnwritable += (advertisedButUnwritable.empty() ? "" : ", ") + extension;
    }
    Check(advertisedButUnwritable.empty(),
          "every advertised extension has a converter that exports" +
                  (advertisedButUnwritable.empty() ? std::string()
                                                   : " (claimed: " + advertisedButUnwritable + ")"));

    // Backwards: nothing readable is quietly writable without being listed.
    // This is the direction that loses a feature rather than inventing one.
    std::string writableButUnlisted;
    for (const std::string& extension : UltraCanvasModelFormatsPlugin::SupportedLoadExtensions()) {
        auto converter = UltraCanvasModelFormatsPlugin::CreateConverterForExtension(extension);
        if (converter && converter->CanExport() && !Claims(save, extension))
            writableButUnlisted += (writableButUnlisted.empty() ? "" : ", ") + extension;
    }
    Check(writableButUnlisted.empty(),
          "every exporting converter is advertised" +
                  (writableButUnlisted.empty() ? std::string()
                                               : " (missing: " + writableButUnlisted + ")"));

    Check(Claims(save, "3ds"), ".3ds is writable");
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
    Check(Claims(save, "dae"), ".dae is writable");
#endif
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
    Check(Claims(save, "x3d") && Claims(save, "wrl"),
          "both X3D encodings are writable - .x3d as XML, .wrl as Classic VRML");
#endif
}

// A document built here rather than read, so the contract half of this suite
// runs with no sample files at all: two materials, an n-gon, a normal and a
// texture coordinate per vertex, under a node with a transform.
static ModelDocument SyntheticDocument() {
    MeshPrimitive quad;
    quad.Mode = PrimitiveMode::Polygons;
    quad.Positions = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    quad.Normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    quad.Indices = {0, 1, 2, 3};
    quad.FaceStarts = {0, 4};
    quad.Material = 0;

    VertexAttribute uv;
    uv.Semantic = AttributeSemantic::TexCoord;
    uv.Components = 2;
    uv.Values = {0, 0, 1, 0, 1, 1, 0, 1};
    quad.Attributes.push_back(uv);

    MeshPrimitive triangle;
    triangle.Mode = PrimitiveMode::Triangles;
    triangle.Positions = {{2, 0, 0}, {4, 0, 0}, {3, 2, 0}};
    triangle.Normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    triangle.Indices = {0, 1, 2};
    triangle.Material = 1;

    ModelMesh mesh;
    mesh.Name = "plate";
    mesh.Primitives = {quad, triangle};

    ModelDocument document;
    document.Title = "synthetic";
    document.Up = UpAxis::YUp;

    ModelMaterial red;
    red.Name = "red";
    red.BaseColorFactor = {1.0f, 0.0f, 0.0f, 1.0f};
    ModelMaterial blue;
    blue.Name = "blue";
    blue.BaseColorFactor = {0.0f, 0.0f, 1.0f, 1.0f};
    document.AddMaterial(red);
    document.AddMaterial(blue);

    ModelNode node;
    node.Name = "plate node";
    node.Translation = {0.0, 3.0, 0.0};
    node.Mesh = document.AddMesh(std::move(mesh));
    document.AddNode(std::move(node));
    return document;
}

static void TestSyntheticRoundTrips() {
    std::printf("A hand-built document survives each writer\n");
    const ModelDocument document = SyntheticDocument();

    // A quad and a triangle, under a node with a translation, with two
    // materials - so this pins the n-gon path, the transform and the
    // per-primitive material split without needing a sample file.
    RoundTrip(document, "3ds", "3DS");
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
    RoundTrip(document, "dae", "COLLADA");
#endif
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
    RoundTrip(document, "x3d", "X3D (XML)");
    RoundTrip(document, "wrl", "X3D (Classic VRML)");
#endif
}

static void TestEmptyDocumentIsRefusedNotWritten() {
    std::printf("An empty document is refused rather than written as a stub\n");
    const ModelDocument empty;
    ConversionOptions options;
    const std::filesystem::path path = TempDir() / "empty.3ds";
    // 3DS has no way to express a file with no objects that any reader will
    // accept, so producing one would be worse than saying no.
    Check(!UltraCanvasModelFormatsPlugin::SaveModelDocument(empty, path.string(), options),
          "3DS refuses a document with no geometry");
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void TestWarningsAreRaisedForWhatIsDropped() {
    std::printf("What a format cannot carry is reported, not silently dropped\n");
    ModelDocument document = SyntheticDocument();
    ModelAnimation animation;
    animation.Name = "spin";
    document.Animations.push_back(animation);

    auto wroteWithWarning = [&](const std::string& extension, const std::string& needle) {
        std::vector<std::string> warnings;
        ConversionOptions options;
        options.WarningCallback = [&](const std::string& message) { warnings.push_back(message); };
        const std::filesystem::path path = TempDir() / ("warn." + extension);
        UltraCanvasModelFormatsPlugin::SaveModelDocument(document, path.string(), options);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        for (const std::string& warning : warnings)
            if (warning.find(needle) != std::string::npos) return true;
        return false;
    };

    Check(wroteWithWarning("3ds", "animation"), "3DS says the animation was dropped");
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
    Check(wroteWithWarning("dae", "animation"), "COLLADA says the animation was dropped");
#endif
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
    Check(wroteWithWarning("x3d", "animation"), "X3D says the animation was dropped");
#endif

    // The up-axis conversions are a change to the numbers, so they are the
    // other thing a caller must be told about.
    Check(wroteWithWarning("3ds", "Z-up"),
          "3DS says a Y-up document was rotated into the format's Z-up");
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
    ModelDocument zUp = SyntheticDocument();
    zUp.Up = UpAxis::ZUp;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&](const std::string& message) { warnings.push_back(message); };
    const std::filesystem::path path = TempDir() / "zup.x3d";
    UltraCanvasModelFormatsPlugin::SaveModelDocument(zUp, path.string(), options);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    bool said = false;
    for (const std::string& warning : warnings)
        if (warning.find("Y-up") != std::string::npos) said = true;
    Check(said, "X3D says a Z-up document was rotated into the format's Y-up");
#endif
}

// ===== THE SAMPLES =====

static void TestSamples(const std::string& mediaRoot) {
    std::printf("Real files survive a round trip through each writer\n");

    struct Case {
        const char* source;      // relative to media/3D
        const char* extension;   // what to write
        const char* label;
    };
    const std::vector<Case> cases = {
            {"3DS/E-45-Aircraft.3ds",   "3ds", "3DS -> 3DS"},
            {"MS3D/E-45-Aircraft.ms3d", "3ds", "MilkShape -> 3DS (Y-up rotated)"},
            {"OBJ/E-45-Aircraft.obj",   "3ds", "OBJ -> 3DS"},
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
            {"COLLADA/E-45-Aircraft.dae", "dae", "COLLADA -> COLLADA"},
            {"3DS/E-45-Aircraft.3ds",     "dae", "3DS -> COLLADA"},
            {"STEP/Pin.step",             "dae", "STEP -> COLLADA (tessellated)"},
#endif
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
            {"X3D/E-45-Aircraft.x3d",     "x3d", "X3D -> X3D"},
            {"MS3D/E-45-Aircraft.ms3d",   "x3d", "MilkShape -> X3D"},
            {"MS3D/E-45-Aircraft.ms3d",   "wrl", "MilkShape -> VRML"},
            {"3DS/E-45-Aircraft.3ds",     "wrl", "3DS -> VRML (Z-up rotated)"},
#endif
    };

    for (const Case& item : cases) {
        const std::filesystem::path source = std::filesystem::path(mediaRoot) / item.source;
        if (!std::filesystem::exists(source)) {
            std::printf("  [SKIP] %s (sample not present)\n", item.label);
            continue;
        }
        ConversionOptions read;
        read.TriangulateOnImport = true;
        read.TessellateOnImport = true;
        auto document = UltraCanvasModelFormatsPlugin::LoadModelDocument(source.string(), read);
        if (!document || document->Empty()) {
            Check(false, std::string(item.label) + ": the source could not be read");
            continue;
        }
        RoundTrip(*document, item.extension, item.label);
    }
}

int main(int argc, char** argv) {
    TestSaveListMatchesTheConverters();
    TestSyntheticRoundTrips();
    TestEmptyDocumentIsRefusedNotWritten();
    TestWarningsAreRaisedForWhatIsDropped();
    if (argc > 1) TestSamples(argv[1]);
    else std::printf("Samples: skipped (pass the media/3D path to run them)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
