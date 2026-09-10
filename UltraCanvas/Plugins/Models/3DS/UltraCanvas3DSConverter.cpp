// Plugins/Models/3DS/UltraCanvas3DSConverter.cpp
// The 3DS chunk reader.
//
// Every read goes through Cursor, which bounds-checks against the enclosing
// chunk rather than the file: a truncated or hostile file yields a short read
// and a warning, never an out-of-bounds access. Chunk lengths are treated as
// untrusted — a length that overruns its parent ends that parent's scan.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/3DS/UltraCanvas3DSConverter.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;

namespace {

// ===== CHUNK IDS =====
// Only the ones this reader acts on; anything else is skipped by length.
enum : uint16_t {
    CHUNK_MAIN            = 0x4D4D,
    CHUNK_M3D_VERSION     = 0x0002,
    CHUNK_MDATA           = 0x3D3D,
    CHUNK_MESH_VERSION    = 0x3D3E,
    CHUNK_MASTER_SCALE    = 0x0100,

    CHUNK_COLOR_F         = 0x0010,
    CHUNK_COLOR_24        = 0x0011,
    CHUNK_LIN_COLOR_24    = 0x0012,
    CHUNK_LIN_COLOR_F     = 0x0013,
    CHUNK_INT_PERCENTAGE  = 0x0030,
    CHUNK_FLOAT_PERCENT   = 0x0031,

    CHUNK_MAT_ENTRY       = 0xAFFF,
    CHUNK_MAT_NAME        = 0xA000,
    CHUNK_MAT_AMBIENT     = 0xA010,
    CHUNK_MAT_DIFFUSE     = 0xA020,
    CHUNK_MAT_SPECULAR    = 0xA030,
    CHUNK_MAT_SHININESS   = 0xA040,
    CHUNK_MAT_SHIN2PCT    = 0xA041,
    CHUNK_MAT_TRANSPARENCY= 0xA050,
    CHUNK_MAT_TWO_SIDE    = 0xA081,
    CHUNK_MAT_SHADING     = 0xA100,
    CHUNK_MAT_TEXMAP      = 0xA200,
    CHUNK_MAT_SPECMAP     = 0xA204,
    CHUNK_MAT_OPACMAP     = 0xA210,
    CHUNK_MAT_REFLMAP     = 0xA220,
    CHUNK_MAT_BUMPMAP     = 0xA230,
    CHUNK_MAT_MAPNAME     = 0xA300,
    CHUNK_MAT_MAP_USCALE  = 0xA354,
    CHUNK_MAT_MAP_VSCALE  = 0xA356,
    CHUNK_MAT_MAP_UOFFSET = 0xA358,
    CHUNK_MAT_MAP_VOFFSET = 0xA35A,

    CHUNK_NAMED_OBJECT    = 0x4000,
    CHUNK_N_TRI_OBJECT    = 0x4100,
    CHUNK_POINT_ARRAY     = 0x4110,
    CHUNK_FACE_ARRAY      = 0x4120,
    CHUNK_MSH_MAT_GROUP   = 0x4130,
    CHUNK_TEX_VERTS       = 0x4140,
    CHUNK_SMOOTH_GROUP    = 0x4150,
    CHUNK_MESH_MATRIX     = 0x4160,
    CHUNK_N_DIRECT_LIGHT  = 0x4600,
    CHUNK_DL_SPOTLIGHT    = 0x4610,
    CHUNK_DL_OFF          = 0x4620,
    CHUNK_DL_MULTIPLIER   = 0x465B,
    CHUNK_N_CAMERA        = 0x4700,

    CHUNK_KFDATA          = 0xB000
};

constexpr size_t kChunkHeaderSize = 6;

// A bounds-checked view of one chunk's payload. Every accessor refuses to read
// past `end`, so a malformed length can shorten a read but never escape the
// buffer.
class Cursor {
public:
    Cursor(const uint8_t* data, size_t begin, size_t end)
            : data_(data), pos_(begin), end_(end) {}

    size_t Position() const { return pos_; }
    size_t End() const { return end_; }
    size_t Remaining() const { return pos_ < end_ ? end_ - pos_ : 0; }
    bool Has(size_t bytes) const { return Remaining() >= bytes; }
    void Seek(size_t pos) { pos_ = std::min(pos, end_); }

    uint8_t U8()   { uint8_t v = 0;  Read(&v, 1); return v; }
    uint16_t U16() { uint16_t v = 0; Read(&v, 2); return v; }
    uint32_t U32() { uint32_t v = 0; Read(&v, 4); return v; }
    int16_t  I16() { int16_t v = 0;  Read(&v, 2); return v; }
    float    F32() { float v = 0.0f; Read(&v, 4); return v; }

    // A NUL-terminated name. 3DS caps these at 12 bytes in practice but the
    // terminator is what actually delimits them; an unterminated run stops at
    // the chunk end.
    std::string Name() {
        std::string out;
        while (pos_ < end_) {
            const char c = static_cast<char>(data_[pos_++]);
            if (c == '\0') return out;
            out.push_back(c);
        }
        return out;
    }

    // Reads the next chunk header. False at the end of the payload, or when
    // the declared length cannot fit — which ends the scan rather than
    // trusting it.
    bool NextChunk(uint16_t& id, size_t& bodyBegin, size_t& chunkEnd) {
        if (!Has(kChunkHeaderSize)) return false;
        const size_t start = pos_;
        id = U16();
        const uint32_t length = U32();
        if (length < kChunkHeaderSize) return false;
        if (start + length > end_) return false;
        bodyBegin = start + kChunkHeaderSize;
        chunkEnd = start + length;
        pos_ = chunkEnd;
        return true;
    }

private:
    template <typename T>
    void Read(T* out, size_t bytes) {
        if (!Has(bytes)) { pos_ = end_; return; }
        std::memcpy(out, data_ + pos_, bytes);
        pos_ += bytes;
    }

    const uint8_t* data_;
    size_t pos_;
    size_t end_;
};

// ===== SHARED SUB-CHUNK READERS =====

// A colour is stored as a sub-chunk, in one of four encodings. The linear
// variants (0x0012 / 0x0013) are what a renderer wants; gamma is the fallback
// when a writer emitted only that.
bool ReadColor(const uint8_t* data, size_t begin, size_t end, Vec3f& out) {
    Cursor c(data, begin, end);
    bool found = false;
    bool linear = false;

    uint16_t id; size_t body, chunkEnd;
    while (c.NextChunk(id, body, chunkEnd)) {
        Cursor v(data, body, chunkEnd);
        Vec3f value;
        bool isLinear = false;
        switch (id) {
            case CHUNK_LIN_COLOR_24: isLinear = true; [[fallthrough]];
            case CHUNK_COLOR_24: {
                const float r = v.U8() / 255.0f, g = v.U8() / 255.0f, b = v.U8() / 255.0f;
                value = Vec3f(r, g, b);
                break;
            }
            case CHUNK_LIN_COLOR_F: isLinear = true; [[fallthrough]];
            case CHUNK_COLOR_F: {
                value = Vec3f(v.F32(), v.F32(), v.F32());
                break;
            }
            default: continue;
        }
        // Prefer a linear encoding; otherwise take the first one seen.
        if (!found || (isLinear && !linear)) {
            out = value;
            found = true;
            linear = isLinear;
        }
    }
    return found;
}

// A percentage is likewise a sub-chunk: int16 0..100, or a float 0..1.
bool ReadPercentage(const uint8_t* data, size_t begin, size_t end, float& out) {
    Cursor c(data, begin, end);
    uint16_t id; size_t body, chunkEnd;
    while (c.NextChunk(id, body, chunkEnd)) {
        Cursor v(data, body, chunkEnd);
        if (id == CHUNK_INT_PERCENTAGE) { out = v.I16() / 100.0f; return true; }
        if (id == CHUNK_FLOAT_PERCENT)  { out = v.F32(); return true; }
    }
    return false;
}

// ===== READER =====

class Reader {
public:
    Reader(const std::vector<uint8_t>& data, const ConversionOptions& options)
            : data_(data.data()), size_(data.size()), options_(options) {}

    std::shared_ptr<ModelDocument> Run() {
        auto document = std::make_shared<ModelDocument>();
        document->SourceFormat = "3ds";
        document->Generator = "3D Studio";
        // 3DS is Z-up and right-handed, and states no physical unit: a "unit"
        // is whatever the modeller worked in. MASTER_SCALE, when present, is a
        // display scale factor, not a unit — it is recorded as metadata rather
        // than pretended into SourceUnit.
        document->Up = UpAxis::ZUp;
        document->Chirality = Handedness::RightHanded;
        document->SourceUnit = options_.AssumeUnit;
        document->UnitScaleToMeters = MetersPerUnit(options_.AssumeUnit);

        Cursor top(data_, 0, size_);
        uint16_t id; size_t body, chunkEnd;
        if (!top.NextChunk(id, body, chunkEnd) || id != CHUNK_MAIN) {
            options_.Warn("3DS: no MAIN3DS chunk — not a 3D Studio file");
            return nullptr;
        }
        ReadMain(body, chunkEnd, *document);

        if (document->Meshes.empty()) {
            options_.Warn("3DS: file contains no mesh objects");
            return nullptr;
        }
        Finish(*document);
        return document;
    }

private:
    // --- top level ---

    void ReadMain(size_t begin, size_t end, ModelDocument& doc) {
        Cursor c(data_, begin, end);
        uint16_t id; size_t body, chunkEnd;
        while (c.NextChunk(id, body, chunkEnd)) {
            switch (id) {
                case CHUNK_M3D_VERSION: {
                    Cursor v(data_, body, chunkEnd);
                    const uint32_t version = v.U32();
                    doc.Metadata["3ds.version"] = std::to_string(version);
                    if (version < 3)
                        options_.Warn("3DS: file version " + std::to_string(version) +
                                      " predates release 3; reading it as release 3");
                    break;
                }
                case CHUNK_MDATA:
                    ReadMData(body, chunkEnd, doc);
                    break;
                case CHUNK_KFDATA:
                    // The keyframer section: node hierarchy, pivots and
                    // position/rotation/scale tracks. Not read yet — saying so
                    // is the contract, since a scene with a hierarchy would
                    // otherwise arrive silently flattened.
                    options_.Warn("3DS: KFDATA present — node hierarchy, pivots and animation "
                                  "tracks are not imported; objects arrive as siblings");
                    break;
                default:
                    break;
            }
        }
    }

    void ReadMData(size_t begin, size_t end, ModelDocument& doc) {
        Cursor c(data_, begin, end);
        uint16_t id; size_t body, chunkEnd;
        while (c.NextChunk(id, body, chunkEnd)) {
            switch (id) {
                case CHUNK_MESH_VERSION: {
                    Cursor v(data_, body, chunkEnd);
                    doc.Metadata["3ds.meshVersion"] = std::to_string(v.U32());
                    break;
                }
                case CHUNK_MASTER_SCALE: {
                    Cursor v(data_, body, chunkEnd);
                    const float scale = v.F32();
                    if (scale != 0.0f && scale != 1.0f) {
                        std::ostringstream text;
                        text << scale;
                        doc.Metadata["3ds.masterScale"] = text.str();
                        options_.Warn("3DS: MASTER_SCALE " + text.str() +
                                      " recorded as metadata; geometry is not rescaled");
                    }
                    break;
                }
                case CHUNK_MAT_ENTRY:
                    ReadMaterial(body, chunkEnd, doc);
                    break;
                case CHUNK_NAMED_OBJECT:
                    ReadNamedObject(body, chunkEnd, doc);
                    break;
                default:
                    break;
            }
        }
    }

    // --- materials ---

    void ReadMaterial(size_t begin, size_t end, ModelDocument& doc) {
        ModelMaterial material;
        PhongParams phong;
        bool haveShininess = false;
        float shininess = 0.0f;
        float shininessStrength = 1.0f;
        float transparency = 0.0f;

        Cursor c(data_, begin, end);
        uint16_t id; size_t body, chunkEnd;
        while (c.NextChunk(id, body, chunkEnd)) {
            switch (id) {
                case CHUNK_MAT_NAME: {
                    Cursor v(data_, body, chunkEnd);
                    material.Name = v.Name();
                    break;
                }
                case CHUNK_MAT_AMBIENT:  ReadColor(data_, body, chunkEnd, phong.Ambient); break;
                case CHUNK_MAT_DIFFUSE:  ReadColor(data_, body, chunkEnd, phong.Diffuse); break;
                case CHUNK_MAT_SPECULAR: ReadColor(data_, body, chunkEnd, phong.Specular); break;
                case CHUNK_MAT_SHININESS:
                    haveShininess = ReadPercentage(data_, body, chunkEnd, shininess);
                    break;
                case CHUNK_MAT_SHIN2PCT:
                    ReadPercentage(data_, body, chunkEnd, shininessStrength);
                    break;
                case CHUNK_MAT_TRANSPARENCY:
                    ReadPercentage(data_, body, chunkEnd, transparency);
                    break;
                case CHUNK_MAT_TWO_SIDE:
                    material.DoubleSided = true;
                    break;
                case CHUNK_MAT_SHADING: {
                    Cursor v(data_, body, chunkEnd);
                    const int16_t shading = v.I16();
                    // 0 wireframe, 1 flat, 2 Gouraud, 3 Phong, 4 metal.
                    material.Extras["3ds.shading"] = std::to_string(shading);
                    phong.IlluminationModel = shading >= 3 ? 3 : 2;
                    break;
                }
                case CHUNK_MAT_TEXMAP:
                    phong.DiffuseTexture = ReadTextureMap(body, chunkEnd, doc, material.Name, "diffuse");
                    break;
                case CHUNK_MAT_SPECMAP:
                    phong.SpecularTexture = ReadTextureMap(body, chunkEnd, doc, material.Name, "specular");
                    break;
                case CHUNK_MAT_BUMPMAP:
                    material.NormalTexture = ReadTextureMap(body, chunkEnd, doc, material.Name, "bump");
                    break;
                case CHUNK_MAT_OPACMAP: {
                    ReadTextureMap(body, chunkEnd, doc, material.Name, "opacity");
                    options_.Warn("3DS: material '" + material.Name +
                                  "' has an opacity map; the document has no opacity-map slot, "
                                  "the image is kept but unreferenced");
                    break;
                }
                case CHUNK_MAT_REFLMAP: {
                    ReadTextureMap(body, chunkEnd, doc, material.Name, "reflection");
                    options_.Warn("3DS: material '" + material.Name +
                                  "' has a reflection map; the document has no reflection slot, "
                                  "the image is kept but unreferenced");
                    break;
                }
                default:
                    break;
            }
        }

        // 3DS shininess is a 0..1 percentage; MTL/Phong shininess is an
        // exponent. 128 is the conventional maximum for the fixed-function
        // pipeline this material model came from.
        phong.Shininess = haveShininess ? shininess * 128.0f : 0.0f;
        if (shininessStrength != 1.0f)
            material.Extras["3ds.shininessStrength"] = std::to_string(shininessStrength);

        material.Phong = phong;
        material.BaseColorFactor = Vec4f(phong.Diffuse.x, phong.Diffuse.y, phong.Diffuse.z,
                                         1.0f - transparency);
        if (transparency > 0.0f) material.Alpha = AlphaMode::Blend;

        // Fills the metallic-roughness side from the Phong side.
        material.DeriveMissingModel();

        materialIndexByName_[material.Name] = static_cast<int>(doc.Materials.size());
        doc.Materials.push_back(std::move(material));
    }

    // A map sub-chunk: a file name plus UV scale and offset. Returns a
    // TextureRef pointing at a ModelImage carrying the name; the bytes are
    // loaded only when the caller asked for it.
    TextureRef ReadTextureMap(size_t begin, size_t end, ModelDocument& doc,
                              const std::string& materialName, const char* slot) {
        TextureRef ref;
        std::string fileName;
        int extraNames = 0;

        Cursor c(data_, begin, end);
        uint16_t id; size_t body, chunkEnd;
        while (c.NextChunk(id, body, chunkEnd)) {
            Cursor v(data_, body, chunkEnd);
            switch (id) {
                case CHUNK_MAT_MAPNAME: {
                    const std::string name = v.Name();
                    // Some exporters stack several maps into one slot. The
                    // document has one image per slot, so the first wins and
                    // the rest are reported.
                    if (fileName.empty()) fileName = name;
                    else ++extraNames;
                    break;
                }
                case CHUNK_MAT_MAP_USCALE:  ref.ScaleU = v.F32(); break;
                case CHUNK_MAT_MAP_VSCALE:  ref.ScaleV = v.F32(); break;
                case CHUNK_MAT_MAP_UOFFSET: ref.OffsetU = v.F32(); break;
                case CHUNK_MAT_MAP_VOFFSET: ref.OffsetV = v.F32(); break;
                default: break;
            }
        }

        if (fileName.empty()) return ref;
        if (extraNames > 0) {
            options_.Warn("3DS: material '" + materialName + "' stacks " +
                          std::to_string(extraNames + 1) + " images in its " + slot +
                          " slot; using '" + fileName + "' and dropping the rest");
        }
        // 3DS truncates map names to 8.3 (12 characters with the dot), so a
        // name at the limit is very likely cut off and will not resolve.
        if (fileName.size() >= 12) {
            options_.Warn("3DS: texture name '" + fileName +
                          "' is at the format's 12-character limit and is probably truncated");
        }

        ref.Image = AddImage(doc, fileName);
        return ref;
    }

    int AddImage(ModelDocument& doc, const std::string& fileName) {
        auto it = imageIndexByName_.find(fileName);
        if (it != imageIndexByName_.end()) return it->second;

        ModelImage image;
        image.Name = fileName;
        image.Uri = fileName;
        const size_t dot = fileName.rfind('.');
        if (dot != std::string::npos) {
            std::string ext = fileName.substr(dot + 1);
            for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (ext == "jpg" || ext == "jpeg") image.MimeType = "image/jpeg";
            else if (ext == "png") image.MimeType = "image/png";
            else if (ext == "tga") image.MimeType = "image/x-tga";
            else if (ext == "bmp") image.MimeType = "image/bmp";
        }

        const int index = static_cast<int>(doc.Images.size());
        doc.Images.push_back(std::move(image));
        imageIndexByName_[fileName] = index;
        return index;
    }

    // --- objects ---

    void ReadNamedObject(size_t begin, size_t end, ModelDocument& doc) {
        Cursor c(data_, begin, end);
        const std::string name = c.Name();

        uint16_t id; size_t body, chunkEnd;
        while (c.NextChunk(id, body, chunkEnd)) {
            switch (id) {
                case CHUNK_N_TRI_OBJECT: ReadTriObject(body, chunkEnd, doc, name); break;
                case CHUNK_N_CAMERA:     ReadCamera(body, chunkEnd, doc, name); break;
                case CHUNK_N_DIRECT_LIGHT: ReadLight(body, chunkEnd, doc, name); break;
                default: break;
            }
        }
    }

    void ReadTriObject(size_t begin, size_t end, ModelDocument& doc, const std::string& name) {
        std::vector<Vec3d> positions;
        std::vector<float> uvs;                  // 2 per vertex
        std::vector<uint32_t> faces;             // 3 per face
        std::vector<std::pair<std::string, std::vector<uint32_t>>> materialGroups;
        Matrix4x4 objectMatrix = Matrix4x4::Identity();
        bool haveMatrix = false;

        Cursor c(data_, begin, end);
        uint16_t id; size_t body, chunkEnd;
        while (c.NextChunk(id, body, chunkEnd)) {
            switch (id) {
                case CHUNK_POINT_ARRAY: {
                    Cursor v(data_, body, chunkEnd);
                    const uint16_t count = v.U16();
                    positions.reserve(count);
                    for (uint16_t i = 0; i < count && v.Has(12); ++i) {
                        const double x = v.F32(), y = v.F32(), z = v.F32();
                        positions.emplace_back(x, y, z);
                    }
                    if (positions.size() != count)
                        options_.Warn("3DS: object '" + name + "' declares " + std::to_string(count) +
                                      " vertices but the chunk holds " + std::to_string(positions.size()));
                    break;
                }
                case CHUNK_TEX_VERTS: {
                    Cursor v(data_, body, chunkEnd);
                    const uint16_t count = v.U16();
                    uvs.reserve(static_cast<size_t>(count) * 2);
                    for (uint16_t i = 0; i < count && v.Has(8); ++i) {
                        uvs.push_back(v.F32());
                        uvs.push_back(v.F32());
                    }
                    break;
                }
                case CHUNK_FACE_ARRAY: {
                    Cursor v(data_, body, chunkEnd);
                    const uint16_t count = v.U16();
                    faces.reserve(static_cast<size_t>(count) * 3);
                    for (uint16_t i = 0; i < count && v.Has(8); ++i) {
                        const uint16_t a = v.U16(), b = v.U16(), cc = v.U16();
                        v.U16();   // edge-visibility flags: a modelling aid, not geometry
                        faces.push_back(a);
                        faces.push_back(b);
                        faces.push_back(cc);
                    }
                    // Sub-chunks follow the face data inside the same chunk.
                    ReadFaceSubChunks(v.Position(), chunkEnd, name, materialGroups);
                    break;
                }
                case CHUNK_MESH_MATRIX: {
                    Cursor v(data_, body, chunkEnd);
                    // Three basis vectors then the origin, row-major in the
                    // file; stored column-major here.
                    float m[12];
                    for (float& value : m) value = v.F32();
                    objectMatrix = Matrix4x4::Identity();
                    objectMatrix.m[0] = m[0];  objectMatrix.m[1] = m[1];  objectMatrix.m[2]  = m[2];
                    objectMatrix.m[4] = m[3];  objectMatrix.m[5] = m[4];  objectMatrix.m[6]  = m[5];
                    objectMatrix.m[8] = m[6];  objectMatrix.m[9] = m[7];  objectMatrix.m[10] = m[8];
                    objectMatrix.m[12] = m[9]; objectMatrix.m[13] = m[10]; objectMatrix.m[14] = m[11];
                    haveMatrix = true;
                    break;
                }
                default:
                    break;
            }
        }

        if (positions.empty() || faces.empty()) {
            options_.Warn("3DS: object '" + name + "' has no geometry and was skipped");
            return;
        }
        if (!uvs.empty() && uvs.size() / 2 != positions.size()) {
            options_.Warn("3DS: object '" + name + "' has " + std::to_string(uvs.size() / 2) +
                          " texture coordinates for " + std::to_string(positions.size()) +
                          " vertices; dropping them");
            uvs.clear();
        }

        // 3DS stores vertices already in world space and MESH_MATRIX as the
        // object's own coordinate system. Putting the matrix on the node and
        // the inverse-transformed vertices in the mesh keeps the object's local
        // frame — its pivot and orientation — as real structure, and composes
        // back to exactly the world positions the file held.
        Matrix4x4 nodeTransform = Matrix4x4::Identity();
        if (haveMatrix && !objectMatrix.IsIdentity(1e-9)) {
            Matrix4x4 inverse;
            if (objectMatrix.InverseAffine(inverse)) {
                for (auto& p : positions) p = inverse.TransformPoint(p);
                nodeTransform = objectMatrix;
            } else {
                options_.Warn("3DS: object '" + name + "' has a singular MESH_MATRIX; "
                              "keeping world-space vertices and dropping the object frame");
            }
        }

        ModelMesh mesh;
        mesh.Name = name;
        BuildPrimitives(name, positions, uvs, faces, materialGroups, mesh);
        if (mesh.Primitives.empty()) return;

        ModelNode node;
        node.Name = name;
        node.Mesh = doc.AddMesh(std::move(mesh));
        if (!nodeTransform.IsIdentity(1e-12)) {
            Vec3d translation, scale;
            Quatd rotation;
            // Prefer TRS when the matrix is a clean rigid transform: it is what
            // scene formats write, and it stays editable.
            if (nodeTransform.DecomposeTRS(translation, rotation, scale)) {
                node.Translation = translation;
                node.Rotation = rotation;
                node.Scale = scale;
            } else {
                node.Matrix = nodeTransform;
            }
        }
        doc.AddNode(std::move(node));
    }

    void ReadFaceSubChunks(size_t begin, size_t end, const std::string& objectName,
                           std::vector<std::pair<std::string, std::vector<uint32_t>>>& groups) {
        Cursor c(data_, begin, end);
        uint16_t id; size_t body, chunkEnd;
        while (c.NextChunk(id, body, chunkEnd)) {
            Cursor v(data_, body, chunkEnd);
            if (id == CHUNK_MSH_MAT_GROUP) {
                const std::string materialName = v.Name();
                const uint16_t count = v.U16();
                std::vector<uint32_t> faceIndices;
                faceIndices.reserve(count);
                for (uint16_t i = 0; i < count && v.Has(2); ++i) faceIndices.push_back(v.U16());
                groups.emplace_back(materialName, std::move(faceIndices));
            } else if (id == CHUNK_SMOOTH_GROUP) {
                // One 32-bit smoothing-group mask per face. The document
                // carries normals, not smoothing groups, so these are resolved
                // by RecomputeNormals and not round-tripped — recorded in the
                // proposal as a known gap rather than silently ignored.
                (void)objectName;
            }
        }
    }

    // Split the object's faces into one primitive per material, remapping each
    // primitive's vertices so it carries only what it uses.
    void BuildPrimitives(const std::string& objectName,
                         const std::vector<Vec3d>& positions,
                         const std::vector<float>& uvs,
                         const std::vector<uint32_t>& faces,
                         const std::vector<std::pair<std::string, std::vector<uint32_t>>>& groups,
                         ModelMesh& mesh) {
        const size_t faceCount = faces.size() / 3;

        // Which material each face belongs to; -1 for faces no group claims.
        std::vector<int> faceMaterial(faceCount, -1);
        for (const auto& group : groups) {
            auto it = materialIndexByName_.find(group.first);
            if (it == materialIndexByName_.end()) {
                options_.Warn("3DS: object '" + objectName + "' references material '" +
                              group.first + "' which the file does not define");
                continue;
            }
            for (uint32_t face : group.second)
                if (face < faceCount) faceMaterial[face] = it->second;
        }

        // Group faces by material, keeping file order within each group.
        std::map<int, std::vector<uint32_t>> byMaterial;
        for (size_t f = 0; f < faceCount; ++f)
            byMaterial[faceMaterial[f]].push_back(static_cast<uint32_t>(f));

        size_t unassigned = 0;
        for (const auto& entry : byMaterial) {
            const int material = entry.first;
            const std::vector<uint32_t>& faceList = entry.second;
            if (material < 0) unassigned = faceList.size();

            MeshPrimitive prim;
            prim.Name = objectName;
            prim.Mode = PrimitiveMode::Triangles;
            prim.Material = material;

            VertexAttribute texcoord;
            texcoord.Semantic = AttributeSemantic::TexCoord;
            texcoord.Name = "TEXCOORD_0";
            texcoord.Components = 2;

            std::map<uint32_t, uint32_t> remap;
            prim.Indices.reserve(faceList.size() * 3);
            for (uint32_t face : faceList) {
                for (int corner = 0; corner < 3; ++corner) {
                    const uint32_t source = faces[face * 3 + corner];
                    if (source >= positions.size()) continue;   // malformed index
                    auto it = remap.find(source);
                    if (it == remap.end()) {
                        const uint32_t fresh = static_cast<uint32_t>(prim.Positions.size());
                        remap.emplace(source, fresh);
                        prim.Positions.push_back(positions[source]);
                        if (!uvs.empty()) {
                            texcoord.Values.push_back(uvs[source * 2]);
                            texcoord.Values.push_back(uvs[source * 2 + 1]);
                        }
                        prim.Indices.push_back(fresh);
                    } else {
                        prim.Indices.push_back(it->second);
                    }
                }
            }

            if (prim.Positions.empty()) continue;
            if (!texcoord.Values.empty()) prim.Attributes.push_back(std::move(texcoord));
            mesh.Primitives.push_back(std::move(prim));
        }

        if (unassigned > 0 && byMaterial.size() > 1) {
            options_.Warn("3DS: object '" + objectName + "' has " + std::to_string(unassigned) +
                          " faces in no material group; they keep the default material");
        }
    }

    // --- cameras and lights ---

    void ReadCamera(size_t begin, size_t end, ModelDocument& doc, const std::string& name) {
        Cursor v(data_, begin, end);
        const double px = v.F32(), py = v.F32(), pz = v.F32();
        const double tx = v.F32(), ty = v.F32(), tz = v.F32();
        const float bank = v.F32();
        const float lens = v.F32();

        ModelCamera camera;
        camera.Name = name;
        camera.Type = CameraType::Perspective;
        // 3DS states the lens in millimetres on a 36 mm frame.
        camera.YFovRadians = lens > 1.0f
                ? 2.0f * std::atan(12.0f / lens)   // half of a 24 mm frame height
                : 0.8f;
        doc.Cameras.push_back(std::move(camera));

        ModelNode node;
        node.Name = name;
        node.Camera = static_cast<int>(doc.Cameras.size()) - 1;
        node.Translation = Vec3d(px, py, pz);
        // The document has no look-at: a camera is oriented by its node. The
        // target and bank are kept so nothing is lost while the orientation is
        // not yet derived.
        node.Extras["3ds.target"] = std::to_string(tx) + " " + std::to_string(ty) + " " +
                                    std::to_string(tz);
        node.Extras["3ds.bank"] = std::to_string(bank);
        doc.AddNode(std::move(node));

        options_.Warn("3DS: camera '" + name + "' imported at its position; the target and bank "
                      "are kept as node metadata, not resolved into an orientation");
    }

    void ReadLight(size_t begin, size_t end, ModelDocument& doc, const std::string& name) {
        Cursor c(data_, begin, end);
        const double px = c.F32(), py = c.F32(), pz = c.F32();

        ModelLight light;
        light.Name = name;
        light.Type = LightType::Point;

        bool enabled = true;
        uint16_t id; size_t body, chunkEnd;
        // The colour is a sub-chunk of the light itself.
        ReadColor(data_, c.Position(), end, light.Color);
        while (c.NextChunk(id, body, chunkEnd)) {
            Cursor v(data_, body, chunkEnd);
            switch (id) {
                case CHUNK_DL_SPOTLIGHT: {
                    light.Type = LightType::Spot;
                    v.F32(); v.F32(); v.F32();          // target
                    const float hotspot = v.F32();
                    const float falloff = v.F32();
                    light.InnerConeRadians = hotspot * 3.14159265358979f / 360.0f;   // degrees/2
                    light.OuterConeRadians = falloff * 3.14159265358979f / 360.0f;
                    break;
                }
                case CHUNK_DL_MULTIPLIER: light.Intensity = v.F32(); break;
                case CHUNK_DL_OFF:        enabled = false; break;
                default: break;
            }
        }
        if (!enabled) light.Intensity = 0.0f;

        doc.Lights.push_back(std::move(light));

        ModelNode node;
        node.Name = name;
        node.Light = static_cast<int>(doc.Lights.size()) - 1;
        node.Translation = Vec3d(px, py, pz);
        doc.AddNode(std::move(node));
    }

    // --- finishing ---

    void Finish(ModelDocument& doc) {
        // 3DS carries no vertex normals at all: the renderer derived them from
        // smoothing groups. Area-weighted normals from the geometry are the
        // closest the document can get.
        if (options_.GenerateMissingNormals) {
            for (auto& mesh : doc.Meshes)
                for (auto& prim : mesh.Primitives)
                    if (prim.Normals.empty()) prim.RecomputeNormals();
        }
        if (options_.WeldTolerance > 0.0) doc.WeldVertices(options_.WeldTolerance);
        if (options_.TriangulateOnImport) doc.TriangulateAll();
        if (options_.ForceUpAxis.has_value()) doc.ConvertUpAxis(*options_.ForceUpAxis);
    }

    const uint8_t* data_;
    size_t size_;
    const ConversionOptions& options_;
    std::map<std::string, int> materialIndexByName_;
    std::map<std::string, int> imageIndexByName_;
};

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const std::streamoff size = file.tellg();
    if (size <= 0) return false;
    out.resize(static_cast<size_t>(size));
    file.seekg(0);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

} // namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities ThreeDSConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.Meshes = true;
    caps.Materials = true;
    caps.Textures = true;
    caps.TextureCoordinates = true;
    caps.Normals = true;          // derived on import; 3DS stores none
    caps.Cameras = true;
    caps.Lights = true;
    caps.Metadata = true;
    caps.UpAxis = true;           // fixed Z-up by the format
    // Deliberately false: 3DS has n-gons nowhere (faces are triangles), no
    // PBR materials, no skinning or morph targets, no double precision, no
    // per-vertex colours, no physical unit, and this reader does not yet read
    // the KFDATA hierarchy or its animation tracks.
    return caps;
}

std::shared_ptr<ModelStorage::ModelDocument> ThreeDSConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    std::vector<uint8_t> data;
    if (!ReadWholeFile(filename, data)) {
        options.Warn("3DS: cannot read " + filename);
        return nullptr;
    }
    auto document = ImportFromMemory(data, options);
    if (document && document->Title.empty()) {
        // 3DS has no title chunk, so the file name is the only name the model
        // has. Take the stem, whichever separator the platform used.
        const size_t slash = filename.find_last_of("/\\");
        std::string stem = slash == std::string::npos ? filename : filename.substr(slash + 1);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos && dot > 0) stem.erase(dot);
        document->Title = stem;
    }
    return document;
}

std::shared_ptr<ModelStorage::ModelDocument> ThreeDSConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    if (!ValidateData(data)) {
        options.Warn("3DS: data does not start with a MAIN3DS chunk");
        return nullptr;
    }
    Reader reader(data, options);
    return reader.Run();
}

std::shared_ptr<ModelStorage::ModelDocument> ThreeDSConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    return ImportFromMemory(data, options);
}

bool ThreeDSConverter::ValidateData(const std::vector<uint8_t>& data) const {
    if (data.size() < kChunkHeaderSize) return false;
    uint16_t id = 0;
    uint32_t length = 0;
    std::memcpy(&id, data.data(), 2);
    std::memcpy(&length, data.data() + 2, 4);
    // The main chunk's declared length is the file length. Some writers pad,
    // so accept a declared length that fits rather than demanding equality.
    return id == CHUNK_MAIN && length >= kChunkHeaderSize && length <= data.size();
}

bool ThreeDSConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> header(kChunkHeaderSize);
    if (!file.read(reinterpret_cast<char*>(header.data()), kChunkHeaderSize)) return false;

    uint16_t id = 0;
    std::memcpy(&id, header.data(), 2);
    if (id != CHUNK_MAIN) return false;

    uint32_t length = 0;
    std::memcpy(&length, header.data() + 2, 4);
    file.seekg(0, std::ios::end);
    return length >= kChunkHeaderSize &&
           length <= static_cast<uint32_t>(std::max<std::streamoff>(0, file.tellg()));
}

} // namespace ModelConverter
} // namespace UltraCanvas
