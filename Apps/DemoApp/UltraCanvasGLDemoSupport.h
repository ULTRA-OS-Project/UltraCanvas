// Apps/DemoApp/UltraCanvasGLDemoSupport.h
// Shared helpers for the OpenGL 3D showcase demo tabs (models / shaders / Zarch).
// Header-only: small vector/matrix math, an OBJ mesh loader, and GL shader helpers.
// Only meaningful when the build enables OpenGL (ULTRACANVAS_ENABLE_GL).
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include <memory>
#include <string>

namespace UltraCanvas {

// Tab builders implemented in the per-tab .cpp files. Assembled by
// CreateGLSurfaceExamples() into a single tabbed showcase.
std::shared_ptr<UltraCanvasUIElement> CreateGLModelsTab();
std::shared_ptr<UltraCanvasUIElement> CreateGLShaderTab();
std::shared_ptr<UltraCanvasUIElement> CreateGLZarchTab();

} // namespace UltraCanvas

#ifdef ULTRACANVAS_ENABLE_GL

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <vector>
#include <array>
#include <iostream>

namespace UltraCanvas {
namespace gldemo {

// ===================================================================== math
constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
inline Vec3 Normalize(const Vec3& v) {
    float l = Length(v);
    return l > 1e-9f ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0, 0, 0};
}

// Column-major 4x4 matrix (OpenGL convention).
struct Mat4 {
    float m[16];

    static Mat4 Identity() {
        Mat4 r{};
        for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    // result = a * b   (apply b first, then a)
    static Mat4 Multiply(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += a.m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = sum;
            }
        }
        return r;
    }

    Mat4 operator*(const Mat4& o) const { return Multiply(*this, o); }
};

inline Mat4 Translate(float x, float y, float z) {
    Mat4 r = Mat4::Identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

inline Mat4 Scale(float x, float y, float z) {
    Mat4 r = Mat4::Identity();
    r.m[0] = x; r.m[5] = y; r.m[10] = z;
    return r;
}

inline Mat4 RotateX(float a) {
    Mat4 r = Mat4::Identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}

inline Mat4 RotateY(float a) {
    Mat4 r = Mat4::Identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}

inline Mat4 RotateZ(float a) {
    Mat4 r = Mat4::Identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}

inline Mat4 Perspective(float fovYRadians, float aspect, float zNear, float zFar) {
    Mat4 r{};
    for (int i = 0; i < 16; ++i) r.m[i] = 0.0f;
    float f = 1.0f / std::tan(fovYRadians * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zFar + zNear) / (zNear - zFar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return r;
}

inline Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = Normalize(center - eye);
    Vec3 s = Normalize(Cross(f, up));
    Vec3 u = Cross(s, f);
    Mat4 r = Mat4::Identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -Dot(s, eye);
    r.m[13] = -Dot(u, eye);
    r.m[14] = Dot(f, eye);
    return r;
}

// 3x3 normal matrix (inverse-transpose of the upper-left 3x3), stored as 9 floats
// column-major. For rigid + uniform-scale transforms this equals the rotation
// part, but computing it properly keeps lighting correct under non-uniform scale.
inline std::array<float, 9> NormalMatrix(const Mat4& mv) {
    float a = mv.m[0], b = mv.m[1], c = mv.m[2];
    float d = mv.m[4], e = mv.m[5], f = mv.m[6];
    float g = mv.m[8], h = mv.m[9], i = mv.m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(det) < 1e-9f) det = 1.0f;
    float invDet = 1.0f / det;
    // inverse of 3x3, then transpose -> assemble column-major
    std::array<float, 9> r{};
    r[0] = (e * i - f * h) * invDet;
    r[1] = (c * h - b * i) * invDet;
    r[2] = (b * f - c * e) * invDet;
    r[3] = (f * g - d * i) * invDet;
    r[4] = (a * i - c * g) * invDet;
    r[5] = (c * d - a * f) * invDet;
    r[6] = (d * h - e * g) * invDet;
    r[7] = (b * g - a * h) * invDet;
    r[8] = (a * e - b * d) * invDet;
    return r;
}

// ============================================================= shader utils
inline GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[GLDemo] Shader compile failed: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

inline GLuint LinkProgram(const char* vsSource, const char* fsSource) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
    if (!vs) return 0;
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);
    if (!fs) { glDeleteShader(vs); return 0; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "[GLDemo] Program link failed: " << log << std::endl;
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ================================================================ mesh / OBJ
// Interleaved vertex: position(3) + normal(3).
struct Mesh {
    std::vector<float> vertices;     // 6 floats per vertex
    std::vector<uint32_t> indices;
    Vec3 boundsMin{0, 0, 0};
    Vec3 boundsMax{0, 0, 0};

    size_t VertexCount() const { return vertices.size() / 6; }
    size_t TriangleCount() const { return indices.size() / 3; }

    // Centre the mesh on the origin and scale so it fits in a unit-ish sphere.
    void NormalizeToUnit() {
        Vec3 center{(boundsMin.x + boundsMax.x) * 0.5f,
                    (boundsMin.y + boundsMax.y) * 0.5f,
                    (boundsMin.z + boundsMax.z) * 0.5f};
        float radius = 1e-6f;
        for (size_t i = 0; i < vertices.size(); i += 6) {
            Vec3 p{vertices[i] - center.x, vertices[i + 1] - center.y, vertices[i + 2] - center.z};
            radius = std::max(radius, Length(p));
        }
        float inv = 1.0f / radius;
        for (size_t i = 0; i < vertices.size(); i += 6) {
            vertices[i]     = (vertices[i] - center.x) * inv;
            vertices[i + 1] = (vertices[i + 1] - center.y) * inv;
            vertices[i + 2] = (vertices[i + 2] - center.z) * inv;
        }
    }
};

// Minimal but robust Wavefront OBJ loader: handles v / vn / f with the
// a, a/t, a//n and a/t/n index forms, negative indices, and polygon
// triangulation (fan). Computes smooth normals when the file has none.
inline bool LoadOBJ(const std::string& path, Mesh& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[GLDemo] Could not open OBJ: " << path << std::endl;
        return false;
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;

    struct Key { int p, n; };
    std::vector<Key> faceKeys;       // triangulated corners, 0-based

    auto resolve = [](int idx, int count) -> int {
        if (idx > 0) return idx - 1;
        if (idx < 0) return count + idx;   // negative = relative to end
        return -1;
    };

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            Vec3 v; ss >> v.x >> v.y >> v.z;
            positions.push_back(v);
        } else if (tag == "vn") {
            Vec3 n; ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (tag == "f") {
            std::vector<Key> poly;
            std::string vert;
            while (ss >> vert) {
                int pi = 0, ni = 0;
                // parse "p", "p/t", "p//n", "p/t/n"
                size_t firstSlash = vert.find('/');
                if (firstSlash == std::string::npos) {
                    pi = std::stoi(vert);
                } else {
                    pi = std::stoi(vert.substr(0, firstSlash));
                    size_t secondSlash = vert.find('/', firstSlash + 1);
                    std::string nStr =
                        (secondSlash == std::string::npos) ? "" : vert.substr(secondSlash + 1);
                    if (!nStr.empty()) ni = std::stoi(nStr);
                }
                Key k;
                k.p = resolve(pi, (int)positions.size());
                k.n = (ni != 0) ? resolve(ni, (int)normals.size()) : -1;
                poly.push_back(k);
            }
            // fan triangulation
            for (size_t i = 2; i < poly.size(); ++i) {
                faceKeys.push_back(poly[0]);
                faceKeys.push_back(poly[i - 1]);
                faceKeys.push_back(poly[i]);
            }
        }
    }

    if (positions.empty() || faceKeys.empty()) {
        std::cerr << "[GLDemo] OBJ has no geometry: " << path << std::endl;
        return false;
    }

    // If the file supplied no normals, compute smooth per-vertex normals.
    bool haveNormals = !normals.empty();
    std::vector<Vec3> computed;
    if (!haveNormals) {
        computed.assign(positions.size(), Vec3{0, 0, 0});
        for (size_t i = 0; i + 2 < faceKeys.size(); i += 3) {
            int a = faceKeys[i].p, b = faceKeys[i + 1].p, c = faceKeys[i + 2].p;
            if (a < 0 || b < 0 || c < 0) continue;
            Vec3 fn = Cross(positions[b] - positions[a], positions[c] - positions[a]);
            computed[a] = computed[a] + fn;
            computed[b] = computed[b] + fn;
            computed[c] = computed[c] + fn;
        }
        for (auto& n : computed) n = Normalize(n);
    }

    out.vertices.clear();
    out.indices.clear();
    out.vertices.reserve(faceKeys.size() * 6);
    out.indices.reserve(faceKeys.size());

    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    uint32_t next = 0;
    for (const Key& k : faceKeys) {
        if (k.p < 0 || k.p >= (int)positions.size()) continue;
        const Vec3& p = positions[k.p];
        Vec3 n;
        if (haveNormals && k.n >= 0 && k.n < (int)normals.size())
            n = normals[k.n];
        else if (!haveNormals)
            n = computed[k.p];
        else
            n = Vec3{0, 1, 0};

        out.vertices.push_back(p.x);
        out.vertices.push_back(p.y);
        out.vertices.push_back(p.z);
        out.vertices.push_back(n.x);
        out.vertices.push_back(n.y);
        out.vertices.push_back(n.z);
        out.indices.push_back(next++);

        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
    out.boundsMin = lo;
    out.boundsMax = hi;
    return !out.vertices.empty();
}

// Procedural fallback so a tab is never empty if an asset is missing:
// a subdivided icosphere with position+normal interleaved.
inline Mesh MakeIcosphere(int subdivisions = 2) {
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<Vec3> verts = {
        Normalize({-1, t, 0}), Normalize({1, t, 0}), Normalize({-1, -t, 0}), Normalize({1, -t, 0}),
        Normalize({0, -1, t}), Normalize({0, 1, t}), Normalize({0, -1, -t}), Normalize({0, 1, -t}),
        Normalize({t, 0, -1}), Normalize({t, 0, 1}), Normalize({-t, 0, -1}), Normalize({-t, 0, 1}),
    };
    std::vector<std::array<int, 3>> faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
    };
    for (int s = 0; s < subdivisions; ++s) {
        std::vector<std::array<int, 3>> next;
        next.reserve(faces.size() * 4);
        auto mid = [&](int a, int b) {
            Vec3 m = Normalize(verts[a] + verts[b]);
            verts.push_back(m);
            return (int)verts.size() - 1;
        };
        for (auto& f : faces) {
            int ab = mid(f[0], f[1]);
            int bc = mid(f[1], f[2]);
            int ca = mid(f[2], f[0]);
            next.push_back({f[0], ab, ca});
            next.push_back({f[1], bc, ab});
            next.push_back({f[2], ca, bc});
            next.push_back({ab, bc, ca});
        }
        faces.swap(next);
    }
    Mesh mesh;
    uint32_t idx = 0;
    for (auto& f : faces) {
        for (int k = 0; k < 3; ++k) {
            const Vec3& p = verts[f[k]];  // unit sphere: position == normal
            mesh.vertices.insert(mesh.vertices.end(), {p.x, p.y, p.z, p.x, p.y, p.z});
            mesh.indices.push_back(idx++);
        }
    }
    mesh.boundsMin = {-1, -1, -1};
    mesh.boundsMax = {1, 1, 1};
    return mesh;
}

} // namespace gldemo
} // namespace UltraCanvas

#endif // ULTRACANVAS_ENABLE_GL
