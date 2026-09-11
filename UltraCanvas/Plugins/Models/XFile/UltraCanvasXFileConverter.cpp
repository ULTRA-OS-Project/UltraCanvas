// Plugins/Models/XFile/UltraCanvasXFileConverter.cpp
// The DirectX .x reader: Frame hierarchy, meshes, materials.
//
// The awkward parts of this format, and what is done about them:
//
//   * A Frame's matrix is sixteen floats written row-major for row vectors,
//     which is Direct3D's convention. The document stores column-major for
//     column vectors - and those two layouts are the *same sixteen numbers in
//     the same order*, because transposing a matrix and swapping which side the
//     vector multiplies on cancel out. So the matrix copies straight across,
//     translation already in elements 12 to 14. Checked against the sample: the
//     frame chain reproduces its world bounds exactly.
//   * Normals are indexed per face corner and positions per vertex, and the two
//     streams need not agree. Where they do - which is what every Blender
//     export writes - vertices are shared; where they do not, corners are
//     resolved into distinct vertices exactly as the OBJ, COLLADA and X3D
//     readers resolve theirs.
//   * A mesh's faces may use several materials, which the document expresses as
//     several primitives. The split is by first appearance, so a single-material
//     mesh - the common case - stays one primitive with its vertices intact.
//   * Winding is left alone, and then checked. See the header for why: the
//     left-handedness lives in the root frame's reflection, and the file's own
//     MeshNormals are the evidence.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/XFile/UltraCanvasXFileConverter.h"
#include "Models/XFile/UltraCanvasXFile.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;

namespace {

// Everything one Mesh object carries, before it becomes primitives.
struct MeshData {
    std::string Name;
    std::vector<Vec3d> Positions;
    std::vector<std::vector<int>> Faces;

    std::vector<Vec3f> Normals;                  // may be a different length
    std::vector<std::vector<int>> NormalFaces;   // parallel to Faces when present

    std::vector<float> TexCoords;                // 2 per position
    std::vector<float> Colors;                   // 4 per position

    std::vector<int> FaceMaterial;               // per face; empty = one material
    std::vector<int> Materials;                  // document material indices
};

// "array MeshFace faces[n]", where each face is its own corner count followed
// by that many indices. Returns false when the object runs out mid-face, which
// is the shape a truncated file takes.
bool ReadFaceArray(XFile::Cursor& cursor, int count, std::vector<std::vector<int>>& out) {
    out.clear();
    if (count <= 0) return true;
    out.reserve(static_cast<size_t>(count));
    for (int face = 0; face < count; ++face) {
        const int corners = cursor.NextCount(1);
        if (!cursor.Ok()) return false;
        std::vector<int> indices;
        indices.reserve(static_cast<size_t>(corners));
        for (int corner = 0; corner < corners; ++corner) indices.push_back(cursor.NextInt());
        if (!cursor.Ok()) return false;
        out.push_back(std::move(indices));
    }
    return true;
}

double Determinant3x3(const Matrix4x4& matrix) {
    const double* m = matrix.m;
    return m[0] * (m[5] * m[10] - m[6] * m[9]) -
           m[4] * (m[1] * m[10] - m[2] * m[9]) +
           m[8] * (m[1] * m[6] - m[2] * m[5]);
}

// ===== READER =====

class Reader {
public:
    Reader(const XFile::File& file, const ConversionOptions& options)
        : file_(file), options_(options) {}

    std::shared_ptr<ModelDocument> Run() {
        document_ = std::make_shared<ModelDocument>();
        document_->SourceFormat = "x";
        // Direct3D's space is Y-up. The format states neither that nor a unit,
        // so both are convention rather than something the file said - which is
        // why Units and UpAxis are false in the capability report.
        document_->Up = UpAxis::YUp;
        document_->Metadata["x.version"] = std::to_string(file_.MajorVersion) + "." +
                                           std::to_string(file_.MinorVersion);
        document_->Metadata["x.encoding"] = XFile::EncodingName(file_.How);
        document_->Metadata["x.floatBits"] = std::to_string(file_.FloatBits);
        if (options_.AssumeUnit != ModelUnit::Unspecified) {
            document_->SourceUnit = options_.AssumeUnit;
            document_->UnitScaleToMeters = MetersPerUnit(options_.AssumeUnit);
        }

        for (const XFile::Node& root : file_.Roots) ReadTopLevel(root);

        if (document_->Meshes.empty()) {
            options_.Warn("X: no mesh found");
            return nullptr;
        }
        Finish();
        return document_;
    }

private:
    void ReadTopLevel(const XFile::Node& node) {
        if (node.Type == "Frame") {
            ReadFrame(node, -1);
        } else if (node.Type == "Mesh") {
            // A mesh with no frame around it sits at the origin. Legal, and
            // what the smallest hand-written .x files look like.
            ModelNode placement;
            placement.Name = node.Name.empty() ? "Mesh" : node.Name;
            const int index = document_->AddNode(std::move(placement), -1);
            AttachMesh(node, index);
        } else if (node.Type == "Material") {
            MaterialIndexOf(node);   // named, so a MeshMaterialList can reference it
        } else if (node.Type == "Header") {
            if (node.Numbers.size() >= 3)
                document_->Metadata["x.header"] =
                        std::to_string(static_cast<int>(node.Numbers[0])) + "." +
                        std::to_string(static_cast<int>(node.Numbers[1]));
        } else if (node.Type == "AnimationSet" || node.Type == "AnimTicksPerSecond") {
            WarnOnce("animation",
                     "X: <AnimationSet> is not read. The rotation keys are quaternions in a "
                     "convention this reader has no sample to verify against, and a silently "
                     "wrong animation is worse than a missing one; the model arrives in its "
                     "rest pose.");
        } else if (!IsKnownIgnorable(node.Type)) {
            WarnOnce("top." + node.Type, "X: top-level '" + node.Type + "' is not read");
        }
    }

    static bool IsKnownIgnorable(const std::string& type) {
        static const std::set<std::string> ignorable = {
                "template", "Header", "AnimTicksPerSecond", "EffectInstance",
                "EffectParamFloats", "EffectParamString", "EffectParamDWord"};
        return ignorable.count(type) != 0;
    }

    // ----- frames -----

    void ReadFrame(const XFile::Node& frame, int parent) {
        ModelNode node;
        node.Name = frame.Name.empty() ? "Frame" : frame.Name;

        if (const XFile::Node* transform = frame.FindChild("FrameTransformMatrix")) {
            if (transform->Numbers.size() >= 16) {
                Matrix4x4 matrix;
                // Straight copy: Direct3D's row-major-with-row-vectors and the
                // document's column-major-with-column-vectors are the same
                // sixteen numbers in the same order.
                for (int i = 0; i < 16; ++i) matrix.m[i] = transform->Numbers[static_cast<size_t>(i)];

                Vec3d translation, scale;
                Quatd rotation;
                if (matrix.DecomposeTRS(translation, rotation, scale)) {
                    node.Translation = translation;
                    node.Rotation = rotation;
                    node.Scale = scale;
                } else if (!matrix.IsIdentity(1e-15)) {
                    // Shear, which TRS cannot express. A reflection is not this
                    // case: DecomposeTRS holds the root frame's mirror as a
                    // negative scale on one axis, which reproduces the matrix
                    // exactly - and keeping it is what converts the file's
                    // left-handed object space back to the space the model was
                    // authored in.
                    node.Matrix = matrix;
                }
            } else {
                options_.Warn("X: a FrameTransformMatrix in frame '" + node.Name +
                              "' has fewer than sixteen values and is ignored");
            }
        }

        const int index = document_->AddNode(std::move(node), parent);

        for (const XFile::Node& child : frame.Children) {
            if (child.Type == "Frame") ReadFrame(child, index);
            else if (child.Type == "Mesh") AttachMesh(child, index);
            else if (child.IsReference()) {
                const XFile::Node* target = file_.Resolve(child);
                if (target && target->Type == "Mesh") AttachMesh(*target, index);
                else if (target && target->Type == "Frame") ReadFrame(*target, index);
            } else if (child.Type == "FrameTransformMatrix") {
                // already read
            } else if (!IsKnownIgnorable(child.Type)) {
                WarnOnce("frame." + child.Type,
                         "X: '" + child.Type + "' inside a Frame is not read");
            }
        }
    }

    // ----- meshes -----

    void AttachMesh(const XFile::Node& meshNode, int nodeIndex) {
        MeshData data;
        data.Name = meshNode.Name.empty()
                            ? document_->Nodes[static_cast<size_t>(nodeIndex)].Name
                            : meshNode.Name;
        if (!ReadMesh(meshNode, data)) return;

        std::vector<MeshPrimitive> primitives = BuildPrimitives(data);
        if (primitives.empty()) return;

        ModelMesh mesh;
        mesh.Name = data.Name;
        mesh.Primitives = std::move(primitives);
        const int meshIndex = document_->AddMesh(std::move(mesh));

        // One node carries one mesh. A frame holding several Mesh objects -
        // rare, but legal - gets a child node per extra mesh rather than
        // silently keeping only the first.
        if (document_->Nodes[static_cast<size_t>(nodeIndex)].Mesh < 0) {
            document_->Nodes[static_cast<size_t>(nodeIndex)].Mesh = meshIndex;
            meshNodes_.push_back(nodeIndex);
        } else {
            ModelNode extra;
            extra.Name = data.Name;
            extra.Mesh = meshIndex;
            meshNodes_.push_back(document_->AddNode(std::move(extra), nodeIndex));
        }
    }

    bool ReadMesh(const XFile::Node& meshNode, MeshData& data) {
        XFile::Cursor cursor(meshNode.Numbers);
        const int vertexCount = cursor.NextCount(3);
        if (!cursor.Ok()) {
            options_.Warn("X: mesh '" + data.Name + "' declares more vertices than it carries");
            return false;
        }
        data.Positions.reserve(static_cast<size_t>(vertexCount));
        for (int i = 0; i < vertexCount; ++i) {
            const double x = cursor.Next();
            const double y = cursor.Next();
            data.Positions.emplace_back(x, y, cursor.Next());
        }

        const int faceCount = cursor.NextInt();
        if (!cursor.Ok() || !ReadFaceArray(cursor, faceCount, data.Faces)) {
            options_.Warn("X: mesh '" + data.Name + "' ends inside its face list");
            if (data.Faces.empty()) return false;
        }

        for (const XFile::Node& child : meshNode.Children) {
            const XFile::Node* resolved = child.IsReference() ? file_.Resolve(child) : &child;
            if (!resolved) continue;
            const std::string& type = resolved->Type;

            if (type == "MeshNormals") ReadNormals(*resolved, data);
            else if (type == "MeshTextureCoords") ReadTexCoords(*resolved, data);
            else if (type == "MeshVertexColors") ReadVertexColors(*resolved, data);
            else if (type == "MeshMaterialList") ReadMaterialList(*resolved, data);
            else if (type == "XSkinMeshHeader" || type == "SkinWeights")
                WarnOnce("skin", "X: skinning (XSkinMeshHeader / SkinWeights) is not read; "
                                 "the mesh arrives in its bind pose");
            else if (type == "DeclData")
                WarnOnce("decl", "X: <DeclData> carries extra vertex streams (tangents, a "
                                 "second UV set) that are not read");
            else if (type == "MeshFaceWraps" || type == "VertexDuplicationIndices")
                { /* wrapping hints and dedup tables; nothing the document holds */ }
            else if (!IsKnownIgnorable(type))
                WarnOnce("mesh." + type, "X: '" + type + "' inside a Mesh is not read");
        }
        return !data.Positions.empty() && !data.Faces.empty();
    }

    void ReadNormals(const XFile::Node& node, MeshData& data) {
        XFile::Cursor cursor(node.Numbers);
        const int count = cursor.NextCount(3);
        if (!cursor.Ok()) {
            options_.Warn("X: MeshNormals in '" + data.Name + "' is truncated and is ignored");
            return;
        }
        data.Normals.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            const float x = static_cast<float>(cursor.Next());
            const float y = static_cast<float>(cursor.Next());
            data.Normals.emplace_back(x, y, static_cast<float>(cursor.Next()));
        }
        const int faceCount = cursor.NextInt();
        if (!cursor.Ok() || !ReadFaceArray(cursor, faceCount, data.NormalFaces)) {
            options_.Warn("X: MeshNormals in '" + data.Name +
                          "' ends inside its face list; its normals are dropped");
            data.Normals.clear();
            data.NormalFaces.clear();
        }
    }

    void ReadTexCoords(const XFile::Node& node, MeshData& data) {
        XFile::Cursor cursor(node.Numbers);
        const int count = cursor.NextCount(2);
        if (!cursor.Ok()) {
            options_.Warn("X: MeshTextureCoords in '" + data.Name + "' is truncated");
            return;
        }
        // Texture coordinates are indexed by vertex, not by corner - the one
        // stream in this format that is.
        data.TexCoords.reserve(static_cast<size_t>(count) * 2);
        for (int i = 0; i < count; ++i) {
            data.TexCoords.push_back(static_cast<float>(cursor.Next()));
            data.TexCoords.push_back(static_cast<float>(cursor.Next()));
        }
        if (static_cast<size_t>(count) != data.Positions.size())
            WarnOnce("uvcount", "X: mesh '" + data.Name + "' has " + std::to_string(count) +
                                " texture coordinates for " + std::to_string(data.Positions.size()) +
                                " vertices; the surplus is ignored and the shortfall filled with zero");
    }

    void ReadVertexColors(const XFile::Node& node, MeshData& data) {
        XFile::Cursor cursor(node.Numbers);
        const int count = cursor.NextCount(5);   // index + RGBA
        if (!cursor.Ok()) {
            options_.Warn("X: MeshVertexColors in '" + data.Name + "' is truncated");
            return;
        }
        data.Colors.assign(data.Positions.size() * 4, 1.0f);
        for (int i = 0; i < count; ++i) {
            const int index = cursor.NextInt();
            const float r = static_cast<float>(cursor.Next());
            const float g = static_cast<float>(cursor.Next());
            const float b = static_cast<float>(cursor.Next());
            const float a = static_cast<float>(cursor.Next());
            if (index < 0 || static_cast<size_t>(index) >= data.Positions.size()) continue;
            const size_t at = static_cast<size_t>(index) * 4;
            data.Colors[at] = r; data.Colors[at + 1] = g;
            data.Colors[at + 2] = b; data.Colors[at + 3] = a;
        }
    }

    void ReadMaterialList(const XFile::Node& node, MeshData& data) {
        XFile::Cursor cursor(node.Numbers);
        const int materialCount = cursor.NextInt();
        const int faceIndexCount = cursor.NextCount(1);
        if (!cursor.Ok()) {
            options_.Warn("X: MeshMaterialList in '" + data.Name + "' is truncated");
            return;
        }
        data.FaceMaterial.reserve(static_cast<size_t>(faceIndexCount));
        for (int i = 0; i < faceIndexCount; ++i) data.FaceMaterial.push_back(cursor.NextInt());

        for (const XFile::Node& child : node.Children) {
            const XFile::Node* resolved = child.IsReference() ? file_.Resolve(child) : &child;
            if (!resolved) {
                options_.Warn("X: MeshMaterialList in '" + data.Name + "' references '" +
                              child.Reference + "', which is not defined in this file");
                data.Materials.push_back(-1);
                continue;
            }
            if (resolved->Type == "Material") data.Materials.push_back(MaterialIndexOf(*resolved));
        }
        if (materialCount >= 0 && static_cast<size_t>(materialCount) != data.Materials.size())
            WarnOnce("matcount", "X: a MeshMaterialList declares " + std::to_string(materialCount) +
                                 " materials but carries " + std::to_string(data.Materials.size()));
    }

    // ----- materials -----

    int MaterialIndexOf(const XFile::Node& node) {
        if (!node.Name.empty()) {
            auto cached = materialByName_.find(node.Name);
            if (cached != materialByName_.end()) return cached->second;
        }

        ModelMaterial material;
        material.Name = node.Name;

        XFile::Cursor cursor(node.Numbers);
        PhongParams phong;
        const float r = static_cast<float>(cursor.Next());
        const float g = static_cast<float>(cursor.Next());
        const float b = static_cast<float>(cursor.Next());
        const float alpha = static_cast<float>(cursor.Next());
        phong.Diffuse = Vec3f(r, g, b);
        // `power` is the specular exponent outright, which is also what
        // PhongParams::Shininess (MTL's Ns) holds - no conversion, unlike X3D's
        // 0..1 shininess.
        phong.Shininess = static_cast<float>(cursor.Next());
        phong.Specular = Vec3f(static_cast<float>(cursor.Next()),
                               static_cast<float>(cursor.Next()),
                               static_cast<float>(cursor.Next()));
        material.EmissiveFactor = Vec3f(static_cast<float>(cursor.Next()),
                                        static_cast<float>(cursor.Next()),
                                        static_cast<float>(cursor.Next()));
        if (!cursor.Ok())
            WarnOnce("material", "X: a Material has fewer than the eleven values the format "
                                 "defines; the missing ones are zero");

        // The format has no ambient term at all, so there is nothing to read
        // and nothing to invent: Phong::Ambient stays at zero and a writer that
        // needs one derives it.
        for (const XFile::Node& child : node.Children) {
            if (child.Type == "TextureFilename") {
                if (child.Strings.empty() || child.Strings.front().empty()) continue;
                phong.DiffuseTexture.Image = ImageIndexOf(child.Strings.front());
            } else if (child.Type == "EffectInstance") {
                WarnOnce("effect", "X: <EffectInstance> shader bindings are not read; the "
                                   "material keeps its fixed-function fields only");
            }
        }

        material.Phong = phong;
        material.BaseColorFactor = Vec4f(r, g, b, alpha);
        if (alpha < 1.0f) material.Alpha = AlphaMode::Blend;
        material.DeriveMissingModel();

        const int index = document_->AddMaterial(std::move(material));
        if (!node.Name.empty()) materialByName_[node.Name] = index;
        return index;
    }

    int ImageIndexOf(const std::string& uri) {
        auto cached = imageByUri_.find(uri);
        if (cached != imageByUri_.end()) return cached->second;

        ModelImage image;
        image.Name = uri;
        image.Uri = uri;
        const size_t dot = uri.rfind('.');
        if (dot != std::string::npos) {
            std::string extension = uri.substr(dot + 1);
            for (char& c : extension)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (extension == "png") image.MimeType = "image/png";
            else if (extension == "jpg" || extension == "jpeg") image.MimeType = "image/jpeg";
            else if (extension == "bmp") image.MimeType = "image/bmp";
            else if (extension == "tga") image.MimeType = "image/x-tga";
            else if (extension == "dds") image.MimeType = "image/vnd-ms.dds";
        }
        const int index = static_cast<int>(document_->Images.size());
        document_->Images.push_back(std::move(image));
        imageByUri_[uri] = index;
        return index;
    }

    // ----- primitives -----

    std::vector<MeshPrimitive> BuildPrimitives(const MeshData& data) {
        // Which material each face uses, and the order the materials first
        // appear in - so a single-material mesh stays a single primitive.
        std::vector<int> order;
        std::map<int, size_t> slotOf;
        const size_t faceCount = data.Faces.size();
        std::vector<int> slotPerFace(faceCount, 0);

        for (size_t face = 0; face < faceCount; ++face) {
            int material = 0;
            if (!data.FaceMaterial.empty())
                material = face < data.FaceMaterial.size() ? data.FaceMaterial[face]
                                                           : data.FaceMaterial.back();
            auto found = slotOf.find(material);
            if (found == slotOf.end()) {
                slotOf.emplace(material, order.size());
                slotPerFace[face] = static_cast<int>(order.size());
                order.push_back(material);
            } else {
                slotPerFace[face] = static_cast<int>(found->second);
            }
        }
        if (order.empty()) order.push_back(0);

        const bool haveNormals = !data.Normals.empty() && data.NormalFaces.size() == faceCount;
        const bool haveTexCoords = !data.TexCoords.empty();
        const bool haveColors = !data.Colors.empty();

        std::vector<MeshPrimitive> primitives(order.size());
        std::vector<std::map<std::pair<int, int>, uint32_t>> caches(order.size());
        std::vector<std::vector<float>> texcoords(order.size());
        std::vector<std::vector<float>> colors(order.size());
        std::vector<size_t> maxCorners(order.size(), 0);
        bool warnedRange = false;

        for (size_t face = 0; face < faceCount; ++face) {
            const std::vector<int>& corners = data.Faces[face];
            if (corners.size() < 3) continue;

            const size_t slot = static_cast<size_t>(slotPerFace[face]);
            MeshPrimitive& prim = primitives[slot];
            const size_t start = prim.Indices.size();
            bool ok = true;

            for (size_t corner = 0; corner < corners.size(); ++corner) {
                const int position = corners[corner];
                if (position < 0 || static_cast<size_t>(position) >= data.Positions.size()) {
                    ok = false;
                    break;
                }
                int normal = -1;
                if (haveNormals) {
                    const std::vector<int>& normalCorners = data.NormalFaces[face];
                    if (corner < normalCorners.size()) normal = normalCorners[corner];
                    if (normal < 0 || static_cast<size_t>(normal) >= data.Normals.size()) normal = -1;
                }

                const std::pair<int, int> key(position, normal);
                auto found = caches[slot].find(key);
                if (found != caches[slot].end()) {
                    prim.Indices.push_back(found->second);
                    continue;
                }
                const uint32_t fresh = static_cast<uint32_t>(prim.Positions.size());
                caches[slot].emplace(key, fresh);
                prim.Indices.push_back(fresh);

                prim.Positions.push_back(data.Positions[static_cast<size_t>(position)]);
                if (haveNormals)
                    prim.Normals.push_back(normal >= 0 ? data.Normals[static_cast<size_t>(normal)]
                                                       : Vec3f(0.0f, 0.0f, 1.0f));
                if (haveTexCoords) {
                    const size_t at = static_cast<size_t>(position) * 2;
                    texcoords[slot].push_back(at + 1 < data.TexCoords.size() ? data.TexCoords[at] : 0.0f);
                    texcoords[slot].push_back(at + 1 < data.TexCoords.size() ? data.TexCoords[at + 1] : 0.0f);
                }
                if (haveColors) {
                    const size_t at = static_cast<size_t>(position) * 4;
                    for (int c = 0; c < 4; ++c)
                        colors[slot].push_back(at + 3 < data.Colors.size()
                                                       ? data.Colors[at + static_cast<size_t>(c)]
                                                       : 1.0f);
                }
            }

            if (!ok) {
                prim.Indices.resize(start);
                if (!warnedRange) {
                    options_.Warn("X: mesh '" + data.Name + "' has a face indexing past its "
                                  "vertex list; those faces are dropped");
                    warnedRange = true;
                }
                continue;
            }
            prim.FaceStarts.push_back(static_cast<uint32_t>(start));
            maxCorners[slot] = std::max(maxCorners[slot], corners.size());
        }

        std::vector<MeshPrimitive> kept;
        for (size_t slot = 0; slot < primitives.size(); ++slot) {
            MeshPrimitive& prim = primitives[slot];
            if (prim.Positions.empty() || prim.FaceStarts.empty()) continue;

            prim.Name = data.Name;
            const int material = order[slot];
            if (material >= 0 && static_cast<size_t>(material) < data.Materials.size())
                prim.Material = data.Materials[static_cast<size_t>(material)];

            if (maxCorners[slot] <= 3) {
                prim.Mode = PrimitiveMode::Triangles;
                prim.FaceStarts.clear();
            } else {
                prim.Mode = PrimitiveMode::Polygons;
                prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
            }

            if (!texcoords[slot].empty()) {
                VertexAttribute uv;
                uv.Semantic = AttributeSemantic::TexCoord;
                uv.Name = "TEXCOORD_0";
                uv.Components = 2;
                uv.Values = std::move(texcoords[slot]);
                prim.Attributes.push_back(std::move(uv));
            }
            if (!colors[slot].empty()) {
                VertexAttribute color;
                color.Semantic = AttributeSemantic::Color;
                color.Name = "COLOR_0";
                color.Components = 4;
                color.Values = std::move(colors[slot]);
                prim.Attributes.push_back(std::move(color));
            }
            kept.push_back(std::move(prim));
        }
        return kept;
    }

    // ----- finishing -----

    // The winding check the header describes. A well-formed export's faces
    // disagree with its own MeshNormals in object space and agree in world
    // space, because the root frame is a reflection. Comparing through the
    // world transform - which here is only its determinant's sign, since that
    // is all a reflection changes about orientation - tells the two cases
    // apart without transforming a single vertex.
    void CheckWinding() {
        size_t agree = 0, disagree = 0;
        for (int nodeIndex : meshNodes_) {
            const ModelNode& node = document_->Nodes[static_cast<size_t>(nodeIndex)];
            if (node.Mesh < 0) continue;
            const double determinant = Determinant3x3(document_->GlobalTransform(nodeIndex));
            if (std::fabs(determinant) < 1e-18) continue;
            const double sign = determinant < 0.0 ? -1.0 : 1.0;

            for (const MeshPrimitive& prim : document_->Meshes[static_cast<size_t>(node.Mesh)].Primitives) {
                if (prim.Normals.size() != prim.Positions.size()) continue;
                for (size_t face = 0; face < prim.FaceCount(); ++face) {
                    const std::vector<uint32_t> corners = prim.Face(face);
                    if (corners.size() < 3) continue;
                    const Vec3d& a = prim.Positions[corners[0]];
                    const Vec3d geometric = (prim.Positions[corners[1]] - a)
                                                    .Cross(prim.Positions[corners[2]] - a);
                    const Vec3f& stored = prim.Normals[corners[0]];
                    const double dot = geometric.Dot(Vec3d(stored.x, stored.y, stored.z)) * sign;
                    if (dot > 0.0) ++agree;
                    else if (dot < 0.0) ++disagree;
                }
            }
        }
        if (disagree > agree && agree + disagree > 0)
            options_.Warn("X: " + std::to_string(disagree) + " of " +
                          std::to_string(agree + disagree) +
                          " faces are wound against the normals the file itself stores, so this "
                          "model will render inside out. Its exporter reversed the winding for a "
                          "left-handed renderer without the matching reflection in a frame.");
    }

    void Finish() {
        CheckWinding();

        if (options_.GenerateMissingNormals) {
            for (auto& mesh : document_->Meshes)
                for (auto& prim : mesh.Primitives)
                    if (prim.Normals.empty() && prim.Mode != PrimitiveMode::Lines &&
                        prim.Mode != PrimitiveMode::Points)
                        prim.RecomputeNormals();
        }
        for (auto& mesh : document_->Meshes)
            for (auto& prim : mesh.Primitives)
                if (!prim.Normals.empty() && prim.Normals.size() != prim.Positions.size())
                    prim.Normals.resize(prim.Positions.size(), Vec3f(0.0f, 0.0f, 1.0f));

        if (options_.WeldTolerance > 0.0) document_->WeldVertices(options_.WeldTolerance);
        if (options_.TriangulateOnImport) document_->TriangulateAll();
        if (options_.ForceUpAxis.has_value()) document_->ConvertUpAxis(*options_.ForceUpAxis);
    }

    void WarnOnce(const std::string& key, const std::string& message) {
        if (!warned_.insert(key).second) return;
        options_.Warn(message);
    }

    const XFile::File& file_;
    const ConversionOptions& options_;
    std::shared_ptr<ModelDocument> document_;

    std::map<std::string, int> materialByName_;
    std::map<std::string, int> imageByUri_;
    std::vector<int> meshNodes_;
    std::set<std::string> warned_;
};

std::shared_ptr<ModelDocument> ReadDocument(const std::vector<uint8_t>& data,
                                            const ConversionOptions& options) {
    XFile::File file;
    std::string error;
    auto warn = [&options](const std::string& message) { options.Warn(message); };
    if (!XFile::Parse(data, file, error, warn)) {
        options.Warn(error);
        return nullptr;
    }
    Reader reader(file, options);
    return reader.Run();
}

} // namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities XFileConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.Meshes = true;
    caps.NGons = true;              // a MeshFace states its own corner count
    caps.SceneGraph = true;         // the Frame hierarchy
    caps.Instancing = true;         // "{ Name }" references a mesh or frame
    caps.Materials = true;
    caps.Textures = true;
    caps.TextureCoordinates = true;
    caps.VertexColors = true;       // MeshVertexColors
    caps.Normals = true;
    caps.Metadata = true;
    caps.DoublePrecision = true;    // an xof ...0064 file stores doubles
    // Deliberately false, and each for a stated reason:
    //   Animations - AnimationSet is reported rather than read; see the header.
    //   Skinning   - XSkinMeshHeader / SkinWeights, likewise.
    //   Units      - the format states none; Y-up is convention, not a field.
    //   UpAxis     - the same.
    //   Lights, Cameras - the format has no such object.
    //   PBRMaterials, Tangents, MorphTargets, EmbeddedTextures - not in it.
    return caps;
}

std::shared_ptr<ModelStorage::ModelDocument> XFileConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        options.Warn("X: cannot open " + filename);
        return nullptr;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
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

std::shared_ptr<ModelStorage::ModelDocument> XFileConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    return ReadDocument(data, options);
}

std::shared_ptr<ModelStorage::ModelDocument> XFileConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    return ImportFromMemory(data, options);
}

bool XFileConverter::ValidateData(const std::vector<uint8_t>& data) const {
    const size_t limit = std::min<size_t>(data.size(), 16);
    return XFile::LooksLikeXFile(
            std::string(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(limit)));
}

bool XFileConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    char head[16] = {};
    file.read(head, 16);
    if (file.gcount() < 16) return false;
    return XFile::LooksLikeXFile(std::string(head, 16));
}

} // namespace ModelConverter
} // namespace UltraCanvas
