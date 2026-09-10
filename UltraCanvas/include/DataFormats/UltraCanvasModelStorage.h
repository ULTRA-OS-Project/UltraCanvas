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
// Docs/Research/UltraCanvas3DModelProposal.md for the survey those
// annotations come from, and for what is deliberately out of scope
// (B-rep/NURBS solids: STEP, IGES, ACIS and the DWG 3DSOLID family).
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MODEL_STORAGE_H
#define ULTRACANVAS_MODEL_STORAGE_H

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace ModelStorage {

// ===== SCALARS =====

// Positions are double. CAD, survey and geospatial sources place geometry far
// from the origin, where float has already lost millimetre precision (a
// coordinate of 1e6 resolves to ~0.06); the 2D model learned the same lesson
// and moved to double. Everything else — normals, tangents, texture
// coordinates, colours, weights — is float, because it is bounded and small.
    struct Vec3d {
        double x = 0.0, y = 0.0, z = 0.0;

        Vec3d() = default;
        Vec3d(double vx, double vy, double vz) : x(vx), y(vy), z(vz) {}

        Vec3d operator+(const Vec3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
        Vec3d operator-(const Vec3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
        Vec3d operator*(double s) const { return {x * s, y * s, z * s}; }
        Vec3d& operator+=(const Vec3d& o) { x += o.x; y += o.y; z += o.z; return *this; }

        double Dot(const Vec3d& o) const { return x * o.x + y * o.y + z * o.z; }
        Vec3d Cross(const Vec3d& o) const {
            return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
        }
        double Length() const { return std::sqrt(x * x + y * y + z * z); }
        Vec3d Normalized() const {
            double len = Length();
            if (len <= 1e-15) return {0.0, 0.0, 0.0};
            return {x / len, y / len, z / len};
        }
    };

    struct Vec3f {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        Vec3f() = default;
        Vec3f(float vx, float vy, float vz) : x(vx), y(vy), z(vz) {}
    };

    struct Vec4f {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
        Vec4f() = default;
        Vec4f(float vx, float vy, float vz, float vw) : x(vx), y(vy), z(vz), w(vw) {}
    };

// Rotation as a unit quaternion (x, y, z, w) — the form glTF, FBX and USD all
// store and the only one that interpolates without gimbal loss. COLLADA's
// axis-angle and 3DS's Euler triples convert on import.
    struct Quatd {
        double x = 0.0, y = 0.0, z = 0.0, w = 1.0;

        Quatd() = default;
        Quatd(double qx, double qy, double qz, double qw) : x(qx), y(qy), z(qz), w(qw) {}

        static Quatd Identity() { return {}; }
        static Quatd FromAxisAngle(const Vec3d& axis, double radians);

        Quatd Normalized() const;
        Quatd operator*(const Quatd& o) const;   // this ∘ o: apply o first
        Vec3d Rotate(const Vec3d& v) const;
    };

// Column-major 4x4, the OpenGL memory layout: m[col * 4 + row], so a
// translation sits in m[12..14] and the array can be handed to glUniformMatrix4fv
// after narrowing. Same convention as the existing float Mat4 in the STL
// plugin, in double.
    struct Matrix4x4 {
        double m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

        static Matrix4x4 Identity() { return {}; }
        static Matrix4x4 Translation(const Vec3d& t);
        static Matrix4x4 Scaling(const Vec3d& s);
        static Matrix4x4 FromQuaternion(const Quatd& q);
        // T * R * S, the composition order every scene format defines TRS in.
        static Matrix4x4 FromTRS(const Vec3d& translation, const Quatd& rotation,
                                 const Vec3d& scale);

        Matrix4x4 operator*(const Matrix4x4& o) const;   // this * o: apply o first
        Vec3d TransformPoint(const Vec3d& p) const;      // w = 1, divides by w
        Vec3d TransformDirection(const Vec3d& v) const;  // w = 0, no translation
        // Normals transform by the inverse transpose; returns false and leaves
        // the result identity when the matrix is not invertible.
        bool InverseTransposeUpper3x3(double out[9]) const;
        bool IsIdentity(double epsilon = 1e-12) const;
        // Decompose into TRS. Returns false when the matrix carries shear or a
        // mirror that TRS cannot express — the caller should keep the matrix.
        bool DecomposeTRS(Vec3d& translation, Quatd& rotation, Vec3d& scale) const;
    };

// ===== BOUNDS =====

    struct Bounds3D {
        Vec3d Min{ std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max() };
        Vec3d Max{ -std::numeric_limits<double>::max(),
                   -std::numeric_limits<double>::max(),
                   -std::numeric_limits<double>::max() };

        bool IsValid() const { return Min.x <= Max.x; }
        void Expand(const Vec3d& p);
        void Expand(const Bounds3D& other);
        Vec3d Center() const;
        Vec3d Size() const;
        double Radius() const;   // half the bounding diagonal; 1.0 when empty
    };

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

        size_t VertexCount() const { return Positions.size(); }
        // Faces for the current Mode: triangles, lines, points or polygons.
        size_t FaceCount() const;
        // Vertex indices of one face, resolved through Indices and Mode.
        std::vector<uint32_t> Face(size_t faceIndex) const;

        Bounds3D ComputeBounds() const;
        const VertexAttribute* FindAttribute(AttributeSemantic semantic, int set = 0) const;
        const VertexAttribute* FindAttribute(const std::string& name) const;

        // Convert any face topology to indexed Triangles in place. Strips and
        // fans expand, polygons fan-triangulate about their first vertex
        // (correct for the convex faces every mesh format in practice emits;
        // concave n-gons are noted in the proposal as a later ear-clipping
        // step). Points and lines are left alone and return false.
        bool Triangulate();

        // Area-weighted vertex normals from the geometry. Used when a file has
        // none (OBJ without vn, PLY without nx) or ships degenerate ones (an
        // STL facet normal of (0,0,0)).
        void RecomputeNormals();
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
        std::vector<ModelMaterial> Materials;
        std::vector<ModelImage> Images;
        std::vector<ModelSampler> Samplers;
        std::vector<ModelSkin> Skins;
        std::vector<ModelAnimation> Animations;
        std::vector<ModelCamera> Cameras;
        std::vector<ModelLight> Lights;

        // --- queries ---
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
        // Merge byte-identical vertices within tolerance, building Indices.
        // STL arrives as unindexed triangle soup — three quarters of the
        // airplane sample's 1.5 M vertices are duplicates.
        size_t WeldVertices(double tolerance = 1e-9);
    };

} // namespace ModelStorage
} // namespace UltraCanvas

#endif // ULTRACANVAS_MODEL_STORAGE_H
