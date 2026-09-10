// Plugins/Models/OBJ/UltraCanvasOBJConverter.cpp
// The OBJ and MTL parsers and writers.
//
// Two things make OBJ harder than it looks, and both are handled here rather
// than papered over:
//
//   * Positions, texture coordinates and normals are three independent index
//     streams. A vertex in the document is one (v, vt, vn) combination, so the
//     reader de-duplicates combinations rather than assuming the streams are
//     parallel — the E-45 sample has 11 749 positions against 12 227 texture
//     coordinates.
//   * Faces are n-gons. The document keeps them (PrimitiveMode::Polygons with
//     FaceStarts), so a quad mesh survives a round trip instead of being
//     silently triangulated. The E-45 sample is 8 110 quads and no triangles.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/OBJ/UltraCanvasOBJConverter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;

namespace {

// ===== TEXT HELPERS =====

std::string Trim(const std::string& text) {
    size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::vector<std::string> SplitWhitespace(const std::string& text) {
    std::vector<std::string> parts;
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) parts.push_back(token);
    return parts;
}

// The directory a referenced file resolves against, with the trailing
// separator. Empty when the path has no directory part.
std::string DirectoryOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string FileStem(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) name.erase(dot);
    return name;
}

// OBJ indices are 1-based, and negative values count back from the most
// recent element. Returns -1 for anything that does not resolve.
int ResolveIndex(long value, size_t count) {
    if (value > 0) {
        const size_t index = static_cast<size_t>(value - 1);
        return index < count ? static_cast<int>(index) : -1;
    }
    if (value < 0) {
        const long index = static_cast<long>(count) + value;
        return index >= 0 && static_cast<size_t>(index) < count ? static_cast<int>(index) : -1;
    }
    return -1;   // 0 is not a valid OBJ index
}

// A face corner: "v", "v/vt", "v//vn" or "v/vt/vn".
struct Corner {
    int Position = -1;
    int TexCoord = -1;
    int Normal = -1;

    bool operator<(const Corner& o) const {
        if (Position != o.Position) return Position < o.Position;
        if (TexCoord != o.TexCoord) return TexCoord < o.TexCoord;
        return Normal < o.Normal;
    }
};

// The trailing filename of a map line, after any -option arguments. MTL map
// options take a known number of values, and the file name is everything
// after them — which is how a name containing spaces still parses.
std::string ParseMapLine(const std::string& arguments, TextureRef& ref) {
    const std::vector<std::string> tokens = SplitWhitespace(arguments);
    size_t i = 0;
    while (i < tokens.size() && !tokens[i].empty() && tokens[i][0] == '-') {
        const std::string& option = tokens[i];
        size_t values = 1;
        if (option == "-s" || option == "-o" || option == "-t") values = 3;
        else if (option == "-mm") values = 2;
        else if (option == "-clamp" || option == "-blendu" || option == "-blendv" ||
                 option == "-cc") values = 1;
        else if (option == "-bm" || option == "-boost" || option == "-texres" ||
                 option == "-imfchan") values = 1;

        if (option == "-s" && i + 2 < tokens.size()) {
            ref.ScaleU = std::strtof(tokens[i + 1].c_str(), nullptr);
            ref.ScaleV = std::strtof(tokens[i + 2].c_str(), nullptr);
        } else if (option == "-o" && i + 2 < tokens.size()) {
            ref.OffsetU = std::strtof(tokens[i + 1].c_str(), nullptr);
            ref.OffsetV = std::strtof(tokens[i + 2].c_str(), nullptr);
        } else if (option == "-bm" && i + 1 < tokens.size()) {
            ref.Scale = std::strtof(tokens[i + 1].c_str(), nullptr);
        }
        i += 1 + values;
    }

    // Rejoin the remainder: a texture name may contain spaces, and the E-45's
    // own material library is called "E 45 Aircraft_obj.mtl".
    std::string name;
    for (; i < tokens.size(); ++i) {
        if (!name.empty()) name += " ";
        name += tokens[i];
    }
    return name;
}

// ===== OBJ READER =====

class Reader {
public:
    Reader(const ConversionOptions& options, std::string baseDirectory)
            : options_(options), baseDirectory_(std::move(baseDirectory)) {}

    std::shared_ptr<ModelDocument> Run(std::istream& stream) {
        document_ = std::make_shared<ModelDocument>();
        document_->SourceFormat = "obj";
        // OBJ declares no axis or unit. Y-up right-handed is the de-facto
        // convention every exporter and viewer assumes, so that is what the
        // document records — with SourceUnit left Unspecified rather than
        // invented, unless the caller stated one.
        document_->Up = UpAxis::YUp;
        document_->Chirality = Handedness::RightHanded;
        document_->SourceUnit = options_.AssumeUnit;
        document_->UnitScaleToMeters = MetersPerUnit(options_.AssumeUnit);

        std::string line;
        while (std::getline(stream, line)) ReadLine(line);
        FlushPrimitive();
        FlushMesh();

        if (document_->Meshes.empty()) {
            options_.Warn("OBJ: no faces found");
            return nullptr;
        }
        Finish();
        return document_;
    }

private:
    // --- line dispatch ---

    void ReadLine(std::string line) {
        if (!line.empty() && line.back() == '\r') line.pop_back();   // CRLF
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') return;

        const size_t space = trimmed.find_first_of(" \t");
        const std::string keyword = trimmed.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string()
                                                            : Trim(trimmed.substr(space + 1));

        if (keyword == "v")            ReadPosition(rest);
        else if (keyword == "vt")      ReadTexCoord(rest);
        else if (keyword == "vn")      ReadNormal(rest);
        else if (keyword == "f")       ReadFace(rest);
        else if (keyword == "o")       BeginObject(rest);
        else if (keyword == "g")       BeginGroup(rest);
        else if (keyword == "usemtl")  UseMaterial(rest);
        else if (keyword == "mtllib")  ReadMaterialLibrary(rest);
        else if (keyword == "s")       ReadSmoothing(rest);
        else if (keyword == "l" || keyword == "p") {
            if (!warnedLinesPoints_) {
                options_.Warn("OBJ: line and point elements are not imported");
                warnedLinesPoints_ = true;
            }
        }
    }

    void ReadPosition(const std::string& arguments) {
        const std::vector<std::string> parts = SplitWhitespace(arguments);
        if (parts.size() < 3) return;
        positions_.emplace_back(std::strtod(parts[0].c_str(), nullptr),
                                std::strtod(parts[1].c_str(), nullptr),
                                std::strtod(parts[2].c_str(), nullptr));
        // The widespread extension "v x y z r g b": a per-vertex colour, which
        // scanners and Blender both emit. A 4th value alone is the rational w.
        if (parts.size() >= 6) {
            colors_.push_back(Vec3f(std::strtof(parts[3].c_str(), nullptr),
                                    std::strtof(parts[4].c_str(), nullptr),
                                    std::strtof(parts[5].c_str(), nullptr)));
        } else if (!colors_.empty()) {
            colors_.push_back(Vec3f(1.0f, 1.0f, 1.0f));
        }
    }

    void ReadTexCoord(const std::string& arguments) {
        const std::vector<std::string> parts = SplitWhitespace(arguments);
        if (parts.empty()) return;
        texcoords_.push_back(std::strtof(parts[0].c_str(), nullptr));
        texcoords_.push_back(parts.size() > 1 ? std::strtof(parts[1].c_str(), nullptr) : 0.0f);
    }

    void ReadNormal(const std::string& arguments) {
        const std::vector<std::string> parts = SplitWhitespace(arguments);
        if (parts.size() < 3) return;
        normals_.push_back(Vec3f(std::strtof(parts[0].c_str(), nullptr),
                                 std::strtof(parts[1].c_str(), nullptr),
                                 std::strtof(parts[2].c_str(), nullptr)));
    }

    Corner ParseCorner(const std::string& token) {
        Corner corner;
        // Split on '/' keeping empty fields: "1//3" means position and normal.
        std::string fields[3];
        int field = 0;
        for (char c : token) {
            if (c == '/') { if (++field > 2) break; continue; }
            fields[field].push_back(c);
        }
        if (!fields[0].empty())
            corner.Position = ResolveIndex(std::strtol(fields[0].c_str(), nullptr, 10),
                                           positions_.size());
        if (!fields[1].empty())
            corner.TexCoord = ResolveIndex(std::strtol(fields[1].c_str(), nullptr, 10),
                                           texcoords_.size() / 2);
        if (!fields[2].empty())
            corner.Normal = ResolveIndex(std::strtol(fields[2].c_str(), nullptr, 10),
                                         normals_.size());
        return corner;
    }

    void ReadFace(const std::string& arguments) {
        const std::vector<std::string> tokens = SplitWhitespace(arguments);
        if (tokens.size() < 3) {
            options_.Warn("OBJ: a face with fewer than 3 corners was skipped");
            return;
        }

        std::vector<uint32_t> face;
        face.reserve(tokens.size());
        for (const std::string& token : tokens) {
            const Corner corner = ParseCorner(token);
            if (corner.Position < 0) {
                if (!warnedBadIndex_) {
                    options_.Warn("OBJ: a face references a vertex the file does not define; "
                                  "those corners are dropped");
                    warnedBadIndex_ = true;
                }
                continue;
            }
            face.push_back(VertexFor(corner));
        }
        if (face.size() < 3) return;

        maxFaceCorners_ = std::max(maxFaceCorners_, face.size());
        faceStarts_.push_back(static_cast<uint32_t>(indices_.size()));
        indices_.insert(indices_.end(), face.begin(), face.end());
    }

    // One document vertex per distinct (position, texcoord, normal) triple.
    uint32_t VertexFor(const Corner& corner) {
        auto it = vertexCache_.find(corner);
        if (it != vertexCache_.end()) return it->second;

        const uint32_t index = static_cast<uint32_t>(primitivePositions_.size());
        vertexCache_.emplace(corner, index);

        primitivePositions_.push_back(positions_[static_cast<size_t>(corner.Position)]);

        if (corner.Normal >= 0 && static_cast<size_t>(corner.Normal) < normals_.size())
            primitiveNormals_.push_back(normals_[static_cast<size_t>(corner.Normal)]);
        else if (!primitiveNormals_.empty())
            primitiveNormals_.push_back(Vec3f(0.0f, 0.0f, 0.0f));

        if (corner.TexCoord >= 0 && static_cast<size_t>(corner.TexCoord) * 2 + 1 < texcoords_.size()) {
            primitiveTexCoords_.push_back(texcoords_[static_cast<size_t>(corner.TexCoord) * 2]);
            primitiveTexCoords_.push_back(texcoords_[static_cast<size_t>(corner.TexCoord) * 2 + 1]);
        } else if (!primitiveTexCoords_.empty()) {
            primitiveTexCoords_.push_back(0.0f);
            primitiveTexCoords_.push_back(0.0f);
        }

        if (!colors_.empty() && static_cast<size_t>(corner.Position) < colors_.size()) {
            const Vec3f& c = colors_[static_cast<size_t>(corner.Position)];
            primitiveColors_.push_back(c.x);
            primitiveColors_.push_back(c.y);
            primitiveColors_.push_back(c.z);
        }
        return index;
    }

    // --- structure ---

    void BeginObject(const std::string& name) {
        FlushPrimitive();
        FlushMesh();
        meshName_ = name;
    }

    void BeginGroup(const std::string& name) {
        // A group inside an object names the following faces without starting
        // a new mesh — splitting on `g` as well would fragment files that use
        // it as a tag rather than a container.
        FlushPrimitive();
        groupName_ = name;
    }

    void UseMaterial(const std::string& name) {
        FlushPrimitive();
        auto it = materialIndexByName_.find(name);
        if (it != materialIndexByName_.end()) {
            currentMaterial_ = it->second;
            return;
        }
        // A usemtl naming a material no library defined: keep the name as a
        // placeholder material so the assignment is not lost.
        options_.Warn("OBJ: material '" + name + "' is not defined in any loaded .mtl; "
                      "a placeholder is used");
        ModelMaterial placeholder;
        placeholder.Name = name;
        placeholder.DeriveMissingModel();
        currentMaterial_ = static_cast<int>(document_->Materials.size());
        materialIndexByName_[name] = currentMaterial_;
        document_->Materials.push_back(std::move(placeholder));
    }

    void ReadSmoothing(const std::string& value) {
        // Smoothing groups decide how normals are generated for files that
        // carry none. The document stores normals, not groups, so this is only
        // consulted when generating them — and the loss is documented.
        smoothing_ = !(value == "off" || value == "0");
    }

    void FlushPrimitive() {
        if (faceStarts_.empty()) { ResetPrimitive(); return; }

        MeshPrimitive prim;
        prim.Name = groupName_.empty() ? meshName_ : groupName_;
        prim.Material = currentMaterial_;
        prim.Positions = std::move(primitivePositions_);
        prim.Normals = std::move(primitiveNormals_);
        prim.Indices = std::move(indices_);

        // Keep the n-gons the file actually has. Every face being a triangle is
        // the only case that can use the simpler mode.
        if (maxFaceCorners_ <= 3) {
            prim.Mode = PrimitiveMode::Triangles;
        } else {
            prim.Mode = PrimitiveMode::Polygons;
            prim.FaceStarts = std::move(faceStarts_);
            prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
        }

        if (!primitiveTexCoords_.empty()) {
            VertexAttribute uv;
            uv.Semantic = AttributeSemantic::TexCoord;
            uv.Name = "TEXCOORD_0";
            uv.Components = 2;
            uv.Values = std::move(primitiveTexCoords_);
            prim.Attributes.push_back(std::move(uv));
        }
        if (!primitiveColors_.empty()) {
            VertexAttribute color;
            color.Semantic = AttributeSemantic::Color;
            color.Name = "COLOR_0";
            color.Components = 3;
            color.Values = std::move(primitiveColors_);
            prim.Attributes.push_back(std::move(color));
        }

        if (!smoothing_ && prim.Normals.empty()) flatShadedPrimitives_ = true;
        pendingPrimitives_.push_back(std::move(prim));
        ResetPrimitive();
    }

    void ResetPrimitive() {
        primitivePositions_.clear();
        primitiveNormals_.clear();
        primitiveTexCoords_.clear();
        primitiveColors_.clear();
        indices_.clear();
        faceStarts_.clear();
        vertexCache_.clear();
        maxFaceCorners_ = 0;
    }

    void FlushMesh() {
        if (pendingPrimitives_.empty()) { groupName_.clear(); return; }

        ModelMesh mesh;
        mesh.Name = meshName_.empty() ? "mesh" : meshName_;
        mesh.Primitives = std::move(pendingPrimitives_);
        pendingPrimitives_.clear();

        ModelNode node;
        node.Name = mesh.Name;
        node.Mesh = document_->AddMesh(std::move(mesh));
        document_->AddNode(std::move(node));

        groupName_.clear();
    }

    // --- materials ---

    void ReadMaterialLibrary(const std::string& reference) {
        // The rest of the line is the file name; OBJ allows several,
        // whitespace-separated, but a name may itself contain spaces. Try the
        // whole remainder first, which is right far more often.
        const std::string path = baseDirectory_ + reference;
        std::ifstream file(path);
        if (!file) {
            for (const std::string& candidate : SplitWhitespace(reference)) {
                std::ifstream each(baseDirectory_ + candidate);
                if (each) { ParseMaterialLibrary(each); return; }
            }
            options_.Warn("OBJ: material library '" + reference +
                          "' not found beside the model; materials fall back to placeholders");
            return;
        }
        ParseMaterialLibrary(file);
    }

    void ParseMaterialLibrary(std::istream& stream) {
        ModelMaterial material;
        PhongParams phong;
        bool open = false;
        float transparency = 0.0f;

        auto commit = [&]() {
            if (!open) return;
            material.Phong = phong;
            material.BaseColorFactor = Vec4f(phong.Diffuse.x, phong.Diffuse.y, phong.Diffuse.z,
                                             1.0f - transparency);
            if (transparency > 0.0f) material.Alpha = AlphaMode::Blend;
            material.DeriveMissingModel();
            materialIndexByName_[material.Name] = static_cast<int>(document_->Materials.size());
            document_->Materials.push_back(std::move(material));
            material = ModelMaterial();
            phong = PhongParams();
            transparency = 0.0f;
            open = false;
        };

        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            const size_t space = trimmed.find_first_of(" \t");
            const std::string keyword = trimmed.substr(0, space);
            const std::string rest = space == std::string::npos ? std::string()
                                                                : Trim(trimmed.substr(space + 1));
            const std::vector<std::string> parts = SplitWhitespace(rest);
            auto number = [&parts](size_t i) {
                return i < parts.size() ? std::strtof(parts[i].c_str(), nullptr) : 0.0f;
            };
            auto color = [&]() { return Vec3f(number(0), number(1), number(2)); };

            if (keyword == "newmtl") {
                commit();
                material.Name = rest;
                open = true;
            } else if (!open) {
                continue;
            } else if (keyword == "Ka") phong.Ambient = color();
            else if (keyword == "Kd") phong.Diffuse = color();
            else if (keyword == "Ks") phong.Specular = color();
            else if (keyword == "Ke") material.EmissiveFactor = color();
            else if (keyword == "Ns") phong.Shininess = number(0);
            else if (keyword == "Ni") material.IndexOfRefraction = number(0);
            else if (keyword == "d")  transparency = 1.0f - number(0);
            else if (keyword == "Tr") transparency = number(0);
            else if (keyword == "illum") phong.IlluminationModel = static_cast<int>(number(0));
            // The widely used PBR extension to MTL.
            else if (keyword == "Pr") { material.RoughnessFactor = number(0); pbrStated_ = true; }
            else if (keyword == "Pm") { material.MetallicFactor = number(0); pbrStated_ = true; }
            else if (keyword == "map_Kd") phong.DiffuseTexture = MapRef(rest);
            else if (keyword == "map_Ka") phong.AmbientTexture = MapRef(rest);
            else if (keyword == "map_Ks") phong.SpecularTexture = MapRef(rest);
            else if (keyword == "map_Ke") material.EmissiveTexture = MapRef(rest);
            else if (keyword == "map_Bump" || keyword == "map_bump" ||
                     keyword == "bump" || keyword == "norm")
                material.NormalTexture = MapRef(rest);
            else if (keyword == "map_Pm") material.MetallicRoughnessTexture = MapRef(rest);
        }
        commit();
    }

    TextureRef MapRef(const std::string& arguments) {
        TextureRef ref;
        const std::string name = ParseMapLine(arguments, ref);
        if (name.empty()) return ref;

        auto it = imageIndexByName_.find(name);
        if (it != imageIndexByName_.end()) { ref.Image = it->second; return ref; }

        ModelImage image;
        image.Name = name;
        image.Uri = name;
        const size_t dot = name.rfind('.');
        if (dot != std::string::npos) {
            std::string ext = name.substr(dot + 1);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == "jpg" || ext == "jpeg") image.MimeType = "image/jpeg";
            else if (ext == "png") image.MimeType = "image/png";
            else if (ext == "tga") image.MimeType = "image/x-tga";
            else if (ext == "bmp") image.MimeType = "image/bmp";
        }
        ref.Image = static_cast<int>(document_->Images.size());
        imageIndexByName_[name] = ref.Image;
        document_->Images.push_back(std::move(image));
        return ref;
    }

    // --- finishing ---

    void Finish() {
        if (options_.GenerateMissingNormals) {
            for (auto& mesh : document_->Meshes)
                for (auto& prim : mesh.Primitives)
                    if (prim.Normals.empty()) prim.RecomputeNormals();
        }
        if (flatShadedPrimitives_)
            options_.Warn("OBJ: smoothing groups are resolved into vertex normals and not "
                          "carried, so 's' statements do not survive a round trip");
        if (pbrStated_)
            options_.Warn("OBJ: the file uses the MTL PBR extension (Pr/Pm); those values are "
                          "taken as authoritative over the derived ones");

        if (options_.WeldTolerance > 0.0) document_->WeldVertices(options_.WeldTolerance);
        if (options_.TriangulateOnImport) document_->TriangulateAll();
        if (options_.ForceUpAxis.has_value()) document_->ConvertUpAxis(*options_.ForceUpAxis);
    }

    const ConversionOptions& options_;
    std::string baseDirectory_;
    std::shared_ptr<ModelDocument> document_;

    // File-scope index streams.
    std::vector<Vec3d> positions_;
    std::vector<float> texcoords_;    // 2 per entry
    std::vector<Vec3f> normals_;
    std::vector<Vec3f> colors_;       // parallel to positions_, when present

    // The primitive being accumulated.
    std::vector<Vec3d> primitivePositions_;
    std::vector<Vec3f> primitiveNormals_;
    std::vector<float> primitiveTexCoords_;
    std::vector<float> primitiveColors_;
    std::vector<uint32_t> indices_;
    std::vector<uint32_t> faceStarts_;
    std::map<Corner, uint32_t> vertexCache_;
    size_t maxFaceCorners_ = 0;

    std::vector<MeshPrimitive> pendingPrimitives_;
    std::string meshName_;
    std::string groupName_;
    int currentMaterial_ = -1;
    bool smoothing_ = true;

    std::map<std::string, int> materialIndexByName_;
    std::map<std::string, int> imageIndexByName_;

    bool warnedLinesPoints_ = false;
    bool warnedBadIndex_ = false;
    bool flatShadedPrimitives_ = false;
    bool pbrStated_ = false;
};

// ===== WRITER =====

void WriteGeometry(const ModelDocument& document, std::ostream& out,
                   const std::string& materialLibrary, const ConversionOptions& options) {
    out << "# Wavefront OBJ written by " << options.Generator << "\n";
    if (!document.Title.empty()) out << "# " << document.Title << "\n";
    if (!materialLibrary.empty()) out << "mtllib " << materialLibrary << "\n";

    // OBJ has no transforms, so node transforms bake into the coordinates.
    // The document's own FlattenTransforms does this; here the walk applies
    // them directly to keep the caller's document untouched.
    size_t positionBase = 1, texcoordBase = 1, normalBase = 1;   // OBJ is 1-based

    auto writeNode = [&](const ModelNode& node, size_t nodeIndex) {
        if (node.Mesh < 0 || node.Mesh >= static_cast<int>(document.Meshes.size())) return;
        const ModelMesh& mesh = document.Meshes[node.Mesh];
        const Matrix4x4 world = document.GlobalTransform(static_cast<int>(nodeIndex));
        double normalMatrix[9];
        const bool normalsUsable = world.InverseTransposeUpper3x3(normalMatrix);

        out << "o " << (mesh.Name.empty() ? "mesh" : mesh.Name) << "\n";

        for (const auto& prim : mesh.Primitives) {
            for (const auto& p : prim.Positions) {
                const Vec3d w = world.TransformPoint(p);
                out << "v " << w.x << " " << w.y << " " << w.z << "\n";
            }
            const VertexAttribute* uv = prim.FindAttribute(AttributeSemantic::TexCoord, 0);
            if (uv) {
                for (size_t i = 0; i + 1 < uv->Values.size(); i += 2)
                    out << "vt " << uv->Values[i] << " " << uv->Values[i + 1] << "\n";
            }
            for (const auto& n : prim.Normals) {
                if (!normalsUsable) { out << "vn " << n.x << " " << n.y << " " << n.z << "\n"; continue; }
                const Vec3d t(normalMatrix[0] * n.x + normalMatrix[3] * n.y + normalMatrix[6] * n.z,
                              normalMatrix[1] * n.x + normalMatrix[4] * n.y + normalMatrix[7] * n.z,
                              normalMatrix[2] * n.x + normalMatrix[5] * n.y + normalMatrix[8] * n.z);
                const Vec3d u = t.Normalized();
                out << "vn " << u.x << " " << u.y << " " << u.z << "\n";
            }

            if (prim.Material >= 0 && prim.Material < static_cast<int>(document.Materials.size()))
                out << "usemtl " << document.Materials[prim.Material].Name << "\n";

            const bool hasUV = uv != nullptr;
            const bool hasNormals = !prim.Normals.empty();
            const size_t faces = prim.FaceCount();
            for (size_t f = 0; f < faces; ++f) {
                const std::vector<uint32_t> face = prim.Face(f);
                if (face.size() < 3) continue;   // points and lines have no OBJ face form
                out << "f";
                for (uint32_t corner : face) {
                    const size_t v = positionBase + corner;
                    out << " " << v;
                    if (hasUV || hasNormals) {
                        out << "/";
                        if (hasUV) out << (texcoordBase + corner);
                        if (hasNormals) out << "/" << (normalBase + corner);
                    }
                }
                out << "\n";
            }

            positionBase += prim.Positions.size();
            if (uv) texcoordBase += uv->Count();
            normalBase += prim.Normals.size();
        }
    };

    if (document.Nodes.empty()) {
        // No scene graph: write the meshes as they are.
        ModelNode synthetic;
        for (size_t i = 0; i < document.Meshes.size(); ++i) {
            synthetic.Mesh = static_cast<int>(i);
            writeNode(synthetic, static_cast<size_t>(-1));
        }
    } else {
        for (size_t i = 0; i < document.Nodes.size(); ++i) writeNode(document.Nodes[i], i);
    }
}

bool DocumentHasPointsOrLines(const ModelDocument& document) {
    for (const auto& mesh : document.Meshes)
        for (const auto& prim : mesh.Primitives)
            if (prim.Mode == PrimitiveMode::Points || prim.Mode == PrimitiveMode::Lines ||
                prim.Mode == PrimitiveMode::LineStrip || prim.Mode == PrimitiveMode::LineLoop)
                return true;
    return false;
}

void ReportExportLosses(const ModelDocument& document, const ConversionOptions& options) {
    if (!document.Animations.empty())
        options.Warn("OBJ: " + std::to_string(document.Animations.size()) +
                     " animation(s) dropped — OBJ stores no animation");
    if (!document.Skins.empty())
        options.Warn("OBJ: skinning dropped — OBJ stores no joints or weights");
    if (!document.Cameras.empty() || !document.Lights.empty())
        options.Warn("OBJ: cameras and lights dropped — OBJ stores neither");
    if (document.Nodes.size() > document.Meshes.size())
        options.Warn("OBJ: the node hierarchy is flattened into baked coordinates — "
                     "OBJ has no transforms");
    if (DocumentHasPointsOrLines(document))
        options.Warn("OBJ: point and line primitives are dropped");
    for (const auto& mesh : document.Meshes)
        for (const auto& prim : mesh.Primitives)
            if (!prim.Targets.empty()) {
                options.Warn("OBJ: morph targets dropped — OBJ stores none");
                return;
            }
}

} // namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities OBJConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.Meshes = true;
    caps.NGons = true;               // faces keep their corner count, both ways
    caps.Materials = true;
    caps.Textures = true;
    caps.TextureCoordinates = true;
    caps.Normals = true;
    caps.VertexColors = true;        // the "v x y z r g b" extension, read only
    caps.CustomAttributes = false;
    caps.Metadata = false;
    // Deliberately false: OBJ has no scene graph, instancing, skinning, morph
    // targets, animation, cameras, lights, units or up axis, and its numbers
    // are written as text at default precision rather than as doubles.
    return caps;
}

std::shared_ptr<ModelStorage::ModelDocument> OBJConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    std::ifstream file(filename);
    if (!file) {
        options.Warn("OBJ: cannot read " + filename);
        return nullptr;
    }
    Reader reader(options, DirectoryOf(filename));
    auto document = reader.Run(file);
    if (document && document->Title.empty()) document->Title = FileStem(filename);
    return document;
}

std::shared_ptr<ModelStorage::ModelDocument> OBJConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    std::string text(data.begin(), data.end());
    std::istringstream stream(text);
    return ImportFromStream(stream, options);
}

std::shared_ptr<ModelStorage::ModelDocument> OBJConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    // No path, so a mtllib reference cannot be resolved; the reader reports it.
    Reader reader(options, std::string());
    return reader.Run(stream);
}

std::string OBJConverter::BuildMaterialLibrary(const ModelDocument& document) {
    std::ostringstream out;
    out << "# MTL library written by UltraCanvas\n";
    for (const auto& material : document.Materials) {
        ModelMaterial resolved = material;
        // Writers here are fixed-function; a PBR-only material needs the
        // Phong side derived before it can be written at all.
        if (!resolved.Phong.has_value()) resolved.DeriveMissingModel();
        const PhongParams& phong = *resolved.Phong;

        out << "newmtl " << (resolved.Name.empty() ? "material" : resolved.Name) << "\n";
        out << "Ka " << phong.Ambient.x << " " << phong.Ambient.y << " " << phong.Ambient.z << "\n";
        out << "Kd " << phong.Diffuse.x << " " << phong.Diffuse.y << " " << phong.Diffuse.z << "\n";
        out << "Ks " << phong.Specular.x << " " << phong.Specular.y << " " << phong.Specular.z << "\n";
        out << "Ns " << phong.Shininess << "\n";
        out << "d " << resolved.BaseColorFactor.w << "\n";
        out << "illum " << phong.IlluminationModel << "\n";
        if (resolved.IndexOfRefraction != 1.0f) out << "Ni " << resolved.IndexOfRefraction << "\n";
        // The PBR extension, so a round trip through OBJ keeps the values a
        // fixed-function derivation would only approximate.
        out << "Pr " << resolved.RoughnessFactor << "\n";
        out << "Pm " << resolved.MetallicFactor << "\n";

        auto writeMap = [&out, &document](const char* keyword, const TextureRef& ref) {
            if (!ref.IsSet() || ref.Image >= static_cast<int>(document.Images.size())) return;
            const std::string& uri = document.Images[static_cast<size_t>(ref.Image)].Uri;
            if (uri.empty()) return;
            out << keyword << " " << uri << "\n";
        };
        writeMap("map_Kd", phong.DiffuseTexture.IsSet() ? phong.DiffuseTexture
                                                        : resolved.BaseColorTexture);
        writeMap("map_Ka", phong.AmbientTexture);
        writeMap("map_Ks", phong.SpecularTexture);
        writeMap("map_Ke", resolved.EmissiveTexture);
        writeMap("map_Bump", resolved.NormalTexture);
        out << "\n";
    }
    return out.str();
}

bool OBJConverter::Export(const ModelDocument& document, const std::string& filename,
                          const ConversionOptions& options) {
    std::ofstream file(filename);
    if (!file) {
        options.Warn("OBJ: cannot write " + filename);
        return false;
    }
    ReportExportLosses(document, options);

    std::string libraryName;
    if (!document.Materials.empty()) {
        libraryName = FileStem(filename) + ".mtl";
        const std::string libraryPath = DirectoryOf(filename) + libraryName;
        std::ofstream library(libraryPath);
        if (library) {
            library << BuildMaterialLibrary(document);
        } else {
            options.Warn("OBJ: cannot write the material library " + libraryPath +
                         "; the model is written without materials");
            libraryName.clear();
        }
    }

    WriteGeometry(document, file, libraryName, options);
    return static_cast<bool>(file);
}

bool OBJConverter::ExportToStream(const ModelDocument& document, std::ostream& stream,
                                  const ConversionOptions& options) {
    ReportExportLosses(document, options);
    if (!document.Materials.empty())
        options.Warn("OBJ: writing to a stream has nowhere to put the .mtl library; "
                     "materials are named but not defined");
    WriteGeometry(document, stream, std::string(), options);
    return static_cast<bool>(stream);
}

bool OBJConverter::ExportToMemory(const ModelDocument& document, std::vector<uint8_t>& outData,
                                  const ConversionOptions& options) {
    std::ostringstream stream;
    if (!ExportToStream(document, stream, options)) return false;
    const std::string text = stream.str();
    outData.assign(text.begin(), text.end());
    return true;
}

bool OBJConverter::ValidateData(const std::vector<uint8_t>& data) const {
    // OBJ has no magic number, so the test is whether the head of the file
    // looks like OBJ statements: a keyword that starts a geometry or structure
    // line, with comments and blanks skipped.
    const size_t limit = std::min<size_t>(data.size(), 8192);
    std::string text(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(limit));
    std::istringstream stream(text);
    std::string line;
    int inspected = 0;
    while (std::getline(stream, line) && inspected < 200) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        ++inspected;
        const size_t space = trimmed.find_first_of(" \t");
        if (space == std::string::npos) return false;
        const std::string keyword = trimmed.substr(0, space);
        static const char* kKeywords[] = {"v", "vt", "vn", "vp", "f", "o", "g", "s",
                                          "usemtl", "mtllib", "l", "p", "curv", "surf"};
        bool known = false;
        for (const char* candidate : kKeywords)
            if (keyword == candidate) { known = true; break; }
        if (!known) return false;
        // A geometry statement is proof enough; structure keywords alone are not.
        if (keyword == "v" || keyword == "f") return true;
    }
    return false;
}

bool OBJConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> head(8192);
    file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(std::max<std::streamsize>(0, file.gcount())));
    return ValidateData(head);
}

} // namespace ModelConverter
} // namespace UltraCanvas
