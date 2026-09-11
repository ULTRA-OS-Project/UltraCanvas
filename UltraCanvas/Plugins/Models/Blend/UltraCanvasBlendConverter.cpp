// Plugins/Models/Blend/UltraCanvasBlendConverter.cpp
// Blender's object model on top of the .blend container.
//
// See UltraCanvasBlendConverter.h for what this reads and, just as important,
// what it does not.
//
// Version: 2.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/Blend/UltraCanvasBlendConverter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;

namespace {

// Blender's Object.type. Only meshes carry geometry this reader can use; the
// rest become empty nodes so the hierarchy and the placement survive.
constexpr int kObjectMesh = 1;

// A datablock name minus the two-character type prefix Blender puts on it:
// "OBCube.021" -> "Cube.021".
std::string DatablockName(const Blend::Ref& block) {
    const std::string raw = block.Inner("id").Text("name");
    return raw.size() > 2 ? raw.substr(2) : raw;
}

// Blender writes a 4x4 as float m[4][4] with m[3] holding the translation, so
// element 12 is the x translation - which is exactly the layout Matrix4x4
// uses for column vectors. The sixteen numbers copy straight across; there is
// no transpose here and adding one would move every object in the file.
Matrix4x4 MatrixFrom(const Blend::Ref& object, const std::string& field) {
    double values[16] = {0};
    Matrix4x4 matrix = Matrix4x4::Identity();
    if (object.Reals(field, values, 16) != 16) return matrix;
    for (int i = 0; i < 16; ++i) matrix.m[i] = values[i];
    return matrix;
}

struct Layer {
    std::string Name;
    const Blend::Block* Data = nullptr;
};

} // namespace

// ===== READER =====

class BlendReader {
public:
    explicit BlendReader(const ConversionOptions& options) : options_(options) {}

    std::shared_ptr<ModelDocument> Run(Blend::File& file) {
        file_ = &file;
        document_ = std::make_shared<ModelDocument>();
        document_->SourceFormat = "blend";
        document_->Generator = "Blender " + PrettyVersion();
        document_->Metadata["blend.version"] = PrettyVersion();
        document_->Metadata["blend.pointerSize"] = std::to_string(file.PointerSize);
        document_->Metadata["blend.compression"] = file.Compression;

        // Blender's world is Z-up, right-handed, and its scene unit is metres
        // scaled by Scene.unit.scale_length.
        document_->Up = UpAxis::ZUp;
        document_->Chirality = Handedness::RightHanded;
        document_->SourceUnit = ModelUnit::Meter;
        document_->UnitScaleToMeters = SceneUnitScale();
        ReadSceneName();

        IndexObjects();
        CollectModifiers();

        for (const Blend::Block* block : objectBlocks_) {
            const Blend::Ref object = file_->At(*block, 0);
            if (!object) continue;
            if (object.Pointer("parent") == 0) ReadObject(*block, -1);
        }
        // An object whose parent is not in the file - a linked library that was
        // not packed - would otherwise never be walked. It still has a world
        // matrix, so it is placed at the root rather than dropped.
        for (const Blend::Block* block : objectBlocks_) {
            if (visited_.count(block->OldAddress)) continue;
            ReadObject(*block, -1);
        }

        ReportModifiers();

        if (document_->Meshes.empty() && document_->Nodes.empty()) {
            options_.Warn("Blender: the file holds no objects");
            return nullptr;
        }
        Finish();
        return document_;
    }

private:
    std::string PrettyVersion() const {
        std::string version = file_->Version;
        if (version.size() == 3) version.insert(1, ".");
        return version;
    }

    // Scene.unit.scale_length, which is how a Blender file states that one
    // unit is not one metre. Absent in old builds, where it is 1.
    double SceneUnitScale() const {
        for (const Blend::Block* block : file_->BlocksOfCode("SC")) {
            const Blend::Ref scene = file_->At(*block, 0);
            if (!scene) continue;
            const Blend::Ref unit = scene.Inner("unit");
            if (!unit) continue;
            const double scale = unit.Real("scale_length", 0.0);
            if (scale > 0.0) return scale;
        }
        return 1.0;
    }

    void ReadSceneName() {
        const std::vector<const Blend::Block*> scenes = file_->BlocksOfCode("SC");
        if (scenes.empty()) return;
        const Blend::Ref scene = file_->At(*scenes.front(), 0);
        if (scene) document_->Title = DatablockName(scene);
    }

    void IndexObjects() {
        objectBlocks_ = file_->BlocksOfCode("OB");
        for (const Blend::Block* block : objectBlocks_) {
            const Blend::Ref object = file_->At(*block, 0);
            if (!object) continue;
            childrenOf_[object.Pointer("parent")].push_back(block);
        }
    }

    // Every modifier in the file, by the struct that names it. This is the
    // whole reason the geometry needs a caveat, so it is gathered before
    // anything is read rather than discovered halfway through.
    void CollectModifiers() {
        static const std::string suffix = "ModifierData";
        for (const Blend::Block& block : file_->Blocks) {
            const Blend::Struct* type = file_->StructAt(block.StructIndex);
            if (!type || type->Name.size() <= suffix.size()) continue;
            if (type->Name.compare(type->Name.size() - suffix.size(), suffix.size(), suffix) != 0)
                continue;
            modifiers_.insert(type->Name.substr(0, type->Name.size() - suffix.size()));
        }
    }

    void ReportModifiers() {
        if (modifiers_.empty()) return;

        std::string names;
        size_t i = 0;
        for (const std::string& name : modifiers_) {
            if (i) names += (i + 1 == modifiers_.size() ? " and " : ", ");
            names += name;
            ++i;
        }
        document_->Metadata["blend.unappliedModifiers"] = names;
        // Not a failure and not a silence: the geometry that was read is real,
        // it is simply the cage rather than the evaluated surface. A caller
        // told which modifiers are missing can decide what to do about it.
        options_.Warn("Blender: this file stores " + names +
                      " unapplied, so what is read is the cage the artist modelled, not the "
                      "evaluated model. A .blend never holds the evaluated mesh; export to "
                      "OBJ, glTF or FBX with modifiers applied for that");
    }

    // ===== OBJECTS =====

    void ReadObject(const Blend::Block& block, int parent) {
        if (!visited_.insert(block.OldAddress).second) return;   // a parent cycle
        const Blend::Ref object = file_->At(block, 0);
        if (!object) return;

        ModelNode node;
        node.Name = DatablockName(object);
        if (node.Name.empty()) node.Name = "Object";

        const Matrix4x4 world = MatrixFrom(object, "obmat");
        // obmat is the *world* matrix Blender last evaluated. The document
        // stores a local transform per node, so the parent's is divided out.
        // Deriving it from loc/rot/size instead would have to reproduce
        // Blender's parenting rules - parentinv, bone parents, vertex parents -
        // and get every one of them right; dividing uses the answer Blender
        // already computed.
        Matrix4x4 local = world;
        if (parent >= 0) {
            Matrix4x4 inverse;
            if (parentWorld_.count(parent) && parentWorld_[parent].InverseAffine(inverse))
                local = inverse * world;
            else
                WarnOnce("singular", "Blender: an object's parent has a singular transform; "
                                     "the child is placed in world space instead");
        }

        Vec3d translation, scale;
        Quatd rotation;
        if (local.DecomposeTRS(translation, rotation, scale)) {
            node.Translation = translation;
            node.Rotation = rotation;
            node.Scale = scale;
        } else if (!local.IsIdentity(1e-15)) {
            // Shear, which no TRS triple can hold. Blender produces it from a
            // non-uniformly scaled parent rotating a child.
            node.Matrix = local;
        }

        const int index = document_->AddNode(std::move(node), parent);
        parentWorld_[index] = world;

        if (object.Int("type", 0) == kObjectMesh) AttachMesh(object, index);

        for (const Blend::Block* child : childrenOf_[block.OldAddress])
            ReadObject(*child, index);
    }

    void AttachMesh(const Blend::Ref& object, int nodeIndex) {
        const Blend::Block* dataBlock = object.PointedBlock("data");
        if (!dataBlock) return;
        const Blend::Struct* type = file_->StructAt(dataBlock->StructIndex);
        if (!type || type->Name != "Mesh") return;

        const Blend::Ref mesh = file_->At(*dataBlock, 0);
        if (!mesh) return;

        // One mesh datablock used by several objects is Blender's own way of
        // instancing, and falls straight onto a shared mesh index.
        auto cached = meshByAddress_.find(dataBlock->OldAddress);
        if (cached != meshByAddress_.end()) {
            document_->Nodes[static_cast<size_t>(nodeIndex)].Mesh = cached->second;
            return;
        }

        ModelMesh built;
        built.Name = DatablockName(mesh);
        MeshPrimitive prim;
        prim.Name = built.Name;
        if (!ReadGeometry(mesh, prim)) return;

        const std::vector<int> materials = ReadMaterials(mesh);
        SplitByMaterial(prim, materials, built);
        if (built.Primitives.empty()) return;

        const int meshIndex = document_->AddMesh(std::move(built));
        meshByAddress_[dataBlock->OldAddress] = meshIndex;
        document_->Nodes[static_cast<size_t>(nodeIndex)].Mesh = meshIndex;
    }

    // ===== GEOMETRY =====

    // Blender has written a mesh two ways. Through 3.x, MVert/MPoly/MLoop are
    // structs reached by pointer from the Mesh. From 3.6 onwards the same data
    // is generic attribute layers inside CustomData, addressed by name. Both
    // describe the identical thing - a corner list with polygon spans - so both
    // fill the same three arrays here and everything after this is shared.
    bool ReadGeometry(const Blend::Ref& mesh, MeshPrimitive& prim) {
        const size_t vertexCount = static_cast<size_t>(std::max<int64_t>(
                0, mesh.Int("totvert", 0)));
        const size_t loopCount = static_cast<size_t>(std::max<int64_t>(
                0, mesh.Int("totloop", mesh.Int("corners_num", 0))));
        const size_t polyCount = static_cast<size_t>(std::max<int64_t>(
                0, mesh.Int("totpoly", mesh.Int("faces_num", 0))));
        if (vertexCount == 0 || loopCount == 0 || polyCount == 0) {
            WarnOnce("emptymesh", "Blender: a mesh datablock states no geometry and is skipped");
            return false;
        }

        std::vector<Vec3d> points;
        std::vector<uint32_t> cornerVertex;
        std::vector<uint32_t> polyStart, polySize;
        std::vector<int> polyMaterial;

        if (!ReadPositions(mesh, vertexCount, points) ||
            !ReadCorners(mesh, loopCount, cornerVertex) ||
            !ReadPolygons(mesh, polyCount, loopCount, polyStart, polySize, polyMaterial))
            return false;

        // A corner is (vertex, uv, colour). Blender indexes UVs and colours by
        // *corner* while positions are indexed by vertex, so corners whose
        // streams differ become distinct document vertices - the same
        // resolution the OBJ, COLLADA, X3D and FBX readers perform.
        std::vector<Vec2d> uv;
        std::vector<Vec4f> colour;
        const bool haveUv = ReadCornerUVs(mesh, loopCount, uv);
        const bool haveColour = ReadCornerColours(mesh, loopCount, colour);

        std::map<std::tuple<uint32_t, uint64_t, uint32_t>, uint32_t> unique;
        std::vector<std::pair<float, float>> uvOut;
        std::vector<Vec4f> colourOut;

        prim.Mode = PrimitiveMode::Polygons;
        prim.FaceStarts.reserve(polyCount + 1);
        prim.Indices.reserve(loopCount);
        materialPerFace_.clear();
        materialPerFace_.reserve(polyCount);

        for (size_t face = 0; face < polyCount; ++face) {
            const size_t start = polyStart[face];
            const size_t size = polySize[face];
            if (size < 3 || start + size > loopCount) continue;

            prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));
            materialPerFace_.push_back(polyMaterial[face]);

            for (size_t c = 0; c < size; ++c) {
                const size_t corner = start + c;
                const uint32_t vertex = cornerVertex[corner];
                if (vertex >= points.size()) {
                    prim.Indices.push_back(0);
                    continue;
                }
                uint64_t uvKey = 0;
                if (haveUv) {
                    // The two floats, bit for bit: two corners share a vertex
                    // only when they name the same UV, and rounding here would
                    // weld corners the file kept apart.
                    const float u = static_cast<float>(uv[corner].x);
                    const float v = static_cast<float>(uv[corner].y);
                    uint32_t a = 0, b = 0;
                    std::memcpy(&a, &u, 4);
                    std::memcpy(&b, &v, 4);
                    uvKey = (static_cast<uint64_t>(a) << 32) | b;
                }
                uint32_t colourKey = 0;
                if (haveColour) {
                    const Vec4f& rgba = colour[corner];
                    colourKey = (static_cast<uint32_t>(rgba.x * 255.0f) << 24) |
                                (static_cast<uint32_t>(rgba.y * 255.0f) << 16) |
                                (static_cast<uint32_t>(rgba.z * 255.0f) << 8) |
                                static_cast<uint32_t>(rgba.w * 255.0f);
                }

                const auto key = std::make_tuple(vertex, uvKey, colourKey);
                auto found = unique.find(key);
                if (found == unique.end()) {
                    const uint32_t index = static_cast<uint32_t>(prim.Positions.size());
                    prim.Positions.push_back(points[vertex]);
                    if (haveUv)
                        uvOut.emplace_back(static_cast<float>(uv[corner].x),
                                           static_cast<float>(uv[corner].y));
                    if (haveColour) colourOut.push_back(colour[corner]);
                    found = unique.emplace(key, index).first;
                }
                prim.Indices.push_back(found->second);
            }
        }
        if (prim.Indices.empty()) return false;
        prim.FaceStarts.push_back(static_cast<uint32_t>(prim.Indices.size()));

        if (haveUv) {
            VertexAttribute attribute;
            attribute.Semantic = AttributeSemantic::TexCoord;
            attribute.Name = "TEXCOORD_0";
            attribute.Components = 2;
            attribute.Values.reserve(uvOut.size() * 2);
            for (const auto& value : uvOut) {
                attribute.Values.push_back(value.first);
                attribute.Values.push_back(value.second);
            }
            prim.Attributes.push_back(std::move(attribute));
        }
        if (haveColour) {
            VertexAttribute attribute;
            attribute.Semantic = AttributeSemantic::Color;
            attribute.Name = "COLOR_0";
            attribute.Components = 4;
            attribute.Values.reserve(colourOut.size() * 4);
            for (const Vec4f& value : colourOut) {
                attribute.Values.push_back(value.x);
                attribute.Values.push_back(value.y);
                attribute.Values.push_back(value.z);
                attribute.Values.push_back(value.w);
            }
            prim.Attributes.push_back(std::move(attribute));
        }

        // Blender stores a vertex normal as three shorts, and only as a cache
        // it recomputes on load; from 3.x it often stores none at all. Both are
        // reasons to generate rather than read: a generated normal is correct
        // for the geometry actually present, and a stale cached one is not.
        prim.RecomputeNormals();
        return true;
    }

    bool ReadPositions(const Blend::Ref& mesh, size_t count, std::vector<Vec3d>& out) {
        // Through 3.x: an MVert array, whose `co` is the position.
        if (const Blend::Block* block = mesh.PointedBlock("mvert")) {
            out.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const Blend::Ref vertex = file_->At(*block, i);
                if (!vertex) break;
                double co[3] = {0, 0, 0};
                vertex.Reals("co", co, 3);
                out.emplace_back(co[0], co[1], co[2]);
            }
            if (out.size() == count) return true;
        }
        // 3.6 and later: a "position" layer of float[3] on the vertex domain.
        if (ReadVectorLayer(mesh, "vdata", {"position", "vert_positions"}, count, 3, out))
            return true;

        WarnOnce("nopositions", "Blender: a mesh states " + std::to_string(count) +
                                        " vertices but stores neither an MVert array nor a "
                                        "'position' attribute layer; it is skipped");
        return false;
    }

    bool ReadCorners(const Blend::Ref& mesh, size_t count, std::vector<uint32_t>& out) {
        // Through 3.x: an MLoop array, whose `v` is the vertex.
        if (const Blend::Block* block = mesh.PointedBlock("mloop")) {
            out.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const Blend::Ref loop = file_->At(*block, i);
                if (!loop) break;
                out.push_back(static_cast<uint32_t>(std::max<int64_t>(0, loop.Int("v", 0))));
            }
            if (out.size() == count) return true;
            out.clear();
        }
        // 3.6 and later: a ".corner_vert" layer of int on the corner domain.
        std::vector<int64_t> values;
        if (ReadIntLayer(mesh, "ldata", {".corner_vert"}, count, values)) {
            out.reserve(count);
            for (int64_t value : values)
                out.push_back(static_cast<uint32_t>(std::max<int64_t>(0, value)));
            return true;
        }

        WarnOnce("nocorners", "Blender: a mesh states corners but stores neither an MLoop array "
                              "nor a '.corner_vert' attribute layer; it is skipped");
        return false;
    }

    bool ReadPolygons(const Blend::Ref& mesh, size_t count, size_t loopCount,
                      std::vector<uint32_t>& start, std::vector<uint32_t>& size,
                      std::vector<int>& material) {
        // Through 3.x: an MPoly array of (loopstart, totloop, mat_nr).
        if (const Blend::Block* block = mesh.PointedBlock("mpoly")) {
            start.reserve(count);
            size.reserve(count);
            material.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const Blend::Ref poly = file_->At(*block, i);
                if (!poly) break;
                start.push_back(static_cast<uint32_t>(std::max<int64_t>(
                        0, poly.Int("loopstart", 0))));
                size.push_back(static_cast<uint32_t>(std::max<int64_t>(
                        0, poly.Int("totloop", 0))));
                material.push_back(static_cast<int>(poly.Int("mat_nr", 0)));
            }
            if (start.size() == count) return true;
            start.clear();
            size.clear();
            material.clear();
        }

        // 4.0 and later: one offsets array of count + 1 entries, so face i runs
        // [offset[i], offset[i + 1]). The field was renamed between 4.0 and
        // 4.1, which is exactly the kind of move reading the SDNA by name
        // absorbs.
        const Blend::Block* offsets = mesh.PointedBlock("poly_offset_indices");
        if (!offsets) offsets = mesh.PointedBlock("face_offset_indices");
        if (offsets) {
            // A raw array like this carries no useful SDNA index - Blender
            // writes 0 for any block that is not a struct - so the element
            // width comes from the bytes present over the count declared,
            // never from the block's nominal type.
            const size_t stride = offsets->BodySize / (count + 1) >= 8 ? 8u : 4u;
            const std::string element = stride == 8 ? "int64_t" : "int";
            std::vector<uint32_t> bounds;
            bounds.reserve(count + 1);
            for (size_t i = 0; i <= count; ++i) {
                const size_t at = offsets->BodyOffset + i * stride;
                if (at + stride > file_->Bytes.size()) break;
                bounds.push_back(static_cast<uint32_t>(std::max<int64_t>(
                        0, file_->ReadInteger(at, element))));
            }
            if (bounds.size() == count + 1) {
                start.reserve(count);
                size.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    start.push_back(bounds[i]);
                    size.push_back(bounds[i + 1] >= bounds[i] ? bounds[i + 1] - bounds[i] : 0);
                }
                material.assign(count, 0);
                std::vector<int64_t> indices;
                if (ReadIntLayer(mesh, "pdata", {"material_index"}, count, indices))
                    for (size_t i = 0; i < count; ++i)
                        material[i] = static_cast<int>(indices[i]);
                return true;
            }
        }

        (void)loopCount;
        WarnOnce("nopolygons", "Blender: a mesh states polygons but stores neither an MPoly "
                               "array nor a face offsets array; it is skipped");
        return false;
    }

    bool ReadCornerUVs(const Blend::Ref& mesh, size_t count, std::vector<Vec2d>& out) {
        // Through 3.x: an MLoopUV array whose `uv` is the pair.
        if (const Blend::Block* block = mesh.PointedBlock("mloopuv")) {
            out.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const Blend::Ref loop = file_->At(*block, i);
                if (!loop) break;
                double uv[2] = {0, 0};
                loop.Reals("uv", uv, 2);
                out.emplace_back(uv[0], uv[1]);
            }
            if (out.size() == count) return true;
            out.clear();
        }
        // 3.6 and later: a float2 layer on the corner domain, named by the
        // artist rather than fixed. The reserved layers all begin with a dot,
        // so the first named corner layer holding exactly two floats per corner
        // is the UV map. Blender's own active-layer flag would be better, and
        // this reader takes the first rather than pretending to know which.
        for (const Layer& layer : LayersOf(mesh, "ldata", "")) {
            if (!layer.Name.empty() && layer.Name[0] == '.') continue;
            if (layer.Data->BodySize < count * 8) continue;
            out.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const size_t at = layer.Data->BodyOffset + i * 8;
                out.emplace_back(file_->ReadNumber(at, "float"),
                                 file_->ReadNumber(at + 4, "float"));
            }
            if (LayersOf(mesh, "ldata", "").size() > 1)
                WarnOnce("uvpick", "Blender: this mesh has several corner attribute layers and "
                                   "the file does not say which is the active UV map; the first "
                                   "named one is used");
            return true;
        }
        return false;
    }

    bool ReadCornerColours(const Blend::Ref& mesh, size_t count, std::vector<Vec4f>& out) {
        // MLoopCol is four bytes, and they are *sRGB* - Blender's own byte
        // colour attribute. The document holds linear floats, so the transfer
        // function has to be undone rather than the byte simply divided by 255.
        if (const Blend::Block* block = mesh.PointedBlock("mloopcol")) {
            out.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const Blend::Ref loop = file_->At(*block, i);
                if (!loop) break;
                out.push_back(Vec4f(SrgbToLinear(loop.Int("r", 255)),
                                    SrgbToLinear(loop.Int("g", 255)),
                                    SrgbToLinear(loop.Int("b", 255)),
                                    static_cast<float>(loop.Int("a", 255)) / 255.0f));
            }
            if (out.size() == count) return true;
            out.clear();
        }
        return false;
    }

    static float SrgbToLinear(int64_t byteValue) {
        const double value = std::min<double>(255.0, std::max<double>(0.0,
                                                                      static_cast<double>(byteValue))) /
                             255.0;
        return static_cast<float>(value <= 0.04045 ? value / 12.92
                                                   : std::pow((value + 0.055) / 1.055, 2.4));
    }

    // ===== CUSTOMDATA LAYERS =====
    //
    // From 3.6 Blender keeps mesh attributes in CustomData: a `layers` array of
    // CustomDataLayer, each with a name, a type and a pointer to its values.
    // Layers are found by *name* rather than by CD_* type number, because those
    // numbers have been reused across releases while the names - "position",
    // ".corner_vert", "material_index" - have not. The values a layer points at
    // are a raw array with no SDNA struct of its own, so their width comes from
    // the bytes present over the count the mesh declares.

    std::vector<Layer> LayersOf(const Blend::Ref& mesh, const std::string& domain,
                                const std::string& typeName) const {
        // `typeName` filters by the data block's SDNA struct when the layer
        // holds one. A layer of plain floats or ints has no struct at all -
        // Blender writes index 0 for raw data - so callers wanting those pass
        // an empty name and check the byte count themselves.
        std::vector<Layer> found;
        const Blend::Ref data = mesh.Inner(domain);
        if (!data) return found;
        const Blend::Block* block = data.PointedBlock("layers");
        if (!block) return found;

        const size_t total = static_cast<size_t>(std::max<int64_t>(0, data.Int("totlayer", 0)));
        for (size_t i = 0; i < total; ++i) {
            const Blend::Ref layer = file_->At(*block, i);
            if (!layer) break;
            const Blend::Block* values = layer.PointedBlock("data");
            if (!values) continue;
            if (!typeName.empty()) {
                const Blend::Struct* type = file_->StructAt(values->StructIndex);
                if (!type || type->Name != typeName) continue;
            }
            found.push_back(Layer{layer.Text("name"), values});
        }
        return found;
    }

    bool ReadVectorLayer(const Blend::Ref& mesh, const std::string& domain,
                         const std::vector<std::string>& names, size_t count, int components,
                         std::vector<Vec3d>& out) {
        for (const Layer& layer : LayersOf(mesh, domain, "")) {
            if (!names.empty() &&
                std::find(names.begin(), names.end(), layer.Name) == names.end())
                continue;
            const size_t stride = static_cast<size_t>(components) * 4;
            if (layer.Data->BodySize < count * stride) continue;
            out.clear();
            out.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const size_t at = layer.Data->BodyOffset + i * stride;
                const double x = file_->ReadNumber(at, "float");
                const double y = components > 1 ? file_->ReadNumber(at + 4, "float") : 0.0;
                const double z = components > 2 ? file_->ReadNumber(at + 8, "float") : 0.0;
                out.emplace_back(x, y, z);
            }
            return true;
        }
        return false;
    }

    bool ReadIntLayer(const Blend::Ref& mesh, const std::string& domain,
                      const std::vector<std::string>& names, size_t count,
                      std::vector<int64_t>& out) {
        for (const Layer& layer : LayersOf(mesh, domain, "")) {
            if (!names.empty() &&
                std::find(names.begin(), names.end(), layer.Name) == names.end())
                continue;
            if (layer.Data->BodySize < count * 4) continue;
            out.clear();
            out.reserve(count);
            for (size_t i = 0; i < count; ++i)
                out.push_back(file_->ReadInteger(layer.Data->BodyOffset + i * 4, "int"));
            return true;
        }
        return false;
    }

    // ===== MATERIALS =====

    std::vector<int> ReadMaterials(const Blend::Ref& mesh) {
        std::vector<int> indices;
        const Blend::Block* slots = mesh.PointedBlock("mat");
        const size_t count = static_cast<size_t>(std::max<int64_t>(0, mesh.Int("totcol", 0)));
        if (!slots || count == 0) return indices;

        // `Material **mat` is an array of pointers, one per slot, and a slot
        // may legitimately be empty.
        for (size_t i = 0; i < count; ++i) {
            const size_t at = slots->BodyOffset + i * static_cast<size_t>(file_->PointerSize);
            if (at + static_cast<size_t>(file_->PointerSize) > file_->Bytes.size()) break;
            const uint64_t address = file_->ReadPointer(at);
            indices.push_back(address ? MaterialIndexOf(address) : -1);
        }
        return indices;
    }

    int MaterialIndexOf(uint64_t address) {
        auto cached = materialByAddress_.find(address);
        if (cached != materialByAddress_.end()) return cached->second;

        const Blend::Block* block = file_->BlockAt(address);
        if (!block) return -1;
        const Blend::Ref source = file_->At(*block, 0);
        if (!source) return -1;

        ModelMaterial material;
        material.Name = DatablockName(source);

        // Blender's own material has been two different things. Through 2.79 it
        // is the fixed-function one these fields describe; from 2.80 the
        // surface is a node tree, and r/g/b plus metallic/roughness are the
        // viewport approximation of it. Both are read here for what they are -
        // the node tree is not evaluated, and saying so beats inventing a PBR
        // material out of fields that do not mean that.
        const double alpha = source.Has("alpha") ? source.Real("alpha", 1.0) : 1.0;
        material.BaseColorFactor = Vec4f(static_cast<float>(source.Real("r", 0.8)),
                                         static_cast<float>(source.Real("g", 0.8)),
                                         static_cast<float>(source.Real("b", 0.8)),
                                         static_cast<float>(alpha));
        if (alpha < 0.999) material.Alpha = AlphaMode::Blend;

        if (source.Has("metallic") || source.Has("roughness")) {
            material.MetallicFactor = static_cast<float>(source.Real("metallic", 0.0));
            material.RoughnessFactor = static_cast<float>(source.Real("roughness", 0.5));
            sawNodeMaterial_ = true;
        }

        PhongParams phong;
        phong.Diffuse = Vec3f(material.BaseColorFactor.x, material.BaseColorFactor.y,
                              material.BaseColorFactor.z);
        // Blender states a specular colour and an intensity separately, and
        // means their product - the same colour-times-factor rule FBX uses.
        const double specular = source.Real("spec", 0.5);
        phong.Specular = Vec3f(static_cast<float>(source.Real("specr", 1.0) * specular),
                               static_cast<float>(source.Real("specg", 1.0) * specular),
                               static_cast<float>(source.Real("specb", 1.0) * specular));
        // `har` is Blender's hardness, 1..511, which is the Phong exponent.
        phong.Shininess = static_cast<float>(source.Real("har", 50.0));
        phong.Ambient = Vec3f(static_cast<float>(source.Real("ambr", 0.0)),
                              static_cast<float>(source.Real("ambg", 0.0)),
                              static_cast<float>(source.Real("ambb", 0.0)));
        material.Phong = phong;

        // `emit` scales the material's own colour, which is what Blender's
        // fixed-function emission meant.
        const double emit = source.Real("emit", 0.0);
        if (emit > 0.0)
            material.EmissiveFactor = Vec3f(
                    static_cast<float>(material.BaseColorFactor.x * emit),
                    static_cast<float>(material.BaseColorFactor.y * emit),
                    static_cast<float>(material.BaseColorFactor.z * emit));
        material.DeriveMissingModel();

        const int index = document_->AddMaterial(std::move(material));
        materialByAddress_[address] = index;
        return index;
    }

    // One primitive per material used, because the document binds a material to
    // a primitive rather than to a face. A mesh whose faces all share one slot -
    // the common case - stays one primitive.
    void SplitByMaterial(MeshPrimitive& prim, const std::vector<int>& materials,
                         ModelMesh& out) {
        std::set<int> used;
        for (int slot : materialPerFace_) used.insert(slot);

        auto resolve = [&materials](int slot) {
            return slot >= 0 && static_cast<size_t>(slot) < materials.size()
                           ? materials[static_cast<size_t>(slot)]
                           : -1;
        };

        if (used.size() <= 1) {
            prim.Material = resolve(materialPerFace_.empty() ? 0 : *used.begin());
            out.Primitives.push_back(std::move(prim));
            return;
        }

        for (int slot : used) {
            MeshPrimitive part;
            part.Name = prim.Name;
            part.Mode = PrimitiveMode::Polygons;
            part.Positions = prim.Positions;
            part.Normals = prim.Normals;
            part.Attributes = prim.Attributes;
            part.Material = resolve(slot);

            for (size_t face = 0; face + 1 < prim.FaceStarts.size(); ++face) {
                if (materialPerFace_[face] != slot) continue;
                part.FaceStarts.push_back(static_cast<uint32_t>(part.Indices.size()));
                for (uint32_t i = prim.FaceStarts[face]; i < prim.FaceStarts[face + 1]; ++i)
                    part.Indices.push_back(prim.Indices[i]);
            }
            if (part.Indices.empty()) continue;
            part.FaceStarts.push_back(static_cast<uint32_t>(part.Indices.size()));
            out.Primitives.push_back(std::move(part));
        }
    }

    void Finish() {
        if (sawNodeMaterial_)
            options_.Warn("Blender: a material's surface is a node tree, which is not evaluated; "
                          "its colour is the viewport approximation Blender stores beside it");
    }

    void WarnOnce(const std::string& key, const std::string& message) {
        if (warned_.insert(key).second) options_.Warn(message);
    }

    const ConversionOptions& options_;
    Blend::File* file_ = nullptr;
    std::shared_ptr<ModelDocument> document_;

    std::vector<const Blend::Block*> objectBlocks_;
    std::map<uint64_t, std::vector<const Blend::Block*>> childrenOf_;
    std::map<uint64_t, int> meshByAddress_;
    std::map<uint64_t, int> materialByAddress_;
    std::map<int, Matrix4x4> parentWorld_;
    std::set<uint64_t> visited_;
    std::set<std::string> modifiers_;
    std::vector<int> materialPerFace_;
    std::set<std::string> warned_;
    bool sawNodeMaterial_ = false;
};

// ===== PUBLIC INTERFACE =====

FormatCapabilities BlendConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.Meshes = true;
    caps.NGons = true;              // MPoly states each face's own corner count
    caps.SceneGraph = true;
    caps.Instancing = true;         // one mesh datablock used by several objects
    caps.Materials = true;
    caps.TextureCoordinates = true;
    caps.VertexColors = true;
    caps.Normals = true;            // generated; Blender's stored ones are a cache
    caps.Units = true;              // Scene.unit.scale_length
    caps.UpAxis = true;             // Blender is Z-up by definition
    caps.Metadata = true;
    caps.DoublePrecision = false;   // Blender stores float
    // Deliberately false, each for a stated reason:
    //   Animations - actions, drivers and the NLA are not read.
    //   Skinning, MorphTargets - armature deform and shape keys are not read.
    //   Textures, EmbeddedTextures - an image is a node tree reference or a
    //     packed blob, and the node tree is not evaluated.
    //   PBRMaterials - the viewport colour is not a PBR surface.
    //   Cameras, Lights - the format has them; this reader does not read them.
    return caps;
}

std::shared_ptr<ModelStorage::ModelDocument> BlendConverter::ImportFromMemory(
        const std::vector<uint8_t>& data, const ConversionOptions& options) {
    Blend::File file;
    if (!Blend::Parse(data, file, [&options](const std::string& message) {
            options.Warn(message);
        }))
        return nullptr;

    BlendReader reader(options);
    return reader.Run(file);
}

std::shared_ptr<ModelStorage::ModelDocument> BlendConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        options.Warn("Blender: cannot open " + filename);
        return nullptr;
    }
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
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

std::shared_ptr<ModelStorage::ModelDocument> BlendConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                                    std::istreambuf_iterator<char>());
    return ImportFromMemory(data, options);
}

bool BlendConverter::ValidateData(const std::vector<uint8_t>& data) const {
    return LooksLikeBlendFile(data);
}

bool BlendConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> head(16);
    file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(std::max<std::streamsize>(0, file.gcount())));
    return LooksLikeBlendFile(head);
}

BlendFileInfo BlendConverter::Inspect(const std::string& filename) {
    return ReadBlendFileInfo(filename);
}

} // namespace ModelConverter
} // namespace UltraCanvas
