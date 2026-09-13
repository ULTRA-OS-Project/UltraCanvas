// Plugins/Models/COLLADA/UltraCanvasColladaConverter.h
// COLLADA (.dae) reader for ModelStorage::ModelDocument.
//
// COLLADA is the format the structure was shaped for: it states its unit and
// up axis, carries a real node hierarchy with instancing, separates geometry
// from the scene that places it, and holds materials and animation. Everything
// the earlier converters had to infer or warn about, a .dae simply says.
//
// Read and write. COLLADA's own consortium handed interchange to glTF, so a
// caller with a free choice should write that - but a great deal of tooling
// still takes .dae and nothing else, and COLLADA is the one writable format
// here that can carry the whole shape of a ModelDocument: unit, up axis, a
// node hierarchy with instancing, materials, n-gons and vertex colours all
// survive a round trip through it.
//
// What the writer does not carry, and says so:
//   * skinning, morph targets and animation - <library_controllers> and
//     <library_animations> are neither read nor written;
//   * cameras and lights;
//   * PBR material parameters beyond what profile_COMMON's <phong> holds; the
//     metallic/roughness pair is written into <extra> so a round trip through
//     this converter keeps it, but another reader will ignore it.
//
// Version: 1.1.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_COLLADA_CONVERTER_H
#define ULTRACANVAS_COLLADA_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class ColladaConverter : public IModelFormatConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::COLLADA; }
    std::string GetFormatName() const override { return "COLLADA"; }
    std::string GetFormatVersion() const override { return "1.4 / 1.5"; }
    std::vector<std::string> GetFileExtensions() const override { return {".dae"}; }
    std::string GetMimeType() const override { return "model/vnd.collada+xml"; }
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

    bool CanImport() const override { return true; }
    bool CanExport() const override { return true; }

    bool Export(const ModelStorage::ModelDocument& document,
                const std::string& filename,
                const ConversionOptions& options = ConversionOptions()) override;
    bool ExportToMemory(const ModelStorage::ModelDocument& document,
                        std::vector<uint8_t>& outData,
                        const ConversionOptions& options = ConversionOptions()) override;
    bool ExportToStream(const ModelStorage::ModelDocument& document,
                        std::ostream& stream,
                        const ConversionOptions& options = ConversionOptions()) override;

    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::vector<uint8_t>& data) const override;
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_COLLADA_CONVERTER_H
