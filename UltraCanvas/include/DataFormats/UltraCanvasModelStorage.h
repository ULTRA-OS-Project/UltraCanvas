// include/DataFormats/UltraCanvasModelStorage.h
// ModelStorage::ModelDocument — the framework's universal 3D scene structure.
//
// This is to 3D what VectorStorage::VectorDocument is to 2D: one in-memory
// model that every 3D file format reads into and writes out of, so a reader
// and a writer never have to know about each other. It is a core service, not
// a plugin's private structure — per Masterfile_modules.md, framework-wide
// facilities live in UltraCanvas/{include,core} and never inside a file-type
// plugin. (Docs/UltraCanvas/VersioningInvestigation.md §6 records why the
// earlier Plugins/Models/STL/UltraCanvas3DTypes.h could not serve as one.)
//
// Shape: flat arrays addressed by index, glTF-style, rather than the
// shared_ptr tree VectorStorage uses. 3D formats are natively reference-based
// (glTF, COLLADA, FBX and 3MF all address geometry and materials by id) and
// instancing — one mesh drawn by many nodes — is the norm rather than the
// exception, so indices are what readers already hold and what writers need
// to emit. It also keeps the document trivially copyable and serialisable.
//
// Every field below is annotated with the formats that fill it. A field no
// format fills does not belong here; see
// Docs/Research/UltraCanvas3DModelProposal.md for the survey those annotations
// come from.
//
// The document holds two kinds of geometry, not one. Meshes are below;
// BrepData (UltraCanvasBrepStorage.h) holds exact trimmed-surface bodies — what
// STEP, IGES, ACIS, Parasolid, OpenNURBS and DWG 3DSOLID/REGION/BODY actually
// contain. A node points at either. Tessellation is then a document operation
// at a tolerance the caller chooses, rather than a decision a reader makes once
// and irreversibly; the exact surfaces stay alongside the mesh they produced.
// The vector and matrix types both halves are built from moved to
// UltraCanvasModelMath.h so the B-rep header can use them without a cycle.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MODEL_STORAGE_H
#define ULTRACANVAS_MODEL_STORAGE_H

#include "DataFormats/UltraCanvasBrepStorage.h"
#include "DataFormats/UltraCanvasModelMath.h"
#include "DataFormats/UltraCanvasPolygonTriangulation.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace ModelStorage {

// ===== UNITS AND ORIENTATION =====

// The three facts a 3D file may state about the space its numbers live in.
// Getting them wrong is the most common way an imported model arrives a
// thousand times too large or lying on its side, so the document records what
// the file said rather than silently normalising.

    enum class ModelUnit {
        Unspecified,   // unitless model units, or the file does not say (STL, OBJ, PLY)
        Micrometer,
        Millimeter,
        Centimeter,
        Decimeter,
        Meter,
        Kilometer,
        Mil,           // 1/1000 in
        Inch,
        Foot,
        Yard,
        Mile
    };

    // Metres in one unit; 0 for Unspecified.
    double MetersPerUnit(ModelUnit unit);
    // Short symbol ("mm", "in", "m", ...); "" for Unspecified.
    const char* ModelUnitSymbol(ModelUnit unit);

    enum class UpAxis {
        YUp,   // glTF, FBX default, COLLADA Y_UP, USD default
        ZUp    // 3MF, AMF, DXF/DWG, COLLADA Z_UP, most CAD and CAM
    };

    enum class Handedness {
        RightHanded,   // glTF, COLLADA, OpenGL, most of the industry
        LeftHanded     // DirectX-family formats
    };

// ===== VERTEX ATTRIBUTES =====

    enum class PrimitiveMode {
        Points,          // PLY/PCD/LAS point clouds, glTF POINTS
        Lines,
        LineStrip,
        LineLoop,
        Triangles,       // the common case
        TriangleStrip,
        TriangleFan,
        Polygons         // n-gons: OBJ, PLY, OFF, 3MF. See FaceStarts.
    };

    enum class AttributeSemantic {
        TexCoord,   // OBJ vt, PLY s/t, glTF TEXCOORD_n, COLLADA TEXCOORD
        Color,      // PLY red/green/blue/alpha, glTF COLOR_n, 3MF colour groups, PCD rgb
        Joints,     // glTF JOINTS_n, COLLADA/FBX skin influences (indices into ModelSkin::Joints)
        Weights,    // glTF WEIGHTS_n, COLLADA/FBX skin weights
        Custom      // PLY properties, LAS intensity/classification, PCD fields — Name identifies it
    };

// A named, tightly packed per-vertex array. Positions, normals and tangents
// have dedicated fields on MeshPrimitive because every consumer needs them by
// name; everything else lives here so that a format with attributes the
// framework has never heard of — PLY's arbitrary property lists, LAS's
// intensity and classification, a scanner's per-point quality — round-trips
// instead of being dropped.
    struct VertexAttribute {
        AttributeSemantic Semantic = AttributeSemantic::Custom;
        std::string Name;          // source name; required for Custom, informative otherwise
        int Set = 0;               // TEXCOORD_<Set> / COLOR_<Set> / JOINTS_<Set>
        int Components = 3;        // 1..4 floats per vertex
        std::vector<float> Values; // Components entries per vertex

        size_t Count() const {
            return Components > 0 ? Values.size() / static_cast<size_t>(Components) : 0;
        }
    };

// A blend shape: deltas added to the base attributes, weighted per node.
// glTF morph targets, COLLADA morph controllers, FBX blend shapes.
    struct MorphTarget {
        std::string Name;
        std::vector<Vec3d> PositionDeltas;
        std::vector<Vec3f> NormalDeltas;    // may be empty
        std::vector<Vec3f> TangentDeltas;   // may be empty
    };

// ===== MESH =====

// One drawable batch: a vertex set, a topology and at most one material.
// A mesh whose faces use several materials becomes several primitives — the
// split every scene format and every GPU draw call requires anyway.
    struct MeshPrimitive {
        std::string Name;
        PrimitiveMode Mode = PrimitiveMode::Triangles;

        std::vector<Vec3d> Positions;              // required
        std::vector<Vec3f> Normals;                // empty = none stored
        std::vector<Vec4f> Tangents;               // xyz + handedness in w; glTF TANGENT
        std::vector<VertexAttribute> Attributes;   // texcoords, colours, skin, custom

        // Empty means the vertices are drawn in order (STL's triangle soup, a
        // point cloud). Otherwise indices into the vertex arrays.
        std::vector<uint32_t> Indices;

        // Polygons mode only: the start offset of each face in Indices, with a
        // final sentinel equal to Indices.size(), so face i spans
        // [FaceStarts[i], FaceStarts[i + 1]). Keeping the n-gons a file
        // actually contains means an OBJ or PLY quad mesh survives a round
        // trip; Triangulate() produces the triangle form on demand.
        std::vector<uint32_t> FaceStarts;

        int Material = -1;                         // index into ModelDocument::Materials
        std::vector<MorphTarget> Targets;

        // One smoothing-group bitmask per face, or empty when the source had
        // none. OBJ `s` statements and the 3DS SMOOTH_GROUP chunk are the same
        // idea under two spellings: faces that share a group smooth across
        // their shared edge, faces that share none crease. Both are 32-bit
        // masks and a face may be in several groups at once; OBJ's `s 0` and
        // `s off` are the value 0.
        //
        // These are resolved into normals at import — that is what a renderer
        // needs — but keeping the masks means an OBJ read and written back
        // still says `s 1`, and that a writer for a format with groups can
        // emit them instead of baking a normal per corner. Parallel to
        // FaceCount(), not to the vertices.
        std::vector<uint32_t> SmoothingGroups;

        size_t VertexCount() const { return Positions.size(); }
        // Faces for the current Mode: triangles, lines, points or polygons.
        size_t FaceCount() const;
        // Vertex indices of one face, resolved through Indices and Mode.
        std::vector<uint32_t> Face(size_t faceIndex) const;

        Bounds3D ComputeBounds() const;
        const VertexAttribute* FindAttribute(AttributeSemantic semantic, int set = 0) const;
        const VertexAttribute* FindAttribute(const std::string& name) const;

        // Convert any face topology to indexed Triangles in place. Strips and
        // fans expand; polygons are ear-clipped in the plane Newell's method
        // fits to them, so a concave n-gon — an L-shaped face, a letterbox
        // slot — comes out as its own area rather than its convex hull. A
        // triangle or a convex quad short-circuits to the fan, which is the
        // same answer for a fraction of the work. Points and lines are left
        // alone and return false.
        //
        // SmoothingGroups, being per-face, are expanded alongside: every
        // triangle a face produces inherits the face's mask.
        bool Triangulate();

        // Area-weighted vertex normals from the geometry. Used when a file has
        // none (OBJ without vn, PLY without nx) or ships degenerate ones (an
        // STL facet normal of (0,0,0)).
        //
        // When SmoothingGroups are present the groups are honoured rather than
        // ignored: faces that share a group average together, faces that share
        // none crease, and a vertex where the two meet is split so that both
        // normals can exist. Attributes, tangents and morph deltas follow the
        // split. Without groups every face at a vertex averages, which is the
        // right guess when the file said nothing.
        void RecomputeNormals();

    private:
        // The group-aware half of RecomputeNormals. Returns false when the
        // primitive is not in a shape it can work on, so the caller falls back.
        bool RecomputeNormalsBySmoothingGroup();
    };

// A named group of primitives — glTF mesh, OBJ o/g, PLY's single element set,
// COLLADA geometry, 3MF object.
    struct ModelMesh {
        std::string Name;
        std::vector<MeshPrimitive> Primitives;

        Bounds3D ComputeBounds() const;
        size_t TotalVertexCount() const;
        size_t TotalFaceCount() const;
    };

// ===== TEXTURES AND MATERIALS =====

    enum class TextureFilter { Nearest, Linear, NearestMipmapNearest, LinearMipmapNearest,
                               NearestMipmapLinear, LinearMipmapLinear };
    enum class TextureWrap   { Repeat, ClampToEdge, MirroredRepeat };

    struct ModelSampler {
        TextureFilter MagFilter = TextureFilter::Linear;
        TextureFilter MinFilter = TextureFilter::LinearMipmapLinear;
        TextureWrap WrapU = TextureWrap::Repeat;
        TextureWrap WrapV = TextureWrap::Repeat;
    };

// An image the model refers to: a path relative to the model file (OBJ/MTL
// map_Kd, COLLADA <init_from>, glTF uri) or bytes embedded in the container
// (GLB buffer view, 3MF package part, FBX embedded media).
    struct ModelImage {
        std::string Name;
        std::string Uri;                 // as written in the file; empty when embedded
        std::string MimeType;            // "image/png", "image/jpeg", ... when known
        std::vector<uint8_t> Data;       // embedded bytes; empty when Uri resolves
    };

    struct TextureRef {
        int Image = -1;                  // index into ModelDocument::Images; -1 = unset
        int Sampler = -1;                // index into ModelDocument::Samplers; -1 = default
        int UVSet = 0;                   // which TexCoord attribute set to sample
        float Scale = 1.0f;              // normal map scale / occlusion strength
        // KHR_texture_transform and MTL's -o/-s options.
        float OffsetU = 0.0f, OffsetV = 0.0f;
        float ScaleU = 1.0f, ScaleV = 1.0f;
        float RotationRadians = 0.0f;

        bool IsSet() const { return Image >= 0; }
    };

    enum class AlphaMode { Opaque, Mask, Blend };

// The classic fixed-function parameters. Kept beside the PBR block rather than
// converted away, because OBJ/MTL, 3DS, COLLADA <phong>/<lambert> and FBX
// materials are all stated in these terms and a converted-then-reconverted
// material comes back wrong. A reader fills whichever set its format has; a
// writer prefers its own and derives the other. DeriveMissingModel() below
// does the derivation once, in one place.
    struct PhongParams {
        Vec3f Ambient{0.0f, 0.0f, 0.0f};      // MTL Ka
        Vec3f Diffuse{0.8f, 0.8f, 0.8f};      // MTL Kd
        Vec3f Specular{0.0f, 0.0f, 0.0f};     // MTL Ks
        float Shininess = 0.0f;               // MTL Ns, 0..1000
        int IlluminationModel = 2;            // MTL illum
        TextureRef AmbientTexture;            // map_Ka
        TextureRef DiffuseTexture;            // map_Kd
        TextureRef SpecularTexture;           // map_Ks
    };

    struct ModelMaterial {
        std::string Name;

        // Physically based metallic-roughness — glTF 2.0's core model, and the
        // one USD, 3MF's material extension and every modern renderer speak.
        Vec4f BaseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        TextureRef BaseColorTexture;
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;
        TextureRef MetallicRoughnessTexture;

        TextureRef NormalTexture;
        TextureRef OcclusionTexture;
        TextureRef EmissiveTexture;
        Vec3f EmissiveFactor{0.0f, 0.0f, 0.0f};

        AlphaMode Alpha = AlphaMode::Opaque;
        float AlphaCutoff = 0.5f;
        bool DoubleSided = false;

        // Present when the source stated the material in fixed-function terms.
        std::optional<PhongParams> Phong;

        // Index of refraction (MTL Ni, COLLADA index_of_refraction,
        // KHR_materials_ior). 1.0 = none stated.
        float IndexOfRefraction = 1.0f;

        // Anything the format carried that this struct has no field for —
        // KHR_materials_* extension blocks, FBX custom properties, a CAD
        // appearance id. Kept as text so nothing is silently lost.
        std::map<std::string, std::string> Extras;

        // Fill the PBR block from Phong, or Phong from the PBR block, whichever
        // is missing, using the conversions the proposal documents. A no-op
        // when both or neither are set.
        void DeriveMissingModel();
    };

// ===== SKINNING AND ANIMATION =====

    struct ModelSkin {
        std::string Name;
        std::vector<int> Joints;                     // node indices, in influence order
        std::vector<Matrix4x4> InverseBindMatrices;  // parallel to Joints; empty = identity
        int Skeleton = -1;                           // common root node, -1 = unspecified
    };

    enum class AnimationPath { Translation, Rotation, Scale, MorphWeights };
    enum class Interpolation { Linear, Step, CubicSpline };

// Keyframes for one property. Times are seconds. Values are packed by path:
// 3 floats for Translation/Scale, 4 for Rotation (quaternion xyzw), one per
// morph target for MorphWeights — and CubicSpline stores in-tangent, value and
// out-tangent for each key, so three times that per keyframe.
    struct AnimationSampler {
        std::vector<float> Times;
        std::vector<float> Values;
        Interpolation Interpolate = Interpolation::Linear;
    };

    struct AnimationChannel {
        int TargetNode = -1;
        AnimationPath Path = AnimationPath::Translation;
        int Sampler = -1;      // index into ModelAnimation::Samplers
    };

    struct ModelAnimation {
        std::string Name;
        std::vector<AnimationChannel> Channels;
        std::vector<AnimationSampler> Samplers;

        float Duration() const;   // the largest sampler time; 0 when empty
    };

// ===== CAMERAS AND LIGHTS =====

    enum class CameraType { Perspective, Orthographic };

    struct ModelCamera {
        std::string Name;
        CameraType Type = CameraType::Perspective;
        float YFovRadians = 0.8f;     // Perspective
        float AspectRatio = 0.0f;     // 0 = use the viewport's
        float XMag = 1.0f, YMag = 1.0f;   // Orthographic
        float ZNear = 0.01f;
        float ZFar = 0.0f;            // 0 = infinite
    };

    enum class LightType { Directional, Point, Spot, Ambient };

    struct ModelLight {
        std::string Name;
        LightType Type = LightType::Point;
        Vec3f Color{1.0f, 1.0f, 1.0f};
        float Intensity = 1.0f;
        float Range = 0.0f;                  // 0 = unlimited
        float InnerConeRadians = 0.0f;       // Spot
        float OuterConeRadians = 0.7853982f; // Spot, default pi/4
    };

// ===== SCENE GRAPH =====

// A node places geometry, a camera or a light in space and may parent others.
// Transform is held as TRS when the source stated it that way (glTF, FBX,
// COLLADA <translate>/<rotate>/<scale>, 3MF components) and as a matrix when
// it did not or when the matrix carries shear that TRS cannot express.
// LocalMatrix() resolves whichever is set.
    struct ModelNode {
        std::string Name;

        Vec3d Translation{0.0, 0.0, 0.0};
        Quatd Rotation = Quatd::Identity();
        Vec3d Scale{1.0, 1.0, 1.0};
        std::optional<Matrix4x4> Matrix;   // set = use instead of TRS

        std::vector<int> Children;         // node indices
        int Parent = -1;                   // node index; -1 for a root

        int Mesh = -1;                     // index into ModelDocument::Meshes
        // Index into ModelDocument::Brep.Solids — the exact body a CAD file
        // placed here. A node may carry both: the solid is what the file said,
        // the mesh is what TessellateBreps() made of it, and keeping the pair
        // is what lets the tessellation be redone finer without re-reading.
        int Solid = -1;
        int Camera = -1;
        int Light = -1;
        int Skin = -1;
        std::vector<float> MorphWeights;   // default weights for the mesh's targets

        std::map<std::string, std::string> Extras;   // FBX properties, 3MF metadata

        Matrix4x4 LocalMatrix() const;
    };

// A named set of root nodes. Most formats have exactly one; glTF and COLLADA
// allow several (a file of variants, a library of props).
    struct ModelScene {
        std::string Name;
        std::vector<int> Roots;   // node indices
    };

// ===== DOCUMENT =====

    class ModelDocument {
    public:
        // --- asset ---
        std::string Title;
        std::string Author;
        std::string Generator;     // the application that wrote the file
        std::string Copyright;
        std::string SourceFormat;  // "stl", "gltf", ... — what it was read from
        std::map<std::string, std::string> Metadata;   // 3MF/AMF metadata, glTF extras, FBX properties

        // --- the space the numbers are in ---
        // SourceUnit is what the file declared; UnitScaleToMeters is how many
        // metres one stored unit is, so a consumer can recover physical size
        // without guessing. Readers do not rescale geometry — a millimetre
        // drawing stays in millimetres — they record what they found, exactly
        // as the 2D model records SourceUnit/PointsPerSourceUnit.
        ModelUnit SourceUnit = ModelUnit::Unspecified;
        double UnitScaleToMeters = 0.0;      // 0 = unknown
        UpAxis Up = UpAxis::YUp;
        Handedness Chirality = Handedness::RightHanded;

        // --- content, addressed by index ---
        std::vector<ModelScene> Scenes;
        int DefaultScene = -1;
        std::vector<ModelNode> Nodes;
        std::vector<ModelMesh> Meshes;
        // Exact boundary representation: trimmed NURBS and analytic surfaces
        // with the topology that closes them into solids. Filled by the CAD
        // formats — STEP, IGES, ACIS/SAT, Parasolid, OpenNURBS, DWG's
        // 3DSOLID/REGION/BODY — which carry no meshes at all. Empty for every
        // mesh format, and costing nothing when it is.
        BrepData Brep;
        std::vector<ModelMaterial> Materials;
        std::vector<ModelImage> Images;
        std::vector<ModelSampler> Samplers;
        std::vector<ModelSkin> Skins;
        std::vector<ModelAnimation> Animations;
        std::vector<ModelCamera> Cameras;
        std::vector<ModelLight> Lights;

        // --- queries ---
        // No meshes and no B-rep bodies. A document holding only exact solids
        // is not empty, even before anything has been tessellated.
        bool Empty() const;
        size_t TotalVertexCount() const;
        size_t TotalFaceCount() const;
        // World bounds of the default scene (or of every mesh when there are no
        // nodes), with node transforms applied.
        Bounds3D ComputeBounds() const;
        // Node transform composed with every ancestor's. Identity for an
        // out-of-range index or a cycle.
        Matrix4x4 GlobalTransform(int nodeIndex) const;

        // --- building ---
        int AddNode(ModelNode node, int parent = -1);
        int AddMesh(ModelMesh mesh);
        int AddMaterial(ModelMaterial material);
        // A single-mesh document — the shape STL, OBJ-without-groups, PLY and
        // OFF produce: one scene, one node, one mesh.
        static ModelDocument FromSingleMesh(ModelMesh mesh, const std::string& name = "");

        // --- normalising, for consumers that cannot handle the general case ---
        // Convert every primitive to indexed triangles. Returns the number
        // changed.
        size_t TriangulateAll();
        // Bake node transforms into vertex positions and reduce the graph to
        // one node per mesh. For a renderer or writer with no scene graph
        // (STL, OBJ, PLY, OFF). Skins and animations are dropped — they are
        // meaningless once transforms are baked — and the count of dropped
        // animations is returned via outDroppedAnimations when non-null.
        void FlattenTransforms(size_t* outDroppedAnimations = nullptr);
        // Rotate the model so Up becomes the requested axis, adjusting the
        // roots' transforms rather than the vertices. No-op when it matches.
        void ConvertUpAxis(UpAxis target);
        // Merge coincident vertices within tolerance, building Indices. STL
        // arrives as unindexed triangle soup — three quarters of the airplane
        // sample's 1.5 M vertices are duplicates.
        //
        // The lattice a vertex quantises into is searched together with its 26
        // neighbours, so two vertices a nanometre apart merge even when the
        // cell boundary runs between them. (Bucketing alone does not: it is a
        // hash of the rounded coordinate, and rounding is discontinuous exactly
        // where geometry tends to sit — on the axis planes, at the origin, at
        // every round number a CAD user typed.) Vertices still only merge when
        // their other attributes agree exactly, because a normal or UV seam is
        // a real discontinuity and welding across it is a visible bug.
        size_t WeldVertices(double tolerance = 1e-9);

        // Approximate every B-rep solid as a mesh and attach it to the node
        // that placed it, leaving the exact bodies in place. Nodes that already
        // have a mesh are left alone. Returns the number of solids meshed;
        // per-face failures are appended to `problems` when non-null rather
        // than aborting the rest.
        //
        // This is the operation the "a B-rep reader tessellates and warns"
        // rule used to hide inside a reader. Out here it can be re-run at a
        // finer tolerance, skipped entirely by a consumer that wants the
        // surfaces, or run twice for two levels of detail.
        size_t TessellateBreps(const BrepTessellationOptions& options = {},
                               std::vector<std::string>* problems = nullptr);
    };

} // namespace ModelStorage
} // namespace UltraCanvas

#endif // ULTRACANVAS_MODEL_STORAGE_H
