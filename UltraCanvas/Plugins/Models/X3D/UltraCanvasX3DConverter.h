// Plugins/Models/X3D/UltraCanvasX3DConverter.h
// X3D (.x3d) reader for ModelStorage::ModelDocument.
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
// Reading only. X3D's remaining consumers are archives and the Web3D browsers;
// a caller wanting to write a scene should write glTF. Writing X3D would be a
// second large XML emitter for a format nothing new consumes.
//
// The VRML classic encoding (.wrl, .x3dv) is a different syntax for the same
// node set and is *not* read here - ValidateFile says so rather than pretending.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_X3D_CONVERTER_H
#define ULTRACANVAS_X3D_CONVERTER_H

#include "DataFormats/UltraCanvasModelConverter.h"

namespace UltraCanvas {
namespace ModelConverter {

class X3DConverter : public ImportOnlyConverter {
public:
    ModelFormat GetFormat() const override { return ModelFormat::X3D; }
    std::string GetFormatName() const override { return "X3D"; }
    std::string GetFormatVersion() const override { return "3.0 - 4.0 (XML encoding)"; }
    std::vector<std::string> GetFileExtensions() const override { return {".x3d"}; }
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

    bool ValidateFile(const std::string& filename) const override;
    bool ValidateData(const std::vector<uint8_t>& data) const override;
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_X3D_CONVERTER_H
