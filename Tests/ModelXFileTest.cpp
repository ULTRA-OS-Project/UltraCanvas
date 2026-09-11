// Tests/ModelXFileTest.cpp
// The DirectX .x reader.
//
// The sample is the same E-45 aircraft every other model suite reads, and it is
// the same *export* as the COLLADA one: 1995 triangles, two meshes, the mirror
// modifier not applied. Asserting that across formats is what stops a later
// change quietly altering either reader.
//
// The synthetic half carries the weight, because one text export reaches
// neither the binary encoding nor any of the cases that make this format
// treacherous: the left-handed winding, normals indexed apart from positions,
// several materials on one mesh, and a truncated object. The binary cases are
// built byte by byte - a binary lexer nothing exercises is a liability, and
// nothing in the sample exercises it.
//
// argv[1] is the .x. Without it only the synthetic cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/XFile/UltraCanvasXFileConverter.h"

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

static std::shared_ptr<ModelDocument> Read(const std::vector<uint8_t>& data,
                                           std::vector<std::string>* warnings = nullptr) {
    XFileConverter converter;
    ConversionOptions options;
    if (warnings)
        options.WarningCallback = [warnings](const std::string& w) { warnings->push_back(w); };
    return converter.ImportFromMemory(data, options);
}

static std::shared_ptr<ModelDocument> ReadText(const std::string& body,
                                               std::vector<std::string>* warnings = nullptr) {
    return Read(Bytes("xof 0303txt 0032\n" + body), warnings);
}

// One triangle, with the caller's frame matrix around it and its own normal.
static std::string Triangle(const std::string& frameMatrix, const std::string& normal) {
    return "Frame Root {\n"
           "  FrameTransformMatrix { " + frameMatrix + ";; }\n"
           "  Mesh {\n"
           "    3;\n"
           "    0.0;0.0;0.0;, 1.0;0.0;0.0;, 0.0;1.0;0.0;;\n"
           "    1;\n"
           "    3;0,1,2;;\n"
           "    MeshNormals {\n"
           "      1;\n"
           "      " + normal + ";;\n"
           "      1;\n"
           "      3;0,0,0;;\n"
           "    }\n"
           "  }\n"
           "}\n";
}

static const char* kIdentity = "1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0";

// ===== BINARY BUILDER =====
// The binary encoding is the same language as the text one, written as 16-bit
// tokens. Building it here by hand is the only way to exercise that lexer.

namespace binary {
enum : uint16_t { Name = 1, String = 2, Integer = 3, IntegerList = 6, FloatList = 7,
                  OBrace = 10, CBrace = 11, Semicolon = 20, Template = 31 };

static void PutU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xff));
    out.push_back(static_cast<uint8_t>(value >> 8));
}
static void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
}
static void PutName(std::vector<uint8_t>& out, const std::string& text) {
    PutU16(out, Name);
    PutU32(out, static_cast<uint32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}
static void PutString(std::vector<uint8_t>& out, const std::string& text) {
    PutU16(out, String);
    PutU32(out, static_cast<uint32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    PutU16(out, Semicolon);
}
static void PutIntegers(std::vector<uint8_t>& out, const std::vector<int32_t>& values) {
    PutU16(out, IntegerList);
    PutU32(out, static_cast<uint32_t>(values.size()));
    for (int32_t v : values) PutU32(out, static_cast<uint32_t>(v));
}
static void PutFloats(std::vector<uint8_t>& out, const std::vector<float>& values) {
    PutU16(out, FloatList);
    PutU32(out, static_cast<uint32_t>(values.size()));
    for (float v : values) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, 4);
        PutU32(out, bits);
    }
}
} // namespace binary

static void TestValidation() {
    std::printf("Validation\n");
    XFileConverter converter;
    Check(!converter.ValidateData({}), "empty data is rejected");
    Check(!converter.ValidateData(Bytes("solid teapot\nfacet normal 0 0 1\n")),
          "an ASCII STL is rejected");
    Check(converter.ValidateData(Bytes("xof 0303txt 0032\n")), "a text .x header is accepted");
    Check(converter.ValidateData(Bytes("xof 0302bin 0064\n")), "a binary .x header is accepted");

    std::vector<std::string> warnings;
    Check(Read(Bytes("xof 0303tzip 0032\nrubbish"), &warnings) == nullptr,
          "an MSZIP-compressed file reads as nothing");
    bool named = false;
    for (const std::string& w : warnings)
        if (w.find("compressed") != std::string::npos && w.find("0303txt") != std::string::npos)
            named = true;
    Check(named, "and the warning names the encoding and what to re-export as");

    warnings.clear();
    Check(Read(Bytes("not an x file at all"), &warnings) == nullptr, "a non-.x file reads as nothing");
    Check(!warnings.empty(), "and says why");

    warnings.clear();
    Check(ReadText("Frame Root { Mesh { 3; 0;0;0;, 1;0;0;, 0;1;0;;", &warnings) == nullptr,
          "a file that ends inside an object reads as nothing");
    Check(!warnings.empty(), "and reports the unterminated object rather than crashing");
}

// The property most likely to be got wrong in this format, and the one that
// decides whether a model renders inside out.
static void TestHandednessAndWinding() {
    std::printf("Left-handedness and winding\n");

    // The triangle is wound counter-clockwise, so its geometric normal is +Z.
    // A stored normal of +Z under an identity frame agrees: no warning.
    std::vector<std::string> warnings;
    auto agreeing = ReadText(Triangle(kIdentity, "0.0;0.0;1.0"), &warnings);
    Check(agreeing != nullptr, "a triangle whose winding matches its normal parses");
    Check(warnings.empty(), "and is not reported as inside out");

    // The same triangle with the opposite stored normal IS inside out, and
    // there is no reflection anywhere to explain it.
    warnings.clear();
    auto reversed = ReadText(Triangle(kIdentity, "0.0;0.0;-1.0"), &warnings);
    Check(reversed != nullptr, "a triangle wound against its normal still parses");
    bool reported = false;
    for (const std::string& w : warnings)
        if (w.find("inside out") != std::string::npos) reported = true;
    Check(reported, "and is reported as inside out, which is what a left-handed export "
                    "without the matching reflection means");

    // Now the real case: the same reversed winding, but under a frame whose
    // matrix is a reflection - the shape of every right-handed export. The two
    // cancel, so this must NOT be reported.
    warnings.clear();
    const char* mirror = "1.0,0.0,0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,0.0,1.0";
    auto mirrored = ReadText(Triangle(mirror, "0.0;0.0;-1.0"), &warnings);
    Check(mirrored != nullptr, "a reversed winding under a reflecting frame parses");
    bool quiet = true;
    for (const std::string& w : warnings)
        if (w.find("inside out") != std::string::npos) quiet = false;
    Check(quiet, "and is NOT reported: the frame's reflection is what makes it correct");

    // And the reflection itself must survive, or the model comes out mirrored.
    if (mirrored) {
        const Matrix4x4 world = mirrored->GlobalTransform(0);
        const double determinant =
                world.m[0] * (world.m[5] * world.m[10] - world.m[6] * world.m[9]) -
                world.m[4] * (world.m[1] * world.m[10] - world.m[2] * world.m[9]) +
                world.m[8] * (world.m[1] * world.m[6] - world.m[2] * world.m[5]);
        Check(determinant < 0.0, "the frame keeps its negative determinant through the document");
    }
}

// Direct3D writes a matrix row-major for row vectors; the document stores
// column-major for column vectors. Those are the same sixteen numbers, and
// getting that wrong transposes every placement in the file.
static void TestFrameMatrix() {
    std::printf("Frame transforms\n");

    auto placed = ReadText(
            "Frame Root {\n"
            "  FrameTransformMatrix { 2.0,0.0,0.0,0.0, 0.0,2.0,0.0,0.0, 0.0,0.0,2.0,0.0, "
            "3.0,4.0,5.0,1.0;; }\n"
            "  Frame Child {\n"
            "    FrameTransformMatrix { 1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, "
            "1.0,0.0,0.0,1.0;; }\n"
            "    Mesh { 3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;; }\n"
            "  }\n"
            "}\n");
    Check(placed != nullptr, "a nested frame chain parses");
    if (placed) {
        Check(placed->Nodes.size() == 2 && placed->Nodes[1].Parent == 0,
              "the child frame is a child node");
        Check(Near(placed->Nodes[0].Translation.x, 3.0, 1e-9) &&
              Near(placed->Nodes[0].Translation.z, 5.0, 1e-9),
              "elements 12 to 14 are the translation, so the matrix copies straight across");
        Check(Near(placed->Nodes[0].Scale.y, 2.0, 1e-9), "and the scale comes back as scale");
        // The child sits one unit along local X, which the parent's scale of 2
        // turns into two units of world X, plus the parent's own offset.
        const Bounds3D bounds = placed->ComputeBounds();
        Check(Near(bounds.Min.x, 5.0, 1e-6) && Near(bounds.Min.y, 4.0, 1e-6) &&
              Near(bounds.Min.z, 5.0, 1e-6),
              "and the chain composes: the child's origin lands at (5, 4, 5)");
    }
}

static void TestGeometry() {
    std::printf("Geometry\n");

    // A quad and a triangle. N-gons must survive as n-gons.
    auto mixed = ReadText(
            "Mesh {\n"
            "  5;\n"
            "  0;0;0;, 1;0;0;, 1;1;0;, 0;1;0;, 2;0;0;;\n"
            "  2;\n"
            "  4;0,1,2,3;,\n"
            "  3;1,4,2;;\n"
            "}\n");
    Check(mixed != nullptr, "a mixed quad and triangle parses");
    if (mixed) {
        const MeshPrimitive& prim = mixed->Meshes[0].Primitives[0];
        Check(prim.Mode == PrimitiveMode::Polygons, "it uses Polygons mode");
        Check(prim.FaceCount() == 2 && prim.Face(0).size() == 4 && prim.Face(1).size() == 3,
              "the quad stays a quad and the triangle a triangle");
        Check(prim.VertexCount() == 5, "with no second index stream, vertices are shared");
    }

    // Normals indexed apart from positions: two faces sharing a position but
    // creasing across it have to become distinct vertices.
    auto creased = ReadText(
            "Mesh {\n"
            "  4;\n"
            "  0;0;0;, 1;0;0;, 1;1;0;, 0;1;0;;\n"
            "  2;\n"
            "  3;0,1,2;, 3;0,2,3;;\n"
            "  MeshNormals {\n"
            "    2;\n"
            "    0;0;1;, 0;1;0;;\n"
            "    2;\n"
            "    3;0,0,0;, 3;1,1,1;;\n"
            "  }\n"
            "}\n");
    Check(creased != nullptr, "a mesh whose normal indices differ from its vertex indices parses");
    if (creased) {
        const MeshPrimitive& prim = creased->Meshes[0].Primitives[0];
        Check(prim.VertexCount() == 6,
              "the two faces cannot share vertices across the crease, so 4 positions "
              "become 6 vertices");
        Check(prim.Normals.size() == prim.VertexCount(), "each with its own normal");
    }

    // Several materials on one mesh become several primitives, which is how
    // the document expresses a per-face material assignment.
    auto split = ReadText(
            "Mesh {\n"
            "  4;\n"
            "  0;0;0;, 1;0;0;, 1;1;0;, 0;1;0;;\n"
            "  2;\n"
            "  3;0,1,2;, 3;0,2,3;;\n"
            "  MeshMaterialList {\n"
            "    2;\n"
            "    2;\n"
            "    0, 1;;\n"
            "    Material Red { 1.0;0.0;0.0;1.0;; 5.0; 0;0;0;; 0;0;0;; }\n"
            "    Material Blue { 0.0;0.0;1.0;1.0;; 5.0; 0;0;0;; 0;0;0;; }\n"
            "  }\n"
            "}\n");
    Check(split != nullptr, "a two-material mesh parses");
    if (split) {
        Check(split->Materials.size() == 2, "both materials are read");
        const ModelMesh& mesh = split->Meshes[0];
        Check(mesh.Primitives.size() == 2, "and the mesh becomes one primitive per material");
        if (mesh.Primitives.size() == 2) {
            Check(mesh.Primitives[0].Material != mesh.Primitives[1].Material,
                  "each bound to a different one");
            Check(mesh.Primitives[0].FaceCount() == 1 && mesh.Primitives[1].FaceCount() == 1,
                  "with the faces divided between them");
        }
        Check(split->Materials[0].Phong.has_value() &&
              Near(split->Materials[0].Phong->Shininess, 5.0, 1e-6),
              "a Material's `power` is the specular exponent outright, so it is Ns unchanged");
    }

    // Vertex colours are indexed, not a flat array.
    auto coloured = ReadText(
            "Mesh {\n"
            "  3; 0;0;0;, 1;0;0;, 0;1;0;;\n"
            "  1; 3;0,1,2;;\n"
            "  MeshVertexColors {\n"
            "    2;\n"
            "    0; 1.0;0.0;0.0;1.0;;,\n"
            "    2; 0.0;1.0;0.0;1.0;;;\n"
            "  }\n"
            "}\n");
    Check(coloured != nullptr, "indexed vertex colours parse");
    if (coloured) {
        const MeshPrimitive& prim = coloured->Meshes[0].Primitives[0];
        const VertexAttribute* colors = prim.FindAttribute(AttributeSemantic::Color, 0);
        Check(colors && colors->Components == 4 && colors->Count() == 3,
              "as a four-component colour per vertex");
        if (colors)
            Check(Near(colors->Values[0], 1.0, 1e-6) && Near(colors->Values[9], 1.0, 1e-6),
                  "with each colour landing on the vertex its index names");
    }
}

static void TestSyntax() {
    std::printf("Container syntax\n");

    // `template` declares a schema. Every reader ignores it in favour of the
    // known layouts, so it must be skipped whole - brackets, GUID and all.
    auto templated = ReadText(
            "template Mesh {\n"
            " <3d82ab44-62da-11cf-ab39-0020af71e433>\n"
            " DWORD nVertices;\n"
            " array Vector vertices[nVertices];\n"
            " DWORD nFaces;\n"
            " array MeshFace faces[nFaces];\n"
            " [...]\n"
            "}\n"
            "Mesh { 3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;; }\n");
    Check(templated && templated->Meshes.size() == 1,
          "a template block is skipped and the object after it still reads");

    // Comments in both spellings, and a GUID after the opening brace.
    auto commented = ReadText(
            "# a hash comment\n"
            "Mesh { // a slash comment\n"
            "  <3d82ab44-62da-11cf-ab39-0020af71e433>\n"
            "  3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;;\n"
            "}\n");
    Check(commented && commented->Meshes.size() == 1,
          "both comment spellings and an inline GUID are stepped over");

    // A material defined at the top of the file and referenced by name from
    // inside a mesh - the format's own instancing.
    auto referenced = ReadText(
            "Material Shared { 0.25;0.5;0.75;1.0;; 9.0; 1;1;1;; 0;0;0;;\n"
            "  TextureFilename { \"hull.png\"; }\n"
            "}\n"
            "Mesh {\n"
            "  3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;;\n"
            "  MeshMaterialList { 1; 1; 0;; { Shared } }\n"
            "}\n");
    Check(referenced != nullptr, "a mesh referencing a material by name parses");
    if (referenced) {
        Check(referenced->Materials.size() == 1, "the material is read once, not twice");
        Check(referenced->Meshes[0].Primitives[0].Material == 0,
              "and the reference resolves to it");
        Check(referenced->Images.size() == 1 && referenced->Images[0].Uri == "hull.png",
              "its TextureFilename becomes an image");
        Check(referenced->Materials[0].BaseColorTexture.IsSet(),
              "reaching the material's base colour slot");
    }

    std::vector<std::string> warnings;
    auto dangling = ReadText(
            "Mesh {\n"
            "  3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;;\n"
            "  MeshMaterialList { 1; 1; 0;; { Missing } }\n"
            "}\n", &warnings);
    Check(dangling != nullptr && !warnings.empty(),
          "a reference to a name the file never defines is reported, and the mesh still reads");

    // A mesh that claims more vertices than it carries must not be trusted
    // about the size - that is the one malformation that would allocate wildly.
    warnings.clear();
    auto lying = ReadText("Mesh { 100000; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;; }\n", &warnings);
    Check(lying == nullptr && !warnings.empty(),
          "a mesh declaring more vertices than it holds is refused, not allocated");
}

// The binary encoding, built by hand. Same scene as the text one, so the two
// must produce the same document.
static void TestBinaryEncoding() {
    std::printf("Binary encoding\n");

    std::vector<uint8_t> data = Bytes("xof 0303bin 0032");
    binary::PutName(data, "Frame");
    binary::PutName(data, "Root");
    binary::PutU16(data, binary::OBrace);
    binary::PutName(data, "FrameTransformMatrix");
    binary::PutU16(data, binary::OBrace);
    binary::PutFloats(data, {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  3, 4, 5, 1});
    binary::PutU16(data, binary::CBrace);
    binary::PutName(data, "Mesh");
    binary::PutU16(data, binary::OBrace);
    binary::PutIntegers(data, {3});
    binary::PutFloats(data, {0, 0, 0,  1, 0, 0,  0, 1, 0});
    binary::PutIntegers(data, {1, 3, 0, 1, 2});
    binary::PutName(data, "MeshMaterialList");
    binary::PutU16(data, binary::OBrace);
    binary::PutIntegers(data, {1, 1, 0});
    binary::PutName(data, "Material");
    binary::PutName(data, "Skin");
    binary::PutU16(data, binary::OBrace);
    binary::PutFloats(data, {0.25f, 0.5f, 0.75f, 1.0f});
    binary::PutFloats(data, {9.0f});
    binary::PutFloats(data, {1, 1, 1});
    binary::PutFloats(data, {0, 0, 0});
    binary::PutName(data, "TextureFilename");
    binary::PutU16(data, binary::OBrace);
    binary::PutString(data, "hull.png");
    binary::PutU16(data, binary::CBrace);
    binary::PutU16(data, binary::CBrace);   // Material
    binary::PutU16(data, binary::CBrace);   // MeshMaterialList
    binary::PutU16(data, binary::CBrace);   // Mesh
    binary::PutU16(data, binary::CBrace);   // Frame

    std::vector<std::string> warnings;
    auto document = Read(data, &warnings);
    Check(document != nullptr, "a hand-built binary .x parses");
    if (!document) return;
    Check(warnings.empty(), "without a warning");
    Check(document->Metadata["x.encoding"] == std::string("binary"),
          "and reports the encoding it was read from");

    Check(document->Meshes.size() == 1 && document->Nodes.size() == 1,
          "one mesh under one frame");
    const MeshPrimitive& prim = document->Meshes[0].Primitives[0];
    Check(prim.VertexCount() == 3 && prim.FaceCount() == 1, "three vertices in one triangle");
    Check(Near(document->Nodes[0].Translation.x, 3.0, 1e-6) &&
          Near(document->Nodes[0].Translation.z, 5.0, 1e-6),
          "its FLOAT_LIST matrix lands where the text one does");
    Check(document->Materials.size() == 1 &&
          Near(document->Materials[0].BaseColorFactor.z, 0.75, 1e-6),
          "the material's floats survive the binary round");
    Check(document->Images.size() == 1 && document->Images[0].Uri == "hull.png",
          "and a binary STRING token becomes the texture name");

    // The same scene in text must give the same answer. This is the assertion
    // that keeps the two encodings from drifting apart.
    auto text = ReadText(
            "Frame Root {\n"
            "  FrameTransformMatrix { 1,0,0,0, 0,1,0,0, 0,0,1,0, 3,4,5,1;; }\n"
            "  Mesh {\n"
            "    3; 0;0;0;, 1;0;0;, 0;1;0;; 1; 3;0,1,2;;\n"
            "    MeshMaterialList { 1; 1; 0;;\n"
            "      Material Skin { 0.25;0.5;0.75;1.0;; 9.0; 1;1;1;; 0;0;0;;\n"
            "        TextureFilename { \"hull.png\"; }\n"
            "      }\n"
            "    }\n"
            "  }\n"
            "}\n");
    Check(text != nullptr, "and the same scene parses as text");
    if (text) {
        const Bounds3D fromBinary = document->ComputeBounds();
        const Bounds3D fromText = text->ComputeBounds();
        Check(Near(fromBinary.Min.x, fromText.Min.x, 1e-6) &&
              Near(fromBinary.Max.y, fromText.Max.y, 1e-6) &&
              text->TotalVertexCount() == document->TotalVertexCount(),
              "with identical geometry - the two encodings are one grammar");
    }

    // A binary list whose count exceeds the bytes left is corrupt, and must be
    // refused rather than believed.
    std::vector<uint8_t> corrupt = Bytes("xof 0303bin 0032");
    binary::PutName(corrupt, "Mesh");
    binary::PutU16(corrupt, binary::OBrace);
    binary::PutU16(corrupt, binary::FloatList);
    binary::PutU32(corrupt, 1000000000u);
    warnings.clear();
    Check(Read(corrupt, &warnings) == nullptr && !warnings.empty(),
          "a binary list longer than the file is refused, not allocated");
}

static void TestSample(const char* path) {
    std::printf("Sample: %s\n", path);
    XFileConverter converter;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    auto doc = converter.Import(path, options);
    if (!doc) { std::printf("  [FAIL] import returned nothing\n"); ++failures; return; }

    // The format carries no title, so it comes from the path; derive the
    // expectation the same way or the assertion breaks on any copy.
    Check(doc->Title == std::filesystem::path(path).stem().string(),
          "the file name titles the document");
    Check(doc->SourceFormat == "x" && doc->Metadata["x.version"] == "3.3" &&
          doc->Metadata["x.encoding"] == "text",
          "read as a version 3.3 text .x");
    Check(warnings.empty(), "and read without a single warning");
    Check(doc->SourceUnit == ModelUnit::Unspecified,
          "the format states no unit, and none is invented");

    Check(doc->Nodes.size() == 4 && doc->Meshes.size() == 2,
          "two meshes under the Root / Armature / Cube frame chain");
    Check(doc->Nodes[0].Name == "Root" && doc->Nodes[1].Name == "Armature" &&
          doc->Nodes[2].Parent == 1 && doc->Nodes[3].Parent == 1,
          "with the frame names and nesting the file wrote");
    Check(doc->TotalVertexCount() == 4055 && doc->TotalFaceCount() == 1030,
          "4055 vertices in 1030 faces");

    // 872 quads and 65 triangles in the hull, 93 quads in the canopy: 1995
    // triangles, which is exactly what the COLLADA export of this scene holds.
    size_t triangles = 0;
    for (const ModelMesh& mesh : doc->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            for (size_t f = 0; f < prim.FaceCount(); ++f)
                triangles += prim.Face(f).size() - 2;
    Check(triangles == 1995,
          "1995 triangles once fanned - the same count the .dae export of this scene has");

    Check(doc->Materials.size() == 2 && doc->Images.size() == 2,
          "both materials and both textures are read");
    bool transparent = false;
    for (const ModelMaterial& material : doc->Materials)
        if (material.BaseColorFactor.w < 0.9f && material.Alpha == AlphaMode::Blend)
            transparent = true;
    Check(transparent, "the canopy glass is read as transparent");

    // The root frame is the left-handed conversion: a reflection, held as a
    // negative scale, which is what makes the winding correct in world space.
    Check(doc->Nodes[0].Scale.x * doc->Nodes[0].Scale.y * doc->Nodes[0].Scale.z < 0.0,
          "the root frame's reflection survives as a negative scale");

    // World bounds, checked against an independent walk of the same frame
    // chain. This is the assertion that the straight matrix copy is right.
    const Bounds3D bounds = doc->ComputeBounds();
    Check(Near(bounds.Min.x, -0.9732, 1e-3) && Near(bounds.Max.x, 0.0, 1e-3) &&
          Near(bounds.Min.y, -2.5754, 1e-3) && Near(bounds.Max.y, 1.6357, 1e-3) &&
          Near(bounds.Min.z, -5.1948, 1e-3) && Near(bounds.Max.z, 0.9626, 1e-3),
          "world bounds match an independent walk of the frame chain");

    // Like the .dae, .abc and .blend - and unlike the OBJ, 3DS, DXF, PLY and
    // X3D - this export was written without applying the mirror modifier.
    // Asserting it stops a later change "fixing" the reader to match.
    Check(bounds.Max.x <= 0.0001,
          "the .x holds only half the hull in X - its mirror modifier was not applied");
}

int main(int argc, char** argv) {
    TestValidation();
    TestHandednessAndWinding();
    TestFrameMatrix();
    TestGeometry();
    TestSyntax();
    TestBinaryEncoding();
    if (argc > 1) TestSample(argv[1]);
    else std::printf("Sample: skipped (pass an .x path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
