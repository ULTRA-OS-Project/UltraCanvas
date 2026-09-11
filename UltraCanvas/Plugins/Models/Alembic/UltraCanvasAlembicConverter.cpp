// Plugins/Models/Alembic/UltraCanvasAlembicConverter.cpp
// AbcGeom meaning on top of the Ogawa structure layer. Declared in
// UltraCanvasAlembicConverter.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/Alembic/UltraCanvasAlembicConverter.h"
#include "Models/Alembic/UltraCanvasOgawaFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;
namespace Og = UltraCanvas::Ogawa;

namespace {

// A corner of a face, as Alembic addresses it: one index into the positions
// and, because normals and UVs are face-varying, its own place in those. Two
// corners merge into one document vertex only when all three agree.
struct Corner {
    uint32_t Position = 0;
    uint32_t Normal = 0;
    uint32_t UV = 0;
    bool operator<(const Corner& other) const {
        if (Position != other.Position) return Position < other.Position;
        if (Normal != other.Normal) return Normal < other.Normal;
        return UV < other.UV;
    }
};

class Reader {
public:
    Reader(const Og::Archive& archive, ModelDocument& document, const ConversionOptions& options)
            : archive_(archive), document_(document), options_(options) {}

    void Run();

private:
    const Og::Archive& archive_;
    ModelDocument& document_;
    const ConversionOptions& options_;
    std::set<std::string> saidOnce_;
    size_t meshes_ = 0;

    void WarnOnce(const std::string& message) {
        if (saidOnce_.insert(message).second) options_.Warn(message);
    }

    void Walk(const Og::Object& object, int parentNode);
    int ReadXform(const Og::Object& object, int parentNode);
    void ReadMesh(const Og::Object& object, int node, bool subdivision);
    // A FaceSet is a child object of the mesh naming a subset of its faces.
    // Collected before the mesh is built, because they decide how it splits.
    std::map<std::string, std::vector<int32_t>> ReadFaceSets(const Og::Object& object);
};

// ===== TRANSFORMS =====

int Reader::ReadXform(const Og::Object& object, int parentNode) {
    ModelNode node;
    node.Name = object.Name();

    const Og::Compound properties = object.Properties();
    const int xformIndex = properties.Valid() ? properties.Find(".xform") : -1;
    if (xformIndex >= 0) {
        const Og::Compound xform = properties.Child(static_cast<size_t>(xformIndex));
        const int valuesIndex = xform.Valid() ? xform.Find(".vals") : -1;
        if (valuesIndex >= 0) {
            const std::vector<double> values = xform.Doubles(static_cast<size_t>(valuesIndex));
            if (values.size() >= 16) {
                // Alembic stores the matrix row-major with translation in the
                // last row; ModelStorage is column-major with translation in
                // the last column, so this is a transpose, not a copy.
                Matrix4x4 matrix;
                for (int row = 0; row < 4; ++row)
                    for (int column = 0; column < 4; ++column)
                        matrix.m[column * 4 + row] = values[static_cast<size_t>(row) * 4 +
                                                            static_cast<size_t>(column)];
                if (!matrix.IsIdentity()) {
                    Vec3d translation, scale;
                    Quatd rotation;
                    // Prefer TRS where it is exact: it is what a writer for a
                    // scene format wants, and it survives interpolation.
                    if (matrix.DecomposeTRS(translation, rotation, scale)) {
                        node.Translation = translation;
                        node.Rotation = rotation;
                        node.Scale = scale;
                    } else {
                        node.Matrix = matrix;
                    }
                }
            } else if (!values.empty()) {
                WarnOnce("Alembic: a transform's matrix is not 16 doubles and was ignored");
            }
        }

        // ".inherits" false means the transform replaces its parent's rather
        // than composing with it — rare, and silently wrong if ignored.
        const int inheritsIndex = properties.Find(".inherits");
        if (inheritsIndex >= 0) {
            const std::vector<uint8_t> raw = properties.Sample(static_cast<size_t>(inheritsIndex));
            if (!raw.empty() && raw[0] == 0)
                WarnOnce("Alembic: a transform does not inherit its parent's, which this reader "
                         "does not model; that branch is placed as if it did");
        }
    }

    const int visibleIndex = properties.Valid() ? properties.Find("visible") : -1;
    if (visibleIndex >= 0) {
        const std::vector<uint8_t> raw = properties.Sample(static_cast<size_t>(visibleIndex));
        // Alembic's visibility is a tri-state: -1 deferred, 0 hidden, 1 shown.
        if (!raw.empty() && static_cast<int8_t>(raw[0]) == 0)
            node.Extras["visible"] = "false";
    }

    return document_.AddNode(std::move(node), parentNode);
}

// ===== FACE SETS =====

std::map<std::string, std::vector<int32_t>> Reader::ReadFaceSets(const Og::Object& object) {
    std::map<std::string, std::vector<int32_t>> sets;
    for (size_t i = 0; i < object.ChildCount(); ++i) {
        const Og::Object child = object.Child(i);
        if (!child.Valid() || child.Schema() != "AbcGeom_FaceSet_v1") continue;
        const Og::Compound properties = child.Properties();
        const int faceSetIndex = properties.Valid() ? properties.Find(".faceset") : -1;
        if (faceSetIndex < 0) continue;
        const Og::Compound faceSet = properties.Child(static_cast<size_t>(faceSetIndex));
        const int facesIndex = faceSet.Valid() ? faceSet.Find(".faces") : -1;
        if (facesIndex < 0) continue;
        std::vector<int32_t> faces = faceSet.Int32s(static_cast<size_t>(facesIndex));
        if (!faces.empty()) sets.emplace(child.Name(), std::move(faces));
    }
    return sets;
}

// ===== MESHES =====

void Reader::ReadMesh(const Og::Object& object, int node, bool subdivision) {
    const Og::Compound properties = object.Properties();
    const int geomIndex = properties.Valid() ? properties.Find(".geom") : -1;
    if (geomIndex < 0) {
        WarnOnce("Alembic: a mesh object has no .geom property and was skipped");
        return;
    }
    const Og::Compound geom = properties.Child(static_cast<size_t>(geomIndex));
    if (!geom.Valid()) return;

    const int positionIndex = geom.Find("P");
    const int faceIndexIndex = geom.Find(".faceIndices");
    const int faceCountIndex = geom.Find(".faceCounts");
    if (positionIndex < 0 || faceIndexIndex < 0 || faceCountIndex < 0) {
        WarnOnce("Alembic: a mesh is missing P, .faceIndices or .faceCounts and was skipped");
        return;
    }

    const std::vector<float> rawPositions = geom.Floats(static_cast<size_t>(positionIndex));
    const std::vector<int32_t> faceIndices = geom.Int32s(static_cast<size_t>(faceIndexIndex));
    const std::vector<int32_t> faceCounts = geom.Int32s(static_cast<size_t>(faceCountIndex));
    if (rawPositions.size() < 3 || faceIndices.empty() || faceCounts.empty()) {
        WarnOnce("Alembic: a mesh's geometry arrays are empty or of an unexpected type");
        return;
    }

    std::vector<Vec3d> positions(rawPositions.size() / 3);
    for (size_t i = 0; i < positions.size(); ++i)
        positions[i] = Vec3d(rawPositions[i * 3], rawPositions[i * 3 + 1], rawPositions[i * 3 + 2]);

    // Normals and UVs are face-varying here: one value per face corner. Some
    // writers store them per vertex instead, which geoScope says.
    std::vector<float> normals;
    bool normalsPerVertex = false;
    const int normalIndex = geom.Find("N");
    if (normalIndex >= 0) {
        const Og::PropertyHeader& header = geom.At(static_cast<size_t>(normalIndex));
        if (header.Kind == Og::PropertyKind::Compound) {
            const Og::Compound compound = geom.Child(static_cast<size_t>(normalIndex));
            const int values = compound.Valid() ? compound.Find(".vals") : -1;
            if (values >= 0) normals = compound.Floats(static_cast<size_t>(values));
            normalsPerVertex = Og::MetadataValue(header.Metadata, "geoScope") == "vtx";
        } else {
            normals = geom.Floats(static_cast<size_t>(normalIndex));
            normalsPerVertex = Og::MetadataValue(header.Metadata, "geoScope") == "vtx";
        }
    }

    // uv is an *indexed* geometry parameter: a pool of values plus one index
    // per corner, which is how a mesh with seams avoids storing them twice.
    std::vector<float> uvValues;
    std::vector<uint32_t> uvIndices;
    bool uvPerVertex = false;
    const int uvIndex = geom.Find("uv");
    if (uvIndex >= 0) {
        const Og::PropertyHeader& header = geom.At(static_cast<size_t>(uvIndex));
        uvPerVertex = Og::MetadataValue(header.Metadata, "geoScope") == "vtx";
        if (header.Kind == Og::PropertyKind::Compound) {
            const Og::Compound compound = geom.Child(static_cast<size_t>(uvIndex));
            if (compound.Valid()) {
                const int values = compound.Find(".vals");
                const int indices = compound.Find(".indices");
                if (values >= 0) uvValues = compound.Floats(static_cast<size_t>(values));
                if (indices >= 0) uvIndices = compound.Uint32s(static_cast<size_t>(indices));
            }
        } else {
            uvValues = geom.Floats(static_cast<size_t>(uvIndex));
        }
    }

    const std::map<std::string, std::vector<int32_t>> faceSets = ReadFaceSets(object);
    // Which set each face belongs to, so the mesh can be split without
    // walking the sets once per face.
    std::vector<int> setForFace(faceCounts.size(), -1);
    std::vector<std::string> setNames;
    for (const auto& entry : faceSets) {
        const int which = static_cast<int>(setNames.size());
        setNames.push_back(entry.first);
        for (int32_t face : entry.second)
            if (face >= 0 && static_cast<size_t>(face) < setForFace.size())
                setForFace[static_cast<size_t>(face)] = which;
    }
    const size_t groupCount = setNames.empty() ? 1 : setNames.size();

    // One primitive per face set, because a set is how Alembic says "these
    // faces are one material".
    struct Group {
        std::vector<Vec3d> Positions;
        std::vector<Vec3f> Normals;
        std::vector<float> UVs;
        std::vector<uint32_t> Indices;
        std::vector<uint32_t> FaceStarts;
        std::map<Corner, uint32_t> Seen;
        size_t MaxCorners = 0;
    };
    std::vector<Group> groups(groupCount);

    size_t at = 0;
    size_t dropped = 0;
    for (size_t face = 0; face < faceCounts.size(); ++face) {
        const int32_t corners = faceCounts[face];
        if (corners < 3 || at + static_cast<size_t>(corners) > faceIndices.size()) {
            at += corners > 0 ? static_cast<size_t>(corners) : 0;
            ++dropped;
            continue;
        }

        const int which = setNames.empty() ? 0 : std::max(0, setForFace[face]);
        Group& group = groups[static_cast<size_t>(which)];
        group.FaceStarts.push_back(static_cast<uint32_t>(group.Indices.size()));
        group.MaxCorners = std::max(group.MaxCorners, static_cast<size_t>(corners));

        // Reversed: Alembic winds a face the opposite way from the outward
        // normal convention. The face-varying attributes are indexed by corner
        // position, so they have to walk backwards with it.
        for (int32_t k = corners - 1; k >= 0; --k) {
            const size_t corner = at + static_cast<size_t>(k);
            const int32_t position = faceIndices[corner];
            if (position < 0 || static_cast<size_t>(position) >= positions.size()) continue;

            Corner key;
            key.Position = static_cast<uint32_t>(position);
            key.Normal = normalsPerVertex ? key.Position : static_cast<uint32_t>(corner);
            if (!uvIndices.empty() && corner < uvIndices.size()) key.UV = uvIndices[corner];
            else if (uvPerVertex) key.UV = key.Position;
            else key.UV = static_cast<uint32_t>(corner);

            auto found = group.Seen.find(key);
            if (found == group.Seen.end()) {
                const uint32_t fresh = static_cast<uint32_t>(group.Positions.size());
                group.Positions.push_back(positions[key.Position]);

                if (!normals.empty()) {
                    const size_t offset = static_cast<size_t>(key.Normal) * 3;
                    if (offset + 2 < normals.size())
                        group.Normals.emplace_back(normals[offset], normals[offset + 1],
                                                   normals[offset + 2]);
                    else
                        group.Normals.emplace_back(0.0f, 0.0f, 0.0f);
                }
                if (!uvValues.empty()) {
                    const size_t offset = static_cast<size_t>(key.UV) * 2;
                    if (offset + 1 < uvValues.size()) {
                        group.UVs.push_back(uvValues[offset]);
                        group.UVs.push_back(uvValues[offset + 1]);
                    } else {
                        group.UVs.push_back(0.0f);
                        group.UVs.push_back(0.0f);
                    }
                }
                found = group.Seen.emplace(key, fresh).first;
            }
            group.Indices.push_back(found->second);
        }
        at += static_cast<size_t>(corners);
    }
    if (dropped)
        WarnOnce("Alembic: " + std::to_string(dropped) +
                 " face(s) had fewer than three corners or ran past the index array");

    ModelMesh mesh;
    mesh.Name = object.Name();
    for (size_t i = 0; i < groups.size(); ++i) {
        Group& group = groups[i];
        if (group.Indices.empty()) continue;

        MeshPrimitive prim;
        prim.Name = setNames.empty() ? mesh.Name : setNames[i];
        prim.Positions = std::move(group.Positions);
        prim.Normals = std::move(group.Normals);
        prim.Indices = std::move(group.Indices);
        // Keep the n-gons the file has; only an all-triangle mesh can use the
        // simpler mode.
        if (group.MaxCorners <= 3) {
            prim.Mode = PrimitiveMode::Triangles;
        } else {
            prim.Mode = PrimitiveMode::Polygons;
            prim.FaceStarts = std::move(group.FaceStarts);
            prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
        }
        if (!group.UVs.empty()) {
            VertexAttribute uv;
            uv.Semantic = AttributeSemantic::TexCoord;
            uv.Name = "TEXCOORD_0";
            uv.Components = 2;
            uv.Values = std::move(group.UVs);
            prim.Attributes.push_back(std::move(uv));
        }
        if (prim.Normals.empty() && options_.GenerateMissingNormals) prim.RecomputeNormals();
        mesh.Primitives.push_back(std::move(prim));
    }
    if (mesh.Primitives.empty()) return;

    const int meshIndex = document_.AddMesh(std::move(mesh));
    if (node >= 0 && static_cast<size_t>(node) < document_.Nodes.size())
        document_.Nodes[static_cast<size_t>(node)].Mesh = meshIndex;
    ++meshes_;

    if (subdivision)
        WarnOnce("Alembic: a SubD object was read as its control cage; the subdivided surface "
                 "is not in the file, and this reader does not subdivide");
}

// ===== WALK =====

void Reader::Walk(const Og::Object& object, int parentNode) {
    const std::string schema = object.Schema();

    int node = parentNode;
    if (schema == "AbcGeom_Xform_v3" || schema == "AbcGeom_Xform_v1" ||
        schema == "AbcGeom_Xform_v2") {
        node = ReadXform(object, parentNode);
    } else if (schema == "AbcGeom_PolyMesh_v1" || schema == "AbcGeom_SubD_v1") {
        // A shape is its own node, so a mesh under an identity transform still
        // has somewhere to hang its name.
        ModelNode shape;
        shape.Name = object.Name();
        node = document_.AddNode(std::move(shape), parentNode);
        ReadMesh(object, node, schema == "AbcGeom_SubD_v1");
    } else if (schema == "AbcGeom_FaceSet_v1") {
        return;   // consumed by the mesh above it
    } else if (!schema.empty() && schema != "AbcGeom_GeomBase_v1") {
        WarnOnce("Alembic: objects of schema '" + schema +
                 "' are not read (cameras, curves, points and NuPatch are not implemented)");
    }

    for (size_t i = 0; i < object.ChildCount(); ++i) {
        const Og::Object child = object.Child(i);
        if (child.Valid()) Walk(child, node);
    }
}

void Reader::Run() {
    const Og::Object top = archive_.Top();
    if (!top.Valid()) {
        options_.Warn("Alembic: the archive has no top object");
        return;
    }
    for (size_t i = 0; i < top.ChildCount(); ++i) {
        const Og::Object child = top.Child(i);
        if (child.Valid()) Walk(child, -1);
    }

    const std::string application = Og::MetadataValue(archive_.Metadata(), "_ai_Application");
    if (!application.empty()) document_.Metadata["writtenBy"] = application;
    const std::string written = Og::MetadataValue(archive_.Metadata(), "_ai_DateWritten");
    if (!written.empty()) document_.Metadata["dateWritten"] = written;
    const std::string source = Og::MetadataValue(archive_.Metadata(), "_ai_Description");
    if (!source.empty()) document_.Metadata["sourceScene"] = source;
    const std::string frames = Og::MetadataValue(archive_.Metadata(), "FramesPerTimeUnit");
    if (!frames.empty()) document_.Metadata["framesPerSecond"] = frames;
    document_.Metadata["alembicVersion"] = std::to_string(archive_.FileVersion());

    if (meshes_ == 0) options_.Warn("Alembic: the archive holds no polygon or subdivision mesh");
}

} // namespace

// ===== CONVERTER =====

FormatCapabilities AlembicConverter::GetCapabilities() const {
    FormatCapabilities capabilities;
    capabilities.Meshes = true;
    capabilities.NGons = true;
    capabilities.SceneGraph = true;
    capabilities.Instancing = false;
    capabilities.Normals = true;
    capabilities.TextureCoordinates = true;
    capabilities.Metadata = true;
    // Everything below is what the format carries but this reader does not
    // take, and a capability report says what a converter does rather than
    // what its format could hold.
    capabilities.Materials = false;   // Alembic names face sets, not materials
    capabilities.Animations = false;  // the first time sample only
    capabilities.Cameras = false;
    capabilities.Lights = false;
    capabilities.Units = false;       // Alembic states none
    capabilities.UpAxis = false;
    capabilities.DoublePrecision = false;   // P is float32 in every writer
    return capabilities;
}

std::shared_ptr<ModelDocument> AlembicConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string text = buffer.str();
    return ImportFromMemory(std::vector<uint8_t>(text.begin(), text.end()), options);
}

std::shared_ptr<ModelDocument> AlembicConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    Og::Archive archive;
    if (!archive.Open(data, [&options](const std::string& m) { options.Warn(m); }))
        return nullptr;

    auto document = std::make_shared<ModelDocument>();
    document->SourceFormat = "abc";
    document->Generator = options.Generator;
    // Alembic states no up axis. Every writer of it works in Y-up, and the
    // sample this was built against is Y-up, so recording Y is the honest
    // reading of the convention rather than a guess dressed as a fact.
    document->Up = UpAxis::YUp;

    Reader reader(archive, *document, options);
    reader.Run();

    if (document->Empty()) {
        options.Warn("Alembic: nothing in the archive could be read as geometry");
        return nullptr;
    }

    if (options.WeldTolerance > 0.0) document->WeldVertices(options.WeldTolerance);
    if (options.TriangulateOnImport) document->TriangulateAll();
    if (options.ForceUpAxis.has_value()) document->ConvertUpAxis(*options.ForceUpAxis);
    return document;
}

std::shared_ptr<ModelDocument> AlembicConverter::Import(const std::string& filename,
                                                        const ConversionOptions& options) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        options.Warn("Alembic: cannot open " + filename);
        return nullptr;
    }
    auto document = ImportFromStream(file, options);
    if (document && document->Title.empty()) {
        const size_t slash = filename.find_last_of("/\\");
        const std::string base = slash == std::string::npos ? filename : filename.substr(slash + 1);
        const size_t dot = base.find_last_of('.');
        document->Title = dot == std::string::npos ? base : base.substr(0, dot);
    }
    return document;
}

bool AlembicConverter::ValidateData(const std::vector<uint8_t>& data) const {
    return Og::LooksLikeOgawaFile(data);
}

bool AlembicConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> head(32);
    file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return Og::LooksLikeOgawaFile(head);
}

} // namespace ModelConverter
} // namespace UltraCanvas
