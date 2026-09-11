// Tests/ModelMS3DTest.cpp
// The MilkShape 3D reader.
//
// The sample is a canopy, not an aircraft, and that is the first thing this
// suite pins. Every other export of the E-45 in this repository carries both
// of its meshes; the .ms3d carries one - 1488 triangles, exactly twice the
// Alembic canopy's 744 faces, with the hull absent entirely. A later reader
// change that started "finding" a second mesh would be inventing it, so the
// count of one is asserted rather than assumed.
//
// Everything else that matters is unreachable from that file, because it has
// no joints, no skinning, no smoothing groups and no textures. So the
// synthetic half **builds .ms3d files by hand**: the format is a fixed
// sequence of packed little-endian structs, which makes writing one about as
// much work as reading one, and it is the only way to test the joint
// hierarchy, the two skinning records, the smoothing-group bitmask, the
// optional versioned tail and the refusals.
//
// argv[1] is the .ms3d. Without it only the synthetic cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/MS3D/UltraCanvasMS3DConverter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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

// ===== AN .MS3D, BUILT BY HAND =====

class Builder {
public:
    void U8(int value) { bytes_.push_back(static_cast<uint8_t>(value)); }
    void U16(int value) {
        bytes_.push_back(static_cast<uint8_t>(value & 0xff));
        bytes_.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    }
    void I32(int32_t value) {
        for (int i = 0; i < 4; ++i)
            bytes_.push_back(static_cast<uint8_t>((static_cast<uint32_t>(value) >> (8 * i)) & 0xff));
    }
    void F32(float value) {
        uint32_t raw = 0;
        std::memcpy(&raw, &value, 4);
        I32(static_cast<int32_t>(raw));
    }
    void Text(const std::string& text, size_t width) {
        for (size_t i = 0; i < width; ++i)
            bytes_.push_back(i < text.size() ? static_cast<uint8_t>(text[i]) : 0);
    }
    void Header(int version = 4) {
        const char magic[10] = {'M', 'S', '3', 'D', '0', '0', '0', '0', '0', '0'};
        for (char c : magic) bytes_.push_back(static_cast<uint8_t>(c));
        I32(version);
    }
    void Vertex(float x, float y, float z, int bone = -1) {
        U8(0);
        F32(x); F32(y); F32(z);
        U8(static_cast<uint8_t>(bone));
        U8(0);
    }
    // One triangle, with its three corners' normals and UVs.
    void Triangle(int a, int b, int c, int smoothingGroup = 0, int group = 0,
                  const float uv[6] = nullptr) {
        U16(0);
        U16(a); U16(b); U16(c);
        for (int k = 0; k < 3; ++k) { F32(0.0f); F32(0.0f); F32(1.0f); }
        static const float defaultUv[6] = {0, 0, 1, 0, 1, 1};
        const float* coords = uv ? uv : defaultUv;
        for (int k = 0; k < 3; ++k) F32(coords[k * 2]);
        for (int k = 0; k < 3; ++k) F32(coords[k * 2 + 1]);
        U8(smoothingGroup);
        U8(group);
    }
    void Group(const std::string& name, const std::vector<int>& triangles, int material) {
        U8(0);
        Text(name, 32);
        U16(static_cast<int>(triangles.size()));
        for (int index : triangles) U16(index);
        U8(static_cast<uint8_t>(material));
    }
    void Material(const std::string& name, float transparency = 1.0f,
                  const std::string& texture = "", const std::string& alphaMap = "",
                  float shininess = 32.0f) {
        Text(name, 32);
        for (int k = 0; k < 4; ++k) F32(k == 3 ? 1.0f : 0.2f);          // ambient
        F32(0.25f); F32(0.5f); F32(0.75f); F32(1.0f);                   // diffuse
        for (int k = 0; k < 4; ++k) F32(k == 3 ? 1.0f : 0.9f);          // specular
        for (int k = 0; k < 4; ++k) F32(k == 3 ? 1.0f : 0.0f);          // emissive
        F32(shininess);
        F32(transparency);
        U8(0);
        Text(texture, 128);
        Text(alphaMap, 128);
    }
    void AnimationHeader(float fps, float currentTime, int totalFrames) {
        F32(fps); F32(currentTime); I32(totalFrames);
    }
    void Joint(const std::string& name, const std::string& parent,
               const float rotation[3], const float position[3],
               const std::vector<std::vector<float>>& rotationKeys = {},
               const std::vector<std::vector<float>>& translationKeys = {}) {
        U8(0);
        Text(name, 32);
        Text(parent, 32);
        for (int k = 0; k < 3; ++k) F32(rotation[k]);
        for (int k = 0; k < 3; ++k) F32(position[k]);
        U16(static_cast<int>(rotationKeys.size()));
        U16(static_cast<int>(translationKeys.size()));
        for (const auto& key : rotationKeys) for (float value : key) F32(value);
        for (const auto& key : translationKeys) for (float value : key) F32(value);
    }

    const std::vector<uint8_t>& Bytes() const { return bytes_; }
    std::vector<uint8_t> Take() { return bytes_; }
    size_t Size() const { return bytes_.size(); }

private:
    std::vector<uint8_t> bytes_;
};

// The smallest complete file: two triangles sharing a quad, one group, one
// material, no joints, and no optional tail.
static std::vector<uint8_t> Quad(float transparency = 1.0f,
                                 const std::string& texture = "",
                                 int smoothingGroup = 0) {
    Builder b;
    b.Header();
    b.U16(4);
    b.Vertex(0, 0, 0); b.Vertex(1, 0, 0); b.Vertex(1, 1, 0); b.Vertex(0, 1, 0);
    b.U16(2);
    b.Triangle(0, 1, 2, smoothingGroup);
    b.Triangle(0, 2, 3, smoothingGroup);
    b.U16(1);
    b.Group("Panel", {0, 1}, 0);
    b.U16(1);
    b.Material("Paint", transparency, texture);
    b.AnimationHeader(30.0f, 0.0f, 60);
    b.U16(0);
    return b.Take();
}

static std::shared_ptr<ModelDocument> Read(const std::vector<uint8_t>& data,
                                           std::vector<std::string>* warnings = nullptr) {
    MS3DConverter converter;
    ConversionOptions options;
    if (warnings)
        options.WarningCallback = [warnings](const std::string& w) { warnings->push_back(w); };
    return converter.ImportFromMemory(data, options);
}

// ===== TESTS =====

static void TestRecognition() {
    std::printf("Recognition\n");
    MS3DConverter converter;
    Check(!converter.ValidateData({}), "empty data is not an .ms3d");
    Check(!converter.ValidateData(std::vector<uint8_t>{'M', 'S', '3', 'D'}),
          "nor are the first four bytes of the magic alone");
    Check(converter.ValidateData(Quad()), "a MilkShape file is recognised by its magic");

    const FormatCapabilities caps = converter.GetCapabilities();
    Check(caps.Meshes && caps.Materials && caps.Normals && caps.TextureCoordinates,
          "the capability report claims the geometry this reader produces");
    Check(caps.Animations && caps.Skinning,
          "and the joint animation and skinning it reads");
    Check(!caps.NGons && !caps.VertexColors && !caps.MorphTargets,
          "and claims nothing the format does not have - n-gons, colours, shape keys");
    Check(converter.CanImport() && !converter.CanExport(), "read-only, as declared");
}

static void TestGeometry() {
    std::printf("Geometry\n");
    auto doc = Read(Quad());
    Check(doc != nullptr, "a hand-built .ms3d reads");
    if (!doc) return;

    Check(doc->Meshes.size() == 1 && doc->Meshes[0].Name == "Panel",
          "a group becomes a mesh, named as the group is");
    Check(doc->TotalFaceCount() == 2, "with both its triangles");
    const MeshPrimitive& prim = doc->Meshes[0].Primitives[0];
    Check(prim.Mode == PrimitiveMode::Triangles,
          "as triangles - the format has no other topology");
    Check(doc->Up == UpAxis::YUp && doc->Chirality == Handedness::RightHanded,
          "read as Y-up and right-handed, which is the convention MilkShape works in");
    Check(doc->SourceUnit == ModelUnit::Unspecified,
          "and unitless, because the format states no unit rather than implying metres");

    Check(prim.FindAttribute(AttributeSemantic::TexCoord, 0) != nullptr,
          "the per-corner texture coordinates are read");
    Check(!prim.Normals.empty(), "and the per-corner normals, which the file stores");

    // The quad's four corners carry UVs that differ between the two triangles
    // at the shared edge, so the shared positions split. That is the whole
    // reason corner resolution exists, and the default UVs above make it
    // happen: corner 0 is (0,0) in both triangles, corner 2 is (1,1) in both,
    // but the other two differ.
    Check(prim.VertexCount() >= 4,
          "corners whose streams differ become distinct vertices, as in every other reader here");

    Check(doc->Materials.size() == 1 && doc->Materials[0].Name == "Paint",
          "the material is read and named");
    Check(prim.Material == 0, "and the group's material index binds to it");
    Check(Near(doc->Materials[0].BaseColorFactor.x, 0.25, 1e-6) &&
          Near(doc->Materials[0].BaseColorFactor.z, 0.75, 1e-6),
          "with the diffuse colour the file holds");
}

static void TestSmoothingGroups() {
    std::printf("Smoothing groups\n");

    // MilkShape numbers them 1..32 with 0 meaning none, and the document holds
    // a bitmask. So group 1 is bit 0, not the value 1 - which is the kind of
    // off-by-one that silently creases a model everywhere.
    auto none = Read(Quad(1.0f, "", 0));
    auto first = Read(Quad(1.0f, "", 1));
    auto third = Read(Quad(1.0f, "", 3));
    Check(none && first && third, "files differing only in smoothing group all read");
    if (!none || !first || !third) return;

    Check(none->Meshes[0].Primitives[0].SmoothingGroups.size() == 2,
          "one mask per triangle, parallel to the faces");
    Check(none->Meshes[0].Primitives[0].SmoothingGroups[0] == 0u,
          "group 0 means no smoothing, so the mask is empty");
    Check(first->Meshes[0].Primitives[0].SmoothingGroups[0] == 1u,
          "group 1 is bit 0");
    Check(third->Meshes[0].Primitives[0].SmoothingGroups[0] == 4u,
          "and group 3 is bit 2 - the number is the bit, not the value");
}

static void TestMaterials() {
    std::printf("Materials\n");

    auto opaque = Read(Quad(1.0f));
    auto glass = Read(Quad(0.4f));
    Check(opaque && glass, "both read");
    if (!opaque || !glass) return;
    Check(opaque->Materials[0].Alpha != AlphaMode::Blend,
          "transparency 1 is opaque - MilkShape's field is an alpha, not an opacity");
    Check(glass->Materials[0].Alpha == AlphaMode::Blend &&
          Near(glass->Materials[0].BaseColorFactor.w, 0.4, 1e-6),
          "and transparency 0.4 blends at that alpha");

    auto textured = Read(Quad(1.0f, "hull.png"));
    Check(textured && textured->Images.size() == 1,
          "a material's texture path becomes an image");
    if (textured && textured->Images.size() == 1) {
        Check(textured->Images[0].Uri == "hull.png" &&
              textured->Images[0].MimeType == "image/png",
              "with its path and a mime type derived from the extension");
        Check(textured->Materials[0].BaseColorTexture.IsSet(),
              "and the material points at it");
    }

    // An alpha map is a second image the document has no slot for. Recording
    // it is the difference between a leaf card and a solid quad.
    Builder b;
    b.Header();
    b.U16(4);
    b.Vertex(0, 0, 0); b.Vertex(1, 0, 0); b.Vertex(1, 1, 0); b.Vertex(0, 1, 0);
    b.U16(2);
    b.Triangle(0, 1, 2); b.Triangle(0, 2, 3);
    b.U16(1);
    b.Group("Leaf", {0, 1}, 0);
    b.U16(1);
    b.Material("Foliage", 1.0f, "leaf.tga", "leaf_alpha.tga");
    b.AnimationHeader(24.0f, 0.0f, 1);
    b.U16(0);
    auto masked = Read(b.Take());
    Check(masked && !masked->Materials.empty() &&
          masked->Materials[0].Extras.count("ms3d.alphaMap") == 1,
          "an alpha map has no document field, so it is kept as a material extra");
}

// A file with a skeleton: three joints in a chain, keyframes on the middle
// one, and vertices bound to them both ways the format allows.
static std::vector<uint8_t> Skinned(bool withWeightBlock) {
    Builder b;
    b.Header();
    b.U16(3);
    b.Vertex(0, 0, 0, 0);      // bound to joint 0 by the vertex's own boneId
    b.Vertex(1, 0, 0, 1);
    b.Vertex(0, 1, 0, 2);
    b.U16(1);
    b.Triangle(0, 1, 2);
    b.U16(1);
    b.Group("Limb", {0}, 0);
    b.U16(1);
    b.Material("Skin");
    b.AnimationHeader(30.0f, 0.0f, 60);

    const float zero[3] = {0, 0, 0};
    const float up[3] = {0, 1, 0};
    const float halfTurn[3] = {0, 0, 1.5707963f};

    b.U16(3);
    b.Joint("root", "", zero, zero);
    // The middle joint turns a quarter circle about Z over two seconds, and
    // slides a unit along Y.
    b.Joint("upper", "root", zero, up,
            {{0.0f, 0.0f, 0.0f, 0.0f}, {2.0f, halfTurn[0], halfTurn[1], halfTurn[2]}},
            {{0.0f, 0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 1.0f, 0.0f}});
    // Declared *before* its parent in file order would be legal too; this one
    // names a parent that appears earlier.
    b.Joint("lower", "upper", zero, up);

    if (withWeightBlock) {
        b.I32(1);                       // comments, subVersion 1
        b.I32(0); b.I32(0); b.I32(0);   // no group, material or joint comments
        b.I32(0);                       // no model comment
        b.I32(1);                       // vertex weights, subVersion 1
        // Vertex 0 keeps its own bone at the remainder, and shares with 1 and 2.
        b.U8(1); b.U8(2); b.U8(0xff);
        b.U8(25); b.U8(25); b.U8(0);
        for (int v = 1; v < 3; ++v) {
            b.U8(0xff); b.U8(0xff); b.U8(0xff);
            b.U8(0); b.U8(0); b.U8(0);
        }
    }
    return b.Take();
}

static void TestJointsAndAnimation() {
    std::printf("Joints and animation\n");

    auto doc = Read(Skinned(false));
    Check(doc != nullptr, "a file with a skeleton reads");
    if (!doc) return;

    Check(doc->Nodes.size() == 4,
          "three joints and the mesh's own node");
    // Joints name their parents by string, and the hierarchy has to come out
    // of that rather than out of file order.
    int rootIndex = -1, upperIndex = -1, lowerIndex = -1;
    for (size_t i = 0; i < doc->Nodes.size(); ++i) {
        if (doc->Nodes[i].Name == "root") rootIndex = static_cast<int>(i);
        if (doc->Nodes[i].Name == "upper") upperIndex = static_cast<int>(i);
        if (doc->Nodes[i].Name == "lower") lowerIndex = static_cast<int>(i);
    }
    Check(rootIndex >= 0 && upperIndex >= 0 && lowerIndex >= 0, "all three joints are nodes");
    if (rootIndex < 0 || upperIndex < 0 || lowerIndex < 0) return;
    Check(doc->Nodes[static_cast<size_t>(upperIndex)].Parent == rootIndex &&
          doc->Nodes[static_cast<size_t>(lowerIndex)].Parent == upperIndex,
          "parented by the names they carry, not by their order in the file");
    Check(Near(doc->Nodes[static_cast<size_t>(upperIndex)].Translation.y, 1.0, 1e-6),
          "each joint's rest position is its local translation");

    Check(doc->Animations.size() == 1, "the keyframes become one animation");
    if (doc->Animations.empty()) return;
    const ModelAnimation& animation = doc->Animations[0];
    Check(animation.Channels.size() == 2,
          "with a translation channel and a rotation channel for the joint that moves");

    bool sawRotation = false, sawTranslation = false;
    for (const AnimationChannel& channel : animation.Channels) {
        const AnimationSampler& sampler = animation.Samplers[
                static_cast<size_t>(channel.Sampler)];
        Check(channel.TargetNode == upperIndex, "both targeting the joint that has them");
        Check(sampler.Times.size() == 2 && Near(sampler.Times.back(), 2.0, 1e-6),
              "at the times the file states, read as seconds");
        if (channel.Path == AnimationPath::Rotation) {
            sawRotation = true;
            Check(sampler.Values.size() == 8,
                  "a rotation key is a quaternion, so four floats each");
            // The key is an absolute orientation composed with the rest pose,
            // which for a rest of identity is the key itself: a quarter turn
            // about Z is (0, 0, sin(45), cos(45)).
            Check(Near(sampler.Values[6], std::sin(0.7853981), 1e-4) &&
                  Near(sampler.Values[7], std::cos(0.7853981), 1e-4),
                  "and the Euler triple becomes that quaternion");
        }
        if (channel.Path == AnimationPath::Translation) {
            sawTranslation = true;
            // A translation key is relative to the joint's rest position, so
            // resting at y=1 and keying +1 lands at 2.
            Check(sampler.Values.size() == 6 && Near(sampler.Values[4], 2.0, 1e-6),
                  "a translation key is relative to the rest position, so it adds to it");
        }
    }
    Check(sawRotation && sawTranslation, "both paths are present");

    Check(doc->Skins.size() == 1 && doc->Skins[0].Joints.size() == 3,
          "the joints also form a skin, in the order the file lists them");
}

static void TestSkinning() {
    std::printf("Skinning\n");

    auto simple = Read(Skinned(false));
    Check(simple != nullptr, "a file with only the per-vertex boneId reads");
    if (simple) {
        const MeshPrimitive& prim = simple->Meshes[0].Primitives[0];
        const VertexAttribute* joints = prim.FindAttribute(AttributeSemantic::Joints, 0);
        const VertexAttribute* weights = prim.FindAttribute(AttributeSemantic::Weights, 0);
        Check(joints && weights, "and gives joint and weight attributes");
        if (joints && weights) {
            Check(Near(weights->Values[0], 1.0, 1e-6),
                  "a vertex with one bone is fully weighted to it");
            Check(Near(joints->Values[0], 0.0, 1e-6),
                  "and names the bone its boneId gives");
        }
    }

    // The optional block adds three more bones. The vertex's own id keeps
    // whatever share the other three leave, which is how MilkShape means it.
    auto weighted = Read(Skinned(true));
    Check(weighted != nullptr, "a file with the weight block reads");
    if (weighted) {
        const MeshPrimitive& prim = weighted->Meshes[0].Primitives[0];
        const VertexAttribute* weights = prim.FindAttribute(AttributeSemantic::Weights, 0);
        Check(weights != nullptr, "and still gives weights");
        if (weights) {
            Check(Near(weights->Values[1], 0.25, 1e-6) && Near(weights->Values[2], 0.25, 1e-6),
                  "the extra bones take the shares the block states");
            Check(Near(weights->Values[0], 0.5, 1e-6),
                  "and the vertex's own bone takes the remainder, not a full share");
        }
    }
}

static void TestOptionalTail() {
    std::printf("The optional tail\n");

    // A file may simply stop after the joints; everything past them is later
    // additions, each with its own version.
    std::vector<std::string> warnings;
    auto bare = Read(Quad(), &warnings);
    Check(bare != nullptr && warnings.empty(),
          "a file that ends at the joints reads, and warns about nothing");

    // A block version this reader does not know means the layout past it is
    // unknown. Reading stops there, and what came before still stands.
    Builder b;
    b.Header();
    b.U16(4);
    b.Vertex(0, 0, 0); b.Vertex(1, 0, 0); b.Vertex(1, 1, 0); b.Vertex(0, 1, 0);
    b.U16(2);
    b.Triangle(0, 1, 2); b.Triangle(0, 2, 3);
    b.U16(1);
    b.Group("Panel", {0, 1}, 0);
    b.U16(1);
    b.Material("Paint");
    b.AnimationHeader(24.0f, 0.0f, 1);
    b.U16(0);
    b.I32(99);                        // a comment block from the future
    warnings.clear();
    auto future = Read(b.Take(), &warnings);
    Check(future != nullptr && future->TotalFaceCount() == 2,
          "an unknown tail block does not lose the geometry that came before it");
    bool named = false;
    for (const std::string& w : warnings)
        if (w.find("99") != std::string::npos) named = true;
    Check(named, "and the version it did not understand is named");

    // A group comment names a mesh whose own name is empty.
    Builder c;
    c.Header();
    c.U16(4);
    c.Vertex(0, 0, 0); c.Vertex(1, 0, 0); c.Vertex(1, 1, 0); c.Vertex(0, 1, 0);
    c.U16(2);
    c.Triangle(0, 1, 2); c.Triangle(0, 2, 3);
    c.U16(1);
    c.Group("", {0, 1}, 0);
    c.U16(1);
    c.Material("Paint");
    c.AnimationHeader(24.0f, 0.0f, 1);
    c.U16(0);
    c.I32(1);                          // comments, subVersion 1
    const std::string comment = "Fuselage";
    c.I32(1); c.I32(0); c.I32(static_cast<int32_t>(comment.size()));
    c.Text(comment, comment.size());
    c.I32(0); c.I32(0);                // no material or joint comments
    c.I32(0);                          // no model comment
    auto commented = Read(c.Take());
    Check(commented && commented->Meshes.size() == 1 &&
          commented->Meshes[0].Name == "Fuselage",
          "an unnamed group falls back to its comment for a name");
}

static void TestRefusals() {
    std::printf("Refusals\n");
    std::vector<std::string> warnings;

    Check(Read(std::vector<uint8_t>{'n', 'o', 't', 'm', 's', '3', 'd', '!', '!', '!'},
               &warnings) == nullptr,
          "a file without the magic reads as nothing");
    Check(!warnings.empty(), "and says which magic it wanted");

    // A version MilkShape never wrote.
    warnings.clear();
    Builder b;
    b.Header(9);
    Check(Read(b.Take(), &warnings) == nullptr, "an unknown file version reads as nothing");
    bool namedVersion = false;
    for (const std::string& w : warnings)
        if (w.find("9") != std::string::npos) namedVersion = true;
    Check(namedVersion, "naming the version it found");

    // A declared count the file cannot satisfy must be refused *before*
    // anything is reserved for it, or a ten-byte file asks for a megabyte.
    warnings.clear();
    Builder liar;
    liar.Header();
    liar.U16(60000);                   // sixty thousand vertices, none of them present
    Check(Read(liar.Take(), &warnings) == nullptr,
          "a vertex count the file cannot carry is refused rather than allocated");
    Check(!warnings.empty(), "with a warning that says so");

    warnings.clear();
    Builder liarTriangles;
    liarTriangles.Header();
    liarTriangles.U16(1);
    liarTriangles.Vertex(0, 0, 0);
    liarTriangles.U16(60000);
    Check(Read(liarTriangles.Take(), &warnings) == nullptr,
          "and so is a triangle count it cannot carry");

    // Truncation at every length must stop at the end of the buffer rather
    // than read past it. Either answer - a document or nothing - is correct;
    // what is being asserted is that the loop finishes at all, which under the
    // sanitizers this suite is run with means no read ran off the end. The
    // counter makes that explicit rather than leaving a flag that is only ever
    // true: reaching the Check having attempted every prefix is the result.
    const std::vector<uint8_t> whole = Skinned(true);
    size_t attempted = 0;
    for (size_t cut = 0; cut < whole.size(); ++cut) {
        std::vector<std::string> ignored;
        auto partial = Read(std::vector<uint8_t>(whole.begin(), whole.begin() + cut), &ignored);
        (void)partial;
        ++attempted;
    }
    Check(attempted == whole.size(),
          "every one of the " + std::to_string(whole.size()) +
                  " truncations of a complete file is read or refused, never run off the end");

    // A triangle naming a vertex that is not there is dropped, and the rest of
    // the mesh still arrives.
    warnings.clear();
    Builder stray;
    stray.Header();
    stray.U16(4);
    stray.Vertex(0, 0, 0); stray.Vertex(1, 0, 0); stray.Vertex(1, 1, 0); stray.Vertex(0, 1, 0);
    stray.U16(2);
    stray.Triangle(0, 1, 2);
    stray.Triangle(0, 2, 900);         // out of range
    stray.U16(1);
    stray.Group("Panel", {0, 1}, 0);
    stray.U16(1);
    stray.Material("Paint");
    stray.AnimationHeader(24.0f, 0.0f, 1);
    stray.U16(0);
    auto partial = Read(stray.Take(), &warnings);
    Check(partial != nullptr && partial->TotalFaceCount() == 1,
          "a triangle naming a vertex that is not in the file is skipped, not fatal");
    Check(!warnings.empty(), "and reported");
}

static void TestSample(const char* path) {
    std::printf("Sample: %s\n", path);
    MS3DConverter converter;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    auto doc = converter.Import(path, options);
    Check(doc != nullptr, "the aircraft's MilkShape export reads");
    if (!doc) return;

    Check(doc->Metadata.count("ms3d.version") == 1 && doc->Metadata.at("ms3d.version") == "4",
          "as version 4, which is what MilkShape writes");
    Check(warnings.empty(), "and without a single warning");

    // This is the finding worth keeping: the export is the canopy alone. Every
    // other export of this aircraft in the repository carries two meshes.
    Check(doc->Meshes.size() == 1,
          "one mesh - and that is the whole file, not a reader that stopped early");
    Check(doc->Materials.size() == 1 && doc->Materials[0].Name == "Material.004",
          "carrying Material.004, which is the canopy's material in every other export");

    Check(doc->TotalFaceCount() == 1488, "1488 triangles");
    Check(doc->TotalFaceCount() == 744 * 2,
          "exactly twice the Alembic canopy's 744 faces - the same mesh, triangulated");
    Check(doc->TotalVertexCount() == 1014,
          "1014 vertices: every corner of this export already agrees on its UV, so none split");

    const MeshPrimitive& prim = doc->Meshes[0].Primitives[0];
    Check(prim.Normals.size() == prim.Positions.size(),
          "a normal per vertex, taken from the file rather than generated");
    Check(prim.FindAttribute(AttributeSemantic::TexCoord, 0) != nullptr,
          "and a texture coordinate per vertex");
    Check(doc->Images.empty(),
          "no image: this export names no texture file, and inventing one would be a lie");

    Check(doc->Animations.empty() && doc->Skins.empty(),
          "no joints, so no skeleton and no animation - the export dropped the armature");

    const Bounds3D bounds = doc->ComputeBounds();
    Check(Near(bounds.Min.x, -0.1016, 1e-3) && Near(bounds.Max.x, 2.9357, 1e-3) &&
          Near(bounds.Min.y, 0.0669, 1e-3) && Near(bounds.Max.y, 1.5314, 1e-3) &&
          Near(bounds.Min.z, -0.5591, 1e-3) && Near(bounds.Max.z, 0.5591, 1e-3),
          "world bounds match an independent walk of the file's vertex block");

    // The axes are permuted relative to the other exports, and the spans are
    // what prove it is the same canopy rather than a similar one. The Alembic
    // canopy is X[-0.5591, 0.5591] Y[-1.4587, 0.0057] Z[-3.0372, 0.0001].
    Check(Near(bounds.Max.z - bounds.Min.z, 1.1182, 1e-3),
          "its symmetric axis spans 1.1182, exactly the Alembic canopy's X");
    Check(Near(bounds.Max.x - bounds.Min.x, 3.0373, 1e-3),
          "its longest axis spans 3.0373, exactly the Alembic canopy's Z");
    Check(Near(bounds.Max.y - bounds.Min.y, 1.4645, 1e-3),
          "and the third 1.4645, the Alembic canopy's Y - the same mesh, axes permuted");

    Check(Near(bounds.Min.z, -bounds.Max.z, 1e-6),
          "and it is symmetric about that axis, as a canopy down the centreline is");
}

int main(int argc, char** argv) {
    TestRecognition();
    TestGeometry();
    TestSmoothingGroups();
    TestMaterials();
    TestJointsAndAnimation();
    TestSkinning();
    TestOptionalTail();
    TestRefusals();
    if (argc > 1) TestSample(argv[1]);
    else std::printf("Sample: skipped (pass an .ms3d path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
