// Plugins/Models/Blend/UltraCanvasBlendConverter.h
// Blender .blend: the geometry the file actually holds, and a warning naming
// what it does not.
//
// A .blend stores modifiers *unapplied*, so its mesh is the cage before
// mirroring, subdivision and bevelling. The E-45 aircraft sample is 1147
// vertices here and 11749 in the OBJ exported from it with those modifiers
// applied. That is a reason to say so, not a reason to refuse: the cage is
// real geometry - it is what the artist modelled and what they would edit -
// and a caller told which modifiers are missing can decide. It is the same
// answer the Alembic reader gives a SubD control cage.
//
// What is read, all of it driven by the file's own SDNA rather than by
// hard-coded offsets, so a build that moved a field still reads:
//
//   * the object hierarchy, each node's local transform divided out of the
//     world matrix Blender evaluated - which is exact, and avoids
//     reimplementing parentinv, bone parents and vertex parents;
//   * meshes in both layouts Blender has used: MVert/MPoly/MLoop through 3.x,
//     and the named CustomData attribute layers of 3.6 and later;
//   * n-gons kept as n-gons, per-corner UVs and per-corner vertex colours -
//     with corners whose streams differ split into distinct vertices, as every
//     other reader here does;
//   * one primitive per material slot actually used, and Blender's material
//     fields as a Phong material;
//   * Scene.unit.scale_length, and Z-up, which Blender is by definition.
//
// Not read, each for a reason: armature deform and shape keys (the cage is
// stored, the deformation is not), actions and drivers, cameras and lights,
// and image textures - an image reaches a material through a node tree, and
// this reader does not evaluate node trees. `FormatCapabilities` says so
// rather than implying otherwise. Read-only: writing a .blend would mean
// writing Blender's structs for one specific build.
//
// Version: 2.0.0
// Last Modified: 2026-09-11
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
    // What this converter does, not what the format could hold in principle:
    // no animation, no skinning, no textures. See the .cpp for each reason.
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

    // What the file holds — the useful half of this converter, for a file
    // browser or a properties panel.
    static BlendFileInfo Inspect(const std::string& filename);
};

} // namespace ModelConverter
} // namespace UltraCanvas

#endif // ULTRACANVAS_BLEND_CONVERTER_H
