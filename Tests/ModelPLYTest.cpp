// Tests/ModelPLYTest.cpp
// The PLY reader and writer.
//
// PLY has no fixed schema - a file declares its own elements and properties -
// so most of this suite is written against small headers constructed inline,
// where the awkward cases live: the two spellings of every type name, the
// three spellings of a texture coordinate, colours as bytes or as floats,
// binary in either byte order, an element nothing understands that still has
// to be stepped over exactly, and a file that stops early.
//
// The sample in media/models/PLY is the E-45 aircraft again, and it is a
// different export from the others: 32 440 quads against the OBJ's 8 110, from
// 'E 45 Aircraft_Export_Ready.blend' rather than the plain one. It is only the
// second export in the set (with the OBJ, 3DS and DXF) whose mirror modifier
// was applied, which the cross-format check at the end pins.
//
// argv[1] is media/models. Without it only the inline cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/PLY/UltraCanvasPLYConverter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
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
template <typename T>
static void Append(std::vector<uint8_t>& out, T value, bool bigEndian = false) {
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &value, sizeof(T));
    const uint16_t probe = 1;
    const bool hostLittle = *reinterpret_cast<const uint8_t*>(&probe) == 1;
    if (bigEndian == hostLittle)
        for (size_t i = 0; i < sizeof(T) / 2; ++i) std::swap(raw[i], raw[sizeof(T) - 1 - i]);
    out.insert(out.end(), raw, raw + sizeof(T));
}

// ===== HEADER =====

static void TestSignature() {
    std::printf("Signature\n");
    PLYConverter converter;
    Check(!converter.ValidateData({}), "empty data is rejected");
    Check(!converter.ValidateData(Bytes("solid teapot\n")), "an ASCII STL is rejected");
    Check(!converter.ValidateData(Bytes("plyx\n")), "'plyx' is not 'ply'");
    Check(converter.ValidateData(Bytes("ply\nformat ascii 1.0\n")), "'ply' then a break is");
    Check(converter.ValidateData(Bytes("ply\r\n")), "with a CRLF line ending too");

    ConversionOptions quiet;
    int warnings = 0;
    quiet.WarningCallback = [&warnings](const std::string&) { ++warnings; };
    Check(converter.ImportFromMemory(Bytes("ply\nformat ascii 1.0\n"), quiet) == nullptr,
          "a header with no end_header is refused");
    Check(warnings > 0, "and says so");
    Check(converter.ImportFromMemory(
                  Bytes("ply\nformat ebcdic 1.0\nend_header\n"), quiet) == nullptr,
          "an encoding that does not exist is refused rather than guessed at");
}

static void TestAsciiBasics() {
    std::printf("An ASCII mesh\n");
    const std::string text =
            "ply\n"
            "format ascii 1.0\n"
            "comment made by hand\n"
            "element vertex 4\n"
            "property float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\n"
            "property uchar red\nproperty uchar green\nproperty uchar blue\n"
            "element face 1\n"
            "property list uchar int vertex_indices\n"
            "end_header\n"
            "0 0 0  0 0 1  255 0 0\n"
            "1 0 0  0 0 1  0 255 0\n"
            "1 1 0  0 0 1  0 0 255\n"
            "0 1 0  0 0 1  255 255 255\n"
            "4 0 1 2 3\n";

    PLYConverter converter;
    ConversionOptions options;
    auto document = converter.ImportFromMemory(Bytes(text), options);
    Check(document != nullptr, "it reads");
    if (!document) return;

    const MeshPrimitive& prim = document->Meshes[0].Primitives[0];
    Check(prim.Positions.size() == 4, "four vertices");
    Check(prim.FaceCount() == 1, "one face");
    Check(prim.Mode == PrimitiveMode::Polygons, "kept as a quad rather than split");
    Check(prim.Face(0).size() == 4, "with four corners");
    Check(prim.Normals.size() == 4, "normals are read");

    const VertexAttribute* color = prim.FindAttribute(AttributeSemantic::Color, 0);
    Check(color != nullptr && color->Count() == 4, "colours are read");
    // A uchar colour is 0..255 and has to be scaled, or every model is white.
    if (color && color->Values.size() >= 4)
        CheckNear(color->Values[0], 1.0f, 1e-6, "and a uchar 255 becomes 1.0, not 255");
    Check(document->Metadata.count("comment") == 1, "the comment survives");
    Check(document->Metadata["encoding"] == "ascii", "and the encoding is recorded");
}

static void TestPropertySpellings() {
    std::printf("The spellings PLY allows\n");
    PLYConverter converter;
    ConversionOptions options;

    // int8/uint8/float32 are the same types as char/uchar/float.
    const std::string sized =
            "ply\nformat ascii 1.0\n"
            "element vertex 3\n"
            "property float32 x\nproperty float32 y\nproperty float32 z\n"
            "property uint8 red\nproperty uint8 green\nproperty uint8 blue\n"
            "element face 1\nproperty list uint8 int32 vertex_indices\n"
            "end_header\n"
            "0 0 0 255 0 0\n1 0 0 0 255 0\n0 1 0 0 0 255\n3 0 1 2\n";
    auto a = converter.ImportFromMemory(Bytes(sized), options);
    Check(a && a->TotalFaceCount() == 1, "the sized type names (float32, uint8) are understood");

    // s/t, u/v and texture_u/texture_v all mean a texture coordinate.
    for (const char* pair : {"s\nproperty float t", "u\nproperty float v",
                             "texture_u\nproperty float texture_v"}) {
        const std::string text =
                "ply\nformat ascii 1.0\nelement vertex 3\n"
                "property float x\nproperty float y\nproperty float z\n"
                "property float " + std::string(pair) + "\n"
                "element face 1\nproperty list uchar int vertex_indices\n"
                "end_header\n"
                "0 0 0 0 0\n1 0 0 1 0\n0 1 0 0 1\n3 0 1 2\n";
        auto document = converter.ImportFromMemory(Bytes(text), options);
        const VertexAttribute* uv =
                document && !document->Meshes.empty()
                        ? document->Meshes[0].Primitives[0].FindAttribute(
                                  AttributeSemantic::TexCoord, 0)
                        : nullptr;
        Check(uv != nullptr && uv->Count() == 3,
              std::string("'") + std::string(pair).substr(0, 1) + "' is a texture coordinate");
    }

    // Older writers spell the face list vertex_index, singular.
    const std::string singular =
            "ply\nformat ascii 1.0\nelement vertex 3\n"
            "property float x\nproperty float y\nproperty float z\n"
            "element face 1\nproperty list uchar int vertex_index\n"
            "end_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 2\n";
    auto b = converter.ImportFromMemory(Bytes(singular), options);
    Check(b && b->TotalFaceCount() == 1, "'vertex_index' is the same list as 'vertex_indices'");
}

// The reason PLY earns its place: a per-vertex property nothing else has a
// field for survives as a named attribute rather than being dropped.
static void TestCustomProperties() {
    std::printf("Properties nothing has a field for\n");
    const std::string text =
            "ply\nformat ascii 1.0\n"
            "element vertex 3\n"
            "property float x\nproperty float y\nproperty float z\n"
            "property float quality\n"
            "property uchar classification\n"
            "element face 1\nproperty list uchar int vertex_indices\n"
            "end_header\n"
            "0 0 0 0.25 7\n1 0 0 0.50 7\n0 1 0 0.75 9\n3 0 1 2\n";

    PLYConverter converter;
    ConversionOptions options;
    auto document = converter.ImportFromMemory(Bytes(text), options);
    Check(document != nullptr, "a file with unknown properties still reads");
    if (!document) return;

    const MeshPrimitive& prim = document->Meshes[0].Primitives[0];
    const VertexAttribute* quality = prim.FindAttribute("quality");
    const VertexAttribute* classification = prim.FindAttribute("classification");
    Check(quality != nullptr, "'quality' is kept under its own name");
    Check(classification != nullptr, "and so is 'classification'");
    if (quality) {
        Check(quality->Semantic == AttributeSemantic::Custom, "as a Custom attribute");
        Check(quality->Count() == 3 && std::fabs(quality->Values[1] - 0.5f) < 1e-6f,
              "with its values intact");
    }
    Check(prim.FindAttribute("x") == nullptr,
          "and a property that does have a field is not duplicated into one");
}

static void TestBinary() {
    std::printf("Binary, in both byte orders\n");
    PLYConverter converter;
    ConversionOptions options;

    for (int pass = 0; pass < 2; ++pass) {
        const bool big = pass == 1;
        std::string header = "ply\nformat ";
        header += big ? "binary_big_endian" : "binary_little_endian";
        header += " 1.0\n"
                  "element vertex 3\n"
                  "property float x\nproperty float y\nproperty float z\n"
                  "element face 1\n"
                  "property list uchar int vertex_indices\n"
                  "end_header\n";
        std::vector<uint8_t> data = Bytes(header);
        const float points[3][3] = {{0, 0, 0}, {2, 0, 0}, {0, 3, 0}};
        for (const auto& point : points)
            for (int k = 0; k < 3; ++k) Append<float>(data, point[k], big);
        data.push_back(3);
        for (int32_t index : {0, 1, 2}) Append<int32_t>(data, index, big);

        auto document = converter.ImportFromMemory(data, options);
        const char* label = big ? "big-endian" : "little-endian";
        Check(document != nullptr, std::string("a ") + label + " binary file reads");
        if (!document) continue;
        const Bounds3D bounds = document->ComputeBounds();
        // Read with the wrong byte order these come out as denormals or
        // enormous numbers, so the extents are the assertion that matters.
        CheckNear(bounds.Size().x, 2.0, 1e-6, std::string("with ") + label + " X intact");
        CheckNear(bounds.Size().y, 3.0, 1e-6, "and Y");
        Check(document->TotalFaceCount() == 1, "and its face");
    }
}

static void TestSkippingAndTruncation() {
    std::printf("Elements nothing understands, and files that stop early\n");
    PLYConverter converter;
    ConversionOptions options;
    std::vector<std::string> warnings;
    options.WarningCallback = [&warnings](const std::string& m) { warnings.push_back(m); };

    // An edge element between the vertices and the faces. Its bytes must be
    // consumed exactly: in a binary file a mis-sized skip does not lose one
    // element, it destroys everything after it.
    std::string header =
            "ply\nformat binary_little_endian 1.0\n"
            "element vertex 3\n"
            "property float x\nproperty float y\nproperty float z\n"
            "element edge 2\n"
            "property int vertex1\nproperty int vertex2\nproperty uchar red\n"
            "element face 1\n"
            "property list uchar int vertex_indices\n"
            "end_header\n";
    std::vector<uint8_t> data = Bytes(header);
    const float points[3][3] = {{0, 0, 0}, {5, 0, 0}, {0, 7, 0}};
    for (const auto& point : points)
        for (int k = 0; k < 3; ++k) Append<float>(data, point[k]);
    for (int e = 0; e < 2; ++e) {
        Append<int32_t>(data, 0);
        Append<int32_t>(data, 1);
        data.push_back(200);
    }
    data.push_back(3);
    for (int32_t index : {0, 1, 2}) Append<int32_t>(data, index);

    auto document = converter.ImportFromMemory(data, options);
    Check(document != nullptr, "a file with an element in the middle still reads");
    if (document) {
        Check(document->TotalFaceCount() == 1,
              "and its face survives, so the edge element was stepped over by exactly its size");
        CheckNear(document->ComputeBounds().Size().y, 7.0, 1e-6, "with the geometry intact");
    }
    bool mentionedEdge = false;
    for (const std::string& warning : warnings)
        if (warning.find("edge") != std::string::npos) mentionedEdge = true;
    Check(mentionedEdge, "and the element it skipped is named rather than passed over in silence");

    // A header promising more than the file holds.
    warnings.clear();
    const std::string truncated =
            "ply\nformat ascii 1.0\nelement vertex 5\n"
            "property float x\nproperty float y\nproperty float z\n"
            "element face 1\nproperty list uchar int vertex_indices\n"
            "end_header\n0 0 0\n1 0 0\n";
    auto partial = converter.ImportFromMemory(Bytes(truncated), options);
    Check(partial != nullptr, "a truncated file yields what it did contain");
    Check(!warnings.empty(), "and says that it was short");
}

static void TestPointCloud() {
    std::printf("A point cloud\n");
    const std::string text =
            "ply\nformat ascii 1.0\n"
            "element vertex 4\n"
            "property float x\nproperty float y\nproperty float z\n"
            "property float intensity\n"
            "end_header\n"
            "0 0 0 0.1\n1 0 0 0.2\n0 1 0 0.3\n0 0 1 0.4\n";

    PLYConverter converter;
    ConversionOptions options;
    auto document = converter.ImportFromMemory(Bytes(text), options);
    Check(document != nullptr, "a file with vertices and no faces reads");
    if (!document) return;
    const MeshPrimitive& prim = document->Meshes[0].Primitives[0];
    Check(prim.Mode == PrimitiveMode::Points,
          "as a point cloud rather than an empty mesh");
    Check(prim.Positions.size() == 4, "with every point");
    Check(prim.FindAttribute("intensity") != nullptr, "and its per-point property");
    Check(prim.Normals.empty(),
          "and no normals are invented for points, which have no facing");
}

static void TestCapabilities() {
    std::printf("Capabilities\n");
    PLYConverter converter;
    const FormatCapabilities capabilities = converter.GetCapabilities();
    Check(converter.CanImport() && converter.CanExport(), "PLY reads and writes");
    Check(capabilities.Meshes && capabilities.NGons && capabilities.PointClouds,
          "claims meshes, n-gons and point clouds");
    Check(capabilities.VertexColors && capabilities.CustomAttributes,
          "and the two things it is chosen for: vertex colours and arbitrary properties");
    Check(!capabilities.SceneGraph && !capabilities.Materials && !capabilities.Units,
          "and claims none of what PLY has no room for");
}

// ===== THE SAMPLE =====

static void TestSample(const std::string& mediaRoot) {
    std::printf("The E-45 aircraft as PLY\n");
    const std::string path = (std::filesystem::path(mediaRoot) / "PLY/E-45-Aircraft.ply").string();

    PLYConverter converter;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& m) { warnings.push_back(m); };

    Check(converter.ValidateFile(path), "the file validates as PLY");
    auto document = converter.Import(path, options);
    Check(document != nullptr, "and reads");
    if (!document) return;
    for (const std::string& warning : warnings) std::printf("      warn: %s\n", warning.c_str());
    Check(warnings.empty(), "with nothing to report");

    const MeshPrimitive& prim = document->Meshes[0].Primitives[0];
    Check(prim.Positions.size() == 40333, "40333 vertices");
    Check(prim.FaceCount() == 32440, "32440 faces");
    size_t quads = 0;
    for (size_t f = 0; f < prim.FaceCount(); ++f) if (prim.Face(f).size() == 4) ++quads;
    Check(quads == 32440, "every one of them a quad, kept rather than triangulated");
    Check(prim.Normals.size() == prim.Positions.size(), "a normal per vertex, from the file");
    Check(prim.FindAttribute(AttributeSemantic::TexCoord, 0) != nullptr, "texture coordinates");
    Check(prim.FindAttribute(AttributeSemantic::Color, 0) != nullptr, "and vertex colours");
    Check(document->Metadata["comment"].find("Blender") != std::string::npos,
          "the comment names the writer");

    // The file stores its own normals, so whether each face's winding agrees
    // with them is an independent check that no reversal is needed here -
    // unlike Alembic, where 1680 of 1681 faces disagree unreversed.
    size_t agree = 0, disagree = 0;
    double volume = 0.0;
    for (size_t f = 0; f < prim.FaceCount(); ++f) {
        const std::vector<uint32_t> face = prim.Face(f);
        std::vector<Vec3d> points;
        for (uint32_t corner : face) points.push_back(prim.Positions[corner]);
        const Vec3d computed = NewellNormal(points);
        const Vec3f& stored = prim.Normals[face[0]];
        const double dot = computed.x * stored.x + computed.y * stored.y + computed.z * stored.z;
        if (dot > 0.0) ++agree; else if (dot < 0.0) ++disagree;
        for (size_t k = 1; k + 1 < points.size(); ++k)
            volume += points[0].Dot(points[k].Cross(points[k + 1])) / 6.0;
    }
    std::printf("      %zu faces agree with the file's own normals, %zu disagree\n",
                agree, disagree);
    Check(agree > disagree * 100, "PLY winds its faces outward already, so none are reversed");
    Check(volume > 0.0, "and the signed volume is positive");
}

static void TestRoundTrip(const std::string& mediaRoot) {
    std::printf("Writing PLY back out\n");
    const std::string path = (std::filesystem::path(mediaRoot) / "PLY/E-45-Aircraft.ply").string();
    PLYConverter converter;
    ConversionOptions quiet;
    auto original = converter.Import(path, quiet);
    if (!original) { Check(false, "the sample reads"); return; }
    const Bounds3D before = original->ComputeBounds();

    for (int pass = 0; pass < 2; ++pass) {
        const bool binary = pass == 1;
        ConversionOptions options;
        options.PreferBinary = binary;
        options.Precision = NumericPrecision::Full;
        const char* label = binary ? "binary" : "ascii";

        std::vector<uint8_t> written;
        Check(converter.ExportToMemory(*original, written, options),
              std::string(label) + " writes");
        Check(converter.ValidateData(written), "and is recognisably PLY");

        auto again = converter.ImportFromMemory(written, options);
        Check(again != nullptr, "and reads back");
        if (!again) continue;
        Check(again->TotalVertexCount() == original->TotalVertexCount(), "every vertex");
        Check(again->TotalFaceCount() == original->TotalFaceCount(), "every face");
        const MeshPrimitive& prim = again->Meshes[0].Primitives[0];
        Check(prim.FindAttribute(AttributeSemantic::TexCoord, 0) != nullptr,
              "the texture coordinates survive");
        Check(prim.FindAttribute(AttributeSemantic::Color, 0) != nullptr, "and the colours");

        // At Full the positions go out as `property double`, so the round trip
        // is exact rather than merely close - which is the whole reason the
        // flag chooses a type rather than a digit count.
        Check(std::string(written.begin(),
                          written.begin() + std::min<size_t>(written.size(), 400))
                      .find("property double x") != std::string::npos,
              std::string(label) + " writes double positions at Full precision");
        const Bounds3D after = again->ComputeBounds();
        CheckNear(after.Size().x, before.Size().x, 1e-9,
                  std::string(label) + " round-trips the extents exactly at full precision");
        CheckNear(after.Size().y, before.Size().y, 1e-9, "in Y");
        CheckNear(after.Size().z, before.Size().z, 1e-9, "and Z");
        std::printf("      %s: %zu bytes\n", label, written.size());
    }

    // Compact writes float positions: smaller, and lossy against a document
    // whose coordinates are double. That trade has to be visible in both
    // encodings - in binary especially, where a digit count would mean nothing
    // and the flag would otherwise be silently ignored.
    for (int pass = 0; pass < 2; ++pass) {
        const bool binary = pass == 1;
        const char* label = binary ? "binary" : "ascii";
        ConversionOptions compact;
        compact.PreferBinary = binary;
        compact.Precision = NumericPrecision::Compact;
        ConversionOptions exact;
        exact.PreferBinary = binary;
        exact.Precision = NumericPrecision::Full;

        std::vector<uint8_t> small, full;
        converter.ExportToMemory(*original, small, compact);
        converter.ExportToMemory(*original, full, exact);
        Check(small.size() < full.size(),
              std::string(label) + ": Compact is smaller than Full, as it is meant to be");
        Check(std::string(small.begin(), small.begin() + std::min<size_t>(small.size(), 400))
                      .find("property float x") != std::string::npos,
              std::string(label) + ": and writes float positions");

        auto back = converter.ImportFromMemory(small, compact);
        Check(back != nullptr && back->TotalVertexCount() == original->TotalVertexCount(),
              std::string(label) + ": which still reads back whole");
        std::printf("      %s: compact %zu bytes vs full %zu\n", label, small.size(), full.size());
    }
}

// The point of keeping this file in the repository: it is a different export
// of the aircraft from the other five, and the differences are the interesting
// part rather than an inconvenience.
static void TestAgainstTheOtherExports(const std::string& mediaRoot) {
    std::printf("Against the other exports of the same aircraft\n");
    const std::string path = (std::filesystem::path(mediaRoot) / "PLY/E-45-Aircraft.ply").string();
    PLYConverter converter;
    ConversionOptions quiet;
    auto ply = converter.Import(path, quiet);
    if (!ply) { Check(false, "the PLY reads"); return; }

    const Bounds3D bounds = ply->ComputeBounds();
    std::printf("      PLY X [%.4f, %.4f]  Y [%.4f, %.4f]  Z [%.4f, %.4f]\n",
                bounds.Min.x, bounds.Max.x, bounds.Min.y, bounds.Max.y,
                bounds.Min.z, bounds.Max.z);

    // Symmetric about X: this export had its mirror modifier applied, unlike
    // the .dae, the .blend and the .abc, which all hold half a hull.
    Check(std::fabs(bounds.Min.x + bounds.Max.x) < bounds.Size().x * 0.02,
          "the PLY is symmetric about X, so its mirror modifier was applied");

    // The 3DS reader's own suite pins that file's world bounds; the PLY is a
    // denser export of the same aircraft and must agree with them closely
    // without being identical - it is not the same mesh.
    CheckNear(bounds.Min.x, -0.9732, 1e-3, "and it agrees with the 3DS export's X extent");
    CheckNear(bounds.Max.x, 0.9732, 1e-3, "on both sides");
    CheckNear(bounds.Size().y, 6.1406, 0.05, "its length within a percent of the 3DS");
    CheckNear(bounds.Size().z, 4.1947, 0.05, "and its height");

    // But it is four times the OBJ's face count: a different, finer export.
    Check(ply->TotalFaceCount() == 32440 && ply->TotalFaceCount() == 4 * 8110,
          "while carrying exactly four times the OBJ export's 8110 quads");
}

int main(int argc, char** argv) {
    TestSignature();
    TestAsciiBasics();
    TestPropertySpellings();
    TestCustomProperties();
    TestBinary();
    TestSkippingAndTruncation();
    TestPointCloud();
    TestCapabilities();
    if (argc > 1) {
        TestSample(argv[1]);
        TestRoundTrip(argv[1]);
        TestAgainstTheOtherExports(argv[1]);
    } else {
        std::printf("Sample: skipped (pass the media/models path to run it)\n");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
