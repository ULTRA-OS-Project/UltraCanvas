// include/DataFormats/UltraCanvasModelMath.h
// The vector, quaternion, matrix and bounds types the 3D structure is built
// from — ModelStorage::ModelDocument, the B-rep bodies in
// UltraCanvasBrepStorage.h, and the converters that fill both.
//
// These live in their own header rather than inside UltraCanvasModelStorage.h
// so that the B-rep types can use them without a circular include: a
// ModelDocument owns BrepData, so BrepStorage.h cannot include ModelStorage.h,
// but both include this. Nothing moved namespace — everything here is still
// UltraCanvas::ModelStorage, and every existing include of
// UltraCanvasModelStorage.h still sees it.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MODEL_MATH_H
#define ULTRACANVAS_MODEL_MATH_H

#include <cmath>
#include <limits>

namespace UltraCanvas {
namespace ModelStorage {

// ===== SCALARS =====

// Positions are double. CAD, survey and geospatial sources place geometry far
// from the origin, where float has already lost millimetre precision (a
// coordinate of 1e6 resolves to ~0.06); the 2D model learned the same lesson
// and moved to double. Everything else — normals, tangents, texture
// coordinates, colours, weights — is float, because it is bounded and small.
    struct Vec3d {
        double x = 0.0, y = 0.0, z = 0.0;

        Vec3d() = default;
        Vec3d(double vx, double vy, double vz) : x(vx), y(vy), z(vz) {}

        Vec3d operator+(const Vec3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
        Vec3d operator-(const Vec3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
        Vec3d operator*(double s) const { return {x * s, y * s, z * s}; }
        Vec3d& operator+=(const Vec3d& o) { x += o.x; y += o.y; z += o.z; return *this; }

        double Dot(const Vec3d& o) const { return x * o.x + y * o.y + z * o.z; }
        Vec3d Cross(const Vec3d& o) const {
            return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
        }
        double Length() const { return std::sqrt(x * x + y * y + z * z); }
        Vec3d Normalized() const {
            double len = Length();
            if (len <= 1e-15) return {0.0, 0.0, 0.0};
            return {x / len, y / len, z / len};
        }
    };

    struct Vec3f {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        Vec3f() = default;
        Vec3f(float vx, float vy, float vz) : x(vx), y(vy), z(vz) {}
    };

    struct Vec4f {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
        Vec4f() = default;
        Vec4f(float vx, float vy, float vz, float vw) : x(vx), y(vy), z(vz), w(vw) {}
    };

// Rotation as a unit quaternion (x, y, z, w) — the form glTF, FBX and USD all
// store and the only one that interpolates without gimbal loss. COLLADA's
// axis-angle and 3DS's Euler triples convert on import.
    struct Quatd {
        double x = 0.0, y = 0.0, z = 0.0, w = 1.0;

        Quatd() = default;
        Quatd(double qx, double qy, double qz, double qw) : x(qx), y(qy), z(qz), w(qw) {}

        static Quatd Identity() { return {}; }
        static Quatd FromAxisAngle(const Vec3d& axis, double radians);

        Quatd Normalized() const;
        Quatd operator*(const Quatd& o) const;   // this ∘ o: apply o first
        Vec3d Rotate(const Vec3d& v) const;
    };

// Column-major 4x4, the OpenGL memory layout: m[col * 4 + row], so a
// translation sits in m[12..14] and the array can be handed to glUniformMatrix4fv
// after narrowing. Same convention as the existing float Mat4 in the STL
// plugin, in double.
    struct Matrix4x4 {
        double m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

        static Matrix4x4 Identity() { return {}; }
        static Matrix4x4 Translation(const Vec3d& t);
        static Matrix4x4 Scaling(const Vec3d& s);
        static Matrix4x4 FromQuaternion(const Quatd& q);
        // T * R * S, the composition order every scene format defines TRS in.
        static Matrix4x4 FromTRS(const Vec3d& translation, const Quatd& rotation,
                                 const Vec3d& scale);

        Matrix4x4 operator*(const Matrix4x4& o) const;   // this * o: apply o first
        Vec3d TransformPoint(const Vec3d& p) const;      // w = 1, divides by w
        Vec3d TransformDirection(const Vec3d& v) const;  // w = 0, no translation
        // Normals transform by the inverse transpose; returns false and leaves
        // the result identity when the matrix is not invertible.
        bool InverseTransposeUpper3x3(double out[9]) const;
        // Inverse of an affine transform (last row 0,0,0,1). Returns false and
        // leaves `out` identity when the upper 3x3 is singular. Readers of
        // formats that store world-space vertices beside an object matrix
        // (3DS) need it to recover object-local coordinates.
        bool InverseAffine(Matrix4x4& out) const;
        bool IsIdentity(double epsilon = 1e-12) const;
        // Decompose into TRS. Returns false when the matrix carries shear or a
        // mirror that TRS cannot express — the caller should keep the matrix.
        bool DecomposeTRS(Vec3d& translation, Quatd& rotation, Vec3d& scale) const;
    };

// ===== BOUNDS =====

    struct Bounds3D {
        Vec3d Min{ std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max() };
        Vec3d Max{ -std::numeric_limits<double>::max(),
                   -std::numeric_limits<double>::max(),
                   -std::numeric_limits<double>::max() };

        bool IsValid() const { return Min.x <= Max.x; }
        void Expand(const Vec3d& p);
        void Expand(const Bounds3D& other);
        Vec3d Center() const;
        Vec3d Size() const;
        double Radius() const;   // half the bounding diagonal; 1.0 when empty
    };

// A point in a surface's parameter space. B-rep trimming curves — STEP's
// pcurves, IGES entity 142, the ACIS coedge's parameter curve — live in (u, v)
// rather than in space, and a trimmed surface cannot be tessellated without
// them. Nothing in the mesh side of the structure uses it.
    struct Vec2d {
        double x = 0.0, y = 0.0;

        Vec2d() = default;
        Vec2d(double vx, double vy) : x(vx), y(vy) {}

        Vec2d operator+(const Vec2d& o) const { return {x + o.x, y + o.y}; }
        Vec2d operator-(const Vec2d& o) const { return {x - o.x, y - o.y}; }
        Vec2d operator*(double s) const { return {x * s, y * s}; }
        Vec2d& operator+=(const Vec2d& o) { x += o.x; y += o.y; return *this; }

        double Dot(const Vec2d& o) const { return x * o.x + y * o.y; }
        // The z of the 3D cross product: positive when o turns left of this.
        double Cross(const Vec2d& o) const { return x * o.y - y * o.x; }
        double Length() const { return std::sqrt(x * x + y * y); }
        Vec2d Normalized() const {
            double len = Length();
            if (len <= 1e-15) return {0.0, 0.0};
            return {x / len, y / len};
        }
    };

} // namespace ModelStorage
} // namespace UltraCanvas

#endif // ULTRACANVAS_MODEL_MATH_H
