// core/UltraCanvasModelRaster.cpp
// The software mesh rasterizer declared in UltraCanvasModelRaster.h.
//
// Lifted out of UltraCanvasFilerWidget.cpp, where it had been the private back
// half of the Filer's 3D thumbnail producer. Nothing about the maths changed
// in the move - the same pose, the same framing, the same flat two-sided
// shading - only who is allowed to call it.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "UltraCanvasModelRaster.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace UltraCanvas {

std::shared_ptr<UCPixmap> RenderMeshPreviewPixmap(const Mesh3D& source,
                                                  int w, int h, float scale) {
    if (source.Empty()) return nullptr;
    if (source.TriangleCount() > kModelPreviewTriangleCap) return nullptr;

    // The bounds drive both the framing and the divisor below, so a mesh that
    // arrives without them gets them computed here.
    Mesh3D mesh = source;
    if (!mesh.bounds.IsValid()) mesh.ComputeBounds();
    if (!mesh.bounds.IsValid()) return nullptr;

    // A mesh whose every vertex sits at one point is refused, and the extent
    // has to be measured directly to catch it: BoundingBox3D::Radius()
    // substitutes 1.0 for a degenerate box rather than returning zero, which
    // is the right answer for framing and the wrong one here. Without this the
    // model would project to a sub-pixel dot and the caller would get a fully
    // transparent tile it then treats as a successful preview - so the file
    // would show nothing at all instead of falling back to its type glyph.
    const Vec3 extent = mesh.bounds.max - mesh.bounds.min;
    if (!(extent.Length() > 1e-6f)) return nullptr;

    const int pw = std::max(8, static_cast<int>(std::lround(
            w * std::max(1.0f, scale))));
    const int ph = std::max(8, static_cast<int>(std::lround(
            h * std::max(1.0f, scale))));

    // Yaw / pitch of the standard "look at it from the front left and
    // slightly above" pose used by model viewers.
    constexpr float kYaw   = -0.55f;   // radians
    constexpr float kPitch =  0.42f;
    const float cy = std::cos(kYaw),   sy = std::sin(kYaw);
    const float cp = std::cos(kPitch), sp = std::sin(kPitch);
    auto rotate = [&](const Vec3& v) {
        const float x1 =  v.x * cy + v.z * sy;
        const float z1 = -v.x * sy + v.z * cy;
        return Vec3{x1, v.y * cp - z1 * sp, v.y * sp + z1 * cp};
    };

    const Vec3 center = mesh.bounds.Center();
    const float radius = mesh.bounds.Radius();
    // 0.92 leaves a hair of margin so the silhouette never touches the
    // tile edge; the rotated bounding sphere fits in either direction.
    const float unit = 0.92f * 0.5f * static_cast<float>(std::min(pw, ph)) / radius;
    const float ox = pw * 0.5f, oy = ph * 0.5f;

    std::vector<float> depth(static_cast<size_t>(pw) * ph,
                             -std::numeric_limits<float>::max());
    std::vector<uint32_t> pixels(static_cast<size_t>(pw) * ph, 0u);

    const Vec3 light = Vec3{0.35f, 0.55f, 0.76f}.Normalized();
    constexpr float kBaseR = 132.0f, kBaseG = 158.0f, kBaseB = 205.0f;

    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const uint32_t ia = mesh.indices[t], ib = mesh.indices[t + 1],
                       ic = mesh.indices[t + 2];
        if (ia >= mesh.positions.size() || ib >= mesh.positions.size() ||
            ic >= mesh.positions.size()) continue;
        const Vec3 a = rotate(mesh.positions[ia] - center);
        const Vec3 b = rotate(mesh.positions[ib] - center);
        const Vec3 c = rotate(mesh.positions[ic] - center);
        Vec3 n = (b - a).Cross(c - a).Normalized();
        if (n.z < 0.0f) n = n * -1.0f;   // two-sided: light the facet we see
        const float lambert = std::max(0.0f, n.Dot(light));
        const float shade = 0.28f + 0.72f * lambert;

        // Screen space: y grows downwards, so the model's up axis is
        // negated. Depth is the rotated z (bigger = closer).
        const float ax = ox + a.x * unit, ay = oy - a.y * unit;
        const float bx = ox + b.x * unit, by = oy - b.y * unit;
        const float cx2 = ox + c.x * unit, cy2 = oy - c.y * unit;
        const float area = (bx - ax) * (cy2 - ay) - (by - ay) * (cx2 - ax);
        if (std::fabs(area) < 1e-6f) continue;
        const float invArea = 1.0f / area;

        int minX = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx2}))));
        int maxX = std::min(pw - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx2}))));
        int minY = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy2}))));
        int maxY = std::min(ph - 1, static_cast<int>(std::ceil(std::max({ay, by, cy2}))));
        if (minX > maxX || minY > maxY) continue;

        const uint8_t rr = static_cast<uint8_t>(std::min(255.0f, kBaseR * shade));
        const uint8_t gg = static_cast<uint8_t>(std::min(255.0f, kBaseG * shade));
        const uint8_t bb = static_cast<uint8_t>(std::min(255.0f, kBaseB * shade));
        const uint32_t argb = 0xFF000000u | (uint32_t(rr) << 16)
                            | (uint32_t(gg) << 8) | uint32_t(bb);

        for (int py = minY; py <= maxY; ++py) {
            const float fy = py + 0.5f;
            for (int px = minX; px <= maxX; ++px) {
                const float fx = px + 0.5f;
                float w0 = ((bx - ax) * (fy - ay) - (by - ay) * (fx - ax)) * invArea;
                float w1 = ((fx - ax) * (cy2 - ay) - (fy - ay) * (cx2 - ax)) * invArea;
                // w0 weights c, w1 weights b, the rest weights a.
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                const float z = a.z * w2 + b.z * w1 + c.z * w0;
                const size_t idx = static_cast<size_t>(py) * pw + px;
                if (z <= depth[idx]) continue;
                depth[idx] = z;
                pixels[idx] = argb;
            }
        }
    }

    auto pm = std::make_shared<UCPixmap>();
    if (!pm->Init(pw, ph)) return nullptr;
    uint32_t* dst = pm->GetPixelData();
    if (!dst) return nullptr;
    std::memcpy(dst, pixels.data(), pixels.size() * sizeof(uint32_t));
    pm->MarkDirty();
    return pm;
}

} // namespace UltraCanvas
