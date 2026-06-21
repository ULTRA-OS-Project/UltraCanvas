// Plugins/Models/STL/UltraCanvasSTLLoader.cpp
// Implementation of the self-contained STL loader/saver
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework

#include "UltraCanvasSTLLoader.h"

#include <fstream>
#include <sstream>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace UltraCanvas {

namespace {

    // Binary STL geometry: 80-byte header + uint32 facet count, then 50 bytes
    // per facet (12 floats = normal + 3 vertices, + 2-byte attribute count).
    constexpr size_t kBinaryHeaderSize = 80;
    constexpr size_t kBinaryCountSize = 4;
    constexpr size_t kBinaryFacetSize = 50;

    void SetError(std::string* outError, const std::string& msg) {
        if (outError) *outError = msg;
    }

    // Read a little-endian 32-bit unsigned int from a byte cursor.
    uint32_t ReadU32LE(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

    // Read a little-endian IEEE-754 float from a byte cursor.
    float ReadF32LE(const uint8_t* p) {
        uint32_t bits = ReadU32LE(p);
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    void WriteU32LE(std::ostream& os, uint32_t v) {
        char b[4] = {
            static_cast<char>(v & 0xFF),
            static_cast<char>((v >> 8) & 0xFF),
            static_cast<char>((v >> 16) & 0xFF),
            static_cast<char>((v >> 24) & 0xFF)
        };
        os.write(b, 4);
    }

    void WriteU16LE(std::ostream& os, uint16_t v) {
        char b[2] = {
            static_cast<char>(v & 0xFF),
            static_cast<char>((v >> 8) & 0xFF)
        };
        os.write(b, 2);
    }

    void WriteF32LE(std::ostream& os, float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        WriteU32LE(os, bits);
    }

    bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out, std::string* err) {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) {
            SetError(err, "Cannot open file: " + path);
            return false;
        }
        std::streamsize size = in.tellg();
        if (size < 0) {
            SetError(err, "Cannot determine file size: " + path);
            return false;
        }
        in.seekg(0, std::ios::beg);
        out.resize(static_cast<size_t>(size));
        if (size > 0 && !in.read(reinterpret_cast<char*>(out.data()), size)) {
            SetError(err, "Failed to read file: " + path);
            return false;
        }
        return true;
    }

} // anonymous namespace

bool UltraCanvasSTLLoader::HasSTLExtension(const std::string& filePath) {
    size_t dot = filePath.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = filePath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "stl";
}

bool UltraCanvasSTLLoader::LooksLikeBinary(const std::vector<uint8_t>& data) {
    // Too small to be valid binary STL -> treat as ASCII.
    if (data.size() < kBinaryHeaderSize + kBinaryCountSize) return false;

    uint32_t count = ReadU32LE(data.data() + kBinaryHeaderSize);
    size_t expected = kBinaryHeaderSize + kBinaryCountSize +
                      static_cast<size_t>(count) * kBinaryFacetSize;

    // The size match is the canonical, reliable discriminator: some binary STL
    // files start with the ASCII token "solid", so a header sniff alone is unsafe.
    if (expected == data.size()) return true;

    // Fall back to a header sniff: ASCII STL begins with "solid" then whitespace.
    bool startsWithSolid = data.size() >= 5 &&
                           std::memcmp(data.data(), "solid", 5) == 0;
    return !startsWithSolid;
}

bool UltraCanvasSTLLoader::Load(const std::string& filePath, Mesh3D& outMesh,
                                std::string* outError) {
    std::vector<uint8_t> data;
    if (!ReadWholeFile(filePath, data, outError)) return false;
    if (!LoadFromMemory(data, outMesh, outError)) return false;
    if (outMesh.name.empty()) {
        size_t slash = filePath.find_last_of("/\\");
        outMesh.name = (slash == std::string::npos) ? filePath : filePath.substr(slash + 1);
    }
    return true;
}

bool UltraCanvasSTLLoader::LoadFromMemory(const std::vector<uint8_t>& data, Mesh3D& outMesh,
                                          std::string* outError) {
    outMesh.Clear();
    if (data.empty()) {
        SetError(outError, "Empty STL data");
        return false;
    }

    if (LooksLikeBinary(data)) {
        return ParseBinary(data, outMesh, outError);
    }
    std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    return ParseAscii(text, outMesh, outError);
}

bool UltraCanvasSTLLoader::ParseBinary(const std::vector<uint8_t>& data, Mesh3D& outMesh,
                                       std::string* outError) {
    if (data.size() < kBinaryHeaderSize + kBinaryCountSize) {
        SetError(outError, "Binary STL truncated (no header/count)");
        return false;
    }

    uint32_t count = ReadU32LE(data.data() + kBinaryHeaderSize);
    size_t needed = kBinaryHeaderSize + kBinaryCountSize +
                    static_cast<size_t>(count) * kBinaryFacetSize;
    if (data.size() < needed) {
        SetError(outError, "Binary STL truncated (facet data shorter than declared count)");
        return false;
    }

    outMesh.positions.reserve(static_cast<size_t>(count) * 3);
    outMesh.normals.reserve(static_cast<size_t>(count) * 3);
    outMesh.indices.reserve(static_cast<size_t>(count) * 3);

    const uint8_t* cursor = data.data() + kBinaryHeaderSize + kBinaryCountSize;
    bool anyDegenerateNormal = false;

    for (uint32_t f = 0; f < count; ++f) {
        Vec3 normal{ ReadF32LE(cursor),     ReadF32LE(cursor + 4), ReadF32LE(cursor + 8) };
        Vec3 v0{     ReadF32LE(cursor + 12), ReadF32LE(cursor + 16), ReadF32LE(cursor + 20) };
        Vec3 v1{     ReadF32LE(cursor + 24), ReadF32LE(cursor + 28), ReadF32LE(cursor + 32) };
        Vec3 v2{     ReadF32LE(cursor + 36), ReadF32LE(cursor + 40), ReadF32LE(cursor + 44) };
        cursor += kBinaryFacetSize; // skip the 2-byte attribute byte count too

        if (normal.Length() < 1e-12f) {
            anyDegenerateNormal = true;
            normal = (v1 - v0).Cross(v2 - v0).Normalized();
        }

        uint32_t base = static_cast<uint32_t>(outMesh.positions.size());
        outMesh.positions.push_back(v0);
        outMesh.positions.push_back(v1);
        outMesh.positions.push_back(v2);
        outMesh.normals.push_back(normal);
        outMesh.normals.push_back(normal);
        outMesh.normals.push_back(normal);
        outMesh.indices.push_back(base);
        outMesh.indices.push_back(base + 1);
        outMesh.indices.push_back(base + 2);
    }

    if (outMesh.Empty()) {
        SetError(outError, "Binary STL contains no triangles");
        return false;
    }
    (void)anyDegenerateNormal;
    outMesh.ComputeBounds();
    return true;
}

bool UltraCanvasSTLLoader::ParseAscii(const std::string& text, Mesh3D& outMesh,
                                      std::string* outError) {
    std::istringstream in(text);
    std::string token;

    Vec3 currentNormal;
    Vec3 verts[3];
    int vertIndex = 0;

    auto pushFacet = [&](const Vec3& normal) {
        Vec3 n = normal;
        if (n.Length() < 1e-12f) {
            n = (verts[1] - verts[0]).Cross(verts[2] - verts[0]).Normalized();
        }
        uint32_t base = static_cast<uint32_t>(outMesh.positions.size());
        for (int i = 0; i < 3; ++i) {
            outMesh.positions.push_back(verts[i]);
            outMesh.normals.push_back(n);
        }
        outMesh.indices.push_back(base);
        outMesh.indices.push_back(base + 1);
        outMesh.indices.push_back(base + 2);
    };

    while (in >> token) {
        if (token == "solid") {
            // Remainder of the line is the optional solid name.
            std::string rest;
            std::getline(in, rest);
            size_t start = rest.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) {
                size_t end = rest.find_last_not_of(" \t\r\n");
                outMesh.name = rest.substr(start, end - start + 1);
            }
        } else if (token == "facet") {
            // Expect: facet normal nx ny nz
            std::string normalKeyword;
            in >> normalKeyword; // "normal"
            in >> currentNormal.x >> currentNormal.y >> currentNormal.z;
            vertIndex = 0;
        } else if (token == "vertex") {
            if (vertIndex < 3) {
                in >> verts[vertIndex].x >> verts[vertIndex].y >> verts[vertIndex].z;
                ++vertIndex;
            } else {
                // Non-triangular facet: skip the extra vertex coordinates.
                Vec3 ignored;
                in >> ignored.x >> ignored.y >> ignored.z;
            }
        } else if (token == "endfacet") {
            if (vertIndex == 3) {
                pushFacet(currentNormal);
            }
            vertIndex = 0;
        }
        // "outer", "loop", "endloop", "endsolid" and unknown tokens are ignored.
    }

    if (outMesh.Empty()) {
        SetError(outError, "ASCII STL contains no complete triangles");
        return false;
    }
    outMesh.ComputeBounds();
    return true;
}

bool UltraCanvasSTLLoader::Save(const std::string& filePath, const Mesh3D& mesh,
                                STLFormat format, std::string* outError) {
    if (mesh.Empty()) {
        SetError(outError, "Cannot save empty mesh to STL");
        return false;
    }
    if (mesh.indices.size() % 3 != 0) {
        SetError(outError, "Mesh index count is not a multiple of 3");
        return false;
    }
    if (format == STLFormat::ASCII) {
        return WriteAscii(filePath, mesh, outError);
    }
    // Auto and Binary both write binary STL (compact and exact).
    return WriteBinary(filePath, mesh, outError);
}

bool UltraCanvasSTLLoader::WriteBinary(const std::string& filePath, const Mesh3D& mesh,
                                       std::string* outError) {
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out) {
        SetError(outError, "Cannot open file for writing: " + filePath);
        return false;
    }

    // 80-byte header (zero-filled, with a short signature for friendliness).
    char header[kBinaryHeaderSize] = {0};
    const char* sig = "UltraCanvas STL";
    std::memcpy(header, sig, std::strlen(sig));
    out.write(header, kBinaryHeaderSize);

    uint32_t triangleCount = static_cast<uint32_t>(mesh.TriangleCount());
    WriteU32LE(out, triangleCount);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        const Vec3& v0 = mesh.positions[a];
        const Vec3& v1 = mesh.positions[b];
        const Vec3& v2 = mesh.positions[c];

        // Prefer the stored normal; recompute if absent/degenerate.
        Vec3 n = (a < mesh.normals.size()) ? mesh.normals[a] : Vec3{};
        if (n.Length() < 1e-12f) {
            n = (v1 - v0).Cross(v2 - v0).Normalized();
        }

        WriteF32LE(out, n.x);  WriteF32LE(out, n.y);  WriteF32LE(out, n.z);
        WriteF32LE(out, v0.x); WriteF32LE(out, v0.y); WriteF32LE(out, v0.z);
        WriteF32LE(out, v1.x); WriteF32LE(out, v1.y); WriteF32LE(out, v1.z);
        WriteF32LE(out, v2.x); WriteF32LE(out, v2.y); WriteF32LE(out, v2.z);
        WriteU16LE(out, 0); // attribute byte count
    }

    if (!out) {
        SetError(outError, "Write error while saving binary STL: " + filePath);
        return false;
    }
    return true;
}

bool UltraCanvasSTLLoader::WriteAscii(const std::string& filePath, const Mesh3D& mesh,
                                      std::string* outError) {
    std::ofstream out(filePath, std::ios::trunc);
    if (!out) {
        SetError(outError, "Cannot open file for writing: " + filePath);
        return false;
    }

    std::string solidName = mesh.name.empty() ? "ultracanvas" : mesh.name;
    out << "solid " << solidName << "\n";
    out.setf(std::ios::scientific);
    out.precision(6);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        const Vec3& v0 = mesh.positions[a];
        const Vec3& v1 = mesh.positions[b];
        const Vec3& v2 = mesh.positions[c];

        Vec3 n = (a < mesh.normals.size()) ? mesh.normals[a] : Vec3{};
        if (n.Length() < 1e-12f) {
            n = (v1 - v0).Cross(v2 - v0).Normalized();
        }

        out << "  facet normal " << n.x << " " << n.y << " " << n.z << "\n";
        out << "    outer loop\n";
        out << "      vertex " << v0.x << " " << v0.y << " " << v0.z << "\n";
        out << "      vertex " << v1.x << " " << v1.y << " " << v1.z << "\n";
        out << "      vertex " << v2.x << " " << v2.y << " " << v2.z << "\n";
        out << "    endloop\n";
        out << "  endfacet\n";
    }

    out << "endsolid " << solidName << "\n";

    if (!out) {
        SetError(outError, "Write error while saving ASCII STL: " + filePath);
        return false;
    }
    return true;
}

} // namespace UltraCanvas
