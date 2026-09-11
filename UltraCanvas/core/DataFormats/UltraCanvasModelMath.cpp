// core/DataFormats/UltraCanvasModelMath.cpp
// Implementation of the vector, quaternion, matrix and bounds types declared
// in include/DataFormats/UltraCanvasModelMath.h.
//
// These were part of UltraCanvasModelStorage.cpp until the B-rep bodies needed
// them too; they moved rather than being duplicated.
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasModelMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

bool Matrix4x4::InverseAffine(Matrix4x4& out) const {
    double it[9];
    // InverseTransposeUpper3x3 returns the cofactor matrix over the
    // determinant, which is the transpose of the inverse; transposing it back
    // gives the inverse of the upper 3x3.
    if (!InverseTransposeUpper3x3(it)) { out = Matrix4x4::Identity(); return false; }

    const double inv3[9] = {it[0], it[3], it[6],
                            it[1], it[4], it[7],
                            it[2], it[5], it[8]};   // row-major inverse of the 3x3

    out = Matrix4x4::Identity();
    // Column-major store: out.m[col * 4 + row] = inv3[row][col].
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            out.m[col * 4 + row] = inv3[row * 3 + col];

    // The translation of the inverse is -inv3 * t.
    const double tx = m[12], ty = m[13], tz = m[14];
    out.m[12] = -(inv3[0] * tx + inv3[1] * ty + inv3[2] * tz);
    out.m[13] = -(inv3[3] * tx + inv3[4] * ty + inv3[5] * tz);
    out.m[14] = -(inv3[6] * tx + inv3[7] * ty + inv3[8] * tz);
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
} // namespace ModelStorage
} // namespace UltraCanvas
