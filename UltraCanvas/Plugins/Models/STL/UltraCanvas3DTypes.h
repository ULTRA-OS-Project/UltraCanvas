// Plugins/Models/STL/UltraCanvas3DTypes.h
// Shared, dependency-free 3D data structures and math helpers for model plugins
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_3D_TYPES_H
#define ULTRACANVAS_3D_TYPES_H

#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <limits>

namespace UltraCanvas {

// ===== WHERE THE CAMERA IS =====
// The view a model is seen from: an orbit around the model's centre, which is
// the only camera a viewer of a single object needs. The model is normalised
// to a unit radius first, so one pose frames any model, however big the file
// says it is.
//
// `UltraCanvasSTLElement` orbits by changing these, and
// `RenderMeshPixmap()` (UltraCanvasModelRaster.h) takes the same three
// numbers - which is what makes the still that is saved the view that was on
// screen.
    struct ModelViewPose {
        float yaw = 0.6f;        // radians, around the model's up axis
        float pitch = 0.4f;      // radians, above the horizon; +-1.5 is straight up/down
        float distance = 3.0f;   // camera distance in model radii; ~3 frames it at 45 degrees

        // The framing a viewer opens at: from the front left, slightly above.
        static ModelViewPose Default() { return ModelViewPose{}; }
    };

// ===== 3-COMPONENT VECTOR =====
    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;

        Vec3() = default;
        Vec3(float vx, float vy, float vz) : x(vx), y(vy), z(vz) {}

        Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

        Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }

        float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

        Vec3 Cross(const Vec3& o) const {
            return {y * o.z - z * o.y,
                    z * o.x - x * o.z,
                    x * o.y - y * o.x};
        }

        float Length() const { return std::sqrt(x * x + y * y + z * z); }

        Vec3 Normalized() const {
            float len = Length();
            if (len <= 1e-12f) return {0.0f, 0.0f, 0.0f};
            float inv = 1.0f / len;
            return {x * inv, y * inv, z * inv};
        }
    };

// ===== AXIS-ALIGNED BOUNDING BOX =====
    struct BoundingBox3D {
        Vec3 min{ std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max() };
        Vec3 max{ -std::numeric_limits<float>::max(),
                  -std::numeric_limits<float>::max(),
                  -std::numeric_limits<float>::max() };

        bool IsValid() const { return min.x <= max.x; }

        void Expand(const Vec3& p) {
            if (p.x < min.x) min.x = p.x;
            if (p.y < min.y) min.y = p.y;
            if (p.z < min.z) min.z = p.z;
            if (p.x > max.x) max.x = p.x;
            if (p.y > max.y) max.y = p.y;
            if (p.z > max.z) max.z = p.z;
        }

        Vec3 Center() const {
            return {(min.x + max.x) * 0.5f,
                    (min.y + max.y) * 0.5f,
                    (min.z + max.z) * 0.5f};
        }

        // Half the length of the bounding diagonal (a good "fit" radius)
        float Radius() const {
            if (!IsValid()) return 1.0f;
            Vec3 d = max - min;
            float r = d.Length() * 0.5f;
            return r > 1e-6f ? r : 1.0f;
        }
    };

// ===== WHICH WAY IS UP =====
// A mesh carries no orientation of its own, but the file it came from had one,
// and the two conventions in use disagree by 90 degrees: STL, STEP, DXF and
// most CAD are Z-up; glTF, FBX and the viewers here are Y-up. The viewers'
// cameras look down -Z with +Y on screen, so a Z-up mesh handed over unrotated
// stands on its nose - which is exactly how every model in the demo used to be
// drawn. Each producer states what it read and the viewers correct for it.
    enum class MeshUpAxis {
        YUp,    // +Y is up: glTF, FBX, MilkShape, and the viewers' own frame
        ZUp     // +Z is up: STL, STEP, DXF, 3D Studio and most CAD
    };

// Rotates a model-space point so the mesh's own up axis points along +Y.
// Z-up to Y-up is -90 degrees about X, the same sense and sign as
// ModelDocument::ConvertUpAxis, so the two agree on what "corrected" means.
    inline Vec3 ToViewerUp(const Vec3& v, MeshUpAxis axis) {
        return axis == MeshUpAxis::ZUp ? Vec3{v.x, v.z, -v.y} : v;
    }

// ===== TRIANGLE MESH =====
// Flat representation: positions/normals are parallel arrays, three consecutive
// entries (i, i+1, i+2) form one triangle when indices are sequential. STL has
// no shared-vertex topology, so the loader emits one vertex per triangle corner.
    struct Mesh3D {
        std::string name;
        std::vector<Vec3> positions;     // vertex positions
        std::vector<Vec3> normals;       // per-vertex normals (parallel to positions)
        std::vector<uint32_t> indices;   // 3 indices per triangle
        BoundingBox3D bounds;
        // Which axis of `positions` points up. The vertex data is left exactly
        // as the file had it - only the viewers rotate - so bounds, extents and
        // saved copies still describe the model in its own frame.
        MeshUpAxis upAxis = MeshUpAxis::YUp;

        bool Empty() const { return positions.empty() || indices.empty(); }
        size_t VertexCount() const { return positions.size(); }
        size_t TriangleCount() const { return indices.size() / 3; }

        void Clear() {
            name.clear();
            positions.clear();
            normals.clear();
            indices.clear();
            bounds = BoundingBox3D{};
            upAxis = MeshUpAxis::YUp;
        }

        void ComputeBounds() {
            bounds = BoundingBox3D{};
            for (const auto& p : positions) bounds.Expand(p);
        }

        // Recompute per-vertex normals from triangle geometry. Used when a source
        // file omits normals (e.g. degenerate STL facet normals of (0,0,0)).
        void RecomputeNormals() {
            normals.assign(positions.size(), Vec3{0.0f, 0.0f, 0.0f});
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
                if (a >= positions.size() || b >= positions.size() || c >= positions.size())
                    continue;
                Vec3 n = (positions[b] - positions[a]).Cross(positions[c] - positions[a]);
                normals[a] += n;
                normals[b] += n;
                normals[c] += n;
            }
            for (auto& n : normals) n = n.Normalized();
        }
    };

// ===== 4x4 MATRIX (column-major, OpenGL layout) =====
    struct Mat4 {
        // m[col * 4 + row]
        float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

        static Mat4 Identity() { return Mat4{}; }

        static Mat4 Translation(float x, float y, float z) {
            Mat4 r;
            r.m[12] = x; r.m[13] = y; r.m[14] = z;
            return r;
        }

        static Mat4 Scale(float s) {
            Mat4 r;
            r.m[0] = s; r.m[5] = s; r.m[10] = s;
            return r;
        }

        static Mat4 RotationX(float radians) {
            Mat4 r;
            float c = std::cos(radians), s = std::sin(radians);
            r.m[5] = c;  r.m[6] = s;
            r.m[9] = -s; r.m[10] = c;
            return r;
        }

        static Mat4 RotationY(float radians) {
            Mat4 r;
            float c = std::cos(radians), s = std::sin(radians);
            r.m[0] = c;  r.m[2] = -s;
            r.m[8] = s;  r.m[10] = c;
            return r;
        }

        static Mat4 Perspective(float fovYRadians, float aspect, float zNear, float zFar) {
            Mat4 r{};
            for (float& v : r.m) v = 0.0f;
            float f = 1.0f / std::tan(fovYRadians * 0.5f);
            r.m[0] = f / (aspect > 1e-6f ? aspect : 1.0f);
            r.m[5] = f;
            r.m[10] = (zFar + zNear) / (zNear - zFar);
            r.m[11] = -1.0f;
            r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
            return r;
        }

        static Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
            Vec3 f = (center - eye).Normalized();
            Vec3 s = f.Cross(up).Normalized();
            Vec3 u = s.Cross(f);
            Mat4 r;
            r.m[0] = s.x;  r.m[4] = s.y;  r.m[8]  = s.z;
            r.m[1] = u.x;  r.m[5] = u.y;  r.m[9]  = u.z;
            r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
            r.m[12] = -s.Dot(eye);
            r.m[13] = -u.Dot(eye);
            r.m[14] = f.Dot(eye);
            r.m[15] = 1.0f;
            return r;
        }

        // this * o  (apply o first, then this)
        Mat4 operator*(const Mat4& o) const {
            Mat4 r{};
            for (float& v : r.m) v = 0.0f;
            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; ++k)
                        sum += m[k * 4 + row] * o.m[col * 4 + k];
                    r.m[col * 4 + row] = sum;
                }
            }
            return r;
        }
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_3D_TYPES_H
