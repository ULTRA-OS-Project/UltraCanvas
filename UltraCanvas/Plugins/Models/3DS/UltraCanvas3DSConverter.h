// Plugins/Models/3DS/UltraCanvas3DSConverter.h
// Autodesk 3D Studio (.3ds) reader for ModelStorage::ModelDocument.
//
// 3DS is a chunked binary format from 3D Studio for DOS: a tree of
// {uint16 id, uint32 length} records, little-endian throughout. It is legacy —
// 16-bit vertex and face counts cap a mesh at 65 535 of each, names are
// truncated, and materials are fixed-function Phong — but it is still one of
// the most widely exchanged mesh formats, and it exercises more of
// ModelDocument than anything else the framework can read today: several named
// meshes, per-object matrices, materials with texture maps, and per-face
// material groups.
//
// Reading only. Writing 3DS would mean emitting a format whose own vendor
// superseded it twice over, with limits the document does not have; a caller
// wanting interchange writes glTF or OBJ. See
// Docs/Research/UltraCanvas3DModelProposal.md.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_3DS_CONVERTER_H
#define ULTRACANVAS_3DS_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class ThreeDSConverter : public ImportOnlyConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::ThreeDS; }
    std::string GetFormatName() const override { return "Autodesk 3D Studio"; }
    std::string GetFormatVersion() const override { return "3DS release 3/4"; }
    std::vector<std::string> GetFileExtensions() const override { return {".3ds"}; }
    std::string GetMimeType() const override { return "application/x-3ds"; }
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

#endif // ULTRACANVAS_3DS_CONVERTER_H
