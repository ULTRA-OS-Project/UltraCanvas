// Plugins/Models/OBJ/UltraCanvasOBJConverter.h
// Wavefront OBJ (+ MTL) reader and writer for ModelStorage::ModelDocument.
//
// OBJ is the lowest common denominator of 3D interchange: a line-based text
// format with no scene graph, no transforms, no animation and no declared
// units — but n-gon faces, separate position/texcoord/normal index streams,
// objects and groups, and a companion .mtl material library. Nearly every tool
// reads it, which makes it the format to write when interchange matters more
// than fidelity.
//
// Read and write both. Unlike 3DS, writing OBJ loses almost nothing the
// document holds for a static mesh, and it is what makes a round trip through
// the structure testable.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_OBJ_CONVERTER_H
#define ULTRACANVAS_OBJ_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class OBJConverter : public IModelFormatConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::OBJ; }
    std::string GetFormatName() const override { return "Wavefront OBJ"; }
    std::string GetFormatVersion() const override { return "OBJ 3.0 / MTL"; }
    std::vector<std::string> GetFileExtensions() const override { return {".obj"}; }
    std::string GetMimeType() const override { return "model/obj"; }
    FormatCapabilities GetCapabilities() const override;

    bool CanImport() const override { return true; }
    bool CanExport() const override { return true; }

    std::shared_ptr<ModelStorage::ModelDocument> Import(
            const std::string& filename,
            const ConversionOptions& options = ConversionOptions()) override;
    std::shared_ptr<ModelStorage::ModelDocument> ImportFromMemory(
            const std::vector<uint8_t>& data,
            const ConversionOptions& options = ConversionOptions()) override;
    std::shared_ptr<ModelStorage::ModelDocument> ImportFromStream(
            std::istream& stream,
            const ConversionOptions& options = ConversionOptions()) override;

    // Writes the .obj; the companion .mtl goes beside it, named after the
    // model file, whenever the document has materials.
    bool Export(const ModelStorage::ModelDocument& document,
                const std::string& filename,
                const ConversionOptions& options = ConversionOptions()) override;
    // Geometry only — an in-memory OBJ has nowhere to put its material library,
    // so materials are reported through the warning callback and dropped.
    bool ExportToMemory(const ModelStorage::ModelDocument& document,
                        std::vector<uint8_t>& outData,
                        const ConversionOptions& options = ConversionOptions()) override;
    bool ExportToStream(const ModelStorage::ModelDocument& document,
                        std::ostream& stream,
                        const ConversionOptions& options = ConversionOptions()) override;

    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::vector<uint8_t>& data) const override;

    // The .mtl text for a document, so a caller writing an OBJ somewhere this
    // converter cannot reach (an archive, a network stream) can place the
    // library itself.
    static std::string BuildMaterialLibrary(
            const ModelStorage::ModelDocument& document,
            NumericPrecision precision = NumericPrecision::Compact);
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_OBJ_CONVERTER_H
