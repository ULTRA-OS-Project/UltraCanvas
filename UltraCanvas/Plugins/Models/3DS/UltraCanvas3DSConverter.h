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
// Read and write. The writer exists because 3DS is still what a great many
// tools and asset pipelines accept, not because it is a good container: its
// limits are real and the writer reports each one it hits rather than
// truncating in silence. A caller that only wants interchange should still
// prefer OBJ or glTF. See Docs/Research/UltraCanvas3DModelProposal.md.
//
// What the writer cannot carry, and says so:
//   * 65 535 vertices and 65 535 faces per object - the counts are uint16.
//     A primitive over either limit is skipped with a warning rather than
//     wrapped around into garbage.
//   * 12-character names. Longer ones are truncated and de-duplicated.
//   * Triangles only. N-gons are triangulated on the way out.
//   * Fixed-function materials, one texture slot each.
//   * No skinning, morph targets, animation or per-vertex colour.
//   * Z-up. A Y-up document is rotated into it, which is a change to the
//     numbers and is warned about; a 3DS read back out is unrotated, so a
//     round trip is identity.
//
// Version: 1.1.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_3DS_CONVERTER_H
#define ULTRACANVAS_3DS_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class ThreeDSConverter : public IModelFormatConverter {
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

    bool CanImport() const override { return true; }
    bool CanExport() const override { return true; }

    bool Export(const ModelStorage::ModelDocument& document,
                const std::string& filename,
                const ConversionOptions& options = ConversionOptions()) override;
    bool ExportToMemory(const ModelStorage::ModelDocument& document,
                        std::vector<uint8_t>& outData,
                        const ConversionOptions& options = ConversionOptions()) override;
    // 3DS is binary, so the stream must be opened in binary mode by the caller.
    bool ExportToStream(const ModelStorage::ModelDocument& document,
                        std::ostream& stream,
                        const ConversionOptions& options = ConversionOptions()) override;

    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::vector<uint8_t>& data) const override;
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_3DS_CONVERTER_H
