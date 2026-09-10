// core/DataFormats/UltraCanvasModelStorage.cpp
// Implementation of the universal 3D scene structure declared in
// include/DataFormats/UltraCanvasModelStorage.h.
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasModelStorage.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace UltraCanvas {
namespace ModelStorage {

// ===== QUATERNION =====

Quatd Quatd::FromAxisAngle(const Vec3d& axis, double radians) {
    Vec3d a = axis.Normalized();
    double half = radians * 0.5;
    double s = std::sin(half);
    return {a.x * s, a.y * s, a.z * s, std::cos(half)};
}

Quatd Quatd::Normalized() const {
    double len = std::sqrt(x * x + y * y + z * z + w * w);
    if (len <= 1e-15) return Identity();
    return {x / len, y / len, z / len, w / len};
}

Quatd Quatd::operator*(const Quatd& o) const {
    return {w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z};
}

Vec3d Quatd::Rotate(const Vec3d& v) const {
    // v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    Vec3d u{x, y, z};
    Vec3d t = u.Cross(v) + v * w;
    return v + u.Cross(t) * 2.0;
}

// ===== MATRIX =====

Matrix4x4 Matrix4x4::Translation(const Vec3d& t) {
    Matrix4x4 r;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

Matrix4x4 Matrix4x4::Scaling(const Vec3d& s) {
    Matrix4x4 r;
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}

Matrix4x4 Matrix4x4::FromQuaternion(const Quatd& q) {
    Quatd n = q.Normalized();
    double xx = n.x * n.x, yy = n.y * n.y, zz = n.z * n.z;
    double xy = n.x * n.y, xz = n.x * n.z, yz = n.y * n.z;
    double wx = n.w * n.x, wy = n.w * n.y, wz = n.w * n.z;

    Matrix4x4 r;
    r.m[0] = 1.0 - 2.0 * (yy + zz);  r.m[1] = 2.0 * (xy + wz);        r.m[2]  = 2.0 * (xz - wy);
    r.m[4] = 2.0 * (xy - wz);        r.m[5] = 1.0 - 2.0 * (xx + zz);  r.m[6]  = 2.0 * (yz + wx);
    r.m[8] = 2.0 * (xz + wy);        r.m[9] = 2.0 * (yz - wx);        r.m[10] = 1.0 - 2.0 * (xx + yy);
    return r;
}

Matrix4x4 Matrix4x4::FromTRS(const Vec3d& translation, const Quatd& rotation, const Vec3d& scale) {
    return Translation(translation) * FromQuaternion(rotation) * Scaling(scale);
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& o) const {
    Matrix4x4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) sum += m[k * 4 + row] * o.m[col * 4 + k];
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

Vec3d Matrix4x4::TransformPoint(const Vec3d& p) const {
    double x = m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12];
    double y = m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13];
    double z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    double w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w != 0.0 && w != 1.0) return {x / w, y / w, z / w};
    return {x, y, z};
}

Vec3d Matrix4x4::TransformDirection(const Vec3d& v) const {
    return {m[0] * v.x + m[4] * v.y + m[8]  * v.z,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z};
}

bool Matrix4x4::InverseTransposeUpper3x3(double out[9]) const {
    // Column-major upper 3x3 as a[row][col].
    const double a00 = m[0], a01 = m[4], a02 = m[8];
    const double a10 = m[1], a11 = m[5], a12 = m[9];
    const double a20 = m[2], a21 = m[6], a22 = m[10];

    const double c00 =  (a11 * a22 - a12 * a21);
    const double c01 = -(a10 * a22 - a12 * a20);
    const double c02 =  (a10 * a21 - a11 * a20);
    const double det = a00 * c00 + a01 * c01 + a02 * c02;
    if (std::fabs(det) < 1e-18) {
        const double identity[9] = {1,0,0, 0,1,0, 0,0,1};
        std::memcpy(out, identity, sizeof(identity));
        return false;
    }
    const double inv = 1.0 / det;
    // inverse = adj / det; transpose of the inverse is the cofactor matrix / det.
    out[0] = c00 * inv;
    out[1] = c01 * inv;
    out[2] = c02 * inv;
    out[3] = -(a01 * a22 - a02 * a21) * inv;
    out[4] =  (a00 * a22 - a02 * a20) * inv;
    out[5] = -(a00 * a21 - a01 * a20) * inv;
    out[6] =  (a01 * a12 - a02 * a11) * inv;
    out[7] = -(a00 * a12 - a02 * a10) * inv;
    out[8] =  (a00 * a11 - a01 * a10) * inv;
    return true;
}

bool Matrix4x4::IsIdentity(double epsilon) const {
    static const Matrix4x4 kIdentity;
    for (int i = 0; i < 16; ++i)
        if (std::fabs(m[i] - kIdentity.m[i]) > epsilon) return false;
    return true;
}

bool Matrix4x4::DecomposeTRS(Vec3d& translation, Quatd& rotation, Vec3d& scale) const {
    translation = {m[12], m[13], m[14]};

    Vec3d c0{m[0], m[1], m[2]};
    Vec3d c1{m[4], m[5], m[6]};
    Vec3d c2{m[8], m[9], m[10]};

    scale = {c0.Length(), c1.Length(), c2.Length()};
    if (scale.x <= 1e-15 || scale.y <= 1e-15 || scale.z <= 1e-15) {
        rotation = Quatd::Identity();
        return false;
    }

    // A negative determinant is a mirror; TRS with a positive scale cannot
    // express it, so fold the flip into the X scale and carry on.
    Vec3d n0 = c0 * (1.0 / scale.x);
    Vec3d n1 = c1 * (1.0 / scale.y);
    Vec3d n2 = c2 * (1.0 / scale.z);
    if (n0.Cross(n1).Dot(n2) < 0.0) {
        scale.x = -scale.x;
        n0 = n0 * -1.0;
    }

    // Shear: the normalised axes must stay orthogonal.
    const double shearXY = n0.Dot(n1), shearXZ = n0.Dot(n2), shearYZ = n1.Dot(n2);
    const bool orthogonal = std::fabs(shearXY) < 1e-6 &&
                            std::fabs(shearXZ) < 1e-6 &&
                            std::fabs(shearYZ) < 1e-6;

    // Rotation matrix (columns n0, n1, n2) to quaternion, Shepperd's method:
    // pick the largest diagonal term so the division never loses precision.
    const double r00 = n0.x, r01 = n1.x, r02 = n2.x;
    const double r10 = n0.y, r11 = n1.y, r12 = n2.y;
    const double r20 = n0.z, r21 = n1.z, r22 = n2.z;
    const double trace = r00 + r11 + r22;
    if (trace > 0.0) {
        double s = std::sqrt(trace + 1.0) * 2.0;
        rotation = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25 * s};
    } else if (r00 > r11 && r00 > r22) {
        double s = std::sqrt(1.0 + r00 - r11 - r22) * 2.0;
        rotation = {0.25 * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
    } else if (r11 > r22) {
        double s = std::sqrt(1.0 + r11 - r00 - r22) * 2.0;
        rotation = {(r01 + r10) / s, 0.25 * s, (r12 + r21) / s, (r02 - r20) / s};
    } else {
        double s = std::sqrt(1.0 + r22 - r00 - r11) * 2.0;
        rotation = {(r02 + r20) / s, (r12 + r21) / s, 0.25 * s, (r10 - r01) / s};
    }
    rotation = rotation.Normalized();
    return orthogonal;
}

// ===== BOUNDS =====

void Bounds3D::Expand(const Vec3d& p) {
    if (p.x < Min.x) Min.x = p.x;
    if (p.y < Min.y) Min.y = p.y;
    if (p.z < Min.z) Min.z = p.z;
    if (p.x > Max.x) Max.x = p.x;
    if (p.y > Max.y) Max.y = p.y;
    if (p.z > Max.z) Max.z = p.z;
}

void Bounds3D::Expand(const Bounds3D& other) {
    if (!other.IsValid()) return;
    Expand(other.Min);
    Expand(other.Max);
}

Vec3d Bounds3D::Center() const {
    if (!IsValid()) return {};
    return {(Min.x + Max.x) * 0.5, (Min.y + Max.y) * 0.5, (Min.z + Max.z) * 0.5};
}

Vec3d Bounds3D::Size() const {
    if (!IsValid()) return {};
    return Max - Min;
}

double Bounds3D::Radius() const {
    if (!IsValid()) return 1.0;
    double r = (Max - Min).Length() * 0.5;
    return r > 1e-12 ? r : 1.0;
}

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
    std::vector<uint32_t> out;
    out.reserve(faces * 3);
    for (size_t f = 0; f < faces; ++f) {
        const std::vector<uint32_t> face = Face(f);
        // Fan about the first vertex. Correct for convex faces, which is what
        // mesh formats emit in practice; concave n-gons need ear clipping and
        // are recorded as a follow-up in the proposal.
        for (size_t k = 2; k < face.size(); ++k) {
            out.push_back(face[0]);
            out.push_back(face[k - 1]);
            out.push_back(face[k]);
        }
    }

    Indices = std::move(out);
    FaceStarts.clear();
    Mode = PrimitiveMode::Triangles;
    return true;
}

void MeshPrimitive::RecomputeNormals() {
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
        // Phong -> PBR. Base colour is the diffuse albedo; a specular colour
        // bright enough to read as metal maps to metallic, and Blinn-Phong
        // shininess maps to roughness by the usual sqrt(2 / (Ns + 2)).
        const PhongParams& p = *Phong;
        BaseColorFactor = Vec4f(p.Diffuse.x, p.Diffuse.y, p.Diffuse.z, BaseColorFactor.w);
        const float specLuma = 0.2126f * p.Specular.x + 0.7152f * p.Specular.y + 0.0722f * p.Specular.z;
        MetallicFactor = specLuma > 0.9f ? 1.0f : 0.0f;
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
    return true;
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

    // Quantise to the tolerance and hash the resulting lattice cell together
    // with the other attributes, so only vertices that agree in every respect
    // merge. Exact-match welding (tolerance <= 0) uses the values themselves.
    const double inv = tolerance > 0.0 ? 1.0 / tolerance : 0.0;
    auto quantise = [inv](double v) -> long long {
        return inv > 0.0 ? static_cast<long long>(std::llround(v * inv))
                         : static_cast<long long>(std::llround(v * 1e9));
    };

    for (auto& mesh : Meshes) {
        for (auto& prim : mesh.Primitives) {
            if (prim.Positions.empty()) continue;

            const size_t before = prim.Positions.size();

            std::unordered_map<std::string, uint32_t> seen;
            seen.reserve(before);

            std::vector<Vec3d> positions;
            std::vector<Vec3f> normals;
            std::vector<Vec4f> tangents;
            std::vector<std::vector<float>> attributeValues(prim.Attributes.size());
            std::vector<uint32_t> remap(before);

            std::string key;
            for (size_t v = 0; v < before; ++v) {
                key.clear();
                const long long qx = quantise(prim.Positions[v].x);
                const long long qy = quantise(prim.Positions[v].y);
                const long long qz = quantise(prim.Positions[v].z);
                key.append(reinterpret_cast<const char*>(&qx), sizeof(qx));
                key.append(reinterpret_cast<const char*>(&qy), sizeof(qy));
                key.append(reinterpret_cast<const char*>(&qz), sizeof(qz));
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

                auto it = seen.find(key);
                if (it != seen.end()) {
                    remap[v] = it->second;
                    continue;
                }

                const uint32_t fresh = static_cast<uint32_t>(positions.size());
                seen.emplace(key, fresh);
                remap[v] = fresh;

                positions.push_back(prim.Positions[v]);
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

} // namespace ModelStorage
} // namespace UltraCanvas
