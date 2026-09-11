// Tests/ModelOBJTest.cpp
// The OBJ/MTL reader and writer against the real E-45 aircraft in
// media/models/OBJ, plus two checks nothing else in the suite can make:
//
//   * a round trip — OBJ -> ModelDocument -> OBJ -> ModelDocument — which is
//     only possible because OBJ is the first format with both a reader and a
//     writer, and which proves the structure holds a mesh without drift;
//   * a cross-format check against the same aircraft exported as 3DS. The OBJ
//     is Y-up and the 3DS Z-up, so ConvertUpAxis on one must land exactly on
//     the other's bounds. Two independent readers agreeing on the same model
//     is worth more than either agreeing with itself.
//
// argv[1] is the .obj, argv[2] the optional .3ds. Without them only the
// synthetic and malformed cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/OBJ/UltraCanvasOBJConverter.h"
#include "Models/3DS/UltraCanvas3DSConverter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelStorage;
using namespace UltraCanvas::ModelConverter;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

static std::vector<uint8_t> Bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

static void TestValidation() {
    std::printf("Validation\n");
    OBJConverter conv;
    Check(!conv.ValidateData({}), "empty data is rejected");
    Check(!conv.ValidateData(Bytes("<html><body>not a model</body></html>\n")),
          "an HTML file is rejected");
    Check(!conv.ValidateData(Bytes("solid teapot\nfacet normal 0 0 1\n")),
          "an ASCII STL is rejected");
    Check(conv.ValidateData(Bytes("# comment\n\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")),
          "a minimal OBJ is accepted");
    Check(conv.ValidateData(Bytes("mtllib a.mtl\no thing\nv 0 0 0\n")),
          "structure keywords before geometry still validate");
}

// The parsing corners a real exporter will eventually produce.
static void TestParsingCorners() {
    std::printf("Parsing corners\n");
    OBJConverter conv;
    ConversionOptions quiet;

    // Negative (relative) indices, the "v//vn" form, and a quad.
    auto relative = conv.ImportFromMemory(Bytes(
            "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
            "vn 0 0 1\n"
            "f -4//-1 -3//-1 -2//-1 -1//-1\n"), quiet);
    Check(relative != nullptr, "negative indices and the v//vn form parse");
    if (relative) {
        const MeshPrimitive& prim = relative->Meshes[0].Primitives[0];
        Check(prim.Mode == PrimitiveMode::Polygons && prim.FaceCount() == 1 &&
              prim.Face(0).size() == 4, "a quad stays one 4-corner face");
        Check(prim.Normals.size() == prim.VertexCount(), "the shared normal reaches every vertex");
    }

    // Separate index streams: 3 positions but 4 texture coordinates, with two
    // corners sharing a position and differing in UV. That must split into two
    // document vertices, not collapse to one.
    auto split = conv.ImportFromMemory(Bytes(
            "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
            "vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\n"
            "f 1/1 2/2 3/3\n"
            "f 1/4 2/2 3/3\n"), quiet);
    Check(split != nullptr, "a file with more texcoords than positions parses");
    if (split) {
        const MeshPrimitive& prim = split->Meshes[0].Primitives[0];
        Check(prim.VertexCount() == 4,
              "a position used with two UVs becomes two vertices, not one");
        Check(prim.FaceCount() == 2, "both faces survive the split");
    }

    // A face referencing a vertex that does not exist must be reported and
    // dropped, not read out of bounds.
    std::vector<std::string> warnings;
    ConversionOptions loud;
    loud.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
    auto bad = conv.ImportFromMemory(Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\nf 1 2 3\n"), loud);
    Check(bad != nullptr && bad->TotalFaceCount() == 1, "an out-of-range index drops its face only");
    bool warnedIndex = false;
    for (const auto& w : warnings)
        if (w.find("does not define") != std::string::npos) warnedIndex = true;
    Check(warnedIndex, "the bad index is reported");

    // Triangles only must not be dressed up as polygons.
    auto triangles = conv.ImportFromMemory(Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"), quiet);
    Check(triangles && triangles->Meshes[0].Primitives[0].Mode == PrimitiveMode::Triangles,
          "an all-triangle file uses Triangles, not Polygons");
}

// Six significant digits is the C++ stream default and what OBJ files in the
// wild contain. It is also catastrophic for a coordinate far from the origin,
// which is what ConversionOptions::Precision exists to let a caller avoid.
static void TestPrecision() {
    std::printf("Write precision\n");
    OBJConverter conv;

    // A survey-scale coordinate with detail in the low bits.
    MeshPrimitive prim;
    prim.Positions = { Vec3d(1234567.8912345678, -987654.3210987654, 42.123456789012345),
                       Vec3d(1234568.8912345678, -987654.3210987654, 42.123456789012345),
                       Vec3d(1234567.8912345678, -987653.3210987654, 42.123456789012345) };
    prim.Indices = {0, 1, 2};
    ModelMesh mesh;
    mesh.Name = "survey";
    mesh.Primitives.push_back(prim);
    const ModelDocument source = ModelDocument::FromSingleMesh(std::move(mesh), "survey");

    auto roundTrip = [&conv, &source](NumericPrecision precision, Vec3d& out, size_t& bytes) {
        ConversionOptions write;
        write.Precision = precision;
        const std::filesystem::path path =
                std::filesystem::temp_directory_path() / "ultracanvas-precision.obj";
        if (!conv.Export(source, path.string(), write)) return false;
        bytes = static_cast<size_t>(std::filesystem::file_size(path));
        ConversionOptions read;
        auto back = conv.Import(path.string(), read);
        std::filesystem::remove(path);
        if (!back || back->Meshes.empty() || back->Meshes[0].Primitives.empty()) return false;
        out = back->Meshes[0].Primitives[0].Positions[0];
        return true;
    };

    Vec3d compact, full;
    size_t compactBytes = 0, fullBytes = 0;
    Check(roundTrip(NumericPrecision::Compact, compact, compactBytes),
          "the document writes and reads back at Compact precision");
    Check(roundTrip(NumericPrecision::Full, full, fullBytes),
          "the document writes and reads back at Full precision");

    const Vec3d& original = source.Meshes[0].Primitives[0].Positions[0];
    Check(std::memcmp(&full, &original, sizeof(Vec3d)) == 0,
          "Full precision returns the position bit-for-bit");
    Check(std::fabs(compact.x - original.x) > 1.0,
          "Compact precision loses whole units on a far-from-origin coordinate");
    Check(fullBytes > compactBytes, "Full precision costs file size, as documented");
    std::printf("  (Compact lost %.2f units; %zu bytes vs %zu)\n",
                std::fabs(compact.x - original.x), compactBytes, fullBytes);

    // Compact stays the default, so nothing changes for a caller who has not
    // asked: an OBJ is a deliverable far more often than an intermediate.
    const ConversionOptions defaults;
    Check(defaults.Precision == NumericPrecision::Compact,
          "Compact is the default, so existing output is unchanged");
}

static void TestCapabilities() {
    std::printf("Capabilities\n");
    OBJConverter conv;
    const FormatCapabilities caps = conv.GetCapabilities();
    Check(conv.CanImport() && conv.CanExport(), "OBJ reads and writes");
    Check(caps.Meshes && caps.NGons && caps.Materials && caps.TextureCoordinates,
          "claims meshes, n-gons, materials and UVs");
    Check(!caps.SceneGraph && !caps.Instancing && !caps.Animations && !caps.Skinning &&
          !caps.Cameras && !caps.Lights && !caps.Units,
          "claims none of what OBJ cannot store");
}

static void TestSample(const char* objPath, const char* dsPath) {
    std::printf("Sample: %s\n", objPath);
    OBJConverter conv;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    Check(conv.ValidateFile(objPath), "the file validates as OBJ");
    auto doc = conv.Import(objPath, options);
    if (!doc) { std::printf("  [FAIL] import returned nothing\n"); ++failures; return; }

    // From the path rather than hard-coded: the rule under test is "the title
    // is the file's stem", and a constant only tests that the sample still
    // lives where it did.
    Check(doc->Title == std::filesystem::path(objPath).stem().string(),
          "the document is titled from the file name");
    Check(doc->Up == UpAxis::YUp, "OBJ is read as Y-up, the format's de-facto convention");
    Check(doc->Meshes.size() == 2 && doc->Nodes.size() == 2, "both objects arrive as mesh nodes");
    Check(doc->TotalFaceCount() == 8110, "all 8110 faces are read");
    Check(doc->TotalVertexCount() == 12227,
          "the 11749 positions and 12227 texcoords resolve to 12227 unique corners");

    // Every face in this file is a quad; nothing may silently triangulate it.
    bool allPolygons = true, allQuads = true;
    for (const auto& mesh : doc->Meshes) {
        for (const auto& prim : mesh.Primitives) {
            if (prim.Mode != PrimitiveMode::Polygons) allPolygons = false;
            for (size_t f = 0; f < prim.FaceCount(); ++f)
                if (prim.Face(f).size() != 4) allQuads = false;
        }
    }
    Check(allPolygons, "the mesh is kept in Polygons mode");
    Check(allQuads, "every one of the 8110 faces is still a quad");

    // The .mtl is not beside the sample, which is the common case for a model
    // pulled out of an archive. It must degrade to named placeholders.
    Check(doc->Materials.size() == 2, "both usemtl names become materials");
    bool warnedMissingLibrary = false;
    for (const auto& w : warnings)
        if (w.find("material library") != std::string::npos) warnedMissingLibrary = true;
    Check(warnedMissingLibrary, "the missing material library is reported");

    // Triangulation on demand: 8110 quads make 16220 triangles.
    ModelDocument triangulated = *doc;
    triangulated.TriangulateAll();
    Check(triangulated.TotalFaceCount() == 16220, "triangulating the quads gives 16220 triangles");

    // --- round trip ---
    const std::filesystem::path out =
            std::filesystem::temp_directory_path() / "ultracanvas-obj-roundtrip.obj";
    std::vector<std::string> exportWarnings;
    ConversionOptions writeOptions;
    writeOptions.WarningCallback = [&exportWarnings](const std::string& w) {
        exportWarnings.push_back(w);
    };
    Check(conv.Export(*doc, out.string(), writeOptions), "the document writes back to OBJ");

    ConversionOptions readBack;
    auto again = conv.Import(out.string(), readBack);
    Check(again != nullptr, "the written OBJ reads back");
    if (again) {
        Check(again->TotalFaceCount() == doc->TotalFaceCount(),
              "the round trip keeps every face");
        Check(again->TotalVertexCount() == doc->TotalVertexCount(),
              "the round trip keeps every vertex");
        const Bounds3D before = doc->ComputeBounds();
        const Bounds3D after = again->ComputeBounds();
        Check(Near(before.Min.x, after.Min.x, 1e-4) && Near(before.Max.y, after.Max.y, 1e-4) &&
              Near(before.Min.z, after.Min.z, 1e-4), "the round trip keeps the bounds");
        bool quadsSurvived = true;
        for (const auto& mesh : again->Meshes)
            for (const auto& prim : mesh.Primitives)
                for (size_t f = 0; f < prim.FaceCount(); ++f)
                    if (prim.Face(f).size() != 4) quadsSurvived = false;
        Check(quadsSurvived, "the round trip keeps the quads as quads");
        Check(again->Meshes[0].Name == doc->Meshes[0].Name, "object names survive the round trip");
    }
    std::filesystem::remove(out);
    std::filesystem::remove(out.parent_path() / "ultracanvas-obj-roundtrip.mtl");

    // --- cross-format ---
    if (!dsPath) return;
    ThreeDSConverter threeDS;
    ConversionOptions quiet;
    auto fromDS = threeDS.Import(dsPath, quiet);
    if (!fromDS) { std::printf("  [FAIL] the 3DS companion did not load\n"); ++failures; return; }

    ModelDocument rotated = *doc;
    rotated.ConvertUpAxis(UpAxis::ZUp);
    const Bounds3D objBounds = rotated.ComputeBounds();
    const Bounds3D dsBounds = fromDS->ComputeBounds();
    Check(Near(objBounds.Min.x, dsBounds.Min.x, 1e-3) && Near(objBounds.Max.x, dsBounds.Max.x, 1e-3) &&
          Near(objBounds.Min.y, dsBounds.Min.y, 1e-3) && Near(objBounds.Max.y, dsBounds.Max.y, 1e-3) &&
          Near(objBounds.Min.z, dsBounds.Min.z, 1e-3) && Near(objBounds.Max.z, dsBounds.Max.z, 1e-3),
          "the Y-up OBJ rotated to Z-up lands exactly on the 3DS export of the same model");
    Check(rotated.TotalVertexCount() == fromDS->TotalVertexCount(),
          "both readers find the same vertex count");
    Check(rotated.TotalFaceCount() * 2 == fromDS->TotalFaceCount(),
          "the OBJ's quads are the 3DS's triangle pairs");
}

// `s` statements used to be resolved into normals and dropped, so an OBJ read
// and written back lost every crease it had asked for. The document carries the
// masks now, and this is the round trip that proves it.
static void TestSmoothingGroups() {
    std::printf("Smoothing groups\n");

    const std::string source =
            "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 2 0 0\nv 2 1 0\nv 3 0 0\nv 3 1 0\n"
            "s 1\n"
            "f 1 2 3 4\n"
            "s 2\n"
            "f 2 5 6 3\n"
            "s off\n"
            "f 5 7 8 6\n";

    OBJConverter converter;
    ConversionOptions options;
    // Normal generation would split vertices by group, which is right but
    // would obscure what this test is about.
    options.GenerateMissingNormals = false;
    auto document = converter.ImportFromMemory(Bytes(source), options);
    Check(document != nullptr, "an OBJ with s statements imports");
    if (!document) return;

    Check(document->Meshes.size() == 1 && document->Meshes[0].Primitives.size() == 1,
          "as one primitive");
    const MeshPrimitive& prim = document->Meshes[0].Primitives[0];
    Check(prim.FaceCount() == 3, "with three faces");
    Check(prim.SmoothingGroups.size() == 3, "each carrying a smoothing group");
    if (prim.SmoothingGroups.size() == 3) {
        Check(prim.SmoothingGroups[0] == 1u, "s 1 becomes the first bit");
        Check(prim.SmoothingGroups[1] == 2u, "s 2 the second");
        Check(prim.SmoothingGroups[2] == 0u, "and s off becomes no group at all");
    }

    // Write it back and read the statements out of the text itself: what
    // matters is what a third-party reader would see.
    std::vector<uint8_t> written;
    Check(converter.ExportToMemory(*document, written, options), "and exports again");
    const std::string text(written.begin(), written.end());
    Check(text.find("\ns 1\n") != std::string::npos, "the written OBJ says s 1");
    Check(text.find("\ns 2\n") != std::string::npos, "and s 2");
    Check(text.find("\ns off\n") != std::string::npos, "and s off");

    // `s` is stateful, so a correct writer states it only where it changes.
    size_t statements = 0;
    for (size_t at = text.find("\ns "); at != std::string::npos; at = text.find("\ns ", at + 1))
        ++statements;
    Check(statements == 3, "and states it exactly three times, not once per face");

    auto again = converter.ImportFromMemory(written, options);
    Check(again != nullptr, "the written OBJ reads back");
    if (!again || again->Meshes.empty() || again->Meshes[0].Primitives.empty()) return;
    const MeshPrimitive& round = again->Meshes[0].Primitives[0];
    Check(round.SmoothingGroups == prim.SmoothingGroups,
          "with exactly the smoothing groups it started with");

    // A file that never mentions smoothing should not gain a per-face array it
    // does not need.
    auto plain = converter.ImportFromMemory(
            Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"), options);
    Check(plain && !plain->Meshes.empty() &&
          plain->Meshes[0].Primitives[0].SmoothingGroups.empty(),
          "an OBJ with no s statement carries no groups, rather than a default one");
}

int main(int argc, char** argv) {
    TestValidation();
    TestParsingCorners();
    TestPrecision();
    TestCapabilities();
    TestSmoothingGroups();
    if (argc > 1) TestSample(argv[1], argc > 2 ? argv[2] : nullptr);
    else std::printf("Sample: skipped (pass an .obj path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
