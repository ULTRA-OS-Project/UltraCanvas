// Plugins/Models/Blend/UltraCanvasBlendFile.h
// Blender .blend files: what is in one, and why it is not imported as geometry.
//
// A .blend is not an interchange format. It is a dump of Blender's own
// in-memory structures, self-describing through an embedded SDNA block, and
// the *evaluated* model — the thing you see in the viewport — is not in it.
// Modifiers are stored unapplied, so the mesh a .blend holds is the cage
// before mirroring, subdivision, bevelling and solidifying, and reconstructing
// what the artist saw would mean reimplementing Blender's modifier stack.
//
// The E-45 aircraft sample makes the point exactly: the .blend stores 1147
// vertices behind Mirror, Subsurf and EdgeSplit modifiers, while the same
// model exported to OBJ — with those modifiers applied — is 11749. Ninety
// percent of the aircraft is not in the file.
//
// So this is an inspector, not a converter. It answers "what is in this
// file?" from the parts of the format that have been stable for many
// releases — the header, the block index and the SDNA — without decoding any
// version-specific struct semantics. ModelConverter::BlendConverter puts that
// answer behind the converter interface, where it declines to import and says
// why.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BLEND_FILE_H
#define ULTRACANVAS_BLEND_FILE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {

struct BlendFileInfo {
    bool Valid = false;
    std::string Error;              // why not, when Valid is false

    std::string Version;            // "2.78", "4.2", ... as the header spells it
    int PointerSize = 0;            // 4 or 8
    bool BigEndian = false;
    std::string Compression;        // "none", "gzip" or "zstd"

    size_t BlockCount = 0;
    // How many of each SDNA struct the file stores, e.g. "Object" -> 5.
    std::map<std::string, size_t> StructCounts;

    std::vector<std::string> ObjectNames;
    std::vector<std::string> MeshNames;
    std::vector<std::string> MaterialNames;
    // Modifier types present anywhere in the file, without the "ModifierData"
    // suffix: "Mirror", "Subsurf", "Bevel", ...
    std::vector<std::string> Modifiers;

    // The geometry actually stored, which is the pre-modifier cage.
    size_t StoredVertexCount = 0;
    size_t StoredPolygonCount = 0;

    bool HasUnappliedModifiers() const { return !Modifiers.empty(); }

    // A sentence a user can act on, naming what the file holds and — when
    // modifiers are unapplied — why importing its geometry would be wrong.
    std::string Summary() const;
};

// Reads the header, block index and SDNA. Never decodes mesh data. `warn` may
// be null.
BlendFileInfo ReadBlendFileInfo(const std::string& filePath,
                                const std::function<void(const std::string&)>& warn = nullptr);

// The same, for bytes already in memory.
BlendFileInfo ReadBlendFileInfo(const std::vector<uint8_t>& data,
                                const std::function<void(const std::string&)>& warn = nullptr);

// Recognises a .blend by signature: the "BLENDER" magic, or the gzip/zstd
// wrapper Blender writes when the file is saved compressed.
bool LooksLikeBlendFile(const std::vector<uint8_t>& head);

} // namespace UltraCanvas

#endif // ULTRACANVAS_BLEND_FILE_H
