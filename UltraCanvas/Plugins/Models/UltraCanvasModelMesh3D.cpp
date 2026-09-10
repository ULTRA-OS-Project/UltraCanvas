// Plugins/Models/UltraCanvasModelMesh3D.cpp
// Implementation of the ModelDocument <-> Mesh3D bridge.
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/UltraCanvasModelMesh3D.h"

namespace UltraCanvas {

using namespace ModelStorage;

namespace {

    Vec3 ToVec3(const Vec3d& v) {
        return Vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
    }

    // Append one primitive's triangles to a flat mesh, transformed by world.
    // The primitive is copied so the triangulation does not disturb the
    // document; only the topology is rewritten, not the vertex data.
    void AppendPrimitive(const MeshPrimitive& source, const Matrix4x4& world,
                         const double normalMatrix[9], bool normalsUsable, Mesh3D& out) {
        MeshPrimitive prim = source;
        if (!prim.Triangulate()) return;   // points and lines have no triangles

        const uint32_t base = static_cast<uint32_t>(out.positions.size());
        const bool identity = world.IsIdentity();

        out.positions.reserve(out.positions.size() + prim.Positions.size());
        for (const auto& p : prim.Positions)
            out.positions.push_back(ToVec3(identity ? p : world.TransformPoint(p)));

        // Normals must stay parallel to positions: a primitive with none
        // contributes zeros here and RecomputeNormals() fixes the whole mesh
        // at the end.
        out.normals.resize(out.positions.size(), Vec3{0.0f, 0.0f, 0.0f});
        for (size_t i = 0; i < prim.Normals.size() && base + i < out.normals.size(); ++i) {
            const Vec3f& n = prim.Normals[i];
            if (identity || !normalsUsable) {
                out.normals[base + i] = Vec3(n.x, n.y, n.z);
                continue;
            }
            const Vec3d t(normalMatrix[0] * n.x + normalMatrix[3] * n.y + normalMatrix[6] * n.z,
                          normalMatrix[1] * n.x + normalMatrix[4] * n.y + normalMatrix[7] * n.z,
                          normalMatrix[2] * n.x + normalMatrix[5] * n.y + normalMatrix[8] * n.z);
            out.normals[base + i] = ToVec3(t.Normalized());
        }

        out.indices.reserve(out.indices.size() + prim.Indices.size());
        for (uint32_t index : prim.Indices) out.indices.push_back(base + index);
    }

    bool HasAnyNormals(const ModelDocument& document) {
        for (const auto& mesh : document.Meshes)
            for (const auto& prim : mesh.Primitives)
                if (!prim.Normals.empty()) return true;
        return false;
    }

} // namespace

Mesh3D ModelDocumentToMesh3D(const ModelDocument& document) {
    Mesh3D out;
    out.name = document.Title;

    if (document.Nodes.empty()) {
        // No scene graph: every mesh in document space.
        const Matrix4x4 identity = Matrix4x4::Identity();
        static const double kIdentity3x3[9] = {1,0,0, 0,1,0, 0,0,1};
        for (const auto& mesh : document.Meshes)
            for (const auto& prim : mesh.Primitives)
                AppendPrimitive(prim, identity, kIdentity3x3, true, out);
    } else {
        for (size_t i = 0; i < document.Nodes.size(); ++i) {
            const ModelNode& node = document.Nodes[i];
            if (node.Mesh < 0 || node.Mesh >= static_cast<int>(document.Meshes.size())) continue;

            const Matrix4x4 world = document.GlobalTransform(static_cast<int>(i));
            double normalMatrix[9];
            const bool normalsUsable = world.InverseTransposeUpper3x3(normalMatrix);
            for (const auto& prim : document.Meshes[node.Mesh].Primitives)
                AppendPrimitive(prim, world, normalMatrix, normalsUsable, out);
        }
    }

    out.ComputeBounds();
    if (!HasAnyNormals(document)) out.RecomputeNormals();
    return out;
}

Mesh3D MeshPrimitiveToMesh3D(const MeshPrimitive& primitive, const std::string& name) {
    Mesh3D out;
    out.name = name.empty() ? primitive.Name : name;

    static const double kIdentity3x3[9] = {1,0,0, 0,1,0, 0,0,1};
    AppendPrimitive(primitive, Matrix4x4::Identity(), kIdentity3x3, true, out);

    out.ComputeBounds();
    if (primitive.Normals.empty()) out.RecomputeNormals();
    return out;
}

ModelDocument Mesh3DToModelDocument(const Mesh3D& mesh) {
    MeshPrimitive prim;
    prim.Name = mesh.name;
    prim.Mode = PrimitiveMode::Triangles;

    prim.Positions.reserve(mesh.positions.size());
    for (const auto& p : mesh.positions) prim.Positions.emplace_back(p.x, p.y, p.z);

    prim.Normals.reserve(mesh.normals.size());
    for (const auto& n : mesh.normals) prim.Normals.emplace_back(n.x, n.y, n.z);

    prim.Indices = mesh.indices;

    ModelMesh modelMesh;
    modelMesh.Name = mesh.name;
    modelMesh.Primitives.push_back(std::move(prim));

    return ModelDocument::FromSingleMesh(std::move(modelMesh), mesh.name);
}

} // namespace UltraCanvas
