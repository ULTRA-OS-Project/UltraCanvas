// Plugins/Models/COLLADA/UltraCanvasColladaConverter.h
// COLLADA (.dae) reader for ModelStorage::ModelDocument.
//
// COLLADA is the format the structure was shaped for: it states its unit and
// up axis, carries a real node hierarchy with instancing, separates geometry
// from the scene that places it, and holds materials and animation. Everything
// the earlier converters had to infer or warn about, a .dae simply says.
//
// Reading only. COLLADA's own consortium handed interchange to glTF, and a
// caller wanting to write a scene should write that; a COLLADA writer would be
// a large amount of XML for a format nothing new consumes.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_COLLADA_CONVERTER_H
#define ULTRACANVAS_COLLADA_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class ColladaConverter : public ImportOnlyConverter {
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

    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::vector<uint8_t>& data) const override;
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_COLLADA_CONVERTER_H
