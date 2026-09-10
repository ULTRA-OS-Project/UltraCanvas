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

    Check(doc->Title == "E-45-Aircraft", "the document is titled from the file name");
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

int main(int argc, char** argv) {
    TestValidation();
    TestParsingCorners();
    TestCapabilities();
    if (argc > 1) TestSample(argv[1], argc > 2 ? argv[2] : nullptr);
    else std::printf("Sample: skipped (pass an .obj path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
