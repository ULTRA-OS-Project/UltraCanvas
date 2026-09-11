// Plugins/Models/Blend/UltraCanvasBlendFile.h
// Blender .blend files: the container, and what can honestly be read from one.
//
// A .blend is not an interchange format. It is a dump of Blender's own
// in-memory structures - every allocation written out as a block tagged with
// the address it lived at - made readable by an embedded SDNA block that
// describes every struct and field in the build that wrote it. That is the
// unusual and rather brilliant part: the file explains its own layout, so a
// reader that walks the SDNA rather than hard-coding offsets keeps working
// across versions that move fields around.
//
// So this layer is the container: the header, the block index, the SDNA, and a
// typed view (`Ref`) that reads a field *by name* at the offset the SDNA gives
// it. It knows nothing about meshes. `UltraCanvasBlendConverter.h` is Blender's
// object model on top of it.
//
// ===== What a .blend does not contain =====
//
// The *evaluated* model - the thing you see in the viewport - is not in the
// file. Modifiers are stored unapplied, so the mesh a .blend holds is the cage
// before mirroring, subdivision, bevelling and solidifying. The E-45 aircraft
// sample makes the point exactly: it stores 1147 vertices behind Mirror,
// Subsurf and EdgeSplit, while the same model exported to OBJ - with those
// modifiers applied - is 11749.
//
// That is a reason to *say so*, not a reason to refuse. The Alembic reader
// already returns a SubD control cage with a warning that the subdivided
// surface is not in the file, and this is the same situation: the cage is real
// geometry, it is what the artist modelled, and a caller told what it is can
// decide. Reading it and naming the modifiers that are not applied beats
// returning nothing, which is indistinguishable from a corrupt file.
//
// Version: 2.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BLEND_FILE_H
#define ULTRACANVAS_BLEND_FILE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== THE INSPECTOR =====
//
// What the file holds, without decoding any geometry. Kept because a file
// browser and a properties panel want exactly this and nothing more.

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

    // A sentence a user can act on, naming what the file holds and - when
    // modifiers are unapplied - what the stored geometry is and is not.
    std::string Summary() const;
};

namespace Blend {

// ===== SDNA =====
//
// One field of one struct, with the offset worked out once. `Name` is as the
// SDNA declares it - "*mvert", "co[3]", "obmat[4][4]" - and the parsed form
// sits beside it so callers never take the declaration apart again.
struct Field {
    std::string Type;
    std::string Name;
    std::string BareName;       // "mvert", "co", "obmat"
    size_t Offset = 0;          // within the struct
    size_t Size = 0;            // total, arrays included
    size_t ElementCount = 1;    // 3 for co[3], 16 for obmat[4][4]
    bool IsPointer = false;
};

struct Struct {
    std::string Name;
    size_t Size = 0;
    std::vector<Field> Fields;
    const Field* Find(const std::string& bareName) const;
};

// ===== BLOCKS =====
//
// One allocation as Blender wrote it. `OldAddress` is where it lived in the
// writing process, and is the only thing a pointer field can be resolved
// against - which is why a .blend can be read at all.
struct Block {
    char Code[4] = {0, 0, 0, 0};
    uint64_t OldAddress = 0;
    int StructIndex = -1;
    uint32_t Count = 0;         // how many structs the block holds
    size_t BodyOffset = 0;      // into File::Bytes
    size_t BodySize = 0;

    // Block codes are a fixed four-char field NUL-padded on the right, so the
    // two-character datablock codes arrive as "OB\0\0". Comparing four bytes
    // against a shorter literal would read past the end of that literal, so
    // this walks to the end of whichever string runs out first.
    bool Is(const char* code) const {
        for (int i = 0; i < 4; ++i) {
            const char want = code[i];
            if (Code[i] != want) return false;
            if (want == '\0') return true;
        }
        return true;
    }
};

class File;

// ===== A TYPED VIEW OF ONE STRUCT =====
//
// Every read goes through the SDNA, so nothing here assumes a field's offset
// or even its presence: `Int("totvert", 0)` returns the fallback on a build
// that never had that field. That is what makes one reader work across
// versions that shuffled their structs.
//
// A Ref is a view into the File's bytes and is only valid while it lives.
class Ref {
public:
    Ref() = default;
    Ref(const File* file, const Struct* type, size_t offset)
        : file_(file), type_(type), offset_(offset) {}

    explicit operator bool() const { return file_ != nullptr && type_ != nullptr; }
    const Struct* Type() const { return type_; }
    size_t Offset() const { return offset_; }

    bool Has(const std::string& field) const { return type_ && type_->Find(field); }

    // Scalars. Integer and floating fields each convert to both, because a
    // caller asking for a count does not want to know whether the build stored
    // it as short or int.
    int64_t Int(const std::string& field, int64_t fallback = 0) const;
    double Real(const std::string& field, double fallback = 0.0) const;

    // An array field: up to `count` elements into `out`. Returns how many were
    // actually read, which is 0 when the field is absent.
    size_t Reals(const std::string& field, double* out, size_t count) const;
    size_t Ints(const std::string& field, int64_t* out, size_t count) const;

    // A fixed char[] field, trimmed at its first NUL.
    std::string Text(const std::string& field) const;

    // The address a pointer field holds, and the block it points at.
    uint64_t Pointer(const std::string& field) const;
    const Block* PointedBlock(const std::string& field) const;

    // A struct field held *inside* this one by value, such as Mesh.vdata.
    Ref Inner(const std::string& field) const;

private:
    const File* file_ = nullptr;
    const Struct* type_ = nullptr;
    size_t offset_ = 0;
};

// ===== THE FILE =====

class File {
public:
    bool Valid = false;
    std::string Error;

    std::string Version;        // "278", "402" - three digits, as the header has it
    int PointerSize = 8;
    bool BigEndian = false;
    std::string Compression;    // "none", "gzip", "zstd"

    std::vector<uint8_t> Bytes;     // decompressed
    std::vector<Block> Blocks;
    std::vector<Struct> Structs;

    const Struct* StructAt(int index) const {
        return index >= 0 && static_cast<size_t>(index) < Structs.size() ? &Structs[index]
                                                                        : nullptr;
    }
    const Struct* StructNamed(const std::string& name) const;

    // The block an old address falls in. Blender writes each allocation whole,
    // so an exact hit is the common case; a pointer into the middle of one is
    // resolved to its containing block, which is what a field pointing at an
    // element of an array needs.
    const Block* BlockAt(uint64_t address) const;

    // The i-th struct in a block, as a typed view. Out of range gives a Ref
    // that converts to false rather than a view of whatever follows.
    Ref At(const Block& block, size_t index = 0) const;

    // Every block of a given two-character datablock code: "OB", "ME", "MA".
    std::vector<const Block*> BlocksOfCode(const char* code) const;

    // Reads at an absolute byte offset, honouring the file's endianness.
    bool ReadBytes(size_t offset, void* out, size_t size) const;
    uint64_t ReadPointer(size_t offset) const;
    double ReadNumber(size_t offset, const std::string& type) const;
    int64_t ReadInteger(size_t offset, const std::string& type) const;

    // The version as a comparable number: "278" -> 278, "402" -> 402.
    int VersionNumber() const;
};

// Reads the header, the block index and the SDNA. `warn` may be null. Geometry
// is not touched: this is the container and nothing else.
bool Parse(const std::vector<uint8_t>& data, File& out,
           const std::function<void(const std::string&)>& warn = nullptr);

} // namespace Blend

// ===== CONVENIENCE =====

// Reads the header, block index and SDNA and reports what is in the file.
// Never decodes mesh data. `warn` may be null.
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
