// Tests/ModelDXFTest.cpp
// DXF read as 3D geometry into ModelDocument.
//
// The interesting cases here are the two kinds of DXF. media/models/DXF holds
// an exported model — 8110 3DFACE quads, which must arrive as 8110 quads with
// the Z the 2D reader discards. media/vector/DXF holds drawings — splines and
// polylines, no 3D at all — which must be refused with an explanation rather
// than returned as an empty document, because a DXF is a drawing format first
// and most files in the wild have nothing for this reader.
//
// argv[1] is the 3D .dxf, argv[2] the .3ds of the same model, argv[3] a 2D
// drawing .dxf. Each is optional.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/DXF/UltraCanvasDXFModelConverter.h"
#include "Models/3DS/UltraCanvas3DSConverter.h"

#include <cmath>
#include <cstdio>
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

// A DXF with one 3DFACE triangle (4th corner repeating the 3rd) and one quad.
static std::string MinimalFaces() {
    return "0\nSECTION\n2\nENTITIES\n"
           "0\n3DFACE\n8\nWING\n"
           "10\n0.0\n20\n0.0\n30\n0.0\n"
           "11\n1.0\n21\n0.0\n31\n0.0\n"
           "12\n0.0\n22\n1.0\n32\n0.0\n"
           "13\n0.0\n23\n1.0\n33\n0.0\n"
           "0\n3DFACE\n8\nWING\n"
           "10\n0.0\n20\n0.0\n30\n1.0\n"
           "11\n1.0\n21\n0.0\n31\n1.0\n"
           "12\n1.0\n22\n1.0\n32\n1.0\n"
           "13\n0.0\n23\n1.0\n33\n1.0\n"
           "0\nENDSEC\n0\nEOF\n";
}

static void TestValidation() {
    std::printf("Validation\n");
    DXFModelConverter conv;
    Check(!conv.ValidateData({}), "empty data is rejected");
    Check(!conv.ValidateData(Bytes("v 0 0 0\nf 1 1 1\n")), "an OBJ is rejected");
    Check(!conv.ValidateData(Bytes("AutoCAD Binary DXF\r\n\x1a")),
          "a binary DXF is refused — this reader handles the tagged ASCII form");
    Check(conv.ValidateData(Bytes(MinimalFaces())), "a tagged ASCII DXF is accepted");

    ConversionOptions options;
    std::vector<std::string> warnings;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
    Check(conv.ImportFromMemory(Bytes("AutoCAD Binary DXF\r\n\x1a\x00 rest"), options) == nullptr,
          "a binary DXF imports as nothing");
    bool saidBinary = false;
    for (const auto& w : warnings)
        if (w.find("binary") != std::string::npos) saidBinary = true;
    Check(saidBinary, "and says why");
}

static void TestEntities() {
    std::printf("Entities\n");
    DXFModelConverter conv;
    ConversionOptions quiet;

    auto faces = conv.ImportFromMemory(Bytes(MinimalFaces()), quiet);
    Check(faces != nullptr, "3DFACE entities import");
    if (faces) {
        Check(faces->Up == UpAxis::ZUp, "DXF is read as Z-up");
        Check(faces->Meshes.size() == 1 && faces->Meshes[0].Name == "WING",
              "the layer becomes the mesh, named after it");
        const MeshPrimitive& prim = faces->Meshes[0].Primitives[0];
        Check(prim.FaceCount() == 2, "both faces arrive");
        Check(prim.Face(0).size() == 3,
              "a 3DFACE whose 4th corner repeats the 3rd is a triangle, not a degenerate quad");
        Check(prim.Face(1).size() == 4, "a genuine quad stays a quad");
        Check(prim.Mode == PrimitiveMode::Polygons,
              "a mesh mixing triangles and quads uses Polygons mode");
        // The Z the 2D reader discards.
        const Bounds3D bounds = prim.ComputeBounds();
        Check(Near(bounds.Min.z, 0.0, 1e-12) && Near(bounds.Max.z, 1.0, 1e-12),
              "the Z coordinate survives, which is the entire point");
    }

    // A polyface mesh: two position vertices short of a cube, one face record.
    auto polyface = conv.ImportFromMemory(Bytes(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nPOLYLINE\n8\nHULL\n70\n64\n71\n4\n72\n1\n"
            "0\nVERTEX\n8\nHULL\n70\n192\n10\n0\n20\n0\n30\n0\n"
            "0\nVERTEX\n8\nHULL\n70\n192\n10\n1\n20\n0\n30\n0\n"
            "0\nVERTEX\n8\nHULL\n70\n192\n10\n1\n20\n1\n30\n0\n"
            "0\nVERTEX\n8\nHULL\n70\n192\n10\n0\n20\n1\n30\n2\n"
            "0\nVERTEX\n8\nHULL\n70\n128\n71\n1\n72\n2\n73\n3\n74\n4\n"
            "0\nSEQEND\n"
            "0\nENDSEC\n0\nEOF\n"), quiet);
    Check(polyface != nullptr, "a polyface mesh imports");
    if (polyface) {
        const MeshPrimitive& prim = polyface->Meshes[0].Primitives[0];
        Check(prim.VertexCount() == 4 && prim.FaceCount() == 1,
              "its position vertices and face record are told apart by their flags");
        Check(Near(prim.ComputeBounds().Max.z, 2.0, 1e-12), "polyface Z survives");
    }

    // A 3x2 polygon mesh grid: 2 quads.
    auto grid = conv.ImportFromMemory(Bytes(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nPOLYLINE\n8\nGRID\n70\n16\n71\n3\n72\n2\n"
            "0\nVERTEX\n10\n0\n20\n0\n30\n0\n"
            "0\nVERTEX\n10\n0\n20\n1\n30\n0\n"
            "0\nVERTEX\n10\n1\n20\n0\n30\n0\n"
            "0\nVERTEX\n10\n1\n20\n1\n30\n0\n"
            "0\nVERTEX\n10\n2\n20\n0\n30\n0\n"
            "0\nVERTEX\n10\n2\n20\n1\n30\n0\n"
            "0\nSEQEND\n"
            "0\nENDSEC\n0\nEOF\n"), quiet);
    Check(grid && grid->TotalFaceCount() == 2, "a 3x2 polygon mesh becomes 2 quads");

    // Lines and points: the document has modes for both.
    auto wire = conv.ImportFromMemory(Bytes(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLINE\n8\nAXIS\n10\n0\n20\n0\n30\n0\n11\n0\n21\n0\n31\n5\n"
            "0\nPOINT\n8\nAXIS\n10\n1\n20\n2\n30\n3\n"
            "0\nENDSEC\n0\nEOF\n"), quiet);
    Check(wire != nullptr, "LINE and POINT import");
    if (wire) {
        bool haveLines = false, havePoints = false;
        for (const auto& prim : wire->Meshes[0].Primitives) {
            if (prim.Mode == PrimitiveMode::Lines) haveLines = true;
            if (prim.Mode == PrimitiveMode::Points) havePoints = true;
        }
        Check(haveLines && havePoints, "they land in Lines and Points primitives");
    }

    // The layer table's colour reaches the material.
    auto colored = conv.ImportFromMemory(Bytes(
            "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n"
            "0\nLAYER\n2\nWING\n62\n1\n"
            "0\nENDTAB\n0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n"
            "0\n3DFACE\n8\nWING\n"
            "10\n0\n20\n0\n30\n0\n11\n1\n21\n0\n31\n0\n"
            "12\n0\n22\n1\n32\n0\n13\n0\n23\n1\n33\n0\n"
            "0\nENDSEC\n0\nEOF\n"), quiet);
    Check(colored && colored->Materials.size() == 1, "the layer becomes a material");
    if (colored) {
        const ModelMaterial& material = colored->Materials[0];
        // ACI 1 is red.
        Check(material.Phong.has_value() && material.Phong->Diffuse.x > 0.9f &&
              material.Phong->Diffuse.y < 0.1f,
              "the layer's ACI colour resolves through the shared CAD palette");
        Check(material.Extras.count("dxf.aci") == 1, "the raw ACI index is kept");
    }

    // A header unit.
    auto metres = conv.ImportFromMemory(Bytes(
            "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n6\n0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n"
            "0\n3DFACE\n8\n0\n"
            "10\n0\n20\n0\n30\n0\n11\n1\n21\n0\n31\n0\n"
            "12\n0\n22\n1\n32\n0\n13\n0\n23\n1\n33\n0\n"
            "0\nENDSEC\n0\nEOF\n"), quiet);
    Check(metres && metres->SourceUnit == ModelUnit::Meter,
          "$INSUNITS reaches SourceUnit");
}

// A DXF with no 3D in it is the common case, and returning an empty document
// would look like a reader bug rather than a property of the file.
static void TestDrawingOnly(const char* drawingPath) {
    std::printf("A drawing, not a model: %s\n", drawingPath);
    DXFModelConverter conv;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    Check(conv.ValidateFile(drawingPath), "it is a valid DXF");
    Check(!DXFModelConverter::HasThreeDimensionalGeometry(drawingPath),
          "the cheap pre-check says it has no 3D entities");
    Check(conv.Import(drawingPath, options) == nullptr, "importing it as a model yields nothing");
    bool explained = false;
    for (const auto& w : warnings)
        if (w.find("no 3D geometry") != std::string::npos &&
            w.find("Vector plugin") != std::string::npos) explained = true;
    Check(explained, "and the warning names the reader that should have it instead");
}

static void TestSample(const char* dxfPath, const char* dsPath) {
    std::printf("Sample: %s\n", dxfPath);
    DXFModelConverter conv;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    Check(DXFModelConverter::HasThreeDimensionalGeometry(dxfPath),
          "the pre-check finds 3D entities");
    auto doc = conv.Import(dxfPath, options);
    if (!doc) { std::printf("  [FAIL] import returned nothing\n"); ++failures; return; }

    Check(doc->Title == "E-45-Aircraft", "the document is titled from the file name");
    Check(doc->Up == UpAxis::ZUp, "DXF is Z-up");
    Check(doc->Metadata.count("dxf.version") == 1 && doc->Metadata.at("dxf.version") == "AC1009",
          "the DXF version is recorded (R12)");
    Check(doc->SourceUnit == ModelUnit::Unspecified,
          "an R12 file states no $INSUNITS, and none is invented");
    Check(doc->Meshes.size() == 1 && doc->Meshes[0].Name == "0",
          "all geometry is on layer 0, so there is one mesh");
    Check(doc->TotalFaceCount() == 8110, "all 8110 3DFACE entities are read");
    Check(doc->TotalVertexCount() == 8110 * 4,
          "3DFACE is unindexed, so each quad brings its own four corners");

    bool allQuads = true;
    for (const auto& prim : doc->Meshes[0].Primitives)
        for (size_t f = 0; f < prim.FaceCount(); ++f)
            if (prim.Face(f).size() != 4) allQuads = false;
    Check(allQuads, "every face is a quad, as in the OBJ export of the same model");

    bool normalsDerived = true;
    for (const auto& prim : doc->Meshes[0].Primitives)
        if (prim.Normals.size() != prim.VertexCount()) normalsDerived = false;
    Check(normalsDerived, "normals are derived — DXF stores none");

    if (!dsPath) return;
    ThreeDSConverter threeDS;
    ConversionOptions quiet;
    auto fromDS = threeDS.Import(dsPath, quiet);
    if (!fromDS) { std::printf("  [FAIL] the 3DS companion did not load\n"); ++failures; return; }

    // Both are Z-up exports of the same scene, so no axis conversion is needed:
    // the coordinates should agree directly.
    const Bounds3D dxfBounds = doc->ComputeBounds();
    const Bounds3D dsBounds = fromDS->ComputeBounds();
    Check(Near(dxfBounds.Min.x, dsBounds.Min.x, 1e-3) && Near(dxfBounds.Max.x, dsBounds.Max.x, 1e-3) &&
          Near(dxfBounds.Min.y, dsBounds.Min.y, 1e-3) && Near(dxfBounds.Max.y, dsBounds.Max.y, 1e-3) &&
          Near(dxfBounds.Min.z, dsBounds.Min.z, 1e-3) && Near(dxfBounds.Max.z, dsBounds.Max.z, 1e-3),
          "the DXF lands on the same coordinates as the 3DS export of the same model");
    Check(doc->TotalFaceCount() * 2 == fromDS->TotalFaceCount(),
          "the DXF's quads are the 3DS's triangle pairs");

    ModelDocument triangulated = *doc;
    triangulated.TriangulateAll();
    Check(triangulated.TotalFaceCount() == fromDS->TotalFaceCount(),
          "triangulating the DXF gives exactly the 3DS triangle count");
}

int main(int argc, char** argv) {
    TestValidation();
    TestEntities();
    if (argc > 1) TestSample(argv[1], argc > 2 ? argv[2] : nullptr);
    else std::printf("Sample: skipped (pass a 3D .dxf path to run it)\n");
    if (argc > 3) TestDrawingOnly(argv[3]);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
