// include/DataFormats/UltraCanvasModelConverter.h
// IModelFormatConverter — the read/write interface every 3D file format
// implements against ModelStorage::ModelDocument.
//
// The 3D counterpart of VectorConverter::IVectorFormatConverter, and
// deliberately the same shape: one interface, format dispatch by extension,
// a capability report, and a warning callback so a lossy conversion says so
// instead of quietly dropping data. Converters live in their format's plugin
// directory (UltraCanvas/Plugins/Models/<FORMAT>/); only the interface and the
// document are core.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MODEL_CONVERTER_H
#define ULTRACANVAS_MODEL_CONVERTER_H

#include "DataFormats/UltraCanvasModelStorage.h"

#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace ModelConverter {

// ===== FORMAT IDENTIFIERS =====

enum class ModelFormat {
    Unknown = 0,

    // Mesh interchange
    STL,        // Stereolithography (ASCII + binary)
    OBJ,        // Wavefront OBJ (+ MTL)
    PLY,        // Stanford polygon / point cloud
    OFF,        // Object File Format

    // Scene interchange
    GLTF,       // glTF 2.0, JSON + external buffers
    GLB,        // glTF 2.0, single binary container
    COLLADA,    // COLLADA 1.4/1.5 (.dae)
    FBX,        // Autodesk FBX
    ThreeDS,    // Autodesk 3D Studio (.3ds)
    X3D,        // X3D / VRML
    XFile,      // DirectX retained-mode .x (text and binary)

    // Manufacturing
    ThreeMF,    // 3D Manufacturing Format (.3mf)
    AMF,        // Additive Manufacturing Format

    // CAD, read for the mesh entities it carries. Its B-rep solids
    // (3DSOLID / REGION / BODY / SURFACE) belong in ModelDocument::Brep; the
    // reader for them is not written yet.
    DXF,        // AutoCAD Drawing Exchange Format
    DWG,        // AutoCAD Drawing

    // Boundary representation: trimmed NURBS and analytic surfaces with the
    // topology that closes them into solids. These carry no meshes at all —
    // a reader fills ModelDocument::Brep, and the caller decides at what
    // tolerance, if ever, to tessellate. See UltraCanvasBrepStorage.h.
    STEP,       // ISO 10303 AP203/214/242 (.step, .stp)
    IGES,       // ANSI/US PRO IGES 5.3 (.iges, .igs)
    ACIS,       // ACIS SAT / SAB (.sat, .sab) — also DWG's 3DSOLID payload
    Parasolid,  // Parasolid XT (.x_t, .x_b)
    OpenNURBS,  // Rhinoceros 3DM (.3dm)

    // Point clouds
    PCD,        // Point Cloud Library
    LAS,        // ASPRS LiDAR
    XYZ,        // plain ASCII points

    // Application-native. Not exchange formats: their geometry is only what
    // the application's own evaluation produces, so a reader recognises them
    // and points at an export rather than importing a partial model.
    Blend,      // Blender

    // Caches: geometry already evaluated, sampled over time. Not a modelling
    // format and not an exact one — what is in the file is what an artist saw
    // on the frame it was written.
    Alembic,    // Alembic .abc (Ogawa backend)

    // Internal
    UltraCanvas // native serialisation of ModelDocument
};

// ===== CONVERSION OPTIONS =====

// How many digits a text format writes per number. Only text formats are
// affected — a binary writer stores the value itself, so there is nothing to
// choose.
enum class NumericPrecision {
    // The C++ stream default: 6 significant digits. Compact, and lossy — it is
    // enough for a model authored in the range a modeller works in, and it is
    // what OBJ, PLY and STL files in the wild almost always contain.
    Compact,
    // Enough digits that every value read back is bit-identical to the one
    // written: 17 for the double positions, 9 for the float attributes
    // (max_digits10 for each). Costs about a third more file size — the E-45
    // aircraft sample goes from 1.37 MB to 1.87 MB as OBJ. Use it for CAD,
    // survey and geospatial models, where coordinates sit far from the origin,
    // and whenever a file is an intermediate step rather than a deliverable.
    //
    // The difference is not academic: a survey coordinate of 1234567.8912345678
    // comes back as 1234570 under Compact — 2.11 units, or two metres, lost to
    // six significant digits.
    Full
};

struct ConversionOptions {
    // --- geometry ---
    // Reduce every primitive to indexed triangles on import. Consumers that
    // only draw triangles (the GL viewer) want this; a converter writing a
    // format with n-gons does not.
    bool TriangulateOnImport = false;
    // Merge duplicate vertices on import. STL and OFF arrive as unindexed
    // soup; 0 disables.
    double WeldTolerance = 0.0;
    // Generate vertex normals when the file carries none.
    bool GenerateMissingNormals = true;

    // --- space ---
    // Rotate the model to this up axis on import. Unset keeps the file's own,
    // recorded in ModelDocument::Up.
    std::optional<ModelStorage::UpAxis> ForceUpAxis;
    // Assume this unit when a format cannot state one (STL, OBJ, PLY are all
    // unitless). Only fills SourceUnit; geometry is never rescaled.
    ModelStorage::ModelUnit AssumeUnit = ModelStorage::ModelUnit::Unspecified;

    // --- resources ---
    // Resolve and load textures referenced by relative path into
    // ModelImage::Data. Off by default: a reader should not touch the
    // filesystem beyond the model file unless asked.
    bool LoadExternalImages = false;
    // Embed image bytes in the output where the format allows (GLB, 3MF, FBX).
    bool EmbedImages = false;

    // --- B-rep ---
    // What to do with exact bodies on import. The document always keeps them;
    // this only decides whether a mesh is built alongside, and how fine. A
    // viewer wants it on; a converter writing STEP to IGES wants it off,
    // because tessellating and discarding is exactly the loss the B-rep
    // structure exists to prevent.
    bool TessellateOnImport = false;
    ModelStorage::BrepTessellationOptions Tessellation;

    // --- writing ---
    // Prefer the compact binary encoding where a format has both (STL, PLY,
    // FBX, glTF/GLB).
    bool PreferBinary = true;
    // Digits per number for text formats. See NumericPrecision: Compact is the
    // stream default of 6 significant digits, Full round-trips every value
    // exactly at roughly twice the file size.
    NumericPrecision Precision = NumericPrecision::Compact;
    std::string Generator = "UltraCanvas";

    // Every fallback, approximation and dropped feature is reported here, the
    // same contract the vector converters follow. A converter that cannot
    // represent something must call this rather than fail silently.
    std::function<void(const std::string&)> WarningCallback;

    void Warn(const std::string& message) const {
        if (WarningCallback) WarningCallback(message);
    }
};

// ===== CAPABILITIES =====

// What a converter's reader and writer actually implement. These must be
// computed from the implementation, not aspirational: the 2D model's survey
// found capability flags that no longer matched their converters, and a flag
// that lies is worse than one that is absent.
struct FormatCapabilities {
    bool Meshes = false;
    bool NGons = false;             // faces with more than 3 vertices survive
    bool PointClouds = false;
    bool Lines = false;
    bool SceneGraph = false;        // node hierarchy with transforms
    bool Instancing = false;        // one mesh referenced by several nodes
    bool Materials = false;
    bool PBRMaterials = false;      // metallic-roughness, not just Phong
    bool Textures = false;
    bool EmbeddedTextures = false;
    bool VertexColors = false;
    bool TextureCoordinates = false;
    bool Normals = false;
    bool Tangents = false;
    bool Skinning = false;
    bool MorphTargets = false;
    bool Animations = false;
    bool Cameras = false;
    bool Lights = false;
    bool Units = false;             // the format states a physical unit
    bool UpAxis = false;            // the format states an up axis
    bool CustomAttributes = false;  // arbitrary per-vertex properties
    bool Metadata = false;
    bool DoublePrecision = false;   // positions survive without float rounding

    // Boundary representation: the format carries exact trimmed surfaces and
    // topology rather than (or as well as) triangles, and the converter fills
    // ModelDocument::Brep. A format with Brep and without Meshes — STEP, IGES,
    // Parasolid — is not a broken mesh reader; it is a reader of something
    // else, and a caller that wants triangles asks for TessellateBreps().
    bool Brep = false;
    bool NurbsSurfaces = false;     // as opposed to analytic surfaces only
    bool Assemblies = false;        // several named bodies with placements
};

// ===== CONVERTER INTERFACE =====

class IModelFormatConverter {
public:
    virtual ~IModelFormatConverter() = default;

    // Format identity
    virtual ModelFormat GetFormat() const = 0;
    virtual std::string GetFormatName() const = 0;
    virtual std::string GetFormatVersion() const = 0;
    virtual std::vector<std::string> GetFileExtensions() const = 0;   // ".stl", ...
    virtual std::string GetMimeType() const = 0;

    // Capabilities
    virtual FormatCapabilities GetCapabilities() const = 0;
    virtual bool CanImport() const = 0;
    virtual bool CanExport() const = 0;

    // Import
    virtual std::shared_ptr<ModelStorage::ModelDocument> Import(
            const std::string& filename,
            const ConversionOptions& options = ConversionOptions()) = 0;
    virtual std::shared_ptr<ModelStorage::ModelDocument> ImportFromMemory(
            const std::vector<uint8_t>& data,
            const ConversionOptions& options = ConversionOptions()) = 0;
    virtual std::shared_ptr<ModelStorage::ModelDocument> ImportFromStream(
            std::istream& stream,
            const ConversionOptions& options = ConversionOptions()) = 0;

    // Export
    virtual bool Export(const ModelStorage::ModelDocument& document,
                        const std::string& filename,
                        const ConversionOptions& options = ConversionOptions()) = 0;
    virtual bool ExportToMemory(const ModelStorage::ModelDocument& document,
                                std::vector<uint8_t>& outData,
                                const ConversionOptions& options = ConversionOptions()) = 0;
    virtual bool ExportToStream(const ModelStorage::ModelDocument& document,
                                std::ostream& stream,
                                const ConversionOptions& options = ConversionOptions()) = 0;

    // Signature sniffing — never trust the extension alone. A binary STL and
    // an ASCII STL share one, and so do glTF's .gltf/.glb.
    virtual bool ValidateFile(const std::string& filename) const = 0;
    virtual bool ValidateData(const std::vector<uint8_t>& data) const = 0;
};

// Convenience base for a format that can only be read (a proprietary format
// with no public write path) or only written.
class ImportOnlyConverter : public IModelFormatConverter {
public:
    bool CanImport() const override { return true; }
    bool CanExport() const override { return false; }

    bool Export(const ModelStorage::ModelDocument&, const std::string&,
                const ConversionOptions& options) override {
        options.Warn(GetFormatName() + ": no writer for this format");
        return false;
    }
    bool ExportToMemory(const ModelStorage::ModelDocument&, std::vector<uint8_t>&,
                        const ConversionOptions& options) override {
        options.Warn(GetFormatName() + ": no writer for this format");
        return false;
    }
    bool ExportToStream(const ModelStorage::ModelDocument&, std::ostream&,
                        const ConversionOptions& options) override {
        options.Warn(GetFormatName() + ": no writer for this format");
        return false;
    }
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_MODEL_CONVERTER_H
