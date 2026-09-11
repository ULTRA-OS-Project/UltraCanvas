// Plugins/Models/PLY/UltraCanvasPLYConverter.h
// Stanford PLY (.ply) read and write for ModelStorage::ModelDocument.
//
// PLY is the format with no fixed schema. A file declares its own elements and
// the properties each one carries, and a reader is expected to cope with
// whatever it finds:
//
//   element vertex 40333
//   property float x
//   property float nx
//   property float s
//   property uchar red
//   property float quality          <- anything at all may appear here
//   element face 32440
//   property list uchar uint vertex_indices
//
// That openness is why PLY is what scanners, photogrammetry and research code
// emit, and it is the reason ModelDocument has open-ended named
// VertexAttributes at all: a per-vertex "quality", "confidence" or
// "classification" has nowhere to go in OBJ or 3DS, and here it round-trips as
// an AttributeSemantic::Custom rather than being dropped.
//
// All three encodings are read - ascii, binary_little_endian and
// binary_big_endian - because a reader that handles only ASCII fails on most
// scanner output, and one that assumes the host's byte order fails silently
// rather than loudly.
//
// Properties this reader gives a meaning to, and the spellings it accepts:
//
//   position    x, y, z
//   normal      nx, ny, nz
//   texture     s/t, u/v, texture_u/texture_v
//   colour      red, green, blue, alpha - uchar 0..255 or float 0..1
//   faces       vertex_indices, or vertex_index as older writers spell it
//
// Everything else per-vertex becomes a Custom attribute under its own name.
// Elements that are neither vertex nor face - edge lists, per-face material
// tables - are skipped, but their bytes are still consumed exactly, because in
// a binary file a mis-sized skip does not lose one element, it destroys
// everything after it.
//
// A PLY with vertices and no faces is a point cloud, and arrives as one
// (PrimitiveMode::Points) rather than as an empty mesh.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_PLY_CONVERTER_H
#define ULTRACANVAS_PLY_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class PLYConverter : public IModelFormatConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::PLY; }
    std::string GetFormatName() const override { return "Stanford PLY"; }
    std::string GetFormatVersion() const override { return "1.0, ascii and binary"; }
    std::vector<std::string> GetFileExtensions() const override { return {".ply"}; }
    std::string GetMimeType() const override { return "model/ply"; }
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

    // Writes one element vertex and one element face, flattening the document's
    // node transforms first - PLY has no scene graph, so geometry that is not
    // baked arrives in the wrong place. ConversionOptions::PreferBinary chooses
    // binary_little_endian over ascii.
    //
    // Precision chooses the type positions are written as, not merely how many
    // digits are printed: Compact writes `property float`, which is what almost
    // every PLY carries, and Full writes `property double`, which is the only
    // way a document that came from CAD survives the trip - a coordinate far
    // from the origin has already lost millimetres by the time it is a float.
    // Written as a type rather than a digit count so the flag means something
    // in binary too, where digits do not exist.
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

#endif // ULTRACANVAS_PLY_CONVERTER_H
