// Plugins/Models/DXF/UltraCanvasDXFModelConverter.h
// DXF read as 3D geometry, into ModelStorage::ModelDocument.
//
// UltraCanvas already reads DXF — in the Vector plugin, into the 2D
// VectorDocument, where 3DFACE and the mesh polylines have their Z discarded
// because there is nowhere to put it. This is the same file read for the
// geometry that reader has to throw away: 3DFACE, polyface meshes, polygon
// meshes, 3D polylines and points, in three dimensions.
//
// The two readers are complements, not rivals. A floor plan is a drawing and
// belongs in VectorDocument; an exported model is geometry and belongs here.
// A caller that wants both reads the file twice.
//
// Scope is the 3D entity set. Anything a drawing carries and a model does not
// — text, dimensions, hatches, splines, arcs — is not read, and the reader
// says so rather than producing a suspiciously empty model.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_DXF_MODEL_CONVERTER_H
#define ULTRACANVAS_DXF_MODEL_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class DXFModelConverter : public ImportOnlyConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::DXF; }
    std::string GetFormatName() const override { return "AutoCAD DXF (3D entities)"; }
    std::string GetFormatVersion() const override { return "R12 and later"; }
    std::vector<std::string> GetFileExtensions() const override { return {".dxf"}; }
    std::string GetMimeType() const override { return "image/vnd.dxf"; }
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

    // True when the file has at least one entity this reader can turn into
    // geometry. A DXF is a drawing format first: most files that open fine in
    // a CAD viewer contain no 3D at all, and a caller is better off asking
    // than receiving an empty document.
    static bool HasThreeDimensionalGeometry(const std::string& filename);
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_DXF_MODEL_CONVERTER_H
