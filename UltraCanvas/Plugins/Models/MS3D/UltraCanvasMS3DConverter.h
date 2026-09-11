// Plugins/Models/MS3D/UltraCanvasMS3DConverter.h
// MilkShape 3D (.ms3d) reader for ModelStorage::ModelDocument.
//
// MS3D is the simplest real format in this matrix, and worth having for that
// reason: it is a fixed sequence of packed little-endian structs with no
// container grammar at all - no chunks, no tags, no offsets - so there is
// nothing to split a container layer off from. Read the header, then a count
// and that many vertices, then a count and that many triangles, and so on to
// the end of the file. Everything after the joints is optional and versioned,
// which is the one place the format grew.
//
// It is also a *game* format rather than an interchange one, and the shape of
// it says so:
//
//   * Positions are indexed and shared; normals and texture coordinates are
//     stored per *triangle corner*. Corners whose streams disagree therefore
//     become distinct document vertices - the same resolution the OBJ,
//     COLLADA, X3D, FBX and Alembic readers perform, for the same reason.
//   * A triangle carries a smoothing group number, which `MeshPrimitive`
//     holds directly as a bitmask. MS3D numbers them 1..32 with 0 meaning
//     none, so the number becomes the bit rather than the value.
//   * A group is a named list of triangle indices plus one material, which is
//     exactly a mesh with one primitive, so groups become meshes.
//   * Skinning is stored twice over. Every vertex has a single `boneId`, and
//     a later optional block adds three more bones with weights. The optional
//     block wins where it is present, because a file that has it wrote the
//     single id only for readers that predate it.
//
// Joints are a flat list that names its own parents *by string*, with local
// rotation as XYZ Euler radians and keyframes for rotation and translation
// held separately. Those become a node hierarchy plus one `ModelAnimation`,
// with the two key streams sampled onto their union - the same treatment FBX's
// independent per-axis curves get.
//
// One thing that cannot be settled from the specification: MilkShape's own
// `ms3dspec.h` comments a keyframe's `time` as seconds, while its interface
// works in frames and the file separately stores `fAnimationFPS` and
// `iTotalFrames`. This reader takes the field as seconds, as the header says,
// and records the frame rate and frame count in metadata so a caller that
// disagrees has what it needs to convert. The test pins that choice rather
// than leaving it implied.
//
// Reading only. MilkShape's exporters are what feed it, and a document leaving
// this framework has OBJ, STEP and the rest.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_MS3D_CONVERTER_H
#define ULTRACANVAS_MS3D_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class MS3DConverter : public ImportOnlyConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::MS3D; }
    std::string GetFormatName() const override { return "MilkShape 3D"; }
    std::string GetFormatVersion() const override { return "3 and 4"; }
    std::vector<std::string> GetFileExtensions() const override { return {".ms3d"}; }
    std::string GetMimeType() const override { return "application/x-milkshape3d"; }
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

#endif // ULTRACANVAS_MS3D_CONVERTER_H
