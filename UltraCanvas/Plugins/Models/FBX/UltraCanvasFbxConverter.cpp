// Plugins/Models/FBX/UltraCanvasFbxConverter.cpp
// The FBX reader: connections first, then geometry.
//
// The shape of this file follows the shape of the format. `Objects` is a flat
// list and `Connections` is the graph, so nothing can be read in file order:
// the objects are indexed by id, the connections are turned into a
// parent-to-children map, and only then does anything walk from the scene root.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/FBX/UltraCanvasFbxConverter.h"
#include "Models/FBX/UltraCanvasFbxFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;

namespace {

constexpr double kPi = 3.14159265358979323846;
// FBX counts time in these per second, on every platform and in every version.
constexpr double kTicksPerSecond = 46186158000.0;

// An object's Name|Class pair is one string with a 0x00 0x01 separator.
std::string ObjectName(const Fbx::Node& node) {
    const std::string raw = node.TextAt(1);
    const size_t separator = raw.find('\0');
    return separator == std::string::npos ? raw : raw.substr(0, separator);
}

Quatd EulerToQuaternion(double x, double y, double z, int order) {
    const double half = kPi / 360.0;   // degrees to radians, halved
    const Quatd qx = Quatd::FromAxisAngle(Vec3d(1.0, 0.0, 0.0), x * 2.0 * half);
    const Quatd qy = Quatd::FromAxisAngle(Vec3d(0.0, 1.0, 0.0), y * 2.0 * half);
    const Quatd qz = Quatd::FromAxisAngle(Vec3d(0.0, 0.0, 1.0), z * 2.0 * half);
    // `a * b` applies b first, so each line below reads in the order the
    // format's name for it does.
    switch (order) {
        case 1: return qy * qz * qx;   // XZY
        case 2: return qx * qz * qy;   // YZX
        case 3: return qy * qx * qz;   // ZXY  (FBX eEulerZXY)
        case 4: return qz * qx * qy;   // YXZ
        case 5: return qx * qy * qz;   // ZYX
        default: return qz * qy * qx;  // XYZ, the default
    }
}

Matrix4x4 RotationMatrix(const Fbx::Node& model, const char* name, int order) {
    double angles[3] = {0.0, 0.0, 0.0};
    if (!Fbx::PropertyVec3(model, name, angles)) return Matrix4x4::Identity();
    return Matrix4x4::FromQuaternion(EulerToQuaternion(angles[0], angles[1], angles[2], order));
}

Matrix4x4 TranslationMatrix(const Fbx::Node& model, const char* name, double sign = 1.0) {
    double values[3] = {0.0, 0.0, 0.0};
    if (!Fbx::PropertyVec3(model, name, values)) return Matrix4x4::Identity();
    return Matrix4x4::Translation(Vec3d(values[0] * sign, values[1] * sign, values[2] * sign));
}

// One vertex layer: normals, UVs or colours. FBX lets each declare its own
// mapping and reference independently, so all eight combinations resolve here.
struct Layer {
    enum class Mapping { None, ByCorner, ByVertex, ByPolygon, AllSame };

    Mapping Map = Mapping::None;
    bool Indexed = false;
    int Components = 3;
    std::vector<double> Data;
    std::vector<int64_t> Index;

    bool Valid() const { return Map != Mapping::None && !Data.empty(); }

    // The element this corner reads, or -1 when the file does not say.
    int64_t Element(size_t corner, size_t polygon, int64_t position) const {
        int64_t raw = 0;
        switch (Map) {
            case Mapping::ByCorner: raw = static_cast<int64_t>(corner); break;
            case Mapping::ByVertex: raw = position; break;
            case Mapping::ByPolygon: raw = static_cast<int64_t>(polygon); break;
            case Mapping::AllSame: raw = 0; break;
            case Mapping::None: return -1;
        }
        if (!Indexed) return raw;
        if (raw < 0 || static_cast<size_t>(raw) >= Index.size()) return -1;
        return Index[static_cast<size_t>(raw)];
    }
    double Value(int64_t element, int component) const {
        if (element < 0) return 0.0;
        const size_t at = static_cast<size_t>(element) * static_cast<size_t>(Components) +
                          static_cast<size_t>(component);
        return at < Data.size() ? Data[at] : 0.0;
    }
};

Layer::Mapping MappingFrom(const std::string& text) {
    if (text == "ByPolygonVertex" || text == "ByPolygonVertexIndex") return Layer::Mapping::ByCorner;
    if (text == "ByVertice" || text == "ByVertex" || text == "ByControlPoint")
        return Layer::Mapping::ByVertex;
    if (text == "ByPolygon") return Layer::Mapping::ByPolygon;
    if (text == "AllSame" || text == "ByModel") return Layer::Mapping::AllSame;
    return Layer::Mapping::None;
}

// Everything one Geometry carries, before it becomes primitives.
struct GeometryData {
    std::string Name;
    std::vector<Vec3d> Positions;
    std::vector<std::vector<int64_t>> Polygons;
    Layer Normals;
    Layer TexCoords;
    Layer Colors;
    Layer Materials;          // its Data is unused; the index array is the assignment
    std::vector<int64_t> MaterialPerPolygon;
};

struct Curve {
    std::vector<double> Times;    // seconds
    std::vector<double> Values;
    double Default = 0.0;

    double At(double time) const {
        if (Times.empty()) return Default;
        if (time <= Times.front()) return Values.front();
        if (time >= Times.back()) return Values.back();
        // Linear between the bracketing keys. FBX stores per-key tangent data
        // for its own cubic evaluation; reading it would be a second
        // interpolation mode the document does not have, so the sampler says
        // Linear and means it.
        const size_t upper = static_cast<size_t>(
                std::lower_bound(Times.begin(), Times.end(), time) - Times.begin());
        const size_t lower = upper - 1;
        const double span = Times[upper] - Times[lower];
        if (span <= 0.0) return Values[lower];
        const double t = (time - Times[lower]) / span;
        return Values[lower] + (Values[upper] - Values[lower]) * t;
    }
};

// ===== READER =====

class Reader {
public:
    Reader(const Fbx::File& file, const ConversionOptions& options)
        : file_(file), options_(options) {}

    std::shared_ptr<ModelDocument> Run() {
        document_ = std::make_shared<ModelDocument>();
        document_->SourceFormat = "fbx";
        document_->Metadata["fbx.version"] = std::to_string(file_.Version);

        ReadHeader();
        ReadGlobalSettings();
        IndexObjects();
        ReadConnections();

        // Models hanging off the scene root, which FBX spells as parent id 0.
        auto roots = childrenOf_.find(0);
        if (roots != childrenOf_.end())
            for (int64_t id : roots->second) ReadModel(id, -1);

        ReadAnimations();

        if (document_->Meshes.empty() && document_->Nodes.empty()) {
            options_.Warn("FBX: no models found");
            return nullptr;
        }
        Finish();
        return document_;
    }

private:
    // ----- header and settings -----

    void ReadHeader() {
        if (const Fbx::Node* creator = file_.Find("Creator"))
            document_->Generator = creator->TextAt(0);
        if (const Fbx::Node* documents = file_.Find("Documents")) {
            if (const Fbx::Node* entry = documents->Find("Document"))
                document_->Title = entry->TextAt(1);
        }
        if (const Fbx::Node* extension = file_.Find("FBXHeaderExtension")) {
            if (const Fbx::Node* info = extension->Find("SceneInfo")) {
                const std::string application = Fbx::PropertyText(*info, "Original|ApplicationName");
                if (!application.empty()) document_->Metadata["fbx.application"] = application;
            }
        }
    }

    void ReadGlobalSettings() {
        const Fbx::Node* settings = file_.Find("GlobalSettings");
        if (!settings) {
            options_.Warn("FBX: no GlobalSettings; Y-up and an unstated unit are assumed");
            document_->Up = UpAxis::YUp;
            return;
        }

        // UpAxis is an axis index, 0/1/2 for X/Y/Z, with a separate sign. The
        // document holds only Y-up or Z-up, so X-up is reported rather than
        // silently turned into one of them.
        const int axis = static_cast<int>(Fbx::PropertyReal(*settings, "UpAxis", 1.0));
        const int sign = static_cast<int>(Fbx::PropertyReal(*settings, "UpAxisSign", 1.0));
        if (axis == 2) document_->Up = UpAxis::ZUp;
        else if (axis == 1) document_->Up = UpAxis::YUp;
        else {
            document_->Up = UpAxis::YUp;
            options_.Warn("FBX: GlobalSettings says the up axis is X, which the document cannot "
                          "express; the model is left as written and will appear rotated");
        }
        if (sign < 0)
            options_.Warn("FBX: GlobalSettings says the up axis points the other way "
                          "(UpAxisSign is negative), which the document cannot express");

        // UnitScaleFactor is centimetres per stored unit - FBX's own unit is
        // the centimetre - so metres per unit is a hundredth of it.
        const double centimetres = Fbx::PropertyReal(*settings, "UnitScaleFactor", 1.0);
        if (centimetres > 0.0) {
            document_->UnitScaleToMeters = centimetres * 0.01;
            document_->SourceUnit = UnitFromMetres(document_->UnitScaleToMeters);
        }
    }

    static ModelUnit UnitFromMetres(double metres) {
        struct Candidate { ModelUnit Unit; double Metres; };
        static const Candidate candidates[] = {
                {ModelUnit::Micrometer, 1e-6}, {ModelUnit::Millimeter, 1e-3},
                {ModelUnit::Centimeter, 1e-2}, {ModelUnit::Decimeter, 1e-1},
                {ModelUnit::Meter, 1.0},       {ModelUnit::Kilometer, 1000.0},
                {ModelUnit::Inch, 0.0254},     {ModelUnit::Foot, 0.3048},
                {ModelUnit::Yard, 0.9144},     {ModelUnit::Mile, 1609.344},
                {ModelUnit::Mil, 0.0000254}};
        for (const Candidate& candidate : candidates)
            if (std::fabs(metres - candidate.Metres) <= candidate.Metres * 1e-6)
                return candidate.Unit;
        return ModelUnit::Unspecified;
    }

    // ----- the graph -----

    void IndexObjects() {
        const Fbx::Node* objects = file_.Find("Objects");
        if (!objects) return;
        for (const Fbx::Node& object : objects->Children) {
            const int64_t id = object.IntegerAt(0);
            if (id == 0) continue;
            objects_.emplace(id, &object);
        }
    }

    void ReadConnections() {
        const Fbx::Node* connections = file_.Find("Connections");
        if (!connections) {
            options_.Warn("FBX: no Connections; the file states no hierarchy at all");
            return;
        }
        for (const Fbx::Node& entry : connections->Children) {
            if (entry.Name != "C") continue;
            const std::string kind = entry.TextAt(0);
            // FBX writes the child first and the parent second, which is the
            // opposite of how it reads.
            const int64_t child = entry.IntegerAt(1);
            const int64_t parent = entry.IntegerAt(2);
            if (kind == "OO") {
                childrenOf_[parent].push_back(child);
            } else if (kind == "OP") {
                propertyLinks_[parent].push_back({child, entry.TextAt(3)});
            }
        }
    }

    const Fbx::Node* ObjectOf(int64_t id) const {
        auto found = objects_.find(id);
        return found == objects_.end() ? nullptr : found->second;
    }
    std::vector<int64_t> ChildrenOfType(int64_t id, const std::string& type) const {
        std::vector<int64_t> found;
        auto children = childrenOf_.find(id);
        if (children == childrenOf_.end()) return found;
        for (int64_t child : children->second) {
            const Fbx::Node* node = ObjectOf(child);
            if (node && node->Name == type) found.push_back(child);
        }
        return found;
    }

    // ----- models -----

    // The full transform chain the format defines. Everything but T, R and S is
    // identity in a Blender export and constantly non-identity in a Maya one.
    Matrix4x4 LocalTransform(const Fbx::Node& model) const {
        const int order = static_cast<int>(Fbx::PropertyReal(model, "RotationOrder", 0.0));

        double scaling[3] = {1.0, 1.0, 1.0};
        Fbx::PropertyVec3(model, "Lcl Scaling", scaling);

        Matrix4x4 result = TranslationMatrix(model, "Lcl Translation");
        result = result * TranslationMatrix(model, "RotationOffset");
        result = result * TranslationMatrix(model, "RotationPivot");
        result = result * RotationMatrix(model, "PreRotation", order);
        result = result * RotationMatrix(model, "Lcl Rotation", order);
        if (Fbx::HasProperty(model, "PostRotation")) {
            Matrix4x4 post = RotationMatrix(model, "PostRotation", order);
            // A rotation matrix's inverse is its transpose.
            Matrix4x4 inverse;
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                    inverse.m[column * 4 + row] = post.m[row * 4 + column];
            result = result * inverse;
        }
        result = result * TranslationMatrix(model, "RotationPivot", -1.0);
        result = result * TranslationMatrix(model, "ScalingOffset");
        result = result * TranslationMatrix(model, "ScalingPivot");
        result = result * Matrix4x4::Scaling(Vec3d(scaling[0], scaling[1], scaling[2]));
        result = result * TranslationMatrix(model, "ScalingPivot", -1.0);
        return result;
    }

    void ReadModel(int64_t id, int parent) {
        const Fbx::Node* model = ObjectOf(id);
        if (!model || model->Name != "Model") return;
        if (!visited_.insert(id).second) return;   // a cycle in Connections

        ModelNode node;
        node.Name = ObjectName(*model);
        if (node.Name.empty()) node.Name = "Model";

        const Matrix4x4 matrix = LocalTransform(*model);
        Vec3d translation, scale;
        Quatd rotation;
        if (matrix.DecomposeTRS(translation, rotation, scale)) {
            node.Translation = translation;
            node.Rotation = rotation;
            node.Scale = scale;
        } else if (!matrix.IsIdentity(1e-15)) {
            node.Matrix = matrix;
        }

        const int index = document_->AddNode(std::move(node), parent);
        nodeIndexById_[id] = index;

        const std::string subtype = model->TextAt(2);
        if (subtype == "LimbNode" || subtype == "Limb") sawBones_ = true;

        AttachGeometry(id, index);

        for (int64_t child : ChildrenOfType(id, "Model")) ReadModel(child, index);

        if (!ChildrenOfType(id, "Deformer").empty()) WarnAboutSkinning();
    }

    void WarnAboutSkinning() {
        WarnOnce("skin", "FBX: skin deformers and their clusters are not read, so a skinned "
                         "mesh arrives in its bind pose");
    }

    void AttachGeometry(int64_t modelId, int nodeIndex) {
        const std::vector<int64_t> geometries = ChildrenOfType(modelId, "Geometry");
        if (geometries.empty()) return;

        // The materials connected to this *model*, in connection order: a
        // geometry's LayerElementMaterial indexes that list, not a global one.
        std::vector<int> materials;
        for (int64_t material : ChildrenOfType(modelId, "Material"))
            materials.push_back(MaterialIndexOf(material));

        const Fbx::Node* model = ObjectOf(modelId);
        for (size_t i = 0; i < geometries.size(); ++i) {
            const int meshIndex = MeshFor(geometries[i], materials);
            if (meshIndex < 0) continue;

            int target = nodeIndex;
            // A geometric transform places the mesh without being inherited by
            // this node's children, which one transform per node cannot say -
            // so it becomes a child node rather than being baked into vertices.
            const Matrix4x4 geometric = model ? GeometricTransform(*model) : Matrix4x4::Identity();
            const bool needsOwnNode = !geometric.IsIdentity(1e-15) || i > 0 ||
                                      document_->Nodes[static_cast<size_t>(nodeIndex)].Mesh >= 0;
            if (needsOwnNode) {
                ModelNode holder;
                holder.Name = document_->Meshes[static_cast<size_t>(meshIndex)].Name;
                Vec3d translation, scale;
                Quatd rotation;
                if (geometric.DecomposeTRS(translation, rotation, scale)) {
                    holder.Translation = translation;
                    holder.Rotation = rotation;
                    holder.Scale = scale;
                } else if (!geometric.IsIdentity(1e-15)) {
                    holder.Matrix = geometric;
                }
                holder.Mesh = meshIndex;
                target = document_->AddNode(std::move(holder), nodeIndex);
            } else {
                document_->Nodes[static_cast<size_t>(nodeIndex)].Mesh = meshIndex;
            }
            meshNodes_.push_back(target);
        }
    }

    static Matrix4x4 GeometricTransform(const Fbx::Node& model) {
        double translation[3] = {0.0, 0.0, 0.0};
        double scaling[3] = {1.0, 1.0, 1.0};
        const bool hasTranslation = Fbx::PropertyVec3(model, "GeometricTranslation", translation);
        const bool hasScaling = Fbx::PropertyVec3(model, "GeometricScaling", scaling);
        const bool hasRotation = Fbx::HasProperty(model, "GeometricRotation");
        if (!hasTranslation && !hasScaling && !hasRotation) return Matrix4x4::Identity();

        const int order = static_cast<int>(Fbx::PropertyReal(model, "RotationOrder", 0.0));
        Matrix4x4 result = Matrix4x4::Translation(Vec3d(translation[0], translation[1], translation[2]));
        result = result * RotationMatrix(model, "GeometricRotation", order);
        return result * Matrix4x4::Scaling(Vec3d(scaling[0], scaling[1], scaling[2]));
    }

    // ----- geometry -----

    int MeshFor(int64_t geometryId, const std::vector<int>& materials) {
        // One geometry used by two models with different material lists is two
        // document meshes, because the material sits on the primitive.
        std::string key = std::to_string(geometryId);
        for (int material : materials) key += "#" + std::to_string(material);
        auto cached = meshCache_.find(key);
        if (cached != meshCache_.end()) return cached->second;

        const Fbx::Node* geometry = ObjectOf(geometryId);
        if (!geometry) return -1;
        if (geometry->TextAt(2) == "NurbsCurve" || geometry->TextAt(2) == "NurbsSurface") {
            WarnOnce("nurbs", "FBX: a NURBS geometry is not read; only meshes are");
            return -1;
        }

        GeometryData data;
        data.Name = ObjectName(*geometry);
        if (!ReadGeometry(*geometry, data)) return -1;

        std::vector<MeshPrimitive> primitives = BuildPrimitives(data, materials);
        if (primitives.empty()) return -1;

        ModelMesh mesh;
        mesh.Name = data.Name;
        mesh.Primitives = std::move(primitives);
        const int index = document_->AddMesh(std::move(mesh));
        meshCache_[key] = index;
        return index;
    }

    bool ReadGeometry(const Fbx::Node& geometry, GeometryData& data) {
        const Fbx::Node* vertices = geometry.Find("Vertices");
        const Fbx::Node* indices = geometry.Find("PolygonVertexIndex");
        if (!vertices || !indices || vertices->Properties.empty() || indices->Properties.empty()) {
            options_.Warn("FBX: geometry '" + data.Name + "' has no Vertices or no "
                          "PolygonVertexIndex");
            return false;
        }

        const Fbx::Property& positions = vertices->Properties[0];
        const size_t count = positions.ArraySize() / 3;
        data.Positions.reserve(count);
        for (size_t i = 0; i < count; ++i)
            data.Positions.emplace_back(positions.ArrayReal(i * 3), positions.ArrayReal(i * 3 + 1),
                                        positions.ArrayReal(i * 3 + 2));

        // A polygon runs until an index arrives negative; that last one is the
        // bitwise complement of the real index, which is how the format marks
        // the end without a separate count.
        const Fbx::Property& corners = indices->Properties[0];
        std::vector<int64_t> polygon;
        for (size_t i = 0; i < corners.ArraySize(); ++i) {
            const int64_t raw = corners.ArrayInteger(i);
            if (raw < 0) {
                polygon.push_back(~raw);
                data.Polygons.push_back(polygon);
                polygon.clear();
            } else {
                polygon.push_back(raw);
            }
        }
        if (!polygon.empty())
            options_.Warn("FBX: geometry '" + data.Name + "' ends with an unterminated polygon, "
                          "which is dropped");

        ReadLayer(geometry, "LayerElementNormal", "Normals", "NormalsIndex", 3, data.Normals);
        ReadLayer(geometry, "LayerElementUV", "UV", "UVIndex", 2, data.TexCoords);
        ReadLayer(geometry, "LayerElementColor", "Colors", "ColorIndex", 4, data.Colors);
        ReadMaterialLayer(geometry, data);

        if (geometry.Find("LayerElementTangent") || geometry.Find("LayerElementBinormal"))
            WarnOnce("tangent", "FBX: tangent and binormal layers are not read");
        if (!geometry.FindAll("LayerElementUV").empty() &&
            geometry.FindAll("LayerElementUV").size() > 1)
            WarnOnce("uvsets", "FBX: a geometry has more than one UV layer; only the first is read");
        return !data.Positions.empty() && !data.Polygons.empty();
    }

    void ReadLayer(const Fbx::Node& geometry, const char* layerName, const char* dataName,
                   const char* indexName, int components, Layer& layer) {
        const Fbx::Node* node = geometry.Find(layerName);
        if (!node) return;
        const Fbx::Node* values = node->Find(dataName);
        if (!values || values->Properties.empty()) return;

        layer.Components = components;
        layer.Map = MappingFrom(node->Find("MappingInformationType")
                                        ? node->Find("MappingInformationType")->TextAt(0)
                                        : std::string());
        const std::string reference = node->Find("ReferenceInformationType")
                                              ? node->Find("ReferenceInformationType")->TextAt(0)
                                              : std::string("Direct");
        layer.Indexed = reference == "IndexToDirect" || reference == "Index";

        const Fbx::Property& source = values->Properties[0];
        layer.Data.resize(source.ArraySize());
        for (size_t i = 0; i < source.ArraySize(); ++i) layer.Data[i] = source.ArrayReal(i);

        if (layer.Indexed) {
            const Fbx::Node* index = node->Find(indexName);
            if (index && !index->Properties.empty()) {
                const Fbx::Property& source2 = index->Properties[0];
                layer.Index.resize(source2.ArraySize());
                for (size_t i = 0; i < source2.ArraySize(); ++i)
                    layer.Index[i] = source2.ArrayInteger(i);
            } else {
                // IndexToDirect with no index array means direct after all.
                layer.Indexed = false;
            }
        }
        if (layer.Map == Layer::Mapping::None) {
            WarnOnce(std::string("mapping.") + layerName,
                     std::string("FBX: a ") + layerName +
                             " declares a mapping this reader does not know; it is dropped");
            layer.Data.clear();
        }
    }

    void ReadMaterialLayer(const Fbx::Node& geometry, GeometryData& data) {
        const Fbx::Node* node = geometry.Find("LayerElementMaterial");
        if (!node) return;
        const Fbx::Node* values = node->Find("Materials");
        if (!values || values->Properties.empty()) return;

        const Layer::Mapping mapping =
                MappingFrom(node->Find("MappingInformationType")
                                    ? node->Find("MappingInformationType")->TextAt(0)
                                    : std::string());
        const Fbx::Property& source = values->Properties[0];

        data.MaterialPerPolygon.assign(data.Polygons.size(), 0);
        if (mapping == Layer::Mapping::AllSame) {
            const int64_t only = source.ArraySize() ? source.ArrayInteger(0) : 0;
            std::fill(data.MaterialPerPolygon.begin(), data.MaterialPerPolygon.end(), only);
        } else if (mapping == Layer::Mapping::ByPolygon) {
            for (size_t i = 0; i < data.MaterialPerPolygon.size(); ++i)
                data.MaterialPerPolygon[i] = i < source.ArraySize() ? source.ArrayInteger(i) : 0;
        } else {
            WarnOnce("materialmapping",
                     "FBX: a LayerElementMaterial is mapped per vertex or per corner, which a "
                     "face assignment cannot be; the whole mesh takes its first material");
        }
    }

    std::vector<MeshPrimitive> BuildPrimitives(const GeometryData& data,
                                               const std::vector<int>& materials) {
        // One primitive per material actually used, in order of first use, so a
        // single-material mesh stays one primitive with its vertices intact.
        std::vector<int64_t> order;
        std::map<int64_t, size_t> slotOf;
        std::vector<size_t> slotPerPolygon(data.Polygons.size(), 0);
        for (size_t polygon = 0; polygon < data.Polygons.size(); ++polygon) {
            const int64_t material = polygon < data.MaterialPerPolygon.size()
                                             ? data.MaterialPerPolygon[polygon] : 0;
            auto found = slotOf.find(material);
            if (found == slotOf.end()) {
                slotOf.emplace(material, order.size());
                slotPerPolygon[polygon] = order.size();
                order.push_back(material);
            } else {
                slotPerPolygon[polygon] = found->second;
            }
        }
        if (order.empty()) order.push_back(0);

        std::vector<MeshPrimitive> primitives(order.size());
        std::vector<std::map<std::array<int64_t, 4>, uint32_t>> caches(order.size());
        std::vector<std::vector<float>> texcoords(order.size());
        std::vector<std::vector<float>> colors(order.size());
        std::vector<size_t> maxCorners(order.size(), 0);
        bool warnedRange = false;
        size_t corner = 0;

        for (size_t polygon = 0; polygon < data.Polygons.size(); ++polygon) {
            const std::vector<int64_t>& indices = data.Polygons[polygon];
            const size_t slot = slotPerPolygon[polygon];
            MeshPrimitive& prim = primitives[slot];
            const size_t start = prim.Indices.size();
            bool ok = indices.size() >= 3;

            for (size_t i = 0; i < indices.size(); ++i, ++corner) {
                if (!ok) continue;
                const int64_t position = indices[i];
                if (position < 0 || static_cast<size_t>(position) >= data.Positions.size()) {
                    ok = false;
                    continue;
                }
                std::array<int64_t, 4> key{position, -1, -1, -1};
                if (data.Normals.Valid()) key[1] = data.Normals.Element(corner, polygon, position);
                if (data.TexCoords.Valid()) key[2] = data.TexCoords.Element(corner, polygon, position);
                if (data.Colors.Valid()) key[3] = data.Colors.Element(corner, polygon, position);

                auto found = caches[slot].find(key);
                if (found != caches[slot].end()) {
                    prim.Indices.push_back(found->second);
                    continue;
                }
                const uint32_t fresh = static_cast<uint32_t>(prim.Positions.size());
                caches[slot].emplace(key, fresh);
                prim.Indices.push_back(fresh);

                prim.Positions.push_back(data.Positions[static_cast<size_t>(position)]);
                if (data.Normals.Valid())
                    prim.Normals.emplace_back(static_cast<float>(data.Normals.Value(key[1], 0)),
                                              static_cast<float>(data.Normals.Value(key[1], 1)),
                                              static_cast<float>(data.Normals.Value(key[1], 2)));
                if (data.TexCoords.Valid()) {
                    texcoords[slot].push_back(static_cast<float>(data.TexCoords.Value(key[2], 0)));
                    texcoords[slot].push_back(static_cast<float>(data.TexCoords.Value(key[2], 1)));
                }
                if (data.Colors.Valid())
                    for (int c = 0; c < 4; ++c)
                        colors[slot].push_back(static_cast<float>(data.Colors.Value(key[3], c)));
            }

            if (!ok) {
                prim.Indices.resize(start);
                if (!warnedRange) {
                    options_.Warn("FBX: geometry '" + data.Name + "' has a polygon indexing past "
                                  "its vertex list, or with fewer than three corners; those "
                                  "polygons are dropped");
                    warnedRange = true;
                }
                continue;
            }
            prim.FaceStarts.push_back(static_cast<uint32_t>(start));
            maxCorners[slot] = std::max(maxCorners[slot], indices.size());
        }

        std::vector<MeshPrimitive> kept;
        for (size_t slot = 0; slot < primitives.size(); ++slot) {
            MeshPrimitive& prim = primitives[slot];
            if (prim.Positions.empty() || prim.FaceStarts.empty()) continue;
            prim.Name = data.Name;

            const int64_t material = order[slot];
            if (material >= 0 && static_cast<size_t>(material) < materials.size())
                prim.Material = materials[static_cast<size_t>(material)];

            if (maxCorners[slot] <= 3) {
                prim.Mode = PrimitiveMode::Triangles;
                prim.FaceStarts.clear();
            } else {
                prim.Mode = PrimitiveMode::Polygons;
                prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
            }
            if (!texcoords[slot].empty()) {
                VertexAttribute uv;
                uv.Semantic = AttributeSemantic::TexCoord;
                uv.Name = "TEXCOORD_0";
                uv.Components = 2;
                uv.Values = std::move(texcoords[slot]);
                prim.Attributes.push_back(std::move(uv));
            }
            if (!colors[slot].empty()) {
                VertexAttribute color;
                color.Semantic = AttributeSemantic::Color;
                color.Name = "COLOR_0";
                color.Components = 4;
                color.Values = std::move(colors[slot]);
                prim.Attributes.push_back(std::move(color));
            }
            kept.push_back(std::move(prim));
        }
        return kept;
    }

    // ----- materials and textures -----

    int MaterialIndexOf(int64_t id) {
        auto cached = materialIndex_.find(id);
        if (cached != materialIndex_.end()) return cached->second;

        const Fbx::Node* node = ObjectOf(id);
        if (!node) return -1;

        ModelMaterial material;
        material.Name = ObjectName(*node);

        PhongParams phong;
        double colour[3] = {0.8, 0.8, 0.8};
        if (Fbx::PropertyVec3(*node, "DiffuseColor", colour)) {
            const double factor = Fbx::PropertyReal(*node, "DiffuseFactor", 1.0);
            phong.Diffuse = Vec3f(static_cast<float>(colour[0] * factor),
                                  static_cast<float>(colour[1] * factor),
                                  static_cast<float>(colour[2] * factor));
        }
        double specular[3] = {0.0, 0.0, 0.0};
        if (Fbx::PropertyVec3(*node, "SpecularColor", specular)) {
            const double factor = Fbx::PropertyReal(*node, "SpecularFactor", 1.0);
            phong.Specular = Vec3f(static_cast<float>(specular[0] * factor),
                                   static_cast<float>(specular[1] * factor),
                                   static_cast<float>(specular[2] * factor));
        }
        double ambient[3] = {0.0, 0.0, 0.0};
        if (Fbx::PropertyVec3(*node, "AmbientColor", ambient)) {
            const double factor = Fbx::PropertyReal(*node, "AmbientFactor", 1.0);
            phong.Ambient = Vec3f(static_cast<float>(ambient[0] * factor),
                                  static_cast<float>(ambient[1] * factor),
                                  static_cast<float>(ambient[2] * factor));
        }
        double emissive[3] = {0.0, 0.0, 0.0};
        if (Fbx::PropertyVec3(*node, "EmissiveColor", emissive)) {
            // EmissiveFactor is usually zero even when EmissiveColor is not,
            // because exporters write the diffuse colour into both. Multiplying
            // is what the format means and what keeps a model from glowing.
            const double factor = Fbx::PropertyReal(*node, "EmissiveFactor", 1.0);
            material.EmissiveFactor = Vec3f(static_cast<float>(emissive[0] * factor),
                                            static_cast<float>(emissive[1] * factor),
                                            static_cast<float>(emissive[2] * factor));
        }
        // ShininessExponent and Shininess are the same number under two names,
        // and it is the specular exponent - MTL's Ns - outright.
        double shininess = Fbx::PropertyReal(*node, "ShininessExponent", -1.0);
        if (shininess < 0.0) shininess = Fbx::PropertyReal(*node, "Shininess", 0.0);
        phong.Shininess = static_cast<float>(shininess);

        // Opacity and TransparencyFactor are two spellings of the same thing,
        // one the complement of the other.
        double opacity = 1.0;
        if (Fbx::HasProperty(*node, "Opacity")) opacity = Fbx::PropertyReal(*node, "Opacity", 1.0);
        else if (Fbx::HasProperty(*node, "TransparencyFactor"))
            opacity = 1.0 - Fbx::PropertyReal(*node, "TransparencyFactor", 0.0);
        opacity = std::min(1.0, std::max(0.0, opacity));

        material.IndexOfRefraction = 1.0f;
        BindTextures(id, phong, material);

        material.Phong = phong;
        material.BaseColorFactor = Vec4f(phong.Diffuse.x, phong.Diffuse.y, phong.Diffuse.z,
                                         static_cast<float>(opacity));
        if (opacity < 1.0) material.Alpha = AlphaMode::Blend;
        material.DeriveMissingModel();

        const int index = document_->AddMaterial(std::move(material));
        materialIndex_[id] = index;
        return index;
    }

    // A texture reaches a material through an OP connection naming the slot it
    // fills. Several textures may name the same slot - FBX layers them - and
    // the document holds one, so the first wins and the rest are reported.
    void BindTextures(int64_t materialId, PhongParams& phong, ModelMaterial& material) {
        auto links = propertyLinks_.find(materialId);
        if (links == propertyLinks_.end()) return;
        std::set<std::string> filled;

        for (const auto& link : links->second) {
            const Fbx::Node* texture = ObjectOf(link.first);
            if (!texture || texture->Name != "Texture") continue;
            const std::string& slot = link.second;

            TextureRef reference;
            reference.Image = ImageIndexOf(link.first);
            if (reference.Image < 0) continue;

            if (!filled.insert(slot).second) {
                WarnOnce("layered." + slot,
                         "FBX: more than one texture is connected to '" + slot +
                                 "' on material '" + material.Name +
                                 "'; FBX layers them and the document holds one, so the first "
                                 "is kept");
                continue;
            }

            if (slot == "DiffuseColor") phong.DiffuseTexture = reference;
            else if (slot == "SpecularColor" || slot == "SpecularFactor")
                phong.SpecularTexture = reference;
            else if (slot == "AmbientColor") phong.AmbientTexture = reference;
            else if (slot == "NormalMap" || slot == "Bump") material.NormalTexture = reference;
            else if (slot == "EmissiveColor") material.EmissiveTexture = reference;
            else
                WarnOnce("slot." + slot,
                         "FBX: a texture is connected to '" + slot +
                                 "', which the document has no slot for; it is not read");
        }
    }

    int ImageIndexOf(int64_t textureId) {
        auto cached = imageIndex_.find(textureId);
        if (cached != imageIndex_.end()) return cached->second;

        const Fbx::Node* texture = ObjectOf(textureId);
        if (!texture) return -1;

        // RelativeFilename is the one worth keeping: FileName is the absolute
        // path on the machine that exported, which is never right anywhere else.
        std::string uri;
        if (const Fbx::Node* relative = texture->Find("RelativeFilename")) uri = relative->TextAt(0);
        if (uri.empty())
            if (const Fbx::Node* absolute = texture->Find("FileName")) uri = absolute->TextAt(0);
        if (uri.empty()) return -1;
        for (char& c : uri)
            if (c == '\\') c = '/';

        // A Video object carries the media, and may embed the bytes outright.
        for (int64_t child : ChildrenOfType(textureId, "Video")) {
            const Fbx::Node* video = ObjectOf(child);
            if (video && video->Find("Content") && !video->Find("Content")->Properties.empty() &&
                !video->Find("Content")->Properties[0].Text.empty())
                WarnOnce("embedded", "FBX: a Video object embeds its image bytes, which are not "
                                     "decoded; the texture keeps its path instead");
        }

        ModelImage image;
        image.Name = ObjectName(*texture);
        image.Uri = uri;
        const size_t dot = uri.rfind('.');
        if (dot != std::string::npos) {
            std::string extension = uri.substr(dot + 1);
            for (char& c : extension)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (extension == "png") image.MimeType = "image/png";
            else if (extension == "jpg" || extension == "jpeg") image.MimeType = "image/jpeg";
            else if (extension == "tga") image.MimeType = "image/x-tga";
            else if (extension == "bmp") image.MimeType = "image/bmp";
            else if (extension == "tif" || extension == "tiff") image.MimeType = "image/tiff";
        }
        const int index = static_cast<int>(document_->Images.size());
        document_->Images.push_back(std::move(image));
        imageIndex_[textureId] = index;
        return index;
    }

    // ----- animation -----
    //
    // FBX keeps no keyframes on the node. An AnimationStack holds layers, a
    // layer holds AnimationCurveNodes, and a curve node is wired by an OP
    // connection to one property of one model - then by three more OP
    // connections to one AnimationCurve per axis. Following those hops is what
    // turns a flat pile of objects into channels.

    Curve ReadCurve(int64_t id, double fallback) const {
        Curve curve;
        curve.Default = fallback;
        const Fbx::Node* node = ObjectOf(id);
        if (!node) return curve;
        const Fbx::Node* times = node->Find("KeyTime");
        const Fbx::Node* values = node->Find("KeyValueFloat");
        if (!times || !values || times->Properties.empty() || values->Properties.empty())
            return curve;

        const Fbx::Property& t = times->Properties[0];
        const Fbx::Property& v = values->Properties[0];
        const size_t count = std::min(t.ArraySize(), v.ArraySize());
        curve.Times.reserve(count);
        curve.Values.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            curve.Times.push_back(static_cast<double>(t.ArrayInteger(i)) / kTicksPerSecond);
            curve.Values.push_back(v.ArrayReal(i));
        }
        return curve;
    }

    void ReadAnimations() {
        // curve node -> the model and property it drives.
        std::map<int64_t, std::pair<int64_t, std::string>> target;
        for (const auto& entry : propertyLinks_) {
            const Fbx::Node* owner = ObjectOf(entry.first);
            if (!owner || owner->Name != "Model") continue;
            for (const auto& link : entry.second) {
                const Fbx::Node* node = ObjectOf(link.first);
                if (node && node->Name == "AnimationCurveNode")
                    target[link.first] = {entry.first, link.second};
            }
        }

        for (const auto& entry : objects_) {
            if (entry.second->Name != "AnimationStack") continue;
            ModelAnimation animation;
            animation.Name = ObjectName(*entry.second);

            for (int64_t layerId : ChildrenOfType(entry.first, "AnimationLayer"))
                for (int64_t curveNodeId : ChildrenOfType(layerId, "AnimationCurveNode"))
                    ReadCurveNode(curveNodeId, target, animation);

            if (!animation.Channels.empty()) document_->Animations.push_back(std::move(animation));
        }
    }

    void ReadCurveNode(int64_t curveNodeId,
                       const std::map<int64_t, std::pair<int64_t, std::string>>& target,
                       ModelAnimation& animation) {
        auto bound = target.find(curveNodeId);
        if (bound == target.end()) return;

        auto node = nodeIndexById_.find(bound->second.first);
        if (node == nodeIndexById_.end()) return;

        AnimationPath path;
        const std::string& property = bound->second.second;
        if (property == "Lcl Translation") path = AnimationPath::Translation;
        else if (property == "Lcl Rotation") path = AnimationPath::Rotation;
        else if (property == "Lcl Scaling") path = AnimationPath::Scale;
        else {
            WarnOnce("animated." + property,
                     "FBX: '" + property + "' is animated, which the document has no animation "
                     "path for; that channel is not read");
            return;
        }

        const Fbx::Node* curveNode = ObjectOf(curveNodeId);
        if (!curveNode) return;

        // The three axis curves, and the curve node's own defaults for any
        // axis that has none.
        const char* axisNames[3] = {"d|X", "d|Y", "d|Z"};
        const double fallbackScale = path == AnimationPath::Scale ? 1.0 : 0.0;
        Curve curves[3];
        for (int axis = 0; axis < 3; ++axis)
            curves[axis].Default = Fbx::PropertyReal(*curveNode, axisNames[axis], fallbackScale);

        auto links = propertyLinks_.find(curveNodeId);
        if (links != propertyLinks_.end()) {
            for (const auto& link : links->second) {
                const Fbx::Node* curve = ObjectOf(link.first);
                if (!curve || curve->Name != "AnimationCurve") continue;
                for (int axis = 0; axis < 3; ++axis)
                    if (link.second == axisNames[axis])
                        curves[axis] = ReadCurve(link.first, curves[axis].Default);
            }
        }

        // The union of the three axes' key times: FBX curves are independent
        // and need not agree, and the document interpolates a vector.
        std::vector<double> times;
        for (int axis = 0; axis < 3; ++axis)
            times.insert(times.end(), curves[axis].Times.begin(), curves[axis].Times.end());
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end()), times.end());
        if (times.empty()) return;

        const Fbx::Node* model = ObjectOf(bound->second.first);
        const int order = model ? static_cast<int>(Fbx::PropertyReal(*model, "RotationOrder", 0.0)) : 0;
        if (model && path == AnimationPath::Rotation) {
            for (const char* pivot : {"RotationPivot", "RotationOffset", "ScalingPivot",
                                      "ScalingOffset"}) {
                double values[3] = {0.0, 0.0, 0.0};
                if (Fbx::PropertyVec3(*model, pivot, values) &&
                    (values[0] != 0.0 || values[1] != 0.0 || values[2] != 0.0)) {
                    WarnOnce("animatedpivot",
                             "FBX: an animated node has a non-zero rotation or scaling pivot, "
                             "which its animation channel cannot carry; the rest pose is exact "
                             "but the animation ignores the pivot");
                    break;
                }
            }
        }

        AnimationSampler sampler;
        sampler.Interpolate = Interpolation::Linear;
        sampler.Times.reserve(times.size());
        for (double time : times) {
            sampler.Times.push_back(static_cast<float>(time));
            const double x = curves[0].At(time);
            const double y = curves[1].At(time);
            const double z = curves[2].At(time);
            if (path == AnimationPath::Rotation) {
                Quatd rotation = EulerToQuaternion(x, y, z, order);
                // The rest transform composes PreRotation before and PostRotation
                // after the local rotation, so an animated channel has to as
                // well or the animated pose will not meet the rest pose.
                if (model && Fbx::HasProperty(*model, "PreRotation")) {
                    double pre[3] = {0.0, 0.0, 0.0};
                    Fbx::PropertyVec3(*model, "PreRotation", pre);
                    rotation = EulerToQuaternion(pre[0], pre[1], pre[2], order) * rotation;
                }
                if (model && Fbx::HasProperty(*model, "PostRotation")) {
                    double post[3] = {0.0, 0.0, 0.0};
                    Fbx::PropertyVec3(*model, "PostRotation", post);
                    const Quatd q = EulerToQuaternion(post[0], post[1], post[2], order);
                    rotation = rotation * Quatd(-q.x, -q.y, -q.z, q.w);
                }
                sampler.Values.push_back(static_cast<float>(rotation.x));
                sampler.Values.push_back(static_cast<float>(rotation.y));
                sampler.Values.push_back(static_cast<float>(rotation.z));
                sampler.Values.push_back(static_cast<float>(rotation.w));
            } else {
                sampler.Values.push_back(static_cast<float>(x));
                sampler.Values.push_back(static_cast<float>(y));
                sampler.Values.push_back(static_cast<float>(z));
            }
        }

        AnimationChannel channel;
        channel.TargetNode = node->second;
        channel.Path = path;
        channel.Sampler = static_cast<int>(animation.Samplers.size());
        animation.Samplers.push_back(std::move(sampler));
        animation.Channels.push_back(channel);
    }

    // ----- finishing -----

    void Finish() {
        for (const auto& entry : objects_) {
            if (entry.second->Name == "Deformer") { WarnAboutSkinning(); break; }
        }
        for (const auto& entry : objects_) {
            if (entry.second->Name == "Geometry" && entry.second->TextAt(2) == "Shape") {
                WarnOnce("blendshape", "FBX: blend shapes are not read; the mesh arrives at its "
                                       "base shape");
                break;
            }
        }
        if (sawBones_)
            document_->Metadata["fbx.skeleton"] = "the file carries LimbNode models, read as "
                                                  "ordinary nodes";

        if (options_.GenerateMissingNormals) {
            for (auto& mesh : document_->Meshes)
                for (auto& prim : mesh.Primitives)
                    if (prim.Normals.empty() && prim.Mode != PrimitiveMode::Lines &&
                        prim.Mode != PrimitiveMode::Points)
                        prim.RecomputeNormals();
        }
        for (auto& mesh : document_->Meshes)
            for (auto& prim : mesh.Primitives)
                if (!prim.Normals.empty() && prim.Normals.size() != prim.Positions.size())
                    prim.Normals.resize(prim.Positions.size(), Vec3f(0.0f, 0.0f, 1.0f));

        if (options_.WeldTolerance > 0.0) document_->WeldVertices(options_.WeldTolerance);
        if (options_.TriangulateOnImport) document_->TriangulateAll();
        if (options_.ForceUpAxis.has_value()) document_->ConvertUpAxis(*options_.ForceUpAxis);
    }

    void WarnOnce(const std::string& key, const std::string& message) {
        if (!warned_.insert(key).second) return;
        options_.Warn(message);
    }

    const Fbx::File& file_;
    const ConversionOptions& options_;
    std::shared_ptr<ModelDocument> document_;

    std::map<int64_t, const Fbx::Node*> objects_;
    std::map<int64_t, std::vector<int64_t>> childrenOf_;
    std::map<int64_t, std::vector<std::pair<int64_t, std::string>>> propertyLinks_;
    std::map<int64_t, int> materialIndex_;
    std::map<int64_t, int> imageIndex_;
    std::map<std::string, int> meshCache_;
    std::map<int64_t, int> nodeIndexById_;
    std::set<int64_t> visited_;
    std::vector<int> meshNodes_;
    std::set<std::string> warned_;
    bool sawBones_ = false;
};

std::shared_ptr<ModelDocument> ReadDocument(const std::vector<uint8_t>& data,
                                            const ConversionOptions& options) {
    Fbx::File file;
    std::string error;
    auto warn = [&options](const std::string& message) { options.Warn(message); };
    if (!Fbx::Parse(data, file, error, warn)) {
        options.Warn(error);
        return nullptr;
    }
    Reader reader(file, options);
    return reader.Run();
}

} // namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities FbxConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.Meshes = true;
    caps.NGons = true;              // PolygonVertexIndex states each polygon's own length
    caps.SceneGraph = true;
    caps.Instancing = true;         // one Geometry connected to several Models
    caps.Materials = true;
    caps.Textures = true;
    caps.TextureCoordinates = true;
    caps.VertexColors = true;
    caps.Normals = true;
    caps.Animations = true;         // AnimationStack / Layer / CurveNode / Curve
    caps.Units = true;              // GlobalSettings UnitScaleFactor
    caps.UpAxis = true;             // GlobalSettings UpAxis
    caps.Metadata = true;
    caps.DoublePrecision = true;    // Vertices is a double array
    // Deliberately false, each for a stated reason:
    //   Skinning, MorphTargets - Deformer clusters and blend shapes are
    //     reported rather than read.
    //   EmbeddedTextures - a Video object's Content blob is not decoded.
    //   Tangents - LayerElementTangent is not read.
    //   PBRMaterials - FBX's own material is fixed-function; the PBR one is a
    //     vendor extension this reader does not interpret.
    //   Cameras, Lights - the format has them; this reader does not read them.
    return caps;
}

std::shared_ptr<ModelStorage::ModelDocument> FbxConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        options.Warn("FBX: cannot open " + filename);
        return nullptr;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    auto document = ImportFromMemory(data, options);
    if (document && document->Title.empty()) {
        const size_t slash = filename.find_last_of("/\\");
        std::string stem = slash == std::string::npos ? filename : filename.substr(slash + 1);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos && dot > 0) stem.erase(dot);
        document->Title = stem;
    }
    return document;
}

std::shared_ptr<ModelStorage::ModelDocument> FbxConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    return ReadDocument(data, options);
}

std::shared_ptr<ModelStorage::ModelDocument> FbxConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    return ImportFromMemory(data, options);
}

bool FbxConverter::ValidateData(const std::vector<uint8_t>& data) const {
    const size_t limit = std::min<size_t>(data.size(), 64);
    const std::string head(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(limit));
    return Fbx::LooksLikeFbxBinary(head);
}

bool FbxConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    char head[64] = {};
    file.read(head, 64);
    return Fbx::LooksLikeFbxBinary(
            std::string(head, static_cast<size_t>(std::max<std::streamsize>(0, file.gcount()))));
}

} // namespace ModelConverter
} // namespace UltraCanvas
