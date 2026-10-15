// Tests/Model3DSTest.cpp
// The 3DS reader against the real E-45 aircraft sample in media/3D/3DS.
//
// 3DS is the first scene format UltraCanvas reads into ModelDocument, so this
// is also the first end-to-end check that the structure holds one: several
// named meshes, per-object matrices, materials with texture maps, and per-face
// material groups.
//
// Pass the .3ds path as argv[1]; without it only the malformed-input cases
// run, so the test still means something in a checkout without media.
//
// The numbers the reader keeps as text (a material's shininess strength, a
// camera's target and bank, in Extras) are written dot-decimal: read under a
// comma-decimal locale a file gives the very same text as under "C" - the
// sample, and a file made here that has both (the sample has neither).
// Skipped where no such locale is installed.
//
// Version: 1.1.0 - extras are the same text under a comma-decimal locale
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/3DS/UltraCanvas3DSConverter.h"
#include "Models/UltraCanvasModelMesh3D.h"

#include <cctype>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <map>
#include <memory>
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
    // 3DS gained a writer; the round trips live in ModelWriterTest, and this
    // is the declaration they rest on.
    Check(conv.CanImport() && conv.CanExport(), "3DS reads and writes");
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

// Every Extras and Metadata entry of a document, keyed by where it is.
static std::map<std::string, std::string> AllExtras(const ModelDocument& doc) {
    std::map<std::string, std::string> all;
    for (const auto& [key, value] : doc.Metadata) all["document/" + key] = value;
    for (size_t i = 0; i < doc.Materials.size(); ++i)
        for (const auto& [key, value] : doc.Materials[i].Extras)
            all["material" + std::to_string(i) + "/" + key] = value;
    for (size_t i = 0; i < doc.Nodes.size(); ++i)
        for (const auto& [key, value] : doc.Nodes[i].Extras)
            all["node" + std::to_string(i) + "/" + key] = value;
    return all;
}

// A 3DS chunk: id, length (header included), body.
static std::vector<uint8_t> Chunk(uint16_t id, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    const uint32_t length = static_cast<uint32_t>(body.size() + 6);
    out.push_back(static_cast<uint8_t>(id & 0xFF));
    out.push_back(static_cast<uint8_t>(id >> 8));
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((length >> (8 * i)) & 0xFF));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
static void Append(std::vector<uint8_t>& out, const std::vector<uint8_t>& more) { out.insert(out.end(), more.begin(), more.end()); }
static void AppendF32(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFF));
}
static void AppendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
static void AppendName(std::vector<uint8_t>& out, const std::string& name) {
    out.insert(out.end(), name.begin(), name.end());
    out.push_back(0);
}

// One triangle, a material whose shininess strength is 35 %, and a camera
// whose target and bank have fractions - every number the reader keeps as
// text.
static std::vector<uint8_t> MakeExtrasFile() {
    std::vector<uint8_t> points;
    AppendU16(points, 3);
    for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) AppendF32(points, v);
    std::vector<uint8_t> faces;
    AppendU16(faces, 1);
    for (uint16_t v : {0, 1, 2, 0}) AppendU16(faces, v);
    std::vector<uint8_t> tri;
    Append(tri, Chunk(0x4110, points));
    Append(tri, Chunk(0x4120, faces));
    std::vector<uint8_t> mesh;
    AppendName(mesh, "tri");
    Append(mesh, Chunk(0x4100, tri));

    std::vector<uint8_t> percent;
    AppendF32(percent, 0.35f);
    std::vector<uint8_t> matName;
    AppendName(matName, "shiny");
    std::vector<uint8_t> material;
    Append(material, Chunk(0xA000, matName));
    Append(material, Chunk(0xA041, Chunk(0x0031, percent)));

    std::vector<uint8_t> cameraBody;
    for (float v : {10.f, 20.f, 30.f, 1.5f, 2.25f, -3.75f, 12.5f, 35.f}) AppendF32(cameraBody, v);
    std::vector<uint8_t> camera;
    AppendName(camera, "cam");
    Append(camera, Chunk(0x4700, cameraBody));

    std::vector<uint8_t> editor;
    Append(editor, Chunk(0xAFFF, material));
    Append(editor, Chunk(0x4000, mesh));
    Append(editor, Chunk(0x4000, camera));
    return Chunk(0x4D4D, Chunk(0x3D3D, editor));
}

// Imports `path` (or `data` when it is not empty) under "C" and under a
// comma-decimal locale; false (and a skip line) where none is installed.
static bool ImportInBothLocales(const char* path, const std::vector<uint8_t>& data,
                                std::shared_ptr<ModelDocument>& inC,
                                std::shared_ptr<ModelDocument>& inComma, std::string& comma) {
    const char* original = std::setlocale(LC_ALL, nullptr);
    const std::string saved = original ? original : "C";
    ThreeDSConverter conv;
    ConversionOptions options;
    auto import = [&]() { return data.empty() ? conv.Import(path, options) : conv.ImportFromMemory(data, options); };
    std::setlocale(LC_ALL, "C");
    inC = import();
    for (const char* candidate : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "de_DE"}) {
        if (std::setlocale(LC_ALL, candidate)) { comma = candidate; break; }
    }
    if (comma.empty()) {
        std::setlocale(LC_ALL, saved.c_str());
        std::printf("  [SKIP] no comma-decimal locale installed\n");
        return false;
    }
    inComma = import();
    std::setlocale(LC_ALL, saved.c_str());
    return true;
}

static void TestExtrasIgnoreLocale() {
    std::printf("Extras under a comma-decimal locale\n");
    std::shared_ptr<ModelDocument> inC, inComma;
    std::string comma;
    if (!ImportInBothLocales(nullptr, MakeExtrasFile(), inC, inComma, comma)) return;
    if (!inC || !inComma) { Check(false, "the made file imports in both locales"); return; }
    const auto c = AllExtras(*inC);
    const auto commaExtras = AllExtras(*inComma);
    bool shininess = false, target = false, bank = false;
    for (const auto& [key, value] : commaExtras) {
        if (key.find("3ds.shininessStrength") != std::string::npos) shininess = value == "0.35";
        if (key.find("3ds.target") != std::string::npos) target = value == "1.5 2.25 -3.75";
        if (key.find("3ds.bank") != std::string::npos) bank = value == "12.5";
    }
    Check(shininess, "the shininess strength is written 0.35 under " + comma);
    Check(target, "the camera target is written 1.5 2.25 -3.75");
    Check(bank, "the camera bank is written 12.5");
    Check(c == commaExtras, "every extra reads the same as under C");
}

static void TestSampleIgnoresLocale(const char* path) {
    std::printf("Sample under a comma-decimal locale\n");
    std::shared_ptr<ModelDocument> inC, inComma;
    std::string comma;
    if (!ImportInBothLocales(path, {}, inC, inComma, comma)) return;
    if (!inC || !inComma) { Check(false, "the sample imports in both locales"); return; }
    const auto c = AllExtras(*inC);
    const auto commaExtras = AllExtras(*inComma);
    Check(c == commaExtras, "every extra reads the same under " + comma + " as under C");
    bool dotDecimal = true;
    for (const auto& [key, value] : commaExtras) {
        for (size_t i = 1; i + 1 < value.size(); ++i)
            if (value[i] == ',' && std::isdigit(static_cast<unsigned char>(value[i - 1])) &&
                std::isdigit(static_cast<unsigned char>(value[i + 1]))) dotDecimal = false;
    }
    Check(dotDecimal, "no extra holds a decimal comma");
}

int main(int argc, char** argv) {
    TestMalformed();
    TestCapabilities();
    TestExtrasIgnoreLocale();
    if (argc > 1) TestSample(argv[1]);
    if (argc > 1) TestSampleIgnoresLocale(argv[1]);
    else std::printf("Sample: skipped (pass a .3ds path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
