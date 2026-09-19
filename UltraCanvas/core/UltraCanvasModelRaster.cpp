// core/UltraCanvasModelRaster.cpp
// The software mesh rasterizer declared in UltraCanvasModelRaster.h, and the
// model-file-to-raster-layer path built on it.
//
// The scan conversion is shared: both entry points hand it a projection and it
// z-buffers and shades whatever comes back. That is what lets the fixed-pose
// thumbnail (orthographic, the framing the Filer has always drawn) and the
// posed still (perspective, matching the GL viewer exactly) differ in nothing
// but their camera.
//
// Version: 2.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework

#include "UltraCanvasModelRaster.h"

#include "UltraCanvasModelPreview.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace UltraCanvas {

namespace {

    // A vertex after projection: where it lands, and how near it is. `depth`
    // is compared with `>`, so bigger is nearer whatever the projection means
    // by it.
    struct Projected {
        float x = 0.0f, y = 0.0f, depth = 0.0f;
        Vec3 rotated;          // for the facet normal, in the camera's frame
    };

    // The mesh, centred and scaled to a unit radius, is the input both
    // projections work from.
    struct NormalizedMesh {
        const Mesh3D* mesh = nullptr;
        Vec3 center;
        float radius = 1.0f;
    };

    // Yaw then pitch, the same order and sense as Mat4::RotationY(yaw) *
    // Mat4::RotationX(pitch) in the GL viewer.
    Vec3 RotateByPose(const Vec3& v, float yaw, float pitch) {
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        const Vec3 afterPitch{v.x, v.y * cp - v.z * sp, v.y * sp + v.z * cp};
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        return Vec3{afterPitch.x * cy + afterPitch.z * sy,
                    afterPitch.y,
                    -afterPitch.x * sy + afterPitch.z * cy};
    }

    // Prepares a mesh for rasterization, or reports why it cannot be drawn.
    bool Normalize(const Mesh3D& source, Mesh3D& storage, NormalizedMesh& out) {
        if (source.Empty()) return false;
        if (source.TriangleCount() > kModelPreviewTriangleCap) return false;

        // The bounds drive both the framing and the divisor below, so a mesh
        // that arrives without them gets them computed here.
        const Mesh3D* mesh = &source;
        if (!source.bounds.IsValid()) {
            storage = source;
            storage.ComputeBounds();
            mesh = &storage;
        }
        if (!mesh->bounds.IsValid()) return false;

        // A mesh whose every vertex sits at one point is refused, and the
        // extent has to be measured directly to catch it:
        // BoundingBox3D::Radius() substitutes 1.0 for a degenerate box rather
        // than returning zero, which is the right answer for framing and the
        // wrong one here. Without this the model would project to a sub-pixel
        // dot and the caller would get a fully transparent tile it then treats
        // as a successful preview - so the file would show nothing at all
        // instead of falling back to its type glyph.
        const Vec3 extent = mesh->bounds.max - mesh->bounds.min;
        if (!(extent.Length() > 1e-6f)) return false;

        out.mesh = mesh;
        out.center = mesh->bounds.Center();
        out.radius = mesh->bounds.Radius();
        return true;
    }

    // Scan-converts the mesh through `project`, flat-shaded and two-sided,
    // onto a transparent background.
    template <typename ProjectFn>
    std::shared_ptr<UCPixmap> RasterizeProjected(const NormalizedMesh& normalized,
                                                 int pw, int ph,
                                                 const Vec3& baseColor,
                                                 ProjectFn project) {
        const Mesh3D& mesh = *normalized.mesh;

        std::vector<float> depth(static_cast<size_t>(pw) * ph,
                                 -std::numeric_limits<float>::max());
        std::vector<uint32_t> pixels(static_cast<size_t>(pw) * ph, 0u);

        const Vec3 light = Vec3{0.4f, 0.7f, 1.0f}.Normalized();
        const float baseR = std::clamp(baseColor.x, 0.0f, 1.0f) * 255.0f;
        const float baseG = std::clamp(baseColor.y, 0.0f, 1.0f) * 255.0f;
        const float baseB = std::clamp(baseColor.z, 0.0f, 1.0f) * 255.0f;

        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const uint32_t ia = mesh.indices[t], ib = mesh.indices[t + 1],
                           ic = mesh.indices[t + 2];
            if (ia >= mesh.positions.size() || ib >= mesh.positions.size() ||
                ic >= mesh.positions.size()) continue;

            Projected a, b, c;
            if (!project(mesh.positions[ia], a) ||
                !project(mesh.positions[ib], b) ||
                !project(mesh.positions[ic], c)) continue;

            // Flat shading from the triangle's own geometry: STL facet normals
            // are often wrong or absent, and a preview that trusts them shows
            // a model lit from inside.
            Vec3 n = (b.rotated - a.rotated).Cross(c.rotated - a.rotated).Normalized();
            if (n.z < 0.0f) n = n * -1.0f;   // two-sided: light the facet we see
            const float lambert = std::max(0.0f, n.Dot(light));
            const float shade = 0.28f + 0.72f * lambert;

            const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (std::fabs(area) < 1e-6f) continue;
            const float invArea = 1.0f / area;

            int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
            int maxX = std::min(pw - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
            int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
            int maxY = std::min(ph - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
            if (minX > maxX || minY > maxY) continue;

            const uint8_t rr = static_cast<uint8_t>(std::min(255.0f, baseR * shade));
            const uint8_t gg = static_cast<uint8_t>(std::min(255.0f, baseG * shade));
            const uint8_t bb = static_cast<uint8_t>(std::min(255.0f, baseB * shade));
            const uint32_t argb = 0xFF000000u | (uint32_t(rr) << 16)
                                | (uint32_t(gg) << 8) | uint32_t(bb);

            for (int py = minY; py <= maxY; ++py) {
                const float fy = py + 0.5f;
                for (int px = minX; px <= maxX; ++px) {
                    const float fx = px + 0.5f;
                    const float w0 = ((b.x - a.x) * (fy - a.y) - (b.y - a.y) * (fx - a.x)) * invArea;
                    const float w1 = ((fx - a.x) * (c.y - a.y) - (fy - a.y) * (c.x - a.x)) * invArea;
                    // w0 weights c, w1 weights b, the rest weights a.
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                    const float z = a.depth * w2 + b.depth * w1 + c.depth * w0;
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

    int DeviceSize(int logical, float scale) {
        return std::max(8, static_cast<int>(std::lround(logical * std::max(1.0f, scale))));
    }

    std::string LowerExtOf(const std::string& path) {
        std::string e = std::filesystem::path(path).extension().string();
        if (!e.empty() && e[0] == '.') e.erase(0, 1);
        std::transform(e.begin(), e.end(), e.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return e;
    }

    std::string StemOf(const std::string& path) {
        std::string stem = std::filesystem::path(path).stem().string();
        return stem.empty() ? std::string("Model") : stem;
    }

} // namespace

// ===========================================================================
// A MESH AS A PICTURE
// ===========================================================================

std::shared_ptr<UCPixmap> RenderMeshPreviewPixmap(const Mesh3D& source,
                                                  int w, int h, float scale) {
    Mesh3D storage;
    NormalizedMesh normalized;
    if (!Normalize(source, storage, normalized)) return nullptr;

    const int pw = DeviceSize(w, scale);
    const int ph = DeviceSize(h, scale);

    // Yaw / pitch of the standard "look at it from the front left and
    // slightly above" pose used by model viewers, orthographic: a thumbnail
    // wants the silhouette, not the foreshortening.
    constexpr float kYaw   = -0.55f;   // radians
    constexpr float kPitch =  0.42f;
    const float cy = std::cos(kYaw),   sy = std::sin(kYaw);
    const float cp = std::cos(kPitch), sp = std::sin(kPitch);

    const Vec3 center = normalized.center;
    const MeshUpAxis mesh3DUpAxis = normalized.mesh->upAxis;
    // 0.92 leaves a hair of margin so the silhouette never touches the
    // tile edge; the rotated bounding sphere fits in either direction.
    const float unit = 0.92f * 0.5f * static_cast<float>(std::min(pw, ph)) / normalized.radius;
    const float ox = pw * 0.5f, oy = ph * 0.5f;

    // Screen space: y grows downwards, so the model's up axis is negated.
    // Depth is the rotated z (bigger = closer).
    auto project = [&](const Vec3& position, Projected& out) {
        // Upright first, exactly as the GL viewer does it, or a Z-up mesh
        // thumbnails on its nose while the live view stands it up.
        const Vec3 v = ToViewerUp(position - center, mesh3DUpAxis);
        const float x1 =  v.x * cy + v.z * sy;
        const float z1 = -v.x * sy + v.z * cy;
        out.rotated = Vec3{x1, v.y * cp - z1 * sp, v.y * sp + z1 * cp};
        out.x = ox + out.rotated.x * unit;
        out.y = oy - out.rotated.y * unit;
        out.depth = out.rotated.z;
        return true;
    };

    return RasterizeProjected(normalized, pw, ph,
                              Vec3{132.0f / 255.0f, 158.0f / 255.0f, 205.0f / 255.0f},
                              project);
}

std::shared_ptr<UCPixmap> RenderMeshPixmap(const Mesh3D& source, int w, int h,
                                           const ModelViewPose& pose,
                                           const Vec3& modelColor, float scale) {
    Mesh3D storage;
    NormalizedMesh normalized;
    if (!Normalize(source, storage, normalized)) return nullptr;

    const int pw = DeviceSize(w, scale);
    const int ph = DeviceSize(h, scale);

    // The GL viewer's camera, to the letter: the model is normalised to a unit
    // radius, rotated by yaw then pitch, and seen from (0, 0, distance)
    // through a 45° vertical field of view. Matching it is the whole point —
    // the still has to be the view that was on screen.
    const float distance = std::max(0.2f, pose.distance);
    constexpr float kFovY = 45.0f * 3.14159265358979323846f / 180.0f;
    const float focal = 1.0f / std::tan(kFovY * 0.5f);
    const float aspect = ph > 0 ? static_cast<float>(pw) / static_cast<float>(ph) : 1.0f;
    const float invRadius = normalized.radius > 1e-6f ? 1.0f / normalized.radius : 1.0f;
    const Vec3 center = normalized.center;
    const MeshUpAxis mesh3DUpAxis = normalized.mesh->upAxis;
    const float ox = pw * 0.5f, oy = ph * 0.5f;

    auto project = [&](const Vec3& position, Projected& out) {
        // The still has to be the view that was on screen, so it stands the
        // mesh up on the same axis the GL viewer does before posing it.
        const Vec3 unitSpace = ToViewerUp(position - center, mesh3DUpAxis) * invRadius;
        out.rotated = RotateByPose(unitSpace, pose.yaw, pose.pitch);
        // Camera space: the eye sits at +Z looking down -Z, so everything in
        // front of it has a negative z once translated.
        const float viewZ = out.rotated.z - distance;
        if (viewZ > -0.01f) return false;        // behind the eye or in the near plane
        const float invW = -1.0f / viewZ;
        out.x = ox + (focal / aspect) * out.rotated.x * invW * ox;
        out.y = oy - focal * out.rotated.y * invW * oy;
        // Nearer means a smaller distance in front of the eye; the scan
        // converter keeps the larger depth, so hand it the reciprocal.
        out.depth = invW;
        return true;
    };

    return RasterizeProjected(normalized, pw, ph, modelColor, project);
}

// ===========================================================================
// A MODEL FILE AS AN EDITABLE LAYER
// ===========================================================================

bool IsModelGraphicsPath(const std::string& path) {
    const std::string ext = LowerExtOf(path);
    if (ext.empty()) return false;
    return CanPreviewModelExtension(ext);
}

std::vector<std::string> GetModelRasterExtensions() {
    return PreviewableModelExtensions();
}

ModelSourceInfo InspectModelFile(const std::string& path) {
    ModelSourceInfo info;
    if (!IsModelGraphicsPath(path)) {
        info.error = "No 3D reader in this build handles ." + LowerExtOf(path);
        return info;
    }
    Mesh3D mesh;
    if (!LoadModelPreviewMesh(path, mesh)) {
        info.error = "Could not read any geometry from " + path;
        return info;
    }
    if (!mesh.bounds.IsValid()) mesh.ComputeBounds();
    info.ok = true;
    info.triangleCount = mesh.TriangleCount();
    info.vertexCount = mesh.VertexCount();
    info.bounds = mesh.bounds;
    return info;
}

std::shared_ptr<UCRasterLayer> RasterizeMesh(const Mesh3D& mesh,
                                             const ModelRasterOptions& options,
                                             std::string& error) {
    error.clear();
    const int w = options.width, h = options.height;
    if (w <= 0 || h <= 0) {
        error = "The raster size must be at least 1 x 1 pixel";
        return nullptr;
    }
    const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (options.maxPixels > 0 && pixels > options.maxPixels) {
        error = "The requested raster size (" + std::to_string(w) + " x " + std::to_string(h) +
                ") is larger than this rasterization allows";
        return nullptr;
    }
    if (mesh.TriangleCount() > kModelPreviewTriangleCap) {
        error = "The model has " + std::to_string(mesh.TriangleCount()) +
                " triangles, more than this renderer draws";
        return nullptr;
    }

    auto pixmap = RenderMeshPixmap(mesh, w, h, options.pose, options.modelColor, 1.0f);
    if (!pixmap) {
        error = "The model holds no drawable geometry";
        return nullptr;
    }

    auto layer = std::make_shared<UCRasterLayer>(w, h, options.background, mesh.name);
    if (!layer->IsValid()) {
        error = "Could not allocate the raster layer";
        return nullptr;
    }

    // The pixmap is opaque-or-transparent ARGB32 with no partial alpha (the
    // rasterizer writes either a shaded pixel or nothing), so the model can be
    // copied straight over the background.
    const uint32_t* src = pixmap->GetPixelData();
    if (!src) {
        error = "The rendered model exposed no pixels";
        return nullptr;
    }
    const int pw = std::min(w, pixmap->GetRawWidth());
    const int ph = std::min(h, pixmap->GetRawHeight());
    for (int y = 0; y < ph; ++y) {
        const uint32_t* srcRow = src + static_cast<size_t>(y) * pixmap->GetRawWidth();
        uint8_t* dstRow = layer->Row(y);
        for (int x = 0; x < pw; ++x) {
            const uint32_t p = srcRow[x];
            const uint8_t a = static_cast<uint8_t>((p >> 24) & 0xFF);
            if (a == 0) continue;
            dstRow[4 * x + 0] = static_cast<uint8_t>((p >> 16) & 0xFF);
            dstRow[4 * x + 1] = static_cast<uint8_t>((p >>  8) & 0xFF);
            dstRow[4 * x + 2] = static_cast<uint8_t>( p        & 0xFF);
            dstRow[4 * x + 3] = 255;
        }
    }
    return layer;
}

std::shared_ptr<UCRasterLayer> RasterizeModelFile(const std::string& path,
                                                  const ModelRasterOptions& options,
                                                  std::string& error) {
    error.clear();
    if (!IsModelGraphicsPath(path)) {
        error = "No 3D reader in this build handles ." + LowerExtOf(path);
        return nullptr;
    }
    Mesh3D mesh;
    if (!LoadModelPreviewMesh(path, mesh)) {
        error = "Could not read any geometry from " + path;
        return nullptr;
    }
    if (mesh.name.empty()) mesh.name = StemOf(path);
    auto layer = RasterizeMesh(mesh, options, error);
    if (layer) layer->name = StemOf(path);
    return layer;
}

} // namespace UltraCanvas
