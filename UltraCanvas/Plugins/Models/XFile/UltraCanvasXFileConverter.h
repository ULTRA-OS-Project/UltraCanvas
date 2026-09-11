// Plugins/Models/XFile/UltraCanvasXFileConverter.h
// DirectX .x reader for ModelStorage::ModelDocument.
//
// The .x file is Direct3D retained mode's scene format: a Frame hierarchy with
// a 4x4 matrix per frame, meshes with n-gon faces, per-face material lists and
// Phong materials with a texture name. It long outlived retained mode itself -
// it is still what many game-asset pipelines, Blender and 3ds Max exporters,
// and two decades of sample code emit - which is why it is worth reading.
//
// Two things about it are worth knowing before trusting what comes out.
//
// **It is a left-handed format, and that is not a detail.** Direct3D's space is
// left-handed, so an exporter converting from a right-handed application puts a
// reflection in the root frame's matrix (its determinant is -1) and writes the
// face indices the other way round to compensate. Both halves of that are in
// the file and they cancel: the E-45 sample's meshes have *negative* signed
// volume in their own object space and *positive* volume once the frame chain
// is applied, and the faces disagree with the file's own MeshNormals in object
// space while agreeing with them in world space, on 93 of 93 and 925 of 937
// faces. So this reader changes neither the matrix nor the winding. A reader
// that "fixed" the winding it saw in object space would deliver a model that is
// inside out - which is the opposite of what the Alembic reader must do, for
// the opposite reason.
//
// Because that only holds for a well-formed export, the reader measures it
// rather than assuming it: where a mesh carries MeshNormals, the winding is
// compared against them *through the node's world transform*, and a file whose
// faces really are inside out is reported instead of being quietly loaded.
//
// **Its data has no field names.** The `template` blocks at the top of a file
// declare layouts, but every real reader ignores them in favour of the known
// ones, because an exporter that wrote a template disagreeing with the spec
// would be unreadable by Direct3D too. So the syntax layer flattens each
// object's data into one numeric array and this one reads it positionally. See
// UltraCanvasXFile.h.
//
// Reading only, and geometry only: AnimationSet, XSkinMeshHeader and
// SkinWeights are recognised and reported rather than read, because the
// quaternion convention of an .x rotation key cannot be verified against a
// sample that carries none - and a silently wrong animation is worse than one
// that says it is missing. The capability report says so too.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_XFILE_CONVERTER_H
#define ULTRACANVAS_XFILE_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class XFileConverter : public ImportOnlyConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::XFile; }
    std::string GetFormatName() const override { return "DirectX X"; }
    std::string GetFormatVersion() const override { return "3.2 / 3.3, text and binary"; }
    std::vector<std::string> GetFileExtensions() const override { return {".x"}; }
    std::string GetMimeType() const override { return "model/x-directx"; }
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

#endif // ULTRACANVAS_XFILE_CONVERTER_H
