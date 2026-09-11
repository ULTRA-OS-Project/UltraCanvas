// Plugins/Models/FBX/UltraCanvasFbxConverter.h
// FBX reader for ModelStorage::ModelDocument.
//
// FBX is the interchange format of the animation industry, and the one this
// framework's format matrix was most obviously missing. It is also the least
// like the others: it is not a scene *tree* on disk at all. Every object -
// model, geometry, material, texture, animation curve - is a flat entry in one
// `Objects` list with a 64-bit id, and a separate `Connections` list wires them
// together. The hierarchy, which mesh a node draws, which material a face uses
// and which curve drives which property are all connections, not nesting. So
// this reader builds the graph first and reads geometry second.
//
// Three things about it are worth knowing before trusting what comes out.
//
// **A node's transform is not TRS.** The format composes it as
//
//     T · Roff · Rp · Rpre · R · Rpost⁻¹ · Rp⁻¹ · Soff · Sp · S · Sp⁻¹
//
// with rotation and scaling pivots, offsets, and pre- and post-rotations that
// Maya and 3ds Max use constantly and that a Blender export leaves at identity.
// The whole chain is built here and then decomposed, so the common case comes
// back as exactly the three fields that were written and the uncommon case
// keeps its meaning instead of being silently dropped. `Lcl Rotation` is Euler
// degrees in whatever order `RotationOrder` names, which is honoured rather
// than assumed to be XYZ.
//
// **Its geometric transform is not part of the hierarchy.** GeometricTranslation,
// GeometricRotation and GeometricScaling place a *mesh* within its node without
// being inherited by that node's children - something the document's one
// transform per node cannot say. Rather than baking it into vertices, a node
// that has one gets a child node carrying the mesh, which is exact and costs
// nothing.
//
// **Its layers are independently indexed.** Normals, UVs, colours and material
// assignments each declare their own MappingInformationType (per corner, per
// vertex, per polygon, or one for the whole mesh) and ReferenceInformationType
// (direct or through an index array). All eight combinations are resolved here,
// and corners whose streams disagree become distinct document vertices - the
// same resolution the OBJ, COLLADA, X3D and .x readers perform.
//
// **Both generations are read, and they are not the same format.** The 7.x
// files that are usually binary are described above. The 6.x files that are
// usually ASCII share the connection idea and almost nothing else:
//
//   * objects have no ids at all - everything is named `"Class::Name"` and the
//     connections refer to those strings, so this reader synthesises an id per
//     name and the rest of it never learns the difference;
//   * there is no separate `Geometry` object: a `Model` holds its own
//     `Vertices` and `PolygonVertexIndex`;
//   * parameters live in `Properties60`, whose records are one field shorter
//     than `Properties70`'s;
//   * a texture connects to the *model*, not to a material property, so the
//     binding has to be inferred - which is exact with one of each, and
//     reported rather than guessed at otherwise;
//   * animation is a `Takes` block of nested `Channel` records rather than
//     stacks, layers and curve nodes. Same ticks, same three paths.
//
// Two things a 6.x file always contains and no scene refers to: a
// "Camera Switcher" model and seven "Producer" cameras, which every exporter
// inserts. They are skipped, and the count is recorded in metadata so their
// absence is stated rather than silent.
//
// Reading only. Skinning (Deformer / SubDeformer clusters), blend shapes and
// embedded media are recognised and reported rather than read; the capability
// report says so rather than implying otherwise.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_FBX_CONVERTER_H
#define ULTRACANVAS_FBX_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class FbxConverter : public ImportOnlyConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::FBX; }
    std::string GetFormatName() const override { return "Autodesk FBX"; }
    std::string GetFormatVersion() const override { return "6.x and 7.x, binary and ASCII"; }
    std::vector<std::string> GetFileExtensions() const override { return {".fbx"}; }
    std::string GetMimeType() const override { return "application/octet-stream"; }
    FormatCapabilities GetCapabilities() const override;

    std::shared_ptr<ModelStorage::ModelDocument> Import(
            const std::string& filename,
            const ConversionOptions& options = ConversionOptions()) override;
    std::shared_ptr<ModelStorage::ModelDocument> ImportFromMemory(
            const std::vector<uint8_t>& data,
            const ConversionOptions& options = ConversionOptions()) override;
    std::shared_ptr<ModelStorage::ModelDocument> ImportFromStream(
            std::istream& stream,
            const ConversionOptions& options = ConversionOptions()) override;

    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::vector<uint8_t>& data) const override;
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_FBX_CONVERTER_H
