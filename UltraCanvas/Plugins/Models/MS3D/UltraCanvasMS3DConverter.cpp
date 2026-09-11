// Plugins/Models/MS3D/UltraCanvasMS3DConverter.cpp
// The MilkShape 3D reader. See UltraCanvasMS3DConverter.h for what the format
// is and which of its decisions this reader had to make.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/MS3D/UltraCanvasMS3DConverter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;

namespace {

constexpr char kMagic[10] = {'M', 'S', '3', 'D', '0', '0', '0', '0', '0', '0'};
constexpr size_t kHeaderSize = 14;   // magic + int32 version

// MilkShape's own limits. A count beyond these is a corrupt file rather than a
// large model - the counts are 16-bit, so they cannot legitimately exceed
// 65535 in the first place, and refusing early keeps a bad length from turning
// into a large allocation.
constexpr uint32_t kMaxVertices = 65534;
constexpr uint32_t kMaxTriangles = 65534;

// ===== A BOUNDS-CHECKED READER =====
//
// Every read is against the bytes actually present. The format states a count
// and then that many fixed-size records, so a truncated file always shows up
// as a count that cannot be satisfied - which is checked *before* anything is
// reserved, so a claimed 60000 triangles in a 200-byte file allocates nothing.

class Cursor {
public:
    Cursor(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool Ok() const { return ok_; }
    size_t Remaining() const { return position_ <= size_ ? size_ - position_ : 0; }
    size_t Position() const { return position_; }

    bool Skip(size_t bytes) {
        if (Remaining() < bytes) return Fail();
        position_ += bytes;
        return true;
    }

    // True when `count` records of `each` bytes are actually present.
    bool Holds(size_t count, size_t each) const {
        return each == 0 || count <= Remaining() / each;
    }

    uint8_t U8() {
        if (Remaining() < 1) { Fail(); return 0; }
        return data_[position_++];
    }
    int8_t I8() { return static_cast<int8_t>(U8()); }

    uint16_t U16() {
        if (Remaining() < 2) { Fail(); return 0; }
        const uint16_t value = static_cast<uint16_t>(data_[position_] |
                                                     (data_[position_ + 1] << 8));
        position_ += 2;
        return value;
    }

    uint32_t U32() {
        if (Remaining() < 4) { Fail(); return 0; }
        const uint32_t value = static_cast<uint32_t>(data_[position_]) |
                               (static_cast<uint32_t>(data_[position_ + 1]) << 8) |
                               (static_cast<uint32_t>(data_[position_ + 2]) << 16) |
                               (static_cast<uint32_t>(data_[position_ + 3]) << 24);
        position_ += 4;
        return value;
    }
    int32_t I32() { return static_cast<int32_t>(U32()); }

    float F32() {
        const uint32_t raw = U32();
        float value = 0.0f;
        std::memcpy(&value, &raw, 4);
        // A NaN or infinity in a coordinate poisons every bound and every
        // normal computed from it, and says the file is damaged rather than
        // unusual. Zero is wrong too, but it is wrong locally.
        return std::isfinite(value) ? value : 0.0f;
    }

    // A fixed-width char field, trimmed at its first NUL. MilkShape pads with
    // NULs but does not guarantee one at the end of a full-width name.
    std::string Text(size_t width) {
        if (Remaining() < width) { Fail(); return {}; }
        const uint8_t* start = data_ + position_;
        size_t length = 0;
        while (length < width && start[length] != 0) ++length;
        position_ += width;
        return std::string(reinterpret_cast<const char*>(start), length);
    }

private:
    bool Fail() {
        ok_ = false;
        position_ = size_;
        return false;
    }

    const uint8_t* data_;
    size_t size_;
    size_t position_ = 0;
    bool ok_ = true;
};

// ===== THE FILE'S OWN RECORDS =====

struct Vertex {
    float Position[3] = {0, 0, 0};
    int8_t BoneId = -1;
};

struct Triangle {
    uint16_t Indices[3] = {0, 0, 0};
    float Normals[3][3] = {};
    float S[3] = {0, 0, 0};
    float T[3] = {0, 0, 0};
    uint8_t SmoothingGroup = 0;
    uint8_t GroupIndex = 0;
};

struct Group {
    std::string Name;
    std::vector<uint16_t> Triangles;
    int8_t Material = -1;
    uint8_t Flags = 0;
};

struct Material {
    std::string Name;
    float Ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    float Diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    float Specular[4] = {0, 0, 0, 1};
    float Emissive[4] = {0, 0, 0, 1};
    float Shininess = 0.0f;      // 0..128
    float Transparency = 1.0f;   // 1 = opaque
    std::string Texture;
    std::string AlphaMap;
};

struct Keyframe {
    float Time = 0.0f;
    float Value[3] = {0, 0, 0};
};

struct Joint {
    std::string Name;
    std::string Parent;
    float Rotation[3] = {0, 0, 0};   // XYZ Euler radians
    float Position[3] = {0, 0, 0};
    std::vector<Keyframe> RotationKeys;
    std::vector<Keyframe> TranslationKeys;
    int Node = -1;                   // filled in while building the document
};

// The optional per-vertex block: three more bone influences beside the
// vertex's own single id.
struct VertexWeights {
    int8_t Bones[3] = {-1, -1, -1};
    uint8_t Weights[3] = {0, 0, 0};
};

struct File {
    int32_t Version = 0;
    std::vector<Vertex> Vertices;
    std::vector<Triangle> Triangles;
    std::vector<Group> Groups;
    std::vector<Material> Materials;
    float FramesPerSecond = 24.0f;
    float CurrentTime = 0.0f;
    int32_t TotalFrames = 0;
    std::vector<Joint> Joints;
    std::vector<VertexWeights> Weights;       // empty when the block is absent
    std::map<int, std::string> GroupComments;
    std::string ModelComment;
    float JointSize = 0.0f;
    int32_t TransparencyMode = 0;
    float AlphaRef = 0.0f;
    bool HasModelExtra = false;
};

Quatd EulerXYZ(double x, double y, double z) {
    // MilkShape composes a joint's rotation X then Y then Z, applied to the
    // vector in that order, which is R = Rz * Ry * Rx as matrices.
    const Quatd rx = Quatd::FromAxisAngle(Vec3d(1.0, 0.0, 0.0), x);
    const Quatd ry = Quatd::FromAxisAngle(Vec3d(0.0, 1.0, 0.0), y);
    const Quatd rz = Quatd::FromAxisAngle(Vec3d(0.0, 0.0, 1.0), z);
    return rz * ry * rx;
}

bool ReadKeys(Cursor& cursor, uint16_t count, std::vector<Keyframe>& out) {
    if (!cursor.Holds(count, 16)) return false;
    out.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        Keyframe key;
        key.Time = cursor.F32();
        key.Value[0] = cursor.F32();
        key.Value[1] = cursor.F32();
        key.Value[2] = cursor.F32();
        out.push_back(key);
    }
    return cursor.Ok();
}

// ===== THE OPTIONAL TAIL =====
//
// Everything past the joints was added after the format shipped, each block
// prefixed with its own version number. A file may stop at any block boundary,
// and an unknown version means the layout past that point is not known - so
// reading stops rather than guessing, and what was already read still stands.

bool ReadComments(Cursor& cursor, File& out, const ConversionOptions& options) {
    const int32_t subVersion = cursor.I32();
    if (!cursor.Ok()) return false;
    if (subVersion != 1) {
        options.Warn("MS3D: comment block version " + std::to_string(subVersion) +
                     " is newer than this reader knows; comments and everything after "
                     "them are not read");
        return false;
    }

    // Group, material and joint comments in that order, then one for the model.
    for (int kind = 0; kind < 3; ++kind) {
        const int32_t count = cursor.I32();
        if (!cursor.Ok() || count < 0) return false;
        for (int32_t i = 0; i < count; ++i) {
            const int32_t index = cursor.I32();
            const int32_t length = cursor.I32();
            if (!cursor.Ok() || length < 0 ||
                static_cast<size_t>(length) > cursor.Remaining())
                return false;
            std::string text = cursor.Text(static_cast<size_t>(length));
            if (kind == 0 && index >= 0) out.GroupComments[index] = std::move(text);
        }
    }
    const int32_t modelLength = cursor.I32();
    if (!cursor.Ok() || modelLength < 0 ||
        static_cast<size_t>(modelLength) > cursor.Remaining())
        return false;
    if (modelLength > 0) out.ModelComment = cursor.Text(static_cast<size_t>(modelLength));
    return cursor.Ok();
}

bool ReadVertexWeights(Cursor& cursor, File& out, const ConversionOptions& options) {
    const int32_t subVersion = cursor.I32();
    if (!cursor.Ok()) return false;
    // 1: three bones and three weights. 2 and 3 add a per-vertex "extra" word
    // this reader has no use for, but whose width it must know to keep its
    // place in the stream.
    size_t each = 0;
    if (subVersion == 1) each = 6;
    else if (subVersion == 2) each = 10;
    else if (subVersion == 3) each = 14;
    else {
        options.Warn("MS3D: vertex weight block version " + std::to_string(subVersion) +
                     " is newer than this reader knows; skinning weights and everything "
                     "after them are not read");
        return false;
    }
    if (!cursor.Holds(out.Vertices.size(), each)) return false;

    out.Weights.resize(out.Vertices.size());
    for (VertexWeights& weights : out.Weights) {
        weights.Bones[0] = cursor.I8();
        weights.Bones[1] = cursor.I8();
        weights.Bones[2] = cursor.I8();
        weights.Weights[0] = cursor.U8();
        weights.Weights[1] = cursor.U8();
        weights.Weights[2] = cursor.U8();
        if (each > 6) cursor.Skip(each - 6);
    }
    return cursor.Ok();
}

bool ReadOptionalTail(Cursor& cursor, File& out, const ConversionOptions& options) {
    if (cursor.Remaining() < 4) return true;
    if (!ReadComments(cursor, out, options)) return false;

    if (cursor.Remaining() < 4) return true;
    if (!ReadVertexWeights(cursor, out, options)) return false;

    // Joint colours: one RGB triple per joint. Nothing the document holds.
    if (cursor.Remaining() < 4) return true;
    const int32_t jointExtra = cursor.I32();
    if (!cursor.Ok()) return false;
    if (jointExtra != 1) return false;
    if (!cursor.Skip(out.Joints.size() * 3 * 4)) {
        // Some writers emit one 32-bit colour per joint rather than three
        // floats. Neither is read, so the only thing that matters is landing
        // in the right place for the block after it.
        return false;
    }

    if (cursor.Remaining() < 4) return true;
    const int32_t modelExtra = cursor.I32();
    if (!cursor.Ok()) return false;
    if (modelExtra != 1) return false;
    out.JointSize = cursor.F32();
    out.TransparencyMode = cursor.I32();
    out.AlphaRef = cursor.F32();
    out.HasModelExtra = cursor.Ok();
    return cursor.Ok();
}

// ===== PARSING =====

bool Parse(const std::vector<uint8_t>& data, File& out, const ConversionOptions& options) {
    if (data.size() < kHeaderSize ||
        std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0) {
        options.Warn("MS3D: not a MilkShape file - it does not begin \"MS3D000000\"");
        return false;
    }

    Cursor cursor(data.data(), data.size());
    cursor.Skip(sizeof(kMagic));
    out.Version = cursor.I32();
    if (out.Version != 3 && out.Version != 4) {
        options.Warn("MS3D: version " + std::to_string(out.Version) +
                     " is not 3 or 4, which are the versions MilkShape has written");
        return false;
    }

    // --- vertices ---
    const uint16_t vertexCount = cursor.U16();
    if (vertexCount > kMaxVertices || !cursor.Holds(vertexCount, 15)) {
        options.Warn("MS3D: the file states " + std::to_string(vertexCount) +
                     " vertices but does not carry them");
        return false;
    }
    out.Vertices.reserve(vertexCount);
    for (uint16_t i = 0; i < vertexCount; ++i) {
        Vertex vertex;
        cursor.U8();                       // flags: selection and hidden state
        vertex.Position[0] = cursor.F32();
        vertex.Position[1] = cursor.F32();
        vertex.Position[2] = cursor.F32();
        vertex.BoneId = cursor.I8();
        cursor.U8();                       // reference count
        out.Vertices.push_back(vertex);
    }

    // --- triangles ---
    const uint16_t triangleCount = cursor.U16();
    if (triangleCount > kMaxTriangles || !cursor.Holds(triangleCount, 70)) {
        options.Warn("MS3D: the file states " + std::to_string(triangleCount) +
                     " triangles but does not carry them");
        return false;
    }
    out.Triangles.reserve(triangleCount);
    for (uint16_t i = 0; i < triangleCount; ++i) {
        Triangle triangle;
        cursor.U16();                      // flags
        for (int k = 0; k < 3; ++k) triangle.Indices[k] = cursor.U16();
        for (int k = 0; k < 3; ++k)
            for (int axis = 0; axis < 3; ++axis) triangle.Normals[k][axis] = cursor.F32();
        for (int k = 0; k < 3; ++k) triangle.S[k] = cursor.F32();
        for (int k = 0; k < 3; ++k) triangle.T[k] = cursor.F32();
        triangle.SmoothingGroup = cursor.U8();
        triangle.GroupIndex = cursor.U8();
        out.Triangles.push_back(triangle);
    }

    // --- groups ---
    const uint16_t groupCount = cursor.U16();
    if (!cursor.Ok()) return false;
    for (uint16_t i = 0; i < groupCount; ++i) {
        Group group;
        group.Flags = cursor.U8();
        group.Name = cursor.Text(32);
        const uint16_t count = cursor.U16();
        if (!cursor.Holds(count, 2)) {
            options.Warn("MS3D: a group states " + std::to_string(count) +
                         " triangles but the file ends first");
            return false;
        }
        group.Triangles.reserve(count);
        for (uint16_t k = 0; k < count; ++k) group.Triangles.push_back(cursor.U16());
        group.Material = cursor.I8();
        if (!cursor.Ok()) return false;
        out.Groups.push_back(std::move(group));
    }

    // --- materials ---
    const uint16_t materialCount = cursor.U16();
    if (!cursor.Holds(materialCount, 361)) {
        options.Warn("MS3D: the file states " + std::to_string(materialCount) +
                     " materials but does not carry them");
        return false;
    }
    for (uint16_t i = 0; i < materialCount; ++i) {
        Material material;
        material.Name = cursor.Text(32);
        for (int k = 0; k < 4; ++k) material.Ambient[k] = cursor.F32();
        for (int k = 0; k < 4; ++k) material.Diffuse[k] = cursor.F32();
        for (int k = 0; k < 4; ++k) material.Specular[k] = cursor.F32();
        for (int k = 0; k < 4; ++k) material.Emissive[k] = cursor.F32();
        material.Shininess = cursor.F32();
        material.Transparency = cursor.F32();
        cursor.U8();                       // mode
        material.Texture = cursor.Text(128);
        material.AlphaMap = cursor.Text(128);
        out.Materials.push_back(std::move(material));
    }
    if (!cursor.Ok()) return false;

    // --- animation header and joints ---
    out.FramesPerSecond = cursor.F32();
    out.CurrentTime = cursor.F32();
    out.TotalFrames = cursor.I32();
    if (!cursor.Ok()) return false;

    const uint16_t jointCount = cursor.U16();
    if (!cursor.Ok()) return false;
    for (uint16_t i = 0; i < jointCount; ++i) {
        Joint joint;
        cursor.U8();                       // flags
        joint.Name = cursor.Text(32);
        joint.Parent = cursor.Text(32);
        for (int k = 0; k < 3; ++k) joint.Rotation[k] = cursor.F32();
        for (int k = 0; k < 3; ++k) joint.Position[k] = cursor.F32();
        const uint16_t rotationKeys = cursor.U16();
        const uint16_t translationKeys = cursor.U16();
        if (!cursor.Ok()) return false;
        if (!ReadKeys(cursor, rotationKeys, joint.RotationKeys) ||
            !ReadKeys(cursor, translationKeys, joint.TranslationKeys)) {
            options.Warn("MS3D: joint \"" + joint.Name +
                         "\" states more keyframes than the file carries");
            return false;
        }
        out.Joints.push_back(std::move(joint));
    }

    // The tail is optional in every sense: a file may simply stop, and a block
    // this reader does not understand stops it without losing what came before.
    ReadOptionalTail(cursor, out, options);
    return true;
}

// ===== BUILDING THE DOCUMENT =====

class Builder {
public:
    explicit Builder(const ConversionOptions& options) : options_(options) {}

    std::shared_ptr<ModelDocument> Build(const File& file) {
        file_ = &file;
        document_ = std::make_shared<ModelDocument>();
        document_->SourceFormat = "ms3d";
        document_->Metadata["ms3d.version"] = std::to_string(file.Version);
        document_->Metadata["ms3d.framesPerSecond"] = std::to_string(file.FramesPerSecond);
        document_->Metadata["ms3d.totalFrames"] = std::to_string(file.TotalFrames);
        if (!file.ModelComment.empty()) document_->Metadata["ms3d.comment"] = file.ModelComment;
        if (file.HasModelExtra && file.TransparencyMode != 0)
            document_->Metadata["ms3d.transparencyMode"] = std::to_string(file.TransparencyMode);

        // MilkShape works in OpenGL's space: Y up, right-handed, and no unit
        // at all - the format states no scale, so a caller is told the number
        // is unitless rather than being handed a fictional metre.
        document_->Up = UpAxis::YUp;
        document_->Chirality = Handedness::RightHanded;
        document_->SourceUnit = ModelUnit::Unspecified;

        BuildMaterials();
        BuildJoints();
        BuildMeshes();
        BuildAnimation();

        if (document_->Meshes.empty() && document_->Nodes.empty()) {
            options_.Warn("MS3D: the file holds no geometry and no joints");
            return nullptr;
        }
        return document_;
    }

private:
    void BuildMaterials() {
        for (const Material& source : file_->Materials) {
            ModelMaterial material;
            material.Name = source.Name;

            // MilkShape's `transparency` is an alpha: 1 is opaque. Its diffuse
            // alpha says the same thing, and exporters disagree about which
            // they fill in, so the more transparent of the two wins - a file
            // that means opaque sets both to 1 and is unaffected.
            const float alpha = std::min(source.Transparency, source.Diffuse[3]);
            material.BaseColorFactor =
                    Vec4f(source.Diffuse[0], source.Diffuse[1], source.Diffuse[2], alpha);
            if (alpha < 0.999f) material.Alpha = AlphaMode::Blend;
            material.EmissiveFactor =
                    Vec3f(source.Emissive[0], source.Emissive[1], source.Emissive[2]);

            PhongParams phong;
            phong.Ambient = Vec3f(source.Ambient[0], source.Ambient[1], source.Ambient[2]);
            phong.Diffuse = Vec3f(source.Diffuse[0], source.Diffuse[1], source.Diffuse[2]);
            phong.Specular = Vec3f(source.Specular[0], source.Specular[1], source.Specular[2]);
            phong.Shininess = source.Shininess;

            if (!source.Texture.empty()) {
                const TextureRef texture = AddImage(source.Texture, source.Name);
                material.BaseColorTexture = texture;
                phong.DiffuseTexture = texture;
            }
            // An alpha map is a second image the document has no slot for; it
            // is recorded rather than dropped, because it is the difference
            // between a leaf card and a solid quad.
            if (!source.AlphaMap.empty())
                material.Extras["ms3d.alphaMap"] = source.AlphaMap;

            material.Phong = phong;
            material.DeriveMissingModel();
            document_->AddMaterial(std::move(material));
        }
    }

    TextureRef AddImage(const std::string& path, const std::string& owner) {
        auto cached = imageByPath_.find(path);
        if (cached == imageByPath_.end()) {
            ModelImage image;
            image.Name = owner.empty() ? path : owner;
            image.Uri = path;
            const size_t dot = path.rfind('.');
            if (dot != std::string::npos) {
                std::string extension = path.substr(dot + 1);
                for (char& c : extension)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (extension == "png") image.MimeType = "image/png";
                else if (extension == "jpg" || extension == "jpeg") image.MimeType = "image/jpeg";
                else if (extension == "bmp") image.MimeType = "image/bmp";
                else if (extension == "tga") image.MimeType = "image/x-tga";
            }
            const int index = static_cast<int>(document_->Images.size());
            document_->Images.push_back(std::move(image));
            cached = imageByPath_.emplace(path, index).first;
        }
        TextureRef texture;
        texture.Image = cached->second;
        return texture;
    }

    // Joints name their parents by string, so the hierarchy is resolved by
    // name and a parent that appears *after* its child still works.
    void BuildJoints() {
        if (file_->Joints.empty()) return;
        joints_ = file_->Joints;

        std::map<std::string, size_t> byName;
        for (size_t i = 0; i < joints_.size(); ++i) byName[joints_[i].Name] = i;

        std::vector<int> state(joints_.size(), 0);   // 0 unvisited, 1 in progress, 2 done
        for (size_t i = 0; i < joints_.size(); ++i) AddJoint(i, byName, state);

        ModelSkin skin;
        skin.Name = "Skeleton";
        for (const Joint& joint : joints_) skin.Joints.push_back(joint.Node);
        if (!joints_.empty() && joints_[0].Parent.empty()) skin.Skeleton = joints_[0].Node;
        document_->Skins.push_back(std::move(skin));
    }

    int AddJoint(size_t index, const std::map<std::string, size_t>& byName,
                 std::vector<int>& state) {
        if (state[index] == 2) return joints_[index].Node;
        if (state[index] == 1) {
            // A parent chain that loops back on itself. The joint becomes a
            // root rather than recursing forever.
            WarnOnce("jointcycle", "MS3D: the joints name a parent cycle; the chain is "
                                   "broken at \"" + joints_[index].Name + "\"");
            return -1;
        }
        state[index] = 1;

        int parent = -1;
        const std::string& parentName = joints_[index].Parent;
        if (!parentName.empty()) {
            auto found = byName.find(parentName);
            if (found == byName.end()) {
                WarnOnce("jointparent", "MS3D: joint \"" + joints_[index].Name +
                                        "\" names a parent \"" + parentName +
                                        "\" that is not in the file; it is placed at the root");
            } else {
                parent = AddJoint(found->second, byName, state);
            }
        }

        ModelNode node;
        node.Name = joints_[index].Name.empty() ? "Joint" : joints_[index].Name;
        node.Translation = Vec3d(joints_[index].Position[0], joints_[index].Position[1],
                                 joints_[index].Position[2]);
        node.Rotation = EulerXYZ(joints_[index].Rotation[0], joints_[index].Rotation[1],
                                 joints_[index].Rotation[2]);
        joints_[index].Node = document_->AddNode(std::move(node), parent);
        state[index] = 2;
        return joints_[index].Node;
    }

    void BuildMeshes() {
        if (file_->Triangles.empty()) return;

        // A group is a named triangle list with one material, which is exactly
        // one mesh with one primitive. A file with no groups at all still has
        // its triangles, so they become a single unnamed mesh rather than
        // being dropped.
        if (file_->Groups.empty()) {
            std::vector<uint16_t> all(file_->Triangles.size());
            for (size_t i = 0; i < all.size(); ++i) all[i] = static_cast<uint16_t>(i);
            Group implied;
            implied.Name = "Mesh";
            implied.Triangles = std::move(all);
            AddGroup(implied, -1);
            return;
        }
        for (size_t i = 0; i < file_->Groups.size(); ++i)
            AddGroup(file_->Groups[i], static_cast<int>(i));
    }

    void AddGroup(const Group& group, int groupIndex) {
        MeshPrimitive prim;
        prim.Mode = PrimitiveMode::Triangles;

        std::map<std::tuple<uint16_t, uint64_t, uint64_t>, uint32_t> unique;
        std::vector<std::pair<float, float>> texcoords;
        std::vector<int> boneOf;            // the winning bone per document vertex

        for (uint16_t triangleIndex : group.Triangles) {
            if (triangleIndex >= file_->Triangles.size()) {
                WarnOnce("badtriangle", "MS3D: a group names a triangle that is not in the "
                                        "file; it is skipped");
                continue;
            }
            const Triangle& triangle = file_->Triangles[triangleIndex];

            bool usable = true;
            for (int k = 0; k < 3; ++k)
                if (triangle.Indices[k] >= file_->Vertices.size()) usable = false;
            if (!usable) {
                WarnOnce("badindex", "MS3D: a triangle names a vertex that is not in the "
                                     "file; it is skipped");
                continue;
            }

            for (int k = 0; k < 3; ++k) {
                const uint16_t position = triangle.Indices[k];
                // Normals and texture coordinates are per corner; positions are
                // shared. Two corners are the same document vertex only when
                // all three agree, bit for bit - rounding here would weld
                // corners the file deliberately kept apart across a UV seam.
                const uint64_t uvKey = PackFloats(triangle.S[k], triangle.T[k]);
                const uint64_t normalKey = PackNormal(triangle.Normals[k]);

                const auto key = std::make_tuple(position, uvKey, normalKey);
                auto found = unique.find(key);
                if (found == unique.end()) {
                    const uint32_t index = static_cast<uint32_t>(prim.Positions.size());
                    const Vertex& vertex = file_->Vertices[position];
                    prim.Positions.emplace_back(vertex.Position[0], vertex.Position[1],
                                                vertex.Position[2]);
                    prim.Normals.emplace_back(triangle.Normals[k][0], triangle.Normals[k][1],
                                              triangle.Normals[k][2]);
                    texcoords.emplace_back(triangle.S[k], triangle.T[k]);
                    boneOf.push_back(position);
                    found = unique.emplace(key, index).first;
                }
                prim.Indices.push_back(found->second);
            }

            // MilkShape numbers smoothing groups 1..32, with 0 meaning none.
            // The document holds a bitmask, so the number is the bit.
            prim.SmoothingGroups.push_back(
                    triangle.SmoothingGroup == 0
                            ? 0u
                            : (1u << ((triangle.SmoothingGroup - 1) & 31)));
        }

        if (prim.Indices.empty()) return;

        VertexAttribute uv;
        uv.Semantic = AttributeSemantic::TexCoord;
        uv.Name = "TEXCOORD_0";
        uv.Components = 2;
        uv.Values.reserve(texcoords.size() * 2);
        for (const auto& value : texcoords) {
            uv.Values.push_back(value.first);
            uv.Values.push_back(value.second);
        }
        prim.Attributes.push_back(std::move(uv));

        AddSkinning(prim, boneOf);

        if (group.Material >= 0 &&
            static_cast<size_t>(group.Material) < document_->Materials.size())
            prim.Material = group.Material;

        ModelMesh mesh;
        mesh.Name = group.Name;
        if (mesh.Name.empty()) {
            auto comment = file_->GroupComments.find(groupIndex);
            mesh.Name = comment == file_->GroupComments.end() ? "Mesh" : comment->second;
        }
        prim.Name = mesh.Name;

        // Every triangle in the group already carries the same smoothing
        // information the file stated, so the normals stored per corner are
        // used as they are. MilkShape writes them; nothing has to guess.
        mesh.Primitives.push_back(std::move(prim));

        const int meshIndex = document_->AddMesh(std::move(mesh));
        ModelNode node;
        node.Name = document_->Meshes[static_cast<size_t>(meshIndex)].Name;
        node.Mesh = meshIndex;
        if (!document_->Skins.empty()) node.Skin = 0;
        document_->AddNode(std::move(node), -1);
    }

    // Bone influences, from whichever of the format's two skinning records the
    // file carries. The optional block holds three bones and three weights per
    // vertex; the vertex's own `boneId` is the fourth, and the one every file
    // has. A weight byte is a percentage, and the first bone's share is
    // whatever the other three leave.
    void AddSkinning(MeshPrimitive& prim, const std::vector<int>& sourceVertex) {
        if (file_->Joints.empty()) return;

        bool any = false;
        VertexAttribute joints;
        joints.Semantic = AttributeSemantic::Joints;
        joints.Name = "JOINTS_0";
        joints.Components = 4;
        VertexAttribute weights;
        weights.Semantic = AttributeSemantic::Weights;
        weights.Name = "WEIGHTS_0";
        weights.Components = 4;

        for (int source : sourceVertex) {
            float bone[4] = {0, 0, 0, 0};
            float weight[4] = {0, 0, 0, 0};

            const int8_t primary = file_->Vertices[static_cast<size_t>(source)].BoneId;
            if (primary >= 0 && static_cast<size_t>(primary) < file_->Joints.size()) {
                bone[0] = static_cast<float>(primary);
                weight[0] = 1.0f;
                any = true;
            }

            if (static_cast<size_t>(source) < file_->Weights.size()) {
                const VertexWeights& extra = file_->Weights[static_cast<size_t>(source)];
                float shared = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    if (extra.Bones[k] < 0 ||
                        static_cast<size_t>(extra.Bones[k]) >= file_->Joints.size())
                        continue;
                    const float share = static_cast<float>(extra.Weights[k]) / 100.0f;
                    if (share <= 0.0f) continue;
                    bone[k + 1] = static_cast<float>(extra.Bones[k]);
                    weight[k + 1] = share;
                    shared += share;
                    any = true;
                }
                if (weight[0] > 0.0f) weight[0] = std::max(0.0f, 1.0f - shared);
            }

            for (int k = 0; k < 4; ++k) {
                joints.Values.push_back(bone[k]);
                weights.Values.push_back(weight[k]);
            }
        }

        if (!any) return;
        prim.Attributes.push_back(std::move(joints));
        prim.Attributes.push_back(std::move(weights));
    }

    // One animation holding every joint's channels. MS3D has no notion of
    // several clips - `iTotalFrames` and `fAnimationFPS` describe one timeline -
    // so one ModelAnimation is what the file actually says.
    void BuildAnimation() {
        ModelAnimation animation;
        animation.Name = "Take";

        for (const Joint& joint : joints_) {
            if (joint.Node < 0) continue;
            if (!joint.TranslationKeys.empty())
                AddChannel(animation, joint, AnimationPath::Translation);
            if (!joint.RotationKeys.empty())
                AddChannel(animation, joint, AnimationPath::Rotation);
        }
        if (animation.Channels.empty()) return;

        if (file_->TotalFrames > 0 && file_->FramesPerSecond > 0.0f)
            document_->Metadata["ms3d.duration"] =
                    std::to_string(static_cast<double>(file_->TotalFrames) /
                                   static_cast<double>(file_->FramesPerSecond));
        document_->Animations.push_back(std::move(animation));
    }

    void AddChannel(ModelAnimation& animation, const Joint& joint, AnimationPath path) {
        const std::vector<Keyframe>& keys =
                path == AnimationPath::Rotation ? joint.RotationKeys : joint.TranslationKeys;

        AnimationSampler sampler;
        sampler.Interpolate = Interpolation::Linear;
        sampler.Times.reserve(keys.size());

        for (const Keyframe& key : keys) {
            sampler.Times.push_back(key.Time);
            if (path == AnimationPath::Rotation) {
                // A key is an absolute Euler orientation, not a delta from the
                // joint's rest pose - MilkShape's own viewer composes the rest
                // rotation with it, so the two are multiplied here rather than
                // the key replacing the rest.
                const Quatd rest = EulerXYZ(joint.Rotation[0], joint.Rotation[1],
                                            joint.Rotation[2]);
                const Quatd turn = EulerXYZ(key.Value[0], key.Value[1], key.Value[2]);
                const Quatd composed = rest * turn;
                sampler.Values.push_back(static_cast<float>(composed.x));
                sampler.Values.push_back(static_cast<float>(composed.y));
                sampler.Values.push_back(static_cast<float>(composed.z));
                sampler.Values.push_back(static_cast<float>(composed.w));
            } else {
                // A translation key is likewise relative to the rest position.
                sampler.Values.push_back(joint.Position[0] + key.Value[0]);
                sampler.Values.push_back(joint.Position[1] + key.Value[1]);
                sampler.Values.push_back(joint.Position[2] + key.Value[2]);
            }
        }

        AnimationChannel channel;
        channel.TargetNode = joint.Node;
        channel.Path = path;
        channel.Sampler = static_cast<int>(animation.Samplers.size());
        animation.Samplers.push_back(std::move(sampler));
        animation.Channels.push_back(channel);
    }

    static uint64_t PackFloats(float a, float b) {
        uint32_t x = 0, y = 0;
        std::memcpy(&x, &a, 4);
        std::memcpy(&y, &b, 4);
        return (static_cast<uint64_t>(x) << 32) | y;
    }

    static uint64_t PackNormal(const float normal[3]) {
        uint32_t x = 0, y = 0, z = 0;
        std::memcpy(&x, &normal[0], 4);
        std::memcpy(&y, &normal[1], 4);
        std::memcpy(&z, &normal[2], 4);
        // Three 32-bit values folded into 64 bits. A collision would weld two
        // corners that differ only in normal, which is why the position and UV
        // stay exact in the key beside it.
        return (static_cast<uint64_t>(x) << 32) ^ (static_cast<uint64_t>(y) << 16) ^ z;
    }

    void WarnOnce(const std::string& key, const std::string& message) {
        if (warned_.insert(key).second) options_.Warn(message);
    }

    const ConversionOptions& options_;
    const File* file_ = nullptr;
    std::shared_ptr<ModelDocument> document_;
    std::vector<Joint> joints_;
    std::map<std::string, int> imageByPath_;
    std::set<std::string> warned_;
};

bool LooksLikeMS3D(const std::vector<uint8_t>& data) {
    return data.size() >= sizeof(kMagic) &&
           std::memcmp(data.data(), kMagic, sizeof(kMagic)) == 0;
}

} // namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities MS3DConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.Meshes = true;
    caps.SceneGraph = true;         // the joint hierarchy, and a node per group
    caps.Materials = true;
    caps.Textures = true;
    caps.TextureCoordinates = true;
    caps.Normals = true;            // stored per corner, not generated
    caps.Animations = true;         // joint rotation and translation keyframes
    caps.Skinning = true;           // boneId, and the optional weight block
    caps.Metadata = true;
    // Deliberately false, each for a stated reason:
    //   NGons - the format stores triangles and nothing else.
    //   VertexColors - MilkShape has none.
    //   MorphTargets - the format has no shape keys.
    //   Units, UpAxis - it states neither; Y-up is a convention, not a field.
    //   PBRMaterials - its material is fixed-function.
    //   Instancing - a group's triangles belong to it alone.
    //   DoublePrecision - every number in the file is a 32-bit float.
    //   Cameras, Lights - the format has none.
    return caps;
}

std::shared_ptr<ModelStorage::ModelDocument> MS3DConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    File file;
    if (!Parse(data, file, options)) return nullptr;
    Builder builder(options);
    return builder.Build(file);
}

std::shared_ptr<ModelStorage::ModelDocument> MS3DConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    std::ifstream stream(filename, std::ios::binary);
    if (!stream) {
        options.Warn("MS3D: cannot open " + filename);
        return nullptr;
    }
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                                    std::istreambuf_iterator<char>());
    auto document = ImportFromMemory(data, options);
    if (document && document->Title.empty()) {
        const size_t slash = filename.find_last_of("/\\");
        std::string stem = slash == std::string::npos ? filename : filename.substr(slash + 1);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos && dot > 0) stem.erase(dot);
        document->Title = stem;
    }
    return document;
}

std::shared_ptr<ModelStorage::ModelDocument> MS3DConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                                    std::istreambuf_iterator<char>());
    return ImportFromMemory(data, options);
}

bool MS3DConverter::ValidateData(const std::vector<uint8_t>& data) const {
    return LooksLikeMS3D(data);
}

bool MS3DConverter::ValidateFile(const std::string& filename) const {
    std::ifstream stream(filename, std::ios::binary);
    if (!stream) return false;
    std::vector<uint8_t> head(sizeof(kMagic));
    stream.read(reinterpret_cast<char*>(head.data()),
                static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(std::max<std::streamsize>(0, stream.gcount())));
    return LooksLikeMS3D(head);
}

} // namespace ModelConverter
} // namespace UltraCanvas
