// Plugins/Models/DXF/UltraCanvasDXFModelConverter.cpp
// The DXF 3D entity reader.
//
// DXF is a flat stream of (group code, value) pairs. The structure is entirely
// in the codes: 0 starts an entity or a section marker, 2 names things, 10/20/30
// are the first point's X/Y/Z, 11/21/31 the second, and so on. This reader
// scans the pairs once and acts only on the entities that carry 3D geometry.
//
// Every layer becomes its own mesh, because a layer is how a CAD file names its
// parts, and the layer's colour becomes that mesh's material.
//
// The tag scanner here is deliberately separate from the Vector plugin's DXF
// reader rather than shared with it: that one is a large 2D machine built
// around VectorDocument, and threading a second output model through it would
// put both at risk. Unifying the two on one tag layer is recorded as a
// follow-up in Docs/Research/UltraCanvas3DModelProposal.md.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/DXF/UltraCanvasDXFModelConverter.h"
#include "DataFormats/UltraCanvasCADPalette.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;

namespace {

// ===== TAGS =====

struct Tag {
    int Code = 0;
    std::string Value;

    double Number() const { return std::atof(Value.c_str()); }
    int Integer() const { return std::atoi(Value.c_str()); }
};

std::string TrimTag(const std::string& text) {
    size_t begin = 0, end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) --end;
    return text.substr(begin, end - begin);
}

// Reads the whole file into tags. A DXF is code line / value line, forever;
// a stray odd line at the end is dropped rather than misread as a pair.
std::vector<Tag> ReadTags(std::istream& stream) {
    std::vector<Tag> tags;
    std::string codeLine, valueLine;
    while (std::getline(stream, codeLine)) {
        if (!std::getline(stream, valueLine)) break;
        const std::string trimmedCode = TrimTag(codeLine);
        if (trimmedCode.empty()) continue;
        // A code line is an integer and nothing else. Anything else means the
        // stream is not tag-aligned, and guessing would produce garbage.
        char* parseEnd = nullptr;
        const long code = std::strtol(trimmedCode.c_str(), &parseEnd, 10);
        if (parseEnd == trimmedCode.c_str() || *parseEnd != '\0') continue;
        tags.push_back(Tag{static_cast<int>(code), TrimTag(valueLine)});
    }
    return tags;
}

// The entities this reader turns into geometry. Everything else in a DXF is a
// drawing element and belongs to the 2D reader.
bool IsGeometryEntity(const std::string& type) {
    return type == "3DFACE" || type == "POLYLINE" || type == "LINE" || type == "POINT";
}

// $INSUNITS, the drawing's unit. The values are AutoCAD's, not arbitrary.
ModelUnit UnitFromInsUnits(int value) {
    switch (value) {
        case 1:  return ModelUnit::Inch;
        case 2:  return ModelUnit::Foot;
        case 3:  return ModelUnit::Mile;
        case 4:  return ModelUnit::Millimeter;
        case 5:  return ModelUnit::Centimeter;
        case 6:  return ModelUnit::Meter;
        case 7:  return ModelUnit::Kilometer;
        case 9:  return ModelUnit::Mil;
        case 10: return ModelUnit::Yard;
        case 13: return ModelUnit::Micrometer;
        case 14: return ModelUnit::Decimeter;
        default: return ModelUnit::Unspecified;   // 0 = unitless, and the exotic rest
    }
}

// ===== READER =====

class Reader {
public:
    explicit Reader(const ConversionOptions& options) : options_(options) {}

    std::shared_ptr<ModelDocument> Run(std::istream& stream) {
        const std::vector<Tag> tags = ReadTags(stream);
        if (tags.empty()) {
            options_.Warn("DXF: no tag pairs found");
            return nullptr;
        }

        document_ = std::make_shared<ModelDocument>();
        document_->SourceFormat = "dxf";
        // DXF is Z-up and right-handed, always.
        document_->Up = UpAxis::ZUp;
        document_->Chirality = Handedness::RightHanded;
        document_->SourceUnit = options_.AssumeUnit;
        document_->UnitScaleToMeters = MetersPerUnit(options_.AssumeUnit);

        ScanSections(tags);
        BuildMeshes();

        if (document_->Meshes.empty()) {
            options_.Warn(entitiesSeen_ == 0
                    ? "DXF: no ENTITIES section"
                    : "DXF: the file has no 3D geometry — it holds " +
                      std::to_string(entitiesSeen_) +
                      " entities, all of them drawing elements. Read it with the Vector "
                      "plugin's DXF converter instead.");
            return nullptr;
        }
        ReportSkipped();
        Finish();
        return document_;
    }

private:
    struct LayerGeometry {
        std::vector<Vec3d> FacePositions;
        std::vector<uint32_t> FaceIndices;
        std::vector<uint32_t> FaceStarts;
        std::vector<Vec3d> LinePositions;
        std::vector<Vec3d> PointPositions;
        int Color = 7;
        bool ColorKnown = false;
    };

    // --- sections ---

    void ScanSections(const std::vector<Tag>& tags) {
        for (size_t i = 0; i < tags.size(); ++i) {
            if (tags[i].Code != 0 || tags[i].Value != "SECTION") continue;
            if (i + 1 >= tags.size() || tags[i + 1].Code != 2) continue;

            const std::string section = tags[i + 1].Value;
            const size_t begin = i + 2;
            size_t end = begin;
            while (end < tags.size() && !(tags[end].Code == 0 && tags[end].Value == "ENDSEC")) ++end;

            if (section == "HEADER")        ScanHeader(tags, begin, end);
            else if (section == "TABLES")   ScanTables(tags, begin, end);
            else if (section == "ENTITIES") ScanEntities(tags, begin, end);
            else if (section == "BLOCKS")   ScanBlocks(tags, begin, end);
            i = end;
        }
    }

    void ScanHeader(const std::vector<Tag>& tags, size_t begin, size_t end) {
        for (size_t i = begin; i + 1 < end; ++i) {
            if (tags[i].Code != 9) continue;
            if (tags[i].Value == "$INSUNITS" && tags[i + 1].Code == 70) {
                const ModelUnit unit = UnitFromInsUnits(tags[i + 1].Integer());
                if (unit != ModelUnit::Unspecified) {
                    document_->SourceUnit = unit;
                    document_->UnitScaleToMeters = MetersPerUnit(unit);
                }
            } else if (tags[i].Value == "$ACADVER" && tags[i + 1].Code == 1) {
                document_->Metadata["dxf.version"] = tags[i + 1].Value;
            }
        }
    }

    // The LAYER table, for the colour each layer's material takes.
    void ScanTables(const std::vector<Tag>& tags, size_t begin, size_t end) {
        bool inLayerTable = false;
        std::string name;
        int color = 7;
        bool open = false;

        auto commit = [&]() {
            if (open && !name.empty()) {
                LayerGeometry& layer = layers_[name];
                layer.Color = color;
                layer.ColorKnown = true;
            }
            open = false;
            name.clear();
            color = 7;
        };

        for (size_t i = begin; i < end; ++i) {
            if (tags[i].Code == 0) {
                commit();
                if (tags[i].Value == "TABLE" && i + 1 < end && tags[i + 1].Code == 2)
                    inLayerTable = tags[i + 1].Value == "LAYER";
                else if (tags[i].Value == "ENDTAB") inLayerTable = false;
                else if (tags[i].Value == "LAYER" && inLayerTable) open = true;
                continue;
            }
            if (!open) continue;
            if (tags[i].Code == 2) name = tags[i].Value;
            // A negative colour marks the layer off; the absolute value is
            // still the colour, and geometry on a hidden layer is still
            // geometry to a model importer.
            else if (tags[i].Code == 62) color = std::abs(tags[i].Integer());
        }
        commit();
    }

    void ScanBlocks(const std::vector<Tag>& tags, size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i)
            if (tags[i].Code == 0 && IsGeometryEntity(tags[i].Value)) {
                options_.Warn("DXF: the BLOCKS section contains 3D entities; block definitions "
                              "and INSERT expansion are not read, so any geometry placed through "
                              "a block is missing from this model");
                return;
            }
    }

    void ScanEntities(const std::vector<Tag>& tags, size_t begin, size_t end) {
        size_t i = begin;
        while (i < end) {
            if (tags[i].Code != 0) { ++i; continue; }
            const std::string type = tags[i].Value;

            size_t entityEnd = i + 1;
            while (entityEnd < end && tags[entityEnd].Code != 0) ++entityEnd;

            ++entitiesSeen_;
            if (type == "3DFACE") {
                Read3DFace(tags, i + 1, entityEnd);
                i = entityEnd;
            } else if (type == "POLYLINE") {
                i = ReadPolyline(tags, i, end);
            } else if (type == "LINE") {
                ReadLine(tags, i + 1, entityEnd);
                i = entityEnd;
            } else if (type == "POINT") {
                ReadPoint(tags, i + 1, entityEnd);
                i = entityEnd;
            } else {
                if (type != "SEQEND" && type != "VERTEX") ++skipped_[type];
                i = entityEnd;
            }
        }
    }

    // --- entity helpers ---

    static const Tag* Find(const std::vector<Tag>& tags, size_t begin, size_t end, int code) {
        for (size_t i = begin; i < end; ++i)
            if (tags[i].Code == code) return &tags[i];
        return nullptr;
    }

    static double Value(const std::vector<Tag>& tags, size_t begin, size_t end, int code,
                        double fallback = 0.0) {
        const Tag* tag = Find(tags, begin, end, code);
        return tag ? tag->Number() : fallback;
    }

    static Vec3d Point(const std::vector<Tag>& tags, size_t begin, size_t end, int base) {
        return Vec3d(Value(tags, begin, end, base),
                     Value(tags, begin, end, base + 10),
                     Value(tags, begin, end, base + 20));
    }

    std::string LayerName(const std::vector<Tag>& tags, size_t begin, size_t end) {
        const Tag* tag = Find(tags, begin, end, 8);
        return tag && !tag->Value.empty() ? tag->Value : std::string("0");
    }

    LayerGeometry& LayerFor(const std::vector<Tag>& tags, size_t begin, size_t end) {
        return layers_[LayerName(tags, begin, end)];
    }

    // --- entities ---

    // A 3- or 4-corner planar face. The convention for a triangle is that the
    // fourth corner repeats the third, so a reader that does not check emits
    // degenerate quads.
    void Read3DFace(const std::vector<Tag>& tags, size_t begin, size_t end) {
        LayerGeometry& layer = LayerFor(tags, begin, end);

        Vec3d corners[4] = {Point(tags, begin, end, 10), Point(tags, begin, end, 11),
                            Point(tags, begin, end, 12), Point(tags, begin, end, 13)};
        size_t count = 4;
        const Vec3d difference = corners[3] - corners[2];
        if (difference.Length() < 1e-12) count = 3;

        layer.FaceStarts.push_back(static_cast<uint32_t>(layer.FaceIndices.size()));
        for (size_t k = 0; k < count; ++k) {
            layer.FaceIndices.push_back(static_cast<uint32_t>(layer.FacePositions.size()));
            layer.FacePositions.push_back(corners[k]);
        }
        maxFaceCorners_ = std::max(maxFaceCorners_, count);
    }

    // POLYLINE owns the VERTEX records that follow it, up to SEQEND. Flag 70
    // says which of the several things a POLYLINE can be it actually is.
    size_t ReadPolyline(const std::vector<Tag>& tags, size_t start, size_t end) {
        size_t headerEnd = start + 1;
        while (headerEnd < end && tags[headerEnd].Code != 0) ++headerEnd;

        const int flags = static_cast<int>(Value(tags, start + 1, headerEnd, 70));
        const std::string layerName = LayerName(tags, start + 1, headerEnd);

        // Collect the VERTEX records.
        std::vector<std::pair<size_t, size_t>> vertices;   // [begin, end) of each
        size_t i = headerEnd;
        while (i < end) {
            if (tags[i].Code != 0) { ++i; continue; }
            if (tags[i].Value == "SEQEND") { ++i; break; }
            if (tags[i].Value != "VERTEX") break;
            size_t vertexEnd = i + 1;
            while (vertexEnd < end && tags[vertexEnd].Code != 0) ++vertexEnd;
            vertices.emplace_back(i + 1, vertexEnd);
            i = vertexEnd;
        }
        // Skip past SEQEND's own tags.
        while (i < end && tags[i].Code != 0) ++i;

        LayerGeometry& layer = layers_[layerName];

        if (flags & 64) ReadPolyfaceMesh(tags, vertices, layer);
        else if (flags & 16) ReadPolygonMesh(tags, start + 1, headerEnd, vertices, layer);
        else if (flags & 8) {
            // A 3D polyline: an open or closed line strip.
            std::vector<Vec3d> points;
            for (const auto& vertex : vertices)
                points.push_back(Point(tags, vertex.first, vertex.second, 10));
            for (size_t k = 0; k + 1 < points.size(); ++k) {
                layer.LinePositions.push_back(points[k]);
                layer.LinePositions.push_back(points[k + 1]);
            }
            if ((flags & 1) && points.size() > 2) {
                layer.LinePositions.push_back(points.back());
                layer.LinePositions.push_back(points.front());
            }
        } else {
            ++skipped_["POLYLINE(2D)"];
        }
        return i;
    }

    // Flag 64: the vertices are either positions or face records, told apart by
    // their own flags — and the test is narrower than it looks. A position
    // vertex carries 192 (128 | 64) and a face record carries 128 alone, so
    // testing bit 128 by itself matches both and a polyface mesh silently
    // arrives with no positions at all. Bit 64 must be clear as well.
    //
    // Face records index the positions 1-based, and a negative index means the
    // edge leading to that corner is invisible — a drawing property with no
    // meaning for a mesh, so the sign is dropped.
    void ReadPolyfaceMesh(const std::vector<Tag>& tags,
                          const std::vector<std::pair<size_t, size_t>>& vertices,
                          LayerGeometry& layer) {
        std::vector<Vec3d> positions;
        std::vector<std::vector<int>> faces;

        for (const auto& vertex : vertices) {
            const int vertexFlags = static_cast<int>(Value(tags, vertex.first, vertex.second, 70));
            const bool isFaceRecord = (vertexFlags & 128) != 0 && (vertexFlags & 64) == 0;
            if (isFaceRecord) {
                std::vector<int> face;
                for (int code = 71; code <= 74; ++code) {
                    const Tag* tag = Find(tags, vertex.first, vertex.second, code);
                    if (!tag) break;
                    const int index = std::abs(tag->Integer());
                    if (index > 0) face.push_back(index - 1);
                }
                if (face.size() >= 3) faces.push_back(std::move(face));
            } else {
                positions.push_back(Point(tags, vertex.first, vertex.second, 10));
            }
        }

        const uint32_t base = static_cast<uint32_t>(layer.FacePositions.size());
        layer.FacePositions.insert(layer.FacePositions.end(), positions.begin(), positions.end());
        for (const auto& face : faces) {
            bool valid = true;
            for (int index : face)
                if (index < 0 || static_cast<size_t>(index) >= positions.size()) valid = false;
            if (!valid) { ++skipped_["POLYLINE(polyface index out of range)"]; continue; }

            layer.FaceStarts.push_back(static_cast<uint32_t>(layer.FaceIndices.size()));
            for (int index : face)
                layer.FaceIndices.push_back(base + static_cast<uint32_t>(index));
            maxFaceCorners_ = std::max(maxFaceCorners_, face.size());
        }
    }

    // Flag 16: an M x N grid of vertices in row-major order, with a quad
    // between each set of four neighbours. Flags 1 and 32 close it in M and N.
    void ReadPolygonMesh(const std::vector<Tag>& tags, size_t headerBegin, size_t headerEnd,
                         const std::vector<std::pair<size_t, size_t>>& vertices,
                         LayerGeometry& layer) {
        const int m = static_cast<int>(Value(tags, headerBegin, headerEnd, 71));
        const int n = static_cast<int>(Value(tags, headerBegin, headerEnd, 72));
        if (m < 2 || n < 2 || static_cast<size_t>(m) * static_cast<size_t>(n) != vertices.size()) {
            ++skipped_["POLYLINE(polygon mesh size mismatch)"];
            return;
        }
        const int flags = static_cast<int>(Value(tags, headerBegin, headerEnd, 70));
        const bool closedM = (flags & 1) != 0;
        const bool closedN = (flags & 32) != 0;

        const uint32_t base = static_cast<uint32_t>(layer.FacePositions.size());
        for (const auto& vertex : vertices)
            layer.FacePositions.push_back(Point(tags, vertex.first, vertex.second, 10));

        const int rows = closedM ? m : m - 1;
        const int columns = closedN ? n : n - 1;
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int r1 = (row + 1) % m, c1 = (column + 1) % n;
                const uint32_t quad[4] = {
                        base + static_cast<uint32_t>(row * n + column),
                        base + static_cast<uint32_t>(row * n + c1),
                        base + static_cast<uint32_t>(r1 * n + c1),
                        base + static_cast<uint32_t>(r1 * n + column)};
                layer.FaceStarts.push_back(static_cast<uint32_t>(layer.FaceIndices.size()));
                for (uint32_t index : quad) layer.FaceIndices.push_back(index);
            }
        }
        maxFaceCorners_ = std::max<size_t>(maxFaceCorners_, 4);
    }

    void ReadLine(const std::vector<Tag>& tags, size_t begin, size_t end) {
        LayerGeometry& layer = LayerFor(tags, begin, end);
        layer.LinePositions.push_back(Point(tags, begin, end, 10));
        layer.LinePositions.push_back(Point(tags, begin, end, 11));
    }

    void ReadPoint(const std::vector<Tag>& tags, size_t begin, size_t end) {
        LayerFor(tags, begin, end).PointPositions.push_back(Point(tags, begin, end, 10));
    }

    // --- assembly ---

    void BuildMeshes() {
        for (auto& entry : layers_) {
            const std::string& name = entry.first;
            LayerGeometry& layer = entry.second;
            if (layer.FacePositions.empty() && layer.LinePositions.empty() &&
                layer.PointPositions.empty())
                continue;

            const int material = MaterialForLayer(name, layer);

            ModelMesh mesh;
            mesh.Name = name;

            if (!layer.FaceIndices.empty()) {
                MeshPrimitive prim;
                prim.Name = name;
                prim.Material = material;
                prim.Positions = std::move(layer.FacePositions);
                prim.Indices = std::move(layer.FaceIndices);
                // A DXF mixes triangles and quads freely — 3DFACE is either,
                // and a polyface mesh can hold both — which is exactly what
                // Polygons mode is for. Only an all-triangle layer can use the
                // simpler mode.
                if (maxFaceCorners_ <= 3) {
                    prim.Mode = PrimitiveMode::Triangles;
                } else {
                    prim.Mode = PrimitiveMode::Polygons;
                    prim.FaceStarts = std::move(layer.FaceStarts);
                    prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
                }
                mesh.Primitives.push_back(std::move(prim));
            }
            if (!layer.LinePositions.empty()) {
                MeshPrimitive prim;
                prim.Name = name + " (lines)";
                prim.Material = material;
                prim.Mode = PrimitiveMode::Lines;
                prim.Positions = std::move(layer.LinePositions);
                mesh.Primitives.push_back(std::move(prim));
            }
            if (!layer.PointPositions.empty()) {
                MeshPrimitive prim;
                prim.Name = name + " (points)";
                prim.Material = material;
                prim.Mode = PrimitiveMode::Points;
                prim.Positions = std::move(layer.PointPositions);
                mesh.Primitives.push_back(std::move(prim));
            }
            if (mesh.Primitives.empty()) continue;

            ModelNode node;
            node.Name = name;
            node.Mesh = document_->AddMesh(std::move(mesh));
            document_->AddNode(std::move(node));
        }
    }

    int MaterialForLayer(const std::string& name, const LayerGeometry& layer) {
        ModelMaterial material;
        material.Name = name;
        const Color color = AciPaletteColor(layer.ColorKnown ? layer.Color : 7);
        // ACI 7 is "white on screen, black on paper" — as a material colour
        // neither is useful, so a layer that never stated a colour gets a
        // neutral grey a shaded viewer can actually show.
        const bool stated = layer.ColorKnown && layer.Color != 7;
        PhongParams phong;
        phong.Diffuse = stated ? Vec3f(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f)
                               : Vec3f(0.78f, 0.80f, 0.85f);
        material.Phong = phong;
        material.DeriveMissingModel();
        material.Extras["dxf.aci"] = std::to_string(layer.ColorKnown ? layer.Color : 7);
        return document_->AddMaterial(std::move(material));
    }

    void ReportSkipped() {
        if (skipped_.empty()) return;
        std::string message = "DXF: entity types with no 3D form were skipped:";
        for (const auto& entry : skipped_)
            message += " " + entry.first + " (x" + std::to_string(entry.second) + ")";
        options_.Warn(message);
    }

    void Finish() {
        // DXF carries no vertex normals at all — a face is a face, and the
        // renderer shades it. Area-weighted normals from the geometry are the
        // closest the document can get.
        if (options_.GenerateMissingNormals) {
            for (auto& mesh : document_->Meshes)
                for (auto& prim : mesh.Primitives)
                    if (prim.Normals.empty() &&
                        (prim.Mode == PrimitiveMode::Triangles || prim.Mode == PrimitiveMode::Polygons))
                        prim.RecomputeNormals();
        }
        if (options_.WeldTolerance > 0.0) document_->WeldVertices(options_.WeldTolerance);
        if (options_.TriangulateOnImport) document_->TriangulateAll();
        if (options_.ForceUpAxis.has_value()) document_->ConvertUpAxis(*options_.ForceUpAxis);
    }

    const ConversionOptions& options_;
    std::shared_ptr<ModelDocument> document_;
    std::map<std::string, LayerGeometry> layers_;
    std::map<std::string, int> skipped_;
    size_t entitiesSeen_ = 0;
    size_t maxFaceCorners_ = 0;
};

// A DXF that is tag-shaped: the first pair should be code 0 with SECTION.
bool LooksLikeDxf(const std::string& head) {
    std::istringstream stream(head);
    std::string codeLine, valueLine;
    int inspected = 0;
    while (std::getline(stream, codeLine) && inspected < 40) {
        if (!std::getline(stream, valueLine)) break;
        const std::string code = TrimTag(codeLine);
        if (code.empty()) continue;
        ++inspected;
        char* parseEnd = nullptr;
        std::strtol(code.c_str(), &parseEnd, 10);
        if (parseEnd == code.c_str() || *parseEnd != '\0') return false;
        if (code == "0" && TrimTag(valueLine) == "SECTION") return true;
    }
    return false;
}

const char kBinarySentinel[] = "AutoCAD Binary DXF";

} // namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities DXFModelConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.Meshes = true;
    caps.NGons = true;              // 3DFACE quads and polyface faces keep their corners
    caps.Lines = true;              // LINE and 3D POLYLINE
    caps.PointClouds = true;        // POINT
    caps.Materials = true;          // one per layer, from its ACI colour
    caps.Normals = true;            // derived; DXF stores none
    caps.Units = true;              // $INSUNITS
    caps.UpAxis = true;             // fixed Z-up by the format
    caps.Metadata = true;
    caps.DoublePrecision = true;    // DXF coordinates are decimal text, read into double
    // Deliberately false: no scene graph or instancing (BLOCKS/INSERT are not
    // expanded), no PBR materials, no texture coordinates or textures, no
    // skinning, morph targets, animation, cameras or lights.
    return caps;
}

std::shared_ptr<ModelStorage::ModelDocument> DXFModelConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        options.Warn("DXF: cannot read " + filename);
        return nullptr;
    }
    auto document = ImportFromStream(file, options);
    if (document && document->Title.empty()) {
        const size_t slash = filename.find_last_of("/\\");
        std::string stem = slash == std::string::npos ? filename : filename.substr(slash + 1);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos && dot > 0) stem.erase(dot);
        document->Title = stem;
    }
    return document;
}

std::shared_ptr<ModelStorage::ModelDocument> DXFModelConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    if (data.size() >= sizeof(kBinarySentinel) - 1 &&
        std::memcmp(data.data(), kBinarySentinel, sizeof(kBinarySentinel) - 1) == 0) {
        options.Warn("DXF: this is a binary DXF; only the tagged ASCII form is read");
        return nullptr;
    }
    std::string text(data.begin(), data.end());
    std::istringstream stream(text);
    return ImportFromStream(stream, options);
}

std::shared_ptr<ModelStorage::ModelDocument> DXFModelConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    Reader reader(options);
    return reader.Run(stream);
}

bool DXFModelConverter::ValidateData(const std::vector<uint8_t>& data) const {
    if (data.size() >= sizeof(kBinarySentinel) - 1 &&
        std::memcmp(data.data(), kBinarySentinel, sizeof(kBinarySentinel) - 1) == 0)
        return false;   // a real DXF, but not one this reader handles
    const size_t limit = std::min<size_t>(data.size(), 4096);
    return LooksLikeDxf(std::string(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(limit)));
}

bool DXFModelConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> head(4096);
    file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(std::max<std::streamsize>(0, file.gcount())));
    return ValidateData(head);
}

bool DXFModelConverter::HasThreeDimensionalGeometry(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) return false;
    // Scan for an entity name this reader acts on, without building anything.
    // POLYLINE is only 3D when its flags say so, so it is checked properly by
    // the reader; here it counts as a maybe, which is the useful answer.
    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = TrimTag(line);
        if (IsGeometryEntity(trimmed) && trimmed != "LINE" && trimmed != "POINT") return true;
    }
    return false;
}

} // namespace ModelConverter
} // namespace UltraCanvas
