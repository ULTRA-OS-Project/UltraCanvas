// Tests/Model3DSTest.cpp
// The 3DS reader against the real E-45 aircraft sample in media/models/3DS.
//
// 3DS is the first scene format UltraCanvas reads into ModelDocument, so this
// is also the first end-to-end check that the structure holds one: several
// named meshes, per-object matrices, materials with texture maps, and per-face
// material groups.
//
// Pass the .3ds path as argv[1]; without it only the malformed-input cases
// run, so the test still means something in a checkout without media.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/3DS/UltraCanvas3DSConverter.h"
#include "Models/UltraCanvasModelMesh3D.h"

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

// A reader must survive anything, not just files a modeller exported.
static void TestMalformed() {
    std::printf("Malformed input\n");
    ThreeDSConverter conv;
    ConversionOptions quiet;

    Check(!conv.ValidateData({}), "empty data is rejected");
    Check(!conv.ValidateData({0x4D, 0x4D}), "a truncated header is rejected");
    Check(!conv.ValidateData({0x00, 0x00, 0x06, 0x00, 0x00, 0x00}),
          "a wrong main chunk id is rejected");
    // MAIN3DS whose declared length far exceeds the data.
    Check(!conv.ValidateData({0x4D, 0x4D, 0xFF, 0xFF, 0xFF, 0x7F}),
          "a main chunk longer than the file is rejected");

    // A well-formed but empty MAIN3DS: valid signature, no meshes.
    std::vector<uint8_t> empty = {0x4D, 0x4D, 0x06, 0x00, 0x00, 0x00};
    Check(conv.ValidateData(empty), "an empty MAIN3DS passes validation");
    Check(conv.ImportFromMemory(empty, quiet) == nullptr, "an empty MAIN3DS imports as nothing");

    // A chunk claiming to extend past its parent must end the scan, not read
    // out of bounds. Under a sanitiser this is the case that would catch it.
    std::vector<uint8_t> overrun = {0x4D, 0x4D, 0x14, 0x00, 0x00, 0x00,
                                    0x3D, 0x3D, 0xF0, 0xFF, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    Check(conv.ImportFromMemory(overrun, quiet) == nullptr,
          "a sub-chunk overrunning its parent is refused, not followed");
}

static void TestCapabilities() {
    std::printf("Capabilities\n");
    ThreeDSConverter conv;
    const FormatCapabilities caps = conv.GetCapabilities();
    Check(conv.CanImport() && !conv.CanExport(), "3DS is read-only");
    Check(caps.Meshes && caps.Materials && caps.Textures && caps.TextureCoordinates,
          "claims meshes, materials, textures and UVs");
    // The capability report must describe the implementation, not the ambition.
    Check(!caps.PBRMaterials && !caps.Skinning && !caps.MorphTargets && !caps.NGons &&
          !caps.DoublePrecision && !caps.VertexColors,
          "claims nothing 3DS or this reader does not do");
    Check(!caps.Animations && !caps.SceneGraph,
          "does not claim the KFDATA hierarchy and animation it skips");
}

static void TestSample(const char* path) {
    std::printf("Sample: %s\n", path);
    ThreeDSConverter conv;
    ConversionOptions options;
    std::vector<std::string> warnings;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    Check(conv.ValidateFile(path), "the file validates by signature");

    auto doc = conv.Import(path, options);
    if (!doc) { std::printf("  [FAIL] import returned nothing\n"); ++failures; return; }

    // Derived from the path rather than hard-coded: the rule under test is
    // "the title is the file's stem", and a constant here only tests that the
    // sample still lives where it did — it fails on any copy of the same file,
    // which is not a defect in the reader.
    Check(doc->Title == std::filesystem::path(path).stem().string(),
          "the document is titled from the file name");
    Check(doc->SourceFormat == "3ds", "the source format is recorded");
    Check(doc->Up == UpAxis::ZUp, "3DS is read as Z-up");
    Check(doc->SourceUnit == ModelUnit::Unspecified,
          "3DS states no unit, and the reader does not invent one");

    // Two NAMED_OBJECTs, each one N_TRI_OBJECT with a single material group.
    Check(doc->Meshes.size() == 2 && doc->Nodes.size() == 2, "both objects arrive as mesh nodes");
    Check(doc->Materials.size() == 2, "both materials are read");
    Check(doc->Images.size() == 4, "the four distinct texture maps become images");
    Check(doc->TotalVertexCount() == 12227, "every vertex is read (11213 + 1014)");
    Check(doc->TotalFaceCount() == 16220, "every face is read (14732 + 1488)");

    // The object matrix goes on the node and its inverse into the vertices, so
    // the composed world bounds must equal the raw world-space POINT_ARRAY
    // extents in the file.
    const Bounds3D bounds = doc->ComputeBounds();
    Check(Near(bounds.Min.x, -0.9732, 1e-3) && Near(bounds.Max.x, 0.9732, 1e-3) &&
          Near(bounds.Min.y, -3.2049, 1e-3) && Near(bounds.Max.y, 2.9357, 1e-3) &&
          Near(bounds.Min.z, -1.3469, 1e-3) && Near(bounds.Max.z, 2.8478, 1e-3),
          "world bounds match the file's world-space vertices");

    // The second object carries a real MESH_MATRIX translation.
    bool foundTransformedNode = false;
    for (const auto& node : doc->Nodes)
        if (Near(node.Translation.z, 1.5256, 1e-3) && Near(node.Translation.y, -0.1015, 1e-3))
            foundTransformedNode = true;
    Check(foundTransformedNode, "the object's own coordinate frame survives as a node transform");

    // Every primitive must be complete: material, UVs and derived normals.
    size_t primitives = 0;
    bool allComplete = true;
    for (const auto& mesh : doc->Meshes) {
        for (const auto& prim : mesh.Primitives) {
            ++primitives;
            const VertexAttribute* uv = prim.FindAttribute(AttributeSemantic::TexCoord, 0);
            if (prim.Material < 0 || prim.Normals.size() != prim.VertexCount() ||
                uv == nullptr || uv->Count() != prim.VertexCount())
                allComplete = false;
        }
    }
    Check(primitives == 2, "one primitive per material group");
    Check(allComplete, "each primitive has a material, per-vertex UVs and derived normals");

    // Regression: 3DS states materials in Phong terms with a white specular on
    // painted, non-metal surfaces. Deriving metalness from the specular alone
    // made the whole aircraft import as raw metal.
    bool anyMetal = false;
    bool phongKept = true;
    for (const auto& material : doc->Materials) {
        if (material.MetallicFactor > 0.0f) anyMetal = true;
        if (!material.Phong.has_value()) phongKept = false;
    }
    Check(!anyMetal, "a white specular on a bright diffuse does not import as metal");
    Check(phongKept, "the source Phong parameters are kept beside the derived PBR");

    // Truncated names and stacked maps are reported, not silently swallowed.
    bool warnedTruncation = false, warnedStackedMaps = false;
    for (const auto& w : warnings) {
        if (w.find("truncated") != std::string::npos) warnedTruncation = true;
        if (w.find("stacks") != std::string::npos) warnedStackedMaps = true;
    }
    Check(warnedTruncation, "12-character texture names are reported as probably truncated");
    Check(warnedStackedMaps, "extra images stacked in one map slot are reported");

    // The path a viewer takes.
    const Mesh3D flat = ModelDocumentToMesh3D(*doc);
    Check(flat.TriangleCount() == 16220, "flattening to Mesh3D keeps every triangle");
    Check(Near(flat.bounds.min.x, bounds.Min.x, 1e-3) &&
          Near(flat.bounds.max.z, bounds.Max.z, 1e-3),
          "flattening to Mesh3D keeps the world bounds");

    std::printf("  (%zu warnings reported)\n", warnings.size());
}

int main(int argc, char** argv) {
    TestMalformed();
    TestCapabilities();
    if (argc > 1) TestSample(argv[1]);
    else std::printf("Sample: skipped (pass a .3ds path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
