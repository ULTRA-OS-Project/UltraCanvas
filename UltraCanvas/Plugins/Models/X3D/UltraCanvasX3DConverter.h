// Plugins/Models/X3D/UltraCanvasX3DConverter.h
// X3D (.x3d, .x3dv) and VRML97 (.wrl) reader for ModelStorage::ModelDocument.
//
// X3D is the ISO successor to VRML97, and the XML encoding read here is the
// one every exporter writes. It is a scene format in the same family as
// COLLADA - a node hierarchy with DEF/USE instancing, Phong materials,
// textures, lights, viewpoints and interpolator-driven animation - so most of
// what the document holds transfers directly.
//
// Where it differs from COLLADA is worth stating, because it is what shapes
// this reader:
//
//   * Geometry is a *node*, not a library entry. <IndexedFaceSet> sits inside
//     the <Shape> that draws it, so instancing is DEF/USE on the geometry (or
//     on a whole <Transform> subtree) rather than a url into a library.
//   * <IndexedFaceSet> is the n-gon workhorse: one index stream with -1 ending
//     each face, and optional parallel streams for texture coordinates,
//     normals and colours. Those parallel streams need not agree with the
//     positions, so corners are resolved into document vertices exactly as the
//     OBJ and COLLADA readers do.
//   * The Immersive profile's geometric primitives - Box, Sphere, Cone,
//     Cylinder - are real geometry, not a convenience. A hand-written X3D is
//     usually nothing else, so they are tessellated here rather than skipped.
//   * Animation is ROUTE plumbing: a TimeSensor drives an interpolator, and
//     the interpolator drives one field of one node. That is the same shape as
//     an AnimationChannel plus an AnimationSampler, so it maps across whole.
//
// Read and write. Both of the standard's text encodings, in both directions:
// the XML one (.x3d) and the Classic VRML one (.x3dv), which VRML97 (.wrl)
// also writes. They are two spellings of one node set, so the writer builds the
// scene once and serialises it twice - which encoding comes out is decided by
// the file extension, since that is the only thing a caller has said. When
// reading, the encoding is decided from the file's first line instead, because
// there the content is available and the extension is often wrong. See
// UltraCanvasX3DScene.h.
//
// What the writer emits is the Interchange profile's core: Transform, Shape,
// Appearance, Material, ImageTexture and IndexedFaceSet, which is what every
// X3D and VRML consumer reads. What it does not carry, and says so:
//   * animation - no TimeSensor, interpolator or ROUTE plumbing is written;
//   * cameras and lights;
//   * skinning and morph targets;
//   * the geometric primitives (Box, Sphere, Cone, Cylinder) - those are read
//     and tessellated on the way in, and come back out as the meshes they
//     became rather than as primitives again.
//
// Version: 1.1.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_X3D_CONVERTER_H
#define ULTRACANVAS_X3D_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class X3DConverter : public IModelFormatConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::X3D; }
    std::string GetFormatName() const override { return "X3D"; }
    std::string GetFormatVersion() const override {
        return "X3D 3.0 - 4.0 and VRML97, XML and Classic VRML encodings";
    }
    std::vector<std::string> GetFileExtensions() const override {
        return {".x3d", ".x3dv", ".wrl", ".vrml"};
    }
    std::string GetMimeType() const override { return "model/x3d+xml"; }
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

    // The extension picks the encoding: .x3d writes XML, .x3dv/.wrl/.vrml
    // write Classic VRML. Anything else writes XML.
    bool Export(const ModelStorage::ModelDocument& document,
                const std::string& filename,
                const ConversionOptions& options = ConversionOptions()) override;
    // No file name, so no extension to read: these write the XML encoding.
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

#endif // ULTRACANVAS_X3D_CONVERTER_H
