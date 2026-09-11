// Tests/ModelAlembicTest.cpp
// The Alembic reader: the Ogawa container, Alembic's object and property
// model, and AbcGeom on top of them.
//
// The sample is media/models/Alembic/E-45-Aircraft.abc — the same aircraft the
// 3DS, OBJ, DXF and COLLADA suites read, exported from the same .blend by
// Blender in 2017. That makes the cross-format comparisons here worth more
// than any single-format assertion: the Alembic and the COLLADA were both
// exported without applying the mirror modifier, and this suite pins that
// difference rather than letting a future change quietly "fix" it.
//
// The one assertion to read carefully is the winding. Alembic winds a face's
// indices the opposite way from the outward-normal convention, and the file
// carries its own per-corner normals — so "does each face's computed normal
// agree with the one the file stored" is an independent check of a decision
// that is otherwise invisible until a model renders inside out.
//
// argv[1] is media/models. Without it only the container cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/Alembic/UltraCanvasAlembicConverter.h"
#include "Models/Alembic/UltraCanvasOgawaFile.h"
#include "Models/OBJ/UltraCanvasOBJConverter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelStorage;
using namespace UltraCanvas::ModelConverter;
namespace Og = UltraCanvas::Ogawa;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static void CheckNear(double actual, double expected, double tolerance,
                      const std::string& what) {
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("  [%s] %s (%.6g vs %.6g)\n", ok ? "PASS" : "FAIL", what.c_str(),
                actual, expected);
    if (!ok) ++failures;
}
static std::vector<uint8_t> Bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

// ===== CONTAINER =====

static void TestSignatures() {
    std::printf("Signatures\n");
    AlembicConverter converter;
    Check(!converter.ValidateData({}), "empty data is rejected");
    Check(!converter.ValidateData(Bytes("solid teapot\nfacet normal 0 0 1\n")),
          "an ASCII STL is rejected");
    Check(!converter.ValidateData(Bytes("ISO-10303-21;\nHEADER;\n")), "a STEP file is rejected");
    // Built byte by byte rather than from a literal: an Ogawa header has a
    // NUL in it, and a string literal would stop there.
    std::vector<uint8_t> ogawa = {'O', 'g', 'a', 'w', 'a', 0xff, 0x00, 0x01};
    ogawa.resize(16, 0);
    Check(converter.ValidateData(ogawa), "the Ogawa magic is what makes it Alembic");
    ogawa.resize(8);
    Check(!converter.ValidateData(ogawa),
          "but a file too short to hold a root pointer is not one");

    // The other Alembic backend. Telling it apart matters: "not Alembic" would
    // be a lie, and the user needs to know a re-export fixes it.
    const std::vector<uint8_t> hdf5 = {0x89, 'H', 'D', 'F', '\r', '\n', 0x1a, '\n',
                                       0, 0, 0, 0, 0, 0, 0, 0};
    Check(Og::LooksLikeHdf5File(hdf5), "an HDF5-backed archive is recognised as such");
    Check(!Og::LooksLikeOgawaFile(hdf5), "and not mistaken for an Ogawa one");

    ConversionOptions options;
    std::string reported;
    options.WarningCallback = [&reported](const std::string& m) { reported = m; };
    Check(converter.ImportFromMemory(hdf5, options) == nullptr, "it is not read");
    Check(reported.find("HDF5") != std::string::npos && reported.find("Ogawa") != std::string::npos,
          "and the warning names the backend and the way out of it");

    Check(converter.ImportFromMemory(Bytes("not an archive"), options) == nullptr,
          "and a file that is not Alembic at all reads as nothing");
    // A truncated archive must not read past its own end.
    Check(converter.ImportFromMemory(Bytes("Ogawa\xff\x00\x01\xff\xff\xff\xff\xff\xff\xff\xff"),
                                     options) == nullptr,
          "an archive whose root offset is past the end of the file is refused, not followed");
}

static void TestMetadataStrings() {
    std::printf("Metadata strings\n");
    const std::string metadata =
            "arrayExtent=1;geoScope=fvr;interpretation=normal;isGeomParam=true;podExtent=3";
    Check(Og::MetadataValue(metadata, "geoScope") == "fvr", "a key in the middle is found");
    Check(Og::MetadataValue(metadata, "arrayExtent") == "1", "the first key is found");
    Check(Og::MetadataValue(metadata, "podExtent") == "3", "and the last, with no trailing ;");
    Check(Og::MetadataValue(metadata, "scope").empty(),
          "a key that is only a suffix of another does not match");
    Check(Og::MetadataValue(metadata, "interp").empty(), "nor a prefix of one");
    Check(Og::MetadataValue("", "geoScope").empty(), "and an empty string yields nothing");
}

// ===== THE SAMPLE =====

static void TestSample(const std::string& mediaRoot) {
    std::printf("The E-45 aircraft, as Blender exported it to Alembic\n");
    const std::string path =
            (std::filesystem::path(mediaRoot) / "Alembic/E-45-Aircraft.abc").string();

    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& m) { warnings.push_back(m); };

    AlembicConverter converter;
    Check(converter.ValidateFile(path), "the file validates as Alembic");
    auto document = converter.Import(path, options);
    Check(document != nullptr, "and reads");
    if (!document) return;
    for (const std::string& warning : warnings) std::printf("      warn: %s\n", warning.c_str());
    Check(warnings.empty(), "with nothing to report");

    // --- the archive's own account of itself ---
    Check(document->Metadata["writtenBy"] == "Blender",
          "the archive says which application wrote it");
    Check(document->Metadata["sourceScene"].find(".blend") != std::string::npos,
          "and names the scene it came from — the same .blend the other exports did");
    Check(document->Metadata["alembicVersion"] == "10600",
          "the file version is Alembic 1.6.0");
    Check(document->Up == UpAxis::YUp, "Alembic states no up axis, so Y is recorded by convention");

    // --- the hierarchy ---
    Check(document->Nodes.size() == 5, "five nodes: an armature, two transforms, two shapes");
    Check(document->Meshes.size() == 2, "and two meshes");
    int roots = 0;
    for (const ModelNode& node : document->Nodes) if (node.Parent < 0) ++roots;
    Check(roots == 1, "under one root");
    Check(!document->Nodes.empty() && document->Nodes[0].Name == "Armature",
          "which is the armature the transforms hang from");

    // --- the geometry ---
    Check(document->TotalFaceCount() == 1681, "1681 faces in total");
    size_t quads = 0, triangles = 0, other = 0;
    for (const ModelMesh& mesh : document->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives) {
            Check(prim.Mode == PrimitiveMode::Polygons,
                  "a mesh of quads is kept as polygons rather than triangulated on the way in");
            for (size_t f = 0; f < prim.FaceCount(); ++f) {
                const size_t corners = prim.Face(f).size();
                if (corners == 4) ++quads;
                else if (corners == 3) ++triangles;
                else ++other;
            }
        }
    Check(quads == 1616 && triangles == 65 && other == 0,
          "1616 quads and 65 triangles, exactly as the file stores them");

    // --- face sets became primitives ---
    bool ship = false, material = false;
    for (const ModelMesh& mesh : document->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives) {
            if (prim.Name == "ship") ship = true;
            if (prim.Name == "Material_004") material = true;
        }
    Check(ship && material,
          "each mesh's face set names its primitive, so a material assignment survives");

    // --- normals and UVs, de-indexed from face-varying ---
    for (const ModelMesh& mesh : document->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives) {
            Check(prim.Normals.size() == prim.Positions.size(),
                  "every vertex has a normal from the file, not a generated one");
            const VertexAttribute* uv = prim.FindAttribute(AttributeSemantic::TexCoord, 0);
            Check(uv != nullptr && uv->Count() == prim.Positions.size(),
                  "and a texture coordinate, resolved through the uv index array");
        }

    // --- winding: the assertion this reader exists to get right ---
    //
    // Alembic winds faces the opposite way from the outward-normal convention.
    // The file carries its own per-corner normals, so comparing each face's
    // computed normal against the stored one is an independent check: before
    // the reversal, 1680 of 1681 faces disagreed.
    size_t agree = 0, disagree = 0;
    double volume = 0.0;
    for (const ModelMesh& mesh : document->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            for (size_t f = 0; f < prim.FaceCount(); ++f) {
                const std::vector<uint32_t> face = prim.Face(f);
                if (face.size() < 3) continue;
                std::vector<Vec3d> points;
                for (uint32_t corner : face) points.push_back(prim.Positions[corner]);
                const Vec3d computed = NewellNormal(points);
                const Vec3f& stored = prim.Normals[face[0]];
                const double dot = computed.x * stored.x + computed.y * stored.y +
                                   computed.z * stored.z;
                if (dot > 0.0) ++agree; else if (dot < 0.0) ++disagree;
                for (size_t k = 1; k + 1 < points.size(); ++k)
                    volume += points[0].Dot(points[k].Cross(points[k + 1])) / 6.0;
            }
    std::printf("      %zu faces agree with the file's own normals, %zu disagree\n",
                agree, disagree);
    Check(agree > disagree * 50,
          "each face's winding agrees with the normal the file stored for it");
    Check(volume > 0.0,
          "and the signed volume is positive, so the model is not inside out");
}

// The interesting comparison, and the reason this file is in the repository:
// the same aircraft, exported twice from one .blend, is not the same model.
static void TestAgainstTheObjExport(const std::string& mediaRoot) {
    std::printf("The same aircraft, against the OBJ export\n");
    const std::string alembicPath =
            (std::filesystem::path(mediaRoot) / "Alembic/E-45-Aircraft.abc").string();
    const std::string objPath =
            (std::filesystem::path(mediaRoot) / "OBJ/E-45-Aircraft.obj").string();
    if (!std::filesystem::exists(objPath)) {
        std::printf("  (skipped: the OBJ sample is not present)\n");
        return;
    }

    ConversionOptions quiet;
    AlembicConverter alembicConverter;
    OBJConverter objConverter;
    auto alembic = alembicConverter.Import(alembicPath, quiet);
    auto obj = objConverter.Import(objPath, quiet);
    Check(alembic && obj, "both exports read");
    if (!alembic || !obj) return;

    Check(obj->TotalFaceCount() == 8110, "the OBJ has 8110 faces");
    Check(alembic->TotalFaceCount() == 1681, "the Alembic has 1681");
    Check(obj->TotalFaceCount() > alembic->TotalFaceCount() * 4,
          "the OBJ is several times denser, because its export applied the modifiers");

    // The giveaway is the shape, not the count. The hull's own X range stops
    // dead at zero in the Alembic: the mirror modifier was never applied, so
    // half the aircraft is missing — exactly as the .dae export in the COLLADA
    // suite is missing it, and from the same .blend.
    const Bounds3D alembicBounds = alembic->ComputeBounds();
    const Bounds3D objBounds = obj->ComputeBounds();
    std::printf("      Alembic X [%.4f, %.4f]   OBJ X [%.4f, %.4f]\n",
                alembicBounds.Min.x, alembicBounds.Max.x, objBounds.Min.x, objBounds.Max.x);
    Check(std::fabs(objBounds.Min.x + objBounds.Max.x) < objBounds.Size().x * 0.05,
          "the OBJ is symmetric about X, as a whole aircraft is");
    Check(std::fabs(alembicBounds.Min.x) > std::fabs(alembicBounds.Max.x) * 1.5,
          "the Alembic is not: its hull runs to one side of zero only, so the mirror "
          "modifier was never applied and half the aircraft is missing");
    CheckNear(alembicBounds.Size().z, objBounds.Size().z, objBounds.Size().z * 0.05,
              "the length along Z does match, so it is the same aircraft, not a different one");
}

int main(int argc, char** argv) {
    TestSignatures();
    TestMetadataStrings();
    if (argc > 1) {
        TestSample(argv[1]);
        TestAgainstTheObjExport(argv[1]);
    } else {
        std::printf("Sample: skipped (pass the media/models path to run it)\n");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
