// Plugins/Models/Blend/UltraCanvasBlendConverter.h
// Blender .blend in the converter matrix — as a format that declines to
// import, and says exactly why.
//
// A converter that never converts sounds like a contradiction, but the
// alternative is worse: a .blend either fails to open with no explanation, or
// imports geometry that is missing most of the model. This one recognises the
// file, reports what is in it, and points at the export that would work. See
// UltraCanvasBlendFile.h for the reasoning and the measurements behind it.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BLEND_CONVERTER_H
#define ULTRACANVAS_BLEND_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"
#include "Models/Blend/UltraCanvasBlendFile.h"

namespace UltraCanvas {
namespace ModelConverter {

class BlendConverter : public ImportOnlyConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::Blend; }
    std::string GetFormatName() const override { return "Blender"; }
    std::string GetFormatVersion() const override { return "2.5 and later"; }
    std::vector<std::string> GetFileExtensions() const override { return {".blend"}; }
    std::string GetMimeType() const override { return "application/x-blender"; }
    // Everything false: this converter produces no geometry, and a capability
    // report exists to say what a converter does, not what its format could
    // hold in principle.
    FormatCapabilities GetCapabilities() const override { return FormatCapabilities(); }

    // All three return nullptr, having reported what the file contains and
    // which export would import.
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

    // What the file holds — the useful half of this converter, for a file
    // browser or a properties panel.
    static BlendFileInfo Inspect(const std::string& filename);
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_BLEND_CONVERTER_H
