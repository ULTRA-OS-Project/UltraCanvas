// core/DataFormats/UltraCanvasModelStorage.cpp
// Implementation of the universal 3D scene structure declared in
// include/DataFormats/UltraCanvasModelStorage.h.
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasModelStorage.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace UltraCanvas {
namespace ModelStorage {

// ===== UNITS =====

double MetersPerUnit(ModelUnit unit) {
    switch (unit) {
        case ModelUnit::Micrometer: return 1e-6;
        case ModelUnit::Millimeter: return 1e-3;
        case ModelUnit::Centimeter: return 1e-2;
        case ModelUnit::Decimeter:  return 1e-1;
        case ModelUnit::Meter:      return 1.0;
        case ModelUnit::Kilometer:  return 1000.0;
        case ModelUnit::Mil:        return 0.0000254;
        case ModelUnit::Inch:       return 0.0254;
        case ModelUnit::Foot:       return 0.3048;
        case ModelUnit::Yard:       return 0.9144;
        case ModelUnit::Mile:       return 1609.344;
        case ModelUnit::Unspecified: break;
    }
    return 0.0;
}

const char* ModelUnitSymbol(ModelUnit unit) {
    switch (unit) {
        case ModelUnit::Micrometer: return "um";
        case ModelUnit::Millimeter: return "mm";
        case ModelUnit::Centimeter: return "cm";
        case ModelUnit::Decimeter:  return "dm";
        case ModelUnit::Meter:      return "m";
        case ModelUnit::Kilometer:  return "km";
        case ModelUnit::Mil:        return "mil";
        case ModelUnit::Inch:       return "in";
        case ModelUnit::Foot:       return "ft";
        case ModelUnit::Yard:       return "yd";
        case ModelUnit::Mile:       return "mi";
        case ModelUnit::Unspecified: break;
    }
    return "";
}

// ===== MESH PRIMITIVE =====

namespace {

    // Vertices per face for the fixed-size modes; 0 for the modes whose face
    // count is not a simple division (strips, fans, polygons).
    size_t FixedFaceStride(PrimitiveMode mode) {
        switch (mode) {
            case PrimitiveMode::Points:    return 1;
            case PrimitiveMode::Lines:     return 2;
            case PrimitiveMode::Triangles: return 3;
            default: return 0;
        }
    }

} // namespace

size_t MeshPrimitive::FaceCount() const {
    const size_t n = Indices.empty() ? Positions.size() : Indices.size();

    if (Mode == PrimitiveMode::Polygons)
        return FaceStarts.size() > 1 ? FaceStarts.size() - 1 : 0;

    if (const size_t stride = FixedFaceStride(Mode)) return n / stride;

    switch (Mode) {
        case PrimitiveMode::LineStrip:     return n >= 2 ? n - 1 : 0;
        case PrimitiveMode::LineLoop:      return n >= 2 ? n : 0;
        case PrimitiveMode::TriangleStrip:
        case PrimitiveMode::TriangleFan:   return n >= 3 ? n - 2 : 0;
        default: return 0;
    }
}

std::vector<uint32_t> MeshPrimitive::Face(size_t faceIndex) const {
    const size_t n = Indices.empty() ? Positions.size() : Indices.size();
    auto at = [this](size_t i) -> uint32_t {
        return Indices.empty() ? static_cast<uint32_t>(i) : Indices[i];
    };

    std::vector<uint32_t> face;
    if (faceIndex >= FaceCount()) return face;

    switch (Mode) {
        case PrimitiveMode::Polygons: {
            const uint32_t begin = FaceStarts[faceIndex];
            const uint32_t end = FaceStarts[faceIndex + 1];
            for (uint32_t i = begin; i < end && i < n; ++i) face.push_back(at(i));
            return face;
        }
        case PrimitiveMode::LineStrip:
            return {at(faceIndex), at(faceIndex + 1)};
        case PrimitiveMode::LineLoop:
            return {at(faceIndex), at((faceIndex + 1) % n)};
        case PrimitiveMode::TriangleStrip:
            // Every other triangle is wound backwards so the strip keeps a
            // consistent facing.
            return (faceIndex % 2) == 0
                   ? std::vector<uint32_t>{at(faceIndex), at(faceIndex + 1), at(faceIndex + 2)}
                   : std::vector<uint32_t>{at(faceIndex + 1), at(faceIndex), at(faceIndex + 2)};
        case PrimitiveMode::TriangleFan:
            return {at(0), at(faceIndex + 1), at(faceIndex + 2)};
        default: {
            const size_t stride = FixedFaceStride(Mode);
            for (size_t k = 0; k < stride; ++k) face.push_back(at(faceIndex * stride + k));
            return face;
        }
    }
}

Bounds3D MeshPrimitive::ComputeBounds() const {
    Bounds3D b;
    for (const auto& p : Positions) b.Expand(p);
    return b;
}

const VertexAttribute* MeshPrimitive::FindAttribute(AttributeSemantic semantic, int set) const {
    for (const auto& a : Attributes)
        if (a.Semantic == semantic && a.Set == set) return &a;
    return nullptr;
}

const VertexAttribute* MeshPrimitive::FindAttribute(const std::string& name) const {
    for (const auto& a : Attributes)
        if (a.Name == name) return &a;
    return nullptr;
}

namespace {

// A convex polygon fans correctly and in a fraction of the time, so the
// expensive path is taken only when it buys something. Convexity is judged
// against the polygon's own Newell normal, which is the only stable reference
// for a face that is not exactly planar.
bool PolygonIsConvex(const std::vector<Vec3d>& points) {
    if (points.size() < 4) return true;
    const Vec3d normal = NewellNormal(points).Normalized();
    if (normal.Length() < 0.5) return false;   // degenerate: let ear clipping judge
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec3d& a = points[(i + points.size() - 1) % points.size()];
        const Vec3d& b = points[i];
        const Vec3d& c = points[(i + 1) % points.size()];
        if ((b - a).Cross(c - b).Dot(normal) < 0.0) return false;
    }
    return true;
}

} // namespace

bool MeshPrimitive::Triangulate() {
    const bool isTriangleTopology = Mode == PrimitiveMode::Triangles ||
                                    Mode == PrimitiveMode::TriangleStrip ||
                                    Mode == PrimitiveMode::TriangleFan ||
                                    Mode == PrimitiveMode::Polygons;
    if (!isTriangleTopology) return false;

    if (Mode == PrimitiveMode::Triangles && FaceStarts.empty()) {
        if (Indices.empty()) {
            Indices.resize(Positions.size());
            for (uint32_t i = 0; i < Indices.size(); ++i) Indices[i] = i;
        }
        return true;
    }

    const size_t faces = FaceCount();
    const bool carriesGroups = SmoothingGroups.size() == faces;

    std::vector<uint32_t> out;
    std::vector<uint32_t> groups;
    out.reserve(faces * 3);
    if (carriesGroups) groups.reserve(faces);

    std::vector<Vec3d> corners;
    std::vector<uint32_t> local;
    for (size_t f = 0; f < faces; ++f) {
        const std::vector<uint32_t> face = Face(f);
        if (face.size() < 3) continue;

        const size_t before = out.size();

        if (face.size() == 3) {
            out.push_back(face[0]);
            out.push_back(face[1]);
            out.push_back(face[2]);
        } else {
            corners.clear();
            corners.reserve(face.size());
            bool resolvable = true;
            for (uint32_t index : face) {
                if (index >= Positions.size()) { resolvable = false; break; }
                corners.push_back(Positions[index]);
            }

            bool clipped = false;
            if (resolvable && !PolygonIsConvex(corners)) {
                // The concave case, and the reason this is not a fan: ear
                // clipping only ever emits triangles that lie inside the
                // polygon, so an L-shaped face keeps its notch.
                local.clear();
                if (TriangulatePolygon3D(corners, local) && !local.empty()) {
                    for (uint32_t k : local) out.push_back(face[k]);
                    clipped = true;
                }
            }
            if (!clipped) {
                // Convex, or a polygon ear clipping could not resolve — a
                // self-intersecting or fully collinear face. The fan is what
                // the file's own author most likely meant.
                for (size_t k = 2; k < face.size(); ++k) {
                    out.push_back(face[0]);
                    out.push_back(face[k - 1]);
                    out.push_back(face[k]);
                }
            }
        }

        if (carriesGroups) {
            const size_t produced = (out.size() - before) / 3;
            groups.insert(groups.end(), produced, SmoothingGroups[f]);
        }
    }

    Indices = std::move(out);
    FaceStarts.clear();
    if (carriesGroups) SmoothingGroups = std::move(groups);
    else SmoothingGroups.clear();
    Mode = PrimitiveMode::Triangles;
    return true;
}

namespace {

// Union-find over a face's smoothing groups. Two faces at a vertex smooth
// together when their masks share a bit; a mask of 0 ("s off") shares nothing,
// not even with another 0, which is exactly OBJ's rule.
struct SmoothingClusters {
    std::vector<int> Parent;
    void Reset(size_t n) {
        Parent.resize(n);
        for (size_t i = 0; i < n; ++i) Parent[i] = static_cast<int>(i);
    }
    int Find(int x) {
        while (Parent[static_cast<size_t>(x)] != x) {
            Parent[static_cast<size_t>(x)] = Parent[static_cast<size_t>(Parent[static_cast<size_t>(x)])];
            x = Parent[static_cast<size_t>(x)];
        }
        return x;
    }
    void Union(int a, int b) {
        a = Find(a); b = Find(b);
        if (a != b) Parent[static_cast<size_t>(a)] = b;
    }
};

} // namespace

void MeshPrimitive::RecomputeNormals() {
    // With smoothing groups present, "the normal at this vertex" is not one
    // vector: a corner where a smoothed face meets a creased one has two, and
    // the vertex has to be split for the crease to exist at all. That path is
    // taken only for the topologies a file can actually attach groups to.
    const bool groupsUsable =
            SmoothingGroups.size() == FaceCount() &&
            (Mode == PrimitiveMode::Triangles || Mode == PrimitiveMode::Polygons);
    if (groupsUsable && RecomputeNormalsBySmoothingGroup()) return;

    std::vector<Vec3d> accum(Positions.size(), Vec3d{});

    const size_t faces = FaceCount();
    for (size_t f = 0; f < faces; ++f) {
        const std::vector<uint32_t> face = Face(f);
        if (face.size() < 3) continue;
        for (size_t k = 2; k < face.size(); ++k) {
            const uint32_t a = face[0], b = face[k - 1], c = face[k];
            if (a >= Positions.size() || b >= Positions.size() || c >= Positions.size()) continue;
            // Un-normalised cross product: its length is twice the triangle
            // area, which is exactly the weight a smooth normal wants.
            const Vec3d n = (Positions[b] - Positions[a]).Cross(Positions[c] - Positions[a]);
            accum[a] += n;
            accum[b] += n;
            accum[c] += n;
        }
    }

    Normals.resize(Positions.size());
    for (size_t i = 0; i < accum.size(); ++i) {
        const Vec3d n = accum[i].Normalized();
        Normals[i] = Vec3f(static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z));
    }
}

bool MeshPrimitive::RecomputeNormalsBySmoothingGroup() {
    const size_t faces = FaceCount();
    if (faces == 0 || Positions.empty()) return false;

    // Face normals first: un-normalised, so their length is twice the area and
    // averaging them weights by area for free.
    std::vector<Vec3d> faceNormals(faces);
    std::vector<std::vector<uint32_t>> faceCorners(faces);
    for (size_t f = 0; f < faces; ++f) {
        faceCorners[f] = Face(f);
        std::vector<Vec3d> points;
        points.reserve(faceCorners[f].size());
        for (uint32_t index : faceCorners[f]) {
            if (index >= Positions.size()) { points.clear(); break; }
            points.push_back(Positions[index]);
        }
        if (points.size() >= 3) faceNormals[f] = NewellNormal(points);
    }

    // Which faces touch each vertex.
    std::vector<std::vector<uint32_t>> incident(Positions.size());
    for (size_t f = 0; f < faces; ++f)
        for (uint32_t index : faceCorners[f])
            if (index < Positions.size()) incident[index].push_back(static_cast<uint32_t>(f));

    std::vector<Vec3d> positions;
    std::vector<Vec3f> normals;
    std::vector<Vec4f> tangents;
    std::vector<std::vector<float>> attributes(Attributes.size());
    std::vector<std::vector<Vec3d>> morphPositions(Targets.size());
    std::vector<std::vector<Vec3f>> morphNormals(Targets.size());

    // corner (face, vertex) -> new vertex index
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> remap;
    SmoothingClusters clusters;

    for (uint32_t v = 0; v < Positions.size(); ++v) {
        const std::vector<uint32_t>& touching = incident[v];
        if (touching.empty()) {
            // An unreferenced vertex still has to keep its slot, or every
            // index after it shifts.
            remap[{std::numeric_limits<uint32_t>::max(), v}] =
                    static_cast<uint32_t>(positions.size());
            positions.push_back(Positions[v]);
            normals.emplace_back(0.0f, 0.0f, 0.0f);
            if (v < Tangents.size()) tangents.push_back(Tangents[v]);
            for (size_t a = 0; a < Attributes.size(); ++a) {
                const auto& src = Attributes[a];
                const size_t stride = static_cast<size_t>(src.Components);
                for (size_t k = 0; k < stride; ++k)
                    attributes[a].push_back(v * stride + k < src.Values.size()
                                            ? src.Values[v * stride + k] : 0.0f);
            }
            for (size_t t = 0; t < Targets.size(); ++t) {
                if (v < Targets[t].PositionDeltas.size())
                    morphPositions[t].push_back(Targets[t].PositionDeltas[v]);
                if (v < Targets[t].NormalDeltas.size())
                    morphNormals[t].push_back(Targets[t].NormalDeltas[v]);
            }
            continue;
        }

        clusters.Reset(touching.size());
        for (size_t i = 0; i < touching.size(); ++i)
            for (size_t j = i + 1; j < touching.size(); ++j)
                if ((SmoothingGroups[touching[i]] & SmoothingGroups[touching[j]]) != 0)
                    clusters.Union(static_cast<int>(i), static_cast<int>(j));

        std::map<int, uint32_t> emitted;
        for (size_t i = 0; i < touching.size(); ++i) {
            const int root = clusters.Find(static_cast<int>(i));
            auto it = emitted.find(root);
            if (it == emitted.end()) {
                Vec3d sum;
                for (size_t j = 0; j < touching.size(); ++j)
                    if (clusters.Find(static_cast<int>(j)) == root) sum += faceNormals[touching[j]];
                const Vec3d unit = sum.Normalized();

                const uint32_t fresh = static_cast<uint32_t>(positions.size());
                positions.push_back(Positions[v]);
                normals.emplace_back(static_cast<float>(unit.x), static_cast<float>(unit.y),
                                     static_cast<float>(unit.z));
                if (v < Tangents.size()) tangents.push_back(Tangents[v]);
                for (size_t a = 0; a < Attributes.size(); ++a) {
                    const auto& src = Attributes[a];
                    const size_t stride = static_cast<size_t>(src.Components);
                    for (size_t k = 0; k < stride; ++k)
                        attributes[a].push_back(v * stride + k < src.Values.size()
                                                ? src.Values[v * stride + k] : 0.0f);
                }
                for (size_t t = 0; t < Targets.size(); ++t) {
                    if (v < Targets[t].PositionDeltas.size())
                        morphPositions[t].push_back(Targets[t].PositionDeltas[v]);
                    if (v < Targets[t].NormalDeltas.size())
                        morphNormals[t].push_back(Targets[t].NormalDeltas[v]);
                }
                it = emitted.emplace(root, fresh).first;
            }
            remap[{touching[i], v}] = it->second;
        }
    }

    // Rewrite the faces against the split vertices. Face sizes are unchanged,
    // so FaceStarts still describes them.
    std::vector<uint32_t> indices;
    indices.reserve(Indices.empty() ? Positions.size() : Indices.size());
    for (size_t f = 0; f < faces; ++f)
        for (uint32_t corner : faceCorners[f]) {
            auto it = remap.find({static_cast<uint32_t>(f), corner});
            indices.push_back(it != remap.end() ? it->second : corner);
        }

    Positions = std::move(positions);
    Normals = std::move(normals);
    Tangents = std::move(tangents);
    for (size_t a = 0; a < Attributes.size(); ++a)
        Attributes[a].Values = std::move(attributes[a]);
    for (size_t t = 0; t < Targets.size(); ++t) {
        Targets[t].PositionDeltas = std::move(morphPositions[t]);
        Targets[t].NormalDeltas = std::move(morphNormals[t]);
        Targets[t].TangentDeltas.clear();
    }
    Indices = std::move(indices);
    return true;
}

// ===== MESH =====

Bounds3D ModelMesh::ComputeBounds() const {
    Bounds3D b;
    for (const auto& p : Primitives) b.Expand(p.ComputeBounds());
    return b;
}

size_t ModelMesh::TotalVertexCount() const {
    size_t n = 0;
    for (const auto& p : Primitives) n += p.VertexCount();
    return n;
}

size_t ModelMesh::TotalFaceCount() const {
    size_t n = 0;
    for (const auto& p : Primitives) n += p.FaceCount();
    return n;
}

// ===== MATERIAL =====

void ModelMaterial::DeriveMissingModel() {
    if (Phong.has_value()) {
        // Phong -> PBR. Base colour is the diffuse albedo, and Blinn-Phong
        // shininess maps to roughness by the usual sqrt(2 / (Ns + 2)).
        //
        // Metalness has no Phong equivalent, and guessing it from the specular
        // colour alone is wrong: a white specular means "shiny", not "metal",
        // and it is the single most common value in MTL, 3DS and COLLADA files
        // — the E-45 aircraft sample sets it on painted fuselage panels. What
        // actually distinguishes a metal is that it has no diffuse albedo:
        // its reflectance IS its base colour. So metal requires a dark diffuse
        // AND a bright specular, and everything else stays dielectric, which
        // is both the safer default and the right answer far more often.
        const PhongParams& p = *Phong;
        BaseColorFactor = Vec4f(p.Diffuse.x, p.Diffuse.y, p.Diffuse.z, BaseColorFactor.w);
        auto luma = [](const Vec3f& c) {
            return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        };
        const float diffuseLuma = luma(p.Diffuse);
        const float specularLuma = luma(p.Specular);
        MetallicFactor = (diffuseLuma < 0.1f && specularLuma > 0.5f) ? 1.0f : 0.0f;
        // A metal's base colour is its specular reflectance, not its (black)
        // diffuse.
        if (MetallicFactor > 0.0f)
            BaseColorFactor = Vec4f(p.Specular.x, p.Specular.y, p.Specular.z, BaseColorFactor.w);
        const float ns = p.Shininess > 0.0f ? p.Shininess : 1.0f;
        RoughnessFactor = std::min(1.0f, std::sqrt(2.0f / (ns + 2.0f)));
        if (!BaseColorTexture.IsSet() && p.DiffuseTexture.IsSet())
            BaseColorTexture = p.DiffuseTexture;
        return;
    }

    // PBR -> Phong, for the fixed-function writers (MTL, 3DS, COLLADA).
    PhongParams p;
    p.Diffuse = Vec3f(BaseColorFactor.x * (1.0f - MetallicFactor),
                      BaseColorFactor.y * (1.0f - MetallicFactor),
                      BaseColorFactor.z * (1.0f - MetallicFactor));
    // Dielectrics reflect ~4%; metals reflect their base colour.
    const float dielectric = 0.04f;
    p.Specular = Vec3f(dielectric + (BaseColorFactor.x - dielectric) * MetallicFactor,
                       dielectric + (BaseColorFactor.y - dielectric) * MetallicFactor,
                       dielectric + (BaseColorFactor.z - dielectric) * MetallicFactor);
    const float r = std::max(1e-3f, RoughnessFactor);
    p.Shininess = std::min(1000.0f, 2.0f / (r * r) - 2.0f);
    p.IlluminationModel = 2;
    p.DiffuseTexture = BaseColorTexture;
    Phong = p;
}

// ===== ANIMATION =====

float ModelAnimation::Duration() const {
    float last = 0.0f;
    for (const auto& s : Samplers)
        if (!s.Times.empty()) last = std::max(last, s.Times.back());
    return last;
}

// ===== NODE =====

Matrix4x4 ModelNode::LocalMatrix() const {
    if (Matrix.has_value()) return *Matrix;
    return Matrix4x4::FromTRS(Translation, Rotation, Scale);
}

// ===== DOCUMENT =====

bool ModelDocument::Empty() const {
    for (const auto& mesh : Meshes)
        for (const auto& p : mesh.Primitives)
            if (!p.Positions.empty()) return false;
    // A STEP or IGES document holds no triangles until something asks for
    // them. It is not empty; it is exact.
    return Brep.Empty();
}

size_t ModelDocument::TotalVertexCount() const {
    size_t n = 0;
    for (const auto& mesh : Meshes) n += mesh.TotalVertexCount();
    return n;
}

size_t ModelDocument::TotalFaceCount() const {
    size_t n = 0;
    for (const auto& mesh : Meshes) n += mesh.TotalFaceCount();
    return n;
}

Matrix4x4 ModelDocument::GlobalTransform(int nodeIndex) const {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(Nodes.size()))
        return Matrix4x4::Identity();

    // Walk to the root, guarding against a malformed parent cycle.
    std::vector<int> chain;
    int current = nodeIndex;
    size_t guard = 0;
    while (current >= 0 && current < static_cast<int>(Nodes.size()) && guard++ <= Nodes.size()) {
        chain.push_back(current);
        current = Nodes[current].Parent;
    }
    if (guard > Nodes.size()) return Matrix4x4::Identity();   // cycle

    Matrix4x4 result = Matrix4x4::Identity();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        result = result * Nodes[*it].LocalMatrix();
    return result;
}

Bounds3D ModelDocument::ComputeBounds() const {
    Bounds3D bounds;

    if (Nodes.empty()) {
        for (const auto& mesh : Meshes) bounds.Expand(mesh.ComputeBounds());
        return bounds;
    }

    for (size_t i = 0; i < Nodes.size(); ++i) {
        const ModelNode& node = Nodes[i];
        if (node.Mesh < 0 || node.Mesh >= static_cast<int>(Meshes.size())) continue;
        const Matrix4x4 world = GlobalTransform(static_cast<int>(i));
        for (const auto& prim : Meshes[node.Mesh].Primitives)
            for (const auto& p : prim.Positions)
                bounds.Expand(world.TransformPoint(p));
    }
    return bounds;
}

int ModelDocument::AddNode(ModelNode node, int parent) {
    node.Parent = parent;
    Nodes.push_back(std::move(node));
    const int index = static_cast<int>(Nodes.size()) - 1;

    if (parent >= 0 && parent < static_cast<int>(Nodes.size()) - 1) {
        Nodes[parent].Children.push_back(index);
    } else {
        if (Scenes.empty()) {
            Scenes.push_back(ModelScene{});
            DefaultScene = 0;
        }
        Scenes[DefaultScene >= 0 ? DefaultScene : 0].Roots.push_back(index);
    }
    return index;
}

int ModelDocument::AddMesh(ModelMesh mesh) {
    Meshes.push_back(std::move(mesh));
    return static_cast<int>(Meshes.size()) - 1;
}

int ModelDocument::AddMaterial(ModelMaterial material) {
    Materials.push_back(std::move(material));
    return static_cast<int>(Materials.size()) - 1;
}

ModelDocument ModelDocument::FromSingleMesh(ModelMesh mesh, const std::string& name) {
    ModelDocument doc;
    doc.Title = name.empty() ? mesh.Name : name;

    ModelNode node;
    node.Name = mesh.Name.empty() ? doc.Title : mesh.Name;
    node.Mesh = doc.AddMesh(std::move(mesh));
    doc.AddNode(std::move(node));
    return doc;
}

size_t ModelDocument::TriangulateAll() {
    size_t changed = 0;
    for (auto& mesh : Meshes)
        for (auto& prim : mesh.Primitives)
            if (prim.Triangulate()) ++changed;
    return changed;
}

void ModelDocument::FlattenTransforms(size_t* outDroppedAnimations) {
    if (Nodes.empty()) return;

    // Bake each node's world transform into a private copy of its mesh, so an
    // instanced mesh drawn by several nodes becomes several baked meshes.
    std::vector<ModelMesh> baked;
    std::vector<ModelNode> flat;
    baked.reserve(Nodes.size());
    flat.reserve(Nodes.size());

    for (size_t i = 0; i < Nodes.size(); ++i) {
        const ModelNode& node = Nodes[i];
        if (node.Mesh < 0 || node.Mesh >= static_cast<int>(Meshes.size())) continue;

        const Matrix4x4 world = GlobalTransform(static_cast<int>(i));
        double normalMatrix[9];
        const bool normalsOk = world.InverseTransposeUpper3x3(normalMatrix);

        ModelMesh mesh = Meshes[node.Mesh];
        if (!world.IsIdentity()) {
            for (auto& prim : mesh.Primitives) {
                for (auto& p : prim.Positions) p = world.TransformPoint(p);
                if (normalsOk) {
                    for (auto& n : prim.Normals) {
                        const double nx = normalMatrix[0] * n.x + normalMatrix[3] * n.y + normalMatrix[6] * n.z;
                        const double ny = normalMatrix[1] * n.x + normalMatrix[4] * n.y + normalMatrix[7] * n.z;
                        const double nz = normalMatrix[2] * n.x + normalMatrix[5] * n.y + normalMatrix[8] * n.z;
                        const Vec3d unit = Vec3d(nx, ny, nz).Normalized();
                        n = Vec3f(static_cast<float>(unit.x), static_cast<float>(unit.y),
                                  static_cast<float>(unit.z));
                    }
                }
            }
        }

        ModelNode leaf;
        leaf.Name = node.Name;
        leaf.Mesh = static_cast<int>(baked.size());
        leaf.Extras = node.Extras;
        baked.push_back(std::move(mesh));
        flat.push_back(std::move(leaf));
    }

    Meshes = std::move(baked);
    Nodes = std::move(flat);

    Scenes.assign(1, ModelScene{});
    DefaultScene = 0;
    Scenes[0].Roots.resize(Nodes.size());
    for (size_t i = 0; i < Nodes.size(); ++i) Scenes[0].Roots[i] = static_cast<int>(i);

    // Skins and animations address nodes that no longer exist, and are
    // meaningless once the pose is baked into the vertices.
    if (outDroppedAnimations) *outDroppedAnimations = Animations.size();
    Animations.clear();
    Skins.clear();
    for (auto& node : Nodes) node.Skin = -1;
}

void ModelDocument::ConvertUpAxis(UpAxis target) {
    if (target == Up) return;

    // Z-up -> Y-up is -90 degrees about X; the reverse is +90. Applied to the
    // root nodes, so vertex data is untouched and the change is exactly
    // reversible.
    const double angle = (Up == UpAxis::ZUp) ? -1.5707963267948966 : 1.5707963267948966;
    const Quatd rotation = Quatd::FromAxisAngle(Vec3d(1.0, 0.0, 0.0), angle);

    if (Nodes.empty()) {
        ModelNode pivot;
        pivot.Name = "UpAxisPivot";
        pivot.Rotation = rotation;
        Nodes.push_back(std::move(pivot));
        Scenes.assign(1, ModelScene{});
        DefaultScene = 0;
        Scenes[0].Roots.push_back(0);
        Up = target;
        return;
    }

    for (size_t i = 0; i < Nodes.size(); ++i) {
        if (Nodes[i].Parent >= 0) continue;
        ModelNode& root = Nodes[i];
        if (root.Matrix.has_value()) {
            root.Matrix = Matrix4x4::FromQuaternion(rotation) * (*root.Matrix);
        } else {
            root.Rotation = rotation * root.Rotation;
            root.Translation = rotation.Rotate(root.Translation);
        }
    }
    Up = target;
}

size_t ModelDocument::WeldVertices(double tolerance) {
    size_t removed = 0;

    // Vertices are bucketed into a lattice of cells the size of the tolerance,
    // and a candidate is looked for in its own cell *and its 26 neighbours*.
    // That neighbourhood is the whole difference from a plain hash: rounding a
    // coordinate is discontinuous exactly where geometry likes to sit — on an
    // axis plane, at the origin, on every round number a CAD user typed — so a
    // single-cell lookup fails to merge precisely the seams that matter most.
    // The cell is only a candidate filter; the merge itself is decided by real
    // distance, and by the other attributes agreeing exactly, because a normal
    // or UV seam is a discontinuity the file meant.
    const double inv = tolerance > 0.0 ? 1.0 / tolerance : 1e9;
    auto quantise = [inv](double v) -> long long {
        return static_cast<long long>(std::floor(v * inv));
    };
    auto cellHash = [](long long x, long long y, long long z) -> uint64_t {
        return static_cast<uint64_t>(x) * 73856093ULL ^
               static_cast<uint64_t>(y) * 19349663ULL ^
               static_cast<uint64_t>(z) * 83492791ULL;
    };

    for (auto& mesh : Meshes) {
        for (auto& prim : mesh.Primitives) {
            if (prim.Positions.empty()) continue;

            const size_t before = prim.Positions.size();

            std::unordered_map<uint64_t, std::vector<uint32_t>> buckets;
            buckets.reserve(before);

            std::vector<Vec3d> positions;
            std::vector<Vec3f> normals;
            std::vector<Vec4f> tangents;
            std::vector<std::vector<float>> attributeValues(prim.Attributes.size());
            std::vector<std::string> attributeKeys;
            std::vector<uint32_t> remap(before);
            attributeKeys.reserve(before);

            std::string key;
            for (size_t v = 0; v < before; ++v) {
                // Everything but the position, packed so that two vertices
                // agree only if every attribute does.
                key.clear();
                if (v < prim.Normals.size())
                    key.append(reinterpret_cast<const char*>(&prim.Normals[v]), sizeof(Vec3f));
                if (v < prim.Tangents.size())
                    key.append(reinterpret_cast<const char*>(&prim.Tangents[v]), sizeof(Vec4f));
                for (const auto& a : prim.Attributes) {
                    const size_t stride = static_cast<size_t>(a.Components);
                    const size_t off = v * stride;
                    if (off + stride <= a.Values.size())
                        key.append(reinterpret_cast<const char*>(&a.Values[off]),
                                   stride * sizeof(float));
                }

                const Vec3d& position = prim.Positions[v];
                const long long qx = quantise(position.x);
                const long long qy = quantise(position.y);
                const long long qz = quantise(position.z);

                uint32_t match = std::numeric_limits<uint32_t>::max();
                for (int dz = -1; dz <= 1 && match == std::numeric_limits<uint32_t>::max(); ++dz)
                    for (int dy = -1; dy <= 1 && match == std::numeric_limits<uint32_t>::max(); ++dy)
                        for (int dx = -1; dx <= 1 && match == std::numeric_limits<uint32_t>::max(); ++dx) {
                            auto it = buckets.find(cellHash(qx + dx, qy + dy, qz + dz));
                            if (it == buckets.end()) continue;
                            for (uint32_t candidate : it->second) {
                                if (attributeKeys[candidate] != key) continue;
                                const Vec3d delta = positions[candidate] - position;
                                // A zero tolerance keeps the old contract: only
                                // vertices that are bit-for-bit the same merge.
                                if (tolerance > 0.0 ? delta.Length() <= tolerance
                                                    : (delta.x == 0.0 && delta.y == 0.0 &&
                                                       delta.z == 0.0)) {
                                    match = candidate;
                                    break;
                                }
                            }
                        }

                if (match != std::numeric_limits<uint32_t>::max()) {
                    remap[v] = match;
                    continue;
                }

                const uint32_t fresh = static_cast<uint32_t>(positions.size());
                buckets[cellHash(qx, qy, qz)].push_back(fresh);
                remap[v] = fresh;
                attributeKeys.push_back(key);

                positions.push_back(position);
                if (v < prim.Normals.size()) normals.push_back(prim.Normals[v]);
                if (v < prim.Tangents.size()) tangents.push_back(prim.Tangents[v]);
                for (size_t a = 0; a < prim.Attributes.size(); ++a) {
                    const auto& src = prim.Attributes[a];
                    const size_t stride = static_cast<size_t>(src.Components);
                    const size_t off = v * stride;
                    for (size_t k = 0; k < stride && off + k < src.Values.size(); ++k)
                        attributeValues[a].push_back(src.Values[off + k]);
                }
            }

            if (positions.size() == before) continue;   // nothing to merge

            if (prim.Indices.empty()) {
                prim.Indices.resize(before);
                for (uint32_t i = 0; i < before; ++i) prim.Indices[i] = remap[i];
            } else {
                for (auto& index : prim.Indices)
                    if (index < remap.size()) index = remap[index];
            }

            removed += before - positions.size();
            prim.Positions = std::move(positions);
            prim.Normals = std::move(normals);
            prim.Tangents = std::move(tangents);
            for (size_t a = 0; a < prim.Attributes.size(); ++a)
                prim.Attributes[a].Values = std::move(attributeValues[a]);
        }
    }
    return removed;
}

// ===== TESSELLATION =====

namespace {

// One primitive per material, accumulated as faces are meshed.
struct PrimitiveBuilder {
    int Material = -1;
    std::vector<Vec3d> Positions;
    std::vector<Vec3f> Normals;
    std::vector<float> UVs;
    std::vector<uint32_t> Indices;
};

MeshPrimitive FinishPrimitive(PrimitiveBuilder& builder, bool wantUVs) {
    MeshPrimitive prim;
    prim.Mode = PrimitiveMode::Triangles;
    prim.Material = builder.Material;
    prim.Positions = std::move(builder.Positions);
    prim.Normals = std::move(builder.Normals);
    prim.Indices = std::move(builder.Indices);
    if (wantUVs && !builder.UVs.empty()) {
        VertexAttribute uv;
        uv.Semantic = AttributeSemantic::TexCoord;
        uv.Name = "TEXCOORD_0";
        uv.Components = 2;
        uv.Values = std::move(builder.UVs);
        prim.Attributes.push_back(std::move(uv));
    }
    return prim;
}

} // namespace

size_t ModelDocument::TessellateBreps(const BrepTessellationOptions& options,
                                      std::vector<std::string>* problems) {
    if (Brep.Solids.empty()) return 0;

    // A solid drawn by several nodes is meshed once; the nodes share the mesh,
    // which is the same instancing rule the rest of the document follows.
    std::map<int, int> meshForSolid;

    auto tessellate = [&](int solidIndex) -> int {
        auto cached = meshForSolid.find(solidIndex);
        if (cached != meshForSolid.end()) return cached->second;
        if (solidIndex < 0 || static_cast<size_t>(solidIndex) >= Brep.Solids.size()) return -1;

        const BrepSolid& solid = Brep.Solids[static_cast<size_t>(solidIndex)];

        BrepTessellationOptions resolved = options;
        // Resolve "0 means pick one" here rather than per face, so every face
        // of a body is meshed to the same standard.
        resolved.ChordTolerance = Brep.ResolveChordTolerance(solidIndex, options);

        std::vector<PrimitiveBuilder> builders;
        auto builderFor = [&builders, &resolved](int material, int faceIndex) -> PrimitiveBuilder& {
            if (resolved.SplitByFace) {
                builders.emplace_back();
                builders.back().Material = material;
                (void)faceIndex;
                return builders.back();
            }
            for (PrimitiveBuilder& builder : builders)
                if (builder.Material == material) return builder;
            builders.emplace_back();
            builders.back().Material = material;
            return builders.back();
        };

        for (int shellIndex : solid.Shells) {
            if (shellIndex < 0 || static_cast<size_t>(shellIndex) >= Brep.Shells.size()) continue;
            for (int faceIndex : Brep.Shells[static_cast<size_t>(shellIndex)].Faces) {
                if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= Brep.Faces.size()) continue;
                const BrepFace& face = Brep.Faces[static_cast<size_t>(faceIndex)];
                const int material = face.Material >= 0 ? face.Material : solid.Material;
                PrimitiveBuilder& builder = builderFor(material, faceIndex);
                Brep.TessellateFace(faceIndex, resolved, builder.Positions, builder.Normals,
                                    builder.UVs, builder.Indices, problems);
            }
        }

        ModelMesh mesh;
        mesh.Name = solid.Name.empty() ? ("solid_" + std::to_string(solidIndex)) : solid.Name;
        for (PrimitiveBuilder& builder : builders) {
            if (builder.Indices.empty()) continue;
            mesh.Primitives.push_back(FinishPrimitive(builder, resolved.GenerateUVs));
        }
        if (mesh.Primitives.empty()) {
            meshForSolid[solidIndex] = -1;
            return -1;
        }

        const int meshIndex = AddMesh(std::move(mesh));
        meshForSolid[solidIndex] = meshIndex;
        return meshIndex;
    };

    size_t tessellated = 0;
    bool placed = false;
    for (ModelNode& node : Nodes) {
        if (node.Solid < 0) continue;
        placed = true;
        if (node.Mesh >= 0) continue;   // already meshed; leave it alone
        const int meshIndex = tessellate(node.Solid);
        if (meshIndex >= 0) { node.Mesh = meshIndex; ++tessellated; }
    }

    if (!placed) {
        // The solids were read without a scene graph — a STEP file with one
        // product, a DWG 3DSOLID in model space. Give each one a node so the
        // document is drawable at all.
        for (size_t i = 0; i < Brep.Solids.size(); ++i) {
            const int meshIndex = tessellate(static_cast<int>(i));
            if (meshIndex < 0) continue;
            ModelNode node;
            node.Name = Brep.Solids[i].Name.empty() ? ("solid_" + std::to_string(i))
                                                    : Brep.Solids[i].Name;
            node.Solid = static_cast<int>(i);
            node.Mesh = meshIndex;
            AddNode(std::move(node));
            ++tessellated;
        }
    }
    return tessellated;
}

} // namespace ModelStorage
} // namespace UltraCanvas
